#pragma once
#include "Reward.h"
#include "BallPrediction.h"
#include "../Math.h"
#include "../CommonValues.h"

namespace RLGC {

// Continuous shaping: rewards the friendly bot closest to the most dangerous opponent
// interceptor when the ball is approaching the opponent's net.
//
// Scenario: ball is moving toward opponent goal, an opponent is between the ball and
// their own goal (a potential interceptor). We find the 3 most likely ball positions
// that opponent could reach, then reward our closest bot for flying at any of them
// at high speed — demoing or bumping the opponent before they can touch the ball.
//
// Complements DemoLeadApproachReward (general approach) — this fires only in
// offensive attacking sequences where an opponent can intercept our scoring chance.
// Works on the ground and in the air — ground demos are more common in practice.
// Rev 1.1 2026-06-03 NJH
class DemoInterceptLeadReward : public Reward {
private:
    static constexpr float MIN_SPEED             = 1400.0f;  // UU/s — start rewarding above here
    static constexpr float MAX_SPEED             = 2300.0f;  // UU/s — full reward at supersonic
    static constexpr float MIN_BALL_GOAL_VEL     = 300.0f;   // UU/s — ball Y-vel toward opp goal
    static constexpr float MIN_ALIGNMENT         = 0.60f;    // dot product — aimed at intercept point
    static constexpr float REWARD_PER_SEC        = 3.0f;     // reward rate at full speed + alignment
    static constexpr float MAX_OPPONENT_RANGE    = 4000.0f;  // UU — max dist from ball to opp interceptor
    static constexpr float OPP_REACH_SPEED_FLOOR = 1200.0f;  // UU/s — min assumed opp speed (boosting)
    static constexpr float INTERCEPT_TIME_BUFFER = 0.10f;    // seconds — reaction time buffer for opp

    BallPred::Cache _ballCache;

    // True if the opponent is between the ball and their own goal (Y-axis).
    // Orange opp is an interceptor when closer to orange goal (+5120) than the ball.
    // Blue opp is an interceptor when closer to blue goal (-5120) than the ball.
    static bool IsOpponentInterceptor(const Player& opp, const GameState& state) {
        return (opp.team == Team::ORANGE)
            ? (opp.pos.y > state.ball.pos.y)
            : (opp.pos.y < state.ball.pos.y);
    }

public:
    virtual void Reset(const GameState& initialState) override {
        _ballCache.Update(initialState);
    }

    virtual void PreStep(const GameState& state) override {
        _ballCache.Update(state);
    }

    virtual float GetReward(const Player& player, const GameState& state, bool) override {
        if (player.isDemoed) return 0.0f;

        float speed = player.vel.Length();
        if (speed < MIN_SPEED) return 0.0f;

        // Ball must be moving toward the opponent's goal (attacker's perspective)
        float ballGoalVel = (player.team == Team::BLUE) ? state.ball.vel.y : -state.ball.vel.y;
        if (ballGoalVel < MIN_BALL_GOAL_VEL) return 0.0f;

        // Ball must be past midfield toward opponent goal
        bool ballInOppHalf = (player.team == Team::BLUE)
            ? (state.ball.pos.y > 0.0f)
            : (state.ball.pos.y < 0.0f);
        if (!ballInOppHalf) return 0.0f;

        // Find closest-to-ball opponent that is an interceptor (behind ball toward own goal)
        const Player* target = nullptr;
        float minDistToBall = MAX_OPPONENT_RANGE;
        for (const auto& opp : state.players) {
            if (opp.team == player.team || opp.isDemoed) continue;
            if (!IsOpponentInterceptor(opp, state)) continue;
            float d = (opp.pos - state.ball.pos).Length();
            if (d < minDistToBall) {
                minDistToBall = d;
                target = &opp;
            }
        }
        if (!target) return 0.0f;

        // Only the friendly closest to the target receives reward (1 bot per scenario)
        const Player* closestFriendly = nullptr;
        float minDistToTarget = 1e9f;
        for (const auto& p : state.players) {
            if (p.team != player.team || p.isDemoed) continue;
            float d = (p.pos - target->pos).Length();
            if (d < minDistToTarget) {
                minDistToTarget = d;
                closestFriendly = &p;
            }
        }
        if (!closestFriendly || closestFriendly->carId != player.carId) return 0.0f;

        // Predict up to 3 ball positions the opponent could plausibly intercept
        float oppReachSpeed = RS_MAX(target->vel.Length(), OPP_REACH_SPEED_FLOOR);
        Vec interceptPoints[3];
        int numPoints = 0;

        for (int i = 0; i < BallPred::NUM_SLICES && numPoints < 3; i++) {
            const BallPred::BallSlice& slice = _ballCache.slices[i];
            float timeNeeded = (slice.pos - target->pos).Length() / oppReachSpeed;
            if (timeNeeded <= slice.t + INTERCEPT_TIME_BUFFER) {
                interceptPoints[numPoints++] = slice.pos;
            }
        }
        if (numPoints == 0) return 0.0f;

        // Reward if aligned to any of the 3 intercept points
        Vec myVelN = player.vel.Normalized();
        float bestAlignment = 0.0f;
        for (int i = 0; i < numPoints; i++) {
            Vec toPoint = (interceptPoints[i] - player.pos).Normalized();
            float alignment = myVelN.Dot(toPoint);
            if (alignment > bestAlignment) bestAlignment = alignment;
        }
        if (bestAlignment < MIN_ALIGNMENT) return 0.0f;

        float speedFactor = RS_CLAMP((speed - MIN_SPEED) / (MAX_SPEED - MIN_SPEED), 0.0f, 1.0f);
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

        return REWARD_PER_SEC * speedFactor * bestAlignment * dt;
    }
};

} // namespace RLGC
