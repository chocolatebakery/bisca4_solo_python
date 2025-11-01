#pragma once
#include "gamestate.h"
#include "mcts.h"
#include <string>
#include <vector>

struct SelfPlaySampleMCTS {
    std::vector<float> features;
    float outcome;
};

std::vector<SelfPlaySampleMCTS> playSelfPlayGameMCTS(const MCTSConfig& cfg,
                                                     RNG& rng);

bool saveSamplesMCTS(const std::vector<SelfPlaySampleMCTS>& samples,
                     const std::string& path);
