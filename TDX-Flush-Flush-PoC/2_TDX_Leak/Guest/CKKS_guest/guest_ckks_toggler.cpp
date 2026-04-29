// guest_ckks_toggler.cpp
// Simulates the linear memory access pattern of CKKS decryption (NTT / element-wise mult)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>
#include <sched.h>
#include <signal.h>
#include <time.h>

static void pin_to_core(int core){
    cpu_set_t set; CPU_ZERO(&set); CPU_SET(core, &set);
    sched_setaffinity(0, sizeof(set), &set);
}

static std::string find_ivshmem_devdir() {
    const char* sys = "/sys/bus/pci/devices";
    DIR* d = opendir(sys);
    if(!d) return {};
    dirent* e;
    while((e = readdir(d))){
        if(e->d_name[0]=='.') continue;
        std::string base = std::string(sys) + "/" + e->d_name;
        auto read_hex = [&](const char* fn)->unsigned{
            std::string p = base + "/" + fn;
            FILE* f = fopen(p.c_str(), "r");
            if(!f) return 0;
            unsigned v=0; if(fscanf(f, "0x%x", &v)!=1) v=0;
            fclose(f); return v;
        };
        if(read_hex("vendor")==0x1af4 && read_hex("device")==0x1110){
            closedir(d); return base;
        }
    }
    closedir(d); return {};
}

static std::string pick_biggest_bar(const std::string& devdir){
    FILE* f = fopen((devdir + "/resource").c_str(), "r");
    if(!f) return {};
    size_t best_sz = 0; std::string best_path;
    char line[256]; int idx=0;
    while(fgets(line, sizeof(line), f)){
        unsigned long long start=0,end=0,flags=0;
        if(sscanf(line, "%llx %llx %llx", &start, &end, &flags)==3){
            size_t sz = (end>=start)? (size_t)(end-start+1) : 0;
            if(sz > best_sz){ best_sz = sz; best_path = devdir + "/resource" + std::to_string(idx); }
        }
        idx++;
    }
    fclose(f); return best_path;
}

static inline uint64_t ns_now(){
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec*1000000000ull + ts.tv_nsec;
}

static inline void sleep_until_ns(uint64_t t_ns){
    timespec ts; ts.tv_sec = t_ns / 1000000000ull; ts.tv_nsec = t_ns % 1000000000ull;
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
}

static volatile uint8_t* g_label = nullptr;
static void* g_map   = nullptr;
static size_t g_mapsz = 0;

static void cleanup_and_exit(int){
    if(g_label) *g_label = 0;
    if(g_map && g_mapsz) munmap(g_map, g_mapsz);
    _exit(0);
}

int main(int argc, char** argv){
    int core=2, warmup_ms=1000, active_ms=2000, idle_ms=2000, reps=15;
    size_t label_off=64, work_off=0x1000;
    size_t poly_degree = 8192; // Standard CKKS ring dimension

    for(int i=1;i<argc;i++){
        auto need=[&](const char* k){ return i+1<argc && std::string(argv[i])==k; };
        if(need("--core")) core = std::atoi(argv[++i]);
        else if(need("--warmup_ms")) warmup_ms = std::atoi(argv[++i]);
        else if(need("--active_ms")) active_ms = std::atoi(argv[++i]);
        else if(need("--idle_ms")) idle_ms = std::atoi(argv[++i]);
        else if(need("--reps")) reps = std::atoi(argv[++i]);
        else if(need("--poly_degree")) poly_degree = (size_t)std::atoll(argv[++i]);
    }

    signal(SIGINT,  cleanup_and_exit);
    signal(SIGTERM, cleanup_and_exit);
    pin_to_core(core);

    std::string devdir = find_ivshmem_devdir();
    if(devdir.empty()) return 1;
    std::string respath = pick_biggest_bar(devdir);
    if(respath.empty()) return 1;

    int fd = ::open(respath.c_str(), O_RDWR | O_SYNC);
    if(fd<0) return 1;
    g_mapsz = 2 * 1024 * 1024; // 2MB
    g_map = mmap(nullptr, g_mapsz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    g_label = (volatile uint8_t*)((uint8_t*)g_map + label_off);
    
    // Treat the shared memory as an array of 64-bit integers (ciphertexts/keys)
    volatile uint64_t* poly_array = (volatile uint64_t*)((uint8_t*)g_map + work_off);

    printf("[guest] Warmup %d ms\n", warmup_ms);
    sleep_until_ns(ns_now() + (uint64_t)warmup_ms*1000000ull);

    volatile uint64_t dummy_accumulator = 0;

    for(int r=0; r<reps; r++){
        *g_label = 1; // Mark Active
        uint64_t tend = ns_now() + (uint64_t)active_ms*1000000ull;

        // CKKS LINEAR SWEEP: Iterate over the polynomial array
        // We do this continuously to simulate heavy decryption load
        while(ns_now() < tend){
            for(size_t i = 0; i < poly_degree; i++) {
                // Simulating c1[i] * s[i]
                dummy_accumulator ^= poly_array[i]; 
            }
        }

        *g_label = 0; // Mark Idle
        sleep_until_ns(ns_now() + (uint64_t)idle_ms*1000000ull);
        printf("[guest] rep %d/%d done\n", r+1, reps);
    }

    *g_label = 0;
    munmap(g_map, g_mapsz);
    printf("[guest] done\n");
    return 0;
}
