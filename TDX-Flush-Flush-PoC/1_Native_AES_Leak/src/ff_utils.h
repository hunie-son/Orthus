#ifndef FF_UTILS_H
#define FF_UTILS_H

#include <stdint.h>
#include <x86intrin.h>

// Fence + RDTSCP
static inline uint64_t rdtscp_barrier() {
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

#endif

