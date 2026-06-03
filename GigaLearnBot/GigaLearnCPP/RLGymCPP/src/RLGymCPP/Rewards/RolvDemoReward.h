// RolvDemoReward.h
// C++ port of the DemoReward function by Rolv-Arild.
// Source: https://github.com/RLGym/rlgym-tools/blob/main/rlgym_tools/rocket_league/reward_functions/demo_reward.py
// Port: NJH (2026-05-28)
//
// Richer demo reward than the existing CommonRewards.h typedef pair
// (DemoReward / DemoedPenalty). This class parameterises the attacker reward,
// the victim punishment, and an optional bump-acceleration bonus, matching the
// Python source exactly.
//
// The Python source stores a prev_state dict; in C++ we use player.prev and
// victim->prev, which the engine maintains per player automatically.
//
// NOTE: The C++ engine callback (_BumpCallback in EnvSet.cpp) fires only for
// cross-team bumps — same-team bumps never set eventState flags. The Python
// source includes a teammate-penalty branch (negative bump_acceleration_reward
// for teammate bumps); that branch is unreachable in C++ and is therefore not
// implemented. If you need to penalise friendly-fire bumps you would need to
// add a separate pass over carContact.otherCarID.
//
// =============================================================================
// USAGE
// =============================================================================
//
// Drop-in replacement for:
//   WeightedReward(new DemoReward(),    weight)   // attacker
//   WeightedReward(new DemoedPenalty(), weight)   // victim
//
// Equivalent:
//   WeightedReward(new RolvDemoReward(1.0f, 1.0f, 0.0f), weight)
//
// With bump-acceleration bonus (scales reward by velocity imparted to victim):
//   WeightedReward(new RolvDemoReward(1.0f, 1.0f, 1.0f), weight)
//
// Wrap in ZeroSumReward as usual for competitive framing.
//
// =============================================================================

#pragma once

#include "Reward.h"
#include "../Math.h"
#include "../CommonValues.h"
#include <cfloat>   // FLT_MAX

namespace RLGC {

    class RolvDemoReward : public Reward {
    private:
        // Reward for the attacker when a demo lands this frame.
        float attackerReward;
        // Magnitude of punishment applied to the victim (returned as negative).
        float victimPunishment;
        // Scale for the bump-acceleration bonus when the victim is NOT demoed:
        //   reward = bumpAccelerationReward * |ΔvVictim| / CAR_MAX_SPEED
        // Default 0.0 → bonus disabled (pure demo/victim signals only).
        float bumpAccelerationReward;

        // Maximum distance between bumper and victim to accept a triangulation
        // match. Mirrors AirDribbleBumpConversionReward::FindBumpTarget().
        static constexpr float MAX_BUMP_CONTACT_DIST = 350.0f;

        // Locate the opponent that this player bumped or demoed this frame.
        // Since the C++ engine does not store a persistent bumpVictimId, we
        // identify the victim by iterating players, filtering on the flag the
        // engine sets for the victim (eventState.bumped || eventState.demoed),
        // and taking the nearest opponent within MAX_BUMP_CONTACT_DIST.
        // Pattern from: AirDribbleBumpConversionReward::FindBumpTarget()
        const Player* FindBumpVictim(const Player& player,
                                     const GameState& state) const {
            const Player* best    = nullptr;
            float         bestDist = FLT_MAX;

            for (const auto& p : state.players) {
                if (p.team   == player.team)  continue;  // skip teammates
                if (p.carId  == player.carId) continue;  // skip self (redundant)
                if (!p.eventState.bumped && !p.eventState.demoed) continue;

                float d = (p.pos - player.pos).Length();
                if (d < bestDist && d < MAX_BUMP_CONTACT_DIST) {
                    bestDist = d;
                    best     = &p;
                }
            }
            return best;
        }

    public:
        // ctor: mirrors Python __init__ signature and defaults exactly.
        explicit RolvDemoReward(float attackerReward_       = 1.0f,
                                float victimPunishment_     = 1.0f,
                                float bumpAccelerationReward_ = 0.0f)
            : attackerReward(attackerReward_)
            , victimPunishment(victimPunishment_)
            , bumpAccelerationReward(bumpAccelerationReward_) {}

        virtual void Reset(const GameState& initialState) override {
            // No per-episode state — previous frame data comes from player.prev.
        }

        virtual float GetReward(const Player& player,
                                const GameState& state,
                                bool isFinal) override {
            float reward = 0.0f;

            // ── PATH A: this player bumped or demoed someone this frame ────────
            if (player.eventState.bump || player.eventState.demo) {
                const Player* victim = FindBumpVictim(player, state);

                if (victim) {
                    // Demo sub-case: victim transitioned to demoed this frame.
                    // Python: if victim_car.is_demoed and not prev_victim.is_demoed
                    if (victim->isDemoed &&
                        victim->prev && !victim->prev->isDemoed) {
                        reward += attackerReward;
                    }
                    // Bump sub-case: victim is NOT demoed, just knocked.
                    // Python: else → acceleration = ||ΔvVictim|| / CAR_MAX_SPEED
                    else if (!victim->isDemoed && bumpAccelerationReward != 0.0f
                             && victim->prev) {
                        Vec velDelta  = victim->vel - victim->prev->vel;
                        float accel   = velDelta.Length();
                        // Teammate bumps never reach this branch (engine filters them).
                        reward += bumpAccelerationReward * accel
                                  / CommonValues::CAR_MAX_SPEED;
                    }
                }
            }

            // ── PATH B: this player was demoed this frame ──────────────────────
            // Python: rewards[victim] -= self.victim_punishment
            if (player.eventState.demoed) {
                reward -= victimPunishment;
            }

            return reward;
        }
    };

} // namespace RLGC
