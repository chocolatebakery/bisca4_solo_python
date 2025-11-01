#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <cstdlib>
#include <vector>

#include "eval_nnue.h"
#include "gamestate.h"
#include "mcts.h"
#include "rand.h"
#include "selfplay_mcts.h"

static bool g_rootMTFlag = false;

struct EngineContextMCTS {
    GameState state;
    MCTSConfig cfg;
    bool perfectInfo = false;
    RNG rng;
    NNUEWeights weights;
    bool hasNNUE = false;
    std::string nnuePath;

    EngineContextMCTS()
        : rng(randomSeed()) {}

    static uint64_t randomSeed() {
        auto now = std::chrono::high_resolution_clock::now()
            .time_since_epoch()
            .count();
        uint64_t x = static_cast<uint64_t>(now);
        x ^= (x << 13);
        x ^= (x >> 7);
        x ^= (x << 17);
        return x;
    }
};

static uint64_t randomSeed() {
    return EngineContextMCTS::randomSeed();
}

static void cmdShow(const GameState& st) {
    std::cout << st.toString() << "\n";
}

static bool applyMoveEngine(GameState& st, RNG& rng, int idx) {
    int p = st.currentPlayer;
    if (!st.playCard(p, idx)) return false;
    st.maybeCloseTrick(rng);
    return true;
}

static void cmdNewGame(EngineContextMCTS& ctx) {
    ctx.state.newGame(ctx.rng);
    std::cout << "Novo jogo (MCTS) iniciado.\n";
    cmdShow(ctx.state);
}

static void cmdPlay(EngineContextMCTS& ctx, int idx) {
    if (!applyMoveEngine(ctx.state, ctx.rng, idx)) {
        std::cout << "Jogada inválida (idx=" << idx << ").\n";
        return;
    }
    std::cout << "Jogada efetuada (idx " << idx << ").\n";
    cmdShow(ctx.state);
}

static void cmdBestMove(EngineContextMCTS& ctx) {
    const int player = ctx.state.currentPlayer;
    RNG searchRng(ctx.rng.nextU64() ^ 0x9e3779b97f4a7c15ULL);
    MCTSResult res = searchBestMoveMCTS(ctx.state, player, searchRng, ctx.cfg);

    std::cout << "bestmove index=" << res.chosenMoveIndex
              << " eval=" << std::fixed << std::setprecision(4) << res.eval
              << " visits=" << res.visits << "\n";
}

static int runEngineMode(MCTSConfig cfg, bool perfectInfo, const std::string& nnuePath) {
    EngineContextMCTS ctx;
    ctx.cfg = cfg;
    ctx.perfectInfo = perfectInfo;
    ctx.cfg.perfectInfo = perfectInfo;
    ctx.nnuePath = nnuePath;
    if (!nnuePath.empty()) {
        if (loadWeights(ctx.weights, nnuePath)) {
            ctx.hasNNUE = true;
            ctx.cfg.weights = &ctx.weights;
            ctx.cfg.useNNUE = true;
        } else {
            std::cerr << "Aviso: nao consegui carregar NNUE '" << nnuePath
                      << "'. Continuando com rollouts aleatorios.\n";
        }
    }

    std::cout << "Bisca4 MCTS Engine pronto. "
              << "iters=" << cfg.iterations
              << " cpuct=" << cfg.exploration
              << " perfectInfo=" << (perfectInfo ? 1 : 0)
              << " nnue=" << (ctx.hasNNUE ? nnuePath : "none")
              << "\n";

    cmdNewGame(ctx);

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        if (cmd == "quit" || cmd == "exit") break;
        else if (cmd == "newgame") cmdNewGame(ctx);
        else if (cmd == "show") cmdShow(ctx.state);
        else if (cmd == "bestmove") cmdBestMove(ctx);
        else if (cmd == "play") {
            int idx; iss >> idx;
            cmdPlay(ctx, idx);
        } else {
            std::cout << "Comando desconhecido.\n";
        }
    }
    return 0;
}

static int runSelfPlayMode(const std::string& outDataset,
                           int games,
                           MCTSConfig cfg,
                           int threads,
                           const std::string& nnuePath)
{
    NNUEWeights weights;
    bool hasNNUE = false;
    if (!nnuePath.empty()) {
        if (loadWeights(weights, nnuePath)) {
            hasNNUE = true;
            cfg.weights = &weights;
            cfg.useNNUE = true;
        } else {
            std::cerr << "Aviso: nao consegui carregar NNUE '" << nnuePath
                      << "'. Continuando com rollouts aleatorios.\n";
        }
    }

    std::vector<SelfPlaySampleMCTS> allSamples;
    allSamples.reserve(games * 40);
    std::mutex samplesMutex;
    std::atomic<long> totalScoreDiff{0};

    int hw = static_cast<int>(std::thread::hardware_concurrency());
    if (threads <= 0) threads = std::max(1, hw);
    threads = std::max(1, std::min(threads, games));

    std::cout << "Self-play MCTS paralelo: threads=" << threads
              << ", jogos=" << games
              << ", iters=" << cfg.iterations
              << ", cpuct=" << cfg.exploration
              << ", perfectInfo=" << (cfg.perfectInfo ? 1 : 0)
              << ", nnue=" << (hasNNUE ? nnuePath : "none")
              << "\n";

    std::vector<std::thread> workers;
    workers.reserve(threads);
    std::atomic<int> gameCounter{0};

    for (int t = 0; t < threads; ++t) {
        workers.emplace_back([&, t]() {
            RNG localRng(randomSeed() ^ (0x9e3779b97f4a7c15ULL * (t + 1)));
            std::vector<SelfPlaySampleMCTS> local;
            local.reserve(1000);

            while (true) {
                int g = gameCounter.fetch_add(1);
                if (g >= games) break;

                auto samples = playSelfPlayGameMCTS(cfg, localRng);
                if (!samples.empty()) {
                    totalScoreDiff += static_cast<long>(std::lround(samples[0].outcome));
                }
                local.insert(local.end(),
                             std::make_move_iterator(samples.begin()),
                             std::make_move_iterator(samples.end()));

                if (local.size() > 5000) {
                    std::lock_guard<std::mutex> lock(samplesMutex);
                    allSamples.insert(allSamples.end(),
                                      std::make_move_iterator(local.begin()),
                                      std::make_move_iterator(local.end()));
                    local.clear();
                }
            }

            if (!local.empty()) {
                std::lock_guard<std::mutex> lock(samplesMutex);
                allSamples.insert(allSamples.end(),
                                  std::make_move_iterator(local.begin()),
                                  std::make_move_iterator(local.end()));
            }
        });
    }

    for (auto& th : workers) th.join();

    std::cout << "Total samples: " << allSamples.size() << "\n";

    if (!saveSamplesMCTS(allSamples, outDataset)) {
        std::cerr << "ERRO: não consegui escrever dataset em " << outDataset << "\n";
    } else {
        std::cout << "Dataset escrito em " << outDataset << "\n";
    }

    std::ofstream rep("selfplay_report.txt");
    if (rep) {
        rep << "Jogos: " << games << "\n";
        rep << "Samples: " << allSamples.size() << "\n";
        rep << "Score médio (P0-P1): "
            << ((games > 0) ? static_cast<double>(totalScoreDiff) / games : 0.0)
            << "\n";
        rep << "perfectInfo=" << (cfg.perfectInfo ? 1 : 0) << "\n";
        rep << "iterations=" << cfg.iterations << "\n";
        rep << "cpuct=" << cfg.exploration << "\n";
        rep << "nnue=" << (hasNNUE ? nnuePath : "none") << "\n";
    }

    return 0;
}

int main(int argc, char** argv) {
    std::string mode = "engine";
    std::string datasetPath = "dataset_mcts.bin";
    int games = 200;
    int iterations = 2000;
    float cpuct = 1.41421356f;
    int threads = 0;
    bool perfectInfo = false;
    std::string nnuePath;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--mode" && i + 1 < argc) mode = argv[++i];
        else if (a == "--dataset" && i + 1 < argc) datasetPath = argv[++i];
        else if (a == "--games" && i + 1 < argc) games = std::max(1, std::atoi(argv[++i]));
        else if (a == "--iterations" && i + 1 < argc) iterations = std::max(1, std::atoi(argv[++i]));
        else if (a == "--depth" && i + 1 < argc) iterations = std::max(1, std::atoi(argv[++i]));
        else if (a == "--cpuct" && i + 1 < argc) cpuct = std::max(0.01f, static_cast<float>(std::atof(argv[++i])));
        else if (a == "--threads" && i + 1 < argc) threads = std::max(0, std::atoi(argv[++i]));
        else if (a == "--info" && i + 1 < argc) {
            std::string inf = argv[++i];
            perfectInfo = (inf == "perfect");
        } else if (a == "--nnue" && i + 1 < argc) {
            nnuePath = argv[++i];
        }
    }

    MCTSConfig cfg;
    cfg.iterations = iterations;
    cfg.exploration = cpuct;
    cfg.perfectInfo = perfectInfo;

    if (mode == "engine") {
        return runEngineMode(cfg, perfectInfo, nnuePath);
    } else if (mode == "selfplay") {
        return runSelfPlayMode(datasetPath, games, cfg, threads, nnuePath);
    }

    std::cerr << "Modo desconhecido '" << mode << "'.\n";
    return 1;
}
