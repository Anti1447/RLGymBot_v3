# Automated Reward Engineering - Quick Start Guide

## Vision

Transform yourself from being the bottleneck in four roles:
1. **Running training** → Automated training loop
2. **Coding new rewards** → Claude generates code
3. **Debugging rewards** → Heuristics detect problems automatically
4. **Balancing weights** → Genetic algorithm optimizes

Into an **oversight role** where you:
- Monitor system health
- Validate big decisions
- Adjust thresholds when needed
- Analyze what emerged from automation

**Expected outcome**: Reduce active work from 40+ hours/phase to <5 hours/phase

---

## Implementation Roadmap

### Phase 1: Metrics Collection (Week 1)

**Goal**: Add detailed per-reward metrics logging to your training loop

**Tasks**:
1. Hook metrics collector into your `GigaLearnCPP/Learner.cpp` training loop
2. Every N steps (suggest 10M), sample each reward and record:
   - Activation count (how many times fired)
   - Mean value (average reward)
   - Variance (smoothed std deviation)
   - Correlation with win rate (r² of reward vs episode outcome)

3. Write metrics to JSON file every B steps for Python to read

**Code integration point**:
```cpp
// In Learner::TrainingStep()
if (stepCount % METRICS_SAMPLE_INTERVAL == 0) {
    metricsCollector.RecordRewardMetrics(
        currentRewards,
        currentGameState,
        latestEpisodeWon
    );
}

if (stepCount % METRICS_FLUSH_INTERVAL == 0) {
    metricsCollector.WriteToFile("metrics.json");
}
```

**Success criteria**: 
- Can read 50+ reward metrics from a training session
- Metrics match manual inspection (activation rates make sense)
- No performance impact on training (<1% slowdown)

---

### Phase 2: Heuristic Diagnostician (Week 1-2)

**Goal**: Automatically identify which rewards are broken

**Use the provided `RewardDiagnostician.h`** - it implements 6 core rules:

1. **LOW_ACTIVATION**: Reward never fires
   - Threshold: activation < 5% AND weight < 0.5
   - Fix: Increase sensitivity or weight

2. **HIGH_VARIANCE**: Reward signal is too noisy
   - Threshold: variance > 0.6 AND mean < 0.5
   - Fix: Add smoothing or scaling

3. **CONFLICTING_WITH_OTHER**: Fighting another reward
   - Threshold: correlation < -0.5 with another high-weight reward
   - Fix: Add state gating to make mutually exclusive

4. **POOR_CORRELATION**: Fires but doesn't help win
   - Threshold: activation > 10% but correlation < 0.15
   - Fix: Retarget the behavior

5. **INCORRECT_SCALING**: Weight doesn't match value
   - Threshold: good reward with weight < 1.0 OR bad reward with weight > 20
   - Fix: Adjust weight

6. **GRADIENT_DILUTION**: Too many rewards competing
   - Threshold: >35 active rewards out of 40+
   - Fix: Gate some rewards or use GA

**Integration**:
```python
# In orchestrator.py
diagnostician = RewardDiagnostician()
diagnoses = diagnostician.DiagnoseAllRewards(metrics, weights)
for diagnosis in diagnoses:
    if diagnosis.severity > 0.5:
        # This reward needs attention
```

**Success criteria**:
- Correctly identifies 4-5 known problem rewards when you feed in test metrics
- No false positives on healthy rewards
- Heuristics are tunable (you can adjust thresholds)

---

### Phase 3: LLM Integration (Week 2-3)

**Goal**: Generate correct C++ code for reward fixes

**Requirements**:
- Anthropic API key set as `ANTHROPIC_API_KEY` env variable
- Claude API v1/messages endpoint access

**Use the provided `LLMRewardGenerator` class** in `automated_orchestrator.py`

**Test first with simple prompts**:
```python
generator = LLMRewardGenerator(model="claude-opus-4-5-20250514")

# Test 1: Ask Claude to fix a threshold
code, compiles = await generator.fix_reward(
    diagnosis=RewardDiagnosis(
        reward_name="DemoReward",
        problem_type=RewardProblemType.LOW_ACTIVATION,
        ...
    ),
    metrics=RewardMetrics(...),
    existing_code=open("DemoReward.h").read(),
    similar_working_rewards=["BumpReward"]
)
```

**Success criteria**:
- Claude generates syntactically valid C++
- Generated code is reasonable (matches constraints)
- Code doesn't have obvious bugs (uses right API)

---

### Phase 4: Compilation Testing (Week 3)

**Goal**: Validate generated code actually compiles

**Implementation**:
1. Write generated code to temp file
2. Try to compile it with your project's cmake
3. Parse compiler errors

```cpp
// In C++
bool TestCompileCode(const std::string& code, const std::string& name) {
    std::string tmpFile = fmt::format("/tmp/test_{}.h", name);
    
    // Write code
    std::ofstream out(tmpFile);
    out << code;
    out.close();
    
    // Try to compile with your build system
    int result = system(
        fmt::format(
            "cd build && cmake .. -DTEST_REWARD_FILE={} && "
            "cmake --build . --target compile_test 2>&1",
            tmpFile
        ).c_str()
    );
    
    return result == 0;
}
```

**Success criteria**:
- Can detect when generated code has compile errors
- Can detect when code is syntactically valid but semantically wrong

---

### Phase 5: Runtime Validation (Week 3-4)

**Goal**: Test generated rewards before adding to training

**Implementation**:
1. Create temporary reward in running training
2. Test on 50-100 episodes in isolation
3. Compare to baseline reward
4. Check for crashes, NaN values, performance

```cpp
struct ValidationResult {
    bool crashesSafely;
    std::vector<float> outputValues;
    float deltaWinRate;
    float latency;
};

ValidationResult ValidateReward(
    const Reward* newReward,
    int testEpisodes = 50
) {
    // Run episodes, measure performance
    // Compare to baseline
    // Return metrics
}
```

**Success criteria**:
- Can safely run untested rewards without crashing main training
- Detects when a reward produces invalid outputs (NaN, Inf)
- Measures actual win rate impact

---

### Phase 6: Full Orchestration (Week 4+)

**Goal**: Run the complete closed-loop system

**Architecture**:
```
Training Loop (C++)
      ↓
Metrics Collection (C++)
      ↓
Write metrics.json
      ↓
Python Orchestrator reads metrics
      ↓
Diagnostician identifies problems
      ↓
For each high-severity problem:
  ├→ LLM generates fix
  ├→ Compilation test
  ├→ Runtime validation
  └→ Deploy if valid
      ↓
For each skill gap (every 5 iterations):
  ├→ LLM generates new reward
  ├→ Compilation test
  ├→ Runtime validation
  └→ Add to config if valid
      ↓
GA optimizes weights
      ↓
Check end-of-phase condition
      ↓
Loop or exit
```

**Run**:
```bash
export ANTHROPIC_API_KEY="sk-..."
python3 automated_orchestrator.py --phase PHASE_4_PLATINUM
```

---

## Quick Integration Checklist

### For Your C++ Codebase

- [ ] Add `RewardMetricsCollector` to training loop
  - [ ] Hook into each reward evaluation
  - [ ] Sample metrics every 10M steps
  - [ ] Write JSON file every 1B steps

- [ ] Create `TestCompileCode()` function
  - [ ] Writes code to temp file
  - [ ] Invokes cmake compilation
  - [ ] Parses errors

- [ ] Create `ValidateReward()` function
  - [ ] Runs isolated test episodes
  - [ ] Measures win rate impact
  - [ ] Detects NaN/crashes

### For Your Python Environment

- [ ] Install dependencies: `pip install anthropic`
- [ ] Set `ANTHROPIC_API_KEY` environment variable
- [ ] Copy `automated_orchestrator.py` to your project
- [ ] Update paths to match your setup:
  - [ ] `training_executable` path
  - [ ] `metrics_file` path
  - [ ] Reward code paths

### For Your Workflow

- [ ] Run orchestrator in separate terminal/tmux session
- [ ] Monitor logs: `tail -f training_automation.log`
- [ ] Check `metrics.json` periodically
- [ ] Review generated rewards before they deploy

---

## Safety Guardrails

The system has built-in safety limits to prevent runaway behavior:

```python
# In AutomatedRewardOrchestrator
SAFETY_LIMITS = {
    "max_rewards_created_per_phase": 10,
    "max_weight_change_per_iteration": 0.3,
    "max_new_reward_weight": 10.0,
    "win_rate_drop_threshold": 0.05,  # Abort if drop >5%
    "compilation_success_rate_min": 0.7,  # Abort if <70% compile
    "validation_success_rate_min": 0.5,   # Abort if <50% improve perf
}
```

If any limit is exceeded, the system **stops and alerts** rather than continuing.

---

## Monitoring Dashboard

What you should check regularly:

```
=== SYSTEM HEALTH ===
Compilation Success Rate:    85% ✓
Validation Success Rate:     72% ✓
Rewards Created This Phase:  3/10
Rewards Fixed This Phase:    7/8
Win Rate Trend:              ↗ +3.2% over 5 iterations

=== RECENT ACTIONS ===
[09:45] Fixed DemoReward - was too noisy
[09:30] Created BetterAerialAccuracy reward
[09:15] Weights optimized via GA
[09:00] Detected conflict between DribbleReward and DefenseReward

=== NEXT ACTIONS QUEUED ===
- Check if RecoveryReward should be adjusted (low correlation)
- Consider splitting AerialReward into specialized variants
```

---

## Troubleshooting

### "Generated code doesn't compile"
- Check Claude's output format (must be valid C++)
- Review the reward pattern it's trying to match
- Add more similar working examples to prompt
- Try a simpler fix first

### "Validation shows improvement but real training gets worse"
- Validation may be too short (try 100 episodes instead of 50)
- Reward might have side effects not captured in isolated test
- Weight may be too high relative to other rewards
- Use GA to rebalance weights

### "Heuristics are detecting false positives"
- Adjust thresholds in `RewardDiagnostician`
- Add more nuanced rules (e.g., check if recently added)
- Consider reward interaction patterns

### "System created a reward but it's making training worse"
- Lower `max_new_reward_weight` in config
- Require higher validation threshold
- Add a "rollback if win rate drops >X%" feature

---

## Expected Timeline

| Week | Deliverable | Effort | Risk |
|------|-------------|--------|------|
| 1 | Metrics collection working | 6-8h | Low |
| 2 | Heuristics detecting 80% of issues | 4-6h | Low |
| 3 | LLM generating valid code | 8-10h | Med |
| 4 | Full system running hands-off | 4-6h | Med |

**Total**: ~25-30 hours of development work over 4 weeks

**Payoff**: Save 35-40 hours/phase × 3+ phases/year = 100+ hours/year

---

## Next Steps

1. **Start with Phase 1**: Add metrics collection
   - Easiest integration point
   - Gets you data for testing
   - No risk to training

2. **Test heuristics offline**
   - Run diagnostician on your current Phase 5 metrics
   - Verify diagnoses match your intuition
   - Adjust thresholds

3. **Generate code with Claude manually**
   - Take a known problem reward
   - Use RewardPromptGenerator to create prompt
   - Paste into Claude.ai
   - See what it generates
   - Validate it makes sense

4. **Build the validation framework**
   - Most critical safety layer
   - Prevents broken code from breaking training

5. **Put it all together**
   - Run on test phase first
   - Monitor closely
   - Adjust thresholds
   - Iterate

---

## Questions to Consider

- **How much should I trust the LLM's code?**
  - Not at all until it passes compilation + validation
  - Always have compilation test before runtime test
  - Validation must show actual improvement

- **What if the system generates many bad rewards?**
  - Reduce `max_new_reward_weight`
  - Increase validation episode count
  - Lower `new_reward_severity_threshold`
  - Require more similarity to working examples

- **Should I keep manually tuning weights?**
  - No - GA handles this better than human intuition
  - But review top rewards to understand decisions
  - Manually override only for strategic shifts

- **What happens if I run this continuously across all phases?**
  - It should work! That's the point
  - System will find and fix problems autonomously
  - You move to strategy role (what to train next)

---

## Success Metrics

You'll know the system is working when:

1. ✓ Training runs for 2B steps unattended
2. ✓ Metrics detect real problems (you verify)
3. ✓ LLM generates >70% valid fixes
4. ✓ Validation catches >90% of bad rewards
5. ✓ Win rate continues improving despite no manual intervention
6. ✓ You spend <1 hour/week monitoring

