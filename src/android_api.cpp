#include "android_api.h"

#include "eval_nnue.h"
#include "gamestate.h"
#include "mcts.h"
#include "rand.h"
#include "search.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

namespace {

constexpr int kNNUEInputSize = 178;

static uint64_t randomSeed() {
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    uint64_t x = static_cast<uint64_t>(now);
    x ^= (x << 13);
    x ^= (x >> 7);
    x ^= (x << 17);
    return x;
}

// ------------------------------- Alpha-beta backend -------------------------------

struct AlphaBetaEngine {
    GameState state;
    NNUEWeights weights;
    int depth = 3;
    bool perfectInfo = false;
    bool rootMT = false;
    RNG rng;
    bool hasWeights = false;

    AlphaBetaEngine() : rng(randomSeed()) {}

    bool init(const BiscaEngineConfig& cfg, std::string& status) {
        depth = std::max(1, cfg.depth > 0 ? cfg.depth : 3);
        perfectInfo = cfg.perfect_info != 0;
        rootMT = cfg.root_mt != 0;

        if (cfg.nnue_path && cfg.nnue_path[0] != '\0') {
            if (loadWeights(weights, cfg.nnue_path)) {
                hasWeights = true;
                status = std::string("NNUE carregada de ") + cfg.nnue_path;
            } else {
                hasWeights = false;
                status = std::string("Aviso: não consegui carregar NNUE de '") +
                         cfg.nnue_path + "'. A usar pesos aleatórios.";
            }
        } else {
            status = "NNUE não especificada. A usar pesos aleatórios.";
        }

        if (!hasWeights) {
            initRandomWeights(weights, kNNUEInputSize, rng);
        } else if (weights.inputSize != kNNUEInputSize) {
            status += " AVISO: inputSize diferente de 178.";
        }

        return true;
    }

    std::string newGame() {
        rng = RNG(randomSeed());
        state.newGame(rng);
        return "Novo jogo iniciado.";
    }

    std::string play(int idx, bool& ok) {
        ok = false;
        const auto legal = state.getLegalMoves(state.currentPlayer);
        if (idx < 0 || idx >= static_cast<int>(legal.size())) {
            return "Jogada inválida.";
        }
        state = applyMove(state, state.currentPlayer, idx);
        ok = true;
        std::ostringstream oss;
        oss << "Jogada efetuada (idx " << idx << ").";
        return oss.str();
    }

    std::string bestmove(int& outIdx, double& outEval) {
        SearchResult res;
        if (rootMT) {
            res = searchBestMoveMT(state, weights, depth, perfectInfo);
        } else {
            res = searchBestMoveID(state, weights, depth, perfectInfo);
        }
        outIdx = res.chosenMoveIndex;
        outEval = static_cast<double>(res.eval);

        std::ostringstream oss;
        oss << "bestmove index=" << res.chosenMoveIndex
            << " eval=" << res.eval;
        return oss.str();
    }

    std::string show() const {
        return state.toString();
    }
};

// ------------------------------- MCTS backend -------------------------------------

GameState applyMoveEngine(GameState st, RNG& rng, int idx) {
    int p = st.currentPlayer;
    if (!st.playCard(p, idx)) {
        return st;
    }
    st.maybeCloseTrick(rng);
    return st;
}

struct MCTSEngine {
    GameState state;
    MCTSConfig cfg;
    bool perfectInfo = false;
    RNG rng;
    NNUEWeights weights;
    bool hasNNUE = false;

    MCTSEngine() : rng(randomSeed()) {
        cfg.iterations = 2000;
        cfg.exploration = 1.41421356f;
    }

    bool init(const BiscaEngineConfig& cfgIn, std::string& status) {
        cfg.iterations = (cfgIn.iterations > 0) ? cfgIn.iterations : 2000;
        cfg.exploration = (cfgIn.cpuct > 0.0) ? static_cast<float>(cfgIn.cpuct) : 1.41421356f;
        perfectInfo = cfgIn.perfect_info != 0;
        cfg.perfectInfo = perfectInfo;

        if (cfgIn.nnue_path && cfgIn.nnue_path[0] != '\0') {
            if (loadWeights(weights, cfgIn.nnue_path)) {
                hasNNUE = true;
                cfg.weights = &weights;
                cfg.useNNUE = true;
                status = std::string("NNUE carregada de ") + cfgIn.nnue_path;
            } else {
                status = std::string("Aviso: não consegui carregar NNUE de '") +
                         cfgIn.nnue_path + "'. Rollouts sem NNUE.";
            }
        } else {
            status = "NNUE não especificada. Rollouts sem NNUE.";
        }

        if (hasNNUE && weights.inputSize != kNNUEInputSize) {
            status += " AVISO: inputSize diferente de 178.";
        }

        return true;
    }

    std::string newGame() {
        rng = RNG(randomSeed());
        state.newGame(rng);
        return "Novo jogo (MCTS) iniciado.";
    }

    std::string play(int idx, bool& ok) {
        ok = state.playCard(state.currentPlayer, idx);
        if (!ok) {
            return "Jogada inválida.";
        }
        state.maybeCloseTrick(rng);
        std::ostringstream oss;
        oss << "Jogada efetuada (idx " << idx << ").";
        return oss.str();
    }

    std::string bestmove(int& outIdx, double& outEval) {
        int player = state.currentPlayer;
        RNG searchRng(rng.nextU64() ^ 0x9e3779b97f4a7c15ULL);
        MCTSResult res = searchBestMoveMCTS(state, player, searchRng, cfg);
        outIdx = res.chosenMoveIndex;
        outEval = static_cast<double>(res.eval);

        std::ostringstream oss;
        oss << "bestmove index=" << res.chosenMoveIndex
            << " eval=" << std::fixed << std::setprecision(4) << res.eval
            << " visits=" << res.visits;
        return oss.str();
    }

    std::string show() const {
        return state.toString();
    }
};

// ------------------------------- Handle wrapper -----------------------------------

struct BiscaEngineHandle {
    BiscaEngineType type;
    std::unique_ptr<AlphaBetaEngine> ab;
    std::unique_ptr<MCTSEngine> mcts;
    std::string lastText;
    std::string status;
};

} // namespace

extern "C" {

BiscaEngineHandle* bisca_engine_create(const BiscaEngineConfig* cfg) {
    if (!cfg) return nullptr;

    auto handle = std::make_unique<BiscaEngineHandle>();
    handle->type = cfg->type;

    if (cfg->type == BISCA_ENGINE_ALPHABETA) {
        handle->ab = std::make_unique<AlphaBetaEngine>();
        if (!handle->ab->init(*cfg, handle->status)) {
            return nullptr;
        }
    } else {
        handle->mcts = std::make_unique<MCTSEngine>();
        if (!handle->mcts->init(*cfg, handle->status)) {
            return nullptr;
        }
    }

    return handle.release();
}

void bisca_engine_destroy(BiscaEngineHandle* handle) {
    delete handle;
}

const char* bisca_engine_status(BiscaEngineHandle* handle) {
    if (!handle) return nullptr;
    return handle->status.c_str();
}

const char* bisca_engine_new_game(BiscaEngineHandle* handle) {
    if (!handle) return nullptr;
    bool ok = false;
    if (handle->type == BISCA_ENGINE_ALPHABETA && handle->ab) {
        handle->lastText = handle->ab->newGame();
        ok = true;
    } else if (handle->mcts) {
        handle->lastText = handle->mcts->newGame();
        ok = true;
    }
    return ok ? handle->lastText.c_str() : nullptr;
}

const char* bisca_engine_show(BiscaEngineHandle* handle) {
    if (!handle) return nullptr;
    if (handle->type == BISCA_ENGINE_ALPHABETA && handle->ab) {
        handle->lastText = handle->ab->show();
    } else if (handle->mcts) {
        handle->lastText = handle->mcts->show();
    } else {
        return nullptr;
    }
    return handle->lastText.c_str();
}

const char* bisca_engine_play(BiscaEngineHandle* handle, int idx) {
    if (!handle) return nullptr;
    bool ok = false;
    if (handle->type == BISCA_ENGINE_ALPHABETA && handle->ab) {
        handle->lastText = handle->ab->play(idx, ok);
    } else if (handle->mcts) {
        handle->lastText = handle->mcts->play(idx, ok);
    } else {
        return nullptr;
    }
    return handle->lastText.c_str();
}

const char* bisca_engine_bestmove(BiscaEngineHandle* handle, int* out_index, double* out_eval) {
    if (!handle || !out_index || !out_eval) return nullptr;

    if (handle->type == BISCA_ENGINE_ALPHABETA && handle->ab) {
        handle->lastText = handle->ab->bestmove(*out_index, *out_eval);
    } else if (handle->mcts) {
        handle->lastText = handle->mcts->bestmove(*out_index, *out_eval);
    } else {
        return nullptr;
    }
    return handle->lastText.c_str();
}

} // extern "C"
