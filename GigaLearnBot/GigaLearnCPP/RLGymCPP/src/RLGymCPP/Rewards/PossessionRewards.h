// PossessionRewards.h
// Advanced Possession Reward Implementations for Rocket League
// Place in: GigaLearnCPP/GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/

#pragma once
#include "Reward.h"
#include "../Math.h"
#include <map>
#include <deque>
#include <vector>

namespace RLGC {

// ============================================================================
// CRITIQUE OF ChatGPT'S APPROACH
// ============================================================================
/*
PROBLEMS WITH ChatGPT'S IMPLEMENTATION:

1. OVERLY SIMPLISTIC DISTANCE METRIC
   - Uses raw Euclidean distance without considering velocity, momentum, or car orientation
   - Doesn't account for boost availability or supersonic speed
   - Ignores whether player can actually *reach* the ball in time
   - Real possession = "time to ball" not "distance to ball"

2. DIRECTION TERM IS BACKWARDS
   - Only rewards ball going toward opponent goal
   - IGNORES the core possession concept: controlling ball AWAY from pressure
   - In real RL, possession often means moving ball to SAFE SPACE, not always forward
   - Dribbling sideways or backward can be excellent possession

3. LAST TOUCH IS TOO BINARY
   - Touch 3 seconds ago vs 0.1 seconds ago treated identically
   - Doesn't decay over time
   - Doesn't distinguish between meaningful touches (slow, controlled) vs clearances

4. NO PRESSURE CONSIDERATION
   - Possession is RELATIVE to opponent pressure
   - Having ball 100 units away means nothing if opponent is supersonic from behind
   - Doesn't track opponent momentum toward ball

5. MISSING CRITICAL MECHANICS
   - No reward for dribbling (maintaining ball on car)
   - No reward for air dribbling possession
   - No reward for wall carries
   - No reward for boost starving opponents while possessing
   - No reward for space creation (moving ball away from multiple opponents)

6. WEIGHTS ARE ARBITRARY
   - w_touch=0.5, w_dist=0.4, w_dir=0.1 have no justification
   - These will need extensive tuning and likely fail in many scenarios

7. NO TEMPORAL CONTEXT
   - Possession is a TIME-BASED concept
   - Maintaining possession for 5 seconds should be rewarded more than 0.5 seconds
   - No tracking of possession duration or streaks

8. GOAL DIRECTION IS OVERSIMPLIFIED
   - Uses single unit vector "toward opponent goal"
   - Real RL: sometimes best play is diagonal, to corner, or even backward
   - Doesn't account for defensive vs offensive contexts
*/

// ============================================================================
// IMPLEMENTATION 1: TIME-TO-BALL POSSESSION
// Most accurate for "who will touch next" prediction
// ============================================================================

class TimeToBalPossessionReward : public Reward {
private:
    // Per-player state tracking (immutable in GetReward, updated in PreStep)
    std::map<uint32_t, float> possessionScoreHistory;
    std::map<uint32_t, float> possessionDuration;

    // Calculate actual time-to-ball considering physics
    float CalculateTimeToReach(const Player& player, const Vec& ballPos, const Vec& ballVel) const {
        Vec toBall = ballPos - player.pos;
        float distToBall = toBall.Length();
        
        if (distToBall < 0.0001f) return 0.0f;
        
        // Account for current velocity toward ball
        Vec dirToBall = toBall.Normalized();
        float currentSpeedToBall = RS_MAX(0.0f, player.vel.Dot(dirToBall));
        
        // Estimate based on boost and current speed
        float maxSpeed = CommonValues::CAR_MAX_SPEED;
        bool hasBoost = player.boost > 5.0f;
        
        // If supersonic or high speed toward ball
        if (currentSpeedToBall > 2200.0f) {
            return distToBall / currentSpeedToBall;
        }
        
        // If not moving toward ball, account for turning time
        float forwardAlignment = player.rotMat.forward.Dot(dirToBall);
        float turnPenalty = 1.0f;
        if (forwardAlignment < 0.9f) {
            // Calculate actual turning angle (in radians, then scale to penalty)
            // Clamp to avoid acos domain errors
            float alignmentClamped = RS_CLAMP(forwardAlignment, -1.0f, 1.0f);
            float turningAngle = acosf(alignmentClamped); // Radians: 0 = aligned, π/2 = perpendicular
            // Convert angle to penalty: 0 rad = 1.0x, π/2 rad = 2.0x, π rad = 3.0x
            turnPenalty = 1.0f + (turningAngle / 3.14159265359f) * 2.0f;
        }
        
        // Estimate time with boost acceleration or without
        float effectiveSpeed = hasBoost ? 
            RS_MIN(currentSpeedToBall + 1000.0f, maxSpeed) : // Can boost
            RS_MIN(currentSpeedToBall + 500.0f, 1410.0f);    // No boost, cap at throttle speed
        
        float baseTime = distToBall / RS_MAX(effectiveSpeed, 500.0f);
        return baseTime * turnPenalty;
    }
    
    // Calculate "ball control quality" - is ball in a controllable state?
    float CalculateBallControllability(const Player& player, const GameState& state) const {
        Vec relBallPos = state.ball.pos - player.pos;
        float distToBall = relBallPos.Length();
        
        // Ball on car = maximum control
        float heightAboveCar = relBallPos.z;
        float horizDist = Vec(relBallPos.x, relBallPos.y, 0).Length();
        if (heightAboveCar > 20.0f && heightAboveCar < 200.0f && horizDist < 150.0f) {
            return 1.0f; // Perfect dribble
        }
        
        // Close and slow = controllable
        float ballSpeed = state.ball.vel.Length();
        if (distToBall < 300.0f && ballSpeed < 800.0f) {
            return 0.8f; // Very controllable
        }
        
        if (distToBall < 600.0f && ballSpeed < 1500.0f) {
            return 0.5f; // Moderately controllable
        }
        
        // Far or fast = low control
        return RS_MAX(0.0f, 1.0f - (distToBall / 2000.0f) - (ballSpeed / 3000.0f));
    }
    
    // Detect air dribble possession (P3-1: Air dribble bonus)
    // Ball must be 200-600 UU above car for air dribble credit
    bool IsAirDribble(const Player& player, const GameState& state) const {
        Vec relBallPos = state.ball.pos - player.pos;
        float heightAboveCar = relBallPos.z;
        float horizDist = Vec(relBallPos.x, relBallPos.y, 0).Length();

        // Ball above car (200-600 UU) and within horizontal range (300 UU)
        if (heightAboveCar > 200.0f && heightAboveCar < 600.0f && horizDist < 300.0f) {
            return true;
        }
        return false;
    }

    // Calculate opponent pressure intensity
    float CalculateOpponentPressure(const Player& player, const GameState& state,
                                    const Vec& ballPos) const {
        float maxPressure = 0.0f;
        
        for (const auto& opp : state.players) {
            if (opp.team == player.team) continue;
            if (opp.isDemoed) continue;
            
            Vec oppToBall = ballPos - opp.pos;
            float oppDist = oppToBall.Length();
            
            // Opponent very close = high pressure
            if (oppDist < 500.0f) {
                maxPressure = RS_MAX(maxPressure, 1.0f);
                continue;
            }
            
            // Check if opponent is supersonic toward ball
            Vec oppDir = oppToBall.Normalized();
            float oppSpeedToBall = RS_MAX(0.0f, opp.vel.Dot(oppDir));
            bool oppSupersonic = oppSpeedToBall > 2200.0f;
            
            if (oppSupersonic && oppDist < 1500.0f) {
                maxPressure = RS_MAX(maxPressure, 0.8f);
            } else if (oppDist < 1000.0f) {
                maxPressure = RS_MAX(maxPressure, 0.5f);
            }
        }
        
        return maxPressure;
    }

    // ------------------------------------------------------------------------
    // Ball-threat gate (borrowed from PossessionControlRewardv2).
    // Used as a SOFT multiplier here, not a hard gate: this class's own header
    // critique (see top of file) argues possession isn't always forward, so we
    // only damp — not zero — the idle-hold channels when the ball is not being
    // carried at pace toward the opponent goal.
    // ------------------------------------------------------------------------
    static constexpr float THREAT_ATTACK_ZONE_DIST = 4000.0f;  // dist from opp GOAL_BACK
    static constexpr float THREAT_MIN_BALL_SPEED   = 600.0f;   // uu/s — real pace, not drift
    static constexpr float THREAT_MIN_ALIGNMENT    = 0.30f;    // matches possession vel-alignment convention
    static constexpr float THREAT_IDLE_MULT        = 0.50f;    // hold-channel scale when no threat

    bool BallThreatActive(Team team, const GameState& state) const {
        Vec oppGoalBack = (team == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_BACK
            : CommonValues::BLUE_GOAL_BACK;
        if ((state.ball.pos - oppGoalBack).Length() > THREAT_ATTACK_ZONE_DIST) return false;
        if (state.ball.vel.Length() < THREAT_MIN_BALL_SPEED) return false;
        Vec toOppGoal = (oppGoalBack - state.ball.pos).Normalized();
        return state.ball.vel.Normalized().Dot(toOppGoal) >= THREAT_MIN_ALIGNMENT;
    }

public:
    virtual void Reset(const GameState& initialState) override {
        possessionScoreHistory.clear();
        possessionDuration.clear();
    }

    virtual void PreStep(const GameState& state) override {
        // Update possession state BEFORE GetReward is called
        // This ensures thread-safe state updates (all calls to PreStep complete before GetReward calls)
        for (const auto& player : state.players) {
            // Calculate time-to-ball for this player
            float myTimeToReach = CalculateTimeToReach(player, state.ball.pos, state.ball.vel);

            float oppMinTimeToReach = 10.0f;
            for (const auto& opp : state.players) {
                if (opp.team == player.team) continue;
                if (opp.isDemoed) continue;

                float oppTime = CalculateTimeToReach(opp, state.ball.pos, state.ball.vel);
                oppMinTimeToReach = RS_MIN(oppMinTimeToReach, oppTime);
            }

            // Possession score: positive if player will reach first
            float timeDiff = oppMinTimeToReach - myTimeToReach;
            float possessionScore = RS_CLAMP(timeDiff / 2.0f, -1.0f, 1.0f);

            // Modify by ball controllability
            float controllability = CalculateBallControllability(player, state);
            possessionScore *= (0.7f + 0.3f * controllability); // Time-based dominates, but control quality still matters

            // Reduce score if under heavy pressure
            float pressure = CalculateOpponentPressure(player, state, state.ball.pos);
            possessionScore *= (1.0f - pressure * pressure); // Quadratic penalty: small pressure = minimal impact, high pressure = severe impact

            // Update possession state for next GetReward call
            possessionScoreHistory[player.carId] = possessionScore;

            // Update duration tracking
            if (possessionScore > 0.3f) {
                possessionDuration[player.carId] += state.deltaTime;
            } else {
                possessionDuration[player.carId] = 0.0f;
            }
        }
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float reward = 0.0f;

        // Read-only access to state computed in PreStep
        float prevScore = possessionScoreHistory.count(player.carId) ? possessionScoreHistory[player.carId] : 0.0f;
        float duration = possessionDuration.count(player.carId) ? possessionDuration[player.carId] : 0.0f;

        // Recalculate current score (from PreStep, it should match)
        float myTimeToReach = CalculateTimeToReach(player, state.ball.pos, state.ball.vel);
        float oppMinTimeToReach = 10.0f;
        for (const auto& opp : state.players) {
            if (opp.team == player.team) continue;
            if (opp.isDemoed) continue;
            float oppTime = CalculateTimeToReach(opp, state.ball.pos, state.ball.vel);
            oppMinTimeToReach = RS_MIN(oppMinTimeToReach, oppTime);
        }

        float timeDiff = oppMinTimeToReach - myTimeToReach;
        float possessionScore = RS_CLAMP(timeDiff / 2.0f, -1.0f, 1.0f);
        float controllability = CalculateBallControllability(player, state);
        possessionScore *= (0.5f + 0.5f * controllability);
        float pressure = CalculateOpponentPressure(player, state, state.ball.pos);
        possessionScore *= (1.0f - pressure * pressure); // Quadratic penalty: matches PreStep calculation

        // Soft threat multiplier: damp the idle-hold channels (maintain +
        // extended-duration) when the ball is not being carried at pace toward
        // the opponent goal. Air-dribble / won / lost channels are NOT scaled.
        float threatMult = BallThreatActive(player.team, state) ? 1.0f : THREAT_IDLE_MULT;

        if (possessionScore > 0.3f) {
            // In possession - reward maintaining it
            reward += possessionScore * 0.5f * threatMult;

            // Bonus for extended possession (diminishing returns, caps at 30s)
            if (duration > 1.0f) {
                reward += RS_MIN(duration / 30.0f, 1.0f) * 0.5f * threatMult;
            }

            // P3-1: Bonus for air dribble possession (more challenging variant)
            if (IsAirDribble(player, state)) {
                reward += 0.3f; // Bonus for maintaining air dribble
            }
        }

        // Reward gaining possession (transition from not having to having)
        if (prevScore < 0.0f && possessionScore > 0.3f) {
            reward += 2.0f; // Won possession
        }

        // Penalize losing possession (transition from having to not having)
        if (prevScore > 0.3f && possessionScore < 0.0f) {
            reward -= 1.0f; // Lost possession
        }

        return reward;
    }
};

// ============================================================================
// ContinuousControlledTouchReward
// Continuous shaping for controlled possession where:
//  - Ball is in front of car, within a cone
//  - Ball is close (<= 700uu)
//  - Ball's relative speed is not exceeding car's speed by much
//  - Ball absolute speed not crazy (> ~1600uu)
// 2025-12-08 - Rev 1 (NJH)
// ============================================================================
class ContinuousControlledTouchReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        Vec rel = state.ball.pos - player.pos;
        float dist = rel.Length();
        if (dist > 700.0f)
            return 0.0f;

        // Ball should be generally in front in a cone
        Vec forward = player.rotMat.forward;
        Vec relDir = rel.Normalized();
        float frontAlign = forward.Dot(relDir); // 1.0 = dead ahead

        // ~60° cone: cos(60°) = 0.5
        if (frontAlign < 0.5f)
            return 0.0f;

        float carSpeed = player.vel.Length();
        float ballSpeed = state.ball.vel.Length();

        // If the car isn't really moving, this isn't meaningful control
        if (carSpeed < 200.0f)
            return 0.0f;

        // Relative speed between ball and car
        Vec relVel = state.ball.vel - player.vel;
        float relSpd = relVel.Length();

        // If ball is not "running away" much faster than the car, full credit.
        // If ball is faster than car, fade reward out.
        float relFactor = 1.0f;
        if (relSpd > carSpeed) {
            float diff = relSpd - carSpeed;
            relFactor = RS_CLAMP(1.0f - diff / 800.0f, 0.0f, 1.0f);
        }
        if (relFactor <= 0.0f)
            return 0.0f;

        // Penalize extremely fast balls: hard to call that control
        float speedPenalty = 1.0f;
        const float baseCap = 1600.0f;
        if (ballSpeed > baseCap) {
            speedPenalty = RS_CLAMP(1.0f - (ballSpeed - baseCap) / 1400.0f, 0.0f, 1.0f);
        }

        // Closer ball = better control
        float distFactor = RS_CLAMP(1.0f - dist / 700.0f, 0.0f, 1.0f);

        // Tighter forward alignment = better
        float alignFactor = RS_CLAMP((frontAlign - 0.5f) / 0.5f, 0.0f, 1.0f); // 0 at 0.5, 1 at 1.0

        float rewardPerStep = 0.5f * distFactor * alignFactor * relFactor * speedPenalty;
        return RS_CLAMP(rewardPerStep, 0.0f, 0.5f);
    }
};

// ============================================================================
// DelayedControlledTouchOutcomeReward
// After a touch, wait a short time window (~0.15s) and then:
//  - Check if player is still closer to ball than any opponent
//  - Ball still in front-cone, not screaming away
// This encourages touches that LEAD to control, not just contact.
// 2025-12-08 - Rev 1 (NJH)
// ============================================================================
class DelayedControlledTouchOutcomeReward : public Reward {
private:
    mutable std::map<uint32_t, float> evalTimer;  // seconds until outcome eval
    mutable std::map<uint32_t, Vec>   touchBallPos;

public:
    virtual void Reset(const GameState& initialState) override {
        evalTimer.clear();
        touchBallPos.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
        float reward = 0.0f;
        auto& timer = evalTimer[player.carId];

        // On ball touch, start an outcome window
        if (player.ballTouchedStep) {
            timer = 0.4f; // ~0.40s delay
            touchBallPos[player.carId] = state.ball.pos;
        }

        if (timer > 0.0f) {
            timer -= dt;
            if (timer <= 0.0f) {
                // Time to evaluate possession outcome
                Vec rel = state.ball.pos - player.pos;
                float distToBall = rel.Length();

                // Find closest opponent to current ball position
                float closestOppDist = 1e9f;
                for (const auto& opp : state.players) {
                    if (opp.team == player.team) continue;
                    if (opp.isDemoed) continue;
                    float d = (opp.pos - state.ball.pos).Length();
                    if (d < closestOppDist) closestOppDist = d;
                }

                // If we are closer to ball than any opponent, good
                if (distToBall < closestOppDist && distToBall < 800.0f) {
                    Vec forward = player.rotMat.forward;
                    Vec relDir = (distToBall > 1e-3f) ? (rel / distToBall) : forward;
                    float align = forward.Dot(relDir);
                    float carSpd = player.vel.Length();
                    float ballSpd = state.ball.vel.Length();

                    // Same constraints: in front cone, not insane speed
                    if (align > 0.5f && ballSpd < 1800.0f && carSpd > 200.0f) {
                        float spaceFactor = RS_CLAMP((closestOppDist - distToBall) / 2000.0f, 0.0f, 1.0f);
                        float alignFactor = RS_CLAMP((align - 0.5f) / 0.5f, 0.0f, 1.0f);

                        reward = 1.0f * spaceFactor * alignFactor;
                    }
                }
            }
        }

        return reward;
    }
};


// ============================================================================
// IMPLEMENTATION 3: MULTI-AGENT POSSESSION
// Best for team play - considers team possession holistically
// ============================================================================

class MultiAgentPossessionReward : public Reward {
private:
    struct TeamPossessionState {
        float possessionScore;
        uint32_t controllingPlayerId;
        float duration;
        Vec ballControlPoint;
    };
    
    mutable std::map<Team, TeamPossessionState> teamPossession;
    
    // Calculate which team has possession and by how much
    float CalculateTeamPossession(const GameState& state, Team team) const {
        float bestMyTime = 10.0f;
        float bestOppTime = 10.0f;
        
        for (const auto& player : state.players) {
            Vec toBall = state.ball.pos - player.pos;
            float dist = toBall.Length();
            
            // Simple time estimate
            float speed = player.vel.Length();
            float effectiveSpeed = RS_MAX(speed, 500.0f);
            if (player.boost > 10.0f) {
                effectiveSpeed += 800.0f;
            }
            
            float time = dist / effectiveSpeed;
            
            if (player.team == team) {
                bestMyTime = RS_MIN(bestMyTime, time);
            } else {
                bestOppTime = RS_MIN(bestOppTime, time);
            }
        }
        
        // Positive if my team closer, negative if opponent closer
        float diff = bestOppTime - bestMyTime;
        return RS_CLAMP(diff / 1.5f, -1.0f, 1.0f);
    }
    
    // Find who on team is "controlling" the ball
    uint32_t FindControllingPlayer(const GameState& state, Team team) const {
        float bestScore = -1.0f;
        uint32_t bestId = 0;
        
        for (const auto& player : state.players) {
            if (player.team != team) continue;
            
            float dist = (player.pos - state.ball.pos).Length();
            float score = 1.0f / (dist + 100.0f);
            
            if (player.ballTouchedStep) {
                score *= 3.0f; // Just touched = likely controller
            }
            
            if (score > bestScore) {
                bestScore = score;
                bestId = player.carId;
            }
        }
        
        return bestId;
    }
    
    // Calculate quality of support positioning for teammates
    float CalculateSupportQuality(const Player& player, const GameState& state,
                                   uint32_t controllingPlayerId) const {
        if (player.carId == controllingPlayerId) {
            return 0.0f; // Player is controller, not support
        }
        
        // Find the controlling player
        const Player* controller = nullptr;
        for (const auto& p : state.players) {
            if (p.carId == controllingPlayerId) {
                controller = &p;
                break;
            }
        }
        
        if (!controller) return 0.0f;
        
        // Good support = not overlapping controller, but ready for pass
        float distToController = (player.pos - controller->pos).Length();
        float distToBall = (player.pos - state.ball.pos).Length();
        
        // Ideal support distance: 1000-2500 units from ball
        float supportQuality = 0.0f;
        if (distToBall > 1000.0f && distToBall < 2500.0f) {
            supportQuality = 1.0f - abs(distToBall - 1750.0f) / 750.0f;
        }
        
        // Penalize being too close to controller (overlap)
        if (distToController < 800.0f) {
            supportQuality *= 0.3f;
        }
        
        // Bonus for being in passing lane
        Vec controllerToBall = (state.ball.pos - controller->pos).Normalized();
        Vec controllerToMe = (player.pos - controller->pos).Normalized();
        float passLaneAlignment = controllerToBall.Dot(controllerToMe);
        
        if (passLaneAlignment > 0.7f) {
            supportQuality *= 1.5f; // In good passing position
        }
        
        return supportQuality;
    }

public:
    virtual void Reset(const GameState& initialState) override {
        teamPossession.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float reward = 0.0f;
        
        // Calculate team possession
        float teamPossScore = CalculateTeamPossession(state, player.team);
        uint32_t controllerId = FindControllingPlayer(state, player.team);
        
        // Update team possession state
        auto& teamState = teamPossession[player.team];
        bool hadPossession = teamState.possessionScore > 0.3f;
        bool hasPossession = teamPossScore > 0.3f;
        
        if (hasPossession) {
            teamState.duration += state.deltaTime;
        } else {
            teamState.duration = 0.0f;
        }
        
        teamState.possessionScore = teamPossScore;
        teamState.controllingPlayerId = controllerId;
        
        // Reward based on role
        if (player.carId == controllerId) {
            // I'm the controlling player
            if (hasPossession) {
                reward += 0.8f; // Base possession reward
                
                // Bonus for maintaining possession
                float duration = teamState.duration;
                if (duration > 1.0f) {
                    reward += RS_MIN(duration / 8.0f, 1.0f) * 0.5f;
                }
                
                // Bonus for ball control quality
                float distToBall = (player.pos - state.ball.pos).Length();
                if (distToBall < 200.0f) {
                    reward += 0.4f; // Close control
                }
            }
            
            // Reward gaining possession for team
            if (!hadPossession && hasPossession) {
                reward += 3.0f; // Won possession!
            }
        } else {
            // I'm a supporting player
            float supportQuality = CalculateSupportQuality(player, state, controllerId);
            
            if (hasPossession && supportQuality > 0.3f) {
                reward += supportQuality * 0.6f; // Good support positioning
            }
            
            // Small penalty for poor support when team has possession
            if (hasPossession && supportQuality < 0.1f) {
                float distToBall = (player.pos - state.ball.pos).Length();
                if (distToBall < 600.0f) {
                    reward -= 0.3f; // Too close, overlapping
                }
            }
        }
        
        // Penalize team losing possession
        if (hadPossession && !hasPossession) {
            // Blame the controller more than support
            if (player.carId == teamState.controllingPlayerId) {
                reward -= 1.5f; // Lost possession as controller
            } else {
                reward -= 0.3f; // Team lost possession
            }
        }
        
        return reward;
    }
};

// ============================================================================
// OffensivePossessionPositionReward
// Rewards: having possession, being goal-side of the ball, and being
//          on the opponent's half of the field.
// 2025-12-07 - Rev 1 - Added offensive possession/position reward. (NJH)
// ============================================================================
class OffensivePossessionPositionReward : public Reward {
private:
    TimeToBalPossessionReward _timeToBallPossession; // reuse possession logic

public:
    virtual void Reset(const GameState& initialState) override {
        _timeToBallPossession.Reset(initialState);
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Base possession score (positive when "we get to ball first")
        float possessionScore = _timeToBallPossession.GetReward(player, state, isFinal);

        // We only care about *positive* possession (actually winning the race)
        if (possessionScore <= 0.0f)
            return 0.0f;

        // Own goal (same as DefensivePositionReward)
        Vec myGoal = (player.team == Team::BLUE)
            ? CommonValues::BLUE_GOAL_BACK
            : CommonValues::ORANGE_GOAL_BACK;

        Vec ballToGoal = myGoal - state.ball.pos;
        float distBG = ballToGoal.Length();
        if (distBG < 1e-5f)
            return 0.0f;

        Vec goalDir = ballToGoal.Normalized();          // ball → own goal
        Vec ballToPlayer = player.pos - state.ball.pos;      // ball → player

        // Player needs to be between ball and own goal (goal-side of the ball)
        float alignment = ballToPlayer.Normalized().Dot(goalDir);
        if (alignment <= 0.0f)
            return 0.0f; // wrong side of the ball

        // Distance along goal direction: reward being near a "good" buffer
        float distAlong = fabsf(ballToPlayer.Dot(goalDir));
        const float optimalDistance = 2000.0f;
        float distanceReward = 1.0f / (1.0f + fabsf(distAlong - optimalDistance) / 1000.0f);

        // Check offensive half: sign of Y depends on team
        float sideSign = (player.team == Team::BLUE) ? 1.0f : -1.0f;
        bool playerInOppHalf = (sideSign * player.pos.y) > 0.0f;
        bool ballInOppHalf = (sideSign * state.ball.pos.y) > 0.0f;

        if (!playerInOppHalf || !ballInOppHalf)
            return 0.0f;

        // Combine: positive possession, good alignment, good distance, in offensive half
        float positionalFactor = alignment * distanceReward;
        if (positionalFactor <= 0.0f)
            return 0.0f;

        // Clamp to keep things sane
        float reward = possessionScore * positionalFactor;
        return RS_MAX(0.0f, reward);
    }
};

} // namespace RLGC
