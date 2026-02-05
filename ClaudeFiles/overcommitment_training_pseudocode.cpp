// ============================================================================
// OVERCOMMITMENT TRAINING: THREE PHASES OF COMPLEXITY
// ============================================================================
// High-level pseudocode for penalizing bad commitments and rewarding 
// defensive reads of opponent mistakes
//
// Key insight: Overcommitment = taking an action that locks you into a bad state
// without sufficient information or resources to recover
// ============================================================================

// ============================================================================
// PHASE 1: SIMPLE - BASIC RESOURCE GATES
// ============================================================================
// Focus: Don't flip/jump when you can't recover
// Cost: ~2-3 simple checks every 0.25-0.5 seconds (NOT every frame)
//
// CRITICAL EXCEPTIONS:
// - Player is closest to ball (or tied with small margin ~100 units)
// - Player is in kickoff formation (all players are equidistant by design)
// - Commitment is for contest (50/50, demo attempt) and player has position advantage
// - Teammate is nearer to ball (defer to teammate's decision)

/*
PSEUDO SimpleOvercommitmentPenalty:
    
    // ===== SAMPLING GATE =====
    static lastOvercommitCheckTime = 0.0
    CHECK_INTERVAL = 0.25  // seconds, or 30 ticks at 120 Hz
    
    IF (currentTime - lastOvercommitCheckTime) < CHECK_INTERVAL:
        RETURN  // Skip this frame
    lastOvercommitCheckTime = currentTime
    
    FOR EACH PLAYER:
        
        // ===== EXCEPTION 1: Check if we're actually closest =====
        ourDistance = distance(player.pos, ball.pos)
        closestOpponentDistance = minDistance(opponents, ball.pos)
        closestTeammateDistance = minDistance(teammates_except_self, ball.pos)
        
        // If we're closest (or within margin), overcommitment is JUSTIFIED
        IF ourDistance <= closestOpponentDistance + BALL_CONTEST_MARGIN (e.g., 100):
            // We're in a valid contest position, don't penalize
            CONTINUE  // Skip to next player
        
        // ===== EXCEPTION 2: Check for kickoff =====
        IF isKickoffFormation(player):
            // All players are equidistant by design, don't penalize
            CONTINUE
        
        // ===== EXCEPTION 3: Check if we should defer to teammate =====
        IF closestTeammateDistance < ourDistance - TEAMMATE_DEFER_MARGIN (e.g., 200):
            // Teammate is clearly closer, we're supporting (not committing)
            CONTINUE
        
        // ===== GATE 1: Flip in danger zones =====
        IF player.isFlipping AND player.boost < FLIP_MIN_BOOST_THRESHOLD (e.g., 20):
            IF distanceToBall > SAFE_FLIP_DISTANCE (e.g., 1000):
                // You flipped far from ball with no recovery boost
                // BUT: exception if we're contesting (opponent is very close)
                IF closestOpponentDistance < 300:
                    // Opponent is close, this flip is valid
                    CONTINUE
                
                // Exception: flip is valid if dodging an imminent demo/bump
                IF isDodgingImminentDemoOrBump(player, opponents):
                    // Using flip/jump to avoid demo = justified
                    CONTINUE
                
                penalizeReward(severity=10.0)
        
        // ===== GATE 2: Double jump without boost when far from goal =====
        IF player.doubleJumpUsed AND player.boost < DOUBLE_JUMP_RECOVERY_BOOST (e.g., 30):
            IF NOT inDefensivePosition(player):
                // Exception: if we're in a contested ball situation
                IF closestOpponentDistance < 300 AND ourDistance < closestOpponentDistance + 100:
                    // Valid contest, don't penalize
                    CONTINUE
                
                // Exception: double jump is justified if dodging demo/bump
                IF isDodgingImminentDemoOrBump(player, opponents):
                    // Using both jumps to avoid demo = justified
                    CONTINUE
                
                // You used both jumps with no boost to recover
                penalizeReward(severity=8.0)
        
        // ===== GATE 3: Opp half without rotation pathway =====
        IF player.posY > OPPONENT_HALF_Y AND player.boost < OPP_HALF_BOOST_MIN (e.g., 25):
            // Exception: if we're pressuring the opponent
            distToClosestOpponent = minDistance(opponents, player.pos)
            IF distToClosestOpponent < 500:
                // Active pressure/demo attempt, don't penalize
                CONTINUE
            
            // Exception: if our teammate is covering defense
            IF teamHasGoalSideCoverage(player):
                CONTINUE
            
            IF NOT hasPathToDefense(player):
                // You're deep in opponent half with depleted boost
                penalizeReward(severity=5.0)
        
        // ===== GATE 4: Challenging in opponent's corner (high risk zone) =====
        IF inOpponentCorner(player.pos):
            // Challenging in corners is risky - low room to maneuver
            // Exception: if we're closest to ball (clearly our ball to fight for)
            IF ourDistance > closestOpponentDistance + BALL_CONTEST_MARGIN:
                // We're not closest to ball
                // Exception: if closest opponent is close enough that corner battle is justified
                IF closestOpponentDistance < 800:
                    // Active corner battle, don't penalize
                    CONTINUE
                
                // We're challenging in the corner when we shouldn't be
                // Extra penalty because corners are high-risk, low-escape areas
                penalizeReward(severity=12.0)
        
        // ===== GATE 5: Landing in opponent's goal/net with low boost (post-shot) =====
        IF inOpponentGoalArea(player.pos) AND player.boost < RECOVERY_BOOST_THRESHOLD (e.g., 50):
            // We're in the goal area with depleted boost after an action
            // This is dangerous because we can't recover quickly
            
            // Exception: if this was intentional (just scored or trying to score)
            IF recentlyAttemptedShot(player, RECENT_WINDOW_SECONDS=0.5):
                // Recent aerial/flick/shot, being in goal is expected
                // But check: can we recover to help defense?
                timeToRecoverBoost = (RECOVERY_BOOST_THRESHOLD - player.boost) / BOOST_DEPLETION_RATE
                
                IF timeToRecoverBoost > 3.0:
                    // Too long to recover, teammates are vulnerable
                    penalizeReward(severity=15.0)
                ELSE:
                    // We can recover quickly enough, allow it
                    CONTINUE
            ELSE:
                // Stuck in opponent goal without recent shot attempt
                // This is bad - leaves team vulnerable
                penalizeReward(severity=14.0)

PSEUDO rewardOpponentOvercommitment_Simple:
    
    // ===== SAMPLING GATE =====
    static lastOpponentCheckTime = 0.0
    CHECK_INTERVAL = 0.5  // seconds, check less frequently than penalty
    
    IF (currentTime - lastOpponentCheckTime) < CHECK_INTERVAL:
        RETURN
    lastOpponentCheckTime = currentTime
    
    FOR EACH OPPONENT:
        
        // ===== EXCEPTION 1: Check if opponent is closest to ball =====
        opponentDistance = distance(opponent.pos, ball.pos)
        ourClosestDistance = minDistance(ourTeam, ball.pos)
        
        // If opponent is closest, they're not overcommitting (they should be involved)
        IF opponentDistance <= ourClosestDistance + BALL_CONTEST_MARGIN:
            CONTINUE  // Skip, this is valid play
        
        // ===== EXCEPTION 2: Check for kickoff =====
        IF isKickoffFormation(opponent):
            CONTINUE
        
        // ===== GATE 1: Opponent used both jumps, no boost, NOT in contest =====
        IF opponent.doubleJumpUsed AND opponent.boost < 20:
            // Exception: if they're in a valid contest (close to ball, close to us)
            IF opponentDistance < 500 AND ourClosestDistance < opponentDistance + 150:
                // They're in a valid contest with us
                CONTINUE
            
            // They overcommitted outside of contest
            IF ball.pos is in attacking zone:
                rewardBonus(severity=15.0)  // Free touch opportunity
        
        // ===== GATE 2: Opponent is stranded in opponent half with no boost =====
        IF opponent.posY > OPPONENT_HALF_Y AND opponent.boost == 0:
            // Exception: if our team is also struggling to capitalize quickly
            ourTeamDistance = minDistance(ourTeam, ball.pos)
            IF ourTeamDistance > 1500:
                // Opponent is stranded but we can't reach them quickly anyway
                CONTINUE
            
            IF ourTeam can score within 2 seconds:
                rewardBonus(severity=8.0)  // Capitalize on stranded opponent
*/

// ============================================================================
// PHASE 2: ADVANCED - COMMITMENT CONTEXT & TRAJECTORIES
// ============================================================================
// Focus: Understand motion locks (flip lock, aerial commitment)
//        and penalize without escape routes
// Cost: ~5-7 per-check calculations (sampled every 0.25 seconds)
//
// KEY EXCEPTIONS (from Phase 1, refined):
// - Skip check if player is closest to ball (within ~100 units)
// - Skip check if in kickoff formation
// - Skip check if there's active ball contest within 300 units
// - Skip check if teammate is covering that position defensively
//
// Key additions from Phase 1:
// - Flip lock detection (you're mid-flip, can't input meaningfully)
// - Aerial commitment detection (airborne far from ground)
// - Commitment without escape route (boost is committed or depleted)

/*
PSEUDO AdvancedOvercommitmentPenalty:
    
    // ===== SAMPLING GATE =====
    static lastAdvancedCheckTime = 0.0
    ADVANCED_CHECK_INTERVAL = 0.25  // Check frequently enough to catch mistakes
    
    IF (currentTime - lastAdvancedCheckTime) < ADVANCED_CHECK_INTERVAL:
        RETURN
    lastAdvancedCheckTime = currentTime

    FOR EACH PLAYER:
        
        // ===== PRE-CHECK EXCEPTIONS =====
        ourDistance = distance(player.pos, ball.pos)
        closestOpponentDistance = minDistance(opponents, ball.pos)
        closestTeammateDistance = minDistance(teammates_except_self, ball.pos)
        
        // If we're clearly closest to ball, don't penalize
        IF ourDistance <= closestOpponentDistance + BALL_CONTEST_MARGIN (e.g., 100):
            CONTINUE
        
        // If in kickoff formation, all positions are equidistant
        IF isKickoffFormation(player):
            CONTINUE
        
        // If we're supporting a teammate who's closer, don't penalize
        IF closestTeammateDistance < ourDistance - 200:
            CONTINUE
        
        // ===== FLIP LOCK ANALYSIS =====
        IF player.isFlipping:
            flipTimeRemaining = getFlipRemainingDuration(player)
            
            // During flip, you can't dodge/jump/steer effectively
            // Calculate where you'll be when flip ends
            projectedPos = projectMotion(player.pos, player.vel, flipTimeRemaining)
            ballDist = distance(projectedPos, ball.pos)
            
            // Exception: flip is valid if it reaches the ball
            IF ballDist < MEANINGFUL_BALL_INTERACTION_DISTANCE (e.g., 500):
                CONTINUE  // This flip will reach the ball
            
            // Exception: flip is valid if we're in a contest
            IF closestOpponentDistance < 400 AND ourDistance < closestOpponentDistance + 150:
                CONTINUE  // Active contest, flip may be valuable
            
            // You're committing to a flip that won't reach the ball
            baseReward = 12.0
            
            // Extra penalty if low boost (can't adjust after flip)
            IF player.boost < 30:
                baseReward *= 1.5  // 18.0 total penalty
            
            // Extra penalty if flip takes us away from goal (offensive flip)
            distanceToOppGoal = distance(projectedPos, opponent_goal)
            IF distanceToOppGoal > 2000 AND ourDistance < closestOpponentDistance:
                // We're getting further from goal unnecessarily
                baseReward *= 1.2  // Stack penalties
            
            penalizeReward(severity=baseReward)
        
        // ===== AERIAL COMMITMENT ANALYSIS =====
        IF player.isInAir AND player.vel.z < 0:  // Falling
            // You're committed to this aerial touch opportunity
            timeToGround = calculateTimeToGround(player)
            airTimeUsed = player.airborneTime
            
            // Did you jump/boost specifically to reach this ball?
            IF airTimeUsed > 0.3:  // Meaningful air time investment
                
                // Exception: if teammate is closer and they can land first
                IF closestTeammateDistance < ourDistance AND timeToGround > 0.2:
                    // Teammate will get it, you're backing up
                    CONTINUE
                
                // Now check: can you actually do something useful?
                ballHeight = ball.pos.z
                playerZAtBall = projectZPosition(player, ball, timeToGround)
                
                IF abs(playerZAtBall - ballHeight) > AERIAL_TOUCH_MARGIN (e.g., 200):
                    // You committed to an aerial you can't make
                    // BUT: exception if we're pressuring and denying opponent touch
                    opponentAerialTime = calculateOpponentAerialCapability(opponents, ball)
                    IF opponentAerialTime < timeToGround + 0.1:
                        // We're denying opponent the aerial, even if we miss
                        // This is valid (denial play)
                        CONTINUE
                    
                    penalizeReward(severity=15.0)
                
                // Alternative: you'll miss the timing window
                timeToReachBall = calculateTimeToReachBall(player, ball)
                IF timeToReachBall > timeToGround + GROUND_CUSHION (e.g., 0.2):
                    // You're coming down before you can touch the ball
                    penalizeReward(severity=12.0)
        
        // ===== BOOST COMMITMENT ANALYSIS =====
        IF player.boost > 0:
            boostVelocity = calculateBoostTrajectory(player)
            boostTimeToEmpty = player.boost / BOOST_DEPLETION_RATE
            projectedPosAtEmpty = projectMotion(player.pos, boostVelocity, boostTimeToEmpty)
            
            // Where will you be when boost runs out?
            IF inOpponentHalf(projectedPosAtEmpty):
                distanceToOwnGoal = distance(projectedPosAtEmpty, ownGoal)
                
                // Exception: if opponent is within demo range when boost empties
                distToOpponentAtEmpty = minDistance(opponents, projectedPosAtEmpty)
                IF distToOpponentAtEmpty < 300:
                    // We'll be pressuring/demoing opponent, don't penalize
                    CONTINUE
                
                // Exception: if this boost path takes us to a good defensive position
                IF hasLineOfSightToOwnGoal(projectedPosAtEmpty):
                    // We're maintaining defensive positioning
                    CONTINUE
                
                // If you'll be stranded far from goal when boost depletes
                IF distanceToOwnGoal > SAFE_ROTATION_DISTANCE (e.g., 3000):
                    // You're committing boost to a position you can't recover from
                    penalizeReward(severity=7.0)

PSEUDO rewardOpponentOvercommitment_Advanced:
    
    // ===== SAMPLING GATE =====
    static lastOpponentAdvancedCheckTime = 0.0
    OPPONENT_CHECK_INTERVAL = 0.5
    
    IF (currentTime - lastOpponentAdvancedCheckTime) < OPPONENT_CHECK_INTERVAL:
        RETURN
    lastOpponentAdvancedCheckTime = currentTime

    FOR EACH OPPONENT:
        
        // ===== PRE-CHECK EXCEPTIONS =====
        opponentDistance = distance(opponent.pos, ball.pos)
        ourClosestDistance = minDistance(ourTeam, ball.pos)
        
        // If opponent is closest, they're making the right decision
        IF opponentDistance <= ourClosestDistance + BALL_CONTEST_MARGIN:
            CONTINUE
        
        // If in kickoff formation, skip
        IF isKickoffFormation(opponent):
            CONTINUE
        
        // ===== DETECT OPPONENT FLIP LOCK WINDOW =====
        IF opponent.isFlipping:
            opponentFlipTimeRemaining = getFlipRemainingDuration(opponent)
            opponentFlipLockEndTime = currentTime + opponentFlipTimeRemaining
            
            // Exception: if opponent's flip reaches the ball, don't reward
            projectedFlipPos = projectMotion(opponent.pos, opponent.vel, opponentFlipTimeRemaining)
            IF distance(projectedFlipPos, ball.pos) < 500:
                CONTINUE  // They're flipping to the ball, valid move
            
            // During this window, opponent has limited control
            // Reward us for creating an offensive opportunity
            IF ourTeamCanShootGoal() AND ourTeamActsBefore(opponentFlipLockEndTime):
                rewardBonus(severity=20.0)
            
            // Specifically: reward beating them to a 50/50
            IF ballIsDisputed:
                IF ourTeamReachesBallBefore(opponentFlipLockEndTime):
                    rewardBonus(severity=25.0)  // High value for winning locked-out duel
        
        // ===== DETECT OPPONENT AERIAL OVERSHOOT =====
        IF opponent.isInAir:
            opponentAerialWindow = getAerialTouchWindow(opponent)
            
            // Exception: if opponent can actually touch the ball
            IF ballHeight >= opponentAerialWindow.min AND ballHeight <= opponentAerialWindow.max:
                CONTINUE  // Valid aerial, don't reward
            
            // Opponent is flying to a ball height they can't actually touch
            // Free opportunity for us
            IF ourTeamCanMakePlay():
                rewardBonus(severity=18.0)
        
        // ===== DETECT OPPONENT STRANDED (NO BOOST, FAR FROM GOAL) =====
        distOppToOwnGoal = distance(opponent.pos, ownGoal)
        
        IF opponent.boost < 10 AND distOppToOwnGoal > 3500 AND inOpponentHalf(opponent):
            // Exception: if we're also too far away to capitalize
            ourTeamDistance = minDistance(ourTeam, ball.pos)
            IF ourTeamDistance > 2000:
                CONTINUE  // They're stranded but we can't reach quick enough
            
            // Opponent is stranded with no recovery resources
            // Reward us for capitalizing
            IF canScore() OR canCreateGap():
                rewardBonus(severity=16.0)
            
            // Extra reward if we specifically demo/bump the stranded opponent
            IF weCanDemoWithinSeconds(opponent, 3.0):
                rewardBonus(severity=20.0)  // High value play
*/

// ============================================================================
// PHASE 3: EXPERT - FULL PREDICTIVE MODEL WITH POSITION WEIGHTING
// ============================================================================
// Focus: Understand the entire decision tree and penalize when committing
//        without adequate information or positioning advantage
// Cost: ~15-20 calculations every 0.5 seconds (NOT every frame)
//
// Key additions:
// - Multi-stage commitment chains (flip → aerial → landing)
// - Team spatial coordination (are teammates positioned to cover?)
// - Information asymmetry (do we know where opponent is going?)
// - Risk/reward scoring (is this commit worth the potential downside?)
// - Ball contest awareness (relax penalties if we're closest to ball)
//
// CRITICAL EXCEPTIONS (inherited from Phase 1 & 2):
// - Skip if player is closest to ball (within ~100 units)
// - Skip if in kickoff formation
// - Skip if teammate is clearly closer

/*
PSEUDO ExpertOvercommitmentPenalty:
    
    // ===== SAMPLING GATE =====
    static lastExpertCheckTime = 0.0
    EXPERT_CHECK_INTERVAL = 0.5  // Less frequent, more expensive
    
    IF (currentTime - lastExpertCheckTime) < EXPERT_CHECK_INTERVAL:
        RETURN
    lastExpertCheckTime = currentTime

    FOR EACH PLAYER:
        
        // ===== PRE-CHECK EXCEPTIONS =====
        ourDistance = distance(player.pos, ball.pos)
        closestOpponentDistance = minDistance(opponents, ball.pos)
        closestTeammateDistance = minDistance(teammates_except_self, ball.pos)
        
        // If we're clearly closest to ball, skip analysis
        IF ourDistance <= closestOpponentDistance + BALL_CONTEST_MARGIN (e.g., 100):
            CONTINUE
        
        // If in kickoff formation, skip
        IF isKickoffFormation(player):
            CONTINUE
        
        // If teammate is clearly closer, skip (they handle it)
        IF closestTeammateDistance < ourDistance - 200:
            CONTINUE
        
        commitment = analyzeCurrentCommitment(player)
        
        // ===== COMMITMENT CHAIN ANALYSIS =====
        // A "commitment chain" is: action1 → action2 → recovery
        // e.g., flip → land → rotate, or jump → aerial → land → boost back
        
        commitmentChain = [
            action=flip,
            nextPotentialActions=[aerial, land, dodge],
            recoveryOptions=calculateRecoveryOptions(player),
            timeToRecover=player.boost / BOOST_DEPLETION_RATE
        ]
        
        // Score each action in the chain
        FOR EACH actionInChain:
            actionValue = calculateActionValue(action, ballState, fieldPosition)
            actionCost = calculateActionCost(action, player.resources)
            actionLock = getMotionLockDuration(action)
            
            // Core formula:
            // If (value < threshold) AND (cost is high) AND (lock is long)
            // AND (we lack information about ball/opponent)
            // → PENALIZE
            //
            // Exception: if we're in a meaningful contest (closest to ball within 200 units),
            // lower the penalty threshold significantly
            
            informationQuality = getFieldInformationQuality(player, ball, opponents)
            
            // Adjust threshold based on ball contest
            contestAdjustment = 1.0
            IF ourDistance < closestOpponentDistance + 200:
                // We're in a close contest, higher value actions are acceptable
                contestAdjustment = 0.6  // Relax the threshold by 40%
            
            IF actionValue < MIN_ACTION_VALUE * contestAdjustment (e.g., 5.0 * 0.6) \
               AND actionCost > RESOURCE_THRESHOLD (e.g., 30 boost) \
               AND actionLock > LOCK_THRESHOLD (e.g., 0.15 seconds) \
               AND informationQuality < REQUIRED_INFO_THRESHOLD (e.g., 0.6):
                
                // Full penalty calculation
                penaltyBase = 20.0
                penaltyBoost = (player.boost / actionCost) * 0.5  // Normalize by resource used
                penaltyTime = (actionLock / 0.5) * 0.3  // Normalize by lock duration
                
                finalPenalty = penaltyBase + penaltyBoost + penaltyTime
                penalizeReward(severity=finalPenalty)
        
        // ===== TEAM POSITIONING CHECK =====
        // Don't penalize commits if teammates cover defensive responsibility
        teamCoverageRating = calculateTeamCoverageOfDefense(player, teammates, ball)
        
        IF teamCoverageRating > 0.8:
            // Teammates have defense covered, commits are safer
            penaltyMultiplier = 0.5
        ELSE:
            penaltyMultiplier = 1.0  // Commits are riskier
        
        applyPenaltyMultiplier(penaltyMultiplier)
        
        // ===== PREDICTIVE RECOVERY ANALYSIS =====
        // Simulate: what if my commitment fails?
        worstCaseScenario = simulateCommitmentFailure(player, commitment)
        // e.g., you flip, miss the ball, land far from goal with no boost
        
        IF worstCaseScenario.ownGoalDanger > DANGER_THRESHOLD (e.g., 0.7):
            // Your commitment failure = likely goal against
            // Exception: if opponent is very far away, danger is lower
            opponentDanger = minDistance(opponents, worstCaseScenario.landingPos) / 3000.0
            adjustedDanger = worstCaseScenario.ownGoalDanger * RS_MAX(opponentDanger, 0.5)
            
            penalizeReward(severity=25.0 * adjustedDanger)
        
        // BUT: if worst case is recoverable (teammates cover, etc)
        IF worstCaseScenario.teamCanRecover:
            penalizeReward(severity=8.0)  // Lower penalty for recoverable situations

PSEUDO rewardOpponentOvercommitment_Expert:
    
    // ===== SAMPLING GATE =====
    static lastOpponentExpertCheckTime = 0.0
    OPPONENT_EXPERT_INTERVAL = 0.5
    
    IF (currentTime - lastOpponentExpertCheckTime) < OPPONENT_EXPERT_INTERVAL:
        RETURN
    lastOpponentExpertCheckTime = currentTime

    FOR EACH OPPONENT:
        
        // ===== PRE-CHECK EXCEPTIONS =====
        opponentDistance = distance(opponent.pos, ball.pos)
        ourClosestDistance = minDistance(ourTeam, ball.pos)
        
        // If opponent is closest, they're making sound decision
        IF opponentDistance <= ourClosestDistance + BALL_CONTEST_MARGIN:
            CONTINUE
        
        // If in kickoff formation, skip
        IF isKickoffFormation(opponent):
            CONTINUE
        
        opponentCommitment = analyzeCurrentCommitment(opponent)
        
        // ===== PREDICTIVE EXPLOITATION MODEL =====
        // Question: Can we abuse this commitment?
        
        // Step 1: Where will opponent be locked into?
        lockedPosition = projectCommitmentOutcome(opponentCommitment)
        
        // Step 2: How long is opponent vulnerable?
        vulnerabilityWindow = getCommitmentLockDuration(opponentCommitment)
        
        // Exception: if vulnerability window is very short (< 0.1s), not worth pursuing
        IF vulnerabilityWindow < 0.1:
            CONTINUE
        
        // Step 3: Can we exploit during vulnerability?
        ourOptimalResponse = findBestResponseToCommitment(opponentCommitment, team)
        
        // Step 4: Score the exploit opportunity
        exploitScore = 0.0
        
        // Factor A: Geometric advantage (can we reach contested space first?)
        geometricAdvantage = calculateGeometricAdvantage(ourTeam, opponent, lockedPosition)
        exploitScore += geometricAdvantage * 10.0
        
        // Factor B: Time advantage (do we have time to act before opponent recovers?)
        timeAdvantage = vulnerabilityWindow - getOurResponseTime(ourOptimalResponse)
        exploitScore += timeAdvantage * 5.0  // Seconds of free time
        
        // Factor C: Outcome value (if we execute, what's the best case?)
        bestCaseOutcome = calculateBestOutcome(ourOptimalResponse)
        exploitScore += bestCaseOutcome * 0.5  // e.g., score, demo, gap creation
        
        // Step 5: Reward if we actually exploit it
        IF exploitScore > GOOD_EXPLOIT_THRESHOLD (e.g., 30.0):
            
            // Now check: did we actually capitalize?
            IF ourTeamExecutesResponse(ourOptimalResponse):
                rewardBonus(severity=30.0)  // Excellent read and execution
            
            // Even if we don't fully capitalize, reward awareness
            IF ourTeamMakesAnyProgressInResponse():
                rewardBonus(severity=15.0)  // Partial credit for positioning
        
        // ===== COMMITMENT CHAIN DISRUPTION =====
        // If opponent is mid-chain (e.g., flip → land → boost back)
        // Disrupt the recovery leg = maximum value
        
        FOR EACH recoveryStageInChain:
            recoveryLock = getRecoveryLockDuration(recoveryStage)
            
            // Exception: recovery lock is so short it doesn't matter
            IF recoveryLock < 0.15:
                CONTINUE
            
            IF weCanInterruptDuring(recoveryLock):
                    // Bumping/demoing during recovery = extra value
                    rewardBonus(severity=25.0)
        
        // ===== ASYMMETRIC INFORMATION EXPLOITATION =====
        // Opponent commits without full field information (we can exploit this)
        
        opponentFieldVision = calculateVisibleFieldToOpponent(opponent)
        ourTeamPositions = getOurPlayerPositions()
        
        ourPositionsHidden = countPositionsOutsideOpponentVision(ourTeam)
        
        IF ourPositionsHidden > 0:
            // Opponent made commit without seeing us
            // High-value if we were trying to create pressure anyway
            IF weWerePositioningForCounterAttack():
                rewardBonus(severity=20.0)

// ============================================================================
// UTILITY FUNCTIONS (Pseudo-implementations)
// ============================================================================

/*
// ============================================================================
// NEW HELPER FUNCTIONS FOR PHASE 1 ADVANCED EXCEPTIONS
// ============================================================================

/*
FUNCTION isDodgingImminentDemoOrBump(player, opponents) -> bool:
    // Check if an opponent is about to demo/bump us, making flip/jump justified
    
    FOR EACH opponent IN opponents:
        // Calculate opponent's trajectory toward us
        dirToUs = (player.pos - opponent.pos).Normalized()
        opponentVelTowardUs = opponent.vel.Dot(dirToUs)
        
        // Is opponent moving toward us at high speed?
        IF opponentVelTowardUs > 1500.0:  // Significant approach velocity
            distToOpponent = distance(opponent.pos, player.pos)
            timeToContact = distToOpponent / opponentVelTowardUs
            
            // Is contact imminent? (within ~0.5 seconds)
            IF timeToContact < 0.5 AND timeToContact > 0:
                // Opponent is close and coming fast
                // Flip/jump to dodge is JUSTIFIED
                RETURN true
    
    RETURN false

FUNCTION inOpponentCorner(pos) -> bool:
    // Check if position is in one of the opponent's corners
    // Opponent corners are approximately:
    // - x range: ±4096 (side walls), y > 4000 (deep opponent half)
    // - This creates the "corner zone"
    
    CONST CORNER_X_THRESHOLD = 3500.0
    CONST CORNER_Y_THRESHOLD = 4000.0
    
    isCorner = (abs(pos.x) > CORNER_X_THRESHOLD) AND (abs(pos.y) > CORNER_Y_THRESHOLD)
    
    // Make sure it's opponent half (y > 0 if Blue, < 0 if Orange)
    // Assuming Blue attacks Orange (positive Y):
    isOpponentHalf = pos.y > FIELD_CENTER_Y
    
    RETURN isCorner AND isOpponentHalf

FUNCTION inOpponentGoalArea(pos) -> bool:
    // Check if player is in/near opponent goal
    // Goal area is roughly:
    // - x range: ±892 (goal width)
    // - y: > 5120 (goal line) to ~6000 (back net)
    // - z: 0 to 643 (goal height)
    
    CONST GOAL_WIDTH = 900.0
    CONST GOAL_LINE_Y = 5120.0
    CONST BACK_NET_Y = 6000.0
    CONST GOAL_HEIGHT = 643.0
    
    inGoalX = abs(pos.x) < GOAL_WIDTH
    inGoalY = pos.y > GOAL_LINE_Y AND pos.y < BACK_NET_Y
    inGoalZ = pos.z < GOAL_HEIGHT + 200.0  // Add some buffer for on-rim positions
    
    RETURN inGoalX AND inGoalY AND inGoalZ

FUNCTION recentlyAttemptedShot(player, recentWindowSeconds) -> bool:
    // Check if player was recently executing an aerial/flick/shot
    // Uses game event tracking (should be available via GameState/GameEventTracker)
    
    // This can be implemented via:
    // 1. Checking player's last action type
    // 2. Checking if player was airborne in past N seconds
    // 3. Checking GameEventTracker for shot/flick events
    
    // Simple heuristic: was player airborne recently and now low?
    wasRecentlyAirborne = player.lastAirborneTime < recentWindowSeconds
    isNowLow = NOT player.isInAir AND player.vel.z < 100  // Recently landed
    
    RETURN wasRecentlyAirborne AND isNowLow

FUNCTION isDodgingImminentDemoOrBump(player, opponents) -> bool:
    // (Already defined above)
    ...
*/

FUNCTION analyzeCurrentCommitment(player) -> Commitment:
    // Determine if player is currently locked into an action
    
    commitment.isFlipping = player.isFlipping
    commitment.isAirborne = player.isInAir AND player.vel.z != 0
    commitment.isAccelerating = player.throttle > 0 AND player.boost > 0
    commitment.hasDoubleJumpUsed = player.doubleJumpUsed
    
    // Classify commitment severity
    IF commitment.isFlipping:
        commitment.severity = 9  // High lock (0.4-0.6s flip duration)
    ELSE IF commitment.isAirborne AND commitment.timeInAir > 0.5:
        commitment.severity = 7  // Medium-high lock (aerial)
    ELSE IF commitment.isAccelerating:
        commitment.severity = 4  // Lower lock (can adjust course)
    ELSE:
        commitment.severity = 0  // Free
    
    RETURN commitment

FUNCTION calculateGeometricAdvantage(ourTeam, opponent, contestedPosition) -> float:
    // How much closer are we to the contested space?
    
    ourClosestDistance = minDistance(ourTeam, contestedPosition)
    opponentDistance = distance(opponent, contestedPosition)
    
    advantage = (opponentDistance - ourClosestDistance) / opponentDistance
    // Range: -1 (we're far) to +1 (we're much closer)
    
    RETURN clamp(advantage, -1, 1)

FUNCTION getCommitmentLockDuration(commitment) -> float:
    // How long is the opponent locked into this commitment?
    
    IF commitment.isFlipping:
        RETURN getFlipRemainingDuration(opponent)
    ELSE IF commitment.isAirborne:
        RETURN timeToLand(opponent)
    ELSE IF commitment.hasDoubleJumpUsed AND commitment.boost < 20:
        RETURN (20 - opponent.boost) / BOOST_DEPLETION_RATE  // Time to recover boost
    
    RETURN 0.0

FUNCTION projectCommitmentOutcome(commitment) -> Vec:
    // Where will opponent end up when commitment resolves?
    
    IF commitment.isFlipping:
        flipDuration = getFlipRemainingDuration(opponent)
        RETURN projectMotion(opponent.pos, opponent.vel, flipDuration)
    
    ELSE IF commitment.isAirborne:
        timeToLand = calculateTimeToGround(opponent)
        RETURN projectMotion(opponent.pos, opponent.vel, timeToLand)
    
    ELSE:
        RETURN opponent.pos  // Already resolved

FUNCTION findBestResponseToCommitment(commitment, ourTeam) -> Action:
    // What's our optimal move given opponent's commitment?
    
    lockedPos = projectCommitmentOutcome(commitment)
    lockDuration = getCommitmentLockDuration(commitment)
    
    // Option 1: Race to ball
    IF ball.hasLineOfSight AND lockDuration > getTimeToReachBall(ourTeam, ball):
        RETURN Action.REACH_BALL_FIRST
    
    // Option 2: Positional advantage (get goal-side of opponent)
    IF canRepositionGoalSide(ourTeam, opponent, lockDuration):
        RETURN Action.POSITION_GOAL_SIDE
    
    // Option 3: Demo/bump the locked opponent
    IF distance(ourTeam, lockedPos) < DEMO_RANGE AND lockDuration > DEMO_STARTUP:
        RETURN Action.DEMO_LOCKED_OPPONENT
    
    // Option 4: Create space for teammate
    IF hasTeammateInGoodPosition(ourTeam, lockedPos, lockDuration):
        RETURN Action.CREATE_TEAMMATE_SPACE
    
    RETURN Action.MAINTAIN_PRESSURE

FUNCTION worstCaseScenario(commitment) -> Outcome:
    // If commitment fails, what's the damage?
    
    outcome.ownGoalDanger = 0.0
    
    // Simulate: commitment fails (miss the ball/contact)
    IF commitment.isAirborne AND commitment.isAttemptingTouching:
        // You're falling without touching the ball
        landingPosition = projectCommitmentOutcome(commitment)
        outcome.ownGoalDanger = distance(landingPosition, ourGoal) / FIELD_LENGTH
        // Close to goal = high danger
    
    IF commitment.isFlipping AND commitment.boost < 30:
        // Flip with low boost = risky if it doesn't connect
        outcome.ownGoalDanger += 0.3
    
    outcome.teamCanRecover = hasTeammateToDefend(ourTeam, landingPosition)
    
    RETURN outcome
*/

// ============================================================================
// INTEGRATION EXAMPLE: PHASE 3 REWARD CONFIGURATION
// ============================================================================

/*
In ExampleMain.cpp, Phase 4+ integration:

std::vector<WeightedReward> GetPhasedRewards() {
    switch (CURRENT_PHASE) {
    case TrainingPhase::PHASE_4_PLATINUM:
        return std::vector<WeightedReward>{
            // ... existing rewards ...
            
            // === NEW: Overcommitment Penalties ===
            WeightedReward(new SimpleOvercommitmentPenalty(), 15.0f),           // Phase 4
            
            // === NEW: Opponent Overcommitment Rewards ===
            WeightedReward(new CapitalizeOnOpponentOvercommitment(), 12.0f),   // Phase 4
            
            // ... rest of rewards ...
        };
    
    case TrainingPhase::PHASE_5_DIAMOND:
        return std::vector<WeightedReward>{
            // ... existing rewards ...
            
            WeightedReward(new AdvancedOvercommitmentPenalty(), 18.0f),        // Phase 5: upgrade
            WeightedReward(new AdvancedOpponentOvercommitmentReward(), 16.0f), // Phase 5: upgrade
            
            // ... rest of rewards ...
        };
    
    case TrainingPhase::PHASE_6_CHAMPION:
    case TrainingPhase::PHASE_7_GC:
        return std::vector<WeightedReward>{
            // ... existing rewards ...
            
            WeightedReward(new ExpertOvercommitmentPenalty(), 20.0f),          // Phase 6+: full model
            WeightedReward(new ExpertOpponentOvercommitmentReward(), 20.0f),   // Phase 6+: full model
            
            // ... rest of rewards ...
        };
    }
}
*/

