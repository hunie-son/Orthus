// guest_te0_ff_worker.cpp
// Guest-side Victim: Copies Te0 to Shared Memory and uses it.
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <dirent.h>
#include <sched.h>
#include <signal.h>
#include <elf.h>
#include <openssl/aes.h>

// ----------------------------------------------------------------------
// Utils & Setup
// ----------------------------------------------------------------------

static void pin_to_core(int core) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    (void)sched_setaffinity(0, sizeof(set), &set);
}

static inline void cpu_relax() { asm volatile("pause" ::: "memory"); }

struct Bar {
    std::string devdir, respath;
    size_t size;
};

// Locate IVSHMEM device
static std::string find_ivshmem_devdir() {
    const char* sys = "/sys/bus/pci/devices";
    DIR* d = opendir(sys);
    if (!d) return {};
    dirent* e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        std::string base = std::string(sys) + "/" + e->d_name;
        auto read_hex = [&](const char* fn) -> unsigned {
            std::string p = base + "/" + fn;
            FILE* f = fopen(p.c_str(), "r");
            if (!f) return 0;
            unsigned v = 0;
            if (fscanf(f, "0x%x", &v) != 1) v = 0;
            fclose(f);
            return v;
        };
        unsigned ven = read_hex("vendor");
        unsigned dev = read_hex("device");
        if (ven == 0x1af4 && dev == 0x1110) { closedir(d); return base; }
    }
    closedir(d);
    return {};
}

static Bar pick_biggest_bar(const std::string& devdir) {
    FILE* f = fopen((devdir + "/resource").c_str(), "r");
    if (!f) return {};
    Bar best; best.size = 0;
    char line[256];
    int idx = 0;
    while (fgets(line, sizeof(line), f)) {
        unsigned long long start = 0, end = 0, flags = 0;
        if (sscanf(line, "%llx %llx %llx", &start, &end, &flags) == 3) {
            size_t sz = (end >= start) ? (size_t)(end - start + 1) : 0;
            if (sz > best.size) {
                best.size = sz;
                best.respath = devdir + "/resource" + std::to_string(idx);
            }
        }
        idx++;
    }
    fclose(f);
    return best;
}

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

// Map real Te0 from libcrypto so we can copy the DATA
static bool elf_vaddr_to_off_64(const uint8_t* file, size_t file_sz, uint64_t vaddr, uint64_t& out_off) {
    if (file_sz < sizeof(Elf64_Ehdr)) return false;
    const Elf64_Ehdr* eh = (const Elf64_Ehdr*)file;
    if (eh->e_ident[EI_CLASS] != ELFCLASS64) return false;
    const Elf64_Phdr* ph = (const Elf64_Phdr*)(file + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;
        uint64_t v0 = ph[i].p_vaddr;
        uint64_t v1 = ph[i].p_vaddr + ph[i].p_memsz;
        if (vaddr >= v0 && vaddr < v1) {
            uint64_t off = ph[i].p_offset + (vaddr - v0);
            if (off < file_sz) { out_off = off; return true; }
        }
    }
    return false;
}

static const uint32_t* map_real_te0(const char* libcrypto_path, uint64_t te0_vaddr, size_t te0_bytes, void** out_map, size_t* out_sz) {
    int fd = open(libcrypto_path, O_RDONLY);
    if (fd < 0) return nullptr;
    off_t sz = lseek(fd, 0, SEEK_END);
    uint8_t* file = (uint8_t*)mmap(nullptr, (size_t)sz, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (file == MAP_FAILED) return nullptr;
    uint64_t off = 0;
    if (!elf_vaddr_to_off_64(file, (size_t)sz, te0_vaddr, off)) { munmap(file, sz); return nullptr; }
    if (out_map) *out_map = file;
    if (out_sz) *out_sz = (size_t)sz;
    return (const uint32_t*)(file + off);
}

// ----------------------------------------------------------------------
// Victim Lookup Function
// ----------------------------------------------------------------------

// Accesses the SHARED copy of Te0
static inline void victim_access_shared(uint8_t pt_line, volatile uint32_t* Shared_Te0, volatile uint32_t* acc) {
    // We want to access a random index within the specific cache line requested by pt_line
    uint8_t idx_in_line = (uint8_t)(rand() & 0x0F); // 0..15
    uint32_t idx = (uint32_t)pt_line * 16u + (uint32_t)idx_in_line;
    
    // Read from IVSHMEM
    uint32_t v = Shared_Te0[idx];
    *acc ^= v;
}

// ----------------------------------------------------------------------
// Main
// ----------------------------------------------------------------------

static void* g_map = nullptr;
static size_t g_mapsz = 0;
static Ctrl* g_ctrl = nullptr;

void cleanup(int) {
    if (g_ctrl) g_ctrl->active = 0;
    if (g_map) munmap(g_map, g_mapsz);
    _exit(0);
}

int main(int argc, char** argv) {
    int core = 2;
    size_t ctrl_off = 64;
    size_t te0_shm_off = 0x2000; // Must match host
    size_t te0_bytes = 1024;

    const char* libcrypto_path = "/root/openssl-1.1.1w/libcrypto.so.1.1";
    uint64_t te0_vaddr = 0x1c6d40; // From objdump
				   // 1c6d40

    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--core") core = std::atoi(argv[++i]);
        // add parsers...
    }

    signal(SIGINT, cleanup);
    pin_to_core(core);
    srand(0x12345678);

    // 1. Map IVSHMEM
    std::string devdir = find_ivshmem_devdir();
    if (devdir.empty()) return 1;
    Bar bar = pick_biggest_bar(devdir);
    if (bar.respath.empty()) return 1;

    int fd = open(bar.respath.c_str(), O_RDWR | O_SYNC);
    g_mapsz = bar.size;
    g_map = mmap(nullptr, g_mapsz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    Ctrl* ctrl = (Ctrl*)((uint8_t*)g_map + ctrl_off);
    g_ctrl = ctrl;
    
    // Pointer to where we will store the Fake Te0
    volatile uint32_t* Shared_Te0 = (volatile uint32_t*)((uint8_t*)g_map + te0_shm_off);

    // 2. Get Real Te0 Data and Copy to Shared Memory
    void* lib_map = nullptr; 
    size_t lib_sz = 0;
    const uint32_t* Real_Te0 = map_real_te0(libcrypto_path, te0_vaddr, te0_bytes, &lib_map, &lib_sz);
    if (!Real_Te0) {
        fprintf(stderr, "[guest] Failed to map libcrypto Te0\n");
        return 1;
    }

    // COPY! This makes the attack possible.
    printf("[guest] Copying Te0 to Shared Memory Offset 0x%zx\n", te0_shm_off);
    std::memcpy((void*)Shared_Te0, (const void*)Real_Te0, te0_bytes);
    
    // We can unmap the real lib now, we use the shared copy
    munmap(lib_map, lib_sz);

    // 3. Setup AES dummy vars
    AES_KEY aes_key;
    uint8_t k[16]; memset(k,0,16); AES_set_encrypt_key(k, 128, &aes_key);
    uint8_t in[16], out[16]; memset(in,0,16);
    volatile uint32_t acc = 0;

    // 4. Handshake Loop
    ctrl->active = 0; ctrl->ack = 0; ctrl->finished = 0;
    __sync_synchronize();
    
    printf("[guest] Waiting host ready...\n");
    while(!ctrl->done && ctrl->ready != 1) cpu_relax();
    
    printf("[guest] Waiting token...\n");
    while(!ctrl->done && ctrl->token == 0) cpu_relax();
    
    uint32_t last_token = ctrl->token;
    ctrl->ack = last_token;
    __sync_synchronize();
    printf("[guest] Connected.\n");

    // 5. Work Loop
    while (!ctrl->done) {
        uint32_t t = ctrl->token;
        if (t == 0 || t == last_token) { cpu_relax(); continue; }

        uint8_t pt = ctrl->pt;
        uint8_t pt_line = pt / 16;
        uint32_t iters = ctrl->iters;
        
        // Ack that we saw the new work item
        ctrl->ack = t;
        __sync_synchronize();

        // Victim execution
        for (uint32_t i = 0; i < iters; i++) {
            // Check if host updated token mid-loop (to stop us early)
            if (ctrl->token != t) break;

            // Do some AES work to simulate load
            in[0] = pt; 
            AES_encrypt(in, out, &aes_key);
            
            // Perform the Lookup on the SHARED Table
            // Host is Flush+Flushing this address
            victim_access_shared(pt_line, Shared_Te0, &acc);
        }

        ctrl->finished = t;
        __sync_synchronize();
        last_token = t;
    }

    cleanup(0);
    return 0;
}
