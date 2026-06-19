#pragma once

/*
 * File: TornadoSpinTakeoffReward.h
 * Location: RLGymCPP/Rewards/  (or GigaLearnCPP reward dir)
 *
 * Description:
 *   Rewards a "tornado spin" takeoff: simultaneous yaw and air-roll inputs of
 *   OPPOSITE sign (yaw +1 / roll -1, OR yaw -1 / roll +1), applied during the
 *   takeoff window, used productively to either:
 *     (a) orient the car's nose toward the ball / intercept point, or
 *     (b) invert the hood (drive up.z down) for an inverted-aerial entry.
 *
 *   Both chiralities collapse to:  tornadoMag = clamp(-yaw*roll, 0, 1)
 *     yaw +1, roll -1 -> +1 ;  yaw -1, roll +1 -> +1 ;  same-sign turn -> 0.
 *
 *   Anti-farm: payout is gated on PRODUCTIVENESS measured as a positive delta —
 *   nose converging on the ball  (d(forward.toBall) > 0)  and/or hood inverting
 *   (up.z decreasing). Deltas are self-limiting, so spinning in place earns
 *   nothing once alignment/inversion stops improving. Capped per takeoff.
 *
 *   Window: from leaving the ground until cfg.takeoffWindowSec elapses or the
 *   ball is touched (takeoff is "spent" once you make contact).
 *
 *   Shadow Mode: cfg.shadowMode = true logs metrics, returns 0.0f to optimizer.
 *
 * Author: Nathan + Claude
 * Date: 2026-06-08
 * Revision: 1.0
 */

#include "Reward.h"
#include "../Math.h"
#include "RewardMath.h"
#include "../CommonValues.h"
#include "BallPrediction.h"
#include <map>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace RLGC {

    class TornadoSpinTakeoffReward : public Reward {
    public:
        struct Config {
            // ---- Takeoff window ----
            float takeoffWindowSec = 0.8f;    // reward only within this long after leaving ground
            float minCarHeightArm  = 30.0f;   // must clear this to count as "airborne takeoff"

            // ---- Tornado input gate ----
            float minTornadoInput  = 0.4f;    // clamp(-yaw*roll,0,1) must exceed this
            float minSpinAngVel    = 1.5f;    // optional: require real rotation (rad/s); 0 disables

            // ---- Productiveness weighting ----
            float alignWeight      = 1.0f;    // reward for nose converging on ball/intercept
            float invertWeight     = 0.7f;    // reward for hood inverting (up.z dropping)
            float scale            = 0.8f;    // overall magnitude scale

            // ---- Intercept aim ----
            bool  aimAtCurrentBall    = false;  // false: BallPred kinematic intercept (recommended)
            float interceptLeadSec    = 0.6f;   // fallback lead if no feasible intercept is found
            float maxInterceptHorizon = 2.5f;   // only consider ball slices within this many seconds
            float interceptAccel      = 1060.0f;// UU/s^2 effective car accel (throttle+boost estimate)
            float maxInterceptSpeed   = 2300.0f;// UU/s supersonic cap on reachable speed
            float interceptPadUU      = 92.0f;  // reach slack (~ball radius) so grazing counts

            // ---- Caps ----
            float perTakeoffCap    = 0.5f;    // total reward per takeoff (0.1-0.5 band friendly)

            // ---- Ops ----
            bool  shadowMode       = true;
        };

    private:
        struct TakeoffState {
            bool  wasOnGround    = true;
            bool  active         = false;   // inside a takeoff window
            float timeSinceTakeoff = 0.0f;
            float accumReward    = 0.0f;
            float prevFwdDotBall = 0.0f;
            float prevUpZ        = 1.0f;
            bool  initialized    = false;
        };

        Config cfg;
        mutable std::map<uint32_t, TakeoffState> states;
        BallPred::Cache _ballCache;   // updated once per step in PreStep()

        const float EPS = 1e-6f;

        // ---- Metrics ----
        mutable std::atomic<uint64_t> s_takeoffs{0};
        mutable std::atomic<uint64_t> s_tornadoTakeoffs{0};  // takeoffs with qualifying spin
        mutable std::atomic<uint64_t> s_alignDriven{0};      // frames paid via alignment gain
        mutable std::atomic<uint64_t> s_invertDriven{0};     // frames paid via hood inversion
        mutable std::atomic<long long> s_rewardMilli{0};

        // Earliest predicted ball point the car can plausibly reach.
        // Walks the BallPred slices and returns the first whose required travel
        // distance is within the car's kinematic reach (current speed + accel,
        // capped at supersonic). Falls back to a fixed lead if nothing is feasible.
        Vec GetAimPoint(const Player& player, const GameState& state) const {
            if (cfg.aimAtCurrentBall || !_ballCache.valid)
                return state.ball.pos;

            const Vec   carPos   = player.pos;
            const float curSpeed = player.vel.Length();

            for (int i = 0; i < BallPred::NUM_SLICES; i++) {
                const BallPred::BallSlice& sl = _ballCache.slices[i];
                if (sl.t > cfg.maxInterceptHorizon) break;

                const float dist  = (sl.pos - carPos).Length();
                // Reachable distance under constant-accel kinematics, capped at vmax.
                float reach = curSpeed * sl.t + 0.5f * cfg.interceptAccel * sl.t * sl.t;
                reach = RS_MIN(reach, cfg.maxInterceptSpeed * sl.t);

                if (reach + cfg.interceptPadUU >= dist)
                    return sl.pos;   // earliest interceptable slice
            }
            // No feasible early intercept within the horizon — aim at a short lead.
            return _ballCache.AtTime(cfg.interceptLeadSec).pos;
        }

    public:
        TornadoSpinTakeoffReward() : cfg() {}
        explicit TornadoSpinTakeoffReward(const Config& c) : cfg(c) {}

        virtual void Reset(const GameState& initialState) override {
            states.clear();
        }

        // Ball-trajectory cache updated exactly once per step (before GetReward
        // loops over players), so the intercept solve is computed once, not N times.
        virtual void PreStep(const GameState& state) override {
            _ballCache.Update(state);
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            const uint32_t id = player.carId;
            const float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

            TakeoffState& st = states[id]; // default-constructs on first sight

            // ---- Grounded: arm for next takeoff ----
            if (player.isOnGround) {
                st.wasOnGround = true;
                st.active      = false;
                st.accumReward = 0.0f;
                st.initialized = false;
                return 0.0f;
            }

            // ---- Detect takeoff edge (ground -> air, clear of the floor) ----
            if (st.wasOnGround && player.pos.z >= cfg.minCarHeightArm) {
                st.wasOnGround       = false;
                st.active            = true;
                st.timeSinceTakeoff  = 0.0f;
                st.accumReward       = 0.0f;
                st.initialized       = false;
                s_takeoffs.fetch_add(1);
            }

            if (!st.active) return 0.0f;

            st.timeSinceTakeoff += dt;

            // Window closes on timeout or on ball contact (takeoff is spent)
            if (st.timeSinceTakeoff > cfg.takeoffWindowSec || player.ballTouchedStep) {
                st.active = false;
                return 0.0f;
            }

            // ---- Aim geometry ----
            Vec aim   = GetAimPoint(player, state);
            Vec toAim = aim - player.pos;
            float d   = toAim.Length();
            float fwdDot = (d > EPS) ? player.rotMat.forward.Dot(toAim / d) : st.prevFwdDotBall;
            float upZ    = player.rotMat.up.z;

            // Initialize deltas on the first active frame (no payout that frame)
            if (!st.initialized) {
                st.prevFwdDotBall = fwdDot;
                st.prevUpZ        = upZ;
                st.initialized    = true;
                return 0.0f;
            }

            // ---- Tornado input magnitude (both chiralities) ----
            float tornadoMag = RS_CLAMP(-player.prevAction.yaw * player.prevAction.roll, 0.0f, 1.0f);

            // Optional: require the car to actually be rotating
            bool spinning = (cfg.minSpinAngVel <= 0.0f) || (player.angVel.Length() >= cfg.minSpinAngVel);

            // Productiveness deltas (self-limiting; only positive change pays)
            float alignGain  = RS_MAX(0.0f, fwdDot - st.prevFwdDotBall); // nose converging
            float invertGain = RS_MAX(0.0f, st.prevUpZ - upZ);           // hood inverting

            st.prevFwdDotBall = fwdDot;
            st.prevUpZ        = upZ;

            float reward = 0.0f;
            if (tornadoMag >= cfg.minTornadoInput && spinning) {
                float productive = cfg.alignWeight * alignGain + cfg.invertWeight * invertGain;
                reward = tornadoMag * productive * cfg.scale;

                // Cap per takeoff
                float rem = RS_MAX(0.0f, cfg.perTakeoffCap - st.accumReward);
                reward = RS_MIN(reward, rem);

                if (reward > EPS) {
                    st.accumReward += reward;
                    if (alignGain  > invertGain) s_alignDriven.fetch_add(1);
                    else                          s_invertDriven.fetch_add(1);
                    if (st.accumReward > EPS && reward == st.accumReward)
                        s_tornadoTakeoffs.fetch_add(1); // first paid frame of this takeoff
                    s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));
                }
            }

            return cfg.shadowMode ? 0.0f : reward;
        }

        // ---- W&B metric accessors ----
        struct Metrics {
            uint64_t takeoffs, tornadoTakeoffs, alignDriven, invertDriven;
            double   rewardSum, tornadoRate;
        };
        Metrics CollectMetrics() {
            Metrics m{};
            m.takeoffs        = s_takeoffs.exchange(0);
            m.tornadoTakeoffs = s_tornadoTakeoffs.exchange(0);
            m.alignDriven     = s_alignDriven.exchange(0);
            m.invertDriven    = s_invertDriven.exchange(0);
            m.rewardSum       = double(s_rewardMilli.exchange(0)) / 1000.0;
            m.tornadoRate     = (m.takeoffs > 0) ? double(m.tornadoTakeoffs) / m.takeoffs : 0.0;
            return m;
        }
    };

} // namespace RLGC
