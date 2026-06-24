#pragma once
#include "Reward.h"
#include "../Math.h"
#include "../CommonValues.h"
#include <cmath>

namespace RLGC {

// ============================================================================
// ImpendingShotDefenseReward
// 2026-06-21 | Rev 1.0
//
// Rewards the closest defender to own goal for covering the shot angle when
// an opponent is about to shoot, or the ball is already on trajectory into
// our goal within the next 1.5 seconds.
//
// Two detection paths:
//   Path A: Ball physics projection crosses own goal mouth within SHOT_HORIZON.
//   Path B: Opponent is in contact range of ball, aimed toward our goal, and
//           the ball is within OPP_THREAT_DIST of our goal.
//
// NOTE: BallPred::Cache is intentionally NOT used here. BallPred bounces the
// ball off the back wall (treats it as solid) and would never show a goal.
// Instead we mirror RewardMath::BallIntersectsOpponentGoalMouth's approach —
// raw physics projection (p + v*t + 0.5*g*t^2) with goal-mouth AABB check.
//
// Block point geometry:
//   On the line from goal center → ball, at BLOCK_DIST (800 UU) in front of
//   the goal. This shifts automatically toward the ball's side — a wide-angle
//   shot causes the block point to slide toward the near post.
//
// Only fires for the player on our team closest to own goal. Stateless.
// ============================================================================
class ImpendingShotDefenseReward : public Reward {
    static constexpr float SHOT_HORIZON       = 1.5f;    // seconds for trajectory check
    static constexpr float VEL_TOWARD_GOAL_MIN = 100.0f; // min ball speed toward own goal
    static constexpr float OPP_CONTACT_DIST   = 350.0f;  // opponent within this = in contact range
    static constexpr float OPP_ALIGN_THRESH   = 500.0f;  // opponent vel toward our goal (UU/s)
    static constexpr float OPP_THREAT_DIST    = 4000.0f; // ball within this of own goal = threat zone
    static constexpr float BLOCK_DIST         = 800.0f;  // in front of goal center
    static constexpr float MAX_BLOCK_DIST     = 1800.0f; // reward → 0 beyond this from block point
    static constexpr float MAX_REWARD         = 0.5f;    // internal max; outer weight scales

    Vec GetOwnGoalCenter(Team team) const {
        return (team == Team::BLUE)
            ? CommonValues::BLUE_GOAL_CENTER
            : CommonValues::ORANGE_GOAL_CENTER;
    }

    // Check if current ball trajectory will cross own goal mouth within SHOT_HORIZON.
    // Uses the same linear+gravity projection as BallIntersectsOpponentGoalMouth
    // (RewardMath.h), adapted for own goal.
    bool BallWillEnterOwnGoal(const Player& player, const GameState& state) const {
        // -1 for Blue (own goal at y = -5120), +1 for Orange (own goal at y = +5120)
        const float goalSign  = (player.team == Team::BLUE) ? -1.0f : 1.0f;
        const float goalLineY = goalSign * CommonValues::BACK_WALL_Y; // ±5120

        const Vec& v0 = state.ball.vel;
        const Vec& p0 = state.ball.pos;

        // Ball must be moving toward own goal
        const float vyToward = -goalSign * v0.y; // positive when heading toward own goal
        if (vyToward < VEL_TOWARD_GOAL_MIN) return false;

        // Time to reach goal line (v0.y != 0 guaranteed since vyToward > 0)
        const float t = (goalLineY - p0.y) / v0.y;
        if (t <= 0.0f || t > SHOT_HORIZON) return false;

        // Projected position at goal line (gravity applied to Z)
        const float xAtGoal = p0.x + v0.x * t;
        const float zAtGoal = p0.z + v0.z * t + 0.5f * CommonValues::BALL_GRAVITY * t * t;

        // Goal mouth bounds (ball center must clear radius from posts/crossbar)
        const float r = CommonValues::BALL_RADIUS;
        return (std::abs(xAtGoal) <= CommonValues::GOAL_WIDTH_FROM_CENTER - r)
            && (zAtGoal >= r)
            && (zAtGoal <= CommonValues::GOAL_HEIGHT - r);
    }

    // Check if any opponent is in contact range of ball, aimed toward our goal,
    // and close enough to be a shooting threat.
    bool OpponentPositionedToShoot(const Player& player, const GameState& state) const {
        const Vec ownGoal = GetOwnGoalCenter(player.team);
        if (state.ball.pos.Dist(ownGoal) > OPP_THREAT_DIST) return false;

        Vec toGoal = ownGoal - state.ball.pos;
        const float dist = toGoal.Length();
        if (dist < 1.0f) return false;
        toGoal = toGoal * (1.0f / dist); // normalize

        for (const auto& opp : state.players) {
            if (opp.team == player.team || opp.isDemoed) continue;
            if (opp.pos.Dist(state.ball.pos) > OPP_CONTACT_DIST) continue;
            if (opp.vel.Dot(toGoal) >= OPP_ALIGN_THRESH) return true;
        }
        return false;
    }

    // True if this player is the closest living teammate to own goal.
    bool IsClosestTeammateToOwnGoal(const Player& player, const GameState& state) const {
        const Vec ownGoal = GetOwnGoalCenter(player.team);
        const float myDist = player.pos.Dist(ownGoal);
        for (const auto& tm : state.players) {
            if (tm.team != player.team || tm.carId == player.carId || tm.isDemoed) continue;
            if (tm.pos.Dist(ownGoal) < myDist) return false;
        }
        return true;
    }

public:
    virtual void Reset(const GameState&) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool) override {
        if (player.isDemoed) return 0.0f;

        // Shot must be detected via Path A or Path B
        if (!BallWillEnterOwnGoal(player, state) && !OpponentPositionedToShoot(player, state))
            return 0.0f;

        // Only the defender closest to own goal is rewarded
        if (!IsClosestTeammateToOwnGoal(player, state)) return 0.0f;

        // Block point: on the line from goal center toward ball, BLOCK_DIST out.
        // Shifts toward ball's side automatically — e.g., ball at wide angle → block
        // point shifts toward that post.
        const Vec ownGoal = GetOwnGoalCenter(player.team);
        Vec toBall = state.ball.pos - ownGoal;
        const float dist = toBall.Length();
        if (dist < 1.0f) return 0.0f;
        toBall = toBall * (1.0f / dist);             // direction: goal → ball
        const Vec blockPoint = ownGoal + toBall * BLOCK_DIST;

        const float distToBlock = player.pos.Dist(blockPoint);
        const float t = RS_CLAMP(1.0f - distToBlock / MAX_BLOCK_DIST, 0.0f, 1.0f);
        return MAX_REWARD * t;
    }
};

} // namespace RLGC
