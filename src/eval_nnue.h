#pragma once
#include "gamestate.h"
#include "rand.h"
#include <vector>
#include <cstdint>
#include <string>

// NNUE-style evaluator

struct NNUEWeights {
    // 2 hidden layers: h1=64, h2=32 (por defeito)
    std::vector<float> w1; // [h1][input]
    std::vector<float> b1; // [h1]
    std::vector<float> w2; // [h2][h1]
    std::vector<float> b2; // [h2]
    std::vector<float> w3; // [1][h2]
    float b3 = 0.0f;       // [1]

    int inputSize = 0;
    int hidden1 = 64;
    int hidden2 = 32;
};

// Agora o extractFeatures gera 178 floats:
// 0..39   minhas cartas
// 40..79  cartas opp (se perfectInfo)
// 80..119 trick atual
// 120     minha pontuação /120
// 121     pontuação opp /120
// 122     deck.size()/40
// 123..126 one-hot naipe trunfo
// 127..166 cartas visíveis/conhecidas
// 167     trumpCardGiven flag
// 168..177 one-hot rank da carta de trunfo inicial
//
// Nota: redes antigas (127 inputs) já não são compatíveis.
//
std::vector<float> extractFeatures(const GameState& st,
                                   int player,
                                   bool perfectInfo);

// Inicializa pesos random
void initRandomWeights(NNUEWeights& w, int inputSize, RNG& rng);

// Avalia uma posição do ponto de vista de `player`.
float nnueEvaluate(const NNUEWeights& w,
                   const GameState& st,
                   int player,
                   bool perfectInfo);

// guardar/carregar pesos
bool saveWeights(const NNUEWeights& w, const std::string& path);
bool loadWeights(NNUEWeights& w, const std::string& path);
