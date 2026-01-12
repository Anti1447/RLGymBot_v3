// MCTSNode.h - 2025-01-11 | Rev 1.0 | NJH
// MCTS tree node for MuZero-style reanalysis
// Designed for batched GPU inference during offline search

#pragma once
#include "../FrameworkTorch.h"
#include <vector>
#include <memory>
#include <atomic>

namespace GGL {

// ============================================================================
// MCTS Node - Represents one state in the search tree
// ============================================================================
struct MCTSNode {
    // Parent/child structure
    MCTSNode* parent = nullptr;
    std::vector<std::unique_ptr<MCTSNode>> children;
    
    // Action that led to this node from parent
    int action = -1;
    
    // Visit statistics
    std::atomic<int> visitCount{0};
    std::atomic<float> totalValue{0.0f};
    
    // Prior policy (from neural network)
    std::vector<float> prior;  // P(a|s) for each action
    
    // Game state (optional - can store for debugging)
    // We don't actually need to store full state for reanalysis
    // since we're just improving policy/value targets
    
    // Expansion state
    bool isExpanded = false;
    bool isTerminal = false;
    float terminalValue = 0.0f;
    
    // Constructor
    MCTSNode(MCTSNode* parent, int action, const std::vector<float>& prior)
        : parent(parent), action(action), prior(prior) {}
    
    // UCB score for action selection
    float GetUCB(int totalParentVisits, float cPuct = 1.25f) const {
        if (visitCount == 0) {
            // Unvisited nodes get high exploration bonus
            return cPuct * prior[action] * sqrtf(totalParentVisits) / (1 + visitCount);
        }
        
        float Q = totalValue / visitCount;  // Mean value
        float U = cPuct * prior[action] * sqrtf(totalParentVisits) / (1 + visitCount);
        return Q + U;
    }
    
    // Get mean value estimate
    float GetMeanValue() const {
        if (visitCount == 0) return 0.0f;
        return totalValue / visitCount;
    }
    
    // Select best child according to UCB
    MCTSNode* SelectChild(float cPuct = 1.25f) {
        if (children.empty()) return nullptr;
        
        int totalVisits = visitCount.load();
        MCTSNode* best = nullptr;
        float bestScore = -std::numeric_limits<float>::infinity();
        
        for (auto& child : children) {
            float score = child->GetUCB(totalVisits, cPuct);
            if (score > bestScore) {
                bestScore = score;
                best = child.get();
            }
        }
        
        return best;
    }
    
    // Get visit count distribution (for policy target)
    std::vector<float> GetVisitDistribution() const {
        std::vector<float> dist(prior.size(), 0.0f);
        
        int totalVisits = 0;
        for (const auto& child : children) {
            totalVisits += child->visitCount.load();
        }
        
        if (totalVisits == 0) return dist;
        
        for (const auto& child : children) {
            dist[child->action] = float(child->visitCount.load()) / totalVisits;
        }
        
        return dist;
    }
    
    // Backup value through tree
    void Backup(float value) {
        MCTSNode* node = this;
        while (node != nullptr) {
            node->visitCount++;
            node->totalValue += value;
            value = -value;  // Flip sign for opponent
            node = node->parent;
        }
    }
};

// ============================================================================
// MCTS Search Result - What we get from one search
// ============================================================================
struct MCTSResult {
    std::vector<float> visitDistribution;  // Policy target (visit counts)
    float valueEstimate;                    // Value target
    int selectedAction;                     // Action we'd actually take
    
    // Diagnostics
    int totalSimulations;
    float meanTreeDepth;
};

} // namespace GGL
