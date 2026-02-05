# Overcommitment Training: Three-Phase Framework
## Key Updates for Ball Contest & Kickoff Exceptions

---

## Overview

Three progressive levels of overcommitment detection and punishment, designed to teach both the bot and opponent exploitation:
- **Phase 1 (Simple)**: Resource gates + sampling every 0.25s
- **Phase 2 (Advanced)**: Motion lock detection + trajectory analysis + sampling every 0.25s
- **Phase 3 (Expert)**: Full predictive model + team coordination + sampling every 0.5s

---

## Critical Exceptions (All Phases)

### Exception 1: Ball Contest Margin
```
IF ourDistance <= closestOpponentDistance + BALL_CONTEST_MARGIN (e.g., 100 units):
    SKIP PENALTY  // We're contesting the ball legitimately
```

**Why**: When you're closest to the ball (within ~100 units), aggressive commits are JUSTIFIED. No penalty.

### Exception 2: Kickoff Formations
```
IF isKickoffFormation(player):
    SKIP PENALTY  // All players are equidistant by design
```

**Why**: Kickoff formations intentionally space players equally. Don't penalize normal kickoff behavior.

### Exception 3: Teammate Deference
```
IF closestTeammateDistance < ourDistance - DEFER_MARGIN (e.g., 200 units):
    SKIP PENALTY  // Teammate is handling it, we're supporting
```

**Why**: If a teammate is clearly closer, you're correctly playing support, not overcommitting.

---

## Phase 1: Simple (Every 0.25 seconds)

### Penalty Gates
1. **Flip in danger**: Don't flip far from ball with low boost
   - Exception: If opponent is close (< 300 units), flip is valid contest
   - **NEW Exception**: If dodging imminent demo/bump, flip is JUSTIFIED
   
2. **Double jump depletion**: Don't 2-jump when you can't recover
   - Exception: If in active contest (close to ball, opponent nearby)
   - **NEW Exception**: If dodging imminent demo/bump, double jump is JUSTIFIED
   
3. **Stranded in opp half**: Don't get stuck in opponent half with no boost
   - Exception: If pressuring opponent (< 500 units away)
   - Exception: If teammate has defensive coverage

4. **NEW - Corner challenge risk**: Don't challenge in opponent's corner unless you're closest to ball
   - Exception: If we're closest to ball (clearly our battle)
   - Exception: If opponent is close enough (< 800 units) that corner is active battle
   - Penalty: 12.0 (higher risk, low escape routes)

5. **NEW - Goal net landing trap**: Don't land in opponent goal/net with low boost after shot
   - Exception: If we just shot and can recover boost quickly (< 3 seconds)
   - Penalty: 14-15.0 (leaves teammates very vulnerable)
   - Rationale: You're stuck in goal and can't rotate back; teammates must cover

### Reward (Every 0.5s)
1. **Opponent used jumps**: Reward if they're outside a valid contest
2. **Opponent stranded**: Reward capitalizing on low-boost opponent
   - Exception: If we're too far (> 1500 units) to exploit quickly

---

## Phase 2: Advanced (Every 0.25 seconds)

Builds on Phase 1 with motion lock analysis:

### New Analysis: Flip Lock Window
```
IF player.isFlipping:
    projectedPos = projectMotion(player.pos, player.vel, flipDuration)
    ballDist = distance(projectedPos, ball.pos)
    
    IF ballDist > MEANINGFUL_INTERACTION_DISTANCE (e.g., 500):
        PENALIZE
```

**Exception**: Flip will reach the ball → skip penalty

### New Analysis: Aerial Commitment
```
IF player.isInAir AND player.vel.z < 0 (falling):
    playerZAtBall = projectZPosition(player, ball, timeToGround)
    
    IF abs(playerZAtBall - ballHeight) > AERIAL_TOUCH_MARGIN (e.g., 200):
        PENALIZE (hard miss)
```

**Exception**: Denying opponent aerial (even if we miss) → skip penalty

### New Analysis: Boost Depletion Trajectory
```
IF player.boost > 0:
    projectedPosAtEmpty = projectMotion(player, boostVelocity, boostTimeToEmpty)
    
    IF inOpponentHalf(projectedPosAtEmpty) AND distToOwnGoal > SAFE_DISTANCE:
        PENALIZE (stranded when boost runs out)
```

**Exceptions**:
- If opponent is within demo range at boost-empty point
- If we have line-of-sight to own goal (defensive positioning)

### Reward: Opponent Overcommitment (Every 0.5s)
1. **Opponent flip lock**: Reward winning contested space during opponent flip
   - Exception: If opponent's flip reaches the ball
   
2. **Opponent aerial miss**: Reward if opponent flying to unreachable height
   
3. **Opponent stranded**: Reward capitalizing
   - Exception: If we're also too far (> 2000 units) away to capitalize

---

## Phase 3: Expert (Every 0.5 seconds)

Full decision tree analysis with team coordination.

### Pre-Check Gates (Like Phase 1 & 2)
- Skip if we're closest to ball
- Skip if in kickoff formation
- Skip if teammate is closer

### New: Commitment Chain Analysis
```
commitmentChain = [
    action=flip,
    nextPotentialActions=[aerial, land, dodge],
    recoveryOptions=...
]

FOR EACH action:
    // Adjust threshold if we're in a close contest
    IF ourDistance < closestOpponentDistance + 200:
        contestAdjustment = 0.6  // Relax penalty by 40%
    
    IF actionValue < MIN_VALUE * contestAdjustment \
       AND actionCost > RESOURCE_THRESHOLD \
       AND actionLock > LOCK_THRESHOLD \
       AND informationQuality < INFO_THRESHOLD:
        PENALIZE
```

**Key**: Close contests allow lower-value actions (60% threshold adjustment).

### New: Team Coverage Check
```
teamCoverageRating = calculateTeamCoverageOfDefense(player, teammates, ball)

IF teamCoverageRating > 0.8:
    penaltyMultiplier = 0.5  // Teammates cover, commits are safer
ELSE:
    penaltyMultiplier = 1.0  // Commits are riskier
```

### New: Worst-Case Danger Analysis
```
worstCaseScenario = simulateCommitmentFailure(player, commitment)

IF worstCaseScenario.ownGoalDanger > DANGER_THRESHOLD:
    // Exception: adjust danger based on opponent distance
    opponentDanger = minDistance(opponents, landingPos) / 3000.0
    adjustedDanger = danger * RS_MAX(opponentDanger, 0.5)
    
    PENALIZE(25.0 * adjustedDanger)
```

**Key**: Dangerous worst-cases are heavily penalized, unless opponents are far away.

### Reward: Opponent Exploitation (Every 0.5s)

Pre-checks:
```
IF opponentDistance <= ourClosestDistance + BALL_CONTEST_MARGIN:
    SKIP  // Opponent is making sound decision
IF isKickoffFormation(opponent):
    SKIP  // Normal kickoff play
```

#### Exploit Score Calculation
```
exploitScore = 0.0
exploitScore += geometricAdvantage * 10.0
exploitScore += timeAdvantage * 5.0
exploitScore += bestCaseOutcome * 0.5

IF exploitScore > GOOD_EXPLOIT_THRESHOLD (e.g., 30.0):
    IF ourTeamExecutesResponse:
        REWARD 30.0  // Excellent read + execution
    ELSE IF ourTeamMakesProgress:
        REWARD 15.0  // Partial credit
```

#### Recovery Chain Disruption
```
FOR EACH recoveryStageInChain:
    recoveryLock = getRecoveryLockDuration(stage)
    
    IF recoveryLock < 0.15:
        CONTINUE  // Too short to matter
    
    IF weCanInterruptDuring(recoveryLock):
        REWARD 25.0  // High value: demo during recovery
```

---

## Critical New Exceptions (Phase 1+)

### Demo/Bump Avoidance
```cpp
IF isDodgingImminentDemoOrBump(player, opponents):
    // Flip or double jump to avoid demo = JUSTIFIED
    // Even if it uses resources or is far from ball
    SKIP_PENALTY
```

**Implementation**:
```
FOR EACH opponent:
    dirToUs = (player.pos - opponent.pos).Normalized()
    opponentVelTowardUs = opponent.vel.Dot(dirToUs)
    
    IF opponentVelTowardUs > 1500.0:  // Approaching fast
        distToOpponent = distance(opponent.pos, player.pos)
        timeToContact = distToOpponent / opponentVelTowardUs
        
        IF timeToContact < 0.5:  // Imminent (within ~0.5 seconds)
            RETURN true  // Dodge is justified
```

**Why**: Survival > resource optimization. Using boost/jumps to avoid demo is always the right call.

---

### Opponent Corner Challenge Risk
```cpp
IF inOpponentCorner(player.pos) AND player is NOT closest to ball:
    // Challenging in corner when you're not closest = HIGH RISK
    // Corners have low maneuverability, easy to trap
    
    IF closestOpponentDistance < 800:
        // Opponent is engaged in corner battle, you should be too
        SKIP_PENALTY
    ELSE:
        // You're challenging the corner without a good reason
        PENALIZE(severity=12.0)  // Higher penalty than typical overcommit
```

**Corner Detection**:
```
CONST CORNER_X_THRESHOLD = 3500.0   // Near side walls (±4096)
CONST CORNER_Y_THRESHOLD = 4000.0   // Deep into opponent half

inCorner = (abs(pos.x) > 3500) AND (abs(pos.y) > 4000)
```

**Why**: Opponent's corners are notorious for "sticky" fights. If you're not the one fighting for the ball, you're just getting trapped. Penalty teaches the bot to stay clear unless it's their ball.

---

### Goal Net Landing Trap
```cpp
IF inOpponentGoalArea(player.pos) AND player.boost < 50:
    // You're in the opponent goal/net with depleted boost
    // This is dangerous because:
    // 1. You can't rotate back quickly
    // 2. Teammates must overextend to defend
    // 3. Opponent can counter-attack from behind you
    
    IF recentlyAttemptedShot(player, 0.5 seconds):
        // You just shot/flicked, being in goal is expected
        timeToRecover = (50 - player.boost) / BOOST_DEPLETION_RATE
        
        IF timeToRecover < 3.0:
            // Quick recovery, allow it
            SKIP_PENALTY
        ELSE:
            // Too long, teammates vulnerable
            PENALIZE(severity=15.0)
    ELSE:
        // Stuck in goal without attempting a shot
        // This should never happen
        PENALIZE(severity=14.0)
```

**Goal Area Detection**:
```
CONST GOAL_WIDTH = 900.0
CONST GOAL_LINE_Y = 5120.0
CONST BACK_NET_Y = 6000.0

inGoalX = abs(pos.x) < 900
inGoalY = pos.y > 5120 AND pos.y < 6000
inGoalZ = pos.z < 843  // Goal height + buffer

inGoal = inGoalX AND inGoalY AND inGoalZ
```

**Why**: Landing in the opponent net leaves your team vulnerable to counter-attacks. If you're going to commit to a shot, you better be able to rotate back quickly. High penalty teaches the bot to plan exit routes during aerials/flicks.

---

## Recommended Implementation Additions for Phase 1

Add these helper functions to your codebase:

```cpp
// Imminent demo detection
bool isDodgingImminentDemoOrBump(const Player& player, 
                                  const std::vector<Player>& opponents) {
    const float APPROACH_THRESHOLD = 1500.0f;  // Opponent velocity toward us
    const float TIME_TO_CONTACT_MAX = 0.5f;
    
    for (const auto& opponent : opponents) {
        Vec dirToUs = (player.pos - opponent.pos).Normalized();
        float velTowardUs = opponent.vel.Dot(dirToUs);
        
        if (velTowardUs > APPROACH_THRESHOLD) {
            float distToOpp = distance(opponent.pos, player.pos);
            float timeToContact = distToOpp / velTowardUs;
            
            if (timeToContact > 0 && timeToContact < TIME_TO_CONTACT_MAX) {
                return true;  // Opponent is going to hit us soon
            }
        }
    }
    return false;
}

// Corner zone detection
bool inOpponentCorner(const Vec& pos) {
    const float CORNER_X = 3500.0f;
    const float CORNER_Y = 4000.0f;
    
    bool isCorner = (std::abs(pos.x) > CORNER_X) && (std::abs(pos.y) > CORNER_Y);
    bool isOpponentHalf = pos.y > 0;  // Adjust based on your team
    
    return isCorner && isOpponentHalf;
}

// Goal area detection
bool inOpponentGoalArea(const Vec& pos) {
    const float GOAL_WIDTH = 900.0f;
    const float GOAL_LINE_Y = 5120.0f;
    const float BACK_NET_Y = 6000.0f;
    const float GOAL_HEIGHT = 843.0f;
    
    bool inX = std::abs(pos.x) < GOAL_WIDTH;
    bool inY = pos.y > GOAL_LINE_Y && pos.y < BACK_NET_Y;
    bool inZ = pos.z < GOAL_HEIGHT + 200.0f;
    
    return inX && inY && inZ;
}
```

---

| Scenario | Phase 1 | Phase 2 | Phase 3 | Rationale |
|----------|---------|---------|---------|-----------|
| Closest to ball, tight contest | SKIP | SKIP | SKIP | Aggressive play is justified |
| Kickoff formation spacing | SKIP | SKIP | SKIP | Design intent |
| Teammate much closer | SKIP | SKIP | SKIP | Let teammate handle it |
| **Dodging imminent demo/bump** | **SKIP** | **SKIP** | **SKIP** | **Survival > resource management** |
| Flip reaches ball | PENALIZE | SKIP | SKIP | Motion lock analysis improves |
| Flipped too far away | PENALIZE | PENALIZE | PENALIZE | Clear overcommitment |
| **Challenging in opponent corner, not closest to ball** | **PENALIZE (12.0)** | - | - | **High-risk zone, low escape** |
| **Landing in goal/net with low boost** | **PENALIZE (14-15.0)** | - | - | **Leaves team vulnerable** |
| Denying opponent aerial | PENALIZE | SKIP | SKIP | Defensive value recognized |
| Worst case + opponent far away | PENALIZE | PENALIZE | REDUCED | Distance reduces danger |
| Opponent flip lock window | N/A | REWARD | REWARD | Exploit locked-out opponent |

---

## Integration into ExampleMain.cpp

### Phase 4 (Current)
```cpp
WeightedReward(new SimpleOvercommitmentPenalty(), 15.0f),
WeightedReward(new CapitalizeOnOpponentOvercommitment(), 12.0f),
```

### Phase 5-6
```cpp
WeightedReward(new AdvancedOvercommitmentPenalty(), 18.0f),
WeightedReward(new AdvancedOpponentOvercommitmentReward(), 16.0f),
```

### Phase 7+
```cpp
WeightedReward(new ExpertOvercommitmentPenalty(), 20.0f),
WeightedReward(new ExpertOpponentOvercommitmentReward(), 20.0f),
```

---

## Implementation Notes

## Implementation Notes

### Sampling Intervals: 0.25s is Perfect
- **Phase 1 penalty**: 0.25s (30 ticks @ 120 Hz) ✓
- **Phase 1 reward**: 0.5s (60 ticks)
- **Phase 2**: 0.25s (penalty), 0.5s (reward)
- **Phase 3**: 0.5s (both penalty and reward)

**Why 0.25s is ideal** (your observation):
- Demo detection needs ~0.5s window to look ahead → sampling at 0.25s catches imminent demos
- Corner challenges change state slowly → 0.25s updates are sufficient
- Goal net landing check needs recency → 0.25s keeps tracking fresh
- Computational cost: 5-7 checks per 0.25s = ~20-28 checks per second (very cheap)
- Resolution: Good enough to catch mistakes without being reactive per-frame

### Ball Contest Margin (~100 units)
- This is approximately **1 car length**
- Allows teammates to contest simultaneously
- Matches human-level "tied possession" scenarios

### Kickoff Detection
Use built-in kickoff state detection. Can check:
```cpp
bool isKickoffFormation(const Player& p) {
    // Check if player position matches any of CommonValues::KICKOFF_*_CORNER/etc
    // Already available in your codebase
}
```

### Motion Lock Durations
- **Flip**: 0.4-0.6 seconds (full flip duration)
- **Aerial**: time to landing or time to ball contact
- **Double jump recovery**: (30 - current_boost) / BOOST_DEPLETION_RATE

---

## Final Recommendation for Phase 4

Start with **Phase 1 (Simple)** since:
1. You're still optimizing ground/air mechanics
2. Low compute cost (2-3 checks per 0.25s)
3. Teaches resource management clearly
4. Upgrade to Phase 2 around Phase 5-6 when you want subtlety

Expected impact:
- Bot learns to respect boost economy
- Fewer "yolo" flips that miss the ball
- Better positioning in own half
- Cleaner transitions between ground and air play
