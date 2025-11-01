#include "selfplay_mcts.h"
#include "eval_nnue.h"
#include <fstream>
#include <iostream>

std::vector<SelfPlaySampleMCTS> playSelfPlayGameMCTS(const MCTSConfig& cfg,
                                                     RNG& rng)
{
    GameState st;
    st.newGame(rng);

    std::vector<SelfPlaySampleMCTS> result;
    result.reserve(200);

    while (!st.finished) {
        int p = st.currentPlayer;

        SelfPlaySampleMCTS sample;
        sample.features = extractFeatures(st, p, cfg.perfectInfo);
        sample.outcome = 0.0f;

        MCTSResult sr = searchBestMoveMCTS(st, p, rng, cfg);
        int moveIdx = sr.chosenMoveIndex;
        if (moveIdx < 0) {
            st.finished = true;
            break;
        }

        if (!st.playCard(p, moveIdx)) {
            st.finished = true;
            break;
        }
        st.maybeCloseTrick(rng);

        result.push_back(std::move(sample));
    }

    int diff = st.score[0] - st.score[1];
    for (auto& s : result) {
        s.outcome = static_cast<float>(diff);
    }

    std::cout << "Self-play MCTS terminou. "
              << "Score0=" << st.score[0]
              << " Score1=" << st.score[1]
              << " Diff="   << diff
              << " perfectInfo=" << (cfg.perfectInfo ? 1 : 0)
              << "\n";

    return result;
}

bool saveSamplesMCTS(const std::vector<SelfPlaySampleMCTS>& samples,
                     const std::string& path)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    uint32_t n = static_cast<uint32_t>(samples.size());
    f.write(reinterpret_cast<const char*>(&n), sizeof(uint32_t));

    for (const auto& s : samples) {
        uint32_t flen = static_cast<uint32_t>(s.features.size());
        f.write(reinterpret_cast<const char*>(&flen), sizeof(uint32_t));
        f.write(reinterpret_cast<const char*>(s.features.data()),
                flen * sizeof(float));
        f.write(reinterpret_cast<const char*>(&s.outcome), sizeof(float));
    }

    return true;
}
