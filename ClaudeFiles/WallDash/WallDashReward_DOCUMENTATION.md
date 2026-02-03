# WallDashReward - Advanced Wall Dash Detection & Reward System

## Overview

WallDashReward is a sophisticated reward system that detects and encourages proper execution of the **wall dash** technique in Rocket League. This is an advanced movement mechanic that allows a car to maintain or gain speed while traveling along a wall.

## What is a Wall Dash?

A wall dash is an advanced movement technique where a player:
1. Drives into a wall at high speed (≥ 1000 uu/s)
2. Points their car's nose upward (15-25° pitch angle)
3. Holds handbrake for steering
4. Performs air roll perpendicular to the wall to maintain contact
5. Spams jump (every other frame) to gain vertical lift
6. Results in maintained or increased velocity along the wall

## Technical Implementation

### State Tracking

The reward tracks per-player state to understand the progression of a wall dash attempt:

```cpp
struct PlayerWallDashState {
    float continuousAirtimeCounter;  // How long off the wall
    bool wasOnWallLastFrame;         // Previous frame wall status
    float velocityBaseline;          // Velocity when dash started
    bool isInDashAttempt;            // Currently attempting dash
    int jumpActionCounter;           // Track jump cadence
    bool wasOnWall;                  // Wall transition tracking
};
```

### Configuration Parameters

All detection thresholds are tunable constants:

| Parameter | Value | Purpose |
|-----------|-------|---------|
| `MIN_FORWARD_VEL` | 1000 uu/s | Minimum speed to attempt dash |
| `MIN_PITCH_DEGREES` | 15° | Minimum nose-up angle |
| `MAX_PITCH_DEGREES` | 25° | Maximum nose-up angle |
| `PITCH_TOLERANCE` | ±5° | Tolerance around target angle |
| `MAX_CONTINUOUS_AIRTIME` | 0.5 sec | Max time off wall before failure |
| `WALL_PROXIMITY_THRESHOLD` | 200 uu | Distance from wall edge |
| `HANDBRAKE_THRESHOLD` | 0.5 | Input magnitude (0-1) |
| `AIR_ROLL_THRESHOLD` | 0.5 | Input magnitude (0-1) |
| `THROTTLE_THRESHOLD` | 0.5 | Input magnitude (0-1) |
| `VELOCITY_MAINTENANCE` | 0.95 | Minimum velocity ratio (95% baseline) |

### Wall Detection Algorithm

The reward identifies walls through three checks:

1. **Ground Status**: `player.isOnGround == true`
   - The physics engine must report the car as on a surface

2. **Wall-Facing Normal**: Check if car's up vector is horizontal
   ```
   verticalComponent = abs(player.rotMat.up.z)
   if (verticalComponent <= 0.3)  // Mostly horizontal = wall
   ```
   - On a regular floor, up.z ≈ 1.0 (pointing straight up)
   - On a wall, up.z ≈ 0.0 (pointing horizontally away from wall)

3. **Proximity to Boundary**: Check distance from wall edges
   ```
   nearSideWall = abs(x) > (4096 - 200)    // Near x = ±4096
   nearBackWall = abs(y) > (5120 - 200)    // Near y = ±5120
   ```

### Wall Dash Execution Flow

```
Frame N: On Wall, Velocity ≥ 1000
  ↓
Set isInDashAttempt = true
Store velocityBaseline
Reset airtime counter
  ↓
Frames N+1 to N+k: Continuous Evaluation
  - Check pitch angle (15-25°)
  - Check handbrake input
  - Check air roll alignment
  - Check jump cadence (every other frame)
  - Check velocity maintenance
  - Award +0.5 to +4.25 reward per frame
  ↓
Frame N+k+1: Leaves Wall OR Maintains Wall
  
  If Maintained:
    → Continue rewarding active dash
    
  If Left Wall (airborne):
    → Accumulate airtime counter
    
    If airtime < 0.5 sec:
      → Evaluate dash quality
      → Award 2-5 reward bonus
      
    If airtime ≥ 0.5 sec:
      → Failure (-5 penalty)
      → End attempt
```

## Reward Structure

### Per-Frame Rewards (Active Dash)

While the car is actively wall dashing:

| Criterion | Reward | Condition |
|-----------|--------|-----------|
| Forward Velocity | +0.5 | velocity ≥ 1000 uu/s |
| Pitch Angle | +1.0 | 15° ≤ pitch ≤ 25° |
| Pitch Angle (Partial) | +0.5 | Within ±5° of target |
| Handbrake | +0.5 | Input ≥ 0.5 magnitude |
| Air Roll Alignment | +0.75 | Perpendicular to wall + rolling |
| Jump Cadence | +0.5 | Every other frame |
| Jump Cadence (Bad) | -0.1 | Every frame (too frequent) |
| Velocity Maintenance | +0.5 | velocity ≥ baseline × 0.95 |

**Maximum per-frame reward: ~4.25**

### End-of-Attempt Rewards (Successful Exit)

When the car completes a dash attempt by leaving the wall:

| Criterion | Reward | Condition |
|-----------|--------|-----------|
| Speed Increase | +2.0 to +3.0 | velGain > 0, up to +200 uu/s |
| Velocity Maintenance | +1.5 | velocity ≥ baseline × 0.95 |
| Clean Execution | +1.0 | airtime < 0.25 sec |

**Maximum completion reward: ~5.5**

### Failure Penalties

| Failure Mode | Penalty | Condition |
|--------------|---------|-----------|
| Fall Off Wall | -5.0 | Airborne ≥ 0.5 sec |

## Integration into Training

### Inclusion in Reward Vector

Add to your phase configuration:

```cpp
// Phase 4: Platinum onwards
WeightedReward(new WallDashReward(), 15.0f)  // Weight: 15.0
```

### Weight Progression

Suggested weight progression by phase:

- **Phase 3 (Gold)**: 8.0 - Introduction, low weight
- **Phase 4 (Platinum)**: 12.0-15.0 - Active development
- **Phase 5 (Diamond)**: 15.0-18.0 - Refinement
- **Phase 6+ (Champion/GC)**: 15.0-20.0 - High emphasis

### Complementary Rewards

Works well alongside:
- `WallTouchReward` - Encourages initial wall contact
- `PowerSlideControlReward` - Teaches steering mechanics
- `EnhancedSpeedFlipReward` - Teaches aerial control
- `VelocityReward` - General movement encouragement
- `AirRollShotReward` - Air roll technique

## Behavior and Learning

### What the Bot Should Learn

1. **Detection**: Recognize when it's on a wall vs. normal ground
2. **Setup**: Approach walls at sufficient speed
3. **Technique**: Pitch nose up (15-25°), handbrake, air roll
4. **Timing**: Jump every other frame (not every frame)
5. **Follow-through**: Maintain wall contact through dash
6. **Exit Strategy**: Complete dash safely with velocity maintained

### Expected Learning Curve

- **Early Training (0-2B steps)**: Bot learns wall detection, occasional wall dashes
- **Mid Training (2-5B steps)**: Consistent wall dash attempts, variable technique
- **Late Training (5B+ steps)**: Clean, purposeful wall dashes with speed gains

### Potential Failure Modes

1. **Over-jumping**: Bot tries to jump every frame instead of every other
   - Penalty of -0.1 per frame should discourage this
   - May need higher weight ratio to other movement rewards

2. **Insufficient Pitch**: Bot doesn't rotate nose up enough
   - Pitch detection gives 0 reward for < 15° or > 25°
   - Watch for bots that only get +0.5 partial rewards

3. **Overshooting**: Bot falls off wall due to over-air-roll
   - Accumulation of continuous airtime counter (0.5 sec limit)
   - -5.0 penalty should eventually discourage

4. **Speed Loss**: Bot maintains wall contact but loses velocity
   - No +1.5 completion bonus, only per-frame rewards

## Tuning Guide

### If Bot Isn't Learning Wall Dashes

1. **Increase weight**: Change from 15.0 to 20.0-25.0
2. **Reduce threshold speed**: Change `MIN_FORWARD_VEL` from 1000 to 800
3. **Widen pitch angle**: Change tolerance from 5° to 10°
4. **Pair with ground mechanics reward**: Reduce weight of `PowerSlideControlReward`

### If Bot Over-Dashes

1. **Reduce weight**: Change to 8.0-10.0
2. **Increase airtime limit**: Change from 0.5 to 0.75 seconds
3. **Reduce completion bonus**: Edit `EvaluateDashAttempt()` return values

### If Bot Does Perfect Dashes but Doesn't Use Them Tactically

1. **Increase weight further**: Competition from offensive rewards
2. **Pair with offensive position rewards**: Combine with shot setup rewards
3. **Use team-based progression**: Introduce in Phase 5 when bot has solid fundamentals

## Code Structure

### Key Methods

```cpp
// Main evaluation
float GetReward(const Player& player, const GameState& state)

// Wall detection (3-part check)
bool IsOnWall(const Player& player, const GameState& state)

// Pitch angle in degrees
float GetPitchAngle(const Player& player)

// Horizontal velocity on wall
float GetWallVelocity(const Player& player)

// Active dash phase reward
float EvaluateActiveDash(const Player& player, const GameState& state,
                         PlayerWallDashState& pState)

// Completion phase reward
float EvaluateDashAttempt(const Player& player, const GameState& state,
                          PlayerWallDashState& pState)

// Wall normal vector (pointing away from wall)
Vec GetWallNormal(const Player& player)
```

### State Management

- **Reset()**: Clears all player state tracking (called at episode start)
- **Per-frame state updates**: Automatic via `playerStates` map
- **Per-player isolation**: Each car ID tracked independently

## Example Training Configurations

### Aggressive Wall Dash Learning (Early Phase 4)

```cpp
case TrainingPhase::PHASE_4_PLATINUM:
    return std::vector<WeightedReward>{
        // ... existing rewards ...
        WeightedReward(new WallDashReward(), 25.0f),  // High focus
        WeightedReward(new PowerSlideControlReward(), 0.5f),  // Lower
        // ... rest of rewards ...
    };
```

### Balanced Integration (Mid Phase 5)

```cpp
case TrainingPhase::PHASE_5_DIAMOND:
    return std::vector<WeightedReward>{
        // ... existing rewards ...
        WeightedReward(new WallDashReward(), 12.0f),  // Balanced
        WeightedReward(new VelocityReward(), 0.3f),
        // ... rest of rewards ...
    };
```

### Competition Phase (Phase 6+)

```cpp
case TrainingPhase::PHASE_6_CHAMPION:
    return std::vector<WeightedReward>{
        // ... existing rewards ...
        WeightedReward(new WallDashReward(), 8.0f),  // Lower, competes with advanced mechanics
        WeightedReward(new EnhancedDoubleTapReward(), 30.0f),
        // ... rest of rewards ...
    };
```

## Performance Notes

- **Computational Cost**: Minimal (simple vector math, < 1ms per frame total)
- **State Memory**: ~100 bytes per player
- **Scalability**: Tested with up to 310 concurrent games

## Debugging Tips

To check if wall dash detection is working:

1. Enable metrics tracking in `StepCallback`
2. Check `player.isOnGround` and `player.rotMat.up.z` values
3. Verify pitch angle calculation with `asin(forward.z)`
4. Watch replays for wall contact detection failures

Example metrics:

```cpp
report.AddAvg("WallDash/OnWall", onWall ? 1.0f : 0.0f);
report.AddAvg("WallDash/PitchAngle", pitchAngle);
report.AddAvg("WallDash/WallVelocity", wallVelocity);
```

## Future Enhancements

Potential improvements:

1. **Curved walls**: Detect and reward dashes on corner walls
2. **Flip momentum**: Reward wall dash into flip for additional speed
3. **Wave dash integration**: Detect wall → ground transitions for wave dashing
4. **Air dribble setup**: Reward wall dash as setup for air dribbles
5. **Two-player coordination**: Reward wall dash handoffs in team modes

---

**Last Updated**: 2025-01-30 | **Revision**: 1.0 | **Author**: NJH
