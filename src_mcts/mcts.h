#pragma once

#include "gamestate.h"
#include "rand.h"
#include "eval_nnue.h"
#include <optional>

struct MCTSConfig {
    int iterations = 2000;
    float exploration = 1.41421356f;
    int rolloutLimit = 0; // 0 = play to end
    const NNUEWeights* weights = nullptr;
    bool useNNUE = false;
    bool perfectInfo = false;
};

struct MCTSResult {
    float eval = 0.0f;
    int chosenMoveIndex = -1;
    int visits = 0;
};

// Executa uma pesquisa Monte Carlo Tree Search para o estado atual.
// Retorna o índice da carta a jogar (de acordo com GameState::getLegalMoves).
MCTSResult searchBestMoveMCTS(const GameState& state,
                              int rootPlayer,
                              RNG& rng,
                              const MCTSConfig& cfg);
