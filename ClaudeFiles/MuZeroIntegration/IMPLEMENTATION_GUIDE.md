# MuZero Reanalysis Implementation Guide
# 2025-01-11 | Rev 1.0 | NJH

## Overview

This guide walks through implementing MuZero-style reanalysis in GigaLearnCPP.
We'll use Claude Code to help with the implementation.

## Phase 1: File Setup (Manual + Claude Code)

### Step 1.1: Create Directory Structure

```bash
# Run in terminal (from GigaLearnCPP root)
cd GigaLearnCPP/src/private/GigaLearnCPP
mkdir MCTS
cd MCTS
```

### Step 1.2: Copy Files

Copy these files from /tmp to the MCTS directory:
- MCTSNode.h
- MCTSSearch.h
- MCTSSearch.cpp
- ReanalysisBuffer.h

### Step 1.3: Update CMakeLists.txt

**Using Claude Code:**

1. Open `GigaLearnCPP/CMakeLists.txt`
2. Ask Claude Code:
   ```
   Add MCTS source files to the build:
   - src/private/GigaLearnCPP/MCTS/MCTSSearch.cpp
   
   Find the existing add_library() or target_sources() call
   and add this file to the list.
   ```

## Phase 2: Integrate into Learner (Claude Code Heavy)

### Step 2.1: Extend LearnerConfig.h

**Claude Code Prompt:**
```
I need to add reanalysis configuration to LearnerConfig.h.
Look at the file /tmp/LearnerConfig_Extension.h for the 
structure to add. Add this as a new member struct inside 
the LearnerConfig struct, after the existing members.
```

### Step 2.2: Create ReanalysisPPO.h

**Claude Code Prompt:**
```
Create a new file: GigaLearnCPP/src/private/GigaLearnCPP/PPO/ReanalysisPPO.h

This should extend the existing PPO implementation with reanalysis.
Requirements:
1. Include ReanalysisBuffer
2. Include MCTSSearch
3. Add methods:
   - void RunReanalysis() 
   - void UpdateWithReanalysisTargets()
4. Add member variables:
   - ReanalysisBuffer buffer
   - BatchedMCTSSearch searcher

Base this on the existing PPO/PPO.h structure.
```

### Step 2.3: Integrate into Learner::Update()

**Claude Code Prompt:**
```
I need to modify Learner.cpp to add reanalysis.

Find the Learner::Update() method (or wherever PPO updates happen).

After the normal PPO update, add:

if (config.reanalysis.enabled && 
    totalSteps % config.reanalysis.reanalysisInterval == 0) {
    RunReanalysis();
}

You'll need to:
1. Add #include "../PPO/ReanalysisPPO.h" at top
2. Add ReanalysisBuffer as a member variable
3. Implement RunReanalysis() method
```

## Phase 3: Implement Core Reanalysis Logic

### Step 3.1: Fill in RunReanalysis() Method

**File to create:** `Learner.cpp` (modification)

**Claude Code Prompt:**
```
Implement the Learner::RunReanalysis() method:

void Learner::RunReanalysis() {
    // 1. Sample from buffer
    auto samples = reanalysisBuffer.Sample(
        config.reanalysis.samplesPerReanalysis
    );
    
    if (samples.empty()) return;
    
    // 2. Extract states and player indices
    std::vector<RLGC::GameState> states;
    std::vector<int> playerIndices;
    for (const auto& sample : samples) {
        states.push_back(sample.state);
        playerIndices.push_back(sample.playerIndex);
    }
    
    // 3. Run MCTS on all samples
    auto evaluator = [this](const std::vector<RLGC::GameState>& states,
                           const std::vector<int>& indices) {
        // Batch evaluate states using PPO policy/value networks
        return this->BatchEvaluate(states, indices);
    };
    
    BatchedMCTSSearch searcher(/* config */);
    auto results = searcher.SearchBatch(states, playerIndices, evaluator);
    
    // 4. Store results for next PPO update
    reanalysisTargets = results;
}
```

This requires implementing BatchEvaluate() - we'll do that next.
```

### Step 3.2: Implement BatchEvaluate()

**Claude Code Prompt:**
```
I need to implement batch evaluation of states for MCTS.

Create a method in Learner.cpp:

std::vector<std::pair<std::vector<float>, float>> 
Learner::BatchEvaluate(
    const std::vector<RLGC::GameState>& states,
    const std::vector<int>& playerIndices
) {
    // This should:
    // 1. Convert states to observations using obsBuilder
    // 2. Convert to torch tensors
    // 3. Run through policy + value networks
    // 4. Return (policy logits, value) for each state
    
    // Look at existing PPO rollout code for reference
    // on how observations are processed
}
```

### Step 3.3: Fix SimulateAction() Stub

This is the trickiest part. We need to step RocketSim forward.

**Claude Code Prompt:**
```
In MCTSSearch.cpp, I need to implement SimulateAction().

This should:
1. Take current GameState
2. Apply action for one player
3. Step RocketSim forward by tickSkip ticks
4. Return new GameState

Look at how RLGymCPP steps the environment in the main rollout loop.
We need to replicate that but for a single action.

Key challenge: MCTS needs to simulate OPPONENT actions too.
For now, we can:
- Use previous opponent action (stored in GameState)
- Or use policy network to sample opponent action
- Or assume opponent does nothing (fastest but wrong)

Start with "opponent does nothing" to get it working.
```

## Phase 4: Add Experience Collection

### Step 4.1: Modify Rollout to Save Experience

**Claude Code Prompt:**
```
Find where PPO collects rollouts (probably in Learner.cpp or PPO.cpp).

After each step, we need to save to reanalysis buffer:

if (config.reanalysis.enabled) {
    ExperienceSample sample;
    sample.state = state;
    sample.playerIndex = playerIdx;
    sample.action = action;
    sample.reward = reward;
    sample.value = valuePrediction;
    sample.policy = policyLogits;
    sample.timestep = totalSteps;
    
    tempExperienceBuffer.push_back(sample);
}

Then at end of rollout:
reanalysisBuffer.Add(tempExperienceBuffer);
tempExperienceBuffer.clear();
```

## Phase 5: Add Reanalysis Loss

### Step 5.1: Modify PPO Loss Computation

**Claude Code Prompt:**
```
In the PPO update loop, after computing normal PPO loss,
add auxiliary reanalysis loss:

if (hasReanalysisTargets) {
    // Policy loss: KL divergence to MCTS visit distribution
    auto policyReanalysisLoss = torch::kl_div(
        policyLogProbs,           // Current policy
        mctsVisitDistribution,    // MCTS target
        torch::kl_div_reduction::batchmean
    );
    
    // Value loss: MSE to MCTS value estimate
    auto valueReanalysisLoss = torch::mse_loss(
        valuePredictions,         // Current value
        mctsValueTargets          // MCTS target
    );
    
    // Mix into total loss
    totalLoss += config.reanalysis.policyReanalysisWeight * policyReanalysisLoss;
    totalLoss += config.reanalysis.valueReanalysisWeight * valueReanalysisLoss;
}
```

## Phase 6: Testing & Debugging

### Test 1: Compile

```bash
cd build
cmake ..
make -j8
```

**If errors:** Use Claude Code to fix them by showing error message.

### Test 2: Run with Reanalysis Disabled

```cpp
// In ExampleMain.cpp
cfg.reanalysis.enabled = false;  // Should work exactly as before
```

### Test 3: Enable Reanalysis with Minimal Settings

```cpp
cfg.reanalysis.enabled = true;
cfg.reanalysis.mctsSimulations = 2;  // VERY LOW
cfg.reanalysis.reanalysisInterval = 100;  // Infrequent
cfg.reanalysis.samplesPerReanalysis = 100;  // Small batch
```

Watch for:
- Compile errors → Claude Code fix
- Runtime crashes → Show stack trace to Claude Code
- Slow performance → Profile, adjust config

### Test 4: Gradually Increase

```cpp
cfg.reanalysis.mctsSimulations = 4;
cfg.reanalysis.reanalysisInterval = 10;
cfg.reanalysis.samplesPerReanalysis = 1000;
```

Monitor:
- SPS impact (should be minimal if offline)
- GPU utilization (should increase during reanalysis)
- Skill improvement (compare with/without over same wall-clock time)

## Phase 7: Optimization (After Basic Version Works)

### Optimization 1: Parallel MCTS

**Claude Code Prompt:**
```
Modify BatchedMCTSSearch::SearchBatch() to use OpenMP
or thread pool for parallel searches.

Each search is independent, so this should be easy parallelism.
```

### Optimization 2: Batched Inference in MCTS

Currently each MCTS node evaluation calls NN separately.
We should batch them:

**Claude Code Prompt:**
```
Modify MCTSSearch to collect all leaf nodes,
then evaluate them in one batch call.

This requires restructuring the search loop to:
1. Selection phase (collect all leaf nodes)
2. Batch evaluate all leaves
3. Expansion phase (create children)
4. Backup phase (propagate values)

This is a significant refactor but gives huge speedup.
```

### Optimization 3: Virtual Loss

For parallel MCTS, add virtual loss to prevent all threads
from exploring the same path:

**Claude Code Prompt:**
```
In MCTSNode::Backup(), add virtual loss support:

if (config.virtualLoss > 0) {
    // Temporarily reduce value to discourage re-selection
    node->totalValue -= config.virtualLoss;
}

This gets removed when backup completes.
```

## Common Issues & Solutions

### Issue 1: "Cannot convert GameState to Tensor"

**Solution:** Look at existing observation builder usage.
You need `obsBuilder->BuildObs()` call.

### Issue 2: "MCTS is way too slow"

**Solutions:**
1. Reduce numSimulations (start at 2-4)
2. Reduce samplesPerReanalysis
3. Increase reanalysisInterval
4. Implement batched inference
5. Run reanalysis on separate thread

### Issue 3: "Model doesn't improve with reanalysis"

**Debugging:**
1. Print MCTS value vs PPO value (should be similar)
2. Print visit distribution vs policy (should align)
3. Check reanalysis loss magnitude (should be non-zero)
4. Verify targets are actually being used in loss

**Claude Code Prompt for debugging:**
```
Add logging to RunReanalysis():

RG_LOG("Reanalysis: " << samples.size() << " samples");
RG_LOG("Mean MCTS value: " << meanValue);
RG_LOG("Mean PPO value: " << meanPPOValue);
RG_LOG("Mean visit entropy: " << visitEntropy);
```

## Success Metrics

Track these in your Report:

```cpp
report.AddAvg("Reanalysis/SamplesPerStep", ...);
report.AddAvg("Reanalysis/MCTSValueMean", ...);
report.AddAvg("Reanalysis/PolicyKL", ...);  // KL between MCTS and PPO
report.AddAvg("Reanalysis/TimeSeconds", ...);
report.AddAvg("Reanalysis/SPSImpact", ...);  // % SPS reduction
```

Compare:
- With reanalysis ON vs OFF
- Same total steps
- Measure skill improvement per wall-clock hour

## Expected Timeline

- **Day 1-2:** Get it compiling
- **Day 3-4:** Get basic version running (even if slow)
- **Day 5-7:** Optimize and tune
- **Week 2:** Validate skill improvement

## Getting Help from Claude Code

### Good Prompts:

✅ "Show me how X is done in the existing codebase"
✅ "Fix this compilation error: [paste error]"
✅ "Add this functionality based on this existing pattern"
✅ "Review this code for thread safety issues"

### Bad Prompts:

❌ "Write the whole thing for me"
❌ "Make it fast" (be specific about what's slow)
❌ "Fix all bugs" (show specific bug)

### Workflow:

1. **Make small changes**
2. **Compile after each change**
3. **If error, show Claude Code the exact error**
4. **Test incrementally**

## Next Steps

Start with Phase 1 (file setup).
Then work through Phase 2-3 to get basic integration.
Don't worry about optimization until basic version works.

Come back to this guide when you hit issues.
Each phase has Claude Code prompts to help.
