// MCTSSearch.h - 2025-01-11 | Rev 1.0 | NJH
// MCTS search implementation optimized for batched GPU inference
// Key insight: Collect all leaf nodes, batch evaluate them, then continue

#pragma once
#include "MCTSNode.h"
#include "../FrameworkTorch.h"
#include <RLGymCPP/Gamestates/GameState.h>
#include <functional>
#include <vector>

namespace GGL {

// ============================================================================
// MCTS Config
// ============================================================================
struct MCTSConfig {
    int numSimulations = 8;          // Sims per search (start low!)
    float cPuct = 1.25f;             // Exploration constant
    float dirichletAlpha = 0.3f;     // Root noise for exploration
    float dirichletEpsilon = 0.25f;  // Fraction of root prior to replace
    bool addRootNoise = false;       // Enable for training diversity
    
    // Virtual loss for parallel search (advanced - set to 0 initially)
    float virtualLoss = 0.0f;
};

// ============================================================================
// Policy/Value Evaluator - Interface to neural network
// ============================================================================
// This will be called with batches of states
// Returns: (policy vector, value scalar) for each state
using PolicyValueEvaluator = std::function<
    std::vector<std::pair<std::vector<float>, float>>(
        const std::vector<RLGC::GameState>&,  // States to evaluate
        const std::vector<int>&                 // Player indices
    )
>;

// ============================================================================
// MCTS Search
// ============================================================================
class MCTSSearch {
public:
    MCTSSearch(const MCTSConfig& config) : config(config) {}
    
    // Main search function
    // Given a root state, run MCTS and return targets
    MCTSResult Search(
        const RLGC::GameState& rootState,
        int playerIndex,
        PolicyValueEvaluator evaluator
    );
    
private:
    MCTSConfig config;
    
    // Helper: Add Dirichlet noise to root prior (for exploration)
    void AddRootNoise(std::vector<float>& prior);
    
    // Helper: Simulate one environment step
    // (This is the tricky part - we need RLGym integration)
    RLGC::GameState SimulateAction(
        const RLGC::GameState& state,
        int action,
        int playerIndex
    );
    
    // Helper: Check if state is terminal
    bool IsTerminal(const RLGC::GameState& state);
    
    // Helper: Get terminal value
    float GetTerminalValue(const RLGC::GameState& state, int playerIndex);
};

// ============================================================================
// Batched MCTS - Run multiple searches in parallel
// ============================================================================
class BatchedMCTSSearch {
public:
    BatchedMCTSSearch(const MCTSConfig& config) : config(config) {}
    
    // Search multiple states in parallel
    // This is what reanalysis will call
    std::vector<MCTSResult> SearchBatch(
        const std::vector<RLGC::GameState>& states,
        const std::vector<int>& playerIndices,
        PolicyValueEvaluator evaluator
    );
    
private:
    MCTSConfig config;
    std::vector<MCTSSearch> searchers;  // One per thread/state
};

} // namespace GGL
