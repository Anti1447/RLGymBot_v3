# NumericalAdvantageAttackingReward Quick Guide

## What Is This Reward?

A specialized reward that makes your bots **much more aggressive in 2v1, 3v2, and other numerical advantage situations**. When bots have more players near the opponent's goal than defenders are present, this reward teaches them to:

1. **Demo/Bump the defender** while teammate shoots
2. **Pass for high-velocity goals** with teammate
3. **Aggressively push the advantage** instead of retreating

## How It Works (Simple Version)

### Detecting Advantages
- Counts attacking players closer to opponent goal
- Counts defending players in defensive positions
- Activates when: attackers >= defenders + 1
- Examples: 2v1, 3v2, 4v3, etc.

### Demo Path (Most Common)
```
1. Primary attacker (closest to ball) → Positions for demo (< 1500 UU)
2. Primary → Successfully demos/bumps defender
3. Support attacker → Shoots within 2 seconds
4. Both players get rewarded equally
```

### Passing Path (Alternative)
```
1. Primary attacker → Gets ball in passing zone (1500-3500 UU from goal)
2. Primary → Makes high-velocity pass (> 500 UU/s) to teammate
3. Support → Positioned for reception (1000-2500 UU from goal)
4. Support → Shoots within 2 seconds
5. Both get rewarded equally
```

### General Aggression
- Players in attacking zone get +0.1f per frame
- Reward scales with advantage size (bigger advantage = higher reward)
- Retreating during advantage gets -0.05f penalty

## Reward Magnitudes

| Event | Reward | Timing |
|-------|--------|--------|
| Demo/Bump executed | +0.4f | Immediate |
| Post-demo shot | +0.5f | Within ~1 second |
| High-quality pass | +0.4f | Immediate |
| Post-pass shot | +0.5f | Within ~1 second |
| Attacking zone presence | +0.1f | Per frame |
| Retreat penalty | -0.05f | Per frame |

## Phase Integration

| Phase | Weight | When It Activates |
|-------|--------|-------------------|
| Gold | 4.0x | Phase 3+ |
| Platinum | 5.0x | Phase 4+ |
| Diamond | 6.0x | Phase 5+ |

## Configuration Constants

Edit `ShootingRewards.h` line ~311 to adjust behavior:

```cpp
DEMO_RANGE = 1500.0f                  // How close for demo attempt
PASS_QUALITY_THRESHOLD = 500.0f       // Min UU/s for "quality" pass
ATTACKING_ZONE_DIST = 5000.0f        // Max distance from goal to count as attacking
POST_ACTION_WINDOW = 120              // Frames (~1 sec) to reward followup shot
PASS_POSITION_MIN/MAX = 1500/3500     // Valid passing position zone
RECEPTION_POSITION_MIN/MAX = 1000/2500 // Valid receiving position zone
```

## Tweaking Tips

### To Encourage More Demos
- Decrease `DEMO_RANGE` (e.g., 1200.0f) → Requires closer positioning
- Increase phase weights in RewardConfig.cpp
- Check that `DemoAwarenessReward` isn't competing

### To Encourage More Passing
- Increase `RECEPTION_POSITION_MAX` (e.g., 3000.0f) → Larger reception zone
- Decrease `PASS_QUALITY_THRESHOLD` (e.g., 400.0f) → Easier pass standards
- Increase `POST_ACTION_WINDOW` (e.g., 180) → More time for followup

### To Be More Aggressive
- Decrease `ATTACKING_ZONE_DIST` (e.g., 4000.0f) → More aggressive zone
- Increase phase weights (e.g., 6.0x → 8.0x in Platinum)
- Decrease retreat penalty magnitude

### To Be Less Chaotic
- Increase `POST_ACTION_WINDOW` (slows down attack pace)
- Decrease phase weights (reduces priority)
- Increase `PASS_POSITION_MIN` (requires more setup before passing)

## Debugging / Monitoring

### In Training Logs, Look For:
- `NumericalAdvantageAttackingReward` signal appearing in agent rewards
- Correlation with 2v1 situations (reward spikes when outnumbering)
- Similar or higher magnitude to demo/passing rewards at similar phases

### In Bot Behavior, Watch For:
- Primary bot moving close to isolated defender
- Support bot positioning for clean shot after demo
- Fewer solo dribbles when teammate nearby
- More passing attempts in advantageous positions

## Common Issues & Solutions

| Issue | Cause | Fix |
|-------|-------|-----|
| Reward never triggers | Advantage detection too strict | Check if bots actually reaching attacking zone |
| Reward triggers but no behavior change | Weight too low | Increase phase weights in RewardConfig.cpp |
| Bots spam demos | Primary/support distinction unclear | Verify role detection (see `DetermineRole()` method) |
| Bots don't shoot after pass | Post-action window too short | Increase `POST_ACTION_WINDOW` |
| Retreating despite advantage | Other rewards pulling harder | Reduce competing reward weights |

## Integration with Other Rewards

### Works Great With:
- **DemoAwarenessReward** - Teaches general demo awareness
- **BoostAwareRotationReward** - Creates proper spacing for advantages
- **PassingRewards** - Complements passing architecture
- **ShotRewards** - Rewards the finishing shot

### May Compete With:
- **BoostWastePenaltyReward** - Demo uses boost (but demo path has priority)
- **DefensiveRotationBoostReward** - Pulls away from attacking position
- **VelocityPlayerToBallReward** - May encourage chaotic rushing

## Edge Cases Handled

✓ Kickoff situations (doesn't activate during setup)
✓ 1v1 matches (only activates 2v2+)
✓ Ball in own goal area (resets advantage count)
✓ Tied numbers (requires strict advantage to activate)
✓ 3+ player advantages (scales reward with advantage magnitude)

---

## Example Training Curve Expectations

### Initial (0-100M steps)
- Reward activates sporadically
- Bots learn basic demo approach
- Some random passing attempts

### Mid-training (100M-500M steps)
- Consistent demo execution in 2v1
- Improved decision making (demo vs pass)
- Better support positioning

### Late-training (500M+ steps)
- Smooth 2v1 coordination
- Intelligent pass/demo selection
- Natural exploitation of advantages
- Minimal interference with other tactical systems

---

## File Locations

- **Implementation:** `/GigaLearnBot/GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/ShootingRewards.h`
- **Config integration:** `/GigaLearnBot/src/RewardConfig.cpp` (lines ~166, ~346, ~532)
- **Constants:** `ShootingRewards.h`, lines ~310-320

## Questions?

Check the main implementation:
- Class: `NumericalAdvantageAttackingReward` in `ShootingRewards.h`
- Methods: `DetermineRole()`, `HasNumericalAdvantage()`, `IsDemoBumpOpportunity()`
