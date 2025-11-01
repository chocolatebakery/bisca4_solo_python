#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <sstream>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <cmath>

static bool g_rootMTFlag = false;

#include "gamestate.h"
#include "search.h"
#include "eval_nnue.h"
#include "selfplay.h"
#include "rand.h"

// ======================================================================
// Contexto de engine
// ======================================================================
struct EngineContext {
    GameState state;
    NNUEWeights weights;
    int depth = 3;
    bool perfectInfo = false;
    bool rootMT = false;
    RNG rng;

    EngineContext()
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
    auto now = std::chrono::high_resolution_clock::now()
        .time_since_epoch()
        .count();
    uint64_t x = static_cast<uint64_t>(now);
    // "embaralhar" mais os bits
    x ^= (x << 13);
    x ^= (x >> 7);
    x ^= (x << 17);
    return x;
}

// ======================================================================
// Comando SHOW (engine mode)
// ======================================================================
static void cmdShow(const GameState& st) {
    std::cout << st.toString() << "\n";
}

// ======================================================================
// Novo jogo
// ======================================================================
static void cmdNewGame(EngineContext& ctx) {
    ctx.rng = RNG(EngineContext::randomSeed());
    ctx.state.newGame(ctx.rng);
    std::cout << "Novo jogo iniciado.\n";
    cmdShow(ctx.state);
}

// ======================================================================
// Jogar carta (engine mode)
// ======================================================================
static void cmdPlay(EngineContext& ctx, int idx) {
    ctx.state = applyMove(ctx.state, ctx.state.currentPlayer, idx);
    std::cout << "Jogada efetuada (idx " << idx << ").\n";
    cmdShow(ctx.state);
}

// ======================================================================
// Melhor jogada (engine mode)
// ======================================================================
static void cmdBestMove(EngineContext& ctx) {
    SearchResult r;
    if (ctx.rootMT) r = searchBestMoveMT(ctx.state, ctx.weights, ctx.depth, ctx.perfectInfo);
    else            r = searchBestMoveID(ctx.state, ctx.weights, ctx.depth, ctx.perfectInfo);
    std::cout << "bestmove index=" << r.chosenMoveIndex
              << " eval=" << r.eval << "\n";
}

// ======================================================================
// Engine loop (modo interativo para GUI)
// ======================================================================
static int runEngineMode(const std::string& nnuePath, int depth, bool perfectInfo) {
    EngineContext ctx;
    ctx.depth = depth;
    ctx.perfectInfo = perfectInfo;
    ctx.rootMT = g_rootMTFlag;

    if (!loadWeights(ctx.weights, nnuePath)) {
        std::cerr << "Aviso: não consegui carregar NNUE de '" << nnuePath
                  << "'. Usando pesos aleatórios.\n";
        initRandomWeights(ctx.weights, 178, ctx.rng);
    } else {
        std::cout << "NNUE carregada de " << nnuePath << "\n";
    }

    std::cout << "Bisca4 Engine pronto.\n";
    std::string line;
    while (true) {
        if (!std::getline(std::cin, line)) break;
        if (line == "quit" || line == "exit") break;
        else if (line == "newgame") cmdNewGame(ctx);
        else if (line == "show") cmdShow(ctx.state);
        else if (line == "bestmove") cmdBestMove(ctx);
        else if (line.rfind("play", 0) == 0) {
            std::istringstream iss(line);
            std::string w; int idx;
            iss >> w >> idx;
            cmdPlay(ctx, idx);
        } else std::cout << "Comando desconhecido.\n";
    }
    return 0;
}

// ======================================================================
// SELFPLAY MODE – usado pelo loop de treino
// ======================================================================
static int runSelfPlayMode(const std::string& nnuePath,
                           const std::string& outDataset,
                           const std::string& outWeights,
                           int games,
                           int depth,
                           int threads,
                           bool perfectInfo)
{
    RNG rng(randomSeed());
    NNUEWeights weights;

    if (!loadWeights(weights, nnuePath)) {
        std::cerr << "Aviso: não consegui carregar NNUE de '" << nnuePath
                  << "'. A criar pesos aleatórios.\n";
        initRandomWeights(weights, 178, rng);
    } else if (weights.inputSize != 178) {
        std::cerr << "AVISO: rede carregada tem inputSize="
                  << weights.inputSize << " (esperado 178).\n";
    }

    std::vector<SelfPlaySample> allSamples;
    allSamples.reserve(games * 40);
    std::mutex samplesMutex;

    std::atomic<long> totalScoreDiff{0};

    int hw = (int)std::thread::hardware_concurrency();
    if (threads <= 0) threads = std::max(1, hw);
    threads = std::max(1, std::min(threads, games));

    std::cout << "Self-play paralelo: threads=" << threads
              << ", jogos=" << games
              << ", perfectInfo=" << (perfectInfo ? "1" : "0") << "\n";

    std::vector<std::thread> workers;
    workers.reserve(threads);

    std::atomic<int> gameCounter{0};

    for (int t = 0; t < threads; ++t) {
        workers.emplace_back([&, t]() {
            // RNG por thread
            RNG localRng(EngineContext::randomSeed() ^ (0x9e3779b97f4a7c15ULL * (t + 1)));
            std::vector<SelfPlaySample> local;
            local.reserve(1000);

            while (true) {
                int g = gameCounter.fetch_add(1);
                if (g >= games) break;

                auto samples = playSelfPlayGame(weights, depth, localRng, perfectInfo);
                if (!samples.empty())
                    totalScoreDiff += (long)std::lround(samples[0].outcome);

                // mover para local para reduzir lock contention
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

    // grava dataset com o nome exato pedido (--dataset)
    if (!saveSamples(allSamples, outDataset)) {
        std::cerr << "ERRO: não consegui escrever dataset em " << outDataset << "\n";
    } else {
        std::cout << "Dataset escrito em " << outDataset << "\n";
    }

    // relatório simples
    std::ofstream rep("selfplay_report.txt");
    if (rep) {
        rep << "Jogos: " << games << "\n";
        rep << "Samples: " << allSamples.size() << "\n";
        rep << "Score médio (P0-P1): "
            << ((games > 0) ? (double)totalScoreDiff / games : 0.0)
            << "\n";
        rep << "perfectInfo=" << (perfectInfo ? 1 : 0) << "\n";
    }

    if (!outWeights.empty()) {
        saveWeights(weights, outWeights);
    }

    return 0;
}

// ======================================================================
// GENWEIGHTS MODE – apenas gera pesos NNUE aleatórios e grava em disco
// ======================================================================
static int runGenWeightsMode(const std::string& outWeights)
{
    if (outWeights.empty()) {
        std::cerr << "Especifique --out-weights para gravar a NNUE.\n";
        return 1;
    }
    RNG rng(randomSeed());
    NNUEWeights w;
    initRandomWeights(w, 178, rng);
    if (!saveWeights(w, outWeights)) {
        std::cerr << "Falha a gravar pesos aleatórios em '" << outWeights << "'\n";
        return 1;
    }
    std::cout << "NNUE aleatória gravada em '" << outWeights << "' (input=178, h1="
              << w.hidden1 << ", h2=" << w.hidden2 << ")\n";
    return 0;
}

// ======================================================================
// MAIN
// ======================================================================
int main(int argc, char** argv) {
    std::string mode = "engine";
    std::string nnuePath = "nnue.bin";
    std::string datasetPath = "dataset.bin";
    std::string outWeights = "nnue_random.bin";
    int games = 200;
    int depth = 3;
    bool perfectInfo = false;
    int threads = 0; // 0 -> auto

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--mode" && i + 1 < argc) mode = argv[++i];
        else if (a == "--nnue" && i + 1 < argc) nnuePath = argv[++i];
        else if (a == "--depth" && i + 1 < argc) depth = std::max(1, std::atoi(argv[++i]));
        else if (a == "--games" && i + 1 < argc) games = std::max(1, std::atoi(argv[++i]));
        else if (a == "--dataset" && i + 1 < argc) datasetPath = argv[++i];
        else if (a == "--out-weights" && i + 1 < argc) outWeights = argv[++i];
        else if (a == "--info" && i + 1 < argc) {
            std::string inf = argv[++i];
            perfectInfo = (inf == "perfect");
        } else if (a == "--threads" && i + 1 < argc) {
            threads = std::max(0, std::atoi(argv[++i]));
        } else if (a == "--root-mt") {
            g_rootMTFlag = true;
        }
    }

    if (mode == "engine") {
        return runEngineMode(nnuePath, depth, perfectInfo);
    } else if (mode == "selfplay") {
        return runSelfPlayMode(nnuePath, datasetPath, outWeights, games, depth, threads, perfectInfo);
    } else if (mode == "genweights") {
        return runGenWeightsMode(outWeights);
    }

    std::cerr << "Modo desconhecido '" << mode << "'.\n";
    return 1;
}
