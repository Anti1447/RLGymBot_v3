// RolvVelocityPlayerToBallRewards.h
// C++ port of rlgym-tools reward functions by Rolv-Arild.
// Source: https://github.com/RLGym/rlgym-tools/blob/main/rlgym_tools/rocket_league/reward_functions/velocity_player_to_ball_reward.py
// Port: NJH (2026-05-21)
//
// Three variants of velocity-toward-ball reward, each with different closing-speed formulas.
// All variants are normalized so 1.0 = maximum closing speed sustained for 1 second.
// All accept includeNegativeValues=false to zero-clamp reward when moving away from ball.
//
// NOTE: The base class is named RolvVelocityPlayerToBallReward (not VelocityPlayerToBallReward)
// to avoid a name collision with the existing simpler class in CommonRewards.h.
//
// =============================================================================
// WHICH VARIANT TO USE — NOTES
// =============================================================================
//
// All three measure "how fast is the car closing on the ball" but use different
// formulas. DO NOT activate more than one at a time — they are redundant signals
// measuring the same thing. Using all three simultaneously triple-counts the
// "chase ball" gradient and causes dilution against GoalReward etc.
//
// RolvVelocityPlayerToBallReward (base)
//   Formula: (normalize(ball - car)) · car_vel / CAR_MAX_SPEED
//   Question answered: "How fast is the car moving toward where the ball IS RIGHT NOW?"
//   Ball velocity: IGNORED.
//   Edge case: ball rolling perpendicular at 2000 UU/s → full reward even though
//     the car would miss entirely if it kept going straight.
//   When to use: identical formula to the existing VelocityPlayerToBallReward in
//     CommonRewards.h (just dt-normalized). If that reward is already active,
//     this one is fully redundant — don't add it.
//
// DotQuotientVPBReward
//   Formula: (dir · car_vel) / (dir · ball_vel) / (CAR_MAX_SPEED / BALL_RADIUS)
//   Question answered: "How fast is the car catching up RELATIVE TO how fast the
//     ball is running away?"
//   Key quirk: when the ball is STATIONARY, dd ≈ 0 → reward = 0 regardless of
//     car speed. A bot chasing a stationary ball gets ZERO reward from this class.
//   When to use: only if you specifically want to train "catch a moving ball"
//     and are comfortable with the stationary-ball-gives-zero behavior. Not
//     recommended for general ball-chasing phases.
//
// TrajectoryComparisonVPBReward  ← best general-purpose upgrade
//   Formula: (currDist - minDist) / t / (CAR_MAX_SPEED + BALL_MAX_SPEED)
//   Question answered: "If both car and ball keep moving in straight lines, how
//     fast will the gap actually close?"
//   Ball velocity: ACCOUNTED FOR — the bot is rewarded for true interception
//     paths, not just moving toward the ball's current position.
//   Negative t: valid and intentional — when trajectories are diverging, t < 0
//     gives a negative reward, correctly signaling the bot is going the wrong way.
//   When to use: best candidate to REPLACE the existing VelocityPlayerToBallReward
//     if you want a more physically accurate signal. More expensive to compute
//     than the base but still O(1) per step.
//
// Summary table:
//   Variant                        Ball vel?  Stationary ball  Recommended?
//   RolvVelocityPlayerToBallReward   No        Full reward      Redundant if VPB already active
//   DotQuotientVPBReward             Yes       Zero reward      Quirky — avoid unless intentional
//   TrajectoryComparisonVPBReward    Yes       Correct reward   Yes, if replacing existing VPB
// =============================================================================

#pragma once
#include "Reward.h"
#include "../CommonValues.h"
#include <cmath>
#include <limits>

namespace RLGC {

// ============================================================================
// RolvVelocityPlayerToBallReward
//
// Rewards the agent based on its velocity component toward the ball.
// Normalized so max speed toward ball = 1.0 reward per second of game time.
// Moving away yields negative reward unless includeNegativeValues = false.
// ============================================================================
class RolvVelocityPlayerToBallReward : public Reward {
protected:
    bool includeNegativeValues;

    virtual float InnerReward(Vec carPos, Vec carVel, Vec ballPos, Vec ballVel) {
        Vec carToBall = ballPos - carPos;
        float dist = carToBall.Length();
        if (dist == 0.0f) return 0.0f;
        carToBall = carToBall / dist;                   // normalize direction
        float vel = carToBall.Dot(carVel);
        return vel / CommonValues::CAR_MAX_SPEED;
    }

public:
    RolvVelocityPlayerToBallReward(bool includeNegativeValues = true)
        : includeNegativeValues(includeNegativeValues) {}

    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
        float reward = InnerReward(
            player.pos, player.vel,
            state.ball.pos, state.ball.vel
        ) * dt;
        if (!includeNegativeValues)
            reward = RS_MAX(0.0f, reward);
        return reward;
    }
};

// ============================================================================
// DotQuotientVPBReward
//
// Vector form of inverse-time-to-ball: (car_vel · dir) / (ball_vel · dir),
// divided by (CAR_MAX_SPEED / BALL_RADIUS). Accounts for ball movement — if
// the ball is already moving away at the same speed as the car is chasing,
// this returns 0. Max value ≈ CAR_MAX_SPEED / BALL_RADIUS ≈ 24.8 before
// normalization.
// ============================================================================
class DotQuotientVPBReward : public RolvVelocityPlayerToBallReward {
protected:
    virtual float InnerReward(Vec carPos, Vec carVel, Vec ballPos, Vec ballVel) override {
        Vec carToBall = ballPos - carPos;
        float dist = carToBall.Length();
        if (dist == 0.0f) return 0.0f;
        carToBall = carToBall / dist;                   // normalize direction

        float vd = carToBall.Dot(carVel);               // car closing speed along dir
        float dd = carToBall.Dot(ballVel);              // ball speed along same dir
        float invTime = (std::abs(dd) > 1e-6f) ? (vd / dd) : 0.0f;
        return invTime / (CommonValues::CAR_MAX_SPEED / CommonValues::BALL_RADIUS);
    }

public:
    DotQuotientVPBReward(bool includeNegativeValues = true)
        : RolvVelocityPlayerToBallReward(includeNegativeValues) {}
};

// ============================================================================
// TrajectoryComparison helper
//
// Given two linear trajectories (pos1 + t*vel1) and (pos2 + t*vel2), finds
// the time t at which the distance between them is minimized, then returns:
//   currDist — current distance between the two objects
//   minDist  — minimum distance achievable along the trajectories
//   t        — time (seconds) at which minDist occurs
//             (may be negative if trajectories are currently diverging)
//
// maxTime is bounded by the field walls so unrealistic long-horizon predictions
// are not included. Matches the Python implementation's numpy semantics exactly,
// including the zero-velocity trick (replace 0 velocity components with 1.0
// to avoid division-by-zero while preserving the filter on negative times).
// ============================================================================
struct TrajectoryResult { float currDist; float minDist; float t; };

inline TrajectoryResult TrajectoryComparison(
    Vec pos1, Vec vel1,
    Vec pos2, Vec vel2,
    bool checkBounds = true)
{
    using namespace CommonValues;
    float maxTime = std::numeric_limits<float>::infinity();

    if (checkBounds) {
        // For each object find the minimum positive time until it crosses any wall.
        // Zero velocity components are replaced with 1.0 (mirrors Python's
        // `vel + (vel == 0)`) — this produces large but finite times for static
        // axes so they don't incorrectly cap maxTime.
        const float posData[2][3] = {
            {pos1.x, pos1.y, pos1.z},
            {pos2.x, pos2.y, pos2.z}
        };
        const float velData[2][3] = {
            {vel1.x, vel1.y, vel1.z},
            {vel2.x, vel2.y, vel2.z}
        };
        const float boundsLo[3] = { -SIDE_WALL_X, -BACK_WALL_Y, 0.f };
        const float boundsHi[3] = {  SIDE_WALL_X,  BACK_WALL_Y, CEILING_Z };

        for (int obj = 0; obj < 2; ++obj) {
            float objMinTime = std::numeric_limits<float>::infinity();
            for (int axis = 0; axis < 3; ++axis) {
                float v = velData[obj][axis];
                float p = posData[obj][axis];
                float sv = (v == 0.0f) ? 1.0f : v;     // avoid /0, match Python
                float tLo = (boundsLo[axis] - p) / sv;
                float tHi = (boundsHi[axis] - p) / sv;
                if (tLo > 0.0f) objMinTime = RS_MIN(objMinTime, tLo);
                if (tHi > 0.0f) objMinTime = RS_MIN(objMinTime, tHi);
            }
            maxTime = RS_MIN(maxTime, objMinTime);
        }
    }

    // Distance² between trajectories = ||posDiff + t*velDiff||²
    // Minimum occurs at t = -(posDiff · velDiff) / (velDiff · velDiff)
    Vec posDiff = pos1 - pos2;
    Vec velDiff = vel1 - vel2;
    float denom = velDiff.Dot(velDiff);
    float optT  = (denom == 0.0f) ? 0.0f : (-posDiff.Dot(velDiff) / denom);

    // Clamp to field lifetime (but do NOT clamp to ≥0 — negative t is valid
    // and correctly produces negative reward when trajectories are diverging)
    if (optT > maxTime) optT = maxTime;

    float currDist = posDiff.Length();
    float minDist  = (posDiff + velDiff * optT).Length();

    return { currDist, minDist, optT };
}

// ============================================================================
// TrajectoryComparisonVPBReward
//
// Computes effective closing velocity by comparing the linear trajectories of
// the car and ball. Returns (currDist - minDist) / t, normalized by
// (CAR_MAX_SPEED + BALL_MAX_SPEED). Unlike the base reward, this accounts
// for the ball's future motion when measuring how quickly the gap closes.
//
// Result is clamped to [-1, 1] to handle floating-point edge cases at small t.
// ============================================================================
class TrajectoryComparisonVPBReward : public RolvVelocityPlayerToBallReward {
protected:
    virtual float InnerReward(Vec carPos, Vec carVel, Vec ballPos, Vec ballVel) override {
        TrajectoryResult r = TrajectoryComparison(carPos, carVel, ballPos, ballVel);
        float vel    = (std::abs(r.t) > 1e-6f) ? ((r.currDist - r.minDist) / r.t) : 0.0f;
        float normVel = vel / (CommonValues::CAR_MAX_SPEED + CommonValues::BALL_MAX_SPEED);
        return RS_CLAMP(normVel, -1.0f, 1.0f);
    }

public:
    TrajectoryComparisonVPBReward(bool includeNegativeValues = true)
        : RolvVelocityPlayerToBallReward(includeNegativeValues) {}
};

} // namespace RLGC
