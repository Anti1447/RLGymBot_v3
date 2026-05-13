#pragma once
#include "Reward.h"
#include "BallPrediction.h"
#include "RewardMath.h"
#include "../CommonValues.h"
#include <map>
#include <cmath>

namespace RLGC {

// ============================================================================
// AerialOutplayReward
// Teaches tactical awareness during aerial duels
// - Rewards getting the ball past opponent(s) instead of hitting blindly
// - Fallback: reward contested 50/50s if outplay isn't possible
// - Secondary: reward ball direction quality (toward goal / away from defense)
//
// Only activates in 2v2+ modes in Diamond+ phases when aerial technique
// is already established. Complements PitchControlAerialReward (technique).
// ============================================================================
class AerialOutplayReward : public Reward {
private:
    // Per-player aerial state tracking
    struct AerialState {
        bool inAerial = false;           // Currently in an aerial engagement
        bool hasOpponent = false;        // Opponent also airborne nearby
        uint32_t closestOppCarId = UINT32_MAX;  // Closest opponent in air
    };

    std::map<uint32_t, AerialState> trackers;
    BallPred::Cache ballCache;

    // Constants
    static constexpr float AERIAL_MIN_AIRTIME = 0.15f;        // Must be in air for 150ms
    static constexpr float AERIAL_MIN_HEIGHT = 400.0f;        // Must be above 400 UU
    static constexpr float OPPONENT_ENGAGEMENT_DIST = 1500.0f;  // How close opponent must be
    static constexpr float TRAJECTORY_HORIZON = 0.75f;        // Look 0.75s ahead for trajectory
    static constexpr float PLAYER_REACH_RADIUS = 200.0f;      // Approx player size
    static constexpr float OUTPLAY_BONUS = 0.6f;              // Bonus for getting ball past
    static constexpr float FIFTY_FIFTY_BONUS = 0.3f;          // Bonus for contested 50/50
    static constexpr float FIFTY_FIFTY_DIST = 600.0f;         // Distance threshold for 50/50
    static constexpr float FIFTY_FIFTY_VEL = 400.0f;          // Velocity threshold for 50/50
    static constexpr float GOAL_DIRECTION_VEL = 300.0f;       // Velocity toward goal threshold
    static constexpr float DEFENSE_AWAY_VEL = 200.0f;         // Velocity away from defense threshold
    static constexpr float GOAL_DIRECTION_BONUS = 0.2f;
    static constexpr float DEFENSE_AWAY_BONUS = 0.1f;

public:
    void Reset(const GameState& initialState) override {
        trackers.clear();
    }

    void PreStep(const GameState& state) override {
        ballCache.Update(state);
    }

    float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Only reward in 2v2+ (multiplayer aerials)
        if (state.players.size() < 4) {
            return 0.0f;
        }

        auto& tracker = trackers[player.carId];
        float reward = 0.0f;

        // Step 1: Detect if in aerial engagement
        bool isAerial = IsAerialEngagement(player, state);

        if (!isAerial) {
            tracker.inAerial = false;
            return 0.0f;
        }

        // Mark as in aerial (stateless per-frame, okay to trigger every frame while airborne)
        if (!tracker.inAerial) {
            tracker.inAerial = true;
        }

        // Step 2a: Check if we can get past opponents (highest priority)
        if (CanGetPastOpponents(player, state)) {
            reward += OUTPLAY_BONUS;
        }
        // Step 2b: Fallback to 50/50 if can't get past
        else if (IsInFiftyFifty(player, state)) {
            reward += FIFTY_FIFTY_BONUS;
        }

        // Step 3: Always score ball direction quality
        float directionBonus = GetBallDirectionQuality(player, state);
        reward += directionBonus;

        return reward;
    }

private:
    // Detect if player is in aerial engagement with opponent nearby
    bool IsAerialEngagement(const Player& player, const GameState& state) const {
        // Player must be airborne
        if (player.isOnGround || player.airTime < AERIAL_MIN_AIRTIME) {
            return false;
        }
        if (player.pos.z < AERIAL_MIN_HEIGHT) {
            return false;
        }

        // Must have an opponent in air and close to ball
        float closestOppDist = 1e10f;
        bool foundOpponent = false;

        for (const auto& opp : state.players) {
            // Skip teammates and self
            if (opp.team == player.team || opp.carId == player.carId) {
                continue;
            }

            // Opponent must be airborne
            if (opp.isOnGround || opp.airTime < AERIAL_MIN_AIRTIME) {
                continue;
            }
            if (opp.pos.z < AERIAL_MIN_HEIGHT) {
                continue;
            }

            // Check distance to ball
            float oppDistToBall = (opp.pos - state.ball.pos).Length();
            if (oppDistToBall < OPPONENT_ENGAGEMENT_DIST && oppDistToBall < closestOppDist) {
                closestOppDist = oppDistToBall;
                foundOpponent = true;
            }
        }

        return foundOpponent;
    }

    // Check if ball trajectory passes outside all opponent reach cones
    bool CanGetPastOpponents(const Player& player, const GameState& state) const {
        // Project ball position TRAJECTORY_HORIZON seconds ahead
        Vec ballPos = state.ball.pos;
        Vec ballVel = state.ball.vel;

        // Simple linear projection (gravity negligible for small horizon)
        Vec projectedBallPos = ballPos + ballVel * TRAJECTORY_HORIZON;

        // Check if any opponent can reach the projected position
        for (const auto& opp : state.players) {
            if (opp.team == player.team || opp.carId == player.carId) {
                continue;
            }

            // Can opponent reach the projected ball position?
            float distToProjected = (opp.pos - projectedBallPos).Length();
            float oppReachDist = (opp.pos - ballPos).Length() +
                                 (opp.vel * TRAJECTORY_HORIZON).Length() +
                                 PLAYER_REACH_RADIUS;

            // If opponent can't reach projected position, we got past them
            if (distToProjected > oppReachDist) {
                // At least one opponent can't reach it — good sign
                // But check if ball is heading toward goal
                Vec oppGoal = (player.team == Team::BLUE)
                    ? CommonValues::ORANGE_GOAL_CENTER
                    : CommonValues::BLUE_GOAL_CENTER;
                Vec toGoal = oppGoal - ballPos;
                float ballTowardGoal = ballVel.Dot(SafeNorm(toGoal));

                if (ballTowardGoal > 100.0f) {
                    return true;  // Ball going past AND toward goal
                }
            }
        }

        return false;
    }

    // Check if in a contested 50/50 (strict: BOTH distance AND velocity required)
    bool IsInFiftyFifty(const Player& player, const GameState& state) const {
        float closestOppDist = 1e10f;
        float closestOppVelToBall = 0.0f;

        for (const auto& opp : state.players) {
            if (opp.team == player.team || opp.carId == player.carId) {
                continue;
            }

            float oppDistToBall = (opp.pos - state.ball.pos).Length();
            if (oppDistToBall < closestOppDist) {
                closestOppDist = oppDistToBall;

                // Calculate opponent velocity toward ball
                Vec dirToBall = SafeNorm(state.ball.pos - opp.pos);
                closestOppVelToBall = std::max(0.0f, opp.vel.Dot(dirToBall));
            }
        }

        // BOTH conditions required (strict)
        bool distanceGood = closestOppDist < FIFTY_FIFTY_DIST;
        bool velocityGood = closestOppVelToBall > FIFTY_FIFTY_VEL;

        return distanceGood && velocityGood;
    }

    // Score ball direction quality (toward goal and away from defense)
    float GetBallDirectionQuality(const Player& player, const GameState& state) const {
        float directionScore = 0.0f;

        Vec oppGoal = (player.team == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_CENTER
            : CommonValues::BLUE_GOAL_CENTER;
        Vec ownGoal = (player.team == Team::BLUE)
            ? CommonValues::BLUE_GOAL_CENTER
            : CommonValues::ORANGE_GOAL_CENTER;

        // Bonus for ball velocity toward opponent goal
        Vec toOppGoal = SafeNorm(oppGoal - state.ball.pos);
        float ballTowardGoal = state.ball.vel.Dot(toOppGoal);
        if (ballTowardGoal > GOAL_DIRECTION_VEL) {
            directionScore += GOAL_DIRECTION_BONUS;
        }

        // Bonus for ball velocity away from own goal
        Vec toOwnGoal = SafeNorm(ownGoal - state.ball.pos);
        float ballAwayFromOwn = -state.ball.vel.Dot(toOwnGoal);  // Negative = away
        if (ballAwayFromOwn > DEFENSE_AWAY_VEL) {
            directionScore += DEFENSE_AWAY_BONUS;
        }

        return directionScore;
    }

    // Safe normalize vector
    static inline Vec SafeNorm(const Vec& v) {
        const float len = v.Length();
        return (len > 1e-6f) ? (v * (1.0f / len)) : Vec(0, 0, 0);
    }
};

}  // namespace RLGC
