#include "gamestate.h"
#include "card.h"
#include <cassert>
#include <sstream>
#include <vector>
#include <utility>
#include <cstdint>
#include <chrono>

// ======================
// Helpers de texto/cartas
// ======================

// Converte Suit -> nome em português
static std::string suitName(Suit s) {
    switch (s) {
    case Suit::Ouros:   return "Ouros";
    case Suit::Copas:   return "Copas";
    case Suit::Espadas: return "Espadas";
    case Suit::Paus:    return "Paus";
    default:            return "?";
    }
}

// Converte Rank -> "A", "K", "Q", "J", "10", "6", etc.
static std::string rankName(Rank r) {
    switch (r) {
    case Rank::A:   return "A";
    case Rank::K:   return "K";
    case Rank::Q:   return "Q";
    case Rank::J:   return "J";
    case Rank::R10: return "10";
    case Rank::R6:  return "6";
    case Rank::R5:  return "5";
    case Rank::R4:  return "4";
    case Rank::R3:  return "3";
    case Rank::R2:  return "2";
    default:        return "?";
    }
}

// "A de Espadas", "10 de Ouros", etc.
static std::string cardToString(const Card& c) {
    std::ostringstream ss;
    ss << rankName(c.rank) << " de " << suitName(c.suit);
    return ss.str();
}

// Pontos de uma carta individual segundo as regras:
// Dama(Q)=2, Valete(J)=3, Rei(K)=4, 10=10, Ás(A)=11
static int cardPointsLocal(const Card& c) {
    switch (c.rank) {
    case Rank::Q:   return 2;
    case Rank::J:   return 3;
    case Rank::K:   return 4;
    case Rank::R10: return 10;
    case Rank::A:   return 11;
    default:        return 0;
    }
}

// Isto já existe no teu código original
extern int cardStrength(const Card& c);

// Dado o índice i na trick (0..3), quem jogou a carta trick.cards[i]?
static int playerOfIndex(const Trick& t, int idx) {
    if (idx % 2 == 0) return t.starterPlayer;
    return 1 - t.starterPlayer;
}

// ======================
// Funções auxiliares para o baralho
// ======================

// A bisca dos 4 usa: 2,3,4,5,6,10,J,Q,K,A (sem 7,8,9)
static std::vector<Card> makeDeckLocal() {
    std::vector<Card> d;
    d.reserve(40);

    const Rank ranksWanted[] = {
        Rank::R2, Rank::R3, Rank::R4, Rank::R5, Rank::R6,
        Rank::R10, Rank::J, Rank::Q, Rank::K, Rank::A
    };

    for (int s = 0; s < 4; ++s) {
        for (auto r : ranksWanted) {
            Card c;
            c.suit = static_cast<Suit>(s);
            c.rank = r;
            d.push_back(c);
        }
    }

    return d;
}

// Shuffle pseudo-aleatório independente do RNG do projeto
static uint64_t localSeed() {
    auto now = std::chrono::high_resolution_clock::now()
        .time_since_epoch()
        .count();
    uint64_t x = static_cast<uint64_t>(now);
    x ^= (x << 13);
    x ^= (x >> 7);
    x ^= (x << 17);
    return x;
}

static uint64_t next64(uint64_t &s) {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
}

static void shuffleDeckLocal(std::vector<Card>& d) {
    uint64_t s = localSeed();
    for (int i = (int)d.size() - 1; i > 0; --i) {
        uint64_t r = next64(s);
        int j = (int)(r % (uint64_t)(i + 1));
        std::swap(d[i], d[j]);
    }
}

// ======================
// Métodos de GameState
// ======================

void GameState::newGame(RNG& rng) {
    finished = false;
    trumpCardGiven = false;

    score[0] = 0;
    score[1] = 0;

    hands[0].clear();
    hands[1].clear();

    trick.cards.clear();
    trick.starterPlayer = 0;

    currentPlayer = 0;

    // gerar baralho base e baralhar
    std::vector<Card> fullDeck = makeDeckLocal();
    shuffleDeckLocal(fullDeck);

    // última carta vira trunfo
    trumpCard = fullDeck.back();
    trumpSuit = trumpCard.suit;
    fullDeck.pop_back();

    // o resto fica no deck de compra
    deck = fullDeck;

    // dá 4 cartas alternadas a cada jogador
    for (int i = 0; i < 4; ++i) {
        hands[0].push_back(deck.back()); deck.pop_back();
        hands[1].push_back(deck.back()); deck.pop_back();
    }
}

std::vector<int> GameState::getLegalMoves(int p) const {
    std::vector<int> moves;
    if (p != currentPlayer) return moves;
    for (int i = 0; i < (int)hands[p].size(); ++i)
        moves.push_back(i);
    return moves;
}

bool GameState::playCard(int p, int handIndex) {
    if (finished) return false;
    if (p != currentPlayer) return false;
    if (handIndex < 0 || handIndex >= (int)hands[p].size()) return false;

    Card c = hands[p][handIndex];
    hands[p].erase(hands[p].begin() + handIndex);
    trick.cards.push_back(c);

    currentPlayer = 1 - currentPlayer;
    return true;
}

// Decide quem ganhou a vaza de 4 cartas e quantos pontos vale
std::pair<int,int> GameState::evaluateTrick() const {
    assert(trick.cards.size() == 4);

    int potPoints = 0;
    for (auto &c : trick.cards)
        potPoints += cardPointsLocal(c);

    bool anyTrump = false;
    for (auto &c : trick.cards)
        if (c.suit == trumpSuit) anyTrump = true;

    int winnerIndex = 0;
    if (anyTrump) {
        for (int i = 1; i < 4; ++i) {
            bool winIsTrump = (trick.cards[winnerIndex].suit == trumpSuit);
            bool curIsTrump = (trick.cards[i].suit == trumpSuit);
            if (curIsTrump && !winIsTrump)
                winnerIndex = i;
            else if (curIsTrump && winIsTrump &&
                     cardStrength(trick.cards[i]) > cardStrength(trick.cards[winnerIndex]))
                winnerIndex = i;
        }
    } else {
        Suit leadSuit = trick.cards[0].suit;
        for (int i = 1; i < 4; ++i) {
            bool wFollows = (trick.cards[winnerIndex].suit == leadSuit);
            bool cFollows = (trick.cards[i].suit == leadSuit);
            if (cFollows && !wFollows)
                winnerIndex = i;
            else if (cFollows && wFollows &&
                     cardStrength(trick.cards[i]) > cardStrength(trick.cards[winnerIndex]))
                winnerIndex = i;
        }
    }

    int winnerPlayer = playerOfIndex(trick, winnerIndex);
    return { winnerPlayer, potPoints };
}

bool GameState::noMoreCardsToDraw() const {
    return deck.empty() && trumpCardGiven;
}

bool GameState::handsAreEmpty() const {
    return hands[0].empty() && hands[1].empty();
}

void GameState::maybeCloseTrick(RNG& rng) {
    if (trick.cards.size() < 4) return;

    auto [winnerPlayer, potPoints] = evaluateTrick();
    score[winnerPlayer] += potPoints;
    int loserPlayer = 1 - winnerPlayer;


    auto drawFromDeck = [&](int plr){
        if (!deck.empty()) {
            hands[plr].push_back(deck.back());
            deck.pop_back();
        }
    };

    auto needCard = [&](int plr){
        return ((int)hands[plr].size() < 4) && (!deck.empty() || !trumpCardGiven);
    };

    // comprar do deck (vencedor compra primeiro)
    if (needCard(winnerPlayer)) drawFromDeck(winnerPlayer);
    if (needCard(loserPlayer))  drawFromDeck(loserPlayer);
    if (needCard(winnerPlayer)) drawFromDeck(winnerPlayer);
    if (needCard(loserPlayer))  drawFromDeck(loserPlayer);

    // entregar a carta de trunfo virada (a última do baralho)
    // Requisito: vai para quem PERDEU a vaza.
    if (!trumpCardGiven) {
        if (needCard(loserPlayer)) {
            hands[loserPlayer].push_back(trumpCard);
            trumpCardGiven = true;
        } else if (needCard(winnerPlayer)) {
            hands[winnerPlayer].push_back(trumpCard);
            trumpCardGiven = true;
        }
    }

    trick.cards.clear();
    trick.starterPlayer = winnerPlayer;
    currentPlayer = winnerPlayer;

    if (deck.empty() && trumpCardGiven && handsAreEmpty() && trick.cards.empty())
        finished = true;
}

std::string GameState::toString() const {
    std::ostringstream oss;
    oss << "---------------------------------\n";

    oss << "Trunfo: " << cardToString(trumpCard)
        << " (" << suitName(trumpCard.suit) << ")\n";

    oss << "Pontuacao: P0=" << score[0]
        << " P1=" << score[1] << "\n";

    oss << "Deck restante: " << deck.size()
        << " cartas (sem contar trumpCard especial)\n";
    oss << "TrunfoDado: " << (trumpCardGiven ? 1 : 0) << "\n";

    oss << "CurrentPlayer: " << currentPlayer << "\n";

    oss << "Mao P0:\n";
    for (size_t i = 0; i < hands[0].size(); ++i)
        oss << "  [" << i << "] " << cardToString(hands[0][i]) << "\n";

    oss << "Mao P1:\n";
    for (size_t i = 0; i < hands[1].size(); ++i)
        oss << "  [" << i << "] " << cardToString(hands[1][i]) << "\n";

    oss << "Trick atual (" << trick.cards.size()
        << " cartas jogadas nesta vaza):\n";
    for (size_t i = 0; i < trick.cards.size(); ++i)
        oss << "  (" << i << ") " << cardToString(trick.cards[i]) << "\n";

    // (sem imprimir última trick – GUI não depende disso)

    oss << "Jogo terminado: " << (finished ? "SIM" : "NAO") << "\n";
    oss << "---------------------------------\n";
    return oss.str();
}


