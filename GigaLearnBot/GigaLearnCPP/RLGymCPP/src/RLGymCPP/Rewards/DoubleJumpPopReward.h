#pragma once

/*
 * File: DoubleJumpPopReward.h
 * Location: RLGymCPP/Rewards/
 *
 * Description:
 *   Event-gated reward for the "double jump pop" mechanic:
 *
 *     1. Player sustains a ground-dribble carry (ball on roof, on ground)
 *        for at least minCarryTime.
 *     2. Player jumps (first jump) while still carrying.
 *     3. Player double jumps shortly after — NEUTRAL only (hasDoubleJumped
 *        true). RocketSim guarantees hasDoubleJumped only rises on a
 *        non-flip jump input, so this is already exclusive of a directional
 *        dodge/flip (that's a different mechanic — a flick — already
 *        covered by AngleFlickRewards / CompositeFlickReward).
 *     4. Ball pops upward in a controlled band (not a violent flick) and
 *        stays close enough to the car that control is plausible.
 *     5. Optional bonus: ball settles into air-dribble proximity for a
 *        short sustain window after the pop, confirming the transition
 *        actually set up an air dribble rather than just batting the
 *        ball away.
 *
 *   All of 1-4 are required for base payout. #5 is a bonus multiplier only.
 *   Fires at most once per carry→pop sequence (cooldown-gated).
 *
 *   Does NOT supersede DribbleSetupReward's "cut and pop" path (that's a
 *   redirect off an approach with the ball staying grounded throughout,
 *   not a carry+double-jump). GroundToAirDribbleReward (CompleteMechanicsRewards.h)
 *   shares similar carry-detection geometry but is not currently registered
 *   in any training phase — flagged in RewardCatalog.md so it isn't
 *   reactivated without noticing the overlap.
 *
 * Author: Nathan + Claude
 * Date: June 2026
 * Revision: 1.1 — removed shadowMode (house style: ship live, never shadow)
 *   and the isFlipping dodge-rejection branch (confirmed dead code — see
 *   RocketSim Car.cpp: hasDoubleJumped and hasFlipped are mutually exclusive
 *   within a single jump cycle, so a flip can never satisfy the
 *   hasDoubleJumped rising-edge check in the first place).
 */

#include "Reward.h"
#include "../Math.h"
#include "RewardMath.h"
#include "../CommonValues.h"
#include <map>
#include <atomic>
#include <cstdint>

namespace RLGC {

    // ============================================================================
    // DOUBLE JUMP POP REWARD
    // 2026-06-30 | Rev 1.1 | NJH
    // ============================================================================
    class DoubleJumpPopReward : public Reward {
    public:
        struct Config {
            // --- Carry establishment (ball-on-car, on ground) ---
            float carryHeightMin       = 30.0f;   // Ball height above car roof, min
            float carryHeightMax       = 150.0f;  // Ball height above car roof, max
            float carryHorizMax        = 120.0f;  // Max horizontal offset (car-relative)
            float minCarryTime         = 0.30f;   // Must carry this long before pop counts

            // --- Jump → double jump timing window ---
            float maxJumpToDoubleSec   = 0.40f;   // Second jump must land within this of first
            float minJumpToDoubleSec   = 0.05f;   // Reject same-frame jump+double (input glitch)

            // --- Pop quality (controlled, not a flick) ---
            float minPopHeightGain     = 40.0f;   // Ball must gain at least this much z
            float maxPopHeightGain     = 350.0f;  // Above this it's a flick/launch, not a pop
            float maxPopBallSpeed      = 1700.0f; // Ball speed cap right after pop (soft contact)
            float maxCarBallDistAtPop  = 220.0f;  // Ball must still be near car at pop moment

            // --- Air dribble follow-through (bonus only) ---
            float followThroughWindow  = 0.6f;    // Time after pop to check for AD setup
            float adProximityMax       = 220.0f;  // Car-ball distance counted as "AD setup"
            float adRelSpeedMax        = 900.0f;  // |ballVel - carVel| counted as "controlled"
            float followThroughBonusMul = 1.6f;   // Multiplier if follow-through confirmed

            // --- Payouts ---
            float basePopPayout        = 0.4f;    // Base reward for a qualifying pop

            // --- Anti-farming ---
            float cooldownSec          = 1.5f;    // Per-player cooldown between payouts
        };

        struct Metrics {
            int carriesStarted   = 0;
            int jumpsFromCarry   = 0;
            int doubleJumps      = 0;     // neutral double jumps within window
            int popsQualified    = 0;
            int popsRejectedFar  = 0;     // ball too far from car at pop
            int popsRejectedMag  = 0;     // height/speed outside band
            int followThroughHit = 0;
            double rewardSum     = 0.0;

            double jumpRate          = 0.0; // jumpsFromCarry / carriesStarted
            double popQualifyRate    = 0.0; // popsQualified / doubleJumps
            double followThroughRate = 0.0; // followThroughHit / popsQualified
            double avgRewardPerPop   = 0.0; // rewardSum / popsQualified
        };

    private:
        enum class Phase { IDLE, CARRYING, AIRBORNE_FIRST_JUMP, ARMED_AFTER_POP };

        struct PlayerTracking {
            Phase    phase            = Phase::IDLE;
            float    carryTime        = 0.0f;
            float    timeSinceJump    = 0.0f;
            float    timeSincePop     = 0.0f;
            float    ballHeightAtJump = 0.0f;
            bool     followThroughSeen = false;
            float    lastPayoutTime   = -1000.0f;
        };

        mutable std::map<uint32_t, PlayerTracking> track;
        mutable float gameTime = 0.0f;

        Config config;

        // ---- Metrics (W&B) ----
        inline static std::atomic<int> s_carriesStarted   { 0 };
        inline static std::atomic<int> s_jumpsFromCarry    { 0 };
        inline static std::atomic<int> s_doubleJumps       { 0 };
        inline static std::atomic<int> s_popsQualified     { 0 };
        inline static std::atomic<int> s_popsRejectedFar   { 0 };
        inline static std::atomic<int> s_popsRejectedMag   { 0 };
        inline static std::atomic<int> s_followThroughHit  { 0 };
        inline static std::atomic<long long> s_rewardMilli { 0 };

        static bool IsBallOnCarGround(const Player& player, const GameState& state, const Config& cfg) {
            if (!player.isOnGround) return false;
            Vec rel = state.ball.pos - player.pos;
            float height = rel.z;
            float horiz = Vec(rel.x, rel.y, 0).Length();
            return (height > cfg.carryHeightMin && height < cfg.carryHeightMax && horiz < cfg.carryHorizMax);
        }

    public:
        static Metrics GetAndResetMetrics() {
            Metrics m;
            m.carriesStarted   = s_carriesStarted.exchange(0);
            m.jumpsFromCarry   = s_jumpsFromCarry.exchange(0);
            m.doubleJumps      = s_doubleJumps.exchange(0);
            m.popsQualified    = s_popsQualified.exchange(0);
            m.popsRejectedFar  = s_popsRejectedFar.exchange(0);
            m.popsRejectedMag  = s_popsRejectedMag.exchange(0);
            m.followThroughHit = s_followThroughHit.exchange(0);
            long long rm       = s_rewardMilli.exchange(0);
            m.rewardSum        = double(rm) / 1000.0;

            if (m.carriesStarted > 0)
                m.jumpRate = double(m.jumpsFromCarry) / m.carriesStarted;
            if (m.doubleJumps > 0)
                m.popQualifyRate = double(m.popsQualified) / m.doubleJumps;
            if (m.popsQualified > 0) {
                m.followThroughRate = double(m.followThroughHit) / m.popsQualified;
                m.avgRewardPerPop   = m.rewardSum / m.popsQualified;
            }

            return m;
        }

        DoubleJumpPopReward() : config() {}
        explicit DoubleJumpPopReward(const Config& cfg) : config(cfg) {}

        virtual void Reset(const GameState& initialState) override {
            track.clear();
            gameTime = 0.0f;
        }

        // PreStep runs once per step — gameTime must live here, not in GetReward,
        // since GetReward is called once per player per step.
        virtual void PreStep(const GameState& state) override {
            gameTime += (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            // Kickoff exclusion — a ball-on-car state can't legitimately occur off a kickoff anyway,
            // but wipe state defensively so kickoff chaos can't seed a false carry.
            const bool isKickoff =
                (state.ball.pos.Length() < 300.0f && state.ball.vel.Length() < 200.0f);
            if (isKickoff) {
                auto it = track.find(player.carId);
                if (it != track.end()) {
                    it->second = PlayerTracking{};
                }
                return 0.0f;
            }

            const float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            auto& tr = track[player.carId];
            float reward = 0.0f;

            const Player* prevPlayer = nullptr;
            if (state.prev && player.index < (int)state.prev->players.size())
                prevPlayer = &state.prev->players[player.index];

            switch (tr.phase) {

            // ================================================================
            // PHASE: IDLE — wait for a ball-on-car ground carry to begin
            // ================================================================
            case Phase::IDLE: {
                if (IsBallOnCarGround(player, state, config)) {
                    tr.phase = Phase::CARRYING;
                    tr.carryTime = 0.0f;
                    s_carriesStarted.fetch_add(1);
                }
                break;
            }

            // ================================================================
            // PHASE: CARRYING — sustain carry, watch for first jump
            // ================================================================
            case Phase::CARRYING: {
                if (!IsBallOnCarGround(player, state, config)) {
                    // Carry broken without a jump (ball rolled off, knocked away, etc.)
                    tr.phase = Phase::IDLE;
                    break;
                }
                tr.carryTime += dt;

                // First jump: isOnGround transitions true -> false, jump just pressed
                if (prevPlayer && prevPlayer->isOnGround && !player.isOnGround && player.hasJumped) {
                    if (tr.carryTime >= config.minCarryTime) {
                        s_jumpsFromCarry.fetch_add(1);
                        tr.phase = Phase::AIRBORNE_FIRST_JUMP;
                        tr.timeSinceJump = 0.0f;
                        tr.ballHeightAtJump = state.ball.pos.z;
                    } else {
                        // Jumped too early into the carry — not a real carry+pop, just a touch
                        tr.phase = Phase::IDLE;
                    }
                }
                break;
            }

            // ================================================================
            // PHASE: AIRBORNE_FIRST_JUMP — wait for a NEUTRAL double jump
            // ================================================================
            case Phase::AIRBORNE_FIRST_JUMP: {
                tr.timeSinceJump += dt;

                // Sequence died: landed without double jumping, or carry/jump window blown
                if (player.isOnGround || tr.timeSinceJump > config.maxJumpToDoubleSec) {
                    tr.phase = Phase::IDLE;
                    break;
                }

                // hasDoubleJumped only rises on a neutral (non-flip) second jump input —
                // RocketSim mutual-exclusion between hasFlipped/hasDoubleJumped guarantees
                // this can never be a directional dodge. No separate isFlipping check needed.
                const bool doubleJumpedNow =
                    player.hasDoubleJumped && prevPlayer && !prevPlayer->hasDoubleJumped;

                if (doubleJumpedNow) {
                    if (tr.timeSinceJump < config.minJumpToDoubleSec) {
                        // Too fast to be a real double jump input — treat as noise, drop sequence
                        tr.phase = Phase::IDLE;
                        break;
                    }

                    s_doubleJumps.fetch_add(1);

                    // Evaluate pop quality right now, at the moment of the double jump
                    float carBallDist = (state.ball.pos - player.pos).Length();
                    float heightGain = state.ball.pos.z - tr.ballHeightAtJump;
                    float ballSpeed = state.ball.vel.Length();

                    if (carBallDist > config.maxCarBallDistAtPop) {
                        s_popsRejectedFar.fetch_add(1);
                        tr.phase = Phase::IDLE;
                        break;
                    }

                    if (heightGain < config.minPopHeightGain ||
                        heightGain > config.maxPopHeightGain ||
                        ballSpeed > config.maxPopBallSpeed) {
                        s_popsRejectedMag.fetch_add(1);
                        tr.phase = Phase::IDLE;
                        break;
                    }

                    // Qualified pop. Cooldown check before paying out.
                    if (gameTime - tr.lastPayoutTime < config.cooldownSec) {
                        tr.phase = Phase::IDLE;
                        break;
                    }

                    s_popsQualified.fetch_add(1);
                    tr.lastPayoutTime = gameTime;

                    reward += config.basePopPayout;

                    // Arm follow-through check for the air-dribble-setup bonus
                    tr.phase = Phase::ARMED_AFTER_POP;
                    tr.timeSincePop = 0.0f;
                    tr.followThroughSeen = false;
                }
                break;
            }

            // ================================================================
            // PHASE: ARMED_AFTER_POP — base payout already given; check for
            // air-dribble follow-through to award the bonus multiplier
            // ================================================================
            case Phase::ARMED_AFTER_POP: {
                tr.timeSincePop += dt;

                if (!tr.followThroughSeen && !player.isOnGround) {
                    Vec rel = state.ball.pos - player.pos;
                    float dist = rel.Length();
                    Vec relSpeedVec = state.ball.vel - player.vel;
                    float relSpeed = relSpeedVec.Length();

                    if (dist < config.adProximityMax && relSpeed < config.adRelSpeedMax) {
                        tr.followThroughSeen = true;
                        s_followThroughHit.fetch_add(1);

                        float bonus = config.basePopPayout * (config.followThroughBonusMul - 1.0f);
                        reward += bonus;
                    }
                }

                if (tr.timeSincePop > config.followThroughWindow || player.isOnGround) {
                    tr.phase = Phase::IDLE;
                }
                break;
            }
            }

            if (reward <= 0.0f)
                return 0.0f;

            s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));
            return reward;
        }
    };

} // namespace RLGC
