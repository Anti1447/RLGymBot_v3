#pragma once
#include "Reward.h"
#include "../Gamestates/GameState.h"
#include "../CommonValues.h"

namespace RLGC {

// ============================================================================
// GoalsideReward (2026-06-12 | Rev 1.1)
// ============================================================================
// Simple individual defensive fundamental: rewards the player for being
// between the ball and their own goal (goalside), penalizes being caught
// upfield of the ball (offside) at a reduced magnitude.
//
// Y-only margin, same convention as BallSideNumericalAdvantageReward:
//   Blue goalside:   player.pos.y < ball.pos.y  (blue goal at Y = -5120)
//   Orange goalside: player.pos.y > ball.pos.y  (orange goal at Y = +5120)
//
// Smooth ramp: reward = clamp(margin / 800uu, -1, 1), with the negative
// (offside) side scaled by 0.4. Output range [-0.4, +1.0]. The ramp avoids
// a hard cliff at the ball plane so the gradient is clean near the boundary.
//
// Offside dead zone (Rev 1.1, 2026-06-12): no penalty until the player is
// more than 200uu upfield of the ball. Dribbling/flicking puts the car
// slightly ahead of the ball plane; penalizing that fights possession play.
// The penalty ramp starts at margin = -200 and reaches full -0.4 at -1000.
//
// Gates:
//   - Demoed players return 0.
//   - Kickoff skipped (ball near center, velocity < 100 UU/s).
//
// NOT ZeroSum: the asymmetric +1.0/-0.4 design is intentionally
// non-zero-sum — use a plain WeightedReward in RewardConfig.cpp.
//
// Anti-farming note: net-camping always satisfies goalside. Mitigated by low
// weight (2.0f) and offensive rewards pulling the other way. If passivity
// shows in RocketSimVis, halve the weight or gate to ball-on-own-half.
//
// W&B: per-reward mean for GoalsideReward. Healthy: steady positive mean
// (~+0.3 to +0.6 raw). Suspicious: pinned near +1.0 (camping) or negative.
// ============================================================================

class GoalsideReward : public Reward {
private:
    static constexpr float RAMP_UU          = 800.0f; // Y-margin over which reward ramps 0 -> full
    static constexpr float OFFSIDE_SCALE    = 0.4f;   // penalty magnitude relative to reward
    static constexpr float OFFSIDE_DEADZONE = 200.0f; // uu upfield of ball before penalty starts (dribble allowance)

public:
    virtual float GetReward(const Player& player, const GameState& state, bool /*isFinal*/) override {
        if (player.isDemoed) return 0.0f;

        // Skip kickoff: ball near center with near-zero velocity
        if (state.ball.pos.Length() < 300.0f && state.ball.vel.Length() < 100.0f)
            return 0.0f;

        // Positive margin = player is between ball and own goal (Y-only)
        float margin = (player.team == Team::BLUE)
            ? (state.ball.pos.y - player.pos.y)
            : (player.pos.y - state.ball.pos.y);

        if (margin >= 0.0f)
            return RS_MIN(margin / RAMP_UU, 1.0f);

        // Offside: free allowance of OFFSIDE_DEADZONE uu (dribble carry zone),
        // then ramp penalty over RAMP_UU. Range [-0.4, 0].
        float over = margin + OFFSIDE_DEADZONE;
        if (over >= 0.0f) return 0.0f;
        return RS_MAX(over / RAMP_UU, -1.0f) * OFFSIDE_SCALE;
    }
};

} // namespace RLGC
