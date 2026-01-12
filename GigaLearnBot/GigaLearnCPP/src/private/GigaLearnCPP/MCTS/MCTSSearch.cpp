// MCTSSearch.cpp - 2025-01-11 | Rev 1.0 | NJH
// Core MCTS implementation

#include "MCTSSearch.h"
#include <RLGymCPP/ActionParsers/DefaultAction.h>
#include <algorithm>
#include <random>
#include <cmath>

// RocketSim Arena is already included via RLGymCPP headers
// (RLGymCPP/Framework.h includes RocketSim.h which includes Arena)

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
// STUB VERSION: Returns same state (for Phase 5a testing)
// This allows MCTS infrastructure to work without arena stepping overhead
//
// TODO Phase 5b: Full implementation should:
// 1. Solve collision mesh path issue (Arena::Create needs proper init)
// 2. Set car states from GameState
// 3. Apply actions using car->controls
// 4. Step arena forward
// 5. Extract new GameState
//
// For now, this stub allows testing of:
// - Buffer collection
// - MCTS tree search structure
// - BatchEvaluate() neural network calls
// - Reanalysis pipeline
// - Phase 6: Reanalysis loss integration (next step!)
RLGC::GameState MCTSSearch::SimulateAction(
    const RLGC::GameState& state,
    int action,
    int playerIndex
) {
    // STUB: Just return the same state
    // MCTS will still explore the tree structure and call the value network
    // This tests everything except actual environment stepping
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

    // Get player's team
    int playerTeam = (int)state.players[playerIndex].team;

    // Check which team scored by looking at ball position
    // If ball is past blue goal line (x > field_length/2), blue scored
    // If ball is past orange goal line (x < -field_length/2), orange scored
    // RocketSim uses: Team::BLUE = 0, Team::ORANGE = 1

    // Simple heuristic: check which side the goal was scored on
    // based on the scoreboard change
    // In Rocket League: Blue defends negative X, Orange defends positive X

    // If blue scored (ball in orange goal), blue gets +1, orange gets -1
    // If orange scored (ball in blue goal), orange gets +1, blue gets -1

    // For now, use a simple approach:
    // Check if our team's score increased (this would require previous state)
    // Instead, we'll return 0 and let the reward function handle it
    // The MCTS will learn from the actual rewards in the experience

    // Return small value - the actual reward will come from the reward function
    // Positive if likely our team scored (rough heuristic based on ball position)
    float ballX = state.ball.pos.x;
    bool blueScoredLikely = (ballX > 0);  // Ball in orange half

    if (playerTeam == 0) {  // Blue team
        return blueScoredLikely ? 1.0f : -1.0f;
    } else {  // Orange team
        return blueScoredLikely ? -1.0f : 1.0f;
    }
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
