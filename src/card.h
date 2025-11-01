#pragma once
#include <vector>
#include <string>
#include <algorithm>
#include <cassert>
#include <cstdint>

enum class Suit { Paus = 0, Ouros = 1, Copas = 2, Espadas = 3 };
enum class Rank { R2, R3, R4, R5, R6, R10, J, Q, K, A };

struct Card {
    Suit suit;
    Rank rank;
};

// Pontos da carta
int cardPoints(const Card& c);

// Força da carta dentro do mesmo naipe (maior número = carta mais forte)
int cardStrength(const Card& c);

// Helpers para debug/output
std::string suitToString(Suit s);
std::string rankToString(Rank r);
std::string cardToString(const Card& c);

// Gera o baralho inicial de 40 cartas (2,3,4,5,6,10,J,Q,K,A em cada naipe)
// Sem 7,8,9.
std::vector<Card> makeDeck();

// baralhar (in-place)
template <class RNG_T>
void shuffleDeck(std::vector<Card>& deck, RNG_T& rng) {
    for (int i = (int)deck.size() - 1; i > 0; --i) {
        uint32_t j = rng.nextU32() % (uint32_t)(i + 1);
        std::swap(deck[i], deck[j]);
    }
}
