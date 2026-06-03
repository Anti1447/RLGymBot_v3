#pragma once
#include "Reward.h"
#include "../Math.h"
#include "../CommonValues.h"

namespace RLGC {

// Continuous shaping: rewards approaching the opponent's PREDICTED position
// at high speed rather than their current position.
// Teaches "leading the target" — aim where they will be, not where they are.
// Complements DemoReward (event) — this shapes the approach, that rewards the finish.
// Rev 1.0 2026-06-01 NJH
class DemoLeadApproachReward : public Reward {
private:
    static constexpr float MIN_SPEED      = 1600.0f;  // UU/s — start rewarding above here
    static constexpr float MAX_SPEED      = 2300.0f;  // UU/s — full reward at supersonic
    static constexpr float MIN_RANGE      = 400.0f;   // UU — ignore opponents already being bumped
    static constexpr float MAX_RANGE      = 3000.0f;  // UU — max demo-attempt distance
    static constexpr float LEAD_TIME      = 0.25f;    // seconds ahead to predict opponent
    static constexpr float MIN_ALIGNMENT  = 0.65f;    // dot — must be broadly aimed at predicted pos
    static constexpr float LEAD_MARGIN    = 0.05f;    // predicted alignment must beat current by this
    static constexpr float REWARD_PER_SEC = 3.0f;     // reward rate at full speed + alignment

public:
    virtual void Reset(const GameState&) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool) override {
        if (player.isDemoed || player.isOnGround) return 0.0f;

        float speed = player.vel.Length();
        if (speed < MIN_SPEED) return 0.0f;

        // Find nearest non-demoed opponent in demo-attempt range
        const Player* target = nullptr;
        float bestDist = MAX_RANGE;
        for (const auto& opp : state.players) {
            if (opp.team == player.team || opp.isDemoed) continue;
            float d = (opp.pos - player.pos).Length();
            if (d >= MIN_RANGE && d < bestDist) {
                bestDist = d;
                target   = &opp;
            }
        }
        if (!target) return 0.0f;

        // Predict opponent position LEAD_TIME seconds ahead using their current velocity
        Vec predictedPos = target->pos + target->vel * LEAD_TIME;

        Vec myVelN      = player.vel.Normalized();
        Vec toCurrent   = (target->pos  - player.pos).Normalized();
        Vec toPredicted = (predictedPos - player.pos).Normalized();

        float alignCurrent   = myVelN.Dot(toCurrent);
        float alignPredicted = myVelN.Dot(toPredicted);

        // Must be aimed at predicted position, and leading better than current position
        if (alignPredicted < MIN_ALIGNMENT)              return 0.0f;
        if (alignPredicted < alignCurrent + LEAD_MARGIN) return 0.0f;

        float speedFactor = RS_CLAMP((speed - MIN_SPEED) / (MAX_SPEED - MIN_SPEED), 0.0f, 1.0f);
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

        return REWARD_PER_SEC * speedFactor * dt;
    }
};

} // namespace RLGC
