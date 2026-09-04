#pragma once

/*
 * File: DribbleCarryTransitionReward.h
 * Location: RLGymCPP/Rewards/
 *
 * Description:
 *   Event-gated companion to DribbleReward/DribbleMasteryReward (AdvancedRewards.h),
 *   which reward dribble-carry geometry continuously every step. This class instead
 *   pays a one-time bonus on the step a player achieves a clean, uncontested dribble
 *   carry, via a 3-state machine:
 *
 *     IDLE      --> CARRYING   : geometry met AND no intruder within contestRadius
 *     CARRYING  --> CARRYING   : geometry still met, no intruder
 *     CARRYING  --> CONTESTED  : intruder enters contestRadius of ball (geometry still holds)
 *     CARRYING  --> IDLE       : geometry fails (popped/knocked away, airborne, too slow)
 *     CONTESTED --> CARRYING   : intruder leaves AND geometry still holds for same carrier
 *     CONTESTED --> IDLE       : geometry fails while contested
 *     CONTESTED --> CARRYING   : a different, intruder-free car satisfies geometry (STEAL)
 *
 *   Geometry gates are DribbleMasteryReward's five thresholds verbatim (ground-Z, on-ground,
 *   XY distance, height band, forward-dot, speed) plus DribbleReward's relative-velocity gate
 *   (|ballVel - carVel| <= maxRelativeSpeed) — six conditions bundled into one AND. Neither
 *   DribbleReward nor DribbleMasteryReward is modified by this class; both remain pure
 *   continuous per-step shaping — this is a purely additive event layer.
 *
 *   "Contested" = any other living, non-demoed player (teammate or opponent, no exemption)
 *   within contestRadius of the ball.
 *
 *   Payout fires only on arrival into CARRYING (from IDLE, from CONTESTED with the same
 *   carrier once the intruder leaves, or via a steal), and only after the arriving carrier
 *   has held geometry continuously for confirmHoldSec — a pre-payout anti-flicker hold
 *   applied uniformly to every arrival path (distinct from PossessionTransitionReward's
 *   post-payout cooldown). A steal additionally requires the new carrier to be intruder-free
 *   itself, so it represents genuinely winning clean possession rather than inheriting a
 *   carry that's still being challenged by a third player.
 *
 *   PreStep computes all shared state once per step (not per-player) — same rationale as
 *   PossessionTransitionReward: GetReward runs once per player and must not re-derive or
 *   re-trigger the transition per player. Per-car eligibility timestamps (not a single
 *   scalar) are needed because, unlike ball possession (at most one closest player per
 *   team), dribble-carry geometry can in principle be satisfied by multiple players
 *   simultaneously.
 *
 *   No dedicated state setter: this reward's geometry is a strict subset of what
 *   DribbleMasteryReward already organically reaches under RandomState, so it cannot
 *   activate in any state DribbleMasteryReward itself never visits.
 *
 *   See Reward_Change_Log.md 2026-08-27.
 *
 * Author: Nathan + Claude
 * Date: August 2026
 * Revision: 1.0
 */

#include "Reward.h"
#include "../Math.h"
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cfloat>
#include <cmath>
#include <map>
#include <vector>

namespace RLGC {

    // ============================================================================
    // DRIBBLE CARRY TRANSITION REWARD
    // 2026-08-27 | Rev 1.0 | NJH
    // ============================================================================
    class DribbleCarryTransitionReward : public Reward {
    public:
        struct Config {
            // Geometry gates — verbatim from DribbleMasteryReward (AdvancedRewards.h:638-655)
            float maxDribbleDistanceXY  = 105.0f;
            float minBallHeightAboveCar = 40.0f;
            float maxBallHeightAboveCar = 240.0f;
            float minForwardDot         = 0.15f;
            float minSpeed              = 800.0f;
            float groundMaxZ            = 600.0f;

            // Relative-velocity gate — verbatim from DribbleReward (AdvancedRewards.h:855)
            float maxRelativeSpeed      = 800.0f;

            // Contest radius — new constant, no existing codebase precedent to reuse
            float contestRadius         = 150.0f;

            // Anti-flicker pre-payout confirm-hold, applied uniformly to every arrival path
            float confirmHoldSec        = 0.1f;

            float entryReward           = 1.0f;
        };

        struct Metrics {
            int    cleanEntries = 0;
            int    stealEntries = 0;
            double rewardSum = 0.0;
        };

    private:
        enum class CarryState { IDLE, CARRYING, CONTESTED };

        Config config;

        CarryState carryState = CarryState::IDLE;
        uint32_t   carrierId = 0;   // valid car id when carryState != IDLE

        float gameTime = 0.0f;

        // Per-car: gameTime at which this car most recently became continuously
        // geometry-eligible. Erased the instant a car stops satisfying geometry
        // (mirrors DribbleReward/DribbleMasteryReward's sustainedTime.erase(id) convention).
        mutable std::map<uint32_t, float> eligibleSince;

        bool     entryFired = false;
        bool     entryIsSteal = false;
        uint32_t entryCarId = 0;

        inline static std::atomic<int> s_cleanEntries{ 0 };
        inline static std::atomic<int> s_stealEntries{ 0 };
        inline static std::atomic<long long> s_rewardMilli{ 0 };

        bool GeometryMet(const Player& p, const GameState& gs) const {
            if (!p.isOnGround) return false;
            if (gs.ball.pos.z > config.groundMaxZ) return false;

            Vec diff = gs.ball.pos - p.pos;
            Vec diffXY = diff; diffXY.z = 0.0f;
            float distXY = diffXY.Length();
            if (distXY > config.maxDribbleDistanceXY) return false;

            float height = diff.z;
            if (height < config.minBallHeightAboveCar || height > config.maxBallHeightAboveCar) return false;

            float forwardDot = p.rotMat.forward.Dot(diff.Normalized());
            if (forwardDot < config.minForwardDot) return false;

            if (p.vel.Length() < config.minSpeed) return false;

            // Carrying tends to have similar velocities (DribbleReward, AdvancedRewards.h:854)
            float relSpeed = (gs.ball.vel - p.vel).Length();
            if (relSpeed > config.maxRelativeSpeed) return false;

            return true;
        }

        bool HasIntruder(const GameState& gs, uint32_t excludeCarId) const {
            for (const auto& p : gs.players) {
                if (p.isDemoed) continue;
                if (p.carId == excludeCarId) continue;
                if (p.pos.Dist(gs.ball.pos) <= config.contestRadius) return true;
            }
            return false;
        }

        bool IsHeld(uint32_t carId) const {
            auto it = eligibleSince.find(carId);
            return it != eligibleSince.end() && (gameTime - it->second) >= config.confirmHoldSec;
        }

    public:
        static Metrics GetAndResetMetrics() {
            Metrics m;
            m.cleanEntries = s_cleanEntries.exchange(0);
            m.stealEntries = s_stealEntries.exchange(0);
            m.rewardSum = double(s_rewardMilli.exchange(0)) / 1000.0;
            return m;
        }

        DribbleCarryTransitionReward() : config() {}
        explicit DribbleCarryTransitionReward(const Config& cfg) : config(cfg) {}

        virtual void Reset(const GameState& initialState) override {
            carryState = CarryState::IDLE;
            carrierId = 0;
            gameTime = 0.0f;
            eligibleSince.clear();
            entryFired = false;
            entryIsSteal = false;
            entryCarId = 0;
        }

        // PreStep runs exactly once per step — this is where the shared carry state
        // must be computed and mutated. Doing this in GetReward would recompute and
        // re-trigger the transition once per player instead of once per step, and a
        // "steal" specifically requires comparing across players, not just the one
        // GetReward happens to be evaluating.
        virtual void PreStep(const GameState& state) override {
            gameTime += (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            entryFired = false;
            entryIsSteal = false;

            // Pass 1: geometry eligibility per living, non-demoed player. Maintain
            // eligibleSince so the confirm-hold reflects continuous eligibility.
            std::vector<uint32_t> eligibleCars;
            for (const auto& p : state.players) {
                if (p.isDemoed) continue;
                if (GeometryMet(p, state)) {
                    eligibleCars.push_back(p.carId);
                    if (eligibleSince.find(p.carId) == eligibleSince.end())
                        eligibleSince[p.carId] = gameTime;
                } else {
                    eligibleSince.erase(p.carId);
                }
            }

            // Deterministic tie-break for the rare case of simultaneous same-step
            // candidates: earliest eligibleSince (longest continuous hold) wins.
            std::sort(eligibleCars.begin(), eligibleCars.end(),
                [this](uint32_t a, uint32_t b) { return eligibleSince[a] < eligibleSince[b]; });

            bool carrierStillEligible = false;
            for (uint32_t id : eligibleCars) {
                if (id == carrierId) { carrierStillEligible = true; break; }
            }

            switch (carryState) {
                case CarryState::IDLE: {
                    for (uint32_t c : eligibleCars) {
                        if (!IsHeld(c)) continue;
                        if (HasIntruder(state, c)) continue;
                        carryState = CarryState::CARRYING;
                        carrierId = c;
                        entryFired = true;
                        entryIsSteal = false;
                        entryCarId = c;
                        s_cleanEntries.fetch_add(1);
                        break;
                    }
                    break;
                }

                case CarryState::CARRYING: {
                    if (!carrierStillEligible) {
                        carryState = CarryState::IDLE;
                        carrierId = 0;
                        break;
                    }
                    if (HasIntruder(state, carrierId)) {
                        carryState = CarryState::CONTESTED;
                    }
                    // else: stay CARRYING, no re-fire.
                    break;
                }

                case CarryState::CONTESTED: {
                    if (!carrierStillEligible) {
                        carryState = CarryState::IDLE;
                        carrierId = 0;
                        break;
                    }
                    if (!HasIntruder(state, carrierId)) {
                        // Intruder left, same carrier's geometry still holds -> re-enter CARRYING.
                        carryState = CarryState::CARRYING;
                        if (IsHeld(carrierId)) {
                            entryFired = true;
                            entryIsSteal = false;
                            entryCarId = carrierId;
                            s_cleanEntries.fetch_add(1);
                        }
                        break;
                    }
                    // Still contested for the current carrier — check for a steal: a
                    // different, confirm-held, intruder-free car satisfying geometry.
                    for (uint32_t c : eligibleCars) {
                        if (c == carrierId) continue;
                        if (!IsHeld(c)) continue;
                        if (HasIntruder(state, c)) continue;
                        carryState = CarryState::CARRYING;
                        carrierId = c;
                        entryFired = true;
                        entryIsSteal = true;
                        entryCarId = c;
                        s_stealEntries.fetch_add(1);
                        break;
                    }
                    break;
                }
            }
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!entryFired) return 0.0f;
            if (player.carId != entryCarId) return 0.0f;

            s_rewardMilli.fetch_add(static_cast<long long>(config.entryReward * 1000.0f));
            return config.entryReward;
        }
    };

} // namespace RLGC
