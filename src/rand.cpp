#include "rand.h"

// linear congruential-ish just to get going.
// Isto não precisa ser criptograficamente bom.

RNG::RNG(uint64_t seed) : s(seed ? seed : 0xdeadbeefcafebabeULL) {}

uint64_t RNG::nextU64() {
    // LCG 64-bit
    s = s * 6364136223846793005ULL + 1ULL;
    return s;
}

uint32_t RNG::nextU32() {
    return (uint32_t)(nextU64() >> 32);
}

double RNG::nextDouble01() {
    // divide por 2^53 para ter ~[0,1)
    return (nextU64() & ((1ULL<<53)-1)) / double(1ULL<<53);
}
