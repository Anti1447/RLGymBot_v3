# Reanalysis Implementation Checklist
# Track your progress through the implementation

## Phase 1: File Setup ⬜
- [ ] Create `MCTS/` directory in `src/private/GigaLearnCPP/`
- [ ] Copy MCTSNode.h to MCTS/
- [ ] Copy MCTSSearch.h to MCTS/
- [ ] Copy MCTSSearch.cpp to MCTS/
- [ ] Copy ReanalysisBuffer.h to MCTS/
- [ ] Update CMakeLists.txt to include MCTSSearch.cpp
- [ ] Test compile (should succeed even though files aren't used yet)

## Phase 2: Configuration ⬜
- [ ] Add reanalysis config to LearnerConfig.h (see LearnerConfig_Extension.h)
- [ ] Test compile
- [ ] Add reanalysis settings to ExampleMain.cpp (disabled for now)
- [ ] Test compile & run (should work exactly as before)

## Phase 3: Buffer Integration ⬜
- [ ] Add `ReanalysisBuffer reanalysisBuffer;` member to Learner class
- [ ] Initialize buffer in Learner constructor
- [ ] Add experience collection in rollout loop
- [ ] Add `std::vector<ExperienceSample> tempExperienceBuffer;` member
- [ ] After each step: save to tempExperienceBuffer
- [ ] After each rollout: `reanalysisBuffer.Add(tempExperienceBuffer)`
- [ ] Test compile & run with reanalysis.enabled = false
- [ ] Enable reanalysis, verify buffer fills (add logging)

## Phase 4: MCTS Integration ⬜
- [ ] Implement `Learner::BatchEvaluate()` method
  - [ ] Convert states to observations
  - [ ] Run through policy/value networks
  - [ ] Return results
- [ ] Test BatchEvaluate() standalone (just call it, ignore results)
- [ ] Implement `Learner::RunReanalysis()` method
  - [ ] Sample from buffer
  - [ ] Call BatchedMCTSSearch
  - [ ] Store results
- [ ] Add RunReanalysis() call in Learner::Update()
- [ ] Test compile

## Phase 5: MCTS Environment Stepping ⬜
- [ ] Implement `MCTSSearch::SimulateAction()`
  - [ ] Start with "opponent does nothing" version
  - [ ] Convert action index to actual action
  - [ ] Step RocketSim forward
  - [ ] Return new state
- [ ] Implement `MCTSSearch::IsTerminal()`
- [ ] Implement `MCTSSearch::GetTerminalValue()`
- [ ] Test compile & run (MCTS will run but might give poor results)

## Phase 6: Loss Integration ⬜
- [ ] Store MCTS results in Learner class
- [ ] Modify PPO loss to include reanalysis targets
  - [ ] Policy KL loss vs MCTS visits
  - [ ] Value MSE loss vs MCTS values
  - [ ] Mix with PPO loss using config weights
- [ ] Test compile & run
- [ ] Verify losses are non-zero
- [ ] Add logging for reanalysis metrics

## Phase 7: Initial Testing ⬜
- [ ] Run with minimal settings (2 sims, interval=100)
- [ ] Verify no crashes
- [ ] Verify SPS doesn't drop too much (<10% drop)
- [ ] Add Report metrics for reanalysis
- [ ] Compare training curves with/without reanalysis

## Phase 8: Tuning ⬜
- [ ] Gradually increase simulations (2→4→8)
- [ ] Tune reanalysis interval
- [ ] Tune sample batch size
- [ ] Tune loss weights
- [ ] Monitor skill improvement vs wall-clock time

## Phase 9: Optimization (Optional) ⬜
- [ ] Parallel MCTS searches (OpenMP/threads)
- [ ] Batched inference in MCTS
- [ ] Virtual loss for parallel search
- [ ] Async reanalysis (separate thread)

## Debug Checklist (when things break)

If crash:
- [ ] Get stack trace
- [ ] Show to Claude Code
- [ ] Add null checks
- [ ] Check array bounds

If compile error:
- [ ] Copy full error message
- [ ] Show to Claude Code
- [ ] Check include paths
- [ ] Check missing dependencies

If slow:
- [ ] Profile which phase is slow
- [ ] Reduce MCTS sims temporarily
- [ ] Check if running during rollout (should be after)
- [ ] Verify batched inference is working

If not learning:
- [ ] Print MCTS value estimates
- [ ] Print visit distributions
- [ ] Verify targets are being used
- [ ] Check loss weights aren't zero
- [ ] Compare to baseline PPO

## Current Status

Date started: __________
Current phase: __________
Blockers: __________

## Notes

