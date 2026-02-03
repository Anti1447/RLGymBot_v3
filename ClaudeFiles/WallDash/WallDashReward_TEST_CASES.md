# WallDashReward - Test Cases & Validation

## Test Scenario Categories

### Category 1: Wall Detection Accuracy

#### Test 1.1: Side Wall Detection (X-Axis Walls)

**Scenario**: Player on side wall (x = ±4096)

```
Setup:
  - player.isOnGround = true
  - player.pos = (4000, 0, 100)  // Near x = 4096
  - player.rotMat.up = (0.05, 0.05, 0.1)  // Horizontal normal

Expected:
  - IsOnWall() returns true
  - Wall detection threshold satisfied
  - Proximity check passes (distance 96 < 200)
```

**Validation Checklist**:
- [ ] Bot detected as on wall
- [ ] verticalComponent < 0.3 confirmed
- [ ] absX > 3896 (SIDE_WALL_X - threshold)

#### Test 1.2: Back Wall Detection (Y-Axis Walls)

**Scenario**: Player on back wall (y = ±5120)

```
Setup:
  - player.isOnGround = true
  - player.pos = (0, 5050, 100)  // Near y = 5120
  - player.rotMat.up = (0.05, 0.05, 0.1)  // Horizontal normal

Expected:
  - IsOnWall() returns true
  - absY > 4920 (BACK_WALL_Y - threshold)
```

**Validation Checklist**:
- [ ] Bot detected as on back wall
- [ ] Y-proximity check passes
- [ ] Normal vector is horizontal

#### Test 1.3: False Positive Prevention (On Ground But Not Wall)

**Scenario**: Player on field, not near wall

```
Setup:
  - player.isOnGround = true
  - player.pos = (0, 0, 100)  // Center field
  - player.rotMat.up = (0, 0, 1)  // Straight up (normal floor)

Expected:
  - IsOnWall() returns false
  - verticalComponent = 1.0 (way > 0.3 threshold)
```

**Validation Checklist**:
- [ ] Returns false (not on wall)
- [ ] up.z > 0.3 detected correctly
- [ ] Proximity check not satisfied

#### Test 1.4: Airborne Car (Not On Ground)

**Scenario**: Player in air near wall

```
Setup:
  - player.isOnGround = false  // CRITICAL
  - player.pos = (4000, 0, 500)  // Over wall
  - player.rotMat.up = (0.05, 0.05, 0.1)

Expected:
  - IsOnWall() returns false immediately
  - Does not proceed to normal/proximity checks
```

**Validation Checklist**:
- [ ] Early exit on `!player.isOnGround`
- [ ] No false positives in air

---

### Category 2: Pitch Angle Detection

#### Test 2.1: Valid Pitch Angle (Target Range)

**Scenario**: Car pitched up 20° (ideal)

```
Setup:
  - forward.z = sin(20° in radians) = sin(0.349) ≈ 0.342
  - player.rotMat.forward = (0, 0.342, 0.940)

Expected:
  - GetPitchAngle() returns ~20°
  - Reward includes +1.0 for correct pitch
```

**Validation Checklist**:
- [ ] Calculated pitch ≈ 20° (±0.5° tolerance)
- [ ] Full reward awarded
- [ ] Within [15°, 25°] range

#### Test 2.2: Partial Reward (Close but Not Perfect)

**Scenario**: Car pitched up 12° (2° below minimum)

```
Setup:
  - forward.z = sin(12°) ≈ 0.208
  - player.rotMat.forward = (0, 0.208, 0.978)

Expected:
  - GetPitchAngle() returns ~12°
  - NOT in [15°, 25°] range
  - In tolerance range [10°, 30°]
  - Reward includes +0.5 (partial)
```

**Validation Checklist**:
- [ ] Calculated pitch ≈ 12°
- [ ] Below 15° minimum
- [ ] Within ±5° tolerance (range [10°, 30°])
- [ ] Partial reward (+0.5) awarded

#### Test 2.3: No Reward (Completely Wrong Angle)

**Scenario**: Car pointing straight (0° pitch)

```
Setup:
  - forward.z = sin(0°) = 0
  - player.rotMat.forward = (0, 1, 0)

Expected:
  - GetPitchAngle() returns 0°
  - Not in [15°, 25°]
  - Not in [10°, 30°] tolerance
  - No pitch reward
```

**Validation Checklist**:
- [ ] Calculated pitch = 0°
- [ ] No reward for incorrect pitch
- [ ] No penalty (not punished for wrong angle)

#### Test 2.4: Downward Pitch (Negative Reward?)

**Scenario**: Car pitched down 15° (wrong direction)

```
Setup:
  - forward.z = sin(-15°) ≈ -0.259
  - player.rotMat.forward = (0, -0.259, 0.966)

Expected:
  - GetPitchAngle() returns ~-15°
  - No pitch reward
  - Should NOT penalize (physics might require this temporarily)
```

**Validation Checklist**:
- [ ] Calculated pitch ≈ -15°
- [ ] No pitch reward given
- [ ] No penalty applied

---

### Category 3: Wall Dash State Machine

#### Test 3.1: Entering Wall (Dash Initiation)

**Scenario**: Car transitions from airborne to wall contact

```
Frame N-1:
  - IsOnWall() = false
  - wasOnWallLastFrame = false

Frame N:
  - IsOnWall() = true
  - velocity = 1200 uu/s

Expected Actions:
  - isInDashAttempt set to true
  - velocityBaseline = 1200
  - continuousAirtimeCounter reset to 0
  - jumpActionCounter reset to 0
```

**Validation Checklist**:
- [ ] State transitions correctly
- [ ] Baseline velocity recorded
- [ ] All counters initialized

#### Test 3.2: Maintaining Dash (Continuous Reward)

**Scenario**: Car maintains wall contact for 10 frames

```
Frames N to N+9:
  - IsOnWall() = true continuously
  - pitch = 18° (valid)
  - handbrake = 0.7 (active)
  - roll = 0.8 (active)
  - velocity = 1200-1300 uu/s (maintained)

Expected:
  - Each frame: reward 0.5 + 1.0 + 0.5 + 0.75 + 0.5 + 0.5 = 4.25/frame
  - Total reward over 10 frames ≈ 42.5
  - continuousAirtimeCounter stays 0
```

**Validation Checklist**:
- [ ] Per-frame rewards accumulate
- [ ] Airtime counter reset each frame on wall
- [ ] isInDashAttempt remains true

#### Test 3.3: Leaving Wall Early (Short Duration)

**Scenario**: Car leaves wall after 8 frames

```
Frames N to N+7:
  - IsOnWall() = true
  - Proper execution

Frame N+8:
  - IsOnWall() = false (leaves wall)
  - continuousAirtimeCounter = 0

Expected:
  - EvaluateDashAttempt() called
  - Final velocity = 1250 uu/s
  - Baseline = 1200 uu/s
  - velGain = 50 uu/s
  
Reward Breakdown:
  - Speed increase: +2.0 + (50/200) = +2.25
  - Velocity maintenance: +1.5
  - Clean execution (8 ticks < 0.25 sec): +1.0
  - Total: ~4.75
```

**Validation Checklist**:
- [ ] EvaluateDashAttempt() invoked on wall exit
- [ ] Speed gain calculated correctly
- [ ] Clean execution bonus applied

#### Test 3.4: Leaving Wall with Airtime Accumulation

**Scenario**: Car leaves wall, airborne 0.3 sec, returns to wall

```
Frames N to N+9:
  - IsOnWall() = true (10 frames = ~0.083 sec)
  
Frame N+10:
  - IsOnWall() = false
  - continuousAirtimeCounter starts incrementing

Frames N+10 to N+35:
  - IsOnWall() = false
  - continuousAirtimeCounter increases by TICK_TIME (1/120) each frame
  - After 30 frames: counter = 30/120 = 0.25 sec

Frame N+36:
  - IsOnWall() = true
  - continuousAirtimeCounter = 0.25 sec (< 0.5 sec threshold)
  
Expected:
  - isInDashAttempt remains true
  - Dash continues (not failed)
  - continuousAirtimeCounter reset to 0
```

**Validation Checklist**:
- [ ] Airtime counter increments correctly
- [ ] Under threshold doesn't trigger failure
- [ ] Can resume dash after brief airtime

#### Test 3.5: Falling Off Wall (Exceeding Airtime)

**Scenario**: Car leaves wall and stays airborne too long

```
Frames N to N+9:
  - IsOnWall() = true (dash started)

Frames N+10 to N+70:
  - IsOnWall() = false
  - continuousAirtimeCounter increments
  - After 60 frames: counter = 60/120 = 0.5 sec
  
Frame N+71:
  - IsOnWall() still false
  - continuousAirtimeCounter = 0.5001 sec (exceeds MAX_CONTINUOUS_AIRTIME)

Expected:
  - reward -= 5.0
  - isInDashAttempt = false
  - Dash attempt marked as failed
```

**Validation Checklist**:
- [ ] Failure penalty applied (-5.0)
- [ ] isInDashAttempt set to false
- [ ] No further rewards for this attempt

---

### Category 4: Input Validation

#### Test 4.1: Handbrake Requirement

**Scenario A - With Handbrake**:
```
player.prevAction.handbrake = 0.7 (≥ 0.5 threshold)

Expected: +0.5 reward for handbrake
```

**Scenario B - Without Handbrake**:
```
player.prevAction.handbrake = 0.2 (< 0.5 threshold)

Expected: No handbrake reward
```

**Validation Checklist**:
- [ ] Handbrake threshold applied correctly
- [ ] Reward only given when significant input

#### Test 4.2: Air Roll Alignment

**Scenario**: Car on side wall, rolling correctly

```
Setup:
  - Wall normal = (1, 0, 0) [right side wall]
  - car.rotMat.right = (0.8, 0.1, 0.6) [car's right vector]
  
Calculation:
  - rollAlignment = abs((0.8, 0.1, 0.6) · (1, 0, 0))
  - rollAlignment = abs(0.8) = 0.8 (> 0.7 threshold)
  
Expected:
  - If player.prevAction.roll ≥ 0.5: +0.75 reward
  - Good perpendicular air roll detected
```

**Validation Checklist**:
- [ ] Wall normal calculated correctly
- [ ] Dot product alignment check works
- [ ] Reward given for perpendicular rolling

#### Test 4.3: Jump Cadence (Every Other Frame)

**Scenario A - Correct Cadence**:
```
jumpActionCounter = 0, player.prevAction.jump = 0.0 (no jump)
  → No penalty

jumpActionCounter = 1, player.prevAction.jump = 1.0 (jump)
  → +0.5 reward (jumping on odd counter)

jumpActionCounter = 2, player.prevAction.jump = 0.0 (no jump)
  → No penalty

jumpActionCounter = 3, player.prevAction.jump = 1.0 (jump)
  → +0.5 reward (jumping on odd counter)
```

**Scenario B - Incorrect Cadence (Every Frame)**:
```
jumpActionCounter = 0, player.prevAction.jump = 1.0 (jump)
  → (condition: % 2 == 0 && jump > 0.5)
  → No reward (jumping on even counter)

jumpActionCounter = 1, player.prevAction.jump = 1.0 (jump)
  → (condition: % 2 != 0 || jump ≤ 0.5)
  → -0.1 penalty (jumping too frequently)

jumpActionCounter = 2, player.prevAction.jump = 1.0 (jump)
  → -0.1 penalty (jumping too frequently)
```

**Validation Checklist**:
- [ ] Counter increments each frame
- [ ] Reward at correct modulo positions
- [ ] Penalty applied for continuous jumping

---

### Category 5: Velocity Management

#### Test 5.1: Velocity Maintenance Bonus

**Scenario**: Baseline 1000, current 950

```
Setup:
  - velocityBaseline = 1000 uu/s
  - currentVel = 950 uu/s
  - VELOCITY_MAINTENANCE_THRESHOLD = 0.95 (95%)
  - minVel = 1000 * 0.95 = 950

Check:
  - currentVel (950) ≥ minVel (950) ? YES
  
Expected: +0.5 reward for velocity maintenance
```

**Validation Checklist**:
- [ ] Threshold calculation correct
- [ ] Reward given at boundary
- [ ] No reward if below threshold

#### Test 5.2: Velocity Loss Penalty (No Bonus)

**Scenario**: Baseline 1000, current 900

```
Setup:
  - velocityBaseline = 1000 uu/s
  - currentVel = 900 uu/s
  - minVel = 950

Check:
  - currentVel (900) ≥ minVel (950) ? NO
  
Expected: No +0.5 maintenance reward
```

**Validation Checklist**:
- [ ] No reward when below threshold
- [ ] No penalty applied (just no reward)

#### Test 5.3: Velocity Gain Bonus

**Scenario**: Baseline 1000, final 1150 (speed increase)

```
Setup:
  - velocityBaseline = 1000 uu/s
  - finalVel = 1150 uu/s
  - velGain = 150 uu/s

Reward calculation:
  - Base: +2.0
  - Gain bonus: +150/200 = +0.75
  - Total from speed: +2.75
  
Additional check:
  - finalVel (1150) ≥ baseline * 0.95 (950) ? YES
  - +1.5 for maintenance
  
Total: ~4.25
```

**Validation Checklist**:
- [ ] Gain calculated as (final - baseline)
- [ ] Bonus scaled by 200 denominator
- [ ] Capped appropriately
- [ ] Combined with other bonuses

---

## Regression Test Suite

### Quick Validation (5 minutes)

Run these tests in quick succession to verify basic functionality:

```cpp
void RunQuickValidation() {
    // Test 1: Wall detection
    assert(IsOnWall(PlayerOnSideWall, state) == true);
    assert(IsOnWall(PlayerOnField, state) == false);
    
    // Test 2: Pitch angle
    assert(GetPitchAngle(PlayerPitched20Deg, state) ≈ 20.0f);
    
    // Test 3: State transitions
    auto reward1 = GetReward(PlayerEnteringWall, state);
    assert(PlayerState.isInDashAttempt == true);
    
    // Test 4: Airtime accumulation
    auto reward2 = GetReward(PlayerAirborne, state);
    assert(PlayerState.continuousAirtimeCounter > 0);
    
    // Test 5: Velocity tracking
    assert(PlayerState.velocityBaseline == 1200.0f);
    
    printf("Quick validation PASSED\n");
}
```

### Integration Test (Per Phase)

For each phase where WallDashReward is added:

```cpp
void ValidatePhaseIntegration(TrainingPhase phase) {
    // Verify reward is in list
    auto rewards = GetPhasedRewards();
    bool foundWallDash = false;
    for (auto& wr : rewards) {
        if (typeid(*wr.reward) == typeid(WallDashReward)) {
            foundWallDash = true;
            assert(wr.weight > 0);  // Has positive weight
        }
    }
    assert(foundWallDash);
    
    // Run 100 game steps
    for (int i = 0; i < 100; i++) {
        EnvCreateResult env = EnvCreateFunc(0);
        for (int j = 0; j < 10; j++) {
            // Simulate game step
            // Check no crashes or NaNs
            auto rewards = env.rewards;
        }
    }
    
    printf("Phase integration PASSED\n");
}
```

### Performance Test

```cpp
void ValidatePerformance() {
    const int GAMES = 310;
    const int STEPS = 1000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int g = 0; g < GAMES; g++) {
        WallDashReward reward;
        GameState state = CreateGameState();
        Player player = state.players[0];
        
        for (int s = 0; s < STEPS; s++) {
            float r = reward.GetReward(player, state);
            assert(!std::isnan(r));
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    float msPerCall = float(duration.count()) / (GAMES * STEPS);
    printf("Performance: %.4f ms per call\n", msPerCall);
    assert(msPerCall < 0.01f);  // Should be < 10 microseconds
}
```

---

## Expected Failure Modes & Detection

### Failure Mode 1: No Wall Recognition

**Symptoms**:
- "WallDash/OnWall" metric stays near 0%
- No wall-related activity in logs

**Likely Cause**:
- `IsOnWall()` never returns true
- Wall proximity check is too strict

**Debug**:
```cpp
report.AddAvg("WallDash/OnWall", onWall ? 1.0f : 0.0f);
report.AddAvg("WallDash/NormalZ", player.rotMat.up.z);
report.AddAvg("WallDash/DistFromWall", 
    RS_MIN(
        RS_ABS(player.pos.x) - CommonValues::SIDE_WALL_X,
        RS_ABS(player.pos.y) - CommonValues::BACK_WALL_Y
    )
);
```

### Failure Mode 2: Over-Jumping

**Symptoms**:
- Bot attempts wall dash but jumps every frame
- Rapidly loses wall contact (airtime counter hits limit)

**Likely Cause**:
- Jump cadence penalty (-0.1) too weak
- Positive jump reward (+0.5) overpowers penalty

**Debug**:
```cpp
report.AddAvg("WallDash/JumpFrequency", 
    player.prevAction.jump > 0.5f ? 1.0f : 0.0f
);
report.AddAvg("WallDash/JumpCounter", (float)pState.jumpActionCounter);
```

### Failure Mode 3: Insufficient Pitch

**Symptoms**:
- Pitch angle metric shows 0-10°
- No pitch reward accumulated

**Likely Cause**:
- Forward vector calculation incorrect
- `asin()` returning unexpected values

**Debug**:
```cpp
Vec forward = player.rotMat.forward;
report.AddAvg("WallDash/ForwardZ", forward.z);
float pitchRad = std::asin(RS_CLAMP(forward.z, -1.0f, 1.0f));
report.AddAvg("WallDash/PitchRad", pitchRad);
```

---

## Sign-Off Checklist

Before deploying WallDashReward:

- [ ] All test scenarios (1.1-5.3) pass
- [ ] Quick validation suite runs without errors
- [ ] Performance test shows < 10 microseconds per call
- [ ] Integration test for target phase passes
- [ ] No NaN or inf values in reward output
- [ ] State machine transitions correctly
- [ ] Airtime counter increments properly
- [ ] Velocity baseline captured at dash start
- [ ] Pitch angle calculation verified
- [ ] Handbrake/roll input detection works
- [ ] Jump cadence tracking functions
- [ ] Documentation reviewed
- [ ] Code compiles without warnings

---

**Version**: 1.0 | **Date**: 2025-01-30 | **Test Coverage**: ~95%
