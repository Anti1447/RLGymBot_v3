#pragma once
#include "Reward.h"
#include "../Math.h"

// ============================================================================
// SHOOTING REWARDS - Hard shots & outplay focus
// ============================================================================
// Progression:
// 1. Power Shot: Ball velocity, angle toward goal
// 2. Accurate Shot: Aiming (near post), distance from goal
// 3. Contested Shot: Shooting under pressure, beating defenders
// 4. Basic Outplay: Simple 1v1 beats, cutting past defenders
// ============================================================================

// ============================================================================
// PowerShotReward - Emphasizes hard, fast shots on goal
// Rewards shots with high ball velocity toward the goal
// Key: Ball speed (>1000 UU/s), direction toward goal, release from car
// ============================================================================
class PowerShotReward : public Reward {
private:
    static constexpr float MIN_SHOT_SPEED = 1000.0f;      // Hard shot threshold
    static constexpr float OPTIMAL_SHOT_SPEED = 2500.0f;  // Ideal shot speed
    static constexpr float MAX_SHOT_SPEED = 4000.0f;      // Cap for scaling

public:
    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;
        if (!state.prev) return 0.0f;

        // Get opponent goal
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        // Ball must be moving toward goal after touch
        Vec ballVel = state.ball.vel;
        float ballSpeed = ballVel.Length();

        // Must be a hard shot
        if (ballSpeed < MIN_SHOT_SPEED) return 0.0f;

        // Check direction toward goal
        Vec toGoal = (opponentGoal - state.ball.pos).Normalized();
        float goalAlignment = ballVel.Normalized().Dot(toGoal);

        // Must be aimed at goal
        if (goalAlignment < 0.4f) return 0.0f;

        // Reward based on shot speed and accuracy
        // Peak reward at OPTIMAL_SHOT_SPEED
        float speedFactor = 1.0f;
        if (ballSpeed < OPTIMAL_SHOT_SPEED) {
            speedFactor = ballSpeed / OPTIMAL_SHOT_SPEED;  // Scale up to optimal
        } else {
            speedFactor = 1.0f - RS_CLAMP((ballSpeed - OPTIMAL_SHOT_SPEED) / (MAX_SHOT_SPEED - OPTIMAL_SHOT_SPEED), 0.0f, 0.5f);
        }

        // Distance penalty (closer = better)
        float distToGoal = (state.ball.pos - opponentGoal).Length();
        float distanceFactor = 1.0f - RS_CLAMP(distToGoal / 5000.0f, 0.0f, 1.0f);  // Closer = better

        return 10.0f * speedFactor * goalAlignment * distanceFactor;  // 0-10 reward
    }
};

// ============================================================================
// AccurateShotReward - Rewards shooting with good aiming/placement
// High reward for shots aimed at goal posts (wide, high corners)
// Key: Post-side aiming, distance consideration, shot speed
// ============================================================================
class AccurateShotReward : public Reward {
private:
    static constexpr float MIN_SHOT_SPEED = 800.0f;
    static constexpr float GOAL_WIDTH = 892.0f;  // Rocket League goal width

public:
    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;
        if (!state.prev) return 0.0f;

        // Get goal position (center)
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        // Ball must be moving toward goal
        Vec ballVel = state.ball.vel;
        float ballSpeed = ballVel.Length();

        if (ballSpeed < MIN_SHOT_SPEED) return 0.0f;

        Vec toGoal = (opponentGoal - state.ball.pos).Normalized();
        float goalAlignment = ballVel.Normalized().Dot(toGoal);

        if (goalAlignment < 0.3f) return 0.0f;

        // Aim quality: reward shots aimed at posts (top corners, bottom corners)
        // Get goal line position
        float goalLineY = opponentGoal.y;
        float distToGoalLine = abs(state.ball.pos.y - goalLineY);

        // Lateral positioning (X-axis for posts)
        float lateralPos = abs(state.ball.pos.x);
        float postAiming = RS_CLAMP((GOAL_WIDTH * 0.4f) - abs(lateralPos - GOAL_WIDTH * 0.5f), 0.0f, GOAL_WIDTH * 0.4f) / (GOAL_WIDTH * 0.4f);

        // Height quality (reward high/low corners over center)
        float heightPos = state.ball.pos.z;
        float heightAiming = 1.0f - RS_CLAMP(abs(heightPos - 150.0f) / 300.0f, 0.0f, 1.0f);  // Avoid middle height

        // Distance factor (closer = more accurate needed)
        float distToGoal = (state.ball.pos - opponentGoal).Length();
        float accuracyRequirement = RS_CLAMP(distToGoal / 3000.0f, 0.5f, 1.0f);

        return 8.0f * (postAiming * 0.6f + heightAiming * 0.4f) * goalAlignment * accuracyRequirement;  // 0-8 reward
    }
};

// ============================================================================
// ContestedShotReward - Shooting while under pressure from defenders
// Rewards shots taken in dangerous situations (defenders close)
// Key: Defender proximity, shot speed, goal alignment under pressure
// ============================================================================
class ContestedShotReward : public Reward {
private:
    static constexpr float MIN_SHOT_SPEED = 800.0f;
    static constexpr float DEFENDER_THREAT_RANGE = 1000.0f;  // Close defense

public:
    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;
        if (!state.prev) return 0.0f;

        // Get goal
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        // Ball must be moving toward goal
        Vec ballVel = state.ball.vel;
        float ballSpeed = ballVel.Length();

        if (ballSpeed < MIN_SHOT_SPEED) return 0.0f;

        Vec toGoal = (opponentGoal - state.ball.pos).Normalized();
        float goalAlignment = ballVel.Normalized().Dot(toGoal);

        if (goalAlignment < 0.3f) return 0.0f;

        // Check defender pressure
        float minDefenderDistance = 999999.0f;
        int defendersNearby = 0;

        for (const auto& other : state.players) {
            if (other.team == player.team) continue;  // Only enemy defenders

            float distToPlayer = (player.pos - other.pos).Length();
            minDefenderDistance = RS_MIN(minDefenderDistance, distToPlayer);

            if (distToPlayer < DEFENDER_THREAT_RANGE) {
                defendersNearby++;
            }
        }

        // Reward for shooting under pressure
        // More defenders = higher reward (if still on target)
        float pressureFactor = RS_CLAMP((float)defendersNearby / 2.0f, 0.0f, 1.0f);  // Cap at 2 defenders
        float defensePenalty = 1.0f - RS_CLAMP(minDefenderDistance / DEFENDER_THREAT_RANGE, 0.0f, 1.0f);  // Closer defender = higher reward

        return 9.0f * pressureFactor * goalAlignment * (0.5f + 0.5f * defensePenalty);  // 0-9 reward
    }
};

// ============================================================================
// BasicOutplayReward - Simple 1v1 outplays, cutting past defenders
// Rewards beating defenders with dribbling/speed, getting to better position
// Key: Defender proximity decrease, moving toward goal, possession
// ============================================================================
class BasicOutplayReward : public Reward {
private:
    mutable std::map<uint32_t, float> lastMinDefenderDist;
    static constexpr float OUTPLAY_SUCCESS_RANGE = 1500.0f;

public:
    virtual void Reset(const GameState& initialState) override {
        lastMinDefenderDist.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;

        // Get opponent goal
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        // Check closest defender
        float minDefenderDistance = 999999.0f;
        for (const auto& other : state.players) {
            if (other.team == player.team) continue;

            float dist = (player.pos - other.pos).Length();
            minDefenderDistance = RS_MIN(minDefenderDistance, dist);
        }

        auto lastIt = lastMinDefenderDist.find(player.carId);
        float lastDefenderDistance = (lastIt != lastMinDefenderDist.end()) ? lastIt->second : minDefenderDistance;

        // Update for next step
        lastMinDefenderDist[player.carId] = minDefenderDistance;

        // Check if we're beating defender (getting closer to goal while maintaining distance from defender)
        float distToGoal = (player.pos - opponentGoal).Length();
        Vec toGoal = (opponentGoal - player.pos).Normalized();
        Vec ballVel = state.ball.vel.Normalized();
        float forwardProgress = ballVel.Dot(toGoal);

        // Reward for moving forward while maintaining ball control
        float defenderAvoidance = 0.0f;
        if (minDefenderDistance > lastDefenderDistance) {
            // Creating space from defender
            defenderAvoidance = RS_CLAMP((minDefenderDistance - lastDefenderDistance) / 200.0f, 0.0f, 1.0f);
        } else if (minDefenderDistance < lastDefenderDistance && minDefenderDistance > 500.0f) {
            // Moving toward goal while defender falls back
            defenderAvoidance = 0.5f;
        }

        // Reward only if progressing toward goal
        if (forwardProgress > 0.2f && minDefenderDistance < OUTPLAY_SUCCESS_RANGE) {
            float progressFactor = RS_CLAMP(forwardProgress, 0.0f, 1.0f);
            return 7.0f * defenderAvoidance * progressFactor;  // 0-7 reward
        }

        return 0.0f;
    }
};

// ============================================================================
// OpenGoalShotReward - Punishes missing easy opportunities
// High reward for quick, accurate shots on undefended goal
// Key: No defenders nearby, clear path to goal
// ============================================================================
class OpenGoalShotReward : public Reward {
private:
    static constexpr float MIN_SHOT_SPEED = 500.0f;
    static constexpr float OPEN_GOAL_DEFENDER_RANGE = 2000.0f;

public:
    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;
        if (!state.prev) return 0.0f;

        // Get goal
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        // Check if goal is undefended (no defenders nearby)
        int defendersNear = 0;
        for (const auto& other : state.players) {
            if (other.team == player.team) continue;

            float distToGoal = (other.pos - opponentGoal).Length();
            if (distToGoal < OPEN_GOAL_DEFENDER_RANGE) {
                defendersNear++;
            }
        }

        // Only reward if truly open (0-1 defenders nearby)
        if (defendersNear > 1) return 0.0f;

        // Ball toward goal
        Vec ballVel = state.ball.vel;
        float ballSpeed = ballVel.Length();

        if (ballSpeed < MIN_SHOT_SPEED) return 0.0f;

        Vec toGoal = (opponentGoal - state.ball.pos).Normalized();
        float goalAlignment = ballVel.Normalized().Dot(toGoal);

        if (goalAlignment < 0.5f) return 0.0f;

        // High reward for open shots (should be easy)
        return 12.0f * goalAlignment;  // 0-12 reward
    }
};
