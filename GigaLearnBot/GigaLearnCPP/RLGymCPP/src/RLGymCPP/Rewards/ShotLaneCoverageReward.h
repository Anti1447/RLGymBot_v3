#pragma once
#include "Reward.h"
#include "../Math.h"
#include "../CommonValues.h"
#include <atomic>

namespace RLGC {

// ============================================================================
// ShotLaneCoverageReward
// 2026-07-24 | Rev 1.0 | NJH
//
// Team-level, continuous, outcome-based defensive positioning reward. Unlike
// UncoveredNetPenalty (fixed-radius sphere around the goal) this measures
// whether a teammate's body is actually occluding the shot lane from the ball
// to the goal mouth, at the depth that teammate happens to occupy. No zones,
// no slots, no fixed "correct" position — a teammate anywhere along the lane,
// at any distance from goal, earns credit proportional to how well they block
// it. This is a deliberate alternative to hardcoded field-zone rewards
// (AntiLayeringPenalty, DefensiveZoneOverlapPenalty, NoMansLandPenalty,
// OverflipPenalty, DiagonalZoneCoverageReward) which encode a specific human
// idea of rotation shape — this reward instead rewards the *effect* (lane
// occluded) and leaves the *shape* (where exactly to stand, when to rotate)
// for the bot to discover.
//
// Geometry (approximation, not exact solid-angle projection):
//   The shot lane from the ball to the own goal is modeled as a triangle that
//   is a point at the ball and widens to the full goal-mouth width
//   (GOAL_WIDTH_FROM_CENTER, ~893uu each side of center) at the goal line.
//   For a teammate at depth t (0 = at the ball's Y, 1 = at the goal line),
//   the lane center is lerp(ball.x, 0, t) and the lane half-width is
//   lerp(CAR_RADIUS, GOAL_WIDTH_FROM_CENTER, t) — CAR_RADIUS is a body-width
//   floor so the lane isn't literally zero-width right next to the ball.
//   Coverage for that teammate = clamp(1 - lateralOffset / halfWidth, 0, 1).
//
// Team reward = max coverage across all non-demoed teammates, given
// identically to every teammate (same convention as
// BallSideNumericalAdvantageReward) — this doesn't attribute credit to
// whichever specific player did the covering, only to the team having
// achieved it, so the policy is free to decide who rotates back and when.
// Because only the single best-covering teammate's value is used, a second
// player standing next to the first earns no additional reward — the reward
// is self-limiting against clustering without any explicit anti-stacking rule.
//
// Gates (all must be true):
//   1. 2v2+ only (state.players.size() >= 4)
//   2. Not kickoff (ball.pos.Length() >= 200 || ball.vel.Length() >= 150)
//   3. Team is defending (ball closer to own goal than opponent goal)
//   4. Ball moving toward own goal at > THREAT_VEL UU/s (genuine threat,
//      matches UncoveredNetPenalty's gate so both fire on the same scenarios)
//
// Complements (not duplicates):
//   - UncoveredNetPenalty: team-level sphere-around-goal proximity check —
//     coarser, doesn't account for lateral offset from the actual shot line
//   - LastManOvercommitPenalty: individual, penalizes rushing the ball
//   - GoalsideReward: individual, Y-only margin between ball and own goal
//   - BallSideNumericalAdvantageReward: headcount-based, not geometry-based
//
// W&B: shot_lane_coverage_frac (mean coverage across gated steps),
//      shot_lane_uncovered_frac (fraction of gated steps with coverage == 0)
// Tuning: if coverage saturates near 1.0 immediately, suspect a gating bug
//   rather than learned behavior. If it never rises above ~0, check that
//   CAR_RADIUS isn't too small relative to typical defensive spacing.
// ============================================================================
class ShotLaneCoverageReward : public Reward {
    static constexpr float THREAT_VEL = 200.0f;   // min ball vel toward goal (UU/s), matches UncoveredNetPenalty
    static constexpr float CAR_RADIUS = 120.0f;   // body-width floor for lane half-width at t=0

    inline static std::atomic<int>   s_gatedSteps{0};
    inline static std::atomic<int>   s_uncoveredSteps{0};
    inline static std::atomic<float> s_coverageSum{0.0f};

    Vec GetOwnGoal(Team team) const {
        return (team == Team::BLUE)
            ? CommonValues::BLUE_GOAL_CENTER
            : CommonValues::ORANGE_GOAL_CENTER;
    }

    Vec GetOppGoal(Team team) const {
        return (team == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_CENTER
            : CommonValues::BLUE_GOAL_CENTER;
    }

    // Coverage this single player provides for the shot lane, in [0, 1].
    float PlayerLaneCoverage(const Player& p, const Vec& ballPos, const Vec& ownGoal) const {
        float denom = ownGoal.y - ballPos.y;
        if (std::fabs(denom) < 1.0f) return 0.0f;

        float t = (p.pos.y - ballPos.y) / denom;
        if (t < 0.0f || t > 1.0f) return 0.0f;  // not between ball and own goal in depth

        float laneCenterX  = ballPos.x + (0.0f - ballPos.x) * t;
        float laneHalfWidth = CAR_RADIUS + (CommonValues::GOAL_WIDTH_FROM_CENTER - CAR_RADIUS) * t;

        float lateralOffset = std::fabs(p.pos.x - laneCenterX);
        return RS_CLAMP(1.0f - lateralOffset / laneHalfWidth, 0.0f, 1.0f);
    }

public:
    struct Metrics {
        int   gatedSteps      = 0;
        int   uncoveredSteps  = 0;
        float avgCoverage     = 0.0f;
    };

    static Metrics GetAndResetMetrics() {
        Metrics m;
        m.gatedSteps     = s_gatedSteps.exchange(0);
        m.uncoveredSteps = s_uncoveredSteps.exchange(0);
        float sum        = s_coverageSum.exchange(0.0f);
        m.avgCoverage    = m.gatedSteps > 0 ? sum / m.gatedSteps : 0.0f;
        return m;
    }

    virtual void Reset(const GameState&) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool) override {
        if (static_cast<int>(state.players.size()) < 4) return 0.0f;
        if (player.isDemoed) return 0.0f;

        // Kickoff gate
        if (state.ball.pos.Length() < 200.0f && state.ball.vel.Length() < 150.0f)
            return 0.0f;

        Vec ownGoal = GetOwnGoal(player.team);
        float ballDistOwn = state.ball.pos.Dist(ownGoal);
        float ballDistOpp = state.ball.pos.Dist(GetOppGoal(player.team));

        // Must be defending (ball closer to own goal)
        if (ballDistOwn >= ballDistOpp) return 0.0f;

        // Ball must be genuinely moving toward own goal, not just parked in half
        Vec ballToGoal = (ownGoal - state.ball.pos).Normalized();
        if (state.ball.vel.Dot(ballToGoal) < THREAT_VEL) return 0.0f;

        float bestCoverage = 0.0f;
        for (const auto& p : state.players) {
            if (p.team != player.team || p.isDemoed) continue;
            bestCoverage = RS_MAX(bestCoverage, PlayerLaneCoverage(p, state.ball.pos, ownGoal));
        }

        s_gatedSteps.fetch_add(1);
        if (bestCoverage <= 0.0f) s_uncoveredSteps.fetch_add(1);
        float prev = s_coverageSum.load();
        while (!s_coverageSum.compare_exchange_weak(prev, prev + bestCoverage)) {}

        return bestCoverage;
    }
};

} // namespace RLGC
