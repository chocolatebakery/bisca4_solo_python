#include "eval_nnue.h"
#include <fstream>
#include <cmath>

// Mapeia carta -> índice único [0..39] (naipe * 10 + rankIndex)
static int cardIndex(const Card& c) {
    int suitIdx = (int)c.suit; // Suit::Paus=0, Ouros=1, Copas=2, Espadas=3

    int rankIdx = 0;
    switch (c.rank) {
        case Rank::R2:   rankIdx = 0; break;
        case Rank::R3:   rankIdx = 1; break;
        case Rank::R4:   rankIdx = 2; break;
        case Rank::R5:   rankIdx = 3; break;
        case Rank::R6:   rankIdx = 4; break;
        case Rank::R10:  rankIdx = 5; break;
        case Rank::J:    rankIdx = 6; break;
        case Rank::Q:    rankIdx = 7; break;
        case Rank::K:    rankIdx = 8; break;
        case Rank::A:    rankIdx = 9; break;
        default:         rankIdx = 0; break; // safety
    }
    return suitIdx * 10 + rankIdx;
}

// One-hot do rank de uma carta (10 posições fixas)
// ordem: R2,R3,R4,R5,R6,R10,J,Q,K,A
static void encodeTrumpRankOneHot(std::vector<float>& feat, int base, const Card& trumpCard) {
    int idx = -1;
    switch (trumpCard.rank) {
        case Rank::R2:   idx = 0; break;
        case Rank::R3:   idx = 1; break;
        case Rank::R4:   idx = 2; break;
        case Rank::R5:   idx = 3; break;
        case Rank::R6:   idx = 4; break;
        case Rank::R10:  idx = 5; break;
        case Rank::J:    idx = 6; break;
        case Rank::Q:    idx = 7; break;
        case Rank::K:    idx = 8; break;
        case Rank::A:    idx = 9; break;
        default:         idx = -1; break;
    }
    if (idx >= 0 && idx < 10) {
        feat[base + idx] = 1.0f;
    }
}

// NOVO INPUT LAYOUT (178 floats):
//
// [  0.. 39] minhas cartas
// [ 40.. 79] cartas do oponente (0 se partial)
// [ 80..119] cartas na trick atual
// [120]      minha pontuação / 120.0
// [121]      pontuação opp / 120.0
// [122]      deck.size() / 40.0
// [123..126] one-hot do naipe de trunfo (4 floats)
//
// [127..166] cartas "visíveis/conhecidas" neste momento
//            1.0 se a carta está explicitamente conhecida:
//              - na minha mão
//              - NA trick atual (na mesa, logo pública)
//              - se perfectInfo==true e a carta está na mão do opp
//            0.0 caso contrário
//
// [167]      1.0 se trumpCard já foi entregue a alguém (st.trumpCardGiven), 0.0 se ainda não
//
// [168..177] one-hot do RANK da carta de trunfo inicial (10 floats)
//
// Total = 178 floats.
//
// Nota: antes tínhamos 127. Isto muda o tamanho de input da NNUE;
// precisas treinar de raiz.

std::vector<float> extractFeatures(const GameState& st,
                                   int player,
                                   bool perfectInfo)
{
    const int INPUT_SIZE = 178;

    std::vector<float> feat(INPUT_SIZE, 0.0f);

    int me  = player;
    int opp = 1 - player;

    // --- [0..39] minhas cartas
    for (auto &c : st.hands[me]) {
        feat[ cardIndex(c) ] = 1.0f;
    }

    // --- [40..79] cartas do oponente (se perfectInfo)
    if (perfectInfo) {
        for (auto &c : st.hands[opp]) {
            feat[40 + cardIndex(c)] = 1.0f;
        }
    }
    // caso contrário, deixamos [40..79] a zeros.

    // --- [80..119] cartas na trick atual (todas as que foram jogadas nesta vaza)
    for (auto &c : st.trick.cards) {
        feat[80 + cardIndex(c)] = 1.0f;
    }

    // --- [120], [121]: pontuação normalizada
    feat[120] = st.score[me]  / 120.0f;
    feat[121] = st.score[opp] / 120.0f;

    // --- [122]: fase do jogo (deck restante)
    feat[122] = (float)st.deck.size() / 40.0f;

    // --- [123..126]: trunfo suit one-hot
    {
        int ts = (int)st.trumpSuit; // Suit::Paus=0, Ouros=1, Copas=2, Espadas=3
        if (ts >= 0 && ts < 4) {
            feat[123 + ts] = 1.0f;
        }
    }

    // --- [127..166]: cartas visíveis/conhecidas
    // definimos 40 floats, inicial 0.0
    // marcamos 1.0 para:
    //   - minha mão
    //   - trick atual (pública)
    //   - mão do adversário, mas só se temos perfectInfo (modo treino completo)
    for (auto &c : st.hands[me]) {
        feat[127 + cardIndex(c)] = 1.0f;
    }
    for (auto &c : st.trick.cards) {
        feat[127 + cardIndex(c)] = 1.0f;
    }
    if (perfectInfo) {
        for (auto &c : st.hands[opp]) {
            feat[127 + cardIndex(c)] = 1.0f;
        }
    }

    // --- [167]: trunfo já foi dado ou não
    // Se ainda NÃO foi dado (trumpCardGiven == false), significa que o prémio
    // (a carta de trunfo virada no início) ainda está "por ganhar" quando o deck acabar.
    feat[167] = st.trumpCardGiven ? 1.0f : 0.0f;

    // --- [168..177]: one-hot do rank da carta de trunfo inicial
    encodeTrumpRankOneHot(feat, 168, st.trumpCard);

    return feat;
}

void initRandomWeights(NNUEWeights& w, int inputSize, RNG& rng) {
    w.inputSize  = inputSize;
    w.hidden1 = 64;
    w.hidden2 = 32;

    w.w1.resize(w.hidden1 * w.inputSize);
    w.b1.resize(w.hidden1);
    w.w2.resize(w.hidden2 * w.hidden1);
    w.b2.resize(w.hidden2);
    w.w3.resize(w.hidden2);
    w.b3 = 0.0f;

    auto randFloat = [&](float scale){
        return (float)((rng.nextDouble01() * 2.0 - 1.0) * scale);
    };

    for (auto &x : w.w1) x = randFloat(0.08f);
    for (auto &x : w.b1) x = randFloat(0.08f);
    for (auto &x : w.w2) x = randFloat(0.08f);
    for (auto &x : w.b2) x = randFloat(0.08f);
    for (auto &x : w.w3) x = randFloat(0.08f);
    w.b3 = randFloat(0.08f);
}

float nnueEvaluate(const NNUEWeights& w,
                   const GameState& st,
                   int player,
                   bool perfectInfo)
{
    std::vector<float> in = extractFeatures(st, player, perfectInfo);

    // hidden1 = ReLU(W1 * in + b1)
    std::vector<float> h1(w.hidden1);
    for (int h = 0; h < w.hidden1; ++h) {
        float acc = w.b1[h];
        const float* wrow = &w.w1[h * w.inputSize];
        for (int i = 0; i < w.inputSize; ++i) acc += wrow[i] * in[i];
        h1[h] = acc > 0.f ? acc : 0.f;
    }

    // hidden2 optional: if hidden2==0, we use h1 directly to output (compat old weights)
    float out = w.b3;
    if (w.hidden2 > 0) {
        std::vector<float> h2(w.hidden2);
        for (int h = 0; h < w.hidden2; ++h) {
            float acc = w.b2[h];
            const float* wrow = &w.w2[h * w.hidden1];
            for (int i = 0; i < w.hidden1; ++i) acc += wrow[i] * h1[i];
            h2[h] = acc > 0.f ? acc : 0.f;
        }
        for (int i = 0; i < w.hidden2; ++i) out += w.w3[i] * h2[i];
    } else {
        // directly project h1 with w3 (size hidden1)
        for (int i = 0; i < w.hidden1 && i < (int)w.w3.size(); ++i) out += w.w3[i] * h1[i];
    }
    return out;
}

bool saveWeights(const NNUEWeights& w, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    // header: input, h1, h2
    f.write((const char*)&w.inputSize,  sizeof(int));
    f.write((const char*)&w.hidden1,    sizeof(int));
    f.write((const char*)&w.hidden2,    sizeof(int));
    // matrices
    f.write((const char*)w.w1.data(),   w.w1.size()*sizeof(float));
    f.write((const char*)w.b1.data(),   w.b1.size()*sizeof(float));
    f.write((const char*)w.w2.data(),   w.w2.size()*sizeof(float));
    f.write((const char*)w.b2.data(),   w.b2.size()*sizeof(float));
    f.write((const char*)w.w3.data(),   w.w3.size()*sizeof(float));
    f.write((const char*)&w.b3,         sizeof(float));
    return true;
}

bool loadWeights(NNUEWeights& w, const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    int inSz=0, h1=0, h2=0;
    f.read((char*)&inSz, sizeof(int));
    // Try to detect old format (2-int header) vs new (3-int header)
    std::streampos posAfterIn = f.tellg();
    f.read((char*)&h1, sizeof(int));
    std::streampos posAfterH1 = f.tellg();
    f.read((char*)&h2, sizeof(int));

    bool oldFormat = false;
    if (!f.good() || h2 < 0 || h2 > 1024) {
        // old format: rewind to after inSz and read only hiddenSize
        oldFormat = true;
        f.clear();
        f.seekg(posAfterIn);
        f.read((char*)&h1, sizeof(int));
        h2 = 0; // not used
    }

    w.inputSize = inSz;
    if (oldFormat) {
        // map old 1-hidden network into new by placing weights into layer1 and output
        w.hidden1 = h1;
        w.hidden2 = 0; // special case

        w.w1.resize(w.hidden1 * w.inputSize);
        w.b1.resize(w.hidden1);
        w.w2.clear(); w.b2.clear();
        w.w3.resize(w.hidden1);

        f.read((char*)w.w1.data(), w.w1.size()*sizeof(float));
        f.read((char*)w.b1.data(), w.b1.size()*sizeof(float));
        f.read((char*)w.w3.data(), w.w3.size()*sizeof(float));
        f.read((char*)&w.b3,       sizeof(float));
        return true;
    }

    // new format
    w.hidden1 = h1;
    w.hidden2 = h2;
    w.w1.resize(w.hidden1 * w.inputSize);
    w.b1.resize(w.hidden1);
    w.w2.resize(w.hidden2 * w.hidden1);
    w.b2.resize(w.hidden2);
    w.w3.resize(w.hidden2);

    f.read((char*)w.w1.data(), w.w1.size()*sizeof(float));
    f.read((char*)w.b1.data(), w.b1.size()*sizeof(float));
    f.read((char*)w.w2.data(), w.w2.size()*sizeof(float));
    f.read((char*)w.b2.data(), w.b2.size()*sizeof(float));
    f.read((char*)w.w3.data(), w.w3.size()*sizeof(float));
    f.read((char*)&w.b3,       sizeof(float));
    return true;
}
