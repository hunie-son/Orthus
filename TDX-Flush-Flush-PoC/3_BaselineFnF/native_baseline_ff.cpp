#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <string>
#include <sched.h>
#include <x86intrin.h>

// 1. Pin process to a specific core to minimize scheduling noise
static void pin_to_core(int core) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    (void)sched_setaffinity(0, sizeof(set), &set);
}

// 2. Accurate cycle measurement for clflush instruction
static inline uint64_t flush_cycles(volatile void *p) {
    unsigned aux;
    _mm_mfence();                     // Serialize pipeline
    uint64_t t1 = __rdtscp(&aux);     // Start timestamp
    _mm_clflush((const void*)p);      // Flush target address
    _mm_mfence();                     // Ensure clflush completion
    uint64_t t2 = __rdtscp(&aux);     // End timestamp
    return t2 - t1;
}

int main(int argc, char** argv) {
    int core = 2;
    int samples = 500000; 

    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--core" && i + 1 < argc) core = std::atoi(argv[++i]);
        if (std::string(argv[i]) == "--samples" && i + 1 < argc) samples = std::atoi(argv[++i]);
    }

    pin_to_core(core);
    printf("[native_baseline] Pinned to core %d, Samples: %d\n", core, samples);

    // Allocate target memory buffer in native space
    static uint8_t buffer[4096 * 4];
    volatile uint8_t* target = &buffer[2 * 4096];

    std::vector<uint64_t> hits(samples);
    std::vector<uint64_t> misses(samples);

    // Warm-up to initialize memory paging
    for (int i = 0; i < 1000; i++) {
        (void)*target;
    }

    // ---------------------------------------------------------
    // [Phase 1] Miss (Uncached) Measurement
    // Expected to be FAST because the line is already evicted.
    // ---------------------------------------------------------
    for (int i = 0; i < samples; i++) {
        // Initial eviction to ensure 'Uncached' state
        _mm_clflush((const void*)target);
        
        // CRITICAL: Delay to prevent hardware pipeline stalls.
        // This simulates the time gap between victim's action and spy's probe.
        for (volatile int d = 0; d < 500; d++); 

        // Measure clflush on an empty cache line
        misses[i] = flush_cycles(target);
    }

    // ---------------------------------------------------------
    // [Phase 2] Hit (Cached) Measurement
    // Expected to be SLOW because CPU must perform an actual eviction.
    // ---------------------------------------------------------
    for (int i = 0; i < samples; i++) {
        // Ensure clean state before loading
        _mm_clflush((const void*)target);
        
        // Victim simulation: Load target into cache
        (void)*target;
        _mm_mfence(); 
        
        // Measure clflush on a cached line
        hits[i] = flush_cycles(target);
    }

    // Save raw cycles for histogram plotting
    FILE* f = fopen("native_baseline_ff.csv", "w");
    if (!f) { perror("fopen"); return 1; }
    
    fprintf(f, "hit,miss\n");
    for (int i = 0; i < samples; i++) {
        fprintf(f, "%lu,%lu\n", hits[i], misses[i]);
    }
    fclose(f);

    printf("[native_baseline] Saved results to native_baseline_ff.csv\n");
    return 0;
}
