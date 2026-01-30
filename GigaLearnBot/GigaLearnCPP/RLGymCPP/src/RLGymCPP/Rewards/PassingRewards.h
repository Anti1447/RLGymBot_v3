#pragma once
#include "Reward.h"
#include "../Math.h"

// ============================================================================
// PASSING REWARDS - Progressive system from Bronze to SSL
// ============================================================================
// Key concepts:
// 1. Bronze-Diamond: Reward back passes (safe, simple)
// 2. Gold-Plat: Reward forward passes with positioning checks
// 3. Diamond+: Reward team positioning and pass reception
//
// References: Passing Guide by Rank (Bronze-SSL)
// ============================================================================

// ============================================================================
// BackPassReward - Diamond/SSL safe passing
// Rewards passes backward to teammates in recovery or rotation
// Key: Safe, maintains possession, minimal risk
// ============================================================================
class BackPassReward : public Reward {
private:
    mutable std::map<uint32_t, Vec> lastTouchPos;

public:
    virtual void Reset(const GameState& initialState) override {
        lastTouchPos.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;

        // Store touch position for this player
        lastTouchPos[player.carId] = player.pos;

        if (!state.prev) return 0.0f;

        // Check if ball moved backward (toward own goal)
        Vec ballMovement = state.ball.pos - state.prev->ball.pos;
        Vec ownGoal = (player.team == Team::BLUE) ?
            CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

        Vec toOwnGoal = (ownGoal - player.pos).Normalized();

        // Back pass = moving toward own goal after touch
        float backPassAlignment = ballMovement.Normalized().Dot(toOwnGoal);

        if (backPassAlignment > 0.5f) {
            // Check if there's a teammate behind to receive
            float minTeammateDistance = 999999.0f;
            for (const auto& other : state.players) {
                if (other.carId == player.carId || other.team != player.team) continue;

                float dist = (other.pos - player.pos).Length();
                // Teammate should be behind player
                Vec toTeammate = (other.pos - player.pos).Normalized();
                float alignment = toTeammate.Dot(toOwnGoal);

                if (alignment > 0.3f) {  // Behind enough
                    minTeammateDistance = RS_MIN(minTeammateDistance, dist);
                }
            }

            if (minTeammateDistance < 2000.0f) {  // Reasonable back pass distance
                float distance_factor = 1.0f - RS_CLAMP(minTeammateDistance / 2000.0f, 0.0f, 1.0f);
                return 5.0f * distance_factor;  // 0-5 reward for safe back pass
            }
        }

        return 0.0f;
    }
};

// ============================================================================
// ForwardPassReward - Gold+ forward/setup passes
// Rewards passes that move ball toward opponent goal with teammate positioning
// Key: Requires teammate awareness, direction control
// ============================================================================
class ForwardPassReward : public Reward {
private:
    static constexpr float MIN_PASS_DISTANCE = 500.0f;
    static constexpr float MAX_PASS_DISTANCE = 3000.0f;
    static constexpr float TEAMMATE_POSITIONING_RANGE = 2500.0f;

public:
    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;
        if (!state.prev) return 0.0f;

        // Check forward direction (toward opponent goal)
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        Vec toGoal = (opponentGoal - player.pos).Normalized();

        // Ball movement analysis
        Vec ballMovement = (state.ball.pos - state.prev->ball.pos).Normalized();
        float forwardAlignment = ballMovement.Dot(toGoal);

        // Must move forward
        if (forwardAlignment < 0.3f) return 0.0f;

        // Ball must travel a meaningful distance
        float ballDistance = (state.ball.pos - state.prev->ball.pos).Length();
        if (ballDistance < MIN_PASS_DISTANCE) return 0.0f;

        // Check teammate positioning for pass reception
        float bestTeammateScore = 0.0f;
        for (const auto& other : state.players) {
            if (other.carId == player.carId || other.team != player.team) continue;

            // Teammate ahead of passer
            Vec toTeammate = (other.pos - player.pos);
            float distanceToTeammate = toTeammate.Length();

            if (distanceToTeammate < 0.1f) continue;  // Too close

            float aheadAlignment = toTeammate.Normalized().Dot(toGoal);

            // Teammate in good position (ahead, in forward direction)
            if (aheadAlignment > 0.4f && distanceToTeammate < TEAMMATE_POSITIONING_RANGE) {
                // Score based on positioning
                float positionScore = aheadAlignment * (1.0f - RS_CLAMP(distanceToTeammate / TEAMMATE_POSITIONING_RANGE, 0.0f, 1.0f));
                bestTeammateScore = RS_MAX(bestTeammateScore, positionScore);
            }
        }

        // Reward only if teammate is well-positioned
        if (bestTeammateScore > 0.2f) {
            float distance_factor = 1.0f - RS_CLAMP((ballDistance - MIN_PASS_DISTANCE) / (MAX_PASS_DISTANCE - MIN_PASS_DISTANCE), 0.0f, 1.0f);
            return 8.0f * bestTeammateScore * distance_factor;  // 0-8 reward
        }

        return 0.0f;
    }
};

// ============================================================================
// HighPassReward - Plat+ advanced passes (lobs, aerials)
// Rewards high-ball passes that create scoring opportunities
// Key: Ball height + teammate positioning + angle toward goal
// ============================================================================
class HighPassReward : public Reward {
private:
    static constexpr float HIGH_BALL_THRESHOLD = 200.0f;  // Ball above this height
    static constexpr float SCORING_ANGLE_THRESHOLD = 45.0f;  // Degrees toward goal

public:
    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;
        if (!state.prev) return 0.0f;

        // Ball must be elevated after touch
        float heightGain = state.ball.pos.z - state.prev->ball.pos.z;
        if (heightGain < 50.0f) return 0.0f;  // Must lift ball significantly

        if (state.ball.pos.z < HIGH_BALL_THRESHOLD) return 0.0f;

        // Check passing direction toward goal
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        Vec ballVel = state.ball.vel.Normalized();
        Vec toGoal = (opponentGoal - state.ball.pos).Normalized();
        float goalAlignment = ballVel.Dot(toGoal);

        if (goalAlignment < 0.5f) return 0.0f;  // Ball must go toward goal

        // Check teammate positioning for aerial reception
        float bestAerialPositionScore = 0.0f;
        for (const auto& other : state.players) {
            if (other.carId == player.carId || other.team != player.team) continue;

            // Can teammate hit this aerial?
            Vec toTeammate = (other.pos - state.ball.pos);
            float distanceToAerialBall = toTeammate.Length();

            // Teammate should be within aerial range
            if (distanceToAerialBall < 1500.0f && distanceToAerialBall > 200.0f) {
                // Bonus if teammate is aerial-capable (positioned high/mid)
                float heightAlignment = RS_MAX(0.0f, other.pos.z - state.ball.pos.z);  // Teammate below ball
                float positionScore = (1.0f - RS_CLAMP(distanceToAerialBall / 1500.0f, 0.0f, 1.0f)) *
                                    RS_CLAMP(heightAlignment / 500.0f, 0.0f, 1.0f);
                bestAerialPositionScore = RS_MAX(bestAerialPositionScore, positionScore);
            }
        }

        if (bestAerialPositionScore > 0.2f) {
            return 12.0f * bestAerialPositionScore;  // 0-12 reward for high passes
        }

        return 0.0f;
    }
};

// ============================================================================
// OffensivePlayReward - SSL setup passes leading to direct scoring chances
// Rewards creating immediate scoring opportunities through passes
// Key: Teammate in striking position + undefended + high ball speed
// ============================================================================
class OffensivePlayReward : public Reward {
private:
    static constexpr float GOAL_PROXIMITY = 2000.0f;  // Close to enemy goal
    static constexpr float MIN_BALL_SPEED = 500.0f;

public:
    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;
        if (!state.prev) return 0.0f;

        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        // Pass must result in significant ball movement
        float ballSpeed = state.ball.vel.Length();
        if (ballSpeed < MIN_BALL_SPEED) return 0.0f;

        // Check for teammate in striking position
        float bestStrikingScore = 0.0f;
        for (const auto& other : state.players) {
            if (other.carId == player.carId || other.team != player.team) continue;

            float distToGoal = (other.pos - opponentGoal).Length();

            // Teammate close to enemy goal (striking position)
            if (distToGoal < GOAL_PROXIMITY) {
                // Check if opponent is blocking
                int blockingOpponents = 0;
                for (const auto& opp : state.players) {
                    if (opp.team == player.team) continue;
                    Vec toTeammate = (other.pos - opp.pos);
                    if (toTeammate.Length() < 500.0f) blockingOpponents++;
                }

                // Undefended or outnumbering defenders
                if (blockingOpponents <= 1) {
                    float positionScore = 1.0f - RS_CLAMP(distToGoal / GOAL_PROXIMITY, 0.0f, 1.0f);
                    bestStrikingScore = RS_MAX(bestStrikingScore, positionScore);
                }
            }
        }

        if (bestStrikingScore > 0.3f) {
            return 15.0f * bestStrikingScore;  // 0-15 reward for offensive setups
        }

        return 0.0f;
    }
};
