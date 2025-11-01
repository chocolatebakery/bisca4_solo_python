#pragma once
#include <cstdint>

// Pequeno PRNG para termos random determinístico
// Xoshiro / splitmix-style simplificado

class RNG {
public:
    explicit RNG(uint64_t seed = 0x123456789abcdefULL);
    uint64_t nextU64();
    uint32_t nextU32();
    double nextDouble01(); // [0,1)

private:
    uint64_t s;
};
