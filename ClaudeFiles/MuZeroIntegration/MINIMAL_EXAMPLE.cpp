// MINIMAL_EXAMPLE.cpp - 2025-01-11 | Rev 1.0 | NJH
// This shows the CORE integration pattern in simplified form
// Use this as a reference when integrating into Learner.cpp

#include "MCTS/ReanalysisBuffer.h"
#include "MCTS/MCTSSearch.h"

// ============================================================================
// STEP 1: Add to Learner class members
// ============================================================================

class Learner {
private:
    // ... existing members ...
    
    // ADD THESE:
    GGL::ReanalysisBuffer reanalysisBuffer;
    std::vector<GGL::ExperienceSample> tempExperienceBuffer;
    std::vector<GGL::MCTSResult> reanalysisTargets;
    
    // Helper methods
    std::vector<std::pair<std::vector<float>, float>> 
    BatchEvaluate(const std::vector<RLGC::GameState>& states,
                  const std::vector<int>& playerIndices);
    
    void RunReanalysis();
    void UpdateWithReanalysisTargets();
};

// ============================================================================
// STEP 2: Modify rollout loop to collect experience
// ============================================================================

void Learner::CollectRollout() {
    // ... existing rollout code ...
    
    // FOR EACH STEP in the rollout:
    for (int step = 0; step < stepsPerRollout; step++) {
        // ... get state, run policy, take action ...
        
        // ADD THIS: Save to reanalysis buffer
        if (config.reanalysis.enabled) {
            GGL::ExperienceSample sample;
            sample.state = currentState;
            sample.playerIndex = playerIdx;
            sample.action = actionTaken;
            sample.reward = rewardReceived;
            sample.value = valueEstimate;  // from critic
            sample.policy = policyLogits;  // from actor
            sample.timestep = totalSteps;
            
            tempExperienceBuffer.push_back(sample);
        }
    }
    
    // AT END OF ROLLOUT: Move to reanalysis buffer
    if (config.reanalysis.enabled) {
        reanalysisBuffer.Add(tempExperienceBuffer);
        tempExperienceBuffer.clear();
    }
}

// ============================================================================
// STEP 3: Implement BatchEvaluate (calls your neural networks)
// ============================================================================

std::vector<std::pair<std::vector<float>, float>> 
Learner::BatchEvaluate(
    const std::vector<RLGC::GameState>& states,
    const std::vector<int>& playerIndices
) {
    // Convert states to observations
    std::vector<float> allObs;
    for (size_t i = 0; i < states.size(); i++) {
        auto obs = obsBuilder->BuildObs(
            states[i].players[playerIndices[i]], 
            states[i], 
            nullptr  // action - not needed for obs
        );
        allObs.insert(allObs.end(), obs.begin(), obs.end());
    }
    
    // Convert to torch tensor
    int obsSize = obsBuilder->GetObsSize();
    auto obsTensor = torch::from_blob(
        allObs.data(), 
        {(long)states.size(), obsSize}
    ).to(device);
    
    // Run through networks
    torch::NoGradGuard noGrad;
    auto policyLogits = policyNetwork->forward(obsTensor);
    auto values = valueNetwork->forward(obsTensor);
    
    // Convert back to vectors
    std::vector<std::pair<std::vector<float>, float>> results;
    for (size_t i = 0; i < states.size(); i++) {
        std::vector<float> policy(
            policyLogits[i].data_ptr<float>(),
            policyLogits[i].data_ptr<float>() + numActions
        );
        float value = values[i].item<float>();
        results.push_back({policy, value});
    }
    
    return results;
}

// ============================================================================
// STEP 4: Implement RunReanalysis
// ============================================================================

void Learner::RunReanalysis() {
    if (!reanalysisBuffer.CanSample()) {
        return;  // Not enough experience yet
    }
    
    // Sample batch
    auto samples = reanalysisBuffer.Sample(
        config.reanalysis.samplesPerReanalysis
    );
    
    // Extract states
    std::vector<RLGC::GameState> states;
    std::vector<int> playerIndices;
    for (const auto& sample : samples) {
        states.push_back(sample.state);
        playerIndices.push_back(sample.playerIndex);
    }
    
    // Create evaluator
    auto evaluator = [this](
        const std::vector<RLGC::GameState>& states,
        const std::vector<int>& indices
    ) {
        return this->BatchEvaluate(states, indices);
    };
    
    // Run MCTS
    GGL::MCTSConfig mctsConfig;
    mctsConfig.numSimulations = config.reanalysis.mctsSimulations;
    mctsConfig.cPuct = config.reanalysis.mctsCPuct;
    mctsConfig.addRootNoise = config.reanalysis.mctsAddNoise;
    
    GGL::BatchedMCTSSearch searcher(mctsConfig);
    reanalysisTargets = searcher.SearchBatch(states, playerIndices, evaluator);
    
    // Log stats
    float meanValue = 0;
    for (const auto& result : reanalysisTargets) {
        meanValue += result.valueEstimate;
    }
    meanValue /= reanalysisTargets.size();
    
    RG_LOG("Reanalysis: " << samples.size() << " samples, "
           << "mean MCTS value: " << meanValue);
}

// ============================================================================
// STEP 5: Modify PPO update to use reanalysis targets
// ============================================================================

void Learner::UpdatePPO() {
    // ... existing PPO update code ...
    
    // AFTER computing normal PPO loss, ADD THIS:
    
    if (config.reanalysis.enabled && !reanalysisTargets.empty()) {
        // Get current batch predictions
        auto currentPolicy = policyNetwork->forward(obsBatch);
        auto currentValue = valueNetwork->forward(obsBatch);
        
        // Prepare MCTS targets (convert to tensors)
        std::vector<float> mctsVisitFlat;
        std::vector<float> mctsValueFlat;
        
        for (const auto& result : reanalysisTargets) {
            mctsVisitFlat.insert(
                mctsVisitFlat.end(),
                result.visitDistribution.begin(),
                result.visitDistribution.end()
            );
            mctsValueFlat.push_back(result.valueEstimate);
        }
        
        auto mctsVisitTensor = torch::from_blob(
            mctsVisitFlat.data(),
            {(long)reanalysisTargets.size(), numActions}
        ).to(device);
        
        auto mctsValueTensor = torch::from_blob(
            mctsValueFlat.data(),
            {(long)reanalysisTargets.size(), 1}
        ).to(device);
        
        // Compute auxiliary losses
        auto policyReanalysisLoss = torch::kl_div(
            torch::log_softmax(currentPolicy, 1),
            mctsVisitTensor,
            torch::Reduction::Mean
        );
        
        auto valueReanalysisLoss = torch::mse_loss(
            currentValue,
            mctsValueTensor
        );
        
        // Add to total loss
        totalLoss += config.reanalysis.policyReanalysisWeight * policyReanalysisLoss;
        totalLoss += config.reanalysis.valueReanalysisWeight * valueReanalysisLoss;
        
        // Clear for next iteration
        reanalysisTargets.clear();
    }
    
    // ... continue with backward pass ...
}

// ============================================================================
// STEP 6: Call RunReanalysis at right time
// ============================================================================

void Learner::Update() {
    // Collect rollout
    CollectRollout();
    
    // Run reanalysis (BEFORE PPO update so targets are ready)
    if (config.reanalysis.enabled && 
        (updateCount % config.reanalysis.reanalysisInterval == 0)) {
        RunReanalysis();
    }
    
    // PPO update (will use reanalysis targets if available)
    UpdatePPO();
    
    updateCount++;
}

// ============================================================================
// STEP 7: Configuration in ExampleMain.cpp
// ============================================================================

/*
// Add to your ExampleMain.cpp:

cfg.reanalysis.enabled = true;
cfg.reanalysis.bufferSize = 100000;
cfg.reanalysis.minBufferSize = 10000;
cfg.reanalysis.reanalysisInterval = 10;  // Every 10 PPO updates
cfg.reanalysis.samplesPerReanalysis = 1000;

cfg.reanalysis.mctsSimulations = 4;  // START LOW!
cfg.reanalysis.mctsCPuct = 1.25f;
cfg.reanalysis.mctsAddNoise = false;

cfg.reanalysis.policyReanalysisWeight = 0.3f;
cfg.reanalysis.valueReanalysisWeight = 0.3f;
*/

// ============================================================================
// DEBUGGING TIPS
// ============================================================================

/*
1. Add logging at each step:
   - After CollectRollout: "Collected X samples"
   - After RunReanalysis: "Reanalyzed X states, mean value: Y"
   - During PPO update: "Reanalysis loss: policy=X, value=Y"

2. Test incrementally:
   - First: Just collect to buffer (don't run MCTS)
   - Then: Run MCTS (don't use targets)
   - Then: Use targets in loss (check magnitudes)
   - Finally: Tune weights

3. Watch for:
   - Buffer filling (should reach minBufferSize quickly)
   - MCTS values (should be similar to PPO values)
   - Loss magnitudes (shouldn't dominate normal PPO loss)
   - SPS impact (should be minimal if interval is large)

4. If crashes:
   - Check array sizes (visit distribution vs numActions)
   - Check null pointers (states, player indices)
   - Check tensor shapes (batch sizes must match)
*/
