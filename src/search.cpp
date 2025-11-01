#include "search.h"
#include <algorithm>
#include <future>
#include <numeric>

// TT storage
std::unordered_map<uint64_t, TTEntry> g_TT;
std::mutex g_TTMutex;
static constexpr size_t MAX_TT_SIZE = 1'000'000; // cap simples para evitar crescer demais

static inline void hashCombine(uint64_t& h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
}

static uint64_t computeHash(const GameState& st) {
    uint64_t h = 0xCAFEBABE12345678ULL;
    // current player, scores
    hashCombine(h, static_cast<uint64_t>(st.currentPlayer));
    hashCombine(h, static_cast<uint64_t>(st.score[0] & 0xFFFF));
    hashCombine(h, static_cast<uint64_t>(st.score[1] & 0xFFFF));

    // trump
    hashCombine(h, static_cast<uint64_t>(static_cast<int>(st.trumpSuit)));
    hashCombine(h, static_cast<uint64_t>((static_cast<int>(st.trumpCard.suit) << 8) |
                                        static_cast<int>(st.trumpCard.rank)));

    // deck order
    for (const auto& c : st.deck) {
        uint64_t x = (static_cast<uint64_t>(static_cast<int>(c.suit)) << 8) |
                     static_cast<uint64_t>(static_cast<int>(c.rank));
        hashCombine(h, x + 0x1111111111111111ULL);
    }

    // hands
    for (int p = 0; p < 2; ++p) {
        for (const auto& c : st.hands[p]) {
            uint64_t x = (static_cast<uint64_t>(static_cast<int>(c.suit)) << 8) |
                         static_cast<uint64_t>(static_cast<int>(c.rank));
            hashCombine(h, x + (p ? 0x2222222222222222ULL : 0));
        }
    }

    // trick
    for (const auto& c : st.trick.cards) {
        uint64_t x = (static_cast<uint64_t>(static_cast<int>(c.suit)) << 8) |
                     static_cast<uint64_t>(static_cast<int>(c.rank));
        hashCombine(h, x + 0x3333333333333333ULL);
    }
    hashCombine(h, static_cast<uint64_t>(st.trick.starterPlayer));

    // simple flags
    hashCombine(h, st.finished ? 0xF00DF00DULL : 0x0ULL);

    return h;
}

GameState applyMove(const GameState& st, int player, int handIndex) {
    GameState ns = st; // copia
    RNG rng(1234);     // determinístico dentro da busca

    ns.playCard(player, handIndex);
    ns.maybeCloseTrick(rng);

    return ns;
}

bool ttLookup(uint64_t key, int depth,
              float alpha, float beta,
              float& outVal)
{
    std::lock_guard<std::mutex> lock(g_TTMutex);
    auto it = g_TT.find(key);
    if (it == g_TT.end()) return false;
    const TTEntry& e = it->second;
    if (e.depth < depth) return false;
    switch (e.flag) {
        case TTFlag::EXACT:
            outVal = e.value;
            return true;
        case TTFlag::LOWERBOUND:
            if (e.value >= beta) { outVal = e.value; return true; }
            break;
        case TTFlag::UPPERBOUND:
            if (e.value <= alpha) { outVal = e.value; return true; }
            break;
    }
    return false;
}

void ttStore(uint64_t key,
             int depth,
             float val,
             float alphaOrig,
             float betaOrig,
             int bestMoveHandIdx)
{
    TTEntry e;
    e.value = val;
    e.depth = depth;
    e.bestMoveHandIdx = bestMoveHandIdx;
    if (val <= alphaOrig) e.flag = TTFlag::UPPERBOUND;
    else if (val >= betaOrig) e.flag = TTFlag::LOWERBOUND;
    else e.flag = TTFlag::EXACT;

    std::lock_guard<std::mutex> lock(g_TTMutex);
    if (g_TT.size() > MAX_TT_SIZE) {
        g_TT.clear();
    }
    g_TT[key] = e;
}

float quiescenceAfterTrickClear(const GameState& st,
                                const NNUEWeights& w,
                                int rootPlayer,
                                bool perfectInfo)
{
    // se a vaza acabou de ser limpa (mesa vazia), olha 1 ply
    if (!st.trick.cards.empty())
        return nnueEvaluate(w, st, rootPlayer, perfectInfo);

    int p = st.currentPlayer;
    auto moves = st.getLegalMoves(p);
    if (moves.empty())
        return nnueEvaluate(w, st, rootPlayer, perfectInfo);

    float bestValMax = -std::numeric_limits<float>::infinity();
    float bestValMin =  std::numeric_limits<float>::infinity();
    for (int m : moves) {
        GameState ns = applyMove(st, p, m);
        float v = nnueEvaluate(w, ns, rootPlayer, perfectInfo);
        if (p == rootPlayer) bestValMax = std::max(bestValMax, v);
        else                 bestValMin = std::min(bestValMin, v);
    }
    return (p == rootPlayer) ? bestValMax : bestValMin;
}

float searchRecursiveAB(const GameState& st,
                        const NNUEWeights& w,
                        int rootPlayer,
                        int depth,
                        float alpha,
                        float beta,
                        bool perfectInfo)
{
    if (st.finished) {
        return nnueEvaluate(w, st, rootPlayer, perfectInfo);
    }

    float alphaOrig = alpha;
    float betaOrig = beta;

    // TT probe
    if (depth > 0) {
        uint64_t key = computeHash(st);
        float ttVal;
        if (ttLookup(key, depth, alpha, beta, ttVal)) {
            return ttVal;
        }
    }

    if (depth == 0) {
        // mini quiescência para posições logo após fechar a vaza
        return quiescenceAfterTrickClear(st, w, rootPlayer, perfectInfo);
    }

    int p = st.currentPlayer;
    auto moves = st.getLegalMoves(p);

    if (moves.empty()) {
        return nnueEvaluate(w, st, rootPlayer, perfectInfo);
    }

    if (p == rootPlayer) {
        // MAX node
        float bestVal = -std::numeric_limits<float>::infinity();
        // move ordering baseado num quickEval do estado seguinte
        std::vector<std::pair<int,float>> ordered;
        ordered.reserve(moves.size());
        for (int m : moves) {
            GameState ns = applyMove(st, p, m);
            float ev = quickEval(ns, w, rootPlayer, perfectInfo);
            ordered.emplace_back(m, ev);
        }
        std::sort(ordered.begin(), ordered.end(),
                  [](const auto& a, const auto& b){ return a.second > b.second; });

        int bestMoveLocal = ordered.empty() ? -1 : ordered.front().first;
        for (auto [m, _] : ordered) {
            GameState ns = applyMove(st, p, m);
            float val = searchRecursiveAB(ns, w, rootPlayer, depth - 1,
                                          alpha, beta, perfectInfo);
            if (val > bestVal) bestVal = val;
            if (val > alpha) {
                alpha = val;
                bestMoveLocal = m;
            }
            if (alpha >= beta) break; // beta cut
        }
        // store TT
        uint64_t key = computeHash(st);
        ttStore(key, depth, bestVal, alphaOrig, betaOrig, bestMoveLocal);
        return bestVal;
    } else {
        // MIN node
        float bestVal = std::numeric_limits<float>::infinity();
        std::vector<std::pair<int,float>> ordered;
        ordered.reserve(moves.size());
        for (int m : moves) {
            GameState ns = applyMove(st, p, m);
            float ev = quickEval(ns, w, rootPlayer, perfectInfo);
            ordered.emplace_back(m, ev);
        }
        std::sort(ordered.begin(), ordered.end(),
                  [](const auto& a, const auto& b){ return a.second < b.second; });

        int bestMoveLocal = ordered.empty() ? -1 : ordered.front().first;
        for (auto [m, _] : ordered) {
            GameState ns = applyMove(st, p, m);
            float val = searchRecursiveAB(ns, w, rootPlayer, depth - 1,
                                          alpha, beta, perfectInfo);
            if (val < bestVal) bestVal = val;
            if (val < beta) {
                beta = val;
                bestMoveLocal = m;
            }
            if (alpha >= beta) break; // alpha cut
        }
        uint64_t key = computeHash(st);
        ttStore(key, depth, bestVal, alphaOrig, betaOrig, bestMoveLocal);
        return bestVal;
    }
}

SearchResult searchBestMove(const GameState& st,
                            const NNUEWeights& w,
                            int depth,
                            bool perfectInfo)
{
    SearchResult res;
    res.eval = -std::numeric_limits<float>::infinity();
    res.chosenMoveIndex = -1;

    int p = st.currentPlayer;
    auto moves = st.getLegalMoves(p);
    if (moves.empty()) {
        res.eval = nnueEvaluate(w, st, p, perfectInfo);
        res.chosenMoveIndex = -1;
        return res;
    }

    float bestVal = -std::numeric_limits<float>::infinity();
    int bestMove = moves[0];

    float alpha = -std::numeric_limits<float>::infinity();
    float beta  =  std::numeric_limits<float>::infinity();

    // order root moves as well
    std::vector<std::pair<int,float>> ordered;
    ordered.reserve(moves.size());
    for (int m : moves) {
        GameState ns = applyMove(st, p, m);
        float ev = quickEval(ns, w, p, perfectInfo);
        ordered.emplace_back(m, ev);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b){ return a.second > b.second; });

    for (auto [m, _] : ordered) {
        GameState ns = applyMove(st, p, m);
        float val = searchRecursiveAB(ns, w, p, depth - 1,
                                      alpha, beta, perfectInfo);
        if (val > bestVal) {
            bestVal = val;
            bestMove = m;
        }
        if (bestVal > alpha) alpha = bestVal;
    }

    res.eval = bestVal;
    res.chosenMoveIndex = bestMove;
    return res;
}

SearchResult searchBestMoveMT(const GameState& st,
                              const NNUEWeights& w,
                              int depth,
                              bool perfectInfo)
{
    SearchResult res;
    res.eval = -std::numeric_limits<float>::infinity();
    res.chosenMoveIndex = -1;

    int p = st.currentPlayer;
    auto moves = st.getLegalMoves(p);
    if (moves.empty()) {
        res.eval = nnueEvaluate(w, st, p, perfectInfo);
        return res;
    }

    // order moves first
    std::vector<std::pair<int,float>> ordered;
    ordered.reserve(moves.size());
    for (int m : moves) {
        GameState ns = applyMove(st, p, m);
        float ev = quickEval(ns, w, p, perfectInfo);
        ordered.emplace_back(m, ev);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b){ return a.second > b.second; });

    std::vector<std::future<std::pair<int,float>>> futures;
    futures.reserve(ordered.size());

    float alpha = -std::numeric_limits<float>::infinity();
    float beta  =  std::numeric_limits<float>::infinity();

    for (auto [m, _] : ordered) {
        futures.emplace_back(std::async(std::launch::async, [&, m]() {
            GameState ns = applyMove(st, p, m);
            float val = searchRecursiveAB(ns, w, p, depth - 1, alpha, beta, perfectInfo);
            return std::make_pair(m, val);
        }));
    }

    int bestMove = ordered.front().first;
    float bestVal = -std::numeric_limits<float>::infinity();
    for (auto& fut : futures) {
        auto [m, v] = fut.get();
        if (v > bestVal) { bestVal = v; bestMove = m; }
    }

    res.eval = bestVal;
    res.chosenMoveIndex = bestMove;
    return res;
}

SearchResult searchBestMoveID(const GameState& st,
                              const NNUEWeights& w,
                              int depth,
                              bool perfectInfo)
{
    // iterative deepening para aquecer TT e refinar ordering
    const int p = st.currentPlayer;
    auto moves = st.getLegalMoves(p);
    if (moves.empty()) {
        return searchBestMove(st, w, depth, perfectInfo);
    }

    float bestEval = nnueEvaluate(w, st, p, perfectInfo);
    int bestMove = moves.front();

    float alpha, beta;
    for (int d = 1; d <= depth; ++d) {
        // janela de aspiração em torno do score anterior
        float delta = 0.5f + 0.5f * d; // janela cresce com depth
        alpha = bestEval - delta;
        beta  = bestEval + delta;

        // pesquisa com janela; se falhar, alarga
        while (true) {
            float curBest = -std::numeric_limits<float>::infinity();
            int curBestMove = moves.front();

            // ordenar root por quickEval
            std::vector<std::pair<int,float>> ordered;
            ordered.reserve(moves.size());
            for (int m : moves) {
                GameState ns = applyMove(st, p, m);
                float ev = quickEval(ns, w, p, perfectInfo);
                ordered.emplace_back(m, ev);
            }
            std::sort(ordered.begin(), ordered.end(),
                      [](const auto& a, const auto& b){ return a.second > b.second; });

            float a = alpha, b = beta;
            for (auto [m, _] : ordered) {
                GameState ns = applyMove(st, p, m);
                float v = searchRecursiveAB(ns, w, p, d - 1, a, b, perfectInfo);
                if (v > curBest) { curBest = v; curBestMove = m; }
                if (curBest > a) a = curBest;
            }

            if (curBest <= alpha) {
                alpha -= delta; delta *= 2.0f; continue; // fail-low: alarga para baixo
            } else if (curBest >= beta) {
                beta += delta;  delta *= 2.0f; continue; // fail-high: alarga para cima
            }

            bestEval = curBest;
            bestMove = curBestMove;
            break;
        }
    }

    SearchResult res{bestEval, bestMove};
    return res;
}
