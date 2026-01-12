// CompleteMechanicsRewards.h
// Complete reward set for all mechanics from Bronze to Grand Champion + Advanced Mechanics
// Place in: GigaLearnCPP/GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/

#pragma once
#include "Reward.h"
#include "../Math.h"
#include <map>
#include <deque>
#include <vector>
#include <atomic>
#include "RewardMath.h"
#include "AirDribbleAccuracyReward.h"

namespace RLGC {
    // Helpers
    // Helper: directional multiplier for dribble-like rewards
    // Returns:
    //   1.0  -> ball moving toward opponent goal
    //   0.75 -> ball moving toward opponent half (but not clearly at goal)
    //   0.5  -> neither condition
    static inline float ComputeDribbleDirectionMultiplier(const Player& player, const GameState& state) {
        // Defensive guards
        if (!state.prev && state.ball.vel.Length() < 1e-3f) {
            // no reliable velocity; fall back to player facing
        }

        // Opponent goal and vector from ball -> opponent goal
        Vec opponentGoal = (player.team == Team::BLUE) ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        Vec toOpp = opponentGoal - state.ball.pos;
        float toOppLen = toOpp.Length();

        float ballSpeed = state.ball.vel.Length();

        float towardOpp = 0.0f;
        if (ballSpeed > 1e-3f && toOppLen > 1e-5f) {
            towardOpp = state.ball.vel.Normalized().Dot(toOpp / toOppLen);
        }
        else if (toOppLen > 1e-5f) {
            towardOpp = player.rotMat.forward.Dot(toOpp / toOppLen); // fallback
        }

        if (towardOpp > 0.1f) {
            return 1.0f; // clearly moving toward opponent goal
        }

        // Check "toward opponent half" direction
        // For BLUE team opponent half is +Y, for ORANGE it's -Y
        Vec halfDir = Vec(0.0f, (player.team == Team::BLUE) ? 1.0f : -1.0f, 0.0f);

        float towardHalf = 0.0f;
        if (ballSpeed > 1e-3f) {
            towardHalf = state.ball.vel.Normalized().Dot(halfDir);
        }
        else {
            towardHalf = player.rotMat.forward.Dot(halfDir); // fallback to player facing
        }

        if (towardHalf > 0.1f) {
            return 0.75f; // moving toward opponent half but not specifically the goal
        }

        return 0.5f; // neither condition
    }

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

// ============================================================================
// SILVER TIER REWARDS
// ============================================================================

// Half-flip execution
// 2025-12-07 - Rev 2 - Require starting backwards and ending forwards,
//                      not just 180° orientation. (NJH)
class HalfFlipReward : public Reward {
private:
    mutable std::map<uint32_t, Vec>  flipStartForward;
    mutable std::map<uint32_t, Vec>  flipStartVel;
    mutable std::map<uint32_t, bool> isFlipping;
    mutable std::map<uint32_t, bool> startedFromBackwards;

public:
    virtual void Reset(const GameState& initialState) override {
        flipStartForward.clear();
        flipStartVel.clear();
        isFlipping.clear();
        startedFromBackwards.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        bool wasFlipping = isFlipping[player.carId];
        isFlipping[player.carId] = player.isFlipping;

        // Flip just started
        if (!wasFlipping && player.isFlipping) {
            flipStartForward[player.carId] = player.rotMat.forward;
            flipStartVel[player.carId] = player.vel;

            bool backwardsMotion = false;

            // Check motion relative to facing (moving backwards)
            float startSpeed = player.vel.Length();
            if (startSpeed > 300.0f) {
                Vec velDir = player.vel / startSpeed;
                float velAlign = velDir.Dot(player.rotMat.forward);
                if (velAlign < -0.5f) {
                    backwardsMotion = true;
                }
            }

            // Also accept explicit backflip input (stick pitched back)
            if (player.prevAction.pitch < -0.5f) {
                backwardsMotion = true;
            }

            startedFromBackwards[player.carId] = backwardsMotion;
            return 0.0f;
        }

        // Flip just ended
        if (wasFlipping && !player.isFlipping) {
            auto itFwd = flipStartForward.find(player.carId);
            auto itVel = flipStartVel.find(player.carId);
            if (itFwd == flipStartForward.end() || itVel == flipStartVel.end())
                return 0.0f;

            Vec startForward = itFwd->second;
            Vec endForward = player.rotMat.forward;

            // Orientation should be roughly 180° apart
            float forwardAlignment = startForward.Dot(endForward);
            if (forwardAlignment > -0.7f)
                return 0.0f;

            // Must have actually started from a backwards state
            if (!startedFromBackwards[player.carId])
                return 0.0f;

            // End state: moving forwards relative to facing
            float endSpeed = player.vel.Length();
            if (endSpeed < 300.0f)
                return 0.0f;

            Vec endVelDir = player.vel / endSpeed;
            float endVelAlign = endVelDir.Dot(endForward);
            if (endVelAlign < 0.5f)
                return 0.0f;

            // Successful half-flip (backwards → forwards with 180° rotation)
            return 10.0f;
        }

        return 0.0f;
    }
};


// Power slide usage for better control
// 2025-12-07 - Rev 2 - Reward short taps (<=0.25s), penalize long holds. (NJH)
class PowerSlideControlReward : public Reward {
private:
    mutable std::map<uint32_t, float> slideTime;

    float maxTapDuration = 0.25f; // seconds

public:
    virtual void Reset(const GameState& initialState) override {
        slideTime.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

        if (!player.isOnGround || player.prevAction.handbrake <= 0.5f) {
            // Reset when not sliding
            slideTime[player.carId] = 0.0f;
            return 0.0f;
        }

        // Accumulate powerslide duration
        float t = slideTime[player.carId] + dt;
        slideTime[player.carId] = t;

        float speed = player.vel.Length();

        // Only care if we’re actually moving
        if (speed < 300.0f)
            return 0.0f;

        // Short taps (0..0.25s): small positive
        if (t <= maxTapDuration) {
            // Scale by time and speed
            float tapFrac = t / maxTapDuration;          // [0,1]
            float speedFrac = RS_MIN(1.0f, speed / 1500.0f);
            return 0.5f * tapFrac * speedFrac;
        }

        // Long holds: penalize every frame after threshold
        float over = t - maxTapDuration;
        float penaltyRate = 1.0f; // per second
        return -penaltyRate * (over / maxTapDuration) * dt;
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

// ============================================================================
// WallPopReward
// 2025-12-19 | Rev 1.0 | NJH
// - Rewards popping the ball upward after a wall touch
// - Requires: recent wall contact + ball touch + positive Z ball velocity
// ============================================================================
class WallPopReward : public Reward {
private:
    // Wall proximity
    float wallX = CommonValues::SIDE_WALL_X;
    float wallTolerance = 180.0f;

    // Timing
    float recentWallWindow = 0.25f; // seconds

    // Ball pop requirement
    float minBallZVel = 200.0f; // uu/s upward

    // State
    mutable std::map<uint32_t, float> timeSinceOnWall;

public:
    virtual void Reset(const GameState& initialState) override {
        timeSinceOnWall.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        const float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
        const uint32_t id = player.carId;

        // Detect wall contact (not ground)
        const bool nearSideWall =
            fabs(player.pos.x) > (wallX - wallTolerance);

        const bool onWall =
            nearSideWall && !player.isOnGround;

        auto& tSinceWall = timeSinceOnWall[id];

        if (onWall) {
            tSinceWall = 0.0f;
        }
        else {
            tSinceWall += dt;
        }

        // Must touch the ball shortly after wall contact
        if (!player.ballTouchedStep) return 0.0f;
        if (tSinceWall > recentWallWindow) return 0.0f;

        // Ball must be popped upward
        if (state.ball.vel.z < minBallZVel) return 0.0f;

        // Small shaping by pop strength
        float zFrac = RS_CLAMP((state.ball.vel.z - minBallZVel) / 800.0f, 0.0f, 1.0f);
        return 4.0f + 4.0f * zFrac; // 4..8
    }
};

// ============================================================================
// WallShotReward
// 2025-12-19 | Rev 1.0 | NJH
// - Rewards shots on target that originate from wall play
// - Requires recent wall contact + ball touch + on-goal trajectory
// ============================================================================
class WallShotReward : public Reward {
private:
    float wallX = CommonValues::SIDE_WALL_X;
    float wallTolerance = 180.0f;

    float recentWallWindow = 0.35f; // slightly longer than pop

    mutable std::map<uint32_t, float> timeSinceOnWall;

public:
    virtual void Reset(const GameState& initialState) override {
        timeSinceOnWall.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        const float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
        const uint32_t id = player.carId;

        const bool nearSideWall =
            fabs(player.pos.x) > (wallX - wallTolerance);

        const bool onWall =
            nearSideWall && !player.isOnGround;

        auto& tSinceWall = timeSinceOnWall[id];

        if (onWall) {
            tSinceWall = 0.0f;
        }
        else {
            tSinceWall += dt;
        }

        if (!player.ballTouchedStep) return 0.0f;
        if (tSinceWall > recentWallWindow) return 0.0f;

        // Must be a real shot on goal
        if (!RLGC::RewardMath::BallIntersectsOpponentGoalMouth(
            player,
            state,
            2.5f,   // tMaxSec
            true,   // require back-net confirmation
            true    // gravity
        )) {
            return 0.0f;
        }

        // Scale by shot speed
        const float speed = state.ball.vel.Length();
        float speedFrac = RS_CLAMP((speed - 1000.0f) / 2500.0f, 0.0f, 1.0f);

        return 10.0f + 10.0f * speedFrac; // 10..20
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

// ============================================================================
// BackboardReboundReward
//  - Track last time ball was near opponent backboard
//  - If player then follows up within a short timeout and
//    hits a strong shot toward goal from near net, reward.
// 2025-12-09 - Rev 1 (NJH)
// ============================================================================
class BackboardReboundReward : public Reward {
private:
    float currentTime = 0.0f;

    struct BoardState {
        float lastBackboardTime = -1.0f;
        Vec   lastBackboardPos;
    };

    mutable std::map<Team, BoardState> boardStates;

    bool IsNearOpponentBackboard(const Vec& pos, Team playerTeam) const {
        // Opponent back wall Y
        float oppY = (playerTeam == Team::BLUE) ? CommonValues::BACK_WALL_Y
            : -CommonValues::BACK_WALL_Y;
        // Close in Y, above ground a bit
        return (std::fabs(pos.y - oppY) < 300.0f && pos.z > 300.0f);
    }

    bool IsInScoringZone(const Vec& pos, Team playerTeam) const {
        float goalY = (playerTeam == Team::BLUE) ? CommonValues::BACK_WALL_Y
            : -CommonValues::BACK_WALL_Y;
        // Close to goal region in front of net
        return (std::fabs(pos.x) < 1500.0f && std::fabs(pos.y - goalY) < 2000.0f);
    }

public:
    virtual void Reset(const GameState& initialState) override {
        currentTime = 0.0f;
        boardStates.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
        currentTime += dt;

        Team playerTeam = player.team;
        Team oppTeam = (playerTeam == Team::BLUE) ? Team::ORANGE : Team::BLUE;

        auto& bs = boardStates[oppTeam]; // track backboard of opponent's side

        // Track when ball is near opponent backboard
        if (IsNearOpponentBackboard(state.ball.pos, playerTeam)) {
            bs.lastBackboardTime = currentTime;
            bs.lastBackboardPos = state.ball.pos;
        }

        if (!player.ballTouchedStep)
            return 0.0f;

        // Require recent backboard contact
        if (bs.lastBackboardTime < 0.0f || (currentTime - bs.lastBackboardTime) > 2.0f)
            return 0.0f;

        // Player touch after backboard: check if this is near scoring zone
        if (!IsInScoringZone(state.ball.pos, playerTeam))
            return 0.0f;

        Vec targetGoal = (playerTeam == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_BACK
            : CommonValues::BLUE_GOAL_BACK;

        Vec toGoal = (targetGoal - state.ball.pos).Normalized();
        float speed = state.ball.vel.Length();
        if (speed < 900.0f)
            return 0.0f;

        Vec dir = state.ball.vel / speed;
        float align = dir.Dot(toGoal);
        if (align < 0.6f)
            return 0.0f;

        float alignScore = RS_CLAMP((align - 0.6f) / 0.4f, 0.0f, 1.0f);
        float speedScore = RS_CLAMP(speed / 2300.0f, 0.0f, 1.0f);

        float reward = 4.0f * alignScore * speedScore;

        // Optional: big bonus if this frame caused a goal
        if (state.goalScored) {
            reward += 3.0f;
        }

        return RS_CLAMP(reward, 0.0f, 8.0f);
    }
};


// ============================================================================
// HookShotReward
// Detects a "hook shot" chip from ground:
//  - Ball low (rolling / slight bounce), near ground
//  - Pre-touch: ball not strongly going toward goal
//  - Post-touch: ball moving strongly toward opponent goal with higher speed
//  - Player near ground at touch
// 2025-12-09 - Rev 1 (NJH)
// ============================================================================
class HookShotReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep || !state.prev)
            return 0.0f;

        // Only consider low balls
        if (state.ball.pos.z > 200.0f)
            return 0.0f;

        // Player should be essentially grounded at touch
        if (!player.isOnGround && player.pos.z > 150.0f)
            return 0.0f;

        Vec targetGoal = (player.team == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_BACK
            : CommonValues::BLUE_GOAL_BACK;

        Vec toGoal = (targetGoal - state.ball.pos).Normalized();

        Vec prevVel = state.prev->ball.vel;
        Vec curVel = state.ball.vel;

        float prevSpeed = prevVel.Length();
        float curSpeed = curVel.Length();

        if (curSpeed < 800.0f)
            return 0.0f;

        if (prevSpeed < 200.0f)
            return 0.0f;

        Vec prevDir = prevVel / prevSpeed;
        Vec curDir = curVel / curSpeed;

        float prevAlign = prevDir.Dot(toGoal);
        float curAlign = curDir.Dot(toGoal);

        // Before: ball not really going straight at goal
        if (prevAlign > 0.5f)
            return 0.0f;

        // After: ball strongly toward goal
        if (curAlign < 0.7f)
            return 0.0f;

        // Speed gain: hooked it hard
        float speedGain = curSpeed - prevSpeed;
        if (speedGain < 400.0f)
            return 0.0f;

        float alignScore = RS_CLAMP((curAlign - 0.7f) / 0.3f, 0.0f, 1.0f);
        float speedScore = RS_CLAMP(speedGain / 1000.0f, 0.0f, 1.0f);

        float reward = 4.0f * alignScore * speedScore;
        return RS_CLAMP(reward, 0.0f, 6.0f);
    }
};


// 2025-12-17 | Rev 5 | NJH
// - Allow opponent-half credit at 1/4 reward (still learn "win 50s" upfield)
// - Keep max payout near own goal (danger sphere)

class FiftyFiftyReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;

        bool isFiftyFifty = false;
        for (const auto& opponent : state.players) {
            if (opponent.team == player.team) continue;
            if (opponent.ballTouchedStep) { isFiftyFifty = true; break; }
        }
        if (!isFiftyFifty) return 0.0f;

        const Vec myGoalBack = (player.team == Team::BLUE) ? CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;
        const Vec myGoalCenter = (player.team == Team::BLUE) ? CommonValues::BLUE_GOAL_CENTER : CommonValues::ORANGE_GOAL_CENTER;

        const bool ballInOwnHalf = (player.team == Team::BLUE) ? (state.ball.pos.y <= 0.0f) : (state.ball.pos.y >= 0.0f);

        Vec goalToBall = state.ball.pos - myGoalBack;
        float distGB = goalToBall.Length();
        if (distGB < 1e-3f) return 0.0f;

        Vec goalToPlayer = player.pos - myGoalBack;
        Vec lineDir = goalToBall / distGB;

        float projPlayer = goalToPlayer.Dot(lineDir);
        Vec lateral = goalToPlayer - lineDir * projPlayer;
        float lateralDist = lateral.Length();

        if (projPlayer < 0.0f || projPlayer > distGB + 500.0f) return 0.0f;

        float alignmentScore = RS_CLAMP(1.0f - lateralDist / 1500.0f, 0.0f, 1.0f);
        float depthScore = RS_CLAMP(projPlayer / distGB, 0.0f, 1.0f);

        float positionalScore = alignmentScore * depthScore;
        if (positionalScore <= 0.0f) return 0.0f;

        const float distBallToMyGoalCenter = (state.ball.pos - myGoalCenter).Length();

        float zoneMult = 1.0f;
        if (distBallToMyGoalCenter <= 2000.0f) {
            zoneMult = 1.0f;
            positionalScore = 1.0f;
        }
        else {
            zoneMult = ballInOwnHalf ? 0.75f : 0.25f; // <<< key change
        }

        const float baseReward = 5.0f;
        return baseReward * positionalScore * zoneMult;
    }
};

// ============================================================================
// EmergencySaveCommitReward
// 2025-12-16 - Rev 1 (NJH):
//  - When ball is threatening our net, reward committing to the save:
//    * closing speed to ball
//    * positioning between ball and own goal
//  - Idea: makes “do anything to save” outscore low-value mechanic farming.
// ============================================================================
class EmergencySaveCommitReward : public Reward {
private:
    float dangerDist = 2600.0f;     // how close ball is to our goal to trigger
    float minThreatDot = 0.25f;     // ball moving toward our goal

public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        const Vec myGoal = (player.team == Team::BLUE) ? CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

        Vec ballToGoal = (myGoal - state.ball.pos);
        float distBallGoal = ballToGoal.Length();
        if (distBallGoal > dangerDist) return 0.0f;

        Vec ballVelN = SafeNorm(state.ball.vel);
        Vec toGoalN = SafeNorm(ballToGoal);

        float threat = ballVelN.Dot(toGoalN);
        if (threat < minThreatDot) return 0.0f;

        // Reward: close on ball (positive speed toward ball)
        Vec toBallN = SafeNorm(state.ball.pos - player.pos);
        float speedToBall = player.vel.Dot(toBallN); // uu/s

        // Reward: “between ball and goal” geometry
        // player should be closer to goal than ball is
        float distPlayerGoal = (player.pos - myGoal).Length();
        float between = (distPlayerGoal < distBallGoal) ? 1.0f : 0.0f;

        // Normalize speed-to-ball into [0..1] using car max speed
        float speedFrac = RS_CLAMP(speedToBall / CommonValues::CAR_MAX_SPEED, 0.0f, 1.0f);

        // Urgency multiplier increases as ball gets closer
        float urgency = RS_CLAMP(1.0f - (distBallGoal / dangerDist), 0.0f, 1.0f);

        // Small continuous reward per tick (keep it modest; it’s always-on in danger)
        return (0.6f * speedFrac + 0.4f * between) * (0.5f + 0.5f * urgency);
    }
};

// ============================================================================
// ShadowDefenseVsPossessionReward
// 2025-12-16 - Rev 1 (NJH):
//  - Detect opponent possession (dribble-ish or close control).
//  - Reward shadowing: sit on line between ball and our goal at a safe distance,
//    and match lateral (x) as ball moves.
// ============================================================================
class ShadowDefenseVsPossessionReward : public Reward {
private:
    bool OppHasPossession(const Player& me, const GameState& state, Player& outOpp) const {
        // Pick the “best” opponent possessor
        float bestScore = 0.0f;
        bool found = false;

        for (auto& opp : state.players) {
            if (opp.team == me.team) continue;
            if (opp.isDemoed) continue;

            Vec rel = state.ball.pos - opp.pos;
            float h = rel.z;
            float horiz = Vec(rel.x, rel.y, 0).Length();

            bool ballOnCar = (h > 25.0f && h < 210.0f && horiz < 155.0f && opp.isOnGround);

            float distOppBall = rel.Length();
            float ballSpeed = state.ball.vel.Length();

            // Also count “soft control” even if not perfectly on roof
            bool closeControl = (distOppBall < 420.0f && ballSpeed < 1400.0f && opp.isOnGround);

            if (ballOnCar || closeControl) {
                float score = ballOnCar ? 2.0f : 1.0f;
                score += RS_CLAMP(1.0f - distOppBall / 600.0f, 0.0f, 1.0f);

                if (score > bestScore) {
                    bestScore = score;
                    outOpp = opp;
                    found = true;
                }
            }
        }
        return found;
    }

public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        Player possessor;
        if (!OppHasPossession(player, state, possessor)) return 0.0f;

        const Vec myGoal = (player.team == Team::BLUE) ? CommonValues::BLUE_GOAL_CENTER : CommonValues::ORANGE_GOAL_CENTER;

        // Only really matters when ball is in/near our half
        const bool ballInOwnHalf = (player.team == Team::BLUE) ? (state.ball.pos.y <= 800.0f) : (state.ball.pos.y >= -800.0f);
        if (!ballInOwnHalf) return 0.0f;

        Vec ballToGoalN = SafeNorm(myGoal - state.ball.pos);

        // Shadow point: between ball and goal, a fixed distance from ball
        float shadowDist = 1400.0f; // sweet spot for 1v1 shadowing
        Vec shadowPoint = state.ball.pos + ballToGoalN * shadowDist;

        float distToShadow = (player.pos - shadowPoint).Length();

        // Reward closeness to shadow point
        float posScore = RS_CLAMP(1.0f - distToShadow / 1600.0f, 0.0f, 1.0f);

        // Lateral match: encourage covering same x-line as ball (helps “side-to-side across goal”)
        float xErr = fabs(player.pos.x - state.ball.pos.x);
        float lateralScore = RS_CLAMP(1.0f - xErr / 1800.0f, 0.0f, 1.0f);

        // Extra: be between ball and goal (don’t over-commit past ball)
        float distPlayerGoal = (player.pos - myGoal).Length();
        float distBallGoal = (state.ball.pos - myGoal).Length();
        float between = (distPlayerGoal < distBallGoal) ? 1.0f : 0.0f;

        return (0.6f * posScore + 0.3f * lateralScore + 0.1f * between);
    }
};


// ============================================================================
// CatchBallReward
// Rewards soft "catch" touches that slow a fast ball and keep it close & in
// front of the car, promoting possession instead of booming it away.
// 2025-12-08 - Rev 2 - Added positional & speed-shaping. (NJH)
// ============================================================================
class CatchBallReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep)
            return 0.0f;
        if (!state.prev)
            return 0.0f;

        float prevBallSpeed = state.prev->ball.vel.Length();
        float ballSpeed = state.ball.vel.Length();

        // Only treat as "catch" if ball was moving reasonably fast
        if (prevBallSpeed < 800.0f)
            return 0.0f;

        // Slowdown factor: bigger slowdown = better catch
        float slowdown = RS_CLAMP((prevBallSpeed - ballSpeed) / prevBallSpeed, 0.0f, 1.0f);
        if (slowdown <= 0.0f)
            return 0.0f;

        // Ball should be near and in front of the car
        Vec rel = state.ball.pos - player.pos;
        float dist = rel.Length();
        if (dist > 600.0f)
            return 0.0f;

        Vec forward = player.rotMat.forward;
        float frontAlign = forward.Dot(rel.Normalized()); // 1 = directly in front
        if (frontAlign < 0.3f)
            return 0.0f;

        // Penalize if ball leaves too fast even after "catch"
        float baseCap = 1200.0f;
        float speedPenaltyFactor = 1.0f;
        if (ballSpeed > baseCap) {
            speedPenaltyFactor = RS_CLAMP(1.0f - (ballSpeed - baseCap) / 1000.0f, 0.0f, 1.0f);
        }

        float alignFactor = 0.5f + 0.5f * RS_CLAMP(frontAlign, 0.0f, 1.0f);

        // Final catch reward: good slowdown, good alignment, and ball not flying away
        float reward = 4.0f * slowdown * alignFactor * speedPenaltyFactor;
        return RS_CLAMP(reward, 0.0f, 8.0f);
    }
};

// CatchBallOnCarReward
// Rewards soft "catch" touches that slow a fast ball and keep it close & in
// front of the car, promoting possession instead of booming it away.
// 2025-12-08 - Rev 2 - Added positional & speed-shaping. (NJH)
// 2025-12-14 - Rev (tuned) - emphasize catches that end up "on top" of the car:
//   * tighter on-top geometry (height + horiz radius)
//   * larger bonus when ball is effectively carried (low relative velocity)
//   * more weight to slowdown while retaining alignment & speed penalties
class CatchBallOnCarReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep)
            return 0.0f;
        if (!state.prev)
            return 0.0f;

        // Previous and current ball speeds
        float prevBallSpeed = state.prev->ball.vel.Length();
        float ballSpeed = state.ball.vel.Length();

        // Only treat as "catch" if ball was moving reasonably fast (but allow somewhat lower)
        if (prevBallSpeed < 600.0f)
            return 0.0f;

        // Slowdown factor: bigger slowdown = better catch (0..1)
        float slowdown = RS_CLAMP((prevBallSpeed - ballSpeed) / prevBallSpeed, 0.0f, 1.0f);
        if (slowdown <= 0.0f)
            return 0.0f;

        // Ball relative to car
        Vec rel = state.ball.pos - player.pos;
        float horizDist = Vec(rel.x, rel.y, 0).Length();
        float heightAboveCar = rel.z;

        // Tight "on-top" geometry: prefer balls that land near car center and atop the roof
        bool isOnTop = (heightAboveCar > 50.0f && heightAboveCar < 140.0f && horizDist < 100.0f);

        // Also accept slightly laxer "near-top" for partial credit
        bool isNearTop = (heightAboveCar > 30.0f && heightAboveCar < 200.0f && horizDist < 220.0f);

        // Must be roughly in front of the car
        Vec forward = player.rotMat.forward;
        float frontAlign = forward.Dot(rel.Normalized()); // 1 = directly in front
        if (frontAlign < 0.4f)
            return 0.0f; // require stronger frontal alignment than before

        // Penalize if ball leaves too fast even after "catch"
        float baseCap = 1200.0f;
        float speedPenaltyFactor = 1.0f;
        if (ballSpeed > baseCap) {
            speedPenaltyFactor = RS_CLAMP(1.0f - (ballSpeed - baseCap) / 1000.0f, 0.0f, 1.0f);
            if (speedPenaltyFactor <= 0.0f) return 0.0f;
        }

        // Alignment factor: prefer catches directly in front
        float alignFactor = 0.6f + 0.4f * RS_CLAMP(frontAlign, 0.0f, 1.0f); // [0.6..1.0]

        // Relative velocity between ball and car: small => ball is effectively "on" the car
        Vec relVel = state.ball.vel - player.vel;
        float relSpeed = relVel.Length();

        // On-top carry bonus if relative speed is small
        float carryBonus = 0.0f;
        if (isOnTop && relSpeed < 250.0f) {
            carryBonus = 4.0f; // strong bonus for true on-top catches that are moving with car
        }
        else if (isNearTop && relSpeed < 350.0f) {
            carryBonus = 2.0f; // modest bonus for near-top, low relative speed
        }

        // Base catch reward: emphasize slowdown but scale by alignment and speed penalty
        float baseReward = 5.0f * slowdown * alignFactor * speedPenaltyFactor; // up to ~5

        // Extra small bonus for being on-top even if slowdown modest
        float topProximityBonus = isOnTop ? 1.0f : (isNearTop ? 0.4f : 0.0f);

        float reward = baseReward + carryBonus + topProximityBonus;

        // Final clamp: allow bigger payouts for exceptional on-top catches but bounded
        float maxReward = isOnTop ? 15.0f : 8.0f;
        return RS_CLAMP(reward, 0.0f, maxReward);
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

// ============================================================================
// EDITED: ShotAccuracyReward - Now returns multiplier values (1.0 base, 1.5 bonus)
// ============================================================================

// Shot accuracy - aiming for corners of goal
// NOTE: Returns multiplier values (1.0 = no bonus, 1.5 = bonus)
// Use as a multiplier for shot rewards via MathRewardWrapper::MULTIPLY
class ShotAccuracyReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 1.0f; // Base multiplier, no bonus

        Vec targetGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        // Check if ball is moving toward goal
        Vec ballToGoal = (targetGoal - state.ball.pos).Normalized();
        float alignment = state.ball.vel.Normalized().Dot(ballToGoal);

        if (alignment < 0.5f) return 1.0f; // Not shooting at goal - base multiplier

        // Calculate where ball is heading relative to goal
        // Top corners are best
        float goalCenterY = targetGoal.y;
        float goalCenterZ = CommonValues::GOAL_HEIGHT / 2.0f;

        // Target corners
        Vec topLeftCorner = Vec(-CommonValues::GOAL_WIDTH_FROM_CENTER + 200.0f,
            goalCenterY,
            CommonValues::GOAL_HEIGHT - 100.0f);
        Vec topRightCorner = Vec(CommonValues::GOAL_WIDTH_FROM_CENTER - 200.0f,
            goalCenterY,
            CommonValues::GOAL_HEIGHT - 100.0f);

        // Check if ball trajectory aims near corners
        Vec ballVelDir = state.ball.vel.Normalized();
        float distToLeftCorner = (topLeftCorner - state.ball.pos).Normalized().Dot(ballVelDir);
        float distToRightCorner = (topRightCorner - state.ball.pos).Normalized().Dot(ballVelDir);

        float bestCornerAlignment = RS_MAX(distToLeftCorner, distToRightCorner);

        if (bestCornerAlignment > 0.9f) {
            return 1.5f; // Excellent corner shot - 1.5x multiplier
        }
        else if (bestCornerAlignment > 0.7f) {
            return 1.5f; // Good shot placement - 1.5x multiplier
        }

        return 1.0f; // Inaccurate shot - base multiplier
    }
};


// ============================================================================
// REWARD CONFIGURATION: SpeedScaledShot multiplied by ShotAccuracy
// ============================================================================

// This replaces your existing:
// WeightedReward(new ZeroSumReward(new SpeedScaledShotReward(), 1.0f), 38.0f),

// New configuration with MathRewardWrapper multiply:
//WeightedReward(
//    new ZeroSumReward(
//        new MathRewardWrapper(
//            {
//                new SpeedScaledShotReward(),
//                new ShotAccuracyReward()
//            },
//            MathOp::MULTIPLY
//        ),
//        1.0f
//    ),
//    38.0f
//)


// ============================================================================
// HOW THIS WORKS:
// ============================================================================
// 1. SpeedScaledShotReward outputs: shot_reward (varies based on speed)
// 2. ShotAccuracyReward outputs: multiplier (1.0 or 1.5)
// 3. MathRewardWrapper multiplies them: final_reward = shot_reward * multiplier
// 4. ZeroSumReward wraps the result for zero-sum game dynamics
// 5. WeightedReward scales final result by 38.0f
//
// Result: Fast, accurate shots get 1.5x bonus. Fast but inaccurate shots get 1.0x (no bonus).

// BackboardDefenseReward (improved)
// - Hybrid behavior:
//   * Continuous shaping: small reward when the ball's distance from our goal increases (i.e. cleared away).
//   * Discrete on-touch rewards:
//       - strong clear toward corner (fast, well aligned) => immediate reward
//       - soft close touch (possession-preserving) => small immediate reward + stores for follow-up
//       - follow-up aerial within timeout after a soft touch => bonus (encourages setting up aerials / airdribble)
// - Works robustly with and without state.prev
class BackboardDefenseReward : public Reward {
private:
    mutable std::map<uint32_t, float> lastSoftTouchTime;
    mutable std::map<uint32_t, Vec>   lastSoftTouchPos;

    // config
    const float BACKBOARD_Y_MARGIN = 500.0f;   // how close to back wall to consider "on backboard"
    const float MIN_HEIGHT = 500.0f;           // z threshold to consider high backboard
    const float SCORING_ZONE_DIST = 3000.0f;   // within this distance to goal we care more
    const float STRONG_CLEAR_SPEED = 900.0f;   // min resulting ball speed for "strong" clear
    const float STRONG_CLEAR_ALIGN = 0.6f;     // alignment of ball vel -> goal for strong clear
    const float SOFT_TOUCH_SPEED_DROP = 300.0f;// speed drop threshold to consider a soft touch
    const float SOFT_TOUCH_NEAR = 400.0f;      // distance from car for soft-touch detection
    const float FOLLOWUP_TIMEOUT = 2.0f;       // seconds to reward follow-up aerial
    const float MAX_CONT_SHAPE = 6.0f;         // cap for continuous shaping per frame

public:
    virtual void Reset(const GameState& initialState) override {
        lastSoftTouchTime.clear();
        lastSoftTouchPos.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Only meaningful on touches or when state.prev exists for shaping
        float reward = 0.0f;

        // Identify own goal
        Vec myGoal = (player.team == Team::BLUE) ? CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

        // Continuous shaping: reward increases in distance of ball -> our goal (clearing away)
        if (state.prev) {
            float prevDist = (state.prev->ball.pos - myGoal).Length();
            float curDist = (state.ball.pos - myGoal).Length();

            float distDelta = curDist - prevDist; // positive = ball moved away from our goal
            if (distDelta > 0.0f) {
                // scale by closeness to goal (more reward when ball was near)
                float prox = RS_CLAMP(1.0f - (prevDist / SCORING_ZONE_DIST), 0.0f, 1.0f);
                // small shaping reward, capped
                float shape = RS_CLAMP(distDelta * 0.0025f * (0.5f + 0.5f * prox), 0.0f, MAX_CONT_SHAPE);
                reward += shape;
            }
        }

        // Only evaluate touch-based discrete rewards on touch frames
        if (!player.ballTouchedStep) return reward;

        // Ball is high on our defensive backboard?
        float distToBackWall = abs(abs(state.ball.pos.y) - CommonValues::BACK_WALL_Y);
        bool onDefBackboard = (distToBackWall < BACKBOARD_Y_MARGIN && state.ball.pos.z > MIN_HEIGHT);

        // ---------- Strong clear to side/corner ----------
        if (onDefBackboard) {
            // reward strong clears away from backboard toward side corners
            float speed = state.ball.vel.Length();
            if (speed > STRONG_CLEAR_SPEED && state.prev) {
                Vec targetCornerLeft = Vec(-CommonValues::SIDE_WALL_X, myGoal.y + 2000.0f, 0);
                Vec targetCornerRight = Vec(CommonValues::SIDE_WALL_X, myGoal.y + 2000.0f, 0);
                Vec toLeft = (targetCornerLeft - state.ball.pos).Normalized();
                Vec toRight = (targetCornerRight - state.ball.pos).Normalized();
                Vec velDir = state.ball.vel.Normalized();

                float alignL = velDir.Dot(toLeft);
                float alignR = velDir.Dot(toRight);
                float bestAlign = RS_MAX(alignL, alignR);

                // Prefer sideways component too (less straight out the field)
                float sideways = fabsf(velDir.x);

                if (bestAlign > STRONG_CLEAR_ALIGN || sideways > 0.6f) {
                    // Strong clear to side — reward more when it increases distance from goal
                    float prevDist = (state.prev->ball.pos - myGoal).Length();
                    float curDist = (state.ball.pos - myGoal).Length();
                    float distGain = RS_MAX(0.0f, curDist - prevDist);

                    // base for strong clear, scaled by distGain and speed
                    float strongBase = 8.0f + RS_CLAMP(speed / 600.0f, 0.0f, 6.0f);
                    float strongBonus = strongBase * RS_CLAMP(distGain / 1000.0f, 0.0f, 1.0f);

                    reward += RS_CLAMP(strongBonus, 0.0f, 20.0f);
                    return reward; // strong clear is decisive; avoid double-paying soft-touch logic
                }
            }
        }

        // ---------- Soft touch (possession-preserving) ----------
        // Detect a soft touch: touch that tends to slow the ball and leaves it near car.
        if (state.prev) {
            float prevSpeed = state.prev->ball.vel.Length();
            float curSpeed = state.ball.vel.Length();
            float speedDrop = prevSpeed - curSpeed;

            float distToBall = (state.ball.pos - player.pos).Length();

            if (speedDrop > SOFT_TOUCH_SPEED_DROP && distToBall < SOFT_TOUCH_NEAR) {
                // Register soft touch and pay small immediate reward (encourages possession)
                lastSoftTouchTime[player.carId] = (state.prev) ? ((state.prev->deltaTime > 0.0f) ? 0.0f : 0.0f) : 0.0f;
                // store absolute clock-like timestamp using a simple frame-accumulator in map
                // there's no global time here, so use lastSoftTouchTime as seconds relative by accumulating delta
                // We'll store current frame time by reusing a negative marker then increment below. Simpler:
                lastSoftTouchTime[player.carId] = 0.0f; // reset counter stored in map; we'll increment it next frames
                lastSoftTouchPos[player.carId] = state.ball.pos;

                // immediate soft-touch reward
                reward += 6.0f;

                // set an initial small bonus for maintaining possession near goal
                return reward;
            }
        }

        // ---------- Follow-up aerial after a registered soft touch ----------
        // We maintain lastSoftTouchTime as "age" in seconds — increment ages each non-touch call.
        // Because we can't reliably store a global time inside this method without global state,
        // we'll approximate ages by using delta stored on next calls: increment all ages here.
        for (auto& kv : lastSoftTouchTime) {
            // if value == 0.0f that means it was just registered; convert to small epsilon
            if (kv.second == 0.0f) kv.second = state.deltaTime;
            else kv.second += state.deltaTime;
        }

        // now check follow-up: if player touches in air and had a recent soft-touch
        bool isAerialTouch = !player.isOnGround && player.ballTouchedStep;
        if (isAerialTouch) {
            float age = lastSoftTouchTime[player.carId];
            if (age > 0.0f && age <= FOLLOWUP_TIMEOUT) {
                Vec softPos = lastSoftTouchPos[player.carId];
                float followDist = (state.ball.pos - softPos).Length();
                // require follow-up to be reasonably close to the original soft touch area
                if (followDist < 2500.0f) {
                    // reward follow-up aerial (encourages setup → aerial / airdribble)
                    reward += 12.0f;

                    // clear stored soft touch data
                    lastSoftTouchTime[player.carId] = -1.0f;
                    lastSoftTouchPos[player.carId] = Vec(0, 0, 0);
                    return reward;
                }
            }
        }

        // If nothing else, return accumulated shaping + any small immediate touch reward
        return reward;
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

// OutplayReward
// - Rewards a touch that gets the ball past the closest opponent toward the opponent goal.
// - Uses distance-to-goal crossing (previous frame vs current) with margins and minimal speed/alignment checks.
// - Provides simple thread-safe metrics via GetAndResetOutplayMetrics().
class OutplayReward : public Reward {
private:
    const float MIN_BALL_SPEED = 200.0f;   // require some meaningful ball speed after touch
    const float DIST_MARGIN = 50.0f;       // margin to avoid tiny flips being rewarded
    const float BASE_REWARD = 12.0f;       // base reward, scaled by how far past opponent we got

public:
    struct OutplayMetrics {
        int attempts = 0;
        int successes = 0;
        double rewardSum = 0.0;
    };

    inline static std::atomic<int> s_attempts{ 0 };
    inline static std::atomic<int> s_successes{ 0 };
    inline static std::atomic<long long> s_rewardMilli{ 0 };

    static OutplayMetrics GetAndResetOutplayMetrics() {
        OutplayMetrics m;
        m.attempts = s_attempts.exchange(0);
        m.successes = s_successes.exchange(0);
        long long r = s_rewardMilli.exchange(0);
        m.rewardSum = double(r) / 1000.0;
        return m;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // require touch and previous state
        if (!player.ballTouchedStep || !state.prev) return 0.0f;

        // find closest opponent
        const Player* closestOpp = nullptr;
        float minOppDist = FLT_MAX;
        for (const auto& p : state.players) {
            if (p.team == player.team) continue;
            float d = (p.pos - state.ball.pos).Length();
            if (d < minOppDist) {
                minOppDist = d;
                closestOpp = &p;
            }
        }
        if (!closestOpp) return 0.0f;

        s_attempts.fetch_add(1);

        // target = opponent goal (the goal we are attacking)
        Vec targetGoal = (player.team == Team::BLUE) ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        // distances to goal
        float oppDist = (closestOpp->pos - targetGoal).Length();
        float prevBallDist = (state.prev->ball.pos - targetGoal).Length();
        float curBallDist = (state.ball.pos - targetGoal).Length();

        // require meaningful speed and roughly toward-goal motion
        float ballSpeed = state.ball.vel.Length();
        if (ballSpeed < MIN_BALL_SPEED) return 0.0f;

        Vec toGoalNow = (targetGoal - state.ball.pos);
        float toGoalLen = toGoalNow.Length();
        if (toGoalLen < 1e-4f) return 0.0f;
        float align = state.ball.vel.Normalized().Dot(toGoalNow / toGoalLen);
        if (align < 0.12f) return 0.0f; // not meaningfully toward the goal

        // OUTPLAY condition: ball crosses from behind (or near) opponent to past them toward goal
        // strict crossing with margin to avoid farming tiny changes
        if (prevBallDist >= oppDist + DIST_MARGIN && curBallDist <= oppDist - DIST_MARGIN) {
            // scale reward by how much closer to goal we are relative to opponent (clamped)
            float gap = RS_MAX(0.0f, oppDist - curBallDist);
            float scale = RS_CLAMP(gap / RS_MAX(1.0f, oppDist), 0.0f, 1.0f);
            float reward = BASE_REWARD * scale;

            s_successes.fetch_add(1);
            s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));
            return reward;
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
        bool HasFlipOrJump = player.HasFlipOrJump();

        bool prevOnCeiling = wasOnCeiling[player.carId];
        bool prevTouchedCeiling = touchedCeilingWithBallNearby[player.carId];
        bool prevHitBall = hasHitBallFromCeiling[player.carId];
        bool prevUsedFlip = hasUsedFlipFromCeiling[player.carId];

        // STAGE 1: Touch ceiling while ball is nearby AND keep flip
        if (onCeiling && ballNearby && HasFlipOrJump && !prevTouchedCeiling) {
            reward += 5.0f;
            touchedCeilingWithBallNearby[player.carId] = true;
        }

        // STAGE 2: Hit the ball after being on ceiling (much larger reward)
        if (prevTouchedCeiling && player.ballTouchedStep && !player.isOnGround && !prevHitBall) {
            reward += 20.0f;
            hasHitBallFromCeiling[player.carId] = true;
        }

        // STAGE 3: Use flip after being on ceiling (moderate reward)
        if (prevTouchedCeiling && !HasFlipOrJump && prevOnCeiling && !prevUsedFlip) {
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

// Rewards ceiling shots that result in shots on goal
class CeilingShotOnGoalReward : public Reward {
private:
    mutable std::map<uint32_t, bool> wasOnCeiling;
    mutable std::map<uint32_t, bool> hitBallFromCeiling;
    mutable std::map<uint32_t, float> ceilingTouchTime;
    mutable std::map<uint32_t, float> elapsedTime; // Track time manually

public:
    virtual void Reset(const GameState& initialState) override {
        wasOnCeiling.clear();
        hitBallFromCeiling.clear();
        ceilingTouchTime.clear();
        elapsedTime.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
        uint32_t id = player.carId;

        // Update elapsed time
        elapsedTime[id] += dt;

        float reward = 0.0f;

        // Check if on/near ceiling
        bool onCeiling = (player.pos.z > (CommonValues::CEILING_Z - 200.0f) && !player.isOnGround);

        bool prevOnCeiling = wasOnCeiling[id];
        bool prevHitBall = hitBallFromCeiling[id];

        // Track when player was on ceiling
        if (onCeiling && !prevOnCeiling) {
            ceilingTouchTime[id] = elapsedTime[id];
        }

        // Player hit ball after being on ceiling (within 3 seconds)
        if (prevOnCeiling && player.ballTouchedStep && !player.isOnGround) {
            float timeSinceCeiling = elapsedTime[id] - ceilingTouchTime[id];

            if (timeSinceCeiling < 3.0f) { // Within 3 seconds of ceiling touch
                hitBallFromCeiling[id] = true;

                // Determine target goal
                Vec targetGoal = (player.team == Team::BLUE) ?
                    CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

                // Check if ball is heading toward goal
                Vec ballToGoal = (targetGoal - state.ball.pos).Normalized();
                float alignment = state.ball.vel.Normalized().Dot(ballToGoal);

                // Check if shot is on target (heading toward goal)
                if (alignment > 0.6f) {
                    // Base reward for ceiling shot on goal
                    reward += 25.0f;

                    // Bonus for good alignment
                    if (alignment > 0.85f) {
                        reward += 10.0f;
                    }

                    // Bonus for ball speed
                    float ballSpeed = state.ball.vel.Length();
                    if (ballSpeed > 1500.0f) {
                        reward += 5.0f;
                    }
                    if (ballSpeed > 2000.0f) {
                        reward += 5.0f;
                    }

                    // Bonus if flip was used
                    if (!player.HasFlipOrJump() && prevOnCeiling) {
                        reward += 8.0f;
                    }
                }
            }
        }

        // Reset tracking if back on ground
        if (player.isOnGround) {
            hitBallFromCeiling[id] = false;
        }

        wasOnCeiling[id] = onCeiling;

        return reward;
    }
};

// 2025-12-20 | Rev 4.0 | NJH
// - MASSIVELY increased acceleration sensitivity
// - Added directional bonuses (upward + forward acceleration)
// - Rewards "saucy" flicks with high combined acceleration vectors
class FlickReward : public Reward {
private:
    mutable std::map<uint32_t, float> ballOnCarTime;
    float minOnCarTime = 0.25f;

    // Geometry gates
    float minHeightAboveCar = 40.0f;
    float maxHeightAboveCar = 155.0f;
    float maxHorizDist = 120.0f;

    // Acceleration-to-reward mapping (uu/s^2) - MUCH MORE AGGRESSIVE
    float minAccel = 15000.0f;      // Lower threshold (was 20k)
    float goodAccel = 80000.0f;     // Good flick
    float greatAccel = 140000.0f;   // Great flick (was fullAccel 120k)
    float insaneAccel = 200000.0f;  // Insane flick

    float maxFlickReward = 25.0f;   // Increased from 15.0f

public:
    virtual void Reset(const GameState& initialState) override {
        ballOnCarTime.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

        Vec relBallPos = state.ball.pos - player.pos;
        float heightAboveCar = relBallPos.z;
        float horizDist = Vec(relBallPos.x, relBallPos.y, 0).Length();

        bool ballOnCar =
            (heightAboveCar > minHeightAboveCar && heightAboveCar < maxHeightAboveCar && horizDist < maxHorizDist);

        auto& tOnCar = ballOnCarTime[player.carId];
        if (ballOnCar && player.isOnGround) {
            tOnCar += dt;
        }
        else {
            if (!ballOnCar) tOnCar = 0.0f;
        }

        // Flick trigger
        if (player.ballTouchedStep && !ballOnCar && tOnCar >= minOnCarTime) {
            if (!state.prev) { tOnCar = 0.0f; return 0.0f; }

            // Calculate acceleration vector
            Vec dv = state.ball.vel - state.prev->ball.vel;
            float totalAccel = dv.Length() / RS_MAX(dt, 1e-3f);

            // Decompose acceleration into components
            Vec carForward = player.rotMat.forward;
            Vec upVector = Vec(0, 0, 1);

            float forwardAccel = dv.Dot(carForward) / RS_MAX(dt, 1e-3f);
            float upwardAccel = dv.Dot(upVector) / RS_MAX(dt, 1e-3f);

            // Base acceleration reward (progressive scaling)
            float accelReward = 0.0f;
            if (totalAccel >= insaneAccel) {
                accelReward = maxFlickReward; // Full reward for insane flicks
            }
            else if (totalAccel >= greatAccel) {
                float t = (totalAccel - greatAccel) / (insaneAccel - greatAccel);
                accelReward = maxFlickReward * (0.75f + 0.25f * t); // 75-100%
            }
            else if (totalAccel >= goodAccel) {
                float t = (totalAccel - goodAccel) / (greatAccel - goodAccel);
                accelReward = maxFlickReward * (0.45f + 0.30f * t); // 45-75%
            }
            else if (totalAccel >= minAccel) {
                float t = (totalAccel - minAccel) / (goodAccel - minAccel);
                accelReward = maxFlickReward * (0.15f + 0.30f * t); // 15-45%
            }

            // SAUCINESS MULTIPLIERS
            float sauceMultiplier = 1.0f;

            // Forward acceleration bonus (power flicks)
            if (forwardAccel > 50000.0f) {
                float forwardBonus = RS_CLAMP((forwardAccel - 50000.0f) / 80000.0f, 0.0f, 0.4f);
                sauceMultiplier += forwardBonus; // Up to +40%
            }

            // Upward acceleration bonus (pop flicks)
            if (upwardAccel > 40000.0f) {
                float upwardBonus = RS_CLAMP((upwardAccel - 40000.0f) / 60000.0f, 0.0f, 0.35f);
                sauceMultiplier += upwardBonus; // Up to +35%
            }

            // Combined directional bonus (diagonal power)
            if (forwardAccel > 40000.0f && upwardAccel > 30000.0f) {
                sauceMultiplier += 0.3f; // +30% for combined power
            }

            float reward = accelReward * sauceMultiplier;

            // Speed gate (require actual speed increase)
            float prevSpeed = state.prev->ball.vel.Length();
            float curSpeed = state.ball.vel.Length();
            float dSpeed = curSpeed - prevSpeed;
            float speedGate = RS_CLAMP(dSpeed / 600.0f, 0.0f, 1.0f); // Lowered threshold (was 800)

            reward *= (0.25f + 0.75f * speedGate); // Less harsh gating (was 0.35 + 0.65)

            tOnCar = 0.0f;
            return reward;
        }

        // Dribble sustain shaping (minimal)
        if (ballOnCar && player.isOnGround && tOnCar > 0.05f) {
            return 0.12f; // Slightly reduced (was 0.15)
        }

        return 0.0f;
    }
};

// 2025-01-04 | Advanced Flick Reward | NJH
// - Targets high-speed flicks (>2200 UU/s, ideally 2500+)
// - Rewards 45° flicks (diagonal), 90° flicks (sideways), and mixed angles
// - Heavy emphasis on ball speed and directional control
// - Penalizes low-speed flicks to force aggressive play

class AdvancedFlickReward : public Reward {
private:
    mutable std::map<uint32_t, float> ballOnCarTime;
    float minOnCarTime = 0.25f;

    // Geometry gates
    float minHeightAboveCar = 40.0f;
    float maxHeightAboveCar = 155.0f;
    float maxHorizDist = 120.0f;

    // Speed thresholds (UU/s)
    float minFlickSpeed = 1600.0f;      // Minimum viable flick
    float goodFlickSpeed = 2200.0f;     // Good flick target
    float greatFlickSpeed = 2500.0f;    // Great flick
    float insaneFlickSpeed = 3000.0f;   // Insane flick

    float maxFlickReward = 35.0f;       // Increased for high-speed emphasis

    // Angle targets (degrees from forward)
    // 45° = diagonal, 90° = sideways, 135° = back-diagonal
    float angleTolerance = 30.0f;       // ±30° tolerance around target angles

public:
    virtual void Reset(const GameState& initialState) override {
        ballOnCarTime.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

        Vec relBallPos = state.ball.pos - player.pos;
        float heightAboveCar = relBallPos.z;
        float horizDist = Vec(relBallPos.x, relBallPos.y, 0).Length();

        bool ballOnCar =
            (heightAboveCar > minHeightAboveCar && heightAboveCar < maxHeightAboveCar && horizDist < maxHorizDist);

        auto& tOnCar = ballOnCarTime[player.carId];
        if (ballOnCar && player.isOnGround) {
            tOnCar += dt;
        }
        else {
            if (!ballOnCar) tOnCar = 0.0f;
        }

        // Flick trigger
        if (player.ballTouchedStep && !ballOnCar && tOnCar >= minOnCarTime) {
            if (!state.prev) { tOnCar = 0.0f; return 0.0f; }

            // Ball velocity after flick
            Vec ballVel = state.ball.vel;
            float ballSpeed = ballVel.Length();

            // SPEED GATE: Heavily reward fast flicks, penalize slow ones
            if (ballSpeed < minFlickSpeed) {
                tOnCar = 0.0f;
                return -0.5f; // Penalize weak flicks
            }

            float speedReward = 0.0f;
            if (ballSpeed >= insaneFlickSpeed) {
                speedReward = maxFlickReward;
            }
            else if (ballSpeed >= greatFlickSpeed) {
                float t = (ballSpeed - greatFlickSpeed) / (insaneFlickSpeed - greatFlickSpeed);
                speedReward = maxFlickReward * (0.70f + 0.30f * t); // 70-100%
            }
            else if (ballSpeed >= goodFlickSpeed) {
                float t = (ballSpeed - goodFlickSpeed) / (greatFlickSpeed - goodFlickSpeed);
                speedReward = maxFlickReward * (0.45f + 0.25f * t); // 45-70%
            }
            else {
                // minFlickSpeed to goodFlickSpeed
                float t = (ballSpeed - minFlickSpeed) / (goodFlickSpeed - minFlickSpeed);
                speedReward = maxFlickReward * (0.20f + 0.25f * t); // 20-45%
            }

            // DIRECTIONAL ANALYSIS
            Vec carForward = player.rotMat.forward;
            Vec carRight = player.rotMat.right;
            Vec upVector = Vec(0, 0, 1);

            // Normalize ball velocity direction
            Vec ballDir = (ballSpeed > 0.1f) ? ballVel / ballSpeed : carForward;

            // Calculate angles from car orientation
            float forwardComponent = ballDir.Dot(carForward);
            float rightComponent = ballDir.Dot(carRight);
            float upComponent = ballDir.Dot(upVector);

            // Angle from forward (0° = straight, 90° = sideways, 180° = backward)
            float horizontalAngle = std::atan2(rightComponent, forwardComponent) * 180.0f / 3.14159f;
            float verticalAngle = std::atan2(upComponent,
                std::sqrt(forwardComponent * forwardComponent + rightComponent * rightComponent)) * 180.0f / 3.14159f;

            // TARGET ANGLE REWARDS
            // Target angles: 45° (diagonal), 90° (sideways), 135° (back-diagonal)
            float angleBonus = 0.0f;

            auto CheckAngle = [&](float targetAngle) -> float {
                float diff = std::abs(std::abs(horizontalAngle) - std::abs(targetAngle));
                if (diff > 180.0f) diff = 360.0f - diff;

                if (diff < angleTolerance) {
                    return 1.0f - (diff / angleTolerance) * 0.3f; // 100% to 70%
                }
                return 0.0f;
                };

            // Reward for hitting target angles
            float angle45 = CheckAngle(45.0f);      // Diagonal
            float angle90 = CheckAngle(90.0f);      // Sideways
            float angle135 = CheckAngle(135.0f);    // Back-diagonal

            angleBonus = RS_MAX(RS_MAX(angle45, angle90), angle135);

            // Upward angle bonus (pop flicks)
            if (upComponent > 0.3f) {
                float popBonus = RS_CLAMP(upComponent, 0.0f, 1.0f) * 0.25f;
                angleBonus += popBonus; // Up to +25%
            }

            // Apply angle bonus to speed reward
            float directionMultiplier = 1.0f + angleBonus; // 1.0x to 1.25x+

            float reward = speedReward * directionMultiplier;

            // Consistency bonus (if ball is moving cleanly without spin)
            float angVelMag = state.ball.angVel.Length();
            if (angVelMag < 2.0f) {
                reward *= 1.15f; // +15% for clean flicks
            }

            tOnCar = 0.0f;
            return reward;
        }

        // Dribble sustain (minimal)
        if (ballOnCar && player.isOnGround && tOnCar > 0.05f) {
            return 0.05f; // Small reward to encourage ball possession
        }

        return 0.0f;
    }
};

// AirDribbleReward (TUNED: more permissive, height-arc aware)
// - Relaxed directional requirements to allow more varied air dribbles
// - Added height progression bonuses (arc-style air dribbles get multipliers)
// - Distinguishes "air drags" (flat) from "air arcs" (upward trajectory)
// - Softer decay with entry/second-touch bonuses
class AirDribbleReward : public Reward {
private:
    mutable std::map<uint32_t, int> consecutiveAirTouches;
    float minBallHeight = 250.0f;

    // Per-sequence state to limit farming
    mutable std::map<uint32_t, float> seqAccumReward;
    mutable std::map<uint32_t, float> seqTime;
    mutable std::map<uint32_t, int>   seqTouchCount;
    mutable std::map<uint32_t, bool>  seqExhausted;

    // Height tracking for arc detection
    mutable std::map<uint32_t, float> seqStartHeight;
    mutable std::map<uint32_t, float> seqMaxHeight;
    mutable std::map<uint32_t, bool>  heightBonusApplied;

    // ============================================================================
    // TUNABLE CONSTANTS - Adjust these to change reward behavior
    // ============================================================================

    // --- CORE REWARD SCALING ---
    const float PROGRESS_SCALE = 0.010f;
    // What it does: Reward per unit of progress toward goal (uu/s)
    // More aggressive: Increase to 0.015-0.020 for higher per-touch rewards
    // More conservative: Decrease to 0.008-0.010 for lower rewards

    const float PER_TOUCH_MAX = 8.0f;
    // What it does: Maximum reward a single touch can yield
    // More aggressive: Increase to 12.0-15.0 to allow bigger single-touch payouts
    // More conservative: Decrease to 7.0-8.0 to cap individual touches lower

    const float SEQ_REWARD_CAP = 60.0f;
    // What it does: Total maximum reward for an entire air dribble sequence
    // More aggressive: Increase to 60.0-80.0 for longer, more rewarding sequences
    // More conservative: Decrease to 35.0-40.0 to limit total sequence value

    const float MAX_SEQ_DURATION = 12.0f;
    // What it does: Maximum time (seconds) a sequence can last before exhausting
    // More aggressive: Increase to 12.0-15.0 for longer allowable sequences
    // More conservative: Decrease to 7.0-8.0 to force quicker completions

    const int MAX_TOUCHES = 35;
    // What it does: Maximum number of touches before sequence exhausts
    // More aggressive: Increase to 40-50 for more touch allowance
    // More conservative: Decrease to 20-25 to limit farming

    const float MIN_PROGRESS_UU = 15.0f;
    // What it does: Minimum forward progress required to award anything
    // More aggressive: Decrease to 8.0-10.0 to reward smaller progress
    // More conservative: Increase to 15.0-20.0 to require more meaningful progress

    // --- DECAY CURVE ---
    const float DECAY_EXPONENT = 0.45f;
    // What it does: Controls how quickly rewards diminish (0-1 range, lower = softer)
    // More aggressive: Decrease to 0.20-0.25 for even softer early decay
    // More conservative: Increase to 0.45-0.60 for steeper diminishing returns

    // --- BONUS REWARDS ---
    const float ENTRY_BONUS = 0.6f;
    // What it does: One-time bonus when first qualifying touch occurs
    // More aggressive: Increase to 1.0-1.5 to encourage starting sequences
    // More conservative: Decrease to 0.3-0.4 for minimal starting bonus

    const float SECOND_TOUCH_BONUS = 1.0f;
    // What it does: Bonus when second qualifying touch occurs
    // More aggressive: Increase to 1.2-1.5 to encourage continuation
    // More conservative: Decrease to 0.4-0.6 for minimal continuation bonus

    // --- DIRECTIONAL REQUIREMENTS ---
    const float MIN_DIR_MULTIPLIER = 0.5f;
    // What it does: Minimum directional alignment multiplier (was stricter)
    // More aggressive: Decrease to 0.2-0.3 to allow more off-angle dribbles
    // More conservative: Increase to 0.5-0.7 to require better goal alignment

    const float GOOD_DIR_THRESHOLD = 0.55f;
    // What it does: Alignment threshold for "good" direction (dot product)
    // More aggressive: Decrease to 0.3-0.4 to be more lenient
    // More conservative: Increase to 0.6-0.75 to require tighter alignment

    // --- HEIGHT ARC BONUSES ---
    const float HEIGHT_BONUS_THRESHOLD_1 = 350.0f;
    // What it does: Height gain (uu) required for first height bonus multiplier
    // More aggressive: Decrease to 250.0-300.0 for easier threshold
    // More conservative: Increase to 400.0-500.0 for harder threshold

    const float HEIGHT_BONUS_MULTIPLIER_1 = 1.5f;
    // What it does: Reward multiplier when HEIGHT_BONUS_THRESHOLD_1 is reached
    // More aggressive: Increase to 1.75-2.0 for bigger arc rewards
    // More conservative: Decrease to 1.3-1.4 for smaller bonuses

    const float HEIGHT_BONUS_THRESHOLD_2 = 700.0f;
    // What it does: Height gain (uu) required for second (larger) height bonus
    // More aggressive: Decrease to 550.0-650.0 for easier threshold
    // More conservative: Increase to 800.0-1000.0 for harder threshold

    const float HEIGHT_BONUS_MULTIPLIER_2 = 2.0f;
    // What it does: Reward multiplier when HEIGHT_BONUS_THRESHOLD_2 is reached
    // More aggressive: Increase to 2.5-3.0 for massive arc rewards
    // More conservative: Decrease to 1.75-1.85 for smaller bonuses

    // ============================================================================

    const float EPS_REWARD_SKIP = 1e-7f;

    float ComputeDribbleDirectionMultiplier(const Player& player, const GameState& state) const {
        Vec oppGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        Vec ballToGoal = (oppGoal - state.ball.pos).Normalized();
        Vec ballVelDir = state.ball.vel.Normalized();

        float alignment = ballVelDir.Dot(ballToGoal);

        // More lenient directional requirements
        // Allow more off-angle dribbles (air drags, setups, etc.)
        if (alignment < 0.0f) {
            // Moving away from goal - minimal reward
            return MIN_DIR_MULTIPLIER * 0.5f;
        }
        else if (alignment < GOOD_DIR_THRESHOLD) {
            // Somewhat toward goal - moderate reward
            float t = alignment / GOOD_DIR_THRESHOLD;
            return MIN_DIR_MULTIPLIER + (1.0f - MIN_DIR_MULTIPLIER) * t * 0.7f;
        }
        else {
            // Good alignment - full to bonus reward
            float t = (alignment - GOOD_DIR_THRESHOLD) / (1.0f - GOOD_DIR_THRESHOLD);
            return 0.7f + 0.3f * t; // 0.7 to 1.0
        }
    }

    float ComputeHeightBonus(uint32_t id, float currentHeight) {
        // Track maximum height reached
        if (seqMaxHeight.find(id) == seqMaxHeight.end() || currentHeight > seqMaxHeight[id]) {
            seqMaxHeight[id] = currentHeight;
        }

        float startHeight = seqStartHeight[id];
        float maxHeightGain = seqMaxHeight[id] - startHeight;

        // Check if we've reached height thresholds
        if (maxHeightGain >= HEIGHT_BONUS_THRESHOLD_2) {
            return HEIGHT_BONUS_MULTIPLIER_2; // Big arc bonus
        }
        else if (maxHeightGain >= HEIGHT_BONUS_THRESHOLD_1) {
            return HEIGHT_BONUS_MULTIPLIER_1; // Medium arc bonus
        }

        return 1.0f; // No height bonus (flat air drag)
    }

public:
    struct AirDribbleMetrics {
        int attempts = 0;
        int onTarget = 0;
        int arcDribbles = 0;        // NEW: Count of dribbles that achieved height bonus
        double rewardSum = 0.0;
    };

    inline static std::atomic<int> s_attempts{ 0 };
    inline static std::atomic<int> s_onTarget{ 0 };
    inline static std::atomic<int> s_arcDribbles{ 0 };  // NEW
    inline static std::atomic<long long> s_rewardMilli{ 0 };

    static AirDribbleMetrics GetAndResetAirDribbleMetrics() {
        AirDribbleMetrics m;
        m.attempts = s_attempts.exchange(0);
        m.onTarget = s_onTarget.exchange(0);
        m.arcDribbles = s_arcDribbles.exchange(0);  // NEW
        long long r = s_rewardMilli.exchange(0);
        m.rewardSum = double(r) / 1000.0;
        return m;
    }

    virtual void Reset(const GameState& initialState) override {
        consecutiveAirTouches.clear();
        seqAccumReward.clear();
        seqTime.clear();
        seqTouchCount.clear();
        seqExhausted.clear();
        seqStartHeight.clear();
        seqMaxHeight.clear();
        heightBonusApplied.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        uint32_t id = player.carId;
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

        // Reset sequence on grounded / invalid conditions
        if (player.isOnGround || state.ball.pos.z < minBallHeight || player.pos.z >= state.ball.pos.z) {
            consecutiveAirTouches[id] = 0;
            seqAccumReward.erase(id);
            seqTime.erase(id);
            seqTouchCount.erase(id);
            seqExhausted.erase(id);
            seqStartHeight.erase(id);
            seqMaxHeight.erase(id);
            heightBonusApplied.erase(id);
            return 0.0f;
        }

        if (!state.prev)
            return 0.0f;

        if (!player.ballTouchedStep)
            return 0.0f;

        // Initialize sequence state if absent
        if (seqTime.find(id) == seqTime.end()) {
            seqTime[id] = 0.0f;
            seqAccumReward[id] = 0.0f;
            seqTouchCount[id] = 0;
            seqExhausted[id] = false;
            consecutiveAirTouches[id] = 0;
            seqStartHeight[id] = state.ball.pos.z;
            seqMaxHeight[id] = state.ball.pos.z;
            heightBonusApplied[id] = false;
        }

        seqTime[id] += dt;

        if (seqExhausted[id]) {
            return 0.0f;
        }

        if (seqTime[id] > MAX_SEQ_DURATION || seqTouchCount[id] >= MAX_TOUCHES) {
            seqExhausted[id] = true;
            return 0.0f;
        }

        s_attempts.fetch_add(1);

        int& consTouches = consecutiveAirTouches[id];
        int prevTouches = consTouches;
        consTouches++;

        // Compute measurable progress
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        float prevDist = (state.prev->ball.pos - opponentGoal).Length();
        float curDist = (state.ball.pos - opponentGoal).Length();

        float progress = RS_MAX(0.0f, prevDist - curDist);

        if (progress < MIN_PROGRESS_UU) {
            return 0.0f;
        }

        // Base per-touch reward
        float perTouch = progress * PROGRESS_SCALE;
        perTouch = RS_MIN(PER_TOUCH_MAX, perTouch);

        // Softer diminishing returns
        float usedFraction = RS_CLAMP(seqAccumReward[id] / SEQ_REWARD_CAP, 0.0f, 1.0f);
        float remainingFraction = RS_MAX(0.0f, 1.0f - usedFraction);
        float decay = powf(remainingFraction, DECAY_EXPONENT);

        float remainingCap = RS_MAX(0.0f, SEQ_REWARD_CAP - seqAccumReward[id]);
        if (remainingCap <= 0.0f) {
            seqExhausted[id] = true;
            return 0.0f;
        }

        float rewardBeforeDir = RS_MIN(perTouch * decay, remainingCap);

        // MORE LENIENT directional multiplier
        float dirMul = ComputeDribbleDirectionMultiplier(player, state);

        float reward = rewardBeforeDir * dirMul;

        // Entry/second-touch bonuses
        if (prevTouches == 0) {
            reward += ENTRY_BONUS * RS_MIN(1.0f, dirMul);
        }
        else if (prevTouches == 1) {
            reward += SECOND_TOUCH_BONUS * RS_MIN(1.0f, dirMul);
        }

        // HEIGHT ARC BONUS
        float heightBonus = ComputeHeightBonus(id, state.ball.pos.z);
        if (heightBonus > 1.0f && !heightBonusApplied[id]) {
            // First time reaching height threshold - count metric
            s_arcDribbles.fetch_add(1);
            heightBonusApplied[id] = true;
        }

        // Apply height multiplier to reward
        reward *= heightBonus;

        if (reward <= EPS_REWARD_SKIP) {
            return 0.0f;
        }

        // Commit reward
        seqAccumReward[id] += reward;
        seqTouchCount[id] += 1;

        if (seqAccumReward[id] >= SEQ_REWARD_CAP - 1e-3f) {
            seqExhausted[id] = true;
        }

        s_onTarget.fetch_add(1);
        s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));

        return reward;
    }
};

// 2025-12-20 | Rev 1.0 | NJH
// - Rewards recognizing and capitalizing on air dribble opportunities
// - Detects: ball on car, slow bouncing ball, wall roll situations
// - Applies opponent proximity penalty
class AirDribbleOpportunityAwarenessReward : public Reward {
private:
    mutable std::map<uint32_t, bool> opportunityDetected;
    mutable std::map<uint32_t, float> opportunityStartTime;
    mutable std::map<uint32_t, float> elapsedTime; // Track time manually
    mutable std::map<uint32_t, bool> airDribbleStarted;

    // Opportunity detection thresholds
    float slowBallSpeed = 600.0f;
    float bouncingUpwardVel = 200.0f;
    float wallProximity = 200.0f;
    float ballOnCarHeight = 40.0f;
    float ballOnCarMaxHeight = 200.0f;

    // Air dribble detection
    float airDribbleMinHeight = 150.0f;
    float airDribbleMaxDist = 250.0f;

    // Opponent proximity penalty
    float opponentThreatDist = 800.0f;
    float proximityPenalty = 0.25f;

    bool IsOpportunity(const Player& player, const GameState& state) const {
        Vec relBallPos = state.ball.pos - player.pos;
        float ballSpeed = state.ball.vel.Length();
        float ballHeight = state.ball.pos.z;

        // Opportunity 1: Ball on car
        float heightAboveCar = relBallPos.z;
        float horizDist = Vec(relBallPos.x, relBallPos.y, 0).Length();
        bool ballOnCar = (heightAboveCar > ballOnCarHeight &&
            heightAboveCar < ballOnCarMaxHeight &&
            horizDist < 150.0f);

        // Opportunity 2: Slow bouncing ball
        bool slowBouncing = (ballSpeed < slowBallSpeed &&
            state.ball.vel.z > bouncingUpwardVel &&
            ballHeight < 400.0f);

        // Opportunity 3: Ball rolling up wall
        bool nearWall = (fabsf(state.ball.pos.x) > (CommonValues::SIDE_WALL_X - wallProximity) ||
            fabsf(state.ball.pos.y) > (CommonValues::BACK_WALL_Y - wallProximity));

        bool wallRoll = (nearWall &&
            state.ball.vel.z > 150.0f &&
            ballSpeed < 900.0f &&
            ballHeight > 100.0f && ballHeight < 600.0f);

        return (ballOnCar || slowBouncing || wallRoll);
    }

    bool IsAirDribbling(const Player& player, const GameState& state) const {
        if (player.isOnGround) return false;

        float ballHeight = state.ball.pos.z;
        if (ballHeight < airDribbleMinHeight) return false;

        Vec relBallPos = state.ball.pos - player.pos;
        float distToBall = relBallPos.Length();

        return (distToBall < airDribbleMaxDist);
    }

    float GetOpponentProximityFactor(const Player& player, const GameState& state) const {
        float closestOpponentDist = 99999.0f;

        for (int i = 0; i < state.players.size(); i++) {
            const Player& other = state.players[i];
            if (other.team == player.team) continue;

            float dist = (other.pos - player.pos).Length();
            if (dist < closestOpponentDist) {
                closestOpponentDist = dist;
            }
        }

        // If opponent is close, reduce reward
        if (closestOpponentDist < opponentThreatDist) {
            float threatFactor = closestOpponentDist / opponentThreatDist;
            return (1.0f - proximityPenalty) + (proximityPenalty * threatFactor);
        }

        return 1.0f;
    }

public:
    virtual void Reset(const GameState& initialState) override {
        opportunityDetected.clear();
        opportunityStartTime.clear();
        elapsedTime.clear();
        airDribbleStarted.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
        uint32_t id = player.carId;

        // Update elapsed time
        elapsedTime[id] += dt;

        bool isOpportunityNow = IsOpportunity(player, state);
        bool wasOpportunity = opportunityDetected[id];
        bool hasStartedAirDribble = airDribbleStarted[id];

        float reward = 0.0f;

        // Detect new opportunity
        if (isOpportunityNow && !wasOpportunity) {
            opportunityDetected[id] = true;
            opportunityStartTime[id] = elapsedTime[id];
        }

        // Check if player capitalized on opportunity
        if (wasOpportunity && IsAirDribbling(player, state) && !hasStartedAirDribble) {
            airDribbleStarted[id] = true;

            // Time window bonus (faster recognition = better)
            float timeSinceOpportunity = elapsedTime[id] - opportunityStartTime[id];
            float timeBonus = RS_CLAMP(1.0f - (timeSinceOpportunity / 2.0f), 0.5f, 1.0f);

            // Base reward for recognizing and starting air dribble
            float baseReward = 18.0f * timeBonus;

            // Apply opponent proximity penalty
            float proximityFactor = GetOpponentProximityFactor(player, state);
            reward = baseReward * proximityFactor;

            // Bonus if ball was touched during transition
            if (player.ballTouchedStep) {
                reward += 5.0f * proximityFactor;
            }
        }

        // Reset tracking if opportunity lost or air dribble ended
        if (!isOpportunityNow && !IsAirDribbling(player, state)) {
            opportunityDetected[id] = false;
            airDribbleStarted[id] = false;
        }

        return reward;
    }
};


// ============================================================================
// SpeedFlipReward
// Detects a forward-going speed flip with:
//  - Diagonal flip input at start (forward + yaw)
//  - Flip cancel via opposite pitch within 0.3s
//  - Air roll or powerslide used around landing to land cleanly
//  - Car remains facing roughly same direction and gains forward speed
// 2025-12-09 - Rev 3 (NJH)
// ============================================================================
class SpeedFlipReward : public Reward {
private:
    struct FlipState {
        Vec  startForward;
        Vec  startVel;
        bool wasFlipping = false;
        bool startedForward = false;
        bool diagonalInputAtStart = false;
        bool cancelDetected = false;
        bool landingCorrection = false;
        float cancelWindow = 0.0f;
        float landingWindow = 0.0f;
        bool wasOnGround = true;
    };

    mutable std::map<uint32_t, FlipState> flipStates;

public:
    virtual void Reset(const GameState& initialState) override {
        flipStates.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
        auto& fs = flipStates[player.carId];

        bool wasFlipping = fs.wasFlipping;
        fs.wasFlipping = player.isFlipping;

        // Track ground / landing for correction detection
        bool wasOnGround = fs.wasOnGround;
        fs.wasOnGround = player.isOnGround;

        // Update windows
        if (fs.cancelWindow > 0.0f)
            fs.cancelWindow = RS_MAX(0.0f, fs.cancelWindow - dt);
        if (fs.landingWindow > 0.0f)
            fs.landingWindow = RS_MAX(0.0f, fs.landingWindow - dt);

        // Detect flip cancel: opposite pitch within cancel window
        if (fs.cancelWindow > 0.0f) {
            // Initial flip is forward (pitch > 0), so cancel is negative pitch
            if (player.prevAction.pitch < -0.5f) {
                fs.cancelDetected = true;
            }
        }

        // Detect landing correction: air roll or powerslide around landing
        if (!wasOnGround && player.isOnGround && fs.landingWindow > 0.0f) {
            bool usingCorrection =
                (fabsf(player.prevAction.roll) > 0.3f) || (player.prevAction.handbrake > 0.5f);
            if (usingCorrection) {
                fs.landingCorrection = true;
            }
        }

        // Flip just started
        if (!wasFlipping && player.isFlipping) {
            fs.startForward = player.rotMat.forward;
            fs.startVel = player.vel;

            float startSpeed = player.vel.Length();
            fs.startedForward = false;
            if (startSpeed > 300.0f) {
                Vec velDir = player.vel / startSpeed;
                float velAlign = velDir.Dot(player.rotMat.forward);
                if (velAlign > 0.5f) {
                    fs.startedForward = true;
                }
            }

            // Diagonal speed-flip input: forward pitch + sideways yaw
            fs.diagonalInputAtStart =
                (player.prevAction.pitch > 0.4f) &&
                (fabsf(player.prevAction.yaw) > 0.3f);

            // Require both forward motion and diagonal input
            if (fs.startedForward && fs.diagonalInputAtStart) {
                fs.cancelDetected = false;
                fs.landingCorrection = false;
                fs.cancelWindow = 0.3f;   // window to see flip cancel
                fs.landingWindow = 0.6f;  // landing correction around touch-down
            }
            else {
                fs.cancelWindow = 0.0f;
                fs.landingWindow = 0.0f;
            }

            return 0.0f;
        }

        // Flip just ended
        if (wasFlipping && !player.isFlipping) {
            // Only reward if we started with a valid forward diagonal flip
            if (!fs.startedForward || !fs.diagonalInputAtStart)
                return 0.0f;

            // Need a flip cancel and landing correction
            if (!fs.cancelDetected || !fs.landingCorrection)
                return 0.0f;

            Vec startForward = fs.startForward;
            Vec endForward = player.rotMat.forward;

            // Must remain roughly facing same direction (not a half-flip)
            float forwardAlignment = startForward.Dot(endForward);
            if (forwardAlignment < 0.7f)
                return 0.0f;

            float startSpeed = fs.startVel.Length();
            float endSpeed = player.vel.Length();
            if (endSpeed < 800.0f)
                return 0.0f;

            Vec endVelDir = (endSpeed > 1e-3f) ? (player.vel / endSpeed) : endForward;
            float endVelAlign = endVelDir.Dot(endForward);

            // Must be moving strongly forward after flip
            if (endVelAlign < 0.7f)
                return 0.0f;

            // Require meaningful speed gain
            if (endSpeed <= startSpeed + 400.0f)
                return 0.0f;

            float gain = RS_CLAMP((endSpeed - startSpeed) / 800.0f, 0.0f, 1.0f);
            return 10.0f * gain;
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
// ShotOnGoalReward - OPTIMIZED FOR AGGRESSIVE SHOOTING
// 2025-12-23 | Rev 2.0 | Heavily rewards shots on goal, especially open nets
// ============================================================================
class ShotOnGoalReward : public Reward {
private:
    float baseReward = 15.0f;  // Increased from 10.0f

    bool scaleBySpeed = true;
    float slowKph = 40.0f;   // Lowered threshold - reward slower shots too
    float fastKph = 100.0f;  // Lowered - don't require supersonic

public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Only pay on shot event
        if (!player.eventState.shot) return 0.0f;

        // Must be going toward opponent goal
        if (!RLGC::RewardMath::BallIntersectsOpponentGoalMouth(player, state, 2.0f, true, true)) {
            return 0.0f;
        }

        // Speed scaling
        float speedScale = 1.0f;
        if (scaleBySpeed) {
            const float kph = state.ball.vel.Length() * 0.036f;
            if (kph <= slowKph) {
                speedScale = 0.6f;  // Still give decent reward for slower shots
            }
            else if (kph >= fastKph) {
                speedScale = 1.0f;
            }
            else {
                const float t = (kph - slowKph) / (fastKph - slowKph);
                speedScale = 0.6f + 0.4f * t;
            }
        }

        return baseReward * speedScale;
    }
};

// ============================================================================
// LongShotReward - ELITE TIER
// Rewards shooting on goal from distance with high velocity
// Emphasizes accurate aiming and powerful shots from far away
// 2025-01-04 | Rev 1.0 | NJH
// ============================================================================

class LongShotReward : public Reward {
private:
    struct LongShotConfig {
        // Distance thresholds (UU)
        float minShotDistance = 2000.0f;           // Minimum distance to count as "long shot"
        float goodShotDistance = 3500.0f;          // Good long shot
        float greatShotDistance = 5000.0f;         // Great long shot
        float maxShotDistance = 8000.0f;           // Max reasonable distance

        // Velocity thresholds (UU/s)
        float minBallVelocity = 1000.0f;           // Minimum shot speed
        float goodBallVelocity = 1500.0f;          // Good shot speed
        float greatBallVelocity = 2000.0f;         // Great shot speed
        float insaneBallVelocity = 2500.0f;        // Insane shot speed

        // Accuracy thresholds
        float maxAngleToGoal = 0.2f;            // ±30 degrees to center of goal
        float goodAngleToGoal = 0.1f;           // ±15 degrees (tighter)

        // Goal dimensions (Rocket League)
        float goalWidth = 1792.0f;                 // Total goal width
        float goalHeight = 642.775f;               // Goal height
        float goalDepth = 250.0f;                  // Goal depth

        // Shot detection window
        float ballContactDistance = 150.0f;        // Distance at which ball is "contacted"
        float shotDetectionTime = 0.5f;            // Time window to detect shot after contact

        // Ball trajectory check
        float minHeightGain = 50.0f;               // Ball must be traveling upward (lob) or straight
        float maxHeightForPowerShot = 300.0f;      // Height constraint for power shots
    };

    enum ShotPhase {
        PHASE_IDLE,
        PHASE_APPROACH,     // Getting into position
        PHASE_CONTACT,      // About to hit ball
        PHASE_SHOOTING      // Ball traveling toward goal
    };

    mutable std::map<uint32_t, ShotPhase> currentPhase;
    mutable std::map<uint32_t, float> phaseTimer;
    mutable std::map<uint32_t, Vec> ballVelAtContact;
    mutable std::map<uint32_t, Vec> ballPosAtContact;

    LongShotConfig config;

    Vec GetGoalPosition(Team team) const {
        // Blue goal at -Y, Orange goal at +Y
        if (team == Team::BLUE) {
            return Vec(0.0f, -5120.0f, 321.3875f);  // Goal center
        }
        else {
            return Vec(0.0f, 5120.0f, 321.3875f);
        }
    }

    float ComputeAngleToGoal(const Vec& ballPos, const Vec& goalPos) const {
        Vec toGoal = goalPos - ballPos;
        Vec ballDir2D = Vec(toGoal.x, toGoal.y, 0).Normalized();

        if (toGoal.Length() < 1.0f) return 0.0f;

        return acos(RS_CLAMP(ballDir2D.Dot(Vec(0, 1, 0)), -1.0f, 1.0f));
    }

    float ComputeDistanceToGoal(const Vec& ballPos, const Vec& goalPos) const {
        return ballPos.Dist(goalPos);
    }

    bool IsBallTravelingTowardGoal(const Vec& ballVel, const Vec& ballPos, const Vec& goalPos) const {
        Vec toGoal = (goalPos - ballPos).Normalized();
        Vec ballDir = ballVel.Normalized();

        if (ballVel.Length() < 100.0f) return false;

        float dotProduct = ballDir.Dot(toGoal);
        return dotProduct > 0.2f;  // At least somewhat toward goal
    }

    bool IsInGoalBox(const Vec& ballPos, const Vec& goalPos, Team team) const {
        // Check if ball is approaching goal area (box in front of goal)
        float dist = ballPos.Dist(goalPos);

        if (team == Team::BLUE) {
            // Blue attacking goal at -Y
            return ballPos.y < -4500.0f && dist < 3000.0f;
        }
        else {
            // Orange attacking goal at +Y
            return ballPos.y > 4500.0f && dist < 3000.0f;
        }
    }

public:
    LongShotReward() : config() {}

    explicit LongShotReward(const LongShotConfig& cfg) : config(cfg) {}

    virtual void Reset(const GameState& initialState) override {
        currentPhase.clear();
        phaseTimer.clear();
        ballVelAtContact.clear();
        ballPosAtContact.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        Vec goalPos = GetGoalPosition(player.team);
        float distToGoal = ComputeDistanceToGoal(state.ball.pos, goalPos);
        float ballSpeed = state.ball.vel.Length();

        // Initialize state
        if (currentPhase.find(player.carId) == currentPhase.end()) {
            currentPhase[player.carId] = PHASE_IDLE;
            phaseTimer[player.carId] = 0.0f;
            ballVelAtContact[player.carId] = state.ball.vel;
            ballPosAtContact[player.carId] = state.ball.pos;
            return 0.0f;
        }

        ShotPhase& phase = currentPhase[player.carId];
        float& timer = phaseTimer[player.carId];
        timer += CommonValues::TICK_TIME;

        float carBallDist = player.pos.Dist(state.ball.pos);
        float angleToGoal = ComputeAngleToGoal(state.ball.pos, goalPos);
        bool travelingToGoal = IsBallTravelingTowardGoal(state.ball.vel, state.ball.pos, goalPos);

        float reward = 0.0f;

        // =====================================================================
        // PHASE 1: APPROACH - Get into shooting position from far away
        // =====================================================================
        if (phase == PHASE_IDLE || phase == PHASE_APPROACH) {
            if (distToGoal > config.minShotDistance &&
                distToGoal < config.maxShotDistance &&
                carBallDist < 300.0f) {

                // Reward being in position far from goal
                float distQuality = 0.0f;
                if (distToGoal >= config.greatShotDistance) {
                    distQuality = 1.0f;
                }
                else if (distToGoal >= config.goodShotDistance) {
                    float t = (distToGoal - config.goodShotDistance) / (config.greatShotDistance - config.goodShotDistance);
                    distQuality = 0.6f + 0.4f * t;
                }
                else {
                    float t = (distToGoal - config.minShotDistance) / (config.goodShotDistance - config.minShotDistance);
                    distQuality = 0.3f + 0.3f * t;
                }

                reward = distQuality * 0.4f;

                phase = PHASE_APPROACH;
                timer = 0.0f;
                return reward;
            }
        }

        // =====================================================================
        // PHASE 2: CONTACT - About to shoot the ball
        // =====================================================================
        if (phase == PHASE_APPROACH) {
            if (player.ballTouchedStep && carBallDist < config.ballContactDistance) {
                // Ball was just hit
                ballVelAtContact[player.carId] = state.ball.vel;
                ballPosAtContact[player.carId] = state.ball.pos;

                phase = PHASE_CONTACT;
                timer = 0.0f;
                return 0.5f;  // Small reward for making contact
            }
        }

        // =====================================================================
        // PHASE 3: SHOOTING - Ball traveling toward goal with velocity
        // =====================================================================
        if (phase == PHASE_CONTACT) {
            // Check if still in detection window
            if (timer > config.shotDetectionTime) {
                phase = PHASE_IDLE;
                timer = 0.0f;
                return 0.0f;
            }

            // Ball must be traveling toward goal
            if (!travelingToGoal) {
                phase = PHASE_IDLE;
                timer = 0.0f;
                return -0.5f;  // Penalize shots away from goal
            }

            // ========== VELOCITY REWARD ==========
            float velocityReward = 0.0f;
            if (ballSpeed >= config.insaneBallVelocity) {
                velocityReward = 1.0f;
            }
            else if (ballSpeed >= config.greatBallVelocity) {
                float t = (ballSpeed - config.greatBallVelocity) / (config.insaneBallVelocity - config.greatBallVelocity);
                velocityReward = 0.7f + 0.3f * t;
            }
            else if (ballSpeed >= config.goodBallVelocity) {
                float t = (ballSpeed - config.goodBallVelocity) / (config.greatBallVelocity - config.goodBallVelocity);
                velocityReward = 0.45f + 0.25f * t;
            }
            else if (ballSpeed >= config.minBallVelocity) {
                float t = (ballSpeed - config.minBallVelocity) / (config.goodBallVelocity - config.minBallVelocity);
                velocityReward = 0.2f + 0.25f * t;
            }

            // ========== ACCURACY REWARD ==========
            float accuracyReward = 0.0f;
            if (angleToGoal < config.goodAngleToGoal) {
                // Very accurate (±15°)
                float accuracy = 1.0f - (angleToGoal / config.goodAngleToGoal);
                accuracyReward = accuracy * 0.8f;
            }
            else if (angleToGoal < config.maxAngleToGoal) {
                // Decent accuracy (±30°)
                float accuracy = 1.0f - (angleToGoal / config.maxAngleToGoal);
                accuracyReward = accuracy * 0.4f;
            }

            // ========== DISTANCE BONUS ==========
            float distanceBonus = 0.0f;
            if (distToGoal >= config.greatShotDistance) {
                distanceBonus = 0.25f;
            }
            else if (distToGoal >= config.goodShotDistance) {
                float t = (distToGoal - config.goodShotDistance) / (config.greatShotDistance - config.goodShotDistance);
                distanceBonus = 0.1f + 0.15f * t;
            }

            // ========== TRAJECTORY BONUS ==========
            float trajectoryBonus = 0.0f;
            if (state.ball.vel.z > 0) {
                // Ball going upward (lob shot) - harder to defend
                trajectoryBonus = RS_CLAMP(state.ball.vel.z / 1000.0f, 0.0f, 0.2f);
            }
            else if (state.ball.vel.z > -500.0f) {
                // Ball going straight - good power shot
                trajectoryBonus = 0.15f;
            }

            // ========== COMBINE REWARDS ==========
            reward = (velocityReward * 0.5f +
                accuracyReward * 0.3f +
                distanceBonus * 0.15f +
                trajectoryBonus * 0.05f) * 2.0f;

            phase = PHASE_SHOOTING;
            return reward;
        }

        return 0.0f;
    }

    // Getters/Setters for tuning
    void SetMinShotDistance(float dist) { config.minShotDistance = dist; }
    void SetGoodShotDistance(float dist) { config.goodShotDistance = dist; }
    void SetGreatShotDistance(float dist) { config.greatShotDistance = dist; }
    void SetMinBallVelocity(float vel) { config.minBallVelocity = vel; }
    void SetGoodBallVelocity(float vel) { config.goodBallVelocity = vel; }
    void SetGreatBallVelocity(float vel) { config.greatBallVelocity = vel; }
    void SetAngleTolerance(float goodAngle, float maxAngle) {
        config.goodAngleToGoal = goodAngle;
        config.maxAngleToGoal = maxAngle;
    }
};

// ============================================================================
// OpenNetShotReward - UPDATED
// Massively rewards shooting when net is open (no defenders in way)
// Now also checks positioning: player and ball should be closer to opponent goal
// Adds bonus for opponents being out of position
// 2025-01-10 | Rev 2.0 (NJH)
// ============================================================================
class OpenNetShotReward : public Reward {
private:
    float baseReward = 25.0f;  // HUGE reward for open net shots
    float defenderCheckRadius = 2500.0f;  // How far to check for defenders
    float outOfPositionBonus = 15.0f;  // Extra reward when opponent is out of position
    float velocityToGoalBonus = 10.0f;  // Extra reward for ball velocity toward goal when opponent out of position

    // Check if player is closer to opponent goal than all opponents
    bool IsPlayerCloserToGoalThanOpponents(const Player& player, const GameState& state, const Vec& oppGoal) const {
        float playerDistToGoal = (player.pos - oppGoal).Length();

        for (const Player& p : state.players) {
            if (p.team == player.team) continue;  // Skip teammates

            float oppDistToGoal = (p.pos - oppGoal).Length();

            // If any opponent is closer to goal than us, we're not in ideal position
            if (oppDistToGoal < playerDistToGoal - 300.0f) {  // 300 unit margin
                return false;
            }
        }

        return true;
    }

    // Check if ball is closer to opponent goal than all opponents
    bool IsBallCloserToGoalThanOpponents(const GameState& state, const Vec& oppGoal, Team playerTeam) const {
        float ballDistToGoal = (state.ball.pos - oppGoal).Length();

        for (const Player& p : state.players) {
            if (p.team == playerTeam) continue;  // Skip our team

            float oppDistToGoal = (p.pos - oppGoal).Length();

            // If any opponent is closer to goal than ball, they're defending well
            if (oppDistToGoal < ballDistToGoal - 300.0f) {  // 300 unit margin
                return false;
            }
        }

        return true;
    }

    // Check if opponent is out of position (we're closer to both goal and ball)
    bool IsOpponentOutOfPosition(const Player& player, const GameState& state, const Vec& oppGoal) const {
        float playerDistToBall = (player.pos - state.ball.pos).Length();
        float playerDistToGoal = (player.pos - oppGoal).Length();

        for (const Player& p : state.players) {
            if (p.team == player.team) continue;  // Skip teammates

            float oppDistToBall = (p.pos - state.ball.pos).Length();
            float oppDistToGoal = (p.pos - oppGoal).Length();

            // Opponent is in good position if they're closer to ball OR closer to goal
            if (oppDistToBall < playerDistToBall - 200.0f || oppDistToGoal < playerDistToGoal - 300.0f) {
                return false;
            }
        }

        return true;  // We're closer to both ball and goal than all opponents
    }

    bool IsNetOpen(const Player& player, const GameState& state) const {
        Vec oppGoal = (player.team == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_BACK
            : CommonValues::BLUE_GOAL_BACK;

        Vec ballToGoal = oppGoal - state.ball.pos;
        float distToGoal = ballToGoal.Length();
        if (distToGoal < 1e-3f) return true;

        Vec dirToGoal = ballToGoal / distToGoal;

        // Check if any opponent is between ball and goal
        for (const Player& p : state.players) {
            if (p.team == player.team) continue;  // Skip teammates
            if (p.carId == player.carId) continue;

            Vec ballToPlayer = p.pos - state.ball.pos;
            float distToPlayer = ballToPlayer.Length();

            // Is this player in the shooting lane?
            if (distToPlayer < distToGoal) {
                float proj = ballToPlayer.Dot(dirToGoal);

                // Player is ahead of ball toward goal
                if (proj > 0.0f && proj < distToGoal) {
                    Vec perpendicular = ballToPlayer - dirToGoal * proj;
                    float perpDist = perpendicular.Length();

                    // Defender is blocking the shot
                    if (perpDist < defenderCheckRadius) {
                        return false;
                    }
                }
            }
        }

        return true;  // No defenders blocking
    }

public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Only on shot events
        if (!player.eventState.shot) return 0.0f;

        // Must be on target
        if (!RLGC::RewardMath::BallIntersectsOpponentGoalMouth(player, state, 2.0f, true, true)) {
            return 0.0f;
        }

        Vec oppGoal = (player.team == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_BACK
            : CommonValues::BLUE_GOAL_BACK;

        // NEW: Check positioning - both player and ball should be closer to goal than opponents
        bool playerCloserToGoal = IsPlayerCloserToGoalThanOpponents(player, state, oppGoal);
        bool ballCloserToGoal = IsBallCloserToGoalThanOpponents(state, oppGoal, player.team);

        // Require at least one positioning advantage
        if (!playerCloserToGoal && !ballCloserToGoal) {
            return 0.0f;  // Not in advantageous position
        }

        // Check if net is open
        if (!IsNetOpen(player, state)) {
            return 0.0f;
        }

        float totalReward = baseReward;
        float distToGoal = (state.ball.pos - oppGoal).Length();

        // Bonus for close range open nets
        float distanceBonus = 1.0f;
        if (distToGoal < 3000.0f) {
            distanceBonus = 1.0f + 0.5f * (1.0f - distToGoal / 3000.0f);  // Up to 1.5x
        }

        // Speed bonus
        float kph = state.ball.vel.Length() * 0.036f;
        float speedBonus = RS_CLAMP(kph / 80.0f, 0.5f, 1.5f);

        totalReward *= distanceBonus * speedBonus;

        // NEW: Out of position bonus - opponent is further from both goal and ball
        if (IsOpponentOutOfPosition(player, state, oppGoal)) {
            totalReward += outOfPositionBonus;

            // Additional bonus for ball velocity toward goal when opponent is out of position
            Vec dirToGoal = (oppGoal - state.ball.pos).Normalized();
            float velTowardGoal = RS_MAX(0.0f, state.ball.vel.Dot(dirToGoal));
            float velBonus = RS_CLAMP(velTowardGoal / 2300.0f, 0.0f, 1.0f);  // Normalize by max speed

            totalReward += velocityToGoalBonus * velBonus;
        }

        return totalReward;
    }
};

// ============================================================================
// ShotQualityReward - NEW
// Rewards shot quality: placement, power, angle
// Encourages taking good shots over dribbling
// 2025-12-23 | Rev 1.1 - Fixed with correct CommonValues references
// ============================================================================
class ShotQualityReward : public Reward {
private:
    float baseReward = 8.0f;

    float GetPlacementQuality(const Player& player, const GameState& state) const {
        Vec oppGoal = (player.team == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_BACK
            : CommonValues::BLUE_GOAL_BACK;

        // Goal corners (harder to save)
        // Using GOAL_WIDTH_FROM_CENTER (half-width from center)
        Vec topLeft = oppGoal + Vec(-CommonValues::GOAL_WIDTH_FROM_CENTER, 0.0f, CommonValues::GOAL_HEIGHT * 0.8f);
        Vec topRight = oppGoal + Vec(CommonValues::GOAL_WIDTH_FROM_CENTER, 0.0f, CommonValues::GOAL_HEIGHT * 0.8f);
        Vec bottomLeft = oppGoal + Vec(-CommonValues::GOAL_WIDTH_FROM_CENTER, 0.0f, CommonValues::GOAL_HEIGHT * 0.2f);
        Vec bottomRight = oppGoal + Vec(CommonValues::GOAL_WIDTH_FROM_CENTER, 0.0f, CommonValues::GOAL_HEIGHT * 0.2f);

        // Project where ball will hit goal plane
        Vec ballVelNorm = state.ball.vel.Normalized();
        float t = fabsf((oppGoal - state.ball.pos).Dot(Vec(0.0f, (player.team == Team::BLUE) ? 1.0f : -1.0f, 0.0f))
            / state.ball.vel.Dot(Vec(0.0f, (player.team == Team::BLUE) ? 1.0f : -1.0f, 0.0f)));
        Vec hitPoint = state.ball.pos + ballVelNorm * (state.ball.vel.Length() * t);

        // Distance to nearest corner
        float minCornerDist = RS_MIN(
            RS_MIN((hitPoint - topLeft).Length(), (hitPoint - topRight).Length()),
            RS_MIN((hitPoint - bottomLeft).Length(), (hitPoint - bottomRight).Length())
        );

        // Closer to corners = better
        float maxCornerDist = CommonValues::GOAL_TOTAL_WIDTH;  // Use full width for normalization
        return RS_CLAMP(1.0f - (minCornerDist / maxCornerDist), 0.3f, 1.0f);
    }

public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.eventState.shot) return 0.0f;
        if (!RLGC::RewardMath::BallIntersectsOpponentGoalMouth(player, state, 2.0f, true, true)) {
            return 0.0f;
        }

        // Power component
        float kph = state.ball.vel.Length() * 0.036f;
        float powerQuality = RS_CLAMP(kph / 100.0f, 0.3f, 1.2f);

        // Placement component
        float placementQuality = GetPlacementQuality(player, state);

        // Angle component - reward shots from difficult angles
        Vec oppGoal = (player.team == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_BACK
            : CommonValues::BLUE_GOAL_BACK;
        Vec ballToGoal = (oppGoal - state.ball.pos).Normalized();
        Vec carToGoal = (oppGoal - player.pos).Normalized();
        float angleAlignment = carToGoal.Dot(ballToGoal);
        float angleQuality = 0.7f + 0.3f * RS_CLAMP(angleAlignment, 0.0f, 1.0f);

        return baseReward * powerQuality * placementQuality * angleQuality;
    }
};

// ============================================================================
// AntiDribblingReward - NEW
// Penalizes slow ball possession near opponent goal
// Encourages shooting instead of dribbling when close to goal
// 2025-12-23 | Rev 1.0
// ============================================================================
class AntiDribblingReward : public Reward {
private:
    float penaltyPerSecond = -0.3f;  // Small continuous penalty
    float minDistToGoal = 4500.0f;   // Only penalize when close to goal
    float maxBallSpeed = 800.0f;     // Ball must be moving slowly
    float possessionRadius = 350.0f; // How close is "possession"

    mutable std::map<uint32_t, float> possessionTime;

public:
    virtual void Reset(const GameState& initialState) override {
        possessionTime.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        Vec oppGoal = (player.team == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_BACK
            : CommonValues::BLUE_GOAL_BACK;

        float distToGoal = (state.ball.pos - oppGoal).Length();
        float ballSpeed = state.ball.vel.Length();
        float distToBall = (player.pos - state.ball.pos).Length();

        // Are we in "dribbling" state near goal?
        bool isDribblingNearGoal = (distToGoal < minDistToGoal)
            && (ballSpeed < maxBallSpeed)
            && (distToBall < possessionRadius);

        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

        if (isDribblingNearGoal) {
            possessionTime[player.carId] += dt;

            // Penalty scales with time spent dribbling
            float timePenalty = RS_MIN(possessionTime[player.carId], 2.0f);  // Cap at 2 seconds
            return penaltyPerSecond * timePenalty * dt;
        }
        else {
            possessionTime[player.carId] = 0.0f;
        }

        return 0.0f;
    }
};

// ============================================================================
// CHAMPION TIER REWARDS
// ============================================================================

// Rewards exploration of advanced flicks (anything other than front/diagonal flips)
class FlickExplorationReward : public Reward {
private:
    mutable std::map<uint32_t, bool> ballWasOnCar;
    mutable std::map<uint32_t, Vec> lastCarForward;
    mutable std::map<uint32_t, Vec> lastCarUp;
    mutable std::map<uint32_t, float> lastCarPitch;
    mutable std::map<uint32_t, float> lastCarYaw;

public:
    virtual void Reset(const GameState& initialState) override {
        ballWasOnCar.clear();
        lastCarForward.clear();
        lastCarUp.clear();
        lastCarPitch.clear();
        lastCarYaw.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        Vec relBallPos = state.ball.pos - player.pos;
        float heightAboveCar = relBallPos.z;
        float horizDist = Vec(relBallPos.x, relBallPos.y, 0).Length();

        // Ball is on car
        bool ballOnCar = (heightAboveCar > 30.0f && heightAboveCar < 150.0f && horizDist < 120.0f);
        bool wasPrev = ballWasOnCar[player.carId];
        ballWasOnCar[player.carId] = ballOnCar;

        // Flick detected: ball was on car, player touched ball, ball no longer on car
        if (wasPrev && player.ballTouchedStep && !ballOnCar) {
            Vec prevForward = lastCarForward[player.carId];
            Vec prevUp = lastCarUp[player.carId];
            Vec curForward = player.rotMat.forward;
            Vec curUp = player.rotMat.up;

            float forwardAlignment = prevForward.Dot(curForward);
            float upAlignment = prevUp.Dot(curUp);

            // Calculate angular change
            float pitchChange = std::abs(player.angVel.x - lastCarPitch[player.carId]);
            float yawChange = std::abs(player.angVel.z - lastCarYaw[player.carId]);

            float ballSpeed = state.ball.vel.Length();
            float reward = 0.0f;

            // Check for standard front flip flick (forward alignment ~1.0, minimal yaw)
            bool isFrontFlip = (forwardAlignment > 0.85f && yawChange < 1.5f && pitchChange > 2.0f);

            // Check for standard diagonal flick (some forward alignment, moderate yaw)
            bool isDiagonalFlip = (forwardAlignment > 0.6f && forwardAlignment <= 0.85f &&
                yawChange > 1.5f && yawChange < 3.5f);

            // Musty flick: backward flip (forwardAlignment < 0)
            bool isMusty = (forwardAlignment < -0.3f && pitchChange > 2.5f);

            // Side flick: mostly yaw rotation, minimal forward alignment
            bool isSideFlick = (std::abs(forwardAlignment) < 0.5f && yawChange > 3.0f);

            // Tornado flick: high yaw + pitch change
            bool isTornado = (yawChange > 4.0f && pitchChange > 3.0f);

            // Upside-down flicks: car inverted during flick
            bool isInverted = (upAlignment < -0.3f);

            // Reward structure
            if (isFrontFlip || isDiagonalFlip) {
                // Minimal/no reward for standard flicks
                reward = 0.5f;
            }
            else if (isMusty) {
                reward = 30.0f * (ballSpeed / 2000.0f); // Scale with ball speed
            }
            else if (isSideFlick) {
                reward = 25.0f * (ballSpeed / 2000.0f);
            }
            else if (isTornado) {
                reward = 35.0f * (ballSpeed / 2000.0f);
            }
            else if (isInverted) {
                reward = 28.0f * (ballSpeed / 2000.0f);
            }
            else {
                // Any other non-standard flick
                reward = 20.0f * (ballSpeed / 2000.0f);
            }

            // Bonus for high-speed flicks
            if (ballSpeed > 2200.0f && !isFrontFlip && !isDiagonalFlip) {
                reward += 10.0f;
            }

            lastCarForward[player.carId] = curForward;
            lastCarUp[player.carId] = curUp;
            lastCarPitch[player.carId] = player.angVel.x;
            lastCarYaw[player.carId] = player.angVel.z;

            return reward;
        }

        // Update tracking
        lastCarForward[player.carId] = player.rotMat.forward;
        lastCarUp[player.carId] = player.rotMat.up;
        lastCarPitch[player.carId] = player.angVel.x;
        lastCarYaw[player.carId] = player.angVel.z;

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

// Dribble control - encouraging scoring-oriented dribbles
// 2025-12-08 - Rev 3 - Removed invalid PlayerEventState::lastTouch usage.
//                     Uses recent-touch timer for possession, and
//                     rewards long, forward, goal-directed dribbles. (NJH)
class DribbleControlReward : public Reward {
private:
    mutable std::map<uint32_t, float> dribbleTime;
    mutable std::map<uint32_t, Vec>   lastBallVel;
    mutable std::map<uint32_t, float> recentTouchTimer; // seconds since last touch

public:
    virtual void Reset(const GameState& initialState) override {
        dribbleTime.clear();
        lastBallVel.clear();
        recentTouchTimer.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!state.prev)
            return 0.0f;

        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

        // Track recent touches to approximate possession
        if (player.ballTouchedStep) {
            // Consider possession valid for a short window after a touch
            recentTouchTimer[player.carId] = 0.5f; // 0.5s of "we last touched it"
        }
        else {
            float t = recentTouchTimer[player.carId];
            t = RS_MAX(0.0f, t - dt);
            recentTouchTimer[player.carId] = t;
        }

        bool hasPossession = (recentTouchTimer[player.carId] > 0.0f);

        Vec prevBallVel = state.prev->ball.vel;
        Vec curBallVel = state.ball.vel;
        lastBallVel[player.carId] = curBallVel;

        // Opponent goal (target for attack)
        Vec enemyGoal = (player.team == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_BACK
            : CommonValues::BLUE_GOAL_BACK;

        // Ball position relative to car
        Vec rel = state.ball.pos - player.pos;
        float horizDist = Vec(rel.x, rel.y, 0).Length();
        float heightAboveCar = rel.z;

        // "Ball on car" definition
        bool ballOnCar =
            player.isOnGround &&
            heightAboveCar > 30.0f &&
            heightAboveCar < 200.0f &&
            horizDist < 140.0f;

        if (!ballOnCar || !hasPossession) {
            dribbleTime[player.carId] = 0.0f;
            return 0.0f;
        }

        // We are dribbling with (approximate) possession → accumulate dribble time
        dribbleTime[player.carId] += dt;
        float time = dribbleTime[player.carId];

        // Direction toward enemy goal & ball motion alignment
        Vec dirToGoal = (enemyGoal - state.ball.pos).Normalized();
        float curAlign = curBallVel.Normalized().Dot(dirToGoal);
        float prevAlign = prevBallVel.Normalized().Dot(dirToGoal);
        float velImprovement = curAlign - prevAlign; // >0 => more toward goal

        // Only reward dribbles that actually move ball toward goal
        if (curAlign < 0.1f) {
            return 0.0f;
        }

        // Proximity factor: closer to enemy goal → higher threat
        float distToGoal = (enemyGoal - state.ball.pos).Length();
        float goalProx = RS_CLAMP(1.0f - distToGoal / 6000.0f, 0.0f, 1.0f);

        // Time factor: longer successful dribble → more reward (up to a cap)
        float timeFactor = 1.0f + 0.25f * time;      // grows with time
        timeFactor = RS_CLAMP(timeFactor, 1.0f, 3.0f); // cap so it can't go insane

        // Combine contributions:
        //  - strong alignment of ball velocity toward goal (curAlign)
        //  - positive improvement over last frame (velImprovement)
        //  - being closer to goal (goalProx)
        //  - sustaining the dribble (timeFactor)
        float reward =
            (curAlign * 1.2f +
                RS_MAX(0.0f, velImprovement) * 0.8f) *
            timeFactor *
            (0.5f + 0.5f * goalProx);

        return RS_CLAMP(reward, 0.0f, 5.0f);
    }
};



// Ground to air dribble transition
class GroundToAirDribbleReward : public Reward {
private:
    mutable std::map<uint32_t, bool> wasGroundDribbling;
    mutable std::map<uint32_t, int> airDribbleTouchCount;
    mutable std::map<uint32_t, float> lastBallHeight;

public:
    virtual void Reset(const GameState& initialState) override {
        wasGroundDribbling.clear();
        airDribbleTouchCount.clear();
        lastBallHeight.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        Vec relBallPos = state.ball.pos - player.pos;
        Vec ballToCar = player.pos - state.ball.pos;
        float heightAboveCar = relBallPos.z;
        float horizDist = Vec(relBallPos.x, relBallPos.y, 0).Length();
        float ballHeight = state.ball.pos.z;

        // Ball on car detection (ground dribble)
        bool ballOnCar = (heightAboveCar > 30.0f && heightAboveCar < 200.0f &&
            horizDist < 150.0f && player.isOnGround);

        // Ball bouncing up detection
        bool ballBouncingUp = (state.ball.vel.z > 200.0f && ballHeight < 300.0f &&
            ballHeight > 100.0f);

        // Speed matching (within 100 uu/s in XY plane)
        Vec ballVelXY = Vec(state.ball.vel.x, state.ball.vel.y, 0);
        Vec playerVelXY = Vec(player.vel.x, player.vel.y, 0);
        float speedDiff = (ballVelXY - playerVelXY).Length();
        bool speedMatched = (speedDiff < 150.0f); // Slightly more lenient

        bool wasGround = wasGroundDribbling[player.carId];
        bool isAirDribbling = false;
        float reward = 0.0f;

        // Check if currently air dribbling
        if (!player.isOnGround &&
            ballHeight > 150.0f &&
            horizDist < 200.0f &&
            heightAboveCar > -50.0f && heightAboveCar < 250.0f &&
            speedMatched) {
            isAirDribbling = true;
        }

        // Reward transition from ground to air
        if (wasGround && isAirDribbling) {
            reward += 25.0f; // Initial transition reward
        }

        // Reward sustained air dribbling touches
        if (isAirDribbling && player.ballTouchedStep) {
            // Check if it's a "light touch" (not launching the ball away)
            float ballAccel = state.ball.vel.Length() -
                (lastBallHeight[player.carId] > 0 ?
                    std::sqrt(2 * 9.8f * (ballHeight - lastBallHeight[player.carId])) : 0);

            bool isLightTouch = (ballAccel < 500.0f); // Ball doesn't accelerate too much

            if (isLightTouch) {
                int touchCount = airDribbleTouchCount[player.carId];
                airDribbleTouchCount[player.carId] = touchCount + 1;

                // Exponentially reward multiple air touches
                reward += 10.0f * std::min(touchCount + 1, 5); // Cap at 5 touches
            }
            else {
                // Reset if it's a hard touch (shooting)
                airDribbleTouchCount[player.carId] = 0;
            }
        }

        // Reset air dribble count if back on ground
        if (player.isOnGround && !ballOnCar) {
            airDribbleTouchCount[player.carId] = 0;
        }

        // Update state tracking
        wasGroundDribbling[player.carId] = ballOnCar || (ballBouncingUp && speedMatched && player.isOnGround);
        lastBallHeight[player.carId] = ballHeight;

        return reward;
    }
};

// Rewards demolitions in opponent's half
class DemoOffenseReward : public Reward {
private:
    mutable std::map<uint32_t, int> lastDemolishCount;

public:
    virtual void Reset(const GameState& initialState) override {
        lastDemolishCount.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        int currentDemos = player.eventState.demo;
        int lastDemos = lastDemolishCount[player.carId];

        // New demo detected
        if (currentDemos > lastDemos) {
            lastDemolishCount[player.carId] = currentDemos;

            // Determine which half of field based on team
            // Assuming blue team defends negative X, orange defends positive X
            bool isBlueTeam = ((int)player.team == 0);
            float fieldHalf = isBlueTeam ? 1.0f : -1.0f;

            // Check if demo occurred in opponent's half
            if (player.pos.x * fieldHalf > 0.0f) {
                // Scale reward based on how deep in opponent's half
                float depthRatio = std::abs(player.pos.x) / 4096.0f; // Field half-length
                float baseReward = 30.0f;

                // Bonus for demos near opponent's goal
                if (std::abs(player.pos.x) > 3500.0f) {
                    baseReward += 15.0f; // Extra reward for goal-area demos
                }

                return baseReward * depthRatio;
            }
        }

        lastDemolishCount[player.carId] = currentDemos;
        return 0.0f;
    }
};

// ============================================================================
// DemoBumpOutplayReward - ELITE TIER
// Rewards outplaying opponents through strategic demos and bumps
// High-speed demos, bump chains, and positioning punish
// 2025-01-04 | Rev 1.0 | NJH
// ============================================================================

class DemoBumpOutplayReward : public Reward {
private:
    struct OutplayConfig {
        // Demo thresholds
        float minDemoSpeed = 1200.0f;              // Minimum speed for "powerful" demo
        float goodDemoSpeed = 1800.0f;             // Good demo speed
        float greatDemoSpeed = 2300.0f;            // Great demo speed

        // Bump thresholds
        float minBumpSpeed = 800.0f;
        float goodBumpSpeed = 1400.0f;

        // Positioning
        float enemyGoalDistance = 2000.0f;         // Reward demos near enemy goal
        float maxDemoDistance = 6000.0f;           // How far away demo counts

        // Timing
        float demoCooldown = 5.0f;                 // Seconds before reward same opponent again
        float bumpChainWindow = 1.0f;              // Time to chain bumps for bonus
    };

    enum OutplayPhase {
        PHASE_IDLE,
        PHASE_TRACKING,     // Closing in on opponent
        PHASE_CONTACT,      // Made contact
        PHASE_FOLLOWUP      // Chaining hits
    };

    mutable std::map<uint32_t, int> lastDemoCount;
    mutable std::map<uint32_t, int> lastBumpCount;
    mutable std::map<uint32_t, std::map<uint32_t, float>> enemyDemoCooldown;  // carId -> enemyId -> cooldown
    mutable std::map<uint32_t, OutplayPhase> outplayPhase;
    mutable std::map<uint32_t, float> chainTimer;
    mutable std::map<uint32_t, uint32_t> lastHitEnemyId;
    mutable std::map<uint32_t, Vec> enemyPosAtContact;

    OutplayConfig config;

    float ComputeCarCollisionSpeed(const Player& player, const Player& enemy) const {
        // Relative velocity magnitude at collision
        Vec relVel = player.vel - enemy.vel;
        return relVel.Length();
    }

    Vec GetEnemyGoalPosition(Team enemyTeam) const {
        if (enemyTeam == Team::BLUE) {
            return Vec(0.0f, -5120.0f, 321.3875f);
        }
        else {
            return Vec(0.0f, 5120.0f, 321.3875f);
        }
    }

public:
    DemoBumpOutplayReward() : config() {}

    explicit DemoBumpOutplayReward(const OutplayConfig& cfg) : config(cfg) {}

    virtual void Reset(const GameState& initialState) override {
        lastDemoCount.clear();
        lastBumpCount.clear();
        enemyDemoCooldown.clear();
        outplayPhase.clear();
        chainTimer.clear();
        lastHitEnemyId.clear();
        enemyPosAtContact.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Initialize
        if (lastDemoCount.find(player.carId) == lastDemoCount.end()) {
            lastDemoCount[player.carId] = player.eventState.demo;
            lastBumpCount[player.carId] = player.eventState.bump;
            outplayPhase[player.carId] = PHASE_IDLE;
            chainTimer[player.carId] = 0.0f;
            return 0.0f;
        }

        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
        Vec enemyGoalPos = GetEnemyGoalPosition(player.team == Team::BLUE ? Team::ORANGE : Team::BLUE);
        float reward = 0.0f;

        // =====================================================================
        // DEMO REWARD - New demolition detected
        // =====================================================================
        int currentDemos = player.eventState.demo;
        int lastDemos = lastDemoCount[player.carId];

        if (currentDemos > lastDemos) {
            lastDemoCount[player.carId] = currentDemos;

            // Find which enemy was demoed (use state to determine)
            // For now, assume the closest enemy was hit
            const Player* demoed_enemy = nullptr;
            float minDist = 1e9f;

            for (const auto& enemy : state.players) {
                if (enemy.team != player.team) {
                    float dist = player.pos.Dist(enemy.pos);
                    if (dist < minDist && dist < 300.0f) {
                        minDist = dist;
                        demoed_enemy = &enemy;
                    }
                }
            }

            if (demoed_enemy) {
                lastHitEnemyId[player.carId] = demoed_enemy->carId;
                enemyPosAtContact[player.carId] = demoed_enemy->pos;

                // ========== SPEED REWARD ==========
                float carSpeed = player.vel.Length();
                float demoSpeedReward = 0.0f;

                if (carSpeed >= config.greatDemoSpeed) {
                    demoSpeedReward = 1.0f;
                }
                else if (carSpeed >= config.goodDemoSpeed) {
                    float t = (carSpeed - config.goodDemoSpeed) / (config.greatDemoSpeed - config.goodDemoSpeed);
                    demoSpeedReward = 0.6f + 0.4f * t;
                }
                else if (carSpeed >= config.minDemoSpeed) {
                    float t = (carSpeed - config.minDemoSpeed) / (config.goodDemoSpeed - config.minDemoSpeed);
                    demoSpeedReward = 0.3f + 0.3f * t;
                }

                // ========== POSITIONING REWARD ==========
                float distToEnemyGoal = demoed_enemy->pos.Dist(enemyGoalPos);
                float positioningReward = 0.0f;

                if (distToEnemyGoal < config.enemyGoalDistance) {
                    // Demo near enemy goal
                    float depth = 1.0f - (distToEnemyGoal / config.enemyGoalDistance);
                    positioningReward = depth * 0.4f;
                }

                // ========== COOLDOWN CHECK ==========
                float demoReward = (demoSpeedReward * 0.7f + positioningReward * 0.3f) * 50.0f;

                // Apply cooldown so we don't reward same enemy repeatedly
                if (enemyDemoCooldown[player.carId][demoed_enemy->carId] <= 0.0f) {
                    reward = demoReward;
                    enemyDemoCooldown[player.carId][demoed_enemy->carId] = config.demoCooldown;
                }

                outplayPhase[player.carId] = PHASE_CONTACT;
                chainTimer[player.carId] = 0.0f;
            }
        }

        // =====================================================================
        // BUMP REWARD - Bumps as part of outplay chain
        // =====================================================================
        int currentBumps = player.eventState.bump;
        int lastBumps = lastBumpCount[player.carId];

        if (currentBumps > lastBumps) {
            lastBumpCount[player.carId] = currentBumps;

            // Find bumped enemy
            const Player* bumped_enemy = nullptr;
            float minDist = 1e9f;

            for (const auto& enemy : state.players) {
                if (enemy.team != player.team) {
                    float dist = player.pos.Dist(enemy.pos);
                    if (dist < minDist && dist < 400.0f) {
                        minDist = dist;
                        bumped_enemy = &enemy;
                    }
                }
            }

            if (bumped_enemy) {
                float carSpeed = player.vel.Length();
                float bumpSpeedReward = 0.0f;

                if (carSpeed >= config.goodBumpSpeed) {
                    float t = (carSpeed - config.goodBumpSpeed) / 1000.0f;
                    bumpSpeedReward = 0.4f + RS_CLAMP(0.3f * t, 0.0f, 0.3f);
                }
                else if (carSpeed >= config.minBumpSpeed) {
                    float t = (carSpeed - config.minBumpSpeed) / (config.goodBumpSpeed - config.minBumpSpeed);
                    bumpSpeedReward = 0.15f + 0.25f * t;
                }

                float bumpReward = bumpSpeedReward * 20.0f;

                // Bump chain bonus (if chaining hits on same/nearby enemies)
                if (outplayPhase[player.carId] == PHASE_CONTACT && chainTimer[player.carId] < config.bumpChainWindow) {
                    bumpReward *= 1.5f;  // +50% for chaining
                    outplayPhase[player.carId] = PHASE_FOLLOWUP;
                }

                reward += bumpReward;
                chainTimer[player.carId] = 0.0f;
            }
        }

        // Tick down cooldowns
        for (auto& [enemyId, cooldown] : enemyDemoCooldown[player.carId]) {
            cooldown -= dt;
        }

        // Update chain timer
        chainTimer[player.carId] += dt;
        if (chainTimer[player.carId] > config.bumpChainWindow) {
            outplayPhase[player.carId] = PHASE_IDLE;
        }

        return reward;
    }

    void SetMinDemoSpeed(float speed) { config.minDemoSpeed = speed; }
    void SetGoodDemoSpeed(float speed) { config.goodDemoSpeed = speed; }
    void SetMinBumpSpeed(float speed) { config.minBumpSpeed = speed; }
};


// ============================================================================
// DemoChasingReward - ELITE TIER
// Rewards chasing down and intercepting opponent cars at high speed
// Encourages aggressive pursuit and interception mechanics
// 2025-01-04 | Rev 1.0 | NJH
// ============================================================================

class DemoChasingReward : public Reward {
private:
    struct ChasingConfig {
        // Distance thresholds (UU)
        float chasingRangeMin = 500.0f;            // Start chasing within 500 UU
        float chasingRangeMax = 3000.0f;           // Max chasing distance

        // Speed thresholds (UU/s)
        float minChasingSpeed = 1000.0f;           // Minimum speed to chase
        float goodChasingSpeed = 1600.0f;          // Good chase speed
        float greatChasingSpeed = 2000.0f;         // Great chase speed
        float insaneChasingSpeed = 2400.0f;        // Insane chase speed (near max car speed)

        // Interception accuracy
        float maxInterceptionAngle = 0.3927f;      // ±22.5 degrees to intercept vector
        float maxInterceptionDistance = 200.0f;    // How close before "interception"

        // Boost efficiency
        float boostBonusPerSecond = 0.05f;         // Small bonus for maintaining boost
    };

    enum ChasingPhase {
        PHASE_IDLE,
        PHASE_PURSUING,     // Moving toward enemy
        PHASE_INTERCEPTING, // On collision course
        PHASE_CONTACT       // Made contact
    };

    mutable std::map<uint32_t, ChasingPhase> chasingPhase;
    mutable std::map<uint32_t, float> chasingTimer;
    mutable std::map<uint32_t, uint32_t> targetEnemyId;
    mutable std::map<uint32_t, float> boostUptime;

    ChasingConfig config;

    const Player* FindNearestEnemy(const Player& player, const GameState& state) const {
        const Player* nearest = nullptr;
        float minDist = config.chasingRangeMax;

        for (const auto& enemy : state.players) {
            if (enemy.team != player.team) {
                float dist = player.pos.Dist(enemy.pos);
                if (dist < minDist) {
                    minDist = dist;
                    nearest = &enemy;
                }
            }
        }
        return nearest;
    }

    float ComputeInterceptionAngle(const Player& player, const Player& enemy) const {
        // Angle between player's direction and interception vector
        Vec carVel = player.vel;
        Vec toEnemy = (enemy.pos - player.pos);

        if (carVel.Length() < 100.0f || toEnemy.Length() < 10.0f) {
            return 3.14159f;  // 180 degrees (opposite)
        }

        float dotProduct = carVel.Normalized().Dot(toEnemy.Normalized());
        dotProduct = RS_CLAMP(dotProduct, -1.0f, 1.0f);
        return acos(dotProduct);
    }

public:
    DemoChasingReward() : config() {}

    explicit DemoChasingReward(const ChasingConfig& cfg) : config(cfg) {}

    virtual void Reset(const GameState& initialState) override {
        chasingPhase.clear();
        chasingTimer.clear();
        targetEnemyId.clear();
        boostUptime.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Initialize
        if (chasingPhase.find(player.carId) == chasingPhase.end()) {
            chasingPhase[player.carId] = PHASE_IDLE;
            chasingTimer[player.carId] = 0.0f;
            boostUptime[player.carId] = 0.0f;
            return 0.0f;
        }

        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
        float carSpeed = player.vel.Length();
        bool isBootsing = player.boost > 0;

        float reward = 0.0f;

        // =====================================================================
        // PHASE 1: PURSUING - Finding and moving toward enemy
        // =====================================================================
        ChasingPhase& phase = chasingPhase[player.carId];
        float& timer = chasingTimer[player.carId];

        const Player* enemy = FindNearestEnemy(player, state);

        if (!enemy) {
            phase = PHASE_IDLE;
            timer = 0.0f;
            return 0.0f;
        }

        float distToEnemy = player.pos.Dist(enemy->pos);

        if (phase == PHASE_IDLE || phase == PHASE_PURSUING) {
            if (distToEnemy < config.chasingRangeMax && carSpeed >= config.minChasingSpeed) {
                // ========== SPEED REWARD ==========
                float speedReward = 0.0f;

                if (carSpeed >= config.insaneChasingSpeed) {
                    speedReward = 1.0f;
                }
                else if (carSpeed >= config.greatChasingSpeed) {
                    float t = (carSpeed - config.greatChasingSpeed) / (config.insaneChasingSpeed - config.greatChasingSpeed);
                    speedReward = 0.7f + 0.3f * t;
                }
                else if (carSpeed >= config.goodChasingSpeed) {
                    float t = (carSpeed - config.goodChasingSpeed) / (config.greatChasingSpeed - config.goodChasingSpeed);
                    speedReward = 0.4f + 0.3f * t;
                }
                else {
                    float t = (carSpeed - config.minChasingSpeed) / (config.goodChasingSpeed - config.minChasingSpeed);
                    speedReward = 0.15f + 0.25f * t;
                }

                // ========== DISTANCE PENALTY ==========
                float distancePenalty = RS_CLAMP(distToEnemy / config.chasingRangeMax, 0.0f, 1.0f);
                float distanceReward = (1.0f - distancePenalty) * 0.5f;

                // ========== BOOST BONUS ==========
                float boostBonus = 0.0f;
                if (isBootsing) {
                    boostUptime[player.carId] += dt;
                    boostBonus = RS_CLAMP(boostUptime[player.carId] * config.boostBonusPerSecond, 0.0f, 0.2f);
                }
                else {
                    boostUptime[player.carId] = 0.0f;
                }

                reward = (speedReward * 0.6f + distanceReward * 0.3f + boostBonus * 0.1f) * 15.0f;

                phase = PHASE_PURSUING;
                timer = 0.0f;
                targetEnemyId[player.carId] = enemy->carId;
            }
        }

        // =====================================================================
        // PHASE 2: INTERCEPTING - On collision course with high accuracy
        // =====================================================================
        if (phase == PHASE_PURSUING) {
            float interceptionAngle = ComputeInterceptionAngle(player, *enemy);

            if (distToEnemy < config.maxInterceptionDistance &&
                interceptionAngle < config.maxInterceptionAngle &&
                carSpeed >= config.goodChasingSpeed) {

                // ========== INTERCEPTION ACCURACY ==========
                float angleScore = 1.0f - (interceptionAngle / config.maxInterceptionAngle);
                float speedScore = RS_CLAMP((carSpeed - config.goodChasingSpeed) / 800.0f, 0.0f, 1.0f);
                float distScore = 1.0f - (distToEnemy / config.maxInterceptionDistance);

                reward = (angleScore * 0.5f + speedScore * 0.3f + distScore * 0.2f) * 25.0f;

                phase = PHASE_INTERCEPTING;
                timer = 0.0f;
            }
        }

        // =====================================================================
        // PHASE 3: CONTACT - Collision detected
        // =====================================================================
        if (phase == PHASE_INTERCEPTING) {
            if (player.ballTouchedStep || distToEnemy < 150.0f) {
                // Contact made - large reward
                reward = 30.0f;
                phase = PHASE_CONTACT;
                timer = 0.0f;
            }
            // Timeout
            else if (timer > 2.0f) {
                phase = PHASE_PURSUING;
                timer = 0.0f;
            }
        }

        timer += dt;

        return reward;
    }

    void SetChasingRangeMax(float range) { config.chasingRangeMax = range; }
    void SetMinChasingSpeed(float speed) { config.minChasingSpeed = speed; }
    void SetGoodChasingSpeed(float speed) { config.goodChasingSpeed = speed; }
    void SetGreatChasingSpeed(float speed) { config.greatChasingSpeed = speed; }
    void SetInterceptionParams(float angle, float distance) {
        config.maxInterceptionAngle = angle;
        config.maxInterceptionDistance = distance;
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
        bool HasFlipOrJump = player.HasFlipOrJump();
        
        // Regained flip in air = flip reset
        if (!hadFlip && HasFlipOrJump) {
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

// ============================================================================
// WALL PINCH REWARD
// Rewards pinching ball against side/back wall to create speed.
// Key: matching ball velocity + sending ball outward + directional control
// ============================================================================
class WallPinchReward : public Reward {
private:
    mutable std::map<uint32_t, float> prevBallSpeed;

    static constexpr float WALL_DISTANCE_THRESHOLD = 200.0f;  // More lenient
    static constexpr float MIN_PINCH_SPEED_GAIN = 500.0f;     // Speed increase needed
    static constexpr float MIN_RESULT_SPEED = 2000.0f;        // Minimum ball speed after
    static constexpr float VELOCITY_MATCH_BONUS = 0.15f;      // Reward for matching ball speed
    static constexpr float BASE_PINCH_REWARD = 0.4f;
    static constexpr float DODGE_BONUS = 0.3f;

public:
    virtual void Reset(const GameState& initialState) override {
        prevBallSpeed.clear();
    }

    bool is_near_wall(const Vec& ballPos) const {
        float distToWallX = RS_MIN(
            abs(ballPos.x - CommonValues::SIDE_WALL_X),
            abs(ballPos.x + CommonValues::SIDE_WALL_X)
        );
        float distToWallY = RS_MIN(
            abs(ballPos.y - CommonValues::BACK_WALL_Y),
            abs(ballPos.y + CommonValues::BACK_WALL_Y)
        );

        float minWallDist = RS_MIN(distToWallX, distToWallY);
        return minWallDist < WALL_DISTANCE_THRESHOLD;
    }

    bool is_ball_moving_away_from_wall(const Vec& ballPos, const Vec& ballVel) const {
        // Check X direction (side walls)
        float distToLeftWall = abs(ballPos.x + CommonValues::SIDE_WALL_X);
        float distToRightWall = abs(ballPos.x - CommonValues::SIDE_WALL_X);

        if (distToLeftWall < distToRightWall) {
            // Closer to left wall, should move right (positive X)
            if (ballVel.x < -100.0f) return false;  // Moving wrong direction
        }
        else {
            // Closer to right wall, should move left (negative X)
            if (ballVel.x > 100.0f) return false;
        }

        // Check Y direction (back walls)
        float distToBackY = abs(ballPos.y - CommonValues::BACK_WALL_Y);
        float distToOwnY = abs(ballPos.y + CommonValues::BACK_WALL_Y);

        if (distToBackY < distToOwnY) {
            // Near back wall, should move toward own goal
            if (ballVel.y > 100.0f) return false;
        }
        else {
            // Near own goal line, should move toward back wall
            if (ballVel.y < -100.0f) return false;
        }

        return true;
    }

    float compute_velocity_match_bonus(const Player& player, const GameState& state) const {
        // Reward matching ball velocity (critical for pinch setup)
        float playerSpeed = player.vel.Length();
        float ballSpeed = state.ball.vel.Length();

        // Perfect match = 5% of ball speed as bonus
        float speedDiff = abs(playerSpeed - ballSpeed);
        if (speedDiff < 200.0f) {
            // Close match
            return VELOCITY_MATCH_BONUS;
        }
        else if (speedDiff < 500.0f) {
            // Partial match
            return VELOCITY_MATCH_BONUS * 0.5f;
        }

        return 0.0f;
    }

    float compute_goal_direction_multiplier(const Player& player, const GameState& state) const {
        // Get opponent goal position
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_LINE_CENTER :
            CommonValues::BLUE_GOAL_LINE_CENTER;

        // Direction from ball to opponent goal
        Vec toGoal = opponentGoal - state.ball.pos;
        Vec goalDir = toGoal.Normalized();

        // Ball velocity direction
        float ballSpeed = state.ball.vel.Length();
        if (ballSpeed < 100.0f) return 1.0f;  // Stationary, no multiplier

        Vec ballDir = state.ball.vel / ballSpeed;

        // Dot product: how aligned is ball velocity with goal direction?
        float alignment = ballDir.Dot(goalDir);

        // 2.0x if directly toward goal (alignment > 0.7)
        if (alignment > 0.7f) {
            return 2.0f;
        }
        // 1.5x if general direction toward goal (alignment > 0.3)
        else if (alignment > 0.3f) {
            return 1.5f;
        }
        // 1.0x if neutral or wrong direction
        else {
            return 1.0f;
        }
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep || !state.prev) return 0.0f;

        // Check if ball is near wall
        if (!is_near_wall(state.ball.pos)) {
            prevBallSpeed[player.carId] = state.ball.vel.Length();
            return 0.0f;
        }

        float prevSpeed = prevBallSpeed[player.carId];
        float curSpeed = state.ball.vel.Length();
        float speedGain = curSpeed - prevSpeed;

        // Detect pinch: significant speed increase
        if (speedGain < MIN_PINCH_SPEED_GAIN || curSpeed < MIN_RESULT_SPEED) {
            prevBallSpeed[player.carId] = curSpeed;
            return 0.0f;
        }

        // Validate ball is moving AWAY from wall
        if (!is_ball_moving_away_from_wall(state.ball.pos, state.ball.vel)) {
            prevBallSpeed[player.carId] = curSpeed;
            return 0.0f;
        }

        // Successful wall pinch
        float reward = BASE_PINCH_REWARD;

        // Bonus for velocity matching
        reward += compute_velocity_match_bonus(player, state);

        // Bonus for using dodge/flip
        if (player.isFlipping) {
            reward += DODGE_BONUS;
        }

        // Multiplier for shooting toward opponent goal
        float goalMultiplier = compute_goal_direction_multiplier(player, state);
        reward *= goalMultiplier;

        prevBallSpeed[player.carId] = curSpeed;
        return reward;
    }
};

// ============================================================================
// GROUND PINCH REWARD
// Rewards pinching ball on ground between car and opponent/wall.
// Requires: low car, matching speed, directional control, explosive power
// ============================================================================
class GroundPinchReward : public Reward {
private:
    mutable std::map<uint32_t, float> prevBallSpeed;

    static constexpr float GROUND_HEIGHT_THRESHOLD = 150.0f;    // Ball near ground
    static constexpr float MIN_PINCH_SPEED_GAIN = 600.0f;       // Need good speed
    static constexpr float MIN_RESULT_SPEED = 2200.0f;
    static constexpr float VELOCITY_MATCH_BONUS = 0.15f;
    static constexpr float BASE_PINCH_REWARD = 0.5f;
    static constexpr float DODGE_BONUS = 0.35f;

public:
    virtual void Reset(const GameState& initialState) override {
        prevBallSpeed.clear();
    }

    bool is_ball_on_ground(const Vec& ballPos) const {
        return ballPos.z < GROUND_HEIGHT_THRESHOLD;
    }

    float compute_velocity_match_bonus(const Player& player, const GameState& state) const {
        float playerSpeed = player.vel.Length();
        float ballSpeed = state.ball.vel.Length();
        float speedDiff = abs(playerSpeed - ballSpeed);

        if (speedDiff < 200.0f) {
            return VELOCITY_MATCH_BONUS;
        }
        else if (speedDiff < 500.0f) {
            return VELOCITY_MATCH_BONUS * 0.5f;
        }

        return 0.0f;
    }

    float compute_goal_direction_multiplier(const Player& player, const GameState& state) const {
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_LINE_CENTER :
            CommonValues::BLUE_GOAL_LINE_CENTER;

        Vec toGoal = opponentGoal - state.ball.pos;
        Vec goalDir = toGoal.Normalized();

        float ballSpeed = state.ball.vel.Length();
        if (ballSpeed < 100.0f) return 1.0f;

        Vec ballDir = state.ball.vel / ballSpeed;
        float alignment = ballDir.Dot(goalDir);

        if (alignment > 0.7f) {
            return 2.0f;
        }
        else if (alignment > 0.3f) {
            return 1.5f;
        }
        else {
            return 1.0f;
        }
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep || !state.prev) return 0.0f;

        if (!is_ball_on_ground(state.ball.pos)) {
            prevBallSpeed[player.carId] = state.ball.vel.Length();
            return 0.0f;
        }

        float prevSpeed = prevBallSpeed[player.carId];
        float curSpeed = state.ball.vel.Length();
        float speedGain = curSpeed - prevSpeed;

        // Detect pinch
        if (speedGain < MIN_PINCH_SPEED_GAIN || curSpeed < MIN_RESULT_SPEED) {
            prevBallSpeed[player.carId] = curSpeed;
            return 0.0f;
        }

        // Successful ground pinch
        float reward = BASE_PINCH_REWARD;

        reward += compute_velocity_match_bonus(player, state);

        if (player.isFlipping) {
            reward += DODGE_BONUS;
        }

        // Multiplier for shooting toward opponent goal
        float goalMultiplier = compute_goal_direction_multiplier(player, state);
        reward *= goalMultiplier;

        prevBallSpeed[player.carId] = curSpeed;
        return reward;
    }
};

// ============================================================================
// CEILING PINCH REWARD
// Rewards pinching ball against ceiling for aerial plays.
// Hardest pinch - requires ceiling control + matching speed + flip
// ============================================================================
class CeilingPinchReward : public Reward {
private:
    mutable std::map<uint32_t, float> prevBallSpeed;

    static constexpr float CEILING_HEIGHT_THRESHOLD = 4050.0f;  // Near ceiling
    static constexpr float MIN_PINCH_SPEED_GAIN = 700.0f;       // Highest bar
    static constexpr float MIN_RESULT_SPEED = 2400.0f;
    static constexpr float VELOCITY_MATCH_BONUS = 0.2f;         // Higher bonus for aerial
    static constexpr float BASE_PINCH_REWARD = 0.6f;            // Higher base
    static constexpr float DODGE_BONUS = 0.4f;                  // Higher dodge bonus

public:
    virtual void Reset(const GameState& initialState) override {
        prevBallSpeed.clear();
    }

    bool is_ball_on_ceiling(const Vec& ballPos) const {
        return ballPos.z > CEILING_HEIGHT_THRESHOLD;
    }

    float compute_velocity_match_bonus(const Player& player, const GameState& state) const {
        float playerSpeed = player.vel.Length();
        float ballSpeed = state.ball.vel.Length();
        float speedDiff = abs(playerSpeed - ballSpeed);

        if (speedDiff < 150.0f) {
            // Tight match needed for ceiling
            return VELOCITY_MATCH_BONUS;
        }
        else if (speedDiff < 400.0f) {
            return VELOCITY_MATCH_BONUS * 0.5f;
        }

        return 0.0f;
    }

    float compute_goal_direction_multiplier(const Player& player, const GameState& state) const {
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_LINE_CENTER :
            CommonValues::BLUE_GOAL_LINE_CENTER;

        Vec toGoal = opponentGoal - state.ball.pos;
        Vec goalDir = toGoal.Normalized();

        float ballSpeed = state.ball.vel.Length();
        if (ballSpeed < 100.0f) return 1.0f;

        Vec ballDir = state.ball.vel / ballSpeed;
        float alignment = ballDir.Dot(goalDir);

        if (alignment > 0.7f) {
            return 2.0f;
        }
        else if (alignment > 0.3f) {
            return 1.5f;
        }
        else {
            return 1.0f;
        }
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep || !state.prev) return 0.0f;

        if (!is_ball_on_ceiling(state.ball.pos)) {
            prevBallSpeed[player.carId] = state.ball.vel.Length();
            return 0.0f;
        }

        float prevSpeed = prevBallSpeed[player.carId];
        float curSpeed = state.ball.vel.Length();
        float speedGain = curSpeed - prevSpeed;

        // Detect pinch
        if (speedGain < MIN_PINCH_SPEED_GAIN || curSpeed < MIN_RESULT_SPEED) {
            prevBallSpeed[player.carId] = curSpeed;
            return 0.0f;
        }

        // Successful ceiling pinch
        float reward = BASE_PINCH_REWARD;

        reward += compute_velocity_match_bonus(player, state);

        if (player.isFlipping) {
            reward += DODGE_BONUS;
        }

        // Multiplier for shooting toward opponent goal
        float goalMultiplier = compute_goal_direction_multiplier(player, state);
        reward *= goalMultiplier;

        prevBallSpeed[player.carId] = curSpeed;
        return reward;
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




// ============================================================================
// AirDribbleBumpReward
// ============================================================================
// Rewards a specific sequence:
// 1. Start air dribbling (multiple air touches with ball control)
// 2. Bump or demo opponent while maintaining air dribble
// 3. Get in front of the ball (between ball and opponent goal)
//
// This teaches aggressive air dribble plays where you remove defenders
// ============================================================================
class AirDribbleBumpReward : public Reward {
private:
    struct PlayerState {
        bool isAirDribbling = false;
        int airTouchCount = 0;
        float airDribbleStartTime = 0.0f;
        bool hasBumpedDuringDribble = false;
        bool hasPositionedInFront = false;
        bool rewardPaid = false;
        float lastTouchTime = 0.0f;
    };

    mutable std::map<uint32_t, PlayerState> playerStates;
    mutable float gameTime = 0.0f;

    struct Config {
        // Air dribble detection
        float minAirTouchCount = 2.0f;              // Need at least 2 air touches to qualify
        float minBallHeight = 200.0f;               // Ball must be this high
        float maxDistToBall = 300.0f;               // Max distance to ball during air dribble
        float touchTimeout = 1.5f;                  // Max time between touches

        // Bump/demo detection
        float bumpProximity = 250.0f;               // Distance to opponent for bump detection
        float minBumpSpeed = 1800.0f;               // Minimum speed for bump to count

        // Positioning detection (in front of ball)
        float inFrontThreshold = 200.0f;            // Must be this far in front of ball
        float maxLateralDistance = 800.0f;          // Max distance to ball's path to goal

        // Rewards
        float airDribbleStartReward = 1.0f;         // Starting air dribble
        float bumpDuringDribbleReward = 15.0f;      // Bump during air dribble
        float demoDuringDribbleReward = 25.0f;      // Demo during air dribble (even better)
        float positionInFrontReward = 5.0f;         // Getting in front after bump

        // Timeouts
        float maxSequenceDuration = 5.0f;           // Max time for entire sequence
    };

    Config config;

    /**
     * Check if player is air dribbling
     */
    bool IsAirDribbling(const Player& player, const GameState& state, PlayerState& ps) const {
        // Must be airborne
        if (player.isOnGround) return false;

        // Ball must be airborne and at minimum height
        if (state.ball.pos.z < config.minBallHeight) return false;

        // Must be close to ball
        float distToBall = (player.pos - state.ball.pos).Length();
        if (distToBall > config.maxDistToBall) return false;

        return true;
    }

    /**
     * Check if player bumped or demoed an opponent
     */
    float CheckBumpOrDemo(const Player& player, const GameState& state) const {
        // Check if we demoed someone this frame
        if (player.eventState.demo) {
            return config.demoDuringDribbleReward;
        }

        // Check if we bumped someone this frame
        if (player.eventState.bump) {
            return config.bumpDuringDribbleReward;
        }

        return 0.0f;
    }

    /**
     * Check if player is positioned in front of ball (between ball and opponent goal)
     */
    bool IsPositionedInFrontOfBall(const Player& player, const GameState& state) const {
        Vec oppGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        // Calculate distances to goal
        float playerDistToGoal = (player.pos - oppGoal).Length();
        float ballDistToGoal = (state.ball.pos - oppGoal).Length();

        // Player must be closer to goal than ball
        if (playerDistToGoal > ballDistToGoal - config.inFrontThreshold) {
            return false;
        }

        // Check lateral distance to ball-to-goal path
        Vec ballToGoal = (oppGoal - state.ball.pos).Normalized();
        Vec ballToPlayer = player.pos - state.ball.pos;
        float lateralDist = fabsf(ballToPlayer.Dot(Vec(-ballToGoal.y, ballToGoal.x, 0.0f)));

        if (lateralDist > config.maxLateralDistance) {
            return false;
        }

        return true;
    }

public:
    AirDribbleBumpReward() : config() {}
    explicit AirDribbleBumpReward(const Config& cfg) : config(cfg) {}

    virtual void Reset(const GameState& initialState) override {
        playerStates.clear();
        gameTime = 0.0f;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        gameTime += state.deltaTime;
        uint32_t id = player.carId;

        // Initialize player state
        if (playerStates.find(id) == playerStates.end()) {
            playerStates[id] = PlayerState();
        }

        PlayerState& ps = playerStates[id];
        float reward = 0.0f;

        // Check for sequence timeout
        if (ps.isAirDribbling) {
            float sequenceDuration = gameTime - ps.airDribbleStartTime;
            if (sequenceDuration > config.maxSequenceDuration) {
                // Timeout - reset state
                ps = PlayerState();
                return 0.0f;
            }
        }

        // Reset if on ground and no active sequence
        if (player.isOnGround && !ps.hasBumpedDuringDribble) {
            ps = PlayerState();
            return 0.0f;
        }

        // PHASE 1: Detect air dribble start
        if (!ps.isAirDribbling && IsAirDribbling(player, state, ps)) {
            if (player.ballTouchedStep) {
                ps.airTouchCount++;
                ps.lastTouchTime = gameTime;

                if (ps.airTouchCount >= config.minAirTouchCount) {
                    // Air dribble started!
                    ps.isAirDribbling = true;
                    ps.airDribbleStartTime = gameTime;
                    reward += config.airDribbleStartReward;
                }
            }
        }

        // PHASE 2: During air dribble - maintain touches and look for bump/demo
        if (ps.isAirDribbling && !ps.hasBumpedDuringDribble) {
            // Update touch count
            if (player.ballTouchedStep) {
                ps.lastTouchTime = gameTime;
            }

            // Check for touch timeout
            if (gameTime - ps.lastTouchTime > config.touchTimeout) {
                // Lost air dribble
                ps = PlayerState();
                return 0.0f;
            }

            // Check for bump or demo
            float bumpReward = CheckBumpOrDemo(player, state);
            if (bumpReward > 0.0f) {
                ps.hasBumpedDuringDribble = true;
                reward += bumpReward;
            }
        }

        // PHASE 3: After bump - reward getting in front of ball
        if (ps.hasBumpedDuringDribble && !ps.hasPositionedInFront && !ps.rewardPaid) {
            if (IsPositionedInFrontOfBall(player, state)) {
                ps.hasPositionedInFront = true;
                ps.rewardPaid = true;
                reward += config.positionInFrontReward;

                // Sequence complete - reset for next one
                ps = PlayerState();
            }
        }

        return reward;
    }
};

// NOTE: REMOVED OLD AIRDRIBBLEBUMP -- too situational, unreliable detection!
// Air bump / demo reward - more general-purpose
// - Renamed to AirBumpDemoReward
// - Require >= 1s airborne before counting
// - Single solid demo → full reward
// - Single solid bump (close contact with opponent) → 0.75 reward
// - Less strict proximity so this is more likely to trigger in varied scenarios
class AirBumpDemoReward : public Reward {
private:
    mutable std::map<uint32_t, float> airborneTime;    // seconds in air per car
    mutable std::map<uint32_t, bool>  paidThisAir;     // avoid duplicate payouts while airborne

    // configuration (relaxed for general use)
    const float MIN_AIRBORNE_SEC = 1.0f;          // 1s airborne required
    const float BUMP_PROXIMITY_RADIUS = 1200.0f;  // ball reasonably close to car
    const float CONTACT_RADIUS = 350.0f;         // proximity to opponent to consider contact
    const float BASE_REWARD = 20.0f;             // base reward for a demo
    const float BUMP_MULT = 0.75f;               // bump pays 75% of demo

public:
    virtual void Reset(const GameState& initialState) override {
        airborneTime.clear();
        paidThisAir.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        uint32_t id = player.carId;
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

        // Track airborne time and reset when landed
        if (!player.isOnGround) {
            airborneTime[id] += dt;
        }
        else {
            airborneTime[id] = 0.0f;
            paidThisAir[id] = false;
            return 0.0f;
        }

        // Only evaluate while airborne and if not already paid this air sequence
        if (player.isOnGround) return 0.0f;
        if (paidThisAir[id]) return 0.0f;

        // Ball must be reasonably near the car to consider a bump/demo
        float distToBall = (state.ball.pos - player.pos).Length();
        if (distToBall > BUMP_PROXIMITY_RADIUS) return 0.0f;

        // Need minimum airborne time
        if (airborneTime[id] < MIN_AIRBORNE_SEC) return 0.0f;

        // Only evaluate on touch frames
        if (!player.ballTouchedStep) return 0.0f;

        // Detect demo (opponent becomes demoed this frame) — prefer explicit flags
        bool didDemo = false;
        if (player.eventState.demo) {
            didDemo = true;
        }
        else if (state.prev) {
            for (const auto& opp : state.players) {
                if (opp.team == player.team) continue;
                const Player& prevOpp = state.prev->players[opp.index];
                if (!prevOpp.eventState.demoed && opp.eventState.demoed) {
                    // ensure demo is plausibly caused by this car (distance check)
                    if ((opp.pos - player.pos).Length() < CONTACT_RADIUS) {
                        didDemo = true;
                        break;
                    }
                }
            }
        }

        // Detect solid bump/contact (non-demo) with an opponent nearby
        bool didBump = false;
        for (const auto& opp : state.players) {
            if (opp.team == player.team) continue;
            if ((opp.pos - player.pos).Length() < CONTACT_RADIUS) {
                didBump = true;
                break;
            }
        }

        // Nothing relevant happened
        if (!didDemo && !didBump) return 0.0f;

        // Mark paid for this airborne sequence to avoid multiple payouts
        paidThisAir[id] = true;

        // Payout: full for demo, reduced for bump
        if (didDemo) {
            return BASE_REWARD;
        }

        return BASE_REWARD * BUMP_MULT;
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
