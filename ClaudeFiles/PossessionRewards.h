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
    mutable std::map<uint32_t, float> possessionScoreHistory;
    mutable std::map<uint32_t, float> possessionDuration;
    
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
        if (forwardAlignment < 0.7f) {
            // Need to turn - add penalty
            turnPenalty = 1.5f + (1.0f - forwardAlignment) * 0.5f;
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

public:
    virtual void Reset(const GameState& initialState) override {
        possessionScoreHistory.clear();
        possessionDuration.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float reward = 0.0f;
        
        // Calculate time-to-ball for me and closest opponent
        float myTimeToReach = CalculateTimeToReach(player, state.ball.pos, state.ball.vel);
        
        float oppMinTimeToReach = 10.0f;
        for (const auto& opp : state.players) {
            if (opp.team == player.team) continue;
            if (opp.isDemoed) continue;
            
            float oppTime = CalculateTimeToReach(opp, state.ball.pos, state.ball.vel);
            oppMinTimeToReach = RS_MIN(oppMinTimeToReach, oppTime);
        }
        
        // Possession score: positive if I'll reach first
        float timeDiff = oppMinTimeToReach - myTimeToReach;
        float possessionScore = RS_CLAMP(timeDiff / 2.0f, -1.0f, 1.0f);
        
        // Modify by ball controllability
        float controllability = CalculateBallControllability(player, state);
        possessionScore *= (0.5f + 0.5f * controllability); // Scale by control quality
        
        // Reduce score if under heavy pressure
        float pressure = CalculateOpponentPressure(player, state, state.ball.pos);
        possessionScore *= (1.0f - 0.3f * pressure);
        
        // Track possession duration
        float prevScore = possessionScoreHistory[player.carId];
        possessionScoreHistory[player.carId] = possessionScore;
        
        if (possessionScore > 0.3f) {
            // In possession
            possessionDuration[player.carId] += state.deltaTime;
            float duration = possessionDuration[player.carId];
            
            // Reward maintaining possession
            reward += possessionScore * 0.5f;
            
            // Bonus for extended possession (diminishing returns)
            if (duration > 1.0f) {
                reward += RS_MIN(duration / 10.0f, 1.0f) * 0.3f;
            }
        } else {
            possessionDuration[player.carId] = 0.0f;
        }
        
        // Reward gaining possession
        if (prevScore < 0.0f && possessionScore > 0.3f) {
            reward += 2.0f; // Won possession
        }
        
        // Penalize losing possession
        if (prevScore > 0.3f && possessionScore < 0.0f) {
            reward -= 1.0f; // Lost possession
        }
        
        return reward;
    }
};

// ============================================================================
// IMPLEMENTATION 2: CONTEXTUAL POSSESSION
// Rewards different possession types based on game context
// ============================================================================

class ContextualPossessionReward : public Reward {
private:
    mutable std::map<uint32_t, Vec> lastBallPos;
    mutable std::map<uint32_t, float> lastPossessionScore;
    mutable std::map<uint32_t, float> possessionTimer;
    
    enum class PossessionType {
        NONE,
        GROUND_DRIBBLE,
        AIR_DRIBBLE,
        WALL_CARRY,
        CONTROLLED_TOUCH,
        SPACE_CREATION,
        BOOST_STARVE
    };
    
    PossessionType DetectPossessionType(const Player& player, const GameState& state) const {
        Vec relBallPos = state.ball.pos - player.pos;
        float heightAboveCar = relBallPos.z;
        float horizDist = Vec(relBallPos.x, relBallPos.y, 0).Length();
        float distToBall = relBallPos.Length();
        
        // Ground dribble: ball on car, on ground
        if (player.isOnGround && heightAboveCar > 20.0f && heightAboveCar < 200.0f && 
            horizDist < 150.0f) {
            return PossessionType::GROUND_DRIBBLE;
        }
        
        // Air dribble: in air, ball very close
        if (!player.isOnGround && distToBall < 250.0f && player.pos.z > 200.0f) {
            return PossessionType::AIR_DRIBBLE;
        }
        
        // Wall carry: on wall with ball
        float distToWallX = RS_MIN(
            abs(player.pos.x - CommonValues::SIDE_WALL_X),
            abs(player.pos.x + CommonValues::SIDE_WALL_X)
        );
        if (distToWallX < 200.0f && distToBall < 400.0f) {
            return PossessionType::WALL_CARRY;
        }
        
        // Controlled touch: just touched, ball slow
        if (player.ballTouchedStep && state.ball.vel.Length() < 1200.0f) {
            return PossessionType::CONTROLLED_TOUCH;
        }
        
        // Space creation: ball moving away from opponents
        bool movingAwayFromOpponents = true;
        for (const auto& opp : state.players) {
            if (opp.team == player.team) continue;
            Vec oppToBall = (state.ball.pos - opp.pos).Normalized();
            float alignment = state.ball.vel.Normalized().Dot(oppToBall);
            if (alignment > 0.0f) {
                movingAwayFromOpponents = false;
                break;
            }
        }
        
        if (movingAwayFromOpponents && distToBall < 800.0f) {
            return PossessionType::SPACE_CREATION;
        }
        
        return PossessionType::NONE;
    }
    
    // Is player denying boost to opponents while maintaining possession?
    bool IsBoostStarving(const Player& player, const GameState& state) const {
        // Check if we're in opponent half with possession
        Vec myGoal = (player.team == Team::BLUE) ?
            CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;
        
        float distToBall = (player.pos - state.ball.pos).Length();
        bool inOpponentHalf = (player.team == Team::BLUE && player.pos.y > 0.0f) ||
                             (player.team == Team::ORANGE && player.pos.y < 0.0f);
        
        if (!inOpponentHalf || distToBall > 800.0f) {
            return false;
        }
        
        // Check if opponents are low on boost
        int lowBoostOpponents = 0;
        for (const auto& opp : state.players) {
            if (opp.team == player.team) continue;
            if (opp.boost < 20.0f) {
                lowBoostOpponents++;
            }
        }
        
        return lowBoostOpponents >= 1;
    }
    
    float CalculateSpaceQuality(const Player& player, const GameState& state) const {
        // How much space does player have around ball?
        float closestOppDist = 10000.0f;
        
        for (const auto& opp : state.players) {
            if (opp.team == player.team) continue;
            if (opp.isDemoed) continue;
            
            float dist = (opp.pos - state.ball.pos).Length();
            closestOppDist = RS_MIN(closestOppDist, dist);
        }
        
        // More space = better
        return RS_MIN(closestOppDist / 2000.0f, 1.0f);
    }

public:
    virtual void Reset(const GameState& initialState) override {
        lastBallPos.clear();
        lastPossessionScore.clear();
        possessionTimer.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float reward = 0.0f;
        
        PossessionType posType = DetectPossessionType(player, state);
        float spaceQuality = CalculateSpaceQuality(player, state);
        bool boostStarving = IsBoostStarving(player, state);
        
        // Reward based on possession type
        switch (posType) {
            case PossessionType::GROUND_DRIBBLE:
                reward += 1.5f * spaceQuality; // Scale by available space
                possessionTimer[player.carId] += state.deltaTime;
                
                // Bonus for extended dribble
                if (possessionTimer[player.carId] > 2.0f) {
                    reward += 1.0f;
                }
                break;
                
            case PossessionType::AIR_DRIBBLE:
                reward += 2.0f * spaceQuality; // Air dribbles harder
                possessionTimer[player.carId] += state.deltaTime;
                
                // Big bonus for sustained air dribble
                if (possessionTimer[player.carId] > 1.0f) {
                    reward += 2.0f;
                }
                break;
                
            case PossessionType::WALL_CARRY:
                reward += 1.2f * spaceQuality;
                possessionTimer[player.carId] += state.deltaTime;
                break;
                
            case PossessionType::CONTROLLED_TOUCH:
                reward += 0.8f * spaceQuality;
                break;
                
            case PossessionType::SPACE_CREATION:
                reward += 1.0f * spaceQuality;
                
                // Big bonus if created space from pressure
                if (spaceQuality > 0.7f) {
                    reward += 1.5f; // Escaped pressure well
                }
                break;
                
            case PossessionType::NONE:
                possessionTimer[player.carId] = 0.0f;
                break;
        }
        
        // Boost starvation bonus
        if (boostStarving && posType != PossessionType::NONE) {
            reward += 0.8f; // Denying opponent boost while possessing
        }
        
        // Reward transitioning ball toward opportunity
        Vec targetGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        
        Vec toGoal = (targetGoal - state.ball.pos).Normalized();
        float towardGoal = state.ball.vel.Normalized().Dot(toGoal);
        
        // Only reward if we have possession AND moving forward
        if (posType != PossessionType::NONE && towardGoal > 0.3f) {
            reward += towardGoal * 0.5f;
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

} // namespace RLGC
