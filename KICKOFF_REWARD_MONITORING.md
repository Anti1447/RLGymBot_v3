# Kickoff Reward Monitoring Guide

## Quick Start - What to Look For

### Expected Reward Signals (Per Kickoff Episode)

```
KickoffStrategyReward:
  ✓ Standard speedflip: 3.0-3.5 base + speed bonus (2000+ UU/s) = 3.2-4.0 total
  ✓ Fake kickoff: 2.0 base (no speed bonus) = 2.0
  ✓ Corner boost grab: 2.0 base = 2.0
  ✓ Demo rush: 3.5 base + speed bonus (if >2000) = 3.5-4.2 total
  ✓ Wavedash into ball: 3.5 base = 3.5 (speed bonus rare)
  ✓ Variation pattern: 2.5 base = 2.5
  ✓ Support safe: 2.0 base = 2.0
  ✓ Support boost: 2.5 base = 2.5
  ✓ Support demo: 3.0 base = 3.0

KickoffRandomizationReward:
  ✓ Novel approach (>15° diff): 0.5-2.0 (scales with novelty %)
  ✓ Repeated approach (±15° tolerance): 0.0
```

### Per-Episode Total
- **5 Kickoffs with mixed strategies**: 12-20 pts expected
- **Repetitive kickoffs**: 15 pts (strategy only, no novelty bonus)
- **Diverse strategies with good execution**: 20-25 pts possible

---

## Monitoring Checklist

### Session Start (First 10 Episodes)
- [ ] Kickoff events are being detected (ball near center, stationary)
- [ ] At least 3-5 different strategy types observed across episodes
- [ ] Reward signals appear in training logs (non-zero values)
- [ ] No crashes or errors during kickoff phase

### Early Training (Episodes 10-50)
- [ ] Agent begins favoring high-reward strategies (standard speedflip, demos)
- [ ] Less frequent use of low-reward strategies (fake, corner boost)
- [ ] Speed bonuses activating (2000+ UU/s detections)
- [ ] Novelty component preventing 100% repetition

### Established Training (Episodes 50-100)
- [ ] Clear strategy preferences based on game situation
- [ ] 1v1 mode: Mix of speedflips (3.5+) and fakes (2.0)
- [ ] 2v2 mode: Varied first player strategies, consistent support play
- [ ] Novelty rewards preventing pure repetition (some variance in approach angles)

---

## Debug Signals - What Means Something's Wrong

| Signal | Cause | Fix |
|--------|-------|-----|
| **0.0 reward all kickoffs** | IsInKickoffPhase() returns false | Verify ball position/speed thresholds |
| **Same strategy every time** | Novelty not working or too loose | Check angle calculation (atan2 with forward/right) |
| **Only fake kickoff rewards** | Other strategies not detected | Check speed thresholds (WAVEDASH 1200-2200, STANDARD 2200+) |
| **Support player getting challenger rewards** | IsFirstPlayer() detection broken | Verify teammate distance comparison logic |
| **High variance in rewards for same play** | Speed bonus threshold unclear | Check if >2000 detection working |
| **Rewards disappear mid-training** | State not resetting properly | Ensure Reset() called on episode boundary |

---

## Reading the Logs

### Console Output Pattern (Expected)
```
Episode 5, Kickoff 1: KickoffStrategyReward=3.2 (STANDARD) KickoffRandomizationReward=1.5
Episode 5, Kickoff 2: KickoffStrategyReward=2.5 (VARIATION) KickoffRandomizationReward=1.2
Episode 5, Kickoff 3: KickoffStrategyReward=3.5 (DEMO) KickoffRandomizationReward=0.8 (repeated)

Total Episode 5: Strategy=9.2 + Randomization=3.5 = 12.7 from kickoffs
```

### Reward Range Validation
- If min < 0: **ERROR** - rewards should never be negative
- If max > 5.0 (strategy) per kickoff: **LIKELY ERROR** - check speed bonus multiplication
- If novelty always 0: **PROBLEM** - all kickoffs are identical (>15° different)
- If novelty always 2.0: **POSSIBLE** - all kickoffs are completely different (good!)

---

## Collecting Data

### Simple Metric Collection Script (Pseudo-Python)
```python
# Track per-episode kickoff statistics
episode_stats = {
    'strategy_total': 0.0,      # Sum of all strategy rewards
    'novelty_total': 0.0,       # Sum of all novelty rewards
    'strategy_count': 0,        # Number of kickoffs with strategy reward
    'strategy_types': {},       # Histogram of strategy types
    'avg_speed': 0.0,           # Average max speed during kickoff
    'support_vs_first': {'support': 0, 'first': 0}  # Role distribution
}

# Run 100 episodes and aggregate
for episode in episodes[0:100]:
    for kickoff in episode.kickoffs:
        episode_stats['strategy_total'] += kickoff.strategy_reward
        episode_stats['novelty_total'] += kickoff.novelty_reward
        episode_stats['strategy_types'][kickoff.strategy_name] += 1
        episode_stats['avg_speed'] += kickoff.max_speed_achieved
```

### Expected Results (100 Episodes)
- **Total strategy rewards**: 1200-1500 pts
- **Total novelty rewards**: 400-600 pts
- **Strategy type distribution**: Reasonably balanced (not 100% one type)
- **Avg max speed**: 2000-2300 UU/s (benefits from speed bonuses)
- **Support vs first player**: ~50/50 in 2v2/3v3

---

## Tuning Parameters

If results don't match expectations, adjust these:

### Increase Strategy Adoption (Too few diverse strategies)
```cpp
// Make fake/variation more rewarding
REWARD_FAKE: 2.0f → 2.5f
REWARD_VARIATION: 2.5f → 3.0f

// OR reduce speed bonus requirement
SPEED_BONUS_THRESHOLD: 2000.0f → 1800.0f
```

### Increase Novelty Enforcement (Too repetitive)
```cpp
// Tighten angle tolerance
ANGLE_SIMILARITY_THRESHOLD: 15.0f → 10.0f  // Must differ by >10°

// Increase novelty reward
BASE_REWARD: 2.0f → 3.0f
```

### Speed Threshold Adjustment
```cpp
// If 90%+ speedflips: increase threshold
STRATEGY_STANDARD: 2200.0f → 2400.0f

// If no speedflips ever: decrease threshold
STRATEGY_STANDARD: 2200.0f → 2000.0f
```

---

## Training Validation Commands

### Check if KickoffStrategyReward is instantiated
```bash
grep -r "KickoffStrategyReward" /c/Projects/RLGymBot_v3/GigaLearnBot/build/
# Should find references in compiled code
```

### Verify reward weights in config
```bash
grep -A2 "KickoffStrategy" /c/Projects/RLGymBot_v3/GigaLearnBot/src/RewardConfig.cpp
# Should show: WeightedReward(new KickoffStrategyReward(), 15.0f)
```

### Monitor training for reward signals
```bash
# Run training and pipe to grep
python train.py --phase PHASE_5_DIAMOND | grep -i "kickoff"
# Should see non-zero kickoff reward values
```

---

## Expected Training Curve

```
Episode 1-10:   [Exploring all strategies equally] ≈ 12-15 pts/episode
Episode 11-30:  [Gravitating toward high-reward strategies] ≈ 13-18 pts/episode
Episode 31-50:  [Balanced mix learned] ≈ 15-20 pts/episode
Episode 51-100: [Consistent strategic diversity] ≈ 18-25 pts/episode
```

If you see:
- ↗️ Upward trend: **GOOD** - learning to maximize rewards
- ↘️ Downward trend: **PROBLEM** - reward shaping incentivizing bad behavior
- ↗️ Then plateau: **EXPECTED** - converged on near-optimal strategy mix

---

## Integration Success Indicators

✅ **SUCCESS CRITERIA** (All of these should be true after 100 episodes):
1. KickoffStrategyReward averaging 3.5+ per kickoff (not 0)
2. Multiple strategy types present (>3 different types observed)
3. Speed bonuses visible (some rewards >3.5)
4. Novelty rewards active (some rewards from randomization component)
5. No crashes or undefined behavior
6. Agent still learning other skills (not overfitting to kickoff)

---

## Common Issues & Solutions

### Issue: All Rewards Are 0
**Solution**:
- Check that ball is actually in kickoff zone (Y near 0, within 300 units)
- Verify ball speed < 500 when phase starts
- Add debug logging to IsInKickoffPhase() function

### Issue: Only One Strategy Ever Rewarded
**Solution**:
- Check speed thresholds - may be filtering out other strategies
- Verify IsFirstPlayer() detection working (should alternate roles in 2v2/3v3)
- Confirm strategy detection logic executing (add debug logs)

### Issue: Rewards Too High/Low
**Solution**:
- Current weights: Strategy=15.0, Randomization=8.0
- If too high: reduce to 10.0 and 5.0
- If too low: increase to 20.0 and 12.0
- Adjust in RewardConfig.cpp, line ~458

### Issue: No Progress/Learning
**Solution**:
- Verify agent is even in kickoff phase (log IsInKickoffPhase calls)
- Check that player.carId is being tracked correctly
- Ensure Reset() is called between episodes
- Monitor if reward signals only appear once per episode (expected)

---

**Last Updated**: 2026-02-09
**Build Status**: ✅ Verified Compiling (Exit 0)
