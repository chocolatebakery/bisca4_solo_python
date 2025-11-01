#include "gamestate.h"
#include "search.h"
#include "eval_nnue.h"
#include "rand.h"
#include "mcts.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

uint64_t randomSeed64() {
    auto now = std::chrono::high_resolution_clock::now()
                   .time_since_epoch()
                   .count();
    uint64_t x = static_cast<uint64_t>(now);
    x ^= (x << 13);
    x ^= (x >> 7);
    x ^= (x << 17);
    return x;
}

enum class EngineType {
    AlphaBeta,
    MCTS
};

struct EngineSpec {
    EngineType type = EngineType::AlphaBeta;
    std::string name;

    // Alpha-beta parameters
    int depth = 4;
    std::string nnuePath = "nnue_iter0.bin";
    NNUEWeights weights;
    bool weightsLoaded = false;

    // MCTS parameters
    MCTSConfig mctsCfg;
    RNG rng;

    EngineSpec() : rng(randomSeed64()) {}
};

struct MatchConfig {
    EngineSpec engine[2];
    int games = 100;
    bool perfectInfo = false;
    uint64_t seed = randomSeed64();
};

bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

EngineType parseEngineType(const std::string& s) {
    if (iequals(s, "ab") || iequals(s, "alphabeta")) return EngineType::AlphaBeta;
    if (iequals(s, "mcts")) return EngineType::MCTS;
    throw std::runtime_error("Engine type desconhecido: " + s);
}

void ensureWeightsLoaded(EngineSpec& spec) {
    if (spec.weightsLoaded) return;

    if (spec.nnuePath.empty()) {
        if (spec.type == EngineType::MCTS) {
            spec.mctsCfg.weights = nullptr;
            spec.mctsCfg.useNNUE = false;
        }
        spec.weightsLoaded = true;
        return;
    }

    if (!loadWeights(spec.weights, spec.nnuePath)) {
        std::cerr << "Aviso: não consegui carregar NNUE '" << spec.nnuePath << "'.";
        if (spec.type == EngineType::AlphaBeta) {
            std::cerr << " Inicializando pesos aleatórios.\n";
            spec.weights.inputSize = 178;
            spec.weights.hidden1 = 64;
            spec.weights.hidden2 = 32;
            RNG rngInit(randomSeed64());
            initRandomWeights(spec.weights, 178, rngInit);
            spec.weightsLoaded = true;
        } else {
            std::cerr << " Continuando sem NNUE para MCTS.\n";
            spec.mctsCfg.weights = nullptr;
            spec.mctsCfg.useNNUE = false;
            spec.weightsLoaded = true;
        }
        return;
    }

    spec.weightsLoaded = true;
    if (spec.type == EngineType::MCTS) {
        spec.mctsCfg.weights = &spec.weights;
        spec.mctsCfg.useNNUE = true;
    }
}

int chooseMove(const EngineSpec& spec,
               GameState const& state,
               int player,
               bool perfectInfo,
               RNG& rngForSearch)
{
    if (spec.type == EngineType::AlphaBeta) {
        SearchResult res = searchBestMoveID(state, spec.weights, spec.depth, perfectInfo);
        return res.chosenMoveIndex;
    }

    if (spec.type == EngineType::MCTS) {
        MCTSResult res = searchBestMoveMCTS(state, player, rngForSearch, spec.mctsCfg);
        return res.chosenMoveIndex;
    }

    return -1;
}

std::string engineDescription(const EngineSpec& spec) {
    std::ostringstream oss;
    if (!spec.name.empty()) {
        oss << spec.name;
        return oss.str();
    }

    if (spec.type == EngineType::AlphaBeta) {
        oss << "AlphaBeta(depth=" << spec.depth << ", nnue=" << spec.nnuePath << ")";
    } else {
        oss << "MCTS(iter=" << spec.mctsCfg.iterations
            << ", cpuct=" << std::fixed << std::setprecision(2) << spec.mctsCfg.exploration;
        if (spec.mctsCfg.useNNUE && spec.mctsCfg.weights) {
            oss << ", nnue=" << spec.nnuePath;
        }
        oss << ")";
    }
    return oss.str();
}

void printUsage() {
    std::cout << "Uso: bisca4_match [opções]\n"
              << "  --engine1 ab|mcts           Tipo do jogador 1 (default ab)\n"
              << "  --engine2 ab|mcts           Tipo do jogador 2 (default ab)\n"
              << "  --nnue1 caminho.bin         NNUE para engine1 (ab)\n"
              << "  --nnue2 caminho.bin         NNUE para engine2 (ab)\n"
              << "  --depth1 N                  Profundidade para engine1 (ab)\n"
              << "  --depth2 N                  Profundidade para engine2 (ab)\n"
              << "  --iterations1 N             Iterações MCTS jogador1\n"
              << "  --iterations2 N             Iterações MCTS jogador2\n"
              << "  --cpuct1 X                  C constante MCTS jogador1\n"
              << "  --cpuct2 X                  C constante MCTS jogador2\n"
              << "  --games N                   Número de partidas (default 100)\n"
              << "  --perfect-info              Ativa modo perfect info para ambos\n"
              << "  --seed N                    Seed base (uint64)\n"
              << "Exemplos:\n"
              << "  bisca4_match --engine1 ab --engine2 mcts --depth1 6 --iterations2 4000 --games 200\n";
}

MatchConfig parseArgs(int argc, char** argv) {
    MatchConfig cfg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        auto requireValue = [&](const std::string& what) -> const char* {
            if (i + 1 >= argc) throw std::runtime_error("Falta valor para " + what);
            return argv[++i];
        };

        if (arg == "--engine1") {
            cfg.engine[0].type = parseEngineType(requireValue(arg));
        } else if (arg == "--engine2") {
            cfg.engine[1].type = parseEngineType(requireValue(arg));
        } else if (arg == "--nnue1") {
            cfg.engine[0].nnuePath = requireValue(arg);
        } else if (arg == "--nnue2") {
            cfg.engine[1].nnuePath = requireValue(arg);
        } else if (arg == "--depth1") {
            cfg.engine[0].depth = std::max(1, std::atoi(requireValue(arg)));
        } else if (arg == "--depth2") {
            cfg.engine[1].depth = std::max(1, std::atoi(requireValue(arg)));
        } else if (arg == "--iterations1") {
            cfg.engine[0].mctsCfg.iterations = std::max(1, std::atoi(requireValue(arg)));
        } else if (arg == "--iterations2") {
            cfg.engine[1].mctsCfg.iterations = std::max(1, std::atoi(requireValue(arg)));
        } else if (arg == "--cpuct1") {
            cfg.engine[0].mctsCfg.exploration = std::max(0.01f, static_cast<float>(std::atof(requireValue(arg))));
        } else if (arg == "--cpuct2") {
            cfg.engine[1].mctsCfg.exploration = std::max(0.01f, static_cast<float>(std::atof(requireValue(arg))));
        } else if (arg == "--games") {
            cfg.games = std::max(1, std::atoi(requireValue(arg)));
        } else if (arg == "--perfect-info") {
            cfg.perfectInfo = true;
        } else if (arg == "--seed") {
            cfg.seed = static_cast<uint64_t>(std::strtoull(requireValue(arg), nullptr, 10));
        } else if (arg == "--name1") {
            cfg.engine[0].name = requireValue(arg);
        } else if (arg == "--name2") {
            cfg.engine[1].name = requireValue(arg);
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            std::exit(0);
        } else {
            throw std::runtime_error("Argumento desconhecido: " + arg);
        }
    }

    // Defaults for MCTS if not set
    if (cfg.engine[0].mctsCfg.iterations <= 0) cfg.engine[0].mctsCfg.iterations = 2000;
    if (cfg.engine[1].mctsCfg.iterations <= 0) cfg.engine[1].mctsCfg.iterations = 2000;
    if (cfg.engine[0].mctsCfg.exploration <= 0.f) cfg.engine[0].mctsCfg.exploration = 1.40f;
    if (cfg.engine[1].mctsCfg.exploration <= 0.f) cfg.engine[1].mctsCfg.exploration = 1.40f;

    return cfg;
}

} // namespace

int main(int argc, char** argv) {
    try {
        MatchConfig cfg = parseArgs(argc, argv);

        std::cout << "=== Bisca4 Match Runner ===\n";
        std::cout << "Jogos: " << cfg.games << "\n";
        std::cout << "P0: " << engineDescription(cfg.engine[0]) << "\n";
        std::cout << "P1: " << engineDescription(cfg.engine[1]) << "\n";
        std::cout << "PerfectInfo: " << (cfg.perfectInfo ? "SIM" : "NAO") << "\n";
        std::cout << "Seed base: " << cfg.seed << "\n";
        std::cout << "===========================\n";

        ensureWeightsLoaded(cfg.engine[0]);
        ensureWeightsLoaded(cfg.engine[1]);

        RNG rngSeed(cfg.seed);

        int winsEngine[2] = {0, 0};
        int draws = 0;
        long long scoreDiffEngine0 = 0;

        for (int g = 0; g < cfg.games; ++g) {
            bool swap = (g % 2 == 1);
            EngineSpec* playerSpec[2] = { &cfg.engine[0], &cfg.engine[1] };
            if (swap) std::swap(playerSpec[0], playerSpec[1]);

            GameState st;
            RNG gameRng(rngSeed.nextU64());
            st.newGame(gameRng);

            while (!st.finished) {
                int player = st.currentPlayer;
                EngineSpec* spec = playerSpec[player];
                if (spec->type == EngineType::MCTS) {
                    spec->mctsCfg.perfectInfo = cfg.perfectInfo;
                    if (spec->mctsCfg.useNNUE && spec->mctsCfg.weights == nullptr && spec->weightsLoaded) {
                        spec->mctsCfg.weights = &spec->weights;
                    }
                }
                int move = chooseMove(*spec, st, st.currentPlayer, cfg.perfectInfo, spec->rng);
                if (move < 0) {
                    std::cerr << "Jogador " << player << " (" << engineDescription(*spec)
                              << ") não encontrou jogada válida. Forçando terminar.\n";
                    st.finished = true;
                    break;
                }
                if (!st.playCard(player, move)) {
                    std::cerr << "Jogador " << player << " jogou índice inválido " << move
                              << ". Abortando jogo.\n";
                    st.finished = true;
                    break;
                }
                st.maybeCloseTrick(gameRng);
            }

            int score0 = st.score[0];
            int score1 = st.score[1];
            int diff = score0 - score1;

            if (diff > 0) {
                if (!swap) winsEngine[0]++; else winsEngine[1]++;
            } else if (diff < 0) {
                if (!swap) winsEngine[1]++; else winsEngine[0]++;
            } else {
                draws++;
            }

            int diffForEngine0 = swap ? -diff : diff;
            scoreDiffEngine0 += diffForEngine0;

            std::string winner;
            if (diff > 0) {
                winner = "P0 (" + engineDescription(*playerSpec[0]) + ")";
            } else if (diff < 0) {
                winner = "P1 (" + engineDescription(*playerSpec[1]) + ")";
            } else {
                winner = "Empate";
            }

            std::cout << "Game " << std::setw(4) << (g + 1) << "/" << cfg.games
                      << " | P0 " << std::setw(3) << score0
                      << " - P1 " << std::setw(3) << score1
                      << " | vencedor: " << winner << "\n";
        }

        std::cout << "===========================\n";
        std::cout << "Resultados finais:\n";
        std::cout << " Engine #1 (" << engineDescription(cfg.engine[0]) << "): "
                  << winsEngine[0] << " vitórias\n";
        std::cout << " Engine #2 (" << engineDescription(cfg.engine[1]) << "): "
                  << winsEngine[1] << " vitórias\n";
        std::cout << " Empates: " << draws << "\n";
        std::cout << " Diferença média de pontos (Engine1): "
                  << (cfg.games > 0 ? (double)scoreDiffEngine0 / cfg.games : 0.0) << "\n";
        std::cout << "===========================\n";

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Erro: " << ex.what() << "\n";
        printUsage();
        return 1;
    }
}
