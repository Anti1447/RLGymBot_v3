# SSL Training Guide - Phase Reference (Updated)

## Quick Start

1. **Copy reward files to RLGymCPP/Rewards/:**
   - `CompleteMechanicsRewards.h`
   - `GodTierMovementRewards_Enhanced.h`
   - `PossessionRewards.h`

2. **Set phase in ExampleMain.cpp line 40:**
   ```cpp
   constexpr TrainingPhase CURRENT_PHASE = TrainingPhase::PHASE_1_BRONZE;
   ```

3. **Rebuild and run**

---

## Phase Timeline (Logarithmic Scale at 8B steps/day)

| Phase | Steps | Duration | Focus |
|-------|-------|----------|-------|
| **1: Bronze** | 0-1B | ~3 hours | Ball chasing, boost collection |
| **2: Silver** | 1-3B | ~6 hours | Half-flips, wave dashes, recoveries |
| **3: Gold** | 3-5B | ~6 hours | Wall play, power hits, defense |
| **4: Platinum** | 5-8B | ~9 hours | Aerials, 50/50s, speed flips |
| **5: Diamond** | 8-12B | ~12 hours | Flicks, dribbles, air dribbles |
| **6: Champion** | 12-20B | ~1 day | Double taps, flip resets, mustys |
| **7: GC** | 20-40B | ~2.5 days | Consistency, ceiling pinches |
| **8: SSL** | 40-80B | ~5 days | Full mastery, polish |
| **9: PRO** | 80B+ | Ongoing | Pro esports refinement |

**Total to SSL: ~10 days**
**Total to PRO: ~12+ days**

---

## Network Configuration (CONSTANT)

```cpp
cfg.ppo.sharedHead.layerSizes = { 1024, 1024 };
cfg.ppo.policy.layerSizes = { 1024, 1024, 512, 512 };
cfg.ppo.critic.layerSizes = { 1024, 1024, 512, 512 };
```

**Network size cannot change between phases!**

---

## Hyperparameter Schedule

| Phase | Entropy | Learning Rate | No-Touch Timeout |
|-------|---------|---------------|------------------|
| Bronze | 0.025 | 2.5e-4 | 15s |
| Silver | 0.020 | 2.0e-4 | 12s |
| Gold | 0.018 | 1.8e-4 | 10s |
| Platinum | 0.015 | 1.5e-4 | 8s |
| Diamond | 0.012 | 1.2e-4 | 6s |
| Champion | 0.010 | 1.0e-4 | 5s |
| GC | 0.009 | 8e-5 | 4s |
| SSL | 0.008 | 5e-5 | 4s |
| PRO | 0.008 | 3e-5 | 3s |

---

## Zero-Sum Reward Structure

**ALL competitive rewards are zero-sum wrapped:**
- `TouchBallReward` ✓
- `JumpTouchReward` ✓
- `AirTouchReward` ✓
- `WallTouchReward` ✓
- `StrongTouchReward` ✓
- `TouchAccelReward` ✓
- `VelocityBallToGoalReward` ✓
- `ShotReward` ✓
- `SaveReward` ✓
- `BumpReward` ✓
- `DemoReward` ✓
- `LiuDistanceReward` ✓
- `DefensivePositionReward` ✓

**GoalReward** uses built-in zero-sum via `GoalReward(-1.0f)` concedeScale.

---

## Key Mechanics Unlocked by Phase

### Bronze (0-1B)
- `TouchBallReward`
- `VelocityPlayerToBallReward`
- `TurningRadiusReward`
- `ProperLandingReward`
- `BoostEfficiencyReward`

### Silver (1-3B)
- `HalfFlipReward`
- `WavedashReward`
- `PowerSlideControlReward`
- `KickoffSpeedReward`
- `JumpTouchReward`

### Gold (3-5B)
- `WallTouchReward`
- `CornerClearReward`
- `WallHitReward`
- `AirRollShotReward`
- `DemoAwarenessReward`
- **First saves/shots**

### Platinum (5-8B)
- `AirTouchReward` (200uu minimum)
- `FiftyFiftyReward`
- `CatchBallReward`
- `DirectionalAirRollReward`
- `DribbleReward`
- `EnhancedSpeedFlipReward`

### Diamond (8-12B)
- `FlickReward`
- `AirDribbleReward`
- `CeilingShotReward`
- `BackboardDefenseReward`
- `ShotAccuracyReward`
- `FakeChallengeReward`
- `TimeToBalPossessionReward`
- `EnhancedZapDashReward`

### Champion (12-20B)
- `EnhancedDoubleTapReward`
- `FlipResetDetectionReward`
- `AdvancedFlickReward` (mustys)
- `GroundToAirDribbleReward`
- `WallPinchReward`
- `RedirectReward`
- `EnhancedMenaFlipReward`
- `EnhancedWallDashReward`
- `ContextualPossessionReward`
- `BoostStealReward`
- `ReverseAerialReward`

### GC (20-40B)
- `MustyVariationReward`
- `CeilingGroundPinchReward`
- `BounceDribbleReward`
- `AirDribbleBumpReward`
- `ConsistencyReward`
- `MultiAgentPossessionReward`

### SSL (40-80B)
- All above with refined weights
- Maximum scoring emphasis
- Full possession system

### PRO (80B+)
- Maximum consistency focus
- Highest combat weights
- Pro-level boost denial
- Perfect positioning

---

## Metrics to Watch

| Metric | Bronze | Plat | Diamond | GC | SSL |
|--------|--------|------|---------|----|----|
| Touch Height | 100+ | 300+ | 500+ | 700+ | 900+ |
| Goal Speed | 30+ | 50+ | 70+ | 85+ | 100+ |
| Supersonic % | 10% | 25% | 35% | 45% | 50%+ |
| In Air % | 5% | 15% | 25% | 35% | 40%+ |

---

## When to Change Phases

**Signs you're ready:**
- Skill tracker rating plateauing for 500M+ steps
- Touch height appropriate for current phase
- Specific mechanics appearing consistently
- Goal speed increasing

**Don't rush!** The logarithmic scale means early phases are fast anyway.

---

## Troubleshooting

### Bot farms touches cooperatively
- All touch rewards ARE zero-sum wrapped now
- If still happening, check your reward files are updated

### Bot never goes for aerials
- `AirTouchReward` unlocks in Phase 4 (Platinum)
- Before that, focus is ground play

### Bot is inconsistent
- Stay in current phase longer
- `ConsistencyReward` helps in GC phase

### Training unstable after phase change
- Normal for first 100-200M steps
- Reward structure changed significantly
- Consider reducing learning rate temporarily

---

## Recommended Checkpoints

| Steps | Phase | Description |
|-------|-------|-------------|
| 1B | Bronze | Ball-chaser baseline |
| 5B | Platinum | Ground + basic aerials |
| 12B | Diamond | Full mechanical foundation |
| 20B | Champion | Advanced mechanics |
| 40B | GC | Competitive play |
| 80B | SSL | Full mastery |

---

## Files Required

```
RLGymCPP/src/RLGymCPP/Rewards/
├── CommonRewards.h              (existing)
├── AdvancedRewards.h            (existing)
├── ZeroSumReward.h              (existing)
├── CompleteMechanicsRewards.h   (from ClaudeFiles)
├── GodTierMovementRewards_Enhanced.h (from ClaudeFiles)
└── PossessionRewards.h          (from ClaudeFiles)
```

---

## Code Structure

```cpp
// Phase selection (line 40)
constexpr TrainingPhase CURRENT_PHASE = TrainingPhase::PHASE_1_BRONZE;

// GetPhaseConfig() - Returns entropy, LR, timeout per phase
// GetPhasedRewards() - Returns reward vector per phase
// EnvCreateFunc() - Creates environment with phase config
// main() - Uses constant network size, phase-specific hyperparams
```

Good luck reaching PRO! 🚀
