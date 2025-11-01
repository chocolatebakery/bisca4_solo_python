#pragma once
#include "card.h"
#include "rand.h"
#include <vector>
#include <cstdint>
#include <utility>
#include <string>

// A vaza atual
struct Trick {
    // No teu gamestate.cpp atual, trick.cards é std::vector<Card>
    // e tens trick.starterPlayer.
    // Vamos alinhar com isso.
    std::vector<Card> cards;
    int starterPlayer = 0;
};

class GameState {
public:
    // Monte de compra. Topo = back()
    std::vector<Card> deck;

    // Carta de trunfo (a carta que ficou virada no fim)
    Card trumpCard;
    // Naipe de trunfo
    Suit trumpSuit;

    // Se já demos a carta de trunfo ao comprar depois do deck acabar
    bool trumpCardGiven = false;

    // Mãos dos dois jogadores
    std::vector<Card> hands[2];

    // Pontuação acumulada
    int score[2] = {0,0};

    // Quem deve jogar agora (0 ou 1)
    int currentPlayer = 0;

    // Estado da vaza atual
    Trick trick;

    // O jogo acabou?
    bool finished = false;

    // -------------------------------------------------
    // Inicializa um novo jogo (baralha, dá 4 cartas a cada jogador,
    // separa trunfo, etc.)
    // -------------------------------------------------
    void newGame(RNG& rng);

    // -------------------------------------------------
    // Devolve índices das cartas que o jogador p pode jogar.
    // (No teu jogo podemos jogar qualquer carta da mão.)
    // -------------------------------------------------
    std::vector<int> getLegalMoves(int p) const;

    // -------------------------------------------------
    // Joga a carta hands[p][handIndex] para a trick.
    // Retorna false se inválido (mão errada, índice errado, jogo terminado, etc.)
    // -------------------------------------------------
    bool playCard(int p, int handIndex);

    // -------------------------------------------------
    // Avalia a trick de 4 cartas:
    // devolve {winnerPlayer, pontosDaVaza}
    // -------------------------------------------------
    std::pair<int,int> evaluateTrick() const;

    // -------------------------------------------------
    // Helpers de estado
    // -------------------------------------------------
    bool noMoreCardsToDraw() const;
    bool handsAreEmpty() const;

    // -------------------------------------------------
    // Se a trick tiver 4 cartas:
    //   - atribui pontos ao vencedor
    //   - dá cartas (compras) ao vencedor e ao outro
    //   - dá o trunfo se o baralho acabou
    //   - põe starterPlayer = vencedor
    //   - avança currentPlayer = vencedor
    //   - marca finished se não sobrar mesmo mais nada
    // -------------------------------------------------
    void maybeCloseTrick(RNG& rng);

    // -------------------------------------------------
    // Gera texto do estado, usado pelo main para falar com a GUI python.
    // Formato:
    //  ---------------------------------
    //  Trunfo: A de Espadas (Espadas)
    //  Pontuacao: P0=... P1=...
    //  Deck restante: ...
    //  CurrentPlayer: ...
    //  Mao P0:
    //    [0] ...
    //  ...
    //  Mao P1:
    //    ...
    //  Trick atual (N cartas jogadas nesta vaza):
    //    (0) ...
    //  Jogo terminado: SIM/NAO
    //  ---------------------------------
    // -------------------------------------------------
    std::string toString() const;
    

    
};
