#pragma once
#include "Reward.h"

namespace RLGC {

// ============================================================================
// NonPrimaryAirPenalty
// 2026-06-20 | Rev 1.0
//
// Penalizes a non-primary player (any player who has a living teammate
// meaningfully closer to the ball) for being significantly airborne.
//
// Fills the gap in SecondaryAerialPenalty (CompleteMechanicsRewards.h:1687):
// that reward requires ball.z > 250 UU and therefore misses "ball is low but
// secondary player does a random aerial" — the most common form of off-ball
// aerial waste.
//
// Key difference from SecondaryAerialPenalty:
//   SecondaryAerialPenalty: fires when ball.z > 250 AND airborne AND teammate closer
//   NonPrimaryAirPenalty:   fires when player.z > 175 AND airborne > 0.5s AND teammate closer
//   (ball height is irrelevant — we care about WHERE THE PLAYER IS, not the ball)
//
// They can fire simultaneously (ball high, player high, teammate closer) — intentional.
// Combined signal is appropriate for "clearly out of rotation."
//
// No ZeroSum: per-player behavioral penalty. 1v1 immune.
// ============================================================================

class NonPrimaryAirPenalty : public Reward {
    static constexpr float PLAYER_HEIGHT_THRESHOLD = 175.0f;  // UU — below this = small hop, allow it
    static constexpr float DIST_SLACK              = 200.0f;  // UU — teammate must be this much closer
    static constexpr float GRACE_PERIOD            = 0.5f;    // seconds before penalty starts
    static constexpr float PENALTY_PER_SECOND      = 0.08f;   // per-second penalty rate

    mutable std::unordered_map<uint32_t, float> secondaryAirtime;

public:
    virtual void Reset(const GameState&) override {
        secondaryAirtime.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

        // Must be genuinely airborne at meaningful height
        if (player.isOnGround || player.pos.z <= PLAYER_HEIGHT_THRESHOLD) {
            secondaryAirtime[player.carId] = 0.0f;
            return 0.0f;
        }

        // 1v1 immune: skip if no living teammate on same team
        bool hasTeammate = false;
        float myDist = (player.pos - state.ball.pos).Length();
        bool teammateCloser = false;
        for (const auto& p : state.players) {
            if (p.team != player.team || p.carId == player.carId || p.isDemoed) continue;
            hasTeammate = true;
            float tDist = (p.pos - state.ball.pos).Length();
            if (tDist + DIST_SLACK < myDist) {
                teammateCloser = true;
                break;
            }
        }

        if (!hasTeammate || !teammateCloser) {
            secondaryAirtime[player.carId] = 0.0f;
            return 0.0f;
        }

        float& airtime = secondaryAirtime[player.carId];
        airtime += dt;

        if (airtime <= GRACE_PERIOD) return 0.0f;

        return -PENALTY_PER_SECOND * dt;
    }
};

} // namespace RLGC
