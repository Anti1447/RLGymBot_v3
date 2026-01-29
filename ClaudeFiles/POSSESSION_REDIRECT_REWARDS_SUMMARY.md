# POSSESSION REDIRECT REWARDS - HIGH-LEVEL SUMMARY

## OVERVIEW

Three related but distinct possession mechanics that all share a **core angled redirect** from a rolling ball, but differ in strictness, intent, and reward magnitude.

All three rewards are built on the same foundation:
- Ball must be rolling on ground (800-1600 uu/s horizontal, z < 100)
- Car approaches from an angle to the ball's velocity vector
- Car touches ball and changes its direction
- Outcome verification determines which mechanic it is

---

## 1. DIRECTION CHANGE (Flexible Evasion/Reset)

**Intent**: Keep possession while evading opponent pressure or resetting play

**Core Mechanic**:
- Ball rolling in ANY direction
- Car approach angle: WIDE range (15-60° from ball's velocity vector)
- Touch redirects ball in ANY direction EXCEPT toward our own goal
- Ball speed: MAINTAINED or slightly reduced (-100 to +100 uu/s change)

**Examples of Successful Direction Changes**:
```
Example 1: Rolling toward goal, dodge opponent challenge
├─ Ball rolling straight at goal (z = 50, vel = 1000 uu/s toward goal)
├─ Opponent car rushing to block
├─ Your car hits from 30° angle
├─ Ball redirects to opposite corner (now moving away from goal)
├─ Speed drops to 900 uu/s (controlled touch)
└─ Result: Possession maintained, opponent overcommitted to wrong direction

Example 2: Rolling along left wall, create space
├─ Ball rolling parallel to wall on left side
├─ Defenders positioned ahead
├─ Your car hits from 45° angle (sharp redirect)
├─ Ball redirects sharply right toward center field
├─ Speed maintained around 950 uu/s
└─ Result: Ball reset to center, opponent positioning wasted

Example 3: Rolling away from goal, change angle
├─ Ball rolling away from goal (negative Y velocity)
├─ Need to change angle but keep possession
├─ Hit from 20° angle (shallow redirect)
├─ Ball now rolling along wall instead of center
├─ Speed slightly increased to 1050 uu/s
└─ Result: Different angle of attack, setup for next play
```

**Verification Checklist**:
- ✓ Ball touching this frame
- ✓ Ball was rolling (800-1600 uu/s horizontal, z < 100)
- ✓ Angle from ball's velocity: 15-60°
- ✓ Speed change: -100 to +100 uu/s
- ✓ Direction changed significantly (dot product < 0.8 with original)
- ✓ NOT redirected toward our goal (defensive safety)

**Metrics**:
- Attempts: Number of direction changes executed
- Success rate: (Valid redirects / Attempts)
- Average angle used: Statistical insight into play style

**Reward**: 0.3-0.5x (Small, frequent)

---

## 2. HOOK SHOT (Strict Offensive Redirect)

**Intent**: Execute specific angled shot toward goal after ball setup

**Core Mechanic**:
- Ball rolling in NON-goal direction (must be setup phase, not natural approach)
- Car approach angle: STRICT 30±15° from ball's velocity vector
- Touch redirects ball TOWARD OPPONENT'S GOAL specifically
- Ball speed: INCREASED significantly (+200+ uu/s change)
- Upward velocity: REQUIRED (z-component > 500 uu/s)

**Examples of Successful Hook Shots**:
```
Example 1: Ball rolling left (away from goal), hook to goal
├─ Ball rolling parallel to left wall, velocity = 1000 uu/s left
├─ If left alone: ball would never reach goal
├─ Your car positioned 30±15° from ball's path
├─ Hit ball with angled upward trajectory
├─ Ball velocity after: 500 uu/s toward goal, z = 600 uu/s upward
├─ Speed increased from 1000 to 1200 uu/s total
└─ Result: Offensive shot from defensive setup, high-risk shot

Example 2: Ball rolling backward (negative Y), hook to goal
├─ Ball rolling away from opponent goal (toward our goal side)
├─ Your car at 30±15° angle relative to ball's backward motion
├─ Hit with upward component (flip shot, aerial hit, etc.)
├─ Ball redirected 180° to goal direction
├─ Final velocity: 800 uu/s toward goal, z = 650 uu/s
└─ Result: Complete direction reversal + shot, aggressive play

Example 3: Ball rolling at shallow angle (not toward goal), hook shot
├─ Ball rolling at 45° angle relative to goal direction
├─ Car hits from 30±15° angle offset
├─ Contact imparts upward velocity and goal direction
├─ Ball speed increases 200+ uu/s
├─ Final direction: dot product with goal direction > 0.5
└─ Result: Angled shot toward goal, proper hook mechanics
```

**Verification Checklist**:
- ✓ Ball touching this frame
- ✓ Ball was rolling (800-1600 uu/s horizontal, z < 100)
- ✓ Ball NOT naturally pointing at goal before touch (dot with goal < 0.3)
- ✓ Angle from ball's velocity: STRICT 30±15°
- ✓ Speed increased: +200+ uu/s change
- ✓ Upward velocity: z-component > 500 uu/s
- ✓ Direction redirected to goal: dot product with goal direction > 0.5

**Metrics**:
- Attempts: Number of hook shots executed (touches meeting all conditions)
- On-goal rate: (Redirects pointing at goal / Attempts)
- Average speed increase: Insight into shot power
- Successful goal scorer: If goal is scored immediately after

**Reward**: 1.0-2.0x (Large, less frequent)

---

## 3. DOOMSEE DISH (Corner Setup + Aerial Finish)

**Intent**: Execute complex corner setup + backboard aerial to score

**Core Mechanic - Phase 1: Ground Setup**:
- Ball rolls from center field or perpendicular approach
- Uses front flips or speed-matching to keep ball grounded
- Ball velocity: 800-1600 uu/s
- Destination: Ball reaches opponent's corner
- Condition: Ball reaches corner zone, still rolling at proper speed

**Core Mechanic - Phase 2: Wall Liftoff Tap**:
- Timing: AFTER ball crosses ground-to-wall transition zone
- Ball is now on corner wall surface (rolling up wall)
- Light finesse tap to pop ball upward
- Result: Ball is airborne, moving toward goal, near backboard
- Condition: Ball lifts off wall, gains upward velocity, goal-directed

**Core Mechanic - Phase 3: Backboard Aerial Finish**:
- Bot positions on backboard
- Jumps off backboard (wall jump)
- Matches ball's momentum/trajectory while airborne
- Positions slightly away from wall (dodge space)
- Boosts/aerials into flip or direct hit into net
- Result: Ball scores after backboard ricochet

**Examples of Successful Doomsee Dishes**:
```
Example 1: Center to corner to backboard, basic version
├─ Phase 1: Bot rolls ball from center toward opponent's right corner
│           Ball velocity: 1200 uu/s, staying on ground
├─ Ball reaches corner zone (x > 3500, y > 4500)
├─ Phase 2: Bot taps as ball transitions to wall
│           Ball pops to z = 400, velocity toward goal
├─ Ball floating near backboard, still moving forward
├─ Phase 3: Bot jumps from backboard, matches ball momentum
│           Flips/boosts into ball, redirects toward net
└─ Result: Goal scores, complete sequence executed

Example 2: Perpendicular approach (easier setup)
├─ Phase 1: Bot approaches corner parallel to wall
│           Easier rolling angle than head-on center approach
│           Ball rolls along wall direction (400, 3000) velocity
├─ Ball reaches corner (easier entry angle)
├─ Phase 2: Pop tap as wall transition occurs
│           Ball lifts off wall toward goal
├─ Phase 3: Backboard aerial finish
│           Bot has better positioning due to easier approach
└─ Result: Goal scores, preferred route when available

Example 3: Failed doomsee (partial reward potential)
├─ Phase 1: Successfully rolls to corner
│           Gets full phase 1 reward: 0.3x
├─ Phase 2: Pop tap is slightly off, ball goes too high/low
│           Phase 2 partial reward: 0.2x
├─ Phase 3: Bot can't reach backboard for aerial
│           Phase 3 no reward: 0.0x
└─ Result: Some reward, but no goal (teaches the sequence progression)
```

**Verification by Phase**:

**Phase 1 - Ground Setup**:
- ✓ Ball rolling (800-1600 uu/s)
- ✓ Ball on ground (z < 100)
- ✓ Ball approaching opponent's corner

**Phase 2 - Wall Liftoff Tap**:
- ✓ Ball crossed transition zone (on wall surface)
- ✓ Touch this frame lifts ball
- ✓ Ball now airborne (z > 200-300)
- ✓ Ball velocity directed toward goal (not away)

**Phase 3 - Backboard Aerial Finish**:
- ✓ Bot on or near backboard
- ✓ Bot jumped (airborne)
- ✓ Bot touches ball
- ✓ Ball velocity changes toward goal
- ✓ Goal is scored (or ball goes in net)

**Metrics**:
- Phase 1 completions: Balls successfully rolled to corner
- Phase 2 completions: Successful wall pop-ups
- Phase 3 completions: Successful backboard aerials
- Overall success rate: (Goals / Setup attempts)
- Average phase time: Diagnostic for each stage

**Reward**: 1.5-2.5x (Largest, most complex, least frequent)

---

## ANTI-FARMING SAFEGUARDS

**All three rewards share these protections**:

1. **One-frame verification**: All conditions checked within single frame
2. **No intermediate accumulation**: Only reward on successful touch with correct outcome
3. **Speed/direction verification**: Can't farm by getting near ball
4. **Angle requirement**: Direction change has wide angles but hook shot + doomsee have strict angles
5. **Outcome-based**: Reward tied to actual ball direction change, not proximity

**Farming vectors BLOCKED**:
- ✗ Can't accumulate rolling rewards by dribbling forever
- ✗ Can't accumulate positioning rewards by driving at angles without touching
- ✗ Can't accumulate by repeatedly touching at wrong speeds/angles
- ✗ Can't accumulate by touches that don't change ball direction

---

## INTEGRATION SUMMARY

### Where to Use:

**Direction Change**: All phases (Phase 3+)
- Weight: 8-12
- Use ZeroSum: Yes (competitive advantage)
- Frequency: High (many attempts per game)

**Hook Shot**: Phases 3-5 (Gold → Diamond)
- Weight: 10-15
- Use ZeroSum: Yes (competitive advantage)
- Frequency: Medium (several per game)
- Phase out or reduce at higher levels

**Doomsee Dish**: Phases 5-9 (Diamond → Pro)
- Weight: 15-25
- Use ZeroSum: Yes (competitive advantage)
- Frequency: Low (1-3 per game)
- Introduce later when bot has ground/aerial control

### Configuration Template:

```cpp
case TrainingPhase::PHASE_4_PLATINUM:
    return std::vector<WeightedReward>{
        // ... existing rewards ...
        
        // Possession Redirects
        WeightedReward(new ZeroSumReward(new DirectionChangeReward(), 1.0f), 10.0f),
        WeightedReward(new ZeroSumReward(new HookShotReward(), 1.0f), 12.0f),
        
        // ... rest of rewards ...
    };

case TrainingPhase::PHASE_5_DIAMOND:
    return std::vector<WeightedReward>{
        // ... existing rewards ...
        
        // Possession Redirects
        WeightedReward(new ZeroSumReward(new DirectionChangeReward(), 1.0f), 8.0f),
        WeightedReward(new ZeroSumReward(new HookShotReward(), 1.0f), 10.0f),
        WeightedReward(new ZeroSumReward(new DoomseeDisHReward(), 1.0f), 18.0f),
        
        // ... rest of rewards ...
    };
```

### Metrics Reporting:

```cpp
{
    auto m = DirectionChangeReward::GetAndResetMetrics();
    if (m.attempts > 0) {
        report.AddAvg("PossessionRedirect/DirectionChange/Attempts", (float)m.attempts);
        report.AddAvg("PossessionRedirect/DirectionChange/SuccessRate", m.GetSuccessRate());
    }
}

{
    auto m = HookShotReward::GetAndResetMetrics();
    if (m.attempts > 0) {
        report.AddAvg("PossessionRedirect/HookShot/Attempts", (float)m.attempts);
        report.AddAvg("PossessionRedirect/HookShot/OnGoalRate", m.GetOnGoalRate());
    }
}

{
    auto m = DoomseeDisHReward::GetAndResetMetrics();
    if (m.attempts > 0) {
        report.AddAvg("PossessionRedirect/Doomsee/SetupAttempts", (float)m.setupAttempts);
        report.AddAvg("PossessionRedirect/Doomsee/CompletionRate", m.GetCompletionRate());
        report.AddAvg("PossessionRedirect/Doomsee/GoalRate", m.GetGoalRate());
    }
}
```

---

## EXPECTED LEARNING PROGRESSION

### Direction Change:
- Phase 3-4: Bot learns angle approach, starts making redirects (5-20% success)
- Phase 5+: Bot recognizes evasion opportunities naturally (50%+ success)
- Note: Frequent enough that bot practices constantly

### Hook Shot:
- Phase 3-4: Bot learns strict angle requirement, occasional shots (2-10% success)
- Phase 4-5: Bot begins chaining possession + angled shots (10-30% success)
- Phase 6+: Reliable execution on setup balls (30-50%+ success)
- Note: Rarer than direction change, requires specific setup

### Doomsee Dish:
- Phase 5-6: Bot learns individual phases separately (0-5% completion)
- Phase 6-7: Bot chains phases together (5-20% completion)
- Phase 7+: Reliable execution, reads corner opportunities (20-50% completion)
- Note: Rarest, most rewarding, highest skill requirement

