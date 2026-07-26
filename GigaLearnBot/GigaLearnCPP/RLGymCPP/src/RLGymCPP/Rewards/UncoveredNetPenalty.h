#pragma once
#include "Reward.h"
#include "../Math.h"
#include "../CommonValues.h"
#include <atomic>

namespace RLGC {

// ============================================================================
// UncoveredNetPenalty
// 2026-06-25 | Rev 1.0 | NJH
//
// Team-level penalty: fires for every non-demoed player on the defending team
// when NO teammate is currently in net coverage position during a genuine shot
// threat. Fills the gap that per-player rewards cannot cover — each player can
// individually satisfy LastManOvercommitPenalty / ShadowDefense while the team
// collectively leaves the goal open on a fast break or 2v1.
//
// Gates (all must be true):
//   1. 2v2+ only (state.players.size() >= 4)
//   2. Not kickoff (ball.pos.Length() >= 200 || ball.vel.Length() >= 150)
//   3. Team is defending (ball closer to own goal than opponent goal)
//   4. Ball moving toward own goal at > THREAT_VEL UU/s (genuine threat,
//      not just ball parked in own half)
//   5. No non-demoed teammate (including self) is within COVER_DIST of own goal
//      AND between ball and own goal
//
// If the current player IS in coverage, gates pass but step 5 exits 0 — they
// are exempt. Only players who vacated coverage get penalized.
//
// Complements (not duplicates):
//   - LastManOvercommitPenalty: per-player, penalizes WHO rushed the ball
//   - ShadowDefenseVsPossessionReward: per-player, rewards shadow geometry
//   - EmergencySaveCommitReward: rewards the player who commits to the save
// ============================================================================
class UncoveredNetPenalty : public Reward {
    static constexpr float COVER_DIST = 2500.0f;  // UU from own goal center
    static constexpr float THREAT_VEL = 200.0f;   // min ball vel toward goal (UU/s)
    static constexpr float PENALTY    = 0.2f;     // per player per step; outer weight scales

    inline static std::atomic<int> s_activations{0};

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

public:
    struct Metrics {
        int activations = 0;
    };

    static Metrics GetAndResetMetrics() {
        Metrics m;
        m.activations = s_activations.exchange(0);
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

        // Check every non-demoed player on this team (including self).
        // If anyone is in the coverage zone, net is covered — no penalty.
        for (const auto& p : state.players) {
            if (p.team != player.team || p.isDemoed) continue;
            float distOwn = p.pos.Dist(ownGoal);
            if (distOwn < COVER_DIST && distOwn < ballDistOwn)
                return 0.0f;
        }

        s_activations.fetch_add(1);
        return -PENALTY;
    }
};

} // namespace RLGC
