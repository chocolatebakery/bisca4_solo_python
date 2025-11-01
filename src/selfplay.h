#pragma once
#include "gamestate.h"
#include "search.h"
#include <string>
#include <vector>

struct SelfPlaySample {
    std::vector<float> features;
    float outcome;
};

// Joga um jogo completo p0 vs p1 usando searchBestMove(depth)
// perfectInfo controla se os jogadores "vêem" as mãos um do outro
std::vector<SelfPlaySample> playSelfPlayGame(const NNUEWeights& w,
                                             int depth,
                                             RNG& rng,
                                             bool perfectInfo);

bool saveSamples(const std::vector<SelfPlaySample>& samples,
                 const std::string& path);
