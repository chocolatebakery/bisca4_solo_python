#include "mcts.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace {

GameState applyMoveDeterministic(const GameState& st, int player, int handIndex) {
    GameState ns = st;
    if (!ns.playCard(player, handIndex)) {
        return ns;
    }
    RNG rng(1234);
    ns.maybeCloseTrick(rng);
    return ns;
}

void shuffleMoves(std::vector<int>& moves, RNG& rng) {
    if (moves.empty()) return;
    for (int i = static_cast<int>(moves.size()) - 1; i > 0; --i) {
        int j = static_cast<int>(rng.nextU32() % static_cast<uint32_t>(i + 1));
        std::swap(moves[i], moves[j]);
    }
}

struct Node {
    GameState state;
    int playerToMove = 0;
    int moveFromParent = -1;
    Node* parent = nullptr;
    std::vector<int> unexpandedMoves;

    struct Child {
        int move;
        std::unique_ptr<Node> node;
    };

    std::vector<Child> children;
    int visits = 0;
    float totalValue = 0.0f; // armazenado da perspetiva do jogador root
};

Node* selectNode(Node* node, int rootPlayer, const MCTSConfig& cfg) {
    while (true) {
        if (!node->unexpandedMoves.empty() || node->state.finished || node->children.empty()) {
            return node;
        }

        float bestScore = -std::numeric_limits<float>::infinity();
        Node* bestChild = nullptr;
        float parentVisits = static_cast<float>(node->visits + 1);

        for (auto& edge : node->children) {
            Node* child = edge.node.get();
            float score;
            if (child->visits == 0) {
                score = std::numeric_limits<float>::infinity();
            } else {
                float mean = child->totalValue / static_cast<float>(child->visits);
                if (node->playerToMove != rootPlayer) {
                    mean = -mean; // adversário tenta minimizar
                }
                float explore = cfg.exploration *
                                std::sqrt(std::log(parentVisits) / static_cast<float>(child->visits));
                score = mean + explore;
            }

            if (score > bestScore) {
                bestScore = score;
                bestChild = child;
            }
        }

        if (!bestChild) {
            return node;
        }
        node = bestChild;
    }
}

Node* expandNode(Node* node, int rootPlayer, RNG& rng) {
    if (node->unexpandedMoves.empty()) {
        return node;
    }

    int move = node->unexpandedMoves.back();
    node->unexpandedMoves.pop_back();

    GameState next = applyMoveDeterministic(node->state, node->playerToMove, move);
    auto child = std::make_unique<Node>();
    child->state = next;
    child->playerToMove = next.currentPlayer;
    child->moveFromParent = move;
    child->parent = node;
    child->unexpandedMoves = next.getLegalMoves(child->playerToMove);
    shuffleMoves(child->unexpandedMoves, rng);

    Node* childPtr = child.get();
    node->children.push_back(Node::Child{move, std::move(child)});
    return childPtr;
}

float rollout(GameState state,
              int rootPlayer,
              RNG& rng,
              const MCTSConfig& cfg) {
    int steps = 0;
    while (!state.finished) {
        if (cfg.rolloutLimit > 0 && steps >= cfg.rolloutLimit) {
            break;
        }

        int player = state.currentPlayer;
        auto moves = state.getLegalMoves(player);
        if (moves.empty()) {
            break;
        }

        int choice = moves[static_cast<size_t>(rng.nextU32() % moves.size())];
        state = applyMoveDeterministic(state, player, choice);
        steps++;
    }

    int other = 1 - rootPlayer;
    int diff = state.score[rootPlayer] - state.score[other];
    constexpr float normalizer = 120.0f;

    if (state.finished || !cfg.useNNUE || !cfg.weights) {
        return static_cast<float>(diff) / normalizer;
    }

    return nnueEvaluate(*cfg.weights, state, rootPlayer, cfg.perfectInfo);
}

void backpropagate(Node* node, float value) {
    while (node) {
        node->visits += 1;
        node->totalValue += value;
        node = node->parent;
    }
}

} // namespace

MCTSResult searchBestMoveMCTS(const GameState& state,
                              int rootPlayer,
                              RNG& rng,
                              const MCTSConfig& cfg)
{
    MCTSResult result;
    auto moves = state.getLegalMoves(rootPlayer);
    if (moves.empty()) {
        result.eval = 0.0f;
        result.chosenMoveIndex = -1;
        result.visits = 0;
        return result;
    }

    auto root = std::make_unique<Node>();
    root->state = state;
    root->playerToMove = rootPlayer;
    root->unexpandedMoves = moves;
    shuffleMoves(root->unexpandedMoves, rng);

    for (int iter = 0; iter < cfg.iterations; ++iter) {
        Node* node = selectNode(root.get(), rootPlayer, cfg);

        if (!node->state.finished) {
            node = expandNode(node, rootPlayer, rng);
        }

        float value = rollout(node->state, rootPlayer, rng, cfg);
        backpropagate(node, value);
    }

    Node* bestChild = nullptr;
    int bestVisits = -1;
    float bestEval = -std::numeric_limits<float>::infinity();

    for (auto& edge : root->children) {
        Node* child = edge.node.get();
        if (child->visits > bestVisits) {
            bestVisits = child->visits;
            bestChild = child;
        }
        float mean = (child->visits > 0) ? (child->totalValue / child->visits) : 0.0f;
        if (mean > bestEval) {
            bestEval = mean;
        }
    }

    if (bestChild) {
        result.chosenMoveIndex = bestChild->moveFromParent;
        result.eval = (bestChild->visits > 0)
                        ? (bestChild->totalValue / bestChild->visits)
                        : 0.0f;
        result.visits = bestChild->visits;
    } else {
        result.chosenMoveIndex = moves.front();
        result.eval = 0.0f;
        result.visits = 0;
    }

    return result;
}
