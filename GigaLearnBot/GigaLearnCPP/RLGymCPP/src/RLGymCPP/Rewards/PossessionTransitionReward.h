#pragma once

/*
 * File: PossessionTransitionReward.h
 * Location: RLGymCPP/Rewards/
 *
 * Description:
 *   Event-gated reward for the moment a team GAINS ball possession.
 *
 *   Possession each step is determined purely by proximity: whichever
 *   player is closest to the ball owns it for their team. A hysteresis
 *   margin (config.neutralMarginUU) treats near-equal distances as
 *   NEUTRAL (contested / 50-50) rather than flip-flopping team to team.
 *
 *   Payout fires once, on the step where the possessing team changes
 *   FROM {opponent, neutral} TO the player's team. Only the specific
 *   player who is now closest to the ball (the one who "won" it) is
 *   paid — this matches the existing precedent in MultiAgentPossessionReward
 *   (PossessionRewards.h) where only the controller is paid directly and
 *   ZeroSumReward's teamSpirit handles credit-sharing with teammates.
 *
 *   Kickoff frames are excluded from payout (ball near center, near-zero
 *   velocity) since KickoffRewards.h / AdvancedKickoffRewards.h /
 *   KickoffStrategyReward.h already cover kickoff-win incentives —
 *   paying here too would be a duplicate signal on top of an already
 *   crowded kickoff-reward area.
 *
 *   Does NOT reward possession maintenance or penalize giveaways — that's
 *   PossessionControlRewardv2's job. This is a pure transition/gain signal.
 *   Replaces the transition bonus formerly in TimeToBalPossessionReward
 *   (PossessionRewards.h) — see Reward_Change_Log.md 2026-08-27.
 *
 *   Cooldown is a single global timer, not per-player: the trigger is
 *   inherently a global event (only one team can transition possession
 *   at a time, and only one player is ever paid per event), so a
 *   per-player cooldown map would add bookkeeping without changing
 *   behavior. This is a deliberate deviation from this codebase's usual
 *   per-player-cooldown-map convention.
 *
 * Author: Nathan + Claude
 * Date: August 2026
 * Revision: 1.0
 */

#include "Reward.h"
#include "../Math.h"
#include <atomic>
#include <cstdint>
#include <cfloat>
#include <cmath>

namespace RLGC {

    // ============================================================================
    // POSSESSION TRANSITION REWARD
    // 2026-08-26 | Rev 1.0 | NJH
    // ============================================================================
    class PossessionTransitionReward : public Reward {
    public:
        struct Config {
            float neutralMarginUU = 200.0f;  // |distOrange - distBlue| below this = contested/neutral
            float transitionReward = 1.0f;   // payout to the player who just won possession
            float cooldownSec = 0.5f;        // min time between payouts; guards against boundary flicker
        };

        struct Metrics {
            int    blueGains = 0;
            int    orangeGains = 0;
            double rewardSum = 0.0;
        };

    private:
        enum class PossState { BLUE, ORANGE, NEUTRAL };

        Config config;

        PossState lastState = PossState::NEUTRAL;
        PossState curState = PossState::NEUTRAL;
        uint32_t  winningCarId = 0;   // valid only when a transition fired this step
        bool      transitionFired = false;

        float gameTime = 0.0f;
        float lastPayoutTime = -1000.0f;

        inline static std::atomic<int> s_blueGains{ 0 };
        inline static std::atomic<int> s_orangeGains{ 0 };
        inline static std::atomic<long long> s_rewardMilli{ 0 };

    public:
        static Metrics GetAndResetMetrics() {
            Metrics m;
            m.blueGains = s_blueGains.exchange(0);
            m.orangeGains = s_orangeGains.exchange(0);
            m.rewardSum = double(s_rewardMilli.exchange(0)) / 1000.0;
            return m;
        }

        PossessionTransitionReward() : config() {}
        explicit PossessionTransitionReward(const Config& cfg) : config(cfg) {}

        virtual void Reset(const GameState& initialState) override {
            lastState = PossState::NEUTRAL;
            curState = PossState::NEUTRAL;
            winningCarId = 0;
            transitionFired = false;
            gameTime = 0.0f;
            lastPayoutTime = -1000.0f;
        }

        // PreStep runs exactly once per step — this is where the shared possession
        // state must be computed and mutated. Doing this in GetReward would recompute
        // and re-trigger the transition once per player instead of once per step.
        virtual void PreStep(const GameState& state) override {
            gameTime += (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

            float bestBlueDist = FLT_MAX;
            float bestOrangeDist = FLT_MAX;
            uint32_t bestBlueCarId = 0;
            uint32_t bestOrangeCarId = 0;

            for (const auto& p : state.players) {
                if (p.isDemoed) continue;
                float d = p.pos.Dist(state.ball.pos);
                if (p.team == Team::BLUE) {
                    if (d < bestBlueDist) { bestBlueDist = d; bestBlueCarId = p.carId; }
                } else {
                    if (d < bestOrangeDist) { bestOrangeDist = d; bestOrangeCarId = p.carId; }
                }
            }

            lastState = curState;
            transitionFired = false;

            float diff = bestOrangeDist - bestBlueDist; // positive => blue closer
            if (fabsf(diff) < config.neutralMarginUU) {
                curState = PossState::NEUTRAL;
            } else if (diff > 0.0f) {
                curState = PossState::BLUE;
                winningCarId = bestBlueCarId;
            } else {
                curState = PossState::ORANGE;
                winningCarId = bestOrangeCarId;
            }

            // Exclude kickoff: ball still centered and slow.
            const bool isKickoff =
                (state.ball.pos.Length() < 300.0f && state.ball.vel.Length() < 200.0f);

            const bool offCooldown = (gameTime - lastPayoutTime) >= config.cooldownSec;

            if (!isKickoff && offCooldown && curState != PossState::NEUTRAL && curState != lastState) {
                transitionFired = true;
                lastPayoutTime = gameTime;
                if (curState == PossState::BLUE) s_blueGains.fetch_add(1);
                else                              s_orangeGains.fetch_add(1);
            }
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!transitionFired) return 0.0f;
            if (player.carId != winningCarId) return 0.0f;

            s_rewardMilli.fetch_add(static_cast<long long>(config.transitionReward * 1000.0f));
            return config.transitionReward;
        }
    };

} // namespace RLGC
