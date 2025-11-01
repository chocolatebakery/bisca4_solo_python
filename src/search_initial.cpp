#include "search.h"
#include <algorithm>

GameState applyMove(const GameState& st, int player, int handIndex) {
    GameState ns = st; // copia
    // precisamos de RNG para maybeCloseTrick... mas isso só é necessário
    // quando trick fecha e vai haver compra.
    // Na busca nós não queremos aleatório: vamos passar um RNG fixo interno.

    RNG rng(1234);

    ns.playCard(player, handIndex);
    ns.maybeCloseTrick(rng);

    return ns;
}

float searchRecursive(const GameState& st,
                      const NNUEWeights& w,
                      int rootPlayer,
                      int depth)
{
    if (st.finished || depth == 0) {
        return nnueEvaluate(w, st, rootPlayer);
    }

    int p = st.currentPlayer;
    auto moves = st.getLegalMoves(p);

    if (moves.empty()) {
        // sem jogadas? então avalia
        return nnueEvaluate(w, st, rootPlayer);
    }

    // jogador rootPlayer quer maximizar a própria avaliação
    // o outro quer minimizar
    float bestVal = (p == rootPlayer)
        ? -std::numeric_limits<float>::infinity()
        :  std::numeric_limits<float>::infinity();

    for (int m : moves) {
        GameState ns = applyMove(st, p, m);
        float val = searchRecursive(ns, w, rootPlayer, depth - 1);

        if (p == rootPlayer) {
            if (val > bestVal) bestVal = val;
        } else {
            if (val < bestVal) bestVal = val;
        }
    }

    return bestVal;
}

SearchResult searchBestMove(const GameState& st,
                            const NNUEWeights& w,
                            int depth)
{
    SearchResult res;
    res.eval = -std::numeric_limits<float>::infinity();
    res.chosenMoveIndex = -1;

    int p = st.currentPlayer;
    auto moves = st.getLegalMoves(p);
    if (moves.empty()) {
        res.eval = nnueEvaluate(w, st, p);
        res.chosenMoveIndex = -1;
        return res;
    }

    float bestVal = -std::numeric_limits<float>::infinity();
    int bestMove = moves[0];

    for (int m : moves) {
        GameState ns = applyMove(st, p, m);
        float val = searchRecursive(ns, w, p, depth - 1);
        if (val > bestVal) {
            bestVal = val;
            bestMove = m;
        }
    }

    res.eval = bestVal;
    res.chosenMoveIndex = bestMove;
    return res;
}
