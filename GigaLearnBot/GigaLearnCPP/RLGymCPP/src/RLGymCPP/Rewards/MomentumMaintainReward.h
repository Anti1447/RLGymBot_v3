#pragma once

/*
 * File: MomentumMaintainReward.h
 * Location: RLGymCPP/Rewards/
 *
 * Description:
 *   Simple, fully-discrete, bonus-only reward for maintaining momentum through
 *   three specific maneuvers: turning, landing, and jumping+boosting. Each is
 *   its own tiny independent state machine (entry snapshot -> confirm-hold ->
 *   exit resolve) sharing one Config/Metrics. No penalties anywhere — nothing
 *   gets punished for speed loss outside the player's control (a bump mid-turn,
 *   a forced hard landing).
 *
 *   Turning:     while on ground, handbraking, and turning sharply (thresholds
 *                borrowed verbatim from PowerslideSharpTurnReward, PowerslideTurnRewards.h,
 *                for consistency), does exit speed stay >= turnRetentionThreshold
 *                of entry speed?
 *   Landing:     does landing speed stay >= landingRetentionThreshold of
 *                pre-landing speed? Detection reuses ProperLandingReward's exact
 *                pattern (CompleteMechanicsRewards.h): PreStep-computed prev-airborne
 *                edge, >=2 wheels with contact, >=MIN_AIR_TIME airtime. player.prev
 *                is a real pointer to the previous-step snapshot, so
 *                player.prev->vel.Length() on the landing frame directly gives
 *                pre-landing speed — no separately-tracked "last airborne speed"
 *                needed.
 *   Jump+boost:  while airborne, having jumped, and boosting, does speed GAIN
 *                by at least jumpBoostMinGain (UU/s, absolute — not a ratio)?
 *                A ratio would reward slow-entry-speed players disproportionately
 *                and punish fast-entry players unfairly since boost can't move
 *                the needle much in ratio terms near CAR_MAX_SPEED; an absolute
 *                gain threshold is the correct invariant. Uses player.hasJumped,
 *                not isJumping — isJumping is capped by the engine at
 *                RLConst::JUMP_MAX_TIME (0.2s) and is too narrow to catch a
 *                sustained jump+boost combo, while hasJumped persists through the
 *                entire airtime after a ground jump until landing.
 *
 *   Turning and landing are mutually exclusive by construction (turning requires
 *   isOnGround, so it can only ever START on the frame ground is regained, never
 *   resolve on that frame). Jump-boost and landing CAN co-fire on the same tick —
 *   a player boosting through a jump who lands while still holding boost has both
 *   the jump-boost exit-resolve and the landing resolve evaluate on that one
 *   PreStep tick. This is two real, distinct things happening (gained speed from
 *   the boost, AND kept it through the landing), so pending rewards are SUMMED
 *   per player, not overwritten.
 *
 *   Demo handling deliberately deviates from this session's usual "suspend, don't
 *   erase" convention (used by DribbleCarryTransitionReward/InstantSpacingTransitionReward/
 *   RoleCycleTransitionReward): on isDemoed, this class RESETS all in-flight
 *   channel state (no payout) rather than freezing it for later resumption. A
 *   demo respawn resets velocity near-zero, so resuming a stale pre-demo
 *   entrySpeed/wasAirborne comparison after respawn would produce a false result
 *   either way — since this reward is bonus-only, discarding in-flight state is
 *   always the safe choice.
 *
 *   No ZeroSum wrap, no dedicated state setter — individual mechanical execution,
 *   not a competitive/team concept; all 3 mechanics occur constantly in ordinary
 *   play under RandomState.
 *
 *   Note: an unrelated, dead, unwired class named MomentumMaintenanceReward
 *   (ConfidenceRewards.h) already exists — continuous, absolute-speed-floor,
 *   has a penalty branch. Name-adjacent but structurally unrelated; not touched.
 *
 *   See Reward_Change_Log.md 2026-08-28.
 *
 * Author: Nathan + Claude
 * Date: August 2026
 * Revision: 1.0
 */

#include "Reward.h"
#include "../Math.h"
#include "../CommonValues.h"
#include <atomic>
#include <cstdint>
#include <cmath>
#include <unordered_map>

namespace RLGC {

    // ============================================================================
    // MOMENTUM MAINTAIN REWARD
    // 2026-08-28 | Rev 1.0 | NJH
    // ============================================================================
    class MomentumMaintainReward : public Reward {
    public:
        struct Config {
            // Turning channel — thresholds borrowed from PowerslideSharpTurnReward
            float turnMinSpeed           = 500.0f;
            float turnSharpAngVel        = 1.8f;
            float turnConfirmHoldSec     = 0.15f;
            float turnRetentionThreshold = 0.85f;
            float turnReward             = 0.3f;

            // Landing channel — thresholds borrowed from ProperLandingReward
            float landingMinAirTime         = 0.2f;
            float landingRetentionThreshold = 0.85f;
            float landingReward             = 0.3f;

            // Jump+boost channel — no existing precedent, fresh values
            float jumpBoostConfirmHoldSec = 0.15f;
            float jumpBoostMinGain        = 250.0f;
            float jumpBoostReward         = 0.3f;
        };

        struct Metrics {
            int    turnCompletions      = 0;
            int    landingCompletions   = 0;
            int    jumpBoostCompletions = 0;
            double rewardSum            = 0.0;
        };

    private:
        struct PlayerState {
            // turning
            bool  turnHolding = false;
            bool  turnConfirmed = false;
            float turnEligibleSince = 0.0f;
            float turnEntrySpeed = 0.0f;
            // jump+boost
            bool  jbHolding = false;
            bool  jbConfirmed = false;
            float jbEligibleSince = 0.0f;
            float jbEntrySpeed = 0.0f;
            // landing
            bool  wasAirborne = false;
        };

        Config config;

        std::unordered_map<uint32_t, PlayerState> states;
        std::unordered_map<uint32_t, float> pendingRewards;

        float gameTime = 0.0f;

        inline static std::atomic<int> s_turnCompletions{ 0 };
        inline static std::atomic<int> s_landingCompletions{ 0 };
        inline static std::atomic<int> s_jumpBoostCompletions{ 0 };
        inline static std::atomic<long long> s_rewardMilli{ 0 };

        void AddPending(uint32_t carId, float amount) {
            pendingRewards[carId] += amount;
            s_rewardMilli.fetch_add(static_cast<long long>(amount * 1000.0f));
        }

    public:
        static Metrics GetAndResetMetrics() {
            Metrics m;
            m.turnCompletions = s_turnCompletions.exchange(0);
            m.landingCompletions = s_landingCompletions.exchange(0);
            m.jumpBoostCompletions = s_jumpBoostCompletions.exchange(0);
            m.rewardSum = double(s_rewardMilli.exchange(0)) / 1000.0;
            return m;
        }

        MomentumMaintainReward() : config() {}
        explicit MomentumMaintainReward(const Config& cfg) : config(cfg) {}

        virtual void Reset(const GameState& initialState) override {
            states.clear();
            pendingRewards.clear();
            gameTime = 0.0f;
        }

        // PreStep runs exactly once per step — all 3 channels' detection lives
        // here. GetReward just reads a per-player pending value.
        virtual void PreStep(const GameState& state) override {
            gameTime += (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            pendingRewards.clear();

            for (const auto& player : state.players) {
                uint32_t id = player.carId;

                if (player.isDemoed) {
                    // Reset, don't suspend — a demo respawn resets velocity near-zero,
                    // so resuming a stale pre-demo comparison would be meaningless.
                    states.erase(id);
                    continue;
                }

                PlayerState& st = states[id];
                float speed = player.vel.Length();

                // --- Turning channel ---
                bool turningNow = player.isOnGround &&
                    player.prevAction.handbrake > 0.5f &&
                    std::abs(player.angVel.z) > config.turnSharpAngVel &&
                    speed > config.turnMinSpeed;

                if (turningNow) {
                    if (!st.turnHolding) {
                        st.turnHolding = true;
                        st.turnConfirmed = false;
                        st.turnEligibleSince = gameTime;
                        st.turnEntrySpeed = speed;
                    }
                    if (!st.turnConfirmed && (gameTime - st.turnEligibleSince) >= config.turnConfirmHoldSec) {
                        st.turnConfirmed = true;
                    }
                } else {
                    if (st.turnConfirmed) {
                        float retention = speed / RS_MAX(st.turnEntrySpeed, 1.0f);
                        if (retention >= config.turnRetentionThreshold) {
                            AddPending(id, config.turnReward);
                            s_turnCompletions.fetch_add(1);
                        }
                    }
                    st.turnHolding = false;
                    st.turnConfirmed = false;
                }

                // --- Jump+boost channel ---
                bool jbNow = !player.isOnGround &&
                    player.hasJumped &&
                    player.prevAction.boost > 0.5f;

                if (jbNow) {
                    if (!st.jbHolding) {
                        st.jbHolding = true;
                        st.jbConfirmed = false;
                        st.jbEligibleSince = gameTime;
                        st.jbEntrySpeed = speed;
                    }
                    if (!st.jbConfirmed && (gameTime - st.jbEligibleSince) >= config.jumpBoostConfirmHoldSec) {
                        st.jbConfirmed = true;
                    }
                } else {
                    if (st.jbConfirmed) {
                        float gain = speed - st.jbEntrySpeed;
                        if (gain >= config.jumpBoostMinGain) {
                            AddPending(id, config.jumpBoostReward);
                            s_jumpBoostCompletions.fetch_add(1);
                        }
                    }
                    st.jbHolding = false;
                    st.jbConfirmed = false;
                }

                // --- Landing channel (ProperLandingReward's exact pattern) ---
                bool nowAir = !player.isOnGround;
                if (st.wasAirborne && !nowAir) {
                    float airTime = (player.prev != nullptr) ? player.prev->airTime : player.airTime;
                    if (airTime >= config.landingMinAirTime) {
                        int wheels = 0;
                        for (int i = 0; i < 4; i++)
                            if (player.wheelsWithContact[i]) wheels++;

                        if (wheels >= 2) {
                            float preLandSpeed = (player.prev != nullptr) ? player.prev->vel.Length() : speed;
                            float retention = speed / RS_MAX(preLandSpeed, 1.0f);
                            if (retention >= config.landingRetentionThreshold) {
                                AddPending(id, config.landingReward);
                                s_landingCompletions.fetch_add(1);
                            }
                        }
                    }
                }
                st.wasAirborne = nowAir;
            }
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;
            auto it = pendingRewards.find(player.carId);
            return (it != pendingRewards.end()) ? it->second : 0.0f;
        }
    };

} // namespace RLGC
