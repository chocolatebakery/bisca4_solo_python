#pragma once
#include <vector>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <atomic>
#include <thread>
#include <algorithm>
#include <mutex>

#include "gamestate.h"
#include "eval_nnue.h"
#include "rand.h"

// ======================================================
// SearchResult: resultado de pensar um lance na root
// ======================================================

struct SearchResult {
    float eval;
    int chosenMoveIndex; // índice NA MÃO do jogador root a jogar agora
};

// ======================================================
// Transposition Table (TT)
// Guardamos evals já calculados para (hash, depth)
// ======================================================

enum class TTFlag : uint8_t {
    EXACT,
    LOWERBOUND,
    UPPERBOUND
};

struct TTEntry {
    float value;
    int depth;
    TTFlag flag;
    // também podemos guardar bestMove para move ordering
    int bestMoveHandIdx;
};

// TT global simples (podes trocar para array power-of-two depois)
extern std::unordered_map<uint64_t, TTEntry> g_TT;
extern std::mutex g_TTMutex;

// ======================================================
// Funções auxiliares expostas
// ======================================================

// Aplica jogada e devolve novo estado (cópia + playCard + maybeCloseTrick)
GameState applyMove(const GameState& st, int player, int handIndex);

// Busca recursiva alpha-beta com:
// - move ordering
// - quiescence light em depth==0
// - transposition table
float searchRecursiveAB(const GameState& st,
                        const NNUEWeights& w,
                        int rootPlayer,
                        int depth,
                        float alpha,
                        float beta,
                        bool perfectInfo);

// Wrapper single-thread: devolve melhor lance + eval
SearchResult searchBestMove(const GameState& st,
                            const NNUEWeights& w,
                            int depth,
                            bool perfectInfo);

// Wrapper multi-thread (paraleliza cada lance da root)
SearchResult searchBestMoveMT(const GameState& st,
                              const NNUEWeights& w,
                              int depth,
                              bool perfectInfo);

// Iterative deepening + aspiration windows na root
SearchResult searchBestMoveID(const GameState& st,
                              const NNUEWeights& w,
                              int depth,
                              bool perfectInfo);

// ======================================================
// Helpers internos mas precisamos declarar porque o self-play
// também os usa às vezes
// ======================================================

// uma avaliação rápida (sem search) usada para ordenar jogadas
inline float quickEval(const GameState& st,
                       const NNUEWeights& w,
                       int rootPlayer,
                       bool perfectInfo)
{
    return nnueEvaluate(w, st, rootPlayer, perfectInfo);
}

// mini-quiescence "estabilizar depois da vaza"
// se depth==0 mas a mesa acabou de limpar, olha 1 ply
float quiescenceAfterTrickClear(const GameState& st,
                                const NNUEWeights& w,
                                int rootPlayer,
                                bool perfectInfo);

// tenta obter da TT; devolve true se encontrou entrada utilizável
bool ttLookup(uint64_t key, int depth,
              float alpha, float beta,
              float& outVal);

// grava na TT
void ttStore(uint64_t key,
             int depth,
             float val,
             float alphaOrig,
             float betaOrig,
             int bestMoveHandIdx);

// ======================================================
