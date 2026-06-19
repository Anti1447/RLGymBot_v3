#pragma once

/*
 * File: InvertedAirDribbleReward.h
 * Location: RLGymCPP/Rewards/  (or GigaLearnCPP reward dir)
 *
 * Description:
 *   Standalone air-dribble reward specialized for INVERTED aerials.
 *   Structurally based on the (deprecated) UnifiedAirDribbleReward, with:
 *     - dead modes (DEFENSIVE/GENERAL) and DetectMode collapse removed
 *     - "under the ball" gate KEPT (car.pos.z must stay below ball.pos.z)
 *     - high-aerial gate (car airborne above cfg.minCarHeight, default 400)
 *     - inverted-posture gate via a 90 deg arc on the up vector (10 deg tol each end)
 *     - nose-to-ball quality term (rotMat.forward points at ball)
 *     - inverted carries pay EXTRA; non-inverted pays cfg.notInvertedMultiplier
 *       (default 0.0 -> non-inverted carries are left to the standard AirDribbleReward)
 *
 *   Inverted definition (Nathan, 2026-06-08):
 *     car airborne, z > 400, and rotMat.up lies on the arc between "pointing
 *     toward the ground" and "pointing toward the horizontal velocity direction"
 *     (a 90 deg arc, ~10 deg tolerance on each end).
 *
 *   Shadow Mode: cfg.shadowMode = true logs all metrics but returns 0.0f to the
 *   optimizer. Recommended on Machine 2 for 200-500M steps before weight commit.
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

    class InvertedAirDribbleReward : public Reward {
    public:
        struct Config {
            // ---- Validity / high-aerial gate ----
            float minCarHeight     = 400.0f;   // car must be airborne above this (Nathan: z>400)
            float minBallHeight    = 400.0f;   // backstop; ball is above car so usually auto-true
            // (under-the-ball gate is structural: ball.pos.z must exceed car.pos.z)

            // ---- Inverted posture arc ----
            float toleranceDeg     = 10.0f;    // tolerance on each end of the 90 deg arc
            float minHorizSpeed    = 150.0f;   // UU/s; below this, forward-end is undefined

            // ---- Posture payout shaping ----
            float invertedMultiplier    = 1.5f;  // EXTRA reward when posture is in-arc
            float notInvertedMultiplier = 0.0f;  // <-- "less or 0"; 0 keeps this purely inverted
            float centerBonus           = 0.3f;  // up to +30% extra at arc center (-45 deg)

            // ---- Nose-to-ball quality (inverted case touches with the nose) ----
            float noseMinDot       = 0.3f;     // forward.toBall below this -> noseMul floor
            float noseMulFloor     = 0.5f;     // multiplier when nose is badly off-target
            float noseMulCeil      = 1.2f;     // multiplier when nose is on the ball

            // ---- Per-touch reward math (mirror of Unified) ----
            float progressScale    = 0.0015f;  // per UU of goalward ball progress
            float perTouchMax       = 0.40f;   // hard ceiling per touch (0.1-0.5 band)
            float seqRewardCap      = 2.0f;    // per-sequence accumulation cap
            float decayExponent     = 1.5f;    // diminishing returns curve
            float entryBonus        = 0.10f;   // first qualifying touch
            float secondTouchBonus  = 0.07f;   // second qualifying touch
            float minProgressUU     = 5.0f;    // ignore sub-threshold progress

            // ---- Directional (goalward) multiplier ----
            float goodDirThreshold  = 0.5f;    // ball-vel alignment to goal for "good"
            float minDirMultiplier  = 0.2f;

            // ---- Height-arc bonus ----
            float heightBonusThr1   = 200.0f;  float heightBonusMul1 = 1.15f;
            float heightBonusThr2   = 500.0f;  float heightBonusMul2 = 1.30f;

            // ---- Trajectory quality (BallPred) ----
            float trajCheckTime     = 0.75f;
            float trajScale         = 1200.0f;
            float trajMinFactor     = -0.3f;
            float trajMaxFactor     =  0.5f;

            // ---- Sequence limits ----
            float maxSeqDuration    = 5.0f;
            int   maxTouches        = 12;

            // ---- Ops ----
            bool  shadowMode        = true;
        };

    private:
        struct SequenceState {
            int   touchCount        = 0;
            float accumReward       = 0.0f;
            float timeElapsed       = 0.0f;
            float timeSinceLastTouch= 0.0f;
            bool  exhausted         = false;
            float startHeight       = 0.0f;
            bool  firstTouchProcessed = false;
        };

        Config cfg;
        mutable std::map<uint32_t, SequenceState> seqStates;
        BallPred::Cache _ballCache;  // updated once per step in PreStep()

        static constexpr float kPI = 3.14159265358979323846f;
        const float EPS_REWARD_SKIP = 1e-7f;

        // ---- Metrics ----
        mutable std::atomic<uint64_t> s_attempts{0};        // qualifying airborne touches
        mutable std::atomic<uint64_t> s_invertedTouches{0}; // touches passing the arc gate
        mutable std::atomic<uint64_t> s_canopyDown{0};      // posture near -90 deg
        mutable std::atomic<uint64_t> s_roofLeading{0};     // posture near 0 deg
        mutable std::atomic<uint64_t> s_noseTouches{0};     // forward.toBall >= goodDir
        mutable std::atomic<long long> s_rewardMilli{0};

        // ====================================================================
        // Inverted posture: returns true if up vector lies on the arc between
        // "toward ground" (theta=-90) and "toward horizontal velocity" (theta=0).
        // scoreOut in [0,1] is centeredness within the arc (1 at -45 deg center).
        // thetaDegOut is the raw angle for metric bucketing.
        // ====================================================================
        bool ComputeInvertedPosture(const Player& player, float& scoreOut, float& thetaDegOut) const {
            const Vec up = player.rotMat.up;

            Vec velH = player.vel; velH.z = 0.0f;
            const float velHLen = velH.Length();

            float theta; // radians, in the (h, z) plane, measured from +h (horizontal-forward) axis
            if (velHLen > cfg.minHorizSpeed) {
                const Vec h = velH / velHLen;
                const float a = up.x * h.x + up.y * h.y; // up . h_hat
                const float b = up.z;
                theta = std::atan2(b, a);
            } else {
                // No meaningful horizontal velocity: forward end undefined.
                // Collapse horizontal axis onto up's own horizontal projection (a >= 0),
                // reducing the test to a canopy-down check (theta in [-90, +90]).
                const float upHLen = std::sqrt(up.x * up.x + up.y * up.y);
                theta = std::atan2(up.z, upHLen);
            }

            thetaDegOut = theta * (180.0f / kPI);

            const float tol = cfg.toleranceDeg * (kPI / 180.0f);
            const float lo  = -kPI * 0.5f - tol;  // down end  - tol  (~ -100 deg)
            const float hi  =  0.0f       + tol;  // fwd  end  + tol  (~  +10 deg)

            const bool inArc = (theta >= lo && theta <= hi);

            if (inArc) {
                const float center   = -kPI * 0.25f;        // -45 deg
                const float halfSpan = (hi - lo) * 0.5f;
                scoreOut = RS_CLAMP(1.0f - std::fabs(theta - center) / halfSpan, 0.0f, 1.0f);
            } else {
                scoreOut = 0.0f;
            }
            return inArc;
        }

        Vec GetOpponentGoal(const Player& player) const {
            return (player.team == Team::BLUE)
                 ? CommonValues::ORANGE_GOAL_BACK
                 : CommonValues::BLUE_GOAL_BACK;
        }

        // Piecewise goalward alignment multiplier (mirrors Unified offensive path)
        float ComputeDirMultiplier(const Player& player, const GameState& state) const {
            Vec ballVel = state.ball.vel;
            float vlen = ballVel.Length();
            if (vlen < 1e-3f) return cfg.minDirMultiplier;
            Vec toGoal = (GetOpponentGoal(player) - state.ball.pos);
            float glen = toGoal.Length();
            if (glen < 1e-3f) return 1.0f;
            float alignment = (ballVel / vlen).Dot(toGoal / glen); // [-1,1]
            if (alignment <= 0.0f)
                return cfg.minDirMultiplier * 0.5f;
            if (alignment < cfg.goodDirThreshold)
                return cfg.minDirMultiplier
                     + (1.0f - cfg.minDirMultiplier) * (alignment / cfg.goodDirThreshold) * 0.7f;
            return 0.7f + 0.3f * ((alignment - cfg.goodDirThreshold) / (1.0f - cfg.goodDirThreshold));
        }

        // Nose-to-ball: in the inverted carry we want to touch with the nose.
        float ComputeNoseMultiplier(const Player& player, const GameState& state, bool& noseGood) const {
            Vec toBall = state.ball.pos - player.pos;
            float d = toBall.Length();
            noseGood = false;
            if (d < 1e-3f) return 1.0f;
            float fwdDot = player.rotMat.forward.Dot(toBall / d); // [-1,1]
            noseGood = (fwdDot >= cfg.goodDirThreshold);
            float t = RS_CLAMP((fwdDot - cfg.noseMinDot) / (1.0f - cfg.noseMinDot), 0.0f, 1.0f);
            return cfg.noseMulFloor + (cfg.noseMulCeil - cfg.noseMulFloor) * t;
        }

    public:
        InvertedAirDribbleReward() : cfg() {}
        explicit InvertedAirDribbleReward(const Config& c) : cfg(c) {}

        virtual void Reset(const GameState& initialState) override {
            seqStates.clear();
        }

        // BallPred cache updated exactly once per step.
        virtual void PreStep(const GameState& state) override {
            _ballCache.Update(state);  // match the API used by the live air-dribble rewards
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            const uint32_t id = player.carId;
            const float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

            // ---- Validity gate (KEEP under-the-ball; high-aerial only) ----
            const bool underBall = state.ball.pos.z > player.pos.z;
            const bool invalid =
                   player.isOnGround
                || player.pos.z   < cfg.minCarHeight
                || state.ball.pos.z < cfg.minBallHeight
                || !underBall;
            if (invalid) {
                seqStates.erase(id);
                return 0.0f;
            }

            // Advance time-since-last-touch every live frame (before the touch early-out).
            {
                auto it = seqStates.find(id);
                if (it != seqStates.end())
                    it->second.timeSinceLastTouch += dt;
            }

            if (!state.prev || !player.ballTouchedStep)
                return 0.0f;

            // Init sequence on first qualifying touch
            if (seqStates.find(id) == seqStates.end()) {
                SequenceState s;
                s.startHeight = state.ball.pos.z;
                seqStates[id] = s;
            }
            SequenceState& seq = seqStates[id];
            seq.timeElapsed += dt;
            seq.timeSinceLastTouch = 0.0f;

            if (seq.exhausted) return 0.0f;
            if (seq.timeElapsed > cfg.maxSeqDuration || seq.touchCount >= cfg.maxTouches) {
                seq.exhausted = true;
                return 0.0f;
            }

            s_attempts.fetch_add(1);

            // ---- Inverted posture gate ----
            float postureScore = 0.0f, thetaDeg = 0.0f;
            bool inArc = ComputeInvertedPosture(player, postureScore, thetaDeg);

            // ---- Goalward progress (mirror of Unified) ----
            Vec oppGoal   = GetOpponentGoal(player);
            float prevDist= (state.prev->ball.pos - oppGoal).Length();
            float curDist = (state.ball.pos       - oppGoal).Length();
            float progress= RS_MAX(0.0f, prevDist - curDist);
            if (progress < cfg.minProgressUU) return 0.0f;

            float perTouch = RS_MIN(cfg.perTouchMax, progress * cfg.progressScale);

            float usedFrac = RS_CLAMP(seq.accumReward / cfg.seqRewardCap, 0.0f, 1.0f);
            float decay    = std::pow(RS_MAX(0.0f, 1.0f - usedFrac), cfg.decayExponent);
            float remCap   = RS_MAX(0.0f, cfg.seqRewardCap - seq.accumReward);
            if (remCap <= 0.0f) { seq.exhausted = true; return 0.0f; }

            float reward = RS_MIN(perTouch * decay, remCap);

            // Directional multiplier
            float dirMul = ComputeDirMultiplier(player, state);
            reward *= dirMul;

            // Entry / second-touch bonuses
            if (seq.touchCount == 0)
                reward += cfg.entryBonus * RS_MIN(1.0f, dirMul);
            else if (seq.touchCount == 1)
                reward += cfg.secondTouchBonus * RS_MIN(1.0f, dirMul);

            // Height-arc bonus
            float heightGain = state.ball.pos.z - seq.startHeight;
            if      (heightGain >= cfg.heightBonusThr2) reward *= cfg.heightBonusMul2;
            else if (heightGain >= cfg.heightBonusThr1) reward *= cfg.heightBonusMul1;

            // Trajectory quality (BallPred 0.75s lookahead toward goal)
            if (_ballCache.valid) {
                Vec futurePos    = _ballCache.AtTime(cfg.trajCheckTime).pos;
                float futureDist = (futurePos - oppGoal).Length();
                float improvement= curDist - futureDist;
                float trajFactor = RS_CLAMP(improvement / cfg.trajScale, cfg.trajMinFactor, cfg.trajMaxFactor);
                reward *= (1.0f + trajFactor);
            }

            // Nose-to-ball quality (touch with the nose while inverted)
            bool noseGood = false;
            reward *= ComputeNoseMultiplier(player, state, noseGood);

            // ---- Inverted posture payout: extra if in-arc, else less/zero ----
            float postureMul;
            if (inArc) {
                postureMul = cfg.invertedMultiplier * (1.0f + cfg.centerBonus * postureScore);
                s_invertedTouches.fetch_add(1);
                if (thetaDeg < -45.0f) s_canopyDown.fetch_add(1);
                else                   s_roofLeading.fetch_add(1);
            } else {
                postureMul = cfg.notInvertedMultiplier;
            }
            reward *= postureMul;

            if (noseGood) s_noseTouches.fetch_add(1);

            if (reward <= EPS_REWARD_SKIP) {
                seq.touchCount += 1;
                return 0.0f;
            }

            seq.accumReward += reward;
            seq.touchCount  += 1;
            if (seq.accumReward >= cfg.seqRewardCap - 1e-3f) seq.exhausted = true;

            s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));

            return cfg.shadowMode ? 0.0f : reward;
        }

        // ---- W&B metric accessors (call + reset per log interval) ----
        struct Metrics {
            uint64_t attempts, invertedTouches, canopyDown, roofLeading, noseTouches;
            double   rewardSum, invertedRate;
        };
        Metrics CollectMetrics() {
            Metrics m{};
            m.attempts        = s_attempts.exchange(0);
            m.invertedTouches = s_invertedTouches.exchange(0);
            m.canopyDown      = s_canopyDown.exchange(0);
            m.roofLeading     = s_roofLeading.exchange(0);
            m.noseTouches     = s_noseTouches.exchange(0);
            m.rewardSum       = double(s_rewardMilli.exchange(0)) / 1000.0;
            m.invertedRate    = (m.attempts > 0) ? double(m.invertedTouches) / m.attempts : 0.0;
            return m;
        }
    };

} // namespace RLGC
