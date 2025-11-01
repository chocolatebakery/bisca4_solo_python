#pragma once
#include "gamestate.h"
#include "eval_nnue.h"
#include <limits>

// Função de busca estilo minimax com profundidade fixa.
// Sem alpha-beta por enquanto, para ficar simples.

struct SearchResult {
    float eval;
    int chosenMoveIndex; // índice na mão do jogador root
};

// Faz uma cópia do estado, aplica jogada, fecha trick se aplicável
// e devolve.
GameState applyMove(const GameState& st, int player, int handIndex);

// minimax
float searchRecursive(const GameState& st,
                      const NNUEWeights& w,
                      int rootPlayer,
                      int depth);

// wrapper que escolhe a melhor carta para o jogador atual
SearchResult searchBestMove(const GameState& st,
                            const NNUEWeights& w,
                            int depth);
