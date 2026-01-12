// MCTSSearch.cpp - 2025-01-11 | Rev 1.0 | NJH
// Core MCTS implementation

#include "MCTSSearch.h"
#include <RLGymCPP/ActionParsers/DefaultAction.h>
#include <algorithm>
#include <random>
#include <cmath>

namespace GGL {

// ============================================================================
// Helper: Add Dirichlet noise (for exploration at root)
// ============================================================================
void MCTSSearch::AddRootNoise(std::vector<float>& prior) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    
    std::gamma_distribution<float> gamma(config.dirichletAlpha, 1.0f);
    
    std::vector<float> noise(prior.size());
    float noiseSum = 0.0f;
    
    for (size_t i = 0; i < prior.size(); i++) {
        noise[i] = gamma(gen);
        noiseSum += noise[i];
    }
    
    // Normalize noise
    for (auto& n : noise) n /= noiseSum;
    
    // Mix with prior
    for (size_t i = 0; i < prior.size(); i++) {
        prior[i] = (1.0f - config.dirichletEpsilon) * prior[i] + 
                   config.dirichletEpsilon * noise[i];
    }
}

// ============================================================================
// Helper: Simulate one action
// ============================================================================
// CRITICAL: This is where we need RLGym integration
// For now, this is a STUB - we'll implement properly after basic structure works
RLGC::GameState MCTSSearch::SimulateAction(
    const RLGC::GameState& state,
    int action,
    int playerIndex
) {
    // TODO: This needs to:
    // 1. Convert action index to actual action
    // 2. Step RocketSim forward
    // 3. Return new state
    
    // For now, return copy (this is WRONG but compiles)
    // We'll fix this in the integration phase
    return state;
}

// ============================================================================
// Helper: Check terminal
// ============================================================================
bool MCTSSearch::IsTerminal(const RLGC::GameState& state) {
    // Terminal if goal scored
    return state.goalScored;
}

// ============================================================================
// Helper: Get terminal value
// ============================================================================
float MCTSSearch::GetTerminalValue(const RLGC::GameState& state, int playerIndex) {
    if (!state.goalScored) return 0.0f;
    
    // Check which team scored
    // This is simplified - needs proper team checking
    int playerTeam = state.players[playerIndex].team;
    
    // Return +1 if our team scored, -1 if opponent scored
    // (Actual logic needs to check who scored)
    return 0.0f;  // TODO: Implement properly
}

// ============================================================================
// Main Search Function
// ============================================================================
MCTSResult MCTSSearch::Search(
    const RLGC::GameState& rootState,
    int playerIndex,
    PolicyValueEvaluator evaluator
) {
    // Step 1: Evaluate root state
    auto rootEval = evaluator({rootState}, {playerIndex})[0];
    auto rootPrior = rootEval.first;
    float rootValue = rootEval.second;
    
    // Step 2: Add exploration noise if configured
    if (config.addRootNoise) {
        AddRootNoise(rootPrior);
    }
    
    // Step 3: Create root node
    auto root = std::make_unique<MCTSNode>(nullptr, -1, rootPrior);
    
    // Step 4: Run simulations
    for (int sim = 0; sim < config.numSimulations; sim++) {
        // === SELECTION: Walk down tree using UCB ===
        MCTSNode* node = root.get();
        RLGC::GameState currentState = rootState;
        std::vector<int> actionPath;  // Track path for simulation
        
        while (node->isExpanded && !node->isTerminal) {
            node = node->SelectChild(config.cPuct);
            if (node == nullptr) break;
            
            actionPath.push_back(node->action);
            currentState = SimulateAction(currentState, node->action, playerIndex);
        }
        
        // === EXPANSION: Create children if not terminal ===
        float value = 0.0f;
        
        if (IsTerminal(currentState)) {
            node->isTerminal = true;
            value = GetTerminalValue(currentState, playerIndex);
            node->terminalValue = value;
        } else if (!node->isExpanded) {
            // Evaluate this position
            auto eval = evaluator({currentState}, {playerIndex})[0];
            auto prior = eval.first;
            value = eval.second;
            
            // Create children
            for (size_t a = 0; a < prior.size(); a++) {
                node->children.push_back(
                    std::make_unique<MCTSNode>(node, a, prior)
                );
            }
            node->isExpanded = true;
        } else {
            value = node->GetMeanValue();
        }
        
        // === BACKUP: Propagate value up tree ===
        node->Backup(value);
    }
    
    // Step 5: Extract results
    MCTSResult result;
    result.visitDistribution = root->GetVisitDistribution();
    result.valueEstimate = root->GetMeanValue();
    result.totalSimulations = config.numSimulations;
    
    // Select action (deterministic: pick most visited)
    result.selectedAction = std::distance(
        result.visitDistribution.begin(),
        std::max_element(result.visitDistribution.begin(), 
                        result.visitDistribution.end())
    );
    
    // Compute mean depth (diagnostic)
    float totalDepth = 0.0f;
    int leafCount = 0;
    
    std::function<void(MCTSNode*, int)> countDepth = [&](MCTSNode* n, int depth) {
        if (n->children.empty()) {
            totalDepth += depth;
            leafCount++;
        } else {
            for (auto& child : n->children) {
                countDepth(child.get(), depth + 1);
            }
        }
    };
    countDepth(root.get(), 0);
    
    result.meanTreeDepth = leafCount > 0 ? totalDepth / leafCount : 0.0f;
    
    return result;
}

// ============================================================================
// Batched Search (parallel searches)
// ============================================================================
std::vector<MCTSResult> BatchedMCTSSearch::SearchBatch(
    const std::vector<RLGC::GameState>& states,
    const std::vector<int>& playerIndices,
    PolicyValueEvaluator evaluator
) {
    std::vector<MCTSResult> results;
    results.reserve(states.size());
    
    // For now: Sequential (no parallelism yet)
    // Later: Use thread pool or OpenMP
    for (size_t i = 0; i < states.size(); i++) {
        MCTSSearch search(config);
        results.push_back(search.Search(states[i], playerIndices[i], evaluator));
    }
    
    return results;
}

} // namespace GGL
