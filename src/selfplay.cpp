#include "selfplay.h"
#include "eval_nnue.h"
#include <fstream>
#include <iostream>

// Joga um jogo self-play entre dois agentes que usam sempre a mesma NNUE 'w'
// e profundidade fixa 'depth'.
// perfectInfo = true  -> cada jogador vê cartas todas (modo "trapaça/perfeito")
// perfectInfo = false -> cada jogador só vê a própria mão (modo parcial)
std::vector<SelfPlaySample> playSelfPlayGame(const NNUEWeights& w,
                                             int depth,
                                             RNG& rng,
                                             bool perfectInfo)
{
    GameState st;
    st.newGame(rng);

    std::vector<SelfPlaySample> result;
    result.reserve(200); // só para evitar reallocs

    while (!st.finished) {
        int p = st.currentPlayer;

        // Extrair features da perspetiva do jogador atual
        // Nota: a tua eval_nnue.h tem algo tipo:
        //   std::vector<float> extractFeatures(const GameState&, int povPlayer, bool perfectInfo)
        auto featBefore = extractFeatures(st, p, perfectInfo);

        // Escolher jogada com o motor de busca
        SearchResult sr = searchBestMoveID(st, w, depth, perfectInfo);
        int moveIdx = sr.chosenMoveIndex;

        // Se não houver jogada válida (deveria ser raro), termina
        if (moveIdx < 0) {
            st.finished = true;
            break;
        }

        // Jogar carta real
        st.playCard(p, moveIdx);
        st.maybeCloseTrick(rng);

        // Guardar sample para treino
        SelfPlaySample sample;
        sample.features = std::move(featBefore);
        sample.outcome = 0.0f; // vamos preencher no fim com score diff
        result.push_back(std::move(sample));
    }

    // Resultado final do jogo:
    // outcome = score0 - score1, aplicado a TODOS os samples
    int diff = st.score[0] - st.score[1];
    for (auto &s : result) {
        s.outcome = static_cast<float>(diff);
    }

    std::cout << "Self-play terminou. "
              << "Score0=" << st.score[0]
              << " Score1=" << st.score[1]
              << " Diff="   << diff
              << " perfectInfo=" << (perfectInfo ? 1 : 0)
              << "\n";

    return result;
}

// Guarda dataset em binário simples:
// nSamples
// para cada sample:
//   featLen
//   feat[0..featLen-1] (floats)
//   outcome (float)
bool saveSamples(const std::vector<SelfPlaySample>& samples,
                 const std::string& path)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    uint32_t n = static_cast<uint32_t>(samples.size());
    f.write(reinterpret_cast<const char*>(&n), sizeof(uint32_t));

    for (const auto &s : samples) {
        uint32_t flen = static_cast<uint32_t>(s.features.size());
        f.write(reinterpret_cast<const char*>(&flen), sizeof(uint32_t));
        f.write(reinterpret_cast<const char*>(s.features.data()),
                flen * sizeof(float));
        f.write(reinterpret_cast<const char*>(&s.outcome),
                sizeof(float));
    }

    return true;
}
