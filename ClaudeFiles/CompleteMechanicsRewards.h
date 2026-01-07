// CompleteMechanicsRewards.h
// Complete reward set for all mechanics from Bronze to Grand Champion + Advanced Mechanics
// Place in: GigaLearnCPP/GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/

#pragma once
#include "Reward.h"
#include "../Math.h"
#include <map>
#include <deque>
#include <vector>

namespace RLGC {

// ============================================================================
// BRONZE TIER REWARDS
// ============================================================================

// Basic turning radius - smoother turns
class TurningRadiusReward : public Reward {
private:
    mutable std::map<uint32_t, Vec> lastForward;

public:
    virtual void Reset(const GameState& initialState) override {
        lastForward.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.isOnGround) return 0.0f;
        
        Vec curForward = player.rotMat.forward;
        auto it = lastForward.find(player.carId);
        
        if (it == lastForward.end()) {
            lastForward[player.carId] = curForward;
            return 0.0f;
        }
        
        // Reward smooth turning (not jerky movements)
        float turnAngle = acos(RS_CLAMP(curForward.Dot(it->second), -1.0f, 1.0f));
        lastForward[player.carId] = curForward;
        
        // Small reward for controlled turning
        if (turnAngle > 0.01f && turnAngle < 0.3f) {
            return 0.5f;
        }
        return 0.0f;
    }
};

// Landing on 4 wheels after aerial
class ProperLandingReward : public Reward {
private:
    mutable std::map<uint32_t, bool> wasAirborne;

public:
    virtual void Reset(const GameState& initialState) override {
        wasAirborne.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        bool wasInAir = wasAirborne[player.carId];
        wasAirborne[player.carId] = !player.isOnGround;
        
        // Just landed from being airborne
        if (wasInAir && player.isOnGround) {
            // Check if landed upright
            float upZ = player.rotMat.up.z;
            if (upZ > 0.7f) {
                return 1.0f; // Good landing
            }
        }
        return 0.0f;
    }
};

// Boost management - using boost efficiently
class BoostEfficiencyReward : public Reward {
private:
    mutable std::map<uint32_t, float> lastBoost;

public:
    virtual void Reset(const GameState& initialState) override {
        lastBoost.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float prevBoost = lastBoost[player.carId];
        lastBoost[player.carId] = player.boost;
        
        if (prevBoost == 0.0f) return 0.0f;
        
        float boostUsed = prevBoost - player.boost;
        if (boostUsed <= 0.0f) return 0.0f;
        
        // Reward boost usage that results in productive actions
        float speed = player.vel.Length();
        if (speed > 1000.0f && boostUsed > 0.0f) {
            // Using boost while moving fast = good
            return 0.5f;
        }
        
        // Penalize boost wastage (using boost while slow)
        if (speed < 500.0f && boostUsed > 1.0f) {
            return -0.2f;
        }
        
        return 0.0f;
    }
};

// ============================================================================
// SILVER TIER REWARDS
// ============================================================================

// Half-flip execution
class HalfFlipReward : public Reward {
private:
    mutable std::map<uint32_t, Vec> flipStartForward;
    mutable std::map<uint32_t, bool> isFlipping;

public:
    virtual void Reset(const GameState& initialState) override {
        flipStartForward.clear();
        isFlipping.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        bool wasFlipping = isFlipping[player.carId];
        isFlipping[player.carId] = player.isFlipping;
        
        // Starting flip
        if (!wasFlipping && player.isFlipping) {
            flipStartForward[player.carId] = player.rotMat.forward;
            return 0.0f;
        }
        
        // Ending flip
        if (wasFlipping && !player.isFlipping) {
            auto it = flipStartForward.find(player.carId);
            if (it != flipStartForward.end()) {
                Vec startForward = it->second;
                Vec endForward = player.rotMat.forward;
                
                // Check if forward vector reversed (~180 degrees)
                float alignment = startForward.Dot(endForward);
                if (alignment < -0.7f) {
                    // Successful half-flip!
                    return 10.0f;
                }
            }
        }
        
        return 0.0f;
    }
};

// Power slide usage for better control
class PowerSlideControlReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.isOnGround) return 0.0f;

        // Check if using handbrake (power slide)
        if (player.prevAction.handbrake > 0.5f) {
            float speed = player.vel.Length();
            
            // Reward power sliding at high speed
            if (speed > 1000.0f) {
                return 1.0f;
            }
        }
        
        return 0.0f;
    }
};

// Kickoff improvements - speed toward ball at kickoff
class KickoffSpeedReward : public Reward {
private:
    mutable std::map<uint32_t, bool> wasKickoff;

public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Detect kickoff (ball near center, low velocity)
        bool isKickoff = (state.ball.pos.Length() < 200.0f &&
                         state.ball.vel.Length() < 100.0f);

        bool prevKickoff = wasKickoff[player.carId];
        wasKickoff[player.carId] = isKickoff;
        
        if (isKickoff && prevKickoff) {
            // During kickoff, reward speed toward ball
            Vec dirToBall = (state.ball.pos - player.pos).Normalized();
            float speedToBall = player.vel.Dot(dirToBall);
            
            if (speedToBall > 1000.0f) {
                return speedToBall / CommonValues::CAR_MAX_SPEED;
            }
        }
        
        return 0.0f;
    }
};

// ============================================================================
// GOLD TIER REWARDS
// ============================================================================

// Wall hits - touching ball near walls
class WallHitReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;

        // Check if ball is near wall
        float distToWallX = RS_MIN(
            abs(state.ball.pos.x - CommonValues::SIDE_WALL_X),
            abs(state.ball.pos.x + CommonValues::SIDE_WALL_X)
        );
        float distToWallY = RS_MIN(
            abs(state.ball.pos.y - CommonValues::BACK_WALL_Y),
            abs(state.ball.pos.y + CommonValues::BACK_WALL_Y)
        );
        
        float minWallDist = RS_MIN(distToWallX, distToWallY);
        
        if (minWallDist < 500.0f) {
            // Ball is near wall, reward touch
            float proximity = 1.0f - (minWallDist / 500.0f);
            return proximity * 2.0f;
        }
        
        return 0.0f;
    }
};

// Corner clears - hitting ball to corner from defensive position
class CornerClearReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;

        Vec myGoal = (player.team == Team::BLUE) ?
            CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;
        
        // Check if ball was in defensive zone
        float ballDistToGoal = (state.ball.pos - myGoal).Length();
        if (ballDistToGoal > 3000.0f) return 0.0f; // Not defensive
        
        // Check if ball is now moving toward corners
        Vec ballVelNorm = state.ball.vel.Normalized();
        
        // Target corner positions
        Vec corners[] = {
            Vec(CommonValues::SIDE_WALL_X, myGoal.y + 2000.0f, 0),
            Vec(-CommonValues::SIDE_WALL_X, myGoal.y + 2000.0f, 0)
        };
        
        for (auto& corner : corners) {
            Vec toCorner = (corner - state.ball.pos).Normalized();
            float alignment = ballVelNorm.Dot(toCorner);
            
            if (alignment > 0.7f) {
                return 5.0f; // Good defensive clear to corner
            }
        }
        
        return 0.0f;
    }
};

// Air roll shots - using air roll during aerial touches
class AirRollShotReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;
        if (player.isOnGround) return 0.0f;

        // Check if using air roll (angular velocity present)
        float angVelMag = player.angVel.Length();
        
        if (angVelMag > 2.0f) {
            // Using air roll during aerial touch
            return 3.0f;
        }
        
        return 0.0f;
    }
};

// Demo awareness - avoiding demos or demoing opponents
class DemoAwarenessReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Reward for demo
        if (player.eventState.demo) {
            return 10.0f;
        }

        // Penalize being demo'd
        if (player.eventState.demoed) {
            return -5.0f;
        }
        
        return 0.0f;
    }
};

// ============================================================================
// PLATINUM TIER REWARDS
// ============================================================================

// 50/50 awareness - challenging balls at same time as opponent
class FiftyFiftyReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;

        // Check if opponent also touched ball recently
        for (auto& opponent : state.players) {
            if (opponent.team == player.team) continue;
            
            if (opponent.ballTouchedStep) {
                // Both touched ball in same step - this is a 50/50
                return 5.0f;
            }
        }
        
        return 0.0f;
    }
};

// Catch ball + control - soft touches to gain control
class CatchBallReward : public Reward {
private:
    mutable std::map<uint32_t, float> ballSpeedAfterTouch;

public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (player.ballTouchedStep) {
            float ballSpeed = state.ball.vel.Length();
            ballSpeedAfterTouch[player.carId] = ballSpeed;

            // Reward if ball is slow after touch (catch)
            if (!state.prev) return 0.0f;
            
            float prevBallSpeed = state.prev->ball.vel.Length();
            if (prevBallSpeed > 1000.0f && ballSpeed < 500.0f) {
                // Slowed ball down significantly = catch
                return 8.0f;
            }
        }
        
        return 0.0f;
    }
};

// Small pad collection - picking up small boost pads
class SmallPadCollectionReward : public Reward {
private:
    mutable std::map<uint32_t, std::deque<float>> recentBoostPickups;

public:
    virtual void Reset(const GameState& initialState) override {
        recentBoostPickups.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!state.prev) return 0.0f;
        
        float prevBoost = state.prev->players[player.index].boost;
        float curBoost = player.boost;
        float boostGain = curBoost - prevBoost;
        
        if (boostGain > 0.0f && boostGain < 15.0f) {
            // Picked up small pad (12 boost)
            auto& pickups = recentBoostPickups[player.carId];
            pickups.push_back(state.deltaTime);
            
            // Keep only recent pickups (last 3 seconds)
            while (!pickups.empty() && (state.deltaTime - pickups.front()) > 3.0f) {
                pickups.pop_front();
            }
            
            // Reward for collecting multiple small pads in sequence
            if (pickups.size() >= 3) {
                return 5.0f * pickups.size(); // Escalating reward
            }
            
            return 2.0f; // Base reward for small pad
        }
        
        return 0.0f;
    }
};

// Directional air roll - using air roll for car control
class DirectionalAirRollReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (player.isOnGround) return 0.0f;

        // Reward for using roll during aerials
        if (abs(player.prevAction.roll) > 0.5f) {
            // Check if moving meaningfully in air
            if (player.vel.z > 300.0f || abs(player.angVel.Length()) > 2.0f) {
                return 0.5f;
            }
        }
        
        return 0.0f;
    }
};

// ============================================================================
// DIAMOND TIER REWARDS
// ============================================================================

// Shot accuracy - rewards corner goals
class ShotAccuracyReward : public Reward {
private:
    mutable std::map<uint32_t, int> lastGoalCount;
    mutable std::map<uint32_t, Vec> lastBallPos;

public:
    virtual void Reset(const GameState& initialState) override {
        lastGoalCount.clear();
        lastBallPos.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Determine team goal counts
        int currentGoals = (player.team == Team::BLUE) ?
            state.scoreInfo.teamScores[Team::ORANGE] :
            state.scoreInfo.teamScores[Team::BLUE];

        int prevGoals = lastGoalCount[player.carId];

        // Goal scored this step
        if (currentGoals > prevGoals) {
            lastGoalCount[player.carId] = currentGoals;

            // Get the goal being scored on
            Vec goalPos = (player.team == Team::BLUE) ?
                CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

            // Use last ball position (before it went into goal and reset)
            Vec ballPos = lastBallPos[player.carId];

            // Calculate ball position relative to goal center
            float ballY = ballPos.y; // Left-right position
            float ballZ = ballPos.z; // Height

            // Goal dimensions
            float goalWidth = CommonValues::GOAL_WIDTH_FROM_CENTER * 2.0f; // Total width
            float goalHeight = CommonValues::GOAL_HEIGHT;

            // Normalize to -1 to 1 range
            float normalizedY = (ballPos.x) / CommonValues::GOAL_WIDTH_FROM_CENTER; // -1 (left) to 1 (right)
            float normalizedZ = ballZ / goalHeight; // 0 (bottom) to 1 (top)

            // Define corner regions (outer 30% of width, top/bottom 30% of height)
            bool isLeftSide = (normalizedY < -0.4f);
            bool isRightSide = (normalizedY > 0.4f);
            bool isTopCorner = (normalizedZ > 0.7f);
            bool isBottomCorner = (normalizedZ < 0.3f);

            // Check if goal was in any corner
            bool isCornerGoal = (isLeftSide || isRightSide) && (isTopCorner || isBottomCorner);

            if (isCornerGoal) {
                return 1.0f; // Corner goal bonus!
            }
        }

        // Track state
        lastGoalCount[player.carId] = currentGoals;
        lastBallPos[player.carId] = state.ball.pos;

        return 0.0f;
    }
};
// Backboard defense - clearing from backboard
class BackboardDefenseReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;

        Vec myGoal = (player.team == Team::BLUE) ?
            CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;
        
        // Check if ball is on defensive backboard
        float distToBackWall = abs(abs(state.ball.pos.y) - CommonValues::BACK_WALL_Y);
        
        if (distToBackWall < 500.0f && state.ball.pos.z > 500.0f) {
            // Ball is high on backboard
            float ballDistToGoal = (state.ball.pos - myGoal).Length();
            
            if (ballDistToGoal < 3000.0f) {
                // Defensive backboard touch
                
                // Check if cleared toward corner
                Vec ballVelNorm = state.ball.vel.Normalized();
                float sidewaysVel = abs(ballVelNorm.x);
                
                if (sidewaysVel > 0.5f) {
                    return 15.0f; // Excellent backboard clear to side
                }
                
                return 8.0f; // Any defensive backboard touch
            }
        }
        
        return 0.0f;
    }
};

// Fake challenge - not committing to challenge
class FakeChallengeReward : public Reward {
private:
    mutable std::map<uint32_t, float> lastDistToBall;
    mutable std::map<uint32_t, bool> wasApproaching;

public:
    virtual void Reset(const GameState& initialState) override {
        lastDistToBall.clear();
        wasApproaching.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float curDist = (player.pos - state.ball.pos).Length();
        float prevDist = lastDistToBall[player.carId];
        lastDistToBall[player.carId] = curDist;
        
        if (prevDist == 0.0f) return 0.0f;
        
        bool isApproaching = (curDist < prevDist);
        bool prevApproaching = wasApproaching[player.carId];
        wasApproaching[player.carId] = isApproaching;
        
        // Fake = was approaching, then retreated without touching
        if (prevApproaching && !isApproaching && !player.ballTouchedStep && curDist < 1000.0f) {
            // Check if opponent is nearby (fake worked)
            for (auto& opp : state.players) {
                if (opp.team == player.team) continue;
                
                float oppDist = (opp.pos - state.ball.pos).Length();
                if (oppDist < 800.0f) {
                    // Opponent was close, we faked them out
                    return 8.0f;
                }
            }
        }
        
        return 0.0f;
    }
};

// Progressive ceiling shot training with staged rewards
class CeilingShotTrainingReward : public Reward {
private:
    mutable std::map<uint32_t, bool> wasOnCeiling;
    mutable std::map<uint32_t, bool> touchedCeilingWithBallNearby;
    mutable std::map<uint32_t, bool> hasHitBallFromCeiling;
    mutable std::map<uint32_t, bool> hasUsedFlipFromCeiling;

public:
    virtual void Reset(const GameState& initialState) override {
        wasOnCeiling.clear();
        touchedCeilingWithBallNearby.clear();
        hasHitBallFromCeiling.clear();
        hasUsedFlipFromCeiling.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float reward = 0.0f;

        // Check if on ceiling (wheels touching ceiling)
        bool onCeiling = (player.pos.z > (CommonValues::CEILING_Z - 200.0f) &&
            !player.isOnGround); // In air near ceiling

        // Check if ball is nearby (1000 uu sphere)
        float distToBall = (state.ball.pos - player.pos).Length();
        bool ballNearby = (distToBall < 1000.0f);

        // Check if player has flip available
        bool hasFlip = player.hasFlip;

        bool prevOnCeiling = wasOnCeiling[player.carId];
        bool prevTouchedCeiling = touchedCeilingWithBallNearby[player.carId];
        bool prevHitBall = hasHitBallFromCeiling[player.carId];
        bool prevUsedFlip = hasUsedFlipFromCeiling[player.carId];

        // STAGE 1: Touch ceiling while ball is nearby AND keep flip
        if (onCeiling && ballNearby && hasFlip && !prevTouchedCeiling) {
            reward += 5.0f;
            touchedCeilingWithBallNearby[player.carId] = true;
        }

        // STAGE 2: Hit the ball after being on ceiling (much larger reward)
        if (prevTouchedCeiling && player.ballTouchedStep && !player.isOnGround && !prevHitBall) {
            reward += 20.0f;
            hasHitBallFromCeiling[player.carId] = true;
        }

        // STAGE 3: Use flip after being on ceiling (moderate reward)
        if (prevTouchedCeiling && !hasFlip && prevOnCeiling && !prevUsedFlip) {
            // Player used their flip
            reward += 8.0f;
            hasUsedFlipFromCeiling[player.carId] = true;
        }

        // BONUS: All conditions met (ceiling touch + ball hit + flip used)
        if (prevHitBall && prevUsedFlip && player.ballTouchedStep) {
            reward += 15.0f; // Full execution bonus
        }

        // Reset tracking if back on ground
        if (player.isOnGround) {
            touchedCeilingWithBallNearby[player.carId] = false;
            hasHitBallFromCeiling[player.carId] = false;
            hasUsedFlipFromCeiling[player.carId] = false;
        }

        wasOnCeiling[player.carId] = onCeiling;

        return reward;
    }
};

// Flick detection - hitting ball from top of car
class FlickReward : public Reward {
private:
    mutable std::map<uint32_t, bool> ballWasOnCar;

public:
    virtual void Reset(const GameState& initialState) override {
        ballWasOnCar.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Check if ball is on top of car
        Vec relBallPos = state.ball.pos - player.pos;
        float heightAboveCar = relBallPos.z;
        float horizDist = Vec(relBallPos.x, relBallPos.y, 0).Length();
        
        bool ballOnCar = (heightAboveCar > 30.0f && heightAboveCar < 150.0f && horizDist < 120.0f);
        bool wasPrev = ballWasOnCar[player.carId];
        ballWasOnCar[player.carId] = ballOnCar;
        
        // Flick = ball was on car, now we touched it and it's fast
        if (wasPrev && player.ballTouchedStep && !ballOnCar) {
            float ballSpeed = state.ball.vel.Length();
            if (ballSpeed > 1500.0f) {
                return 15.0f; // Successful flick!
            }
        }
        
        // Small reward for maintaining dribble
        if (ballOnCar && player.isOnGround) {
            return 0.5f;
        }
        
        return 0.0f;
    }
};

// Low boost defense - defending with low boost
class LowBoostDefenseReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (player.boost > 30.0f) return 0.0f; // Not low boost

        Vec myGoal = (player.team == Team::BLUE) ?
            CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;
        
        // Check if in defensive position
        float distToGoal = (player.pos - myGoal).Length();
        float ballDistToGoal = (state.ball.pos - myGoal).Length();
        
        if (distToGoal < ballDistToGoal && ballDistToGoal < 3000.0f) {
            // In good defensive position with low boost
            return 2.0f;
        }
        
        return 0.0f;
    }
};

// Air dribble - maintaining ball control in air
class AirDribbleReward : public Reward {
private:
    mutable std::map<uint32_t, int> consecutiveAirTouches;

public:
    virtual void Reset(const GameState& initialState) override {
        consecutiveAirTouches.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (player.isOnGround) {
            consecutiveAirTouches[player.carId] = 0;
            return 0.0f;
        }
        
        if (player.ballTouchedStep) {
            consecutiveAirTouches[player.carId]++;
            
            int touches = consecutiveAirTouches[player.carId];
            if (touches >= 2) {
                // Multiple consecutive air touches = air dribble
                return 10.0f * touches;
            }
        }
        
        return 0.0f;
    }
};

// Speed flip - diagonal flip + cancel for speed
class SpeedFlipReward : public Reward {
private:
    mutable std::map<uint32_t, float> flipStartSpeed;
    mutable std::map<uint32_t, bool> isFlipping;

public:
    virtual void Reset(const GameState& initialState) override {
        flipStartSpeed.clear();
        isFlipping.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        bool wasFlipping = isFlipping[player.carId];
        isFlipping[player.carId] = player.isFlipping;
        
        if (!wasFlipping && player.isFlipping) {
            flipStartSpeed[player.carId] = player.vel.Length();
        }
        
        if (wasFlipping && !player.isFlipping) {
            float startSpeed = flipStartSpeed[player.carId];
            float endSpeed = player.vel.Length();
            
            // Speed flip = gained significant speed from flip
            if (endSpeed > startSpeed + 300.0f && endSpeed > 1800.0f) {
                return 10.0f;
            }
        }
        
        return 0.0f;
    }
};

// Advanced recovery - landing with powerslide
class AdvancedRecoveryReward : public Reward {
private:
    mutable std::map<uint32_t, bool> wasAirborne;

public:
    virtual void Reset(const GameState& initialState) override {
        wasAirborne.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        bool wasInAir = wasAirborne[player.carId];
        wasAirborne[player.carId] = !player.isOnGround;
        
        // Just landed
        if (wasInAir && player.isOnGround) {
            float upZ = player.rotMat.up.z;
            
            // Check if using powerslide on landing
            bool usingPowerslide = (player.prevAction.handbrake > 0.5f);
            
            if (upZ > 0.8f && usingPowerslide) {
                // Perfect recovery with powerslide
                return 5.0f;
            } else if (upZ > 0.7f) {
                // Good landing
                return 2.0f;
            }
        }
        
        return 0.0f;
    }
};

// Boost pad path learning - efficient boost collection
class BoostPathReward : public Reward {
private:
    mutable std::map<uint32_t, std::deque<Vec>> recentPadPositions;

public:
    virtual void Reset(const GameState& initialState) override {
        recentPadPositions.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!state.prev) return 0.0f;
        
        float boostGain = player.boost - state.prev->players[player.index].boost;
        
        if (boostGain > 0.0f) {
            // Find which pad was collected
            Vec padPos = player.pos; // Approximate
            
            auto& path = recentPadPositions[player.carId];
            path.push_back(padPos);
            
            // Keep last 5 pads
            if (path.size() > 5) {
                path.pop_front();
            }
            
            // Reward for following boost path (pads close together)
            if (path.size() >= 3) {
                float totalDist = 0.0f;
                for (size_t i = 1; i < path.size(); i++) {
                    totalDist += (path[i] - path[i-1]).Length();
                }
                
                float avgDist = totalDist / (path.size() - 1);
                
                // Reward efficient pathing (short distances between pads)
                if (avgDist < 1500.0f) {
                    return 5.0f;
                }
            }
        }
        
        return 0.0f;
    }
};

// Kickoff awareness - adapting to opponent kickoff behavior
class KickoffAwarenessReward : public Reward {
private:
    mutable std::map<uint32_t, float> kickoffStartTime;
    mutable std::map<uint32_t, bool> opponentCommitted;

public:
    virtual void Reset(const GameState& initialState) override {
        kickoffStartTime.clear();
        opponentCommitted.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        bool isKickoff = (state.ball.pos.Length() < 200.0f && 
                         state.ball.vel.Length() < 100.0f);
        
        if (!isKickoff) {
            kickoffStartTime[player.carId] = 0.0f;
            opponentCommitted[player.carId] = false;
            return 0.0f;
        }
        
        if (kickoffStartTime[player.carId] == 0.0f) {
            kickoffStartTime[player.carId] = 1.0f; // Mark kickoff started
        }
        
        // Check opponent behavior
        bool oppGoingForBall = false;
        for (auto& opp : state.players) {
            if (opp.team == player.team) continue;
            
            Vec oppToBall = (state.ball.pos - opp.pos).Normalized();
            float oppAlignment = opp.vel.Normalized().Dot(oppToBall);
            
            if (oppAlignment > 0.7f && opp.vel.Length() > 1000.0f) {
                oppGoingForBall = true;
                break;
            }
        }
        
        opponentCommitted[player.carId] = oppGoingForBall;
        
        // Reward different strategies based on opponent
        Vec dirToBall = (state.ball.pos - player.pos).Normalized();
        float myAlignment = player.vel.Normalized().Dot(dirToBall);
        
        if (oppGoingForBall) {
            // Opponent is going - contest it
            if (myAlignment > 0.8f && player.vel.Length() > 1200.0f) {
                return 2.0f; // Good kickoff challenge
            }
        } else {
            // Opponent not going - be ready to cheat or collect boost
            float distFromBall = (player.pos - state.ball.pos).Length();
            if (distFromBall > 1000.0f) {
                return 1.0f; // Good positioning when opponent isn't going
            }
        }
        
        return 0.0f;
    }
};

// Dribbling awareness - recognizing when opponent is dribbling
class DribbleAwarenessReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Check if any opponent is dribbling
        for (auto& opp : state.players) {
            if (opp.team == player.team) continue;

            // Check if ball is on opponent's car
            Vec relBallPos = state.ball.pos - opp.pos;
            float heightAboveCar = relBallPos.z;
            float horizDist = Vec(relBallPos.x, relBallPos.y, 0).Length();
            
            bool oppDribbling = (heightAboveCar > 30.0f && heightAboveCar < 200.0f && 
                                horizDist < 150.0f && opp.isOnGround);
            
            if (oppDribbling) {
                // Opponent is dribbling - reward defensive positioning
                float distToOpp = (player.pos - opp.pos).Length();
                
                if (distToOpp > 500.0f && distToOpp < 1500.0f) {
                    // Good spacing against dribbler
                    return 3.0f;
                }
            }
        }
        
        return 0.0f;
    }
};

// ============================================================================
// CHAMPION TIER REWARDS
// ============================================================================

// 45-degree flicks (musty flick, etc)
class AdvancedFlickReward : public Reward {
private:
    mutable std::map<uint32_t, bool> ballWasOnCar;
    mutable std::map<uint32_t, Vec> lastCarForward;

public:
    virtual void Reset(const GameState& initialState) override {
        ballWasOnCar.clear();
        lastCarForward.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        Vec relBallPos = state.ball.pos - player.pos;
        float heightAboveCar = relBallPos.z;
        float horizDist = Vec(relBallPos.x, relBallPos.y, 0).Length();
        
        bool ballOnCar = (heightAboveCar > 30.0f && heightAboveCar < 150.0f && horizDist < 120.0f);
        bool wasPrev = ballWasOnCar[player.carId];
        ballWasOnCar[player.carId] = ballOnCar;
        
        if (wasPrev && player.ballTouchedStep && !ballOnCar) {
            // Flick occurred
            Vec prevForward = lastCarForward[player.carId];
            Vec curForward = player.rotMat.forward;
            
            // Check for backward flick (musty)
            float forwardAlignment = prevForward.Dot(curForward);
            
            if (forwardAlignment < -0.5f) {
                // Backward flick (musty-style)
                float ballSpeed = state.ball.vel.Length();
                if (ballSpeed > 1800.0f) {
                    return 25.0f; // Advanced flick!
                }
            }
        }
        
        lastCarForward[player.carId] = player.rotMat.forward;
        return 0.0f;
    }
};

// Chip shot + shoot combination
class ChipShotReward : public Reward {
private:
    mutable std::map<uint32_t, bool> justChipped;

public:
    virtual void Reset(const GameState& initialState) override {
        justChipped.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) {
            justChipped[player.carId] = false;
            return 0.0f;
        }
        
        bool wasChipped = justChipped[player.carId];
        
        // Check if this is a chip (ball goes up without much forward velocity)
        if (!wasChipped && state.ball.vel.z > 500.0f) {
            float horizSpeed = Vec(state.ball.vel.x, state.ball.vel.y, 0).Length();
            if (horizSpeed < 800.0f) {
                // This is a chip
                justChipped[player.carId] = true;
                return 5.0f;
            }
        }
        
        // If we just chipped, now shooting = combo
        if (wasChipped && state.ball.vel.Length() > 1500.0f) {
            return 15.0f; // Chip + shoot combo!
        }
        
        return 0.0f;
    }
};

// Double tap - two consecutive air touches (ORIGINAL VERSION)
class DoubleTapReward : public Reward {
private:
    mutable std::map<uint32_t, float> firstAirTouchTime;
    mutable std::map<uint32_t, int> airTouchCount;

public:
    virtual void Reset(const GameState& initialState) override {
        firstAirTouchTime.clear();
        airTouchCount.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (player.isOnGround) {
            firstAirTouchTime[player.carId] = 0.0f;
            airTouchCount[player.carId] = 0;
            return 0.0f;
        }
        
        if (player.ballTouchedStep) {
            int& count = airTouchCount[player.carId];
            count++;
            
            if (count == 1) {
                firstAirTouchTime[player.carId] = 1.0f; // Mark first touch
            } else if (count == 2) {
                // Double tap!
                Vec targetGoal = (player.team == Team::BLUE) ?
                    CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
                
                Vec ballToGoal = (targetGoal - state.ball.pos).Normalized();
                float alignment = state.ball.vel.Normalized().Dot(ballToGoal);
                
                if (alignment > 0.5f) {
                    return 30.0f; // Double tap toward goal!
                }
                return 15.0f; // Any double tap
            }
        }
        
        return 0.0f;
    }
};

// Dribble control - maintaining ball on car
class DribbleControlReward : public Reward {
private:
    mutable std::map<uint32_t, float> dribbleTime;

public:
    virtual void Reset(const GameState& initialState) override {
        dribbleTime.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        Vec relBallPos = state.ball.pos - player.pos;
        float heightAboveCar = relBallPos.z;
        float horizDist = Vec(relBallPos.x, relBallPos.y, 0).Length();
        
        bool ballOnCar = (heightAboveCar > 30.0f && heightAboveCar < 200.0f && 
                         horizDist < 150.0f && player.isOnGround);
        
        if (ballOnCar) {
            dribbleTime[player.carId] += state.deltaTime;
            float time = dribbleTime[player.carId];
            
            // Escalating reward for longer dribbles
            if (time > 3.0f) return 10.0f;
            if (time > 2.0f) return 5.0f;
            if (time > 1.0f) return 2.0f;
            return 0.5f;
        } else {
            dribbleTime[player.carId] = 0.0f;
        }
        
        return 0.0f;
    }
};

// Ground to air dribble transition
class GroundToAirDribbleReward : public Reward {
private:
    mutable std::map<uint32_t, bool> wasGroundDribbling;

public:
    virtual void Reset(const GameState& initialState) override {
        wasGroundDribbling.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        Vec relBallPos = state.ball.pos - player.pos;
        float heightAboveCar = relBallPos.z;
        float horizDist = Vec(relBallPos.x, relBallPos.y, 0).Length();
        
        bool ballOnCar = (heightAboveCar > 30.0f && heightAboveCar < 200.0f && horizDist < 150.0f);
        bool wasGround = wasGroundDribbling[player.carId];
        
        // Transition: was on ground with ball, now in air with ball close
        if (wasGround && !player.isOnGround && ballOnCar) {
            return 20.0f; // Ground to air transition!
        }
        
        wasGroundDribbling[player.carId] = (ballOnCar && player.isOnGround);
        return 0.0f;
    }
};

// Low boost offense - attacking with minimal boost
class LowBoostOffenseReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (player.boost > 30.0f) return 0.0f;

        Vec theirGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        
        float ballDistToGoal = (state.ball.pos - theirGoal).Length();
        float myDistToBall = (player.pos - state.ball.pos).Length();
        
        // In offensive position with low boost
        if (ballDistToGoal < 3000.0f && myDistToBall < 1500.0f) {
            if (player.ballTouchedStep) {
                return 8.0f; // Offensive touch with low boost
            }
            return 2.0f; // Offensive positioning with low boost
        }
        
        return 0.0f;
    }
};

// Flip reset detection
class FlipResetDetectionReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!state.prev) return 0.0f;

        if (player.isOnGround || state.prev->players[player.index].isOnGround) {
            return 0.0f;
        }
        
        bool hadFlip = state.prev->players[player.index].HasFlipOrJump();
        bool hasFlip = player.HasFlipOrJump();
        
        // Regained flip in air = flip reset
        if (!hadFlip && hasFlip) {
            // Check if ball is nearby (likely touched ball for reset)
            float distToBall = (player.pos - state.ball.pos).Length();
            if (distToBall < 300.0f) {
                return 30.0f; // Flip reset!
            }
        }
        
        return 0.0f;
    }
};

// Passing plays - setting up teammates
class PassingPlayReward : public Reward {
private:
    mutable std::map<uint32_t, uint32_t> lastTouchedBy;

public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        uint32_t lastTouch = lastTouchedBy[0]; // Global tracker

        if (player.ballTouchedStep) {
            lastTouchedBy[0] = player.carId;
        }
        
        // If teammate scores after our touch = assist
        if (state.goalScored && lastTouch == player.carId) {
            // Check if a teammate scored (not us)
            for (auto& teammate : state.players) {
                if (teammate.team != player.team) continue;
                if (teammate.carId == player.carId) continue;
                
                if (teammate.ballTouchedStep) {
                    return 25.0f; // Passing assist!
                }
            }
        }
        
        return 0.0f;
    }
};

// Redirect - changing ball direction mid-air
class RedirectReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep || player.isOnGround) return 0.0f;
        if (!state.prev) return 0.0f;

        // Check if ball direction changed significantly
        Vec prevBallDir = state.prev->ball.vel.Normalized();
        Vec curBallDir = state.ball.vel.Normalized();
        
        float dirChange = acos(RS_CLAMP(prevBallDir.Dot(curBallDir), -1.0f, 1.0f));
        
        // Significant direction change = redirect
        if (dirChange > 0.5f) { // ~30+ degrees
            Vec targetGoal = (player.team == Team::BLUE) ?
                CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
            
            Vec ballToGoal = (targetGoal - state.ball.pos).Normalized();
            float alignment = curBallDir.Dot(ballToGoal);
            
            if (alignment > 0.7f) {
                return 20.0f; // Redirect toward goal!
            }
            return 10.0f; // Any redirect
        }
        
        return 0.0f;
    }
};

// Wall pinch clears - pinching ball on wall
class WallPinchReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;
        if (!state.prev) return 0.0f;

        // Check if ball is near wall
        float distToWallX = RS_MIN(
            abs(state.ball.pos.x - CommonValues::SIDE_WALL_X),
            abs(state.ball.pos.x + CommonValues::SIDE_WALL_X)
        );
        float distToWallY = RS_MIN(
            abs(state.ball.pos.y - CommonValues::BACK_WALL_Y),
            abs(state.ball.pos.y + CommonValues::BACK_WALL_Y)
        );
        
        float minWallDist = RS_MIN(distToWallX, distToWallY);
        
        if (minWallDist < 100.0f) {
            // Very close to wall
            float prevSpeed = state.prev->ball.vel.Length();
            float curSpeed = state.ball.vel.Length();
            
            // Pinch = massive speed increase
            if (curSpeed > prevSpeed + 1000.0f && curSpeed > 2500.0f) {
                return 25.0f; // Wall pinch!
            }
        }
        
        return 0.0f;
    }
};

// ============================================================================
// GRAND CHAMPION TIER REWARDS
// ============================================================================

// Advanced bounce dribbles - maintaining control on bounce
class BounceDribbleReward : public Reward {
private:
    mutable std::map<uint32_t, int> bounceTouchCount;
    mutable std::map<uint32_t, float> lastBounceTime;

public:
    virtual void Reset(const GameState& initialState) override {
        bounceTouchCount.clear();
        lastBounceTime.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!state.prev) return 0.0f;
        
        // Detect ball bounce (ball Z velocity changes from negative to positive)
        bool ballBounced = (state.prev->ball.vel.z < -200.0f && state.ball.vel.z > 0.0f);
        
        if (ballBounced && player.ballTouchedStep) {
            int& count = bounceTouchCount[player.carId];
            count++;
            lastBounceTime[player.carId] = 1.0f;
            
            if (count >= 2) {
                return 15.0f; // Bounce dribble mastery!
            }
            return 5.0f;
        }
        
        // Reset if too much time passed
        if (lastBounceTime[player.carId] > 0.0f) {
            if (!player.ballTouchedStep) {
                bounceTouchCount[player.carId] = 0;
            }
        }
        
        return 0.0f;
    }
};

// Air dribble bumps - bumping ball multiple times in air
class AirDribbleBumpReward : public Reward {
private:
    mutable std::map<uint32_t, int> consecutiveBumps;

public:
    virtual void Reset(const GameState& initialState) override {
        consecutiveBumps.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (player.isOnGround) {
            consecutiveBumps[player.carId] = 0;
            return 0.0f;
        }
        
        if (player.ballTouchedStep) {
            int& count = consecutiveBumps[player.carId];
            count++;
            
            // Light touches in air
            if (count >= 3) {
                return 20.0f * count; // Advanced air dribble
            }
        }
        
        return 0.0f;
    }
};

// Ceiling ground pinch - pinching from ceiling to ground
class CeilingGroundPinchReward : public Reward {
private:
    mutable std::map<uint32_t, bool> ballWasAtCeiling;

public:
    virtual void Reset(const GameState& initialState) override {
        ballWasAtCeiling.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        bool ballHighNow = (state.ball.pos.z > 1500.0f);
        bool wasHigh = ballWasAtCeiling[player.carId];
        ballWasAtCeiling[player.carId] = ballHighNow;
        
        if (wasHigh && !ballHighNow && player.ballTouchedStep) {
            // Ball came down from ceiling, we touched it
            float ballSpeed = state.ball.vel.Length();
            
            if (ballSpeed > 3000.0f) {
                return 35.0f; // Ceiling pinch!
            }
        }
        
        return 0.0f;
    }
};

// Musty flick (wall, ceiling variations)
class MustyVariationReward : public Reward {
private:
    mutable std::map<uint32_t, bool> wasOnWallOrCeiling;

public:
    virtual void Reset(const GameState& initialState) override {
        wasOnWallOrCeiling.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Check if on wall or ceiling
        bool onSpecialSurface = false;
        
        // On ceiling
        if (player.pos.z > (CommonValues::CEILING_Z - 100.0f)) {
            onSpecialSurface = true;
        }
        
        // On wall
        float distToWallX = RS_MIN(
            abs(player.pos.x - CommonValues::SIDE_WALL_X),
            abs(player.pos.x + CommonValues::SIDE_WALL_X)
        );
        if (distToWallX < 100.0f) {
            onSpecialSurface = true;
        }
        
        bool wasOnSurface = wasOnWallOrCeiling[player.carId];
        wasOnWallOrCeiling[player.carId] = onSpecialSurface;
        
        // Musty-style flick from special surface
        if (wasOnSurface && player.ballTouchedStep && !onSpecialSurface) {
            float ballSpeed = state.ball.vel.Length();
            if (ballSpeed > 2000.0f) {
                return 40.0f; // Advanced musty variation!
            }
        }
        
        return 0.0f;
    }
};

// Perfecting basics - consistency in fundamental mechanics
class ConsistencyReward : public Reward {
private:
    mutable std::map<uint32_t, std::deque<bool>> recentTouches;

public:
    virtual void Reset(const GameState& initialState) override {
        recentTouches.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        auto& touches = recentTouches[player.carId];
        touches.push_back(player.ballTouchedStep);
        
        // Keep last 20 steps
        if (touches.size() > 20) {
            touches.pop_front();
        }
        
        if (touches.size() >= 20) {
            int touchCount = 0;
            for (bool touched : touches) {
                if (touched) touchCount++;
            }
            
            // High touch consistency
            float consistency = (float)touchCount / 20.0f;
            if (consistency > 0.7f) {
                return 5.0f; // Very consistent ball touches
            }
        }
        
        return 0.0f;
    }
};

// ============================================================================
// ADVANCED MECHANICS REWARDS (NEW)
// ============================================================================

// Enhanced Double Tap - with surface detection
class EnhancedDoubleTapReward : public Reward {
private:
    struct TouchData {
        uint32_t carId;
        float time;
        Vec ballPos;
        Vec ballVel;
        bool wasAerial;
    };
    
    mutable std::vector<TouchData> recentTouches;
    mutable std::map<uint32_t, bool> lastBallWallContact;
    mutable std::map<uint32_t, bool> lastBallBackboardContact;
    mutable std::map<uint32_t, bool> lastBallCeilingContact;
    mutable float currentTime;
    
    const float TOUCH_TIMEOUT = 3.0f;
    const float MIN_AERIAL_HEIGHT = 300.0f;
    
    bool IsNearWall(const Vec& pos) const {
        return abs(pos.x) > CommonValues::SIDE_WALL_X - 300.0f;
    }
    
    bool IsNearBackboard(const Vec& pos) const {
        return (abs(pos.y) > CommonValues::BACK_WALL_Y - 300.0f && pos.z > 1952.0f - 300.0f);
    }
    
    bool IsNearCeiling(const Vec& pos) const {
        return pos.z > CommonValues::CEILING_Z - 300.0f;
    }
    
    bool IsGoal(const Vec& pos, Team team) const {
        float targetY = (team == Team::BLUE) ? CommonValues::BACK_WALL_Y : -CommonValues::BACK_WALL_Y;
        return (abs(pos.x) < 893.0f && 
                pos.z < 642.0f && 
                ((team == Team::BLUE && pos.y > targetY) || (team == Team::ORANGE && pos.y < targetY)));
    }

public:
    virtual void Reset(const GameState& initialState) override {
        recentTouches.clear();
        lastBallWallContact.clear();
        lastBallBackboardContact.clear();
        lastBallCeilingContact.clear();
        currentTime = 0.0f;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        currentTime += state.deltaTime;
        float reward = 0.0f;
        
        // Check if ball touched wall/backboard/ceiling
        bool ballWallContact = IsNearWall(state.ball.pos);
        bool ballBackboardContact = IsNearBackboard(state.ball.pos);
        bool ballCeilingContact = IsNearCeiling(state.ball.pos);
        
        // Detect if player just touched ball
        float distToBall = (state.ball.pos - player.pos).Length();
        bool playerTouchingBall = player.ballTouchedStep;
        
        if (playerTouchingBall) {
            bool isAerial = !player.isOnGround && player.pos.z > MIN_AERIAL_HEIGHT;
            
            // Check if this is second touch in double tap sequence
            if (!recentTouches.empty()) {
                TouchData& lastTouch = recentTouches.back();
                
                // Same player, within timeout
                if (lastTouch.carId == player.carId && 
                    (currentTime - lastTouch.time) < TOUCH_TIMEOUT) {
                    
                    bool surfaceContact = false;
                    float surfaceBonus = 1.0f;
                    
                    // Check which surface was contacted
                    if (ballBackboardContact || lastBallBackboardContact[player.carId]) {
                        surfaceContact = true;
                        surfaceBonus = 2.5f;  // Backboard most valuable
                    } else if (ballWallContact || lastBallWallContact[player.carId]) {
                        surfaceContact = true;
                        surfaceBonus = 2.0f;
                    } else if (ballCeilingContact || lastBallCeilingContact[player.carId]) {
                        surfaceContact = true;
                        surfaceBonus = 2.2f;
                    }
                    
                    if (surfaceContact) {
                        // Check if ball going toward opponent goal
                        Vec targetGoal = (player.team == Team::BLUE) ?
                            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
                        Vec toGoal = (targetGoal - state.ball.pos).Normalized();
                        float towardGoal = state.ball.vel.Normalized().Dot(toGoal);
                        
                        if (towardGoal > 0.3f) {
                            // Base double tap reward
                            reward += 0.8f * surfaceBonus;
                            
                            // Bonus for aerial second touch
                            if (isAerial) {
                                reward += 0.5f;
                            }
                            
                            // Bonus for both touches being aerial
                            if (isAerial && lastTouch.wasAerial) {
                                reward += 0.7f;
                            }
                            
                            // Bonus for speed
                            float speed = state.ball.vel.Length();
                            reward += RS_MIN(speed / 2300.0f, 1.0f) * 0.4f;
                            
                            // Major bonus if it scores
                            Team oppTeam = (player.team == Team::BLUE) ? Team::ORANGE : Team::BLUE;
                            if (IsGoal(state.ball.pos, oppTeam)) {
                                reward += 3.0f * surfaceBonus;
                            }
                        }
                    }
                }
            }
            
            // Record this touch
            TouchData touch;
            touch.carId = player.carId;
            touch.time = currentTime;
            touch.ballPos = state.ball.pos;
            touch.ballVel = state.ball.vel;
            touch.wasAerial = isAerial;
            recentTouches.push_back(touch);
            
            // Clean old touches
            recentTouches.erase(
                std::remove_if(recentTouches.begin(), recentTouches.end(),
                    [this](const TouchData& t) {
                        return (currentTime - t.time) > TOUCH_TIMEOUT;
                    }),
                recentTouches.end()
            );
        }
        
        // Update surface contact tracking
        lastBallWallContact[player.carId] = ballWallContact;
        lastBallBackboardContact[player.carId] = ballBackboardContact;
        lastBallCeilingContact[player.carId] = ballCeilingContact;
        
        return reward;
    }
};

// Boost Stealing Reward
class BoostStealReward : public Reward {
private:
    // Big boost pad locations
    const std::vector<Vec> bigBoostPads = {
        Vec(-3072.0f, -4096.0f, 73.0f),  // Blue back left
        Vec(3072.0f, -4096.0f, 73.0f),   // Blue back right
        Vec(-3584.0f, 0.0f, 73.0f),      // Mid left
        Vec(3584.0f, 0.0f, 73.0f),       // Mid right
        Vec(-3072.0f, 4096.0f, 73.0f),   // Orange back left
        Vec(3072.0f, 4096.0f, 73.0f)     // Orange back right
    };
    
    mutable std::map<uint32_t, float> lastBoostAmounts;
    
    bool IsOpposingSide(const Vec& padPos, Team team) const {
        if (team == Team::BLUE) {
            return padPos.y > 1000.0f;  // In orange half
        } else {
            return padPos.y < -1000.0f;  // In blue half
        }
    }

public:
    virtual void Reset(const GameState& initialState) override {
        lastBoostAmounts.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float reward = 0.0f;
        
        // Initialize tracking
        if (lastBoostAmounts.find(player.carId) == lastBoostAmounts.end()) {
            lastBoostAmounts[player.carId] = player.boost;
            return 0.0f;
        }
        
        float boostGained = player.boost - lastBoostAmounts[player.carId];
        
        // Check if player gained boost (picked up pad)
        if (boostGained > 90.0f) {  // Big boost pad threshold
            // Find which pad was likely picked up
            for (const auto& pad : bigBoostPads) {
                float dist = (pad - player.pos).Length();
                
                if (dist < 200.0f) {
                    if (IsOpposingSide(pad, player.team)) {
                        // Stole boost from opponent side!
                        reward += 0.4f;
                        
                        // Extra reward if in opponent corner
                        bool cornerPad = abs(pad.y) > 3500.0f && abs(pad.x) > 2500.0f;
                        if (cornerPad) {
                            reward += 0.3f;
                        }
                        
                        // Bonus based on how low on boost
                        float desperation = 1.0f - (lastBoostAmounts[player.carId] / 100.0f);
                        reward += desperation * 0.2f;
                    }
                    break;
                }
            }
        }
        
        lastBoostAmounts[player.carId] = player.boost;
        return reward;
    }
};

// Reverse Aerial Reward
class ReverseAerialReward : public Reward {
private:
    struct AerialState {
        bool inReverseAerial;
        float startTime;
        Vec initialForward;
    };
    
    mutable std::map<uint32_t, AerialState> aerialStates;
    mutable float currentTime;
    
    const float MIN_AERIAL_HEIGHT = 250.0f;

public:
    virtual void Reset(const GameState& initialState) override {
        aerialStates.clear();
        currentTime = 0.0f;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        currentTime += state.deltaTime;
        float reward = 0.0f;
        
        // Initialize tracking
        if (aerialStates.find(player.carId) == aerialStates.end()) {
            aerialStates[player.carId] = {false, 0.0f, Vec()};
        }
        
        auto& aerialState = aerialStates[player.carId];
        
        Vec toBall = (state.ball.pos - player.pos).Normalized();
        float forwardDot = player.rotMat.forward.Dot(toBall);
        
        bool isAerial = !player.isOnGround && player.pos.z > MIN_AERIAL_HEIGHT;
        bool facingAway = forwardDot < -0.4f;
        
        // Detect start of reverse aerial
        if (isAerial && facingAway && !aerialState.inReverseAerial) {
            aerialState.inReverseAerial = true;
            aerialState.startTime = currentTime;
            aerialState.initialForward = player.rotMat.forward;
        }
        
        // Check if still in reverse aerial
        if (aerialState.inReverseAerial) {
            if (!isAerial) {
                aerialState.inReverseAerial = false;
            } else {
                // Check if hit ball
                float distToBall = (state.ball.pos - player.pos).Length();
                bool hitBall = player.ballTouchedStep;
                
                if (hitBall) {
                    float duration = currentTime - aerialState.startTime;
                    
                    // Calculate how reversed the initial jump was
                    Vec initialToBall = (state.ball.pos - player.pos).Normalized();
                    float reverseAmount = -aerialState.initialForward.Dot(initialToBall);
                    reverseAmount = RS_MAX(0.0f, reverseAmount);
                    
                    // Base reward
                    reward += 0.8f * reverseAmount;
                    
                    // Bonus for duration
                    reward += RS_MIN(duration / 2.0f, 1.0f) * 0.4f;
                    
                    // Bonus for height
                    float heightRatio = (player.pos.z - MIN_AERIAL_HEIGHT) / 1000.0f;
                    reward += RS_MIN(heightRatio, 1.0f) * 0.3f;
                    
                    // Check if toward goal
                    Vec targetGoal = (player.team == Team::BLUE) ?
                        CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
                    Vec toGoal = (targetGoal - state.ball.pos).Normalized();
                    float towardGoal = state.ball.vel.Normalized().Dot(toGoal);
                    
                    if (towardGoal > 0.5f) {
                        reward += 0.5f;
                        
                        // Bonus if scores
                        if ((player.team == Team::BLUE && state.ball.pos.y > CommonValues::BACK_WALL_Y) ||
                            (player.team == Team::ORANGE && state.ball.pos.y < -CommonValues::BACK_WALL_Y)) {
                            reward += 2.0f;
                        }
                    }
                    
                    aerialState.inReverseAerial = false;
                }
            }
        }
        
        return reward;
    }
};

// Field Spacing Reward
class FieldSpacingReward : public Reward {
private:
    struct CoverageArea {
        Vec center;
        float radius;
        float coverageValue;
    };
    
    CoverageArea CalculateCoverageCone(const Player& player, const Vec& ballPos) const {
        CoverageArea area;
        
        Vec toBall = ballPos - player.pos;
        float distToBall = toBall.Length();
        
        // Base radius on distance and boost
        float baseRadius = 800.0f + (player.boost * 5.0f);
        
        // Coverage is stronger when facing ball
        Vec toBallNorm = toBall.Normalized();
        float facingBall = RS_MAX(0.0f, player.rotMat.forward.Dot(toBallNorm));
        
        area.center = player.pos + toBall * 0.6f;
        area.radius = baseRadius * (0.5f + 0.5f * facingBall);
        
        // Value based on position
        float valueMultiplier = 1.0f;
        
        Vec myGoal = (player.team == Team::BLUE) ?
            CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;
        
        bool betweenBallAndGoal = 
            (player.team == Team::BLUE && player.pos.y < ballPos.y) ||
            (player.team == Team::ORANGE && player.pos.y > ballPos.y);
        
        if (betweenBallAndGoal) {
            valueMultiplier *= 1.3f;
        }
        
        area.coverageValue = valueMultiplier;
        
        return area;
    }
    
    float CalculateOverlap(const CoverageArea& a1, const CoverageArea& a2) const {
        float dist = (a1.center - a2.center).Length();
        float combinedRadius = a1.radius + a2.radius;
        
        if (dist >= combinedRadius) {
            return 0.0f;
        }
        
        float overlap = (combinedRadius - dist) / combinedRadius;
        return overlap;
    }

public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float reward = 0.0f;
        
        // Calculate coverage area for this player
        CoverageArea myCoverage = CalculateCoverageCone(player, state.ball.pos);
        
        // Check overlap with teammates
        for (const auto& other : state.players) {
            if (other.carId == player.carId) continue;
            if (other.team != player.team) continue;
            
            CoverageArea otherCoverage = CalculateCoverageCone(other, state.ball.pos);
            float overlap = CalculateOverlap(myCoverage, otherCoverage);
            
            if (overlap < 0.2f) {
                reward += 0.1f;  // Good spacing
            } else if (overlap >= 0.2f && overlap < 0.5f) {
                reward -= 0.15f * overlap;  // Slight overlap
            } else if (overlap >= 0.5f && overlap < 0.8f) {
                reward -= 0.4f * overlap;  // Medium overlap
            } else {
                reward -= 0.8f * overlap;  // Severe overlap
            }
        }
        
        // Reward good positioning relative to ball
        float distToBall = (state.ball.pos - player.pos).Length();
        
        int teammatesCloser = 0;
        for (const auto& other : state.players) {
            if (other.carId == player.carId) continue;
            if (other.team != player.team) continue;
            
            float otherDist = (state.ball.pos - other.pos).Length();
            if (otherDist < distToBall) {
                teammatesCloser++;
            }
        }
        
        // Reward based on role
        if (teammatesCloser == 0 && distToBall < 1000.0f) {
            reward += 0.15f;  // 1st man
        } else if (teammatesCloser == 1 && distToBall > 1500.0f && distToBall < 3500.0f) {
            reward += 0.2f;  // 2nd man
        } else if (teammatesCloser >= 2 && distToBall > 3000.0f) {
            reward += 0.15f;  // 3rd man
        }
        
        return reward;
    }
};

} // namespace RLGC
