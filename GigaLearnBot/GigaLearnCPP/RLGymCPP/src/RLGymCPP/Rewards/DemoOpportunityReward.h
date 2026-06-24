#pragma once
#include "Reward.h"
#include "../Math.h"
#include "../CommonValues.h"
#include <cfloat>

namespace RLGC {

// Rewards demoing an opponent who was actively blocking the shot lane between
// the player and the opponent's goal, while the player carried or was close
// to the ball. Scales by how directly they were blocking (tighter angle = higher)
// and whether the player was carrying (ball < CARRY_DIST) vs. nearby possession.
//
// Detection pattern mirrors RolvDemoReward: player.eventState.demo fires on
// the attacker; victim found by eventState.demoed + proximity; victim->prev->pos
// is used for the blocking check (pre-demo position is the meaningful one).
//
// Overlaps: additive to DemoReward (unconditional). Complementary to
// DemoLeadApproachReward (approach shaping), DemoAwarenessReward (positioning).
//
// Rev 1.0 2026-06-19 NJH

class DemoOpportunityReward : public Reward {
    static constexpr float MAX_BUMP_DIST = 350.0f;  // attribution radius (matches RolvDemoReward)
    static constexpr float MAX_BALL_DIST = 400.0f;  // loose possession threshold
    static constexpr float CARRY_DIST    = 150.0f;  // carrying threshold
    static constexpr float MAX_BLOCK_DEG = 40.0f;   // cone half-angle: "blocking my shot lane"
    static constexpr float BASE_REWARD   = 12.0f;

    const Player* FindVictim(const Player& player, const GameState& state) const {
        const Player* best     = nullptr;
        float         bestDist = FLT_MAX;
        for (const auto& p : state.players) {
            if (p.team == player.team)  continue;
            if (!p.eventState.demoed)   continue;
            float d = (p.pos - player.pos).Length();
            if (d < bestDist && d < MAX_BUMP_DIST) { bestDist = d; best = &p; }
        }
        return best;
    }

public:
    virtual void Reset(const GameState&) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool) override {
        if (!player.eventState.demo) return 0.0f;

        const Player* victim = FindVictim(player, state);
        if (!victim || !victim->prev || victim->prev->isDemoed) return 0.0f;

        // Player must have ball proximity (carrying or nearby possession)
        float ballDist = (player.pos - state.ball.pos).Length();
        if (ballDist > MAX_BALL_DIST) return 0.0f;

        // Was victim blocking the shot lane? Use pre-demo position.
        Vec oppGoal = (player.team == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_BACK
            : CommonValues::BLUE_GOAL_BACK;

        Vec toGoalNorm   = SafeNorm(oppGoal - player.pos);
        Vec toVictimNorm = SafeNorm(victim->prev->pos - player.pos);
        float cosAngle   = toGoalNorm.Dot(toVictimNorm);
        if (cosAngle <= 0.0f) return 0.0f;  // victim was behind player

        float angleDeg = std::acos(RS_CLAMP(cosAngle, -1.0f, 1.0f)) * (180.0f / 3.14159265f);
        if (angleDeg > MAX_BLOCK_DEG) return 0.0f;

        float angleQuality = 1.0f - (angleDeg / MAX_BLOCK_DEG);  // 1.0 = dead-center block
        float carryBonus   = (ballDist < CARRY_DIST) ? 1.0f : 0.6f;

        return BASE_REWARD * angleQuality * carryBonus;
    }
};

} // namespace RLGC
