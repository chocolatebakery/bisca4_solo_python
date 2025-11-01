#include "card.h"

int cardPoints(const Card& c) {
    switch (c.rank) {
        case Rank::A:  return 11;
        case Rank::R10:return 10;
        case Rank::K:  return 4;
        case Rank::Q:  return 2;
        case Rank::J:  return 3;
        default:       return 0; // 6,5,4,3,2
    }
}

// Força (A > 10 > K > J > Q > 6 > 5 > 4 > 3 > 2)
// Podemos mapear isto para números decrescentes
int cardStrength(const Card& c) {
    switch (c.rank) {
        case Rank::A:   return 9;
        case Rank::R10: return 8;
        case Rank::K:   return 7;
        case Rank::J:   return 6;
        case Rank::Q:   return 5;
        case Rank::R6:  return 4;
        case Rank::R5:  return 3;
        case Rank::R4:  return 2;
        case Rank::R3:  return 1;
        case Rank::R2:  return 0;
    }
    return 0;
}

// Output helpers -----------------
std::string suitToString(Suit s) {
    switch (s) {
        case Suit::Paus:   return "Paus";
        case Suit::Ouros:  return "Ouros";
        case Suit::Copas:  return "Copas";
        case Suit::Espadas:return "Espadas";
    }
    return "?";
}

std::string rankToString(Rank r) {
    switch (r) {
        case Rank::R2:   return "2";
        case Rank::R3:   return "3";
        case Rank::R4:   return "4";
        case Rank::R5:   return "5";
        case Rank::R6:   return "6";
        case Rank::R10:  return "10";
        case Rank::J:    return "J";
        case Rank::Q:    return "Q";
        case Rank::K:    return "K";
        case Rank::A:    return "A";
    }
    return "?";
}

std::string cardToString(const Card& c) {
    return rankToString(c.rank) + " de " + suitToString(c.suit);
}

// Deck ---------------------------
std::vector<Card> makeDeck() {
    std::vector<Card> d;
    d.reserve(40);
    std::vector<Rank> ranks = {
        Rank::R2, Rank::R3, Rank::R4, Rank::R5, Rank::R6,
        Rank::R10, Rank::J, Rank::Q, Rank::K, Rank::A
    };

    for (int s = 0; s < 4; ++s) {
        for (auto r : ranks) {
            d.push_back(Card{ (Suit)s, r });
        }
    }
    return d;
}
