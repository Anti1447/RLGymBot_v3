/*
 * File: LateralPushDribbleReward.h
 * Location: RLGymCPP/Rewards/
 *
 * Description:
 *   Lateral push dribble setup. Rewards shepherding a GROUNDED ball that sits in
 *   FRONT of the car (not carried on the roof) and steering it ACROSS the field
 *   toward the opponent's FAR corner — kept SLOW (<2100 uu/s) and FLAT (|velZ|<300)
 *   so the bot can re-hit / flick at the end of the push.
 *
 *   Target objective:
 *     - The opponent corner FURTHEST from the ball's current position (cross-field), OR
 *     - Anywhere on the corner->net line, with HALFWAY between them as the preferred aim.
 *   Implemented as an aim cone whose angular bisector (the "halfway" axis) is max reward,
 *   with partial credit across the full corner<->net span and zero outside.
 *
 *   NOT a roof carry (see DribbleReward) and NOT the catch/pop mechanic
 *   (see DribbleSetupReward). This is the ground-level directional steering phase.
 *
 * Design notes:
 *   - Per-player state keyed by carId; all mutation touches only states[player.carId].
 *   - Multiplicative [0..1] gates: any weak dimension collapses the reward (no additive
 *     constellation farming). Fail-fast ordered hard gates early-return 0.
 *   - Magnitude in the 0.1-0.5 shaped band (rewardScale = 0.30, clamped to maxPerFrame).
 *   - shadowMode: compute + log metrics but return 0 (no optimizer gradient) for Machine 2.
 *   - Thread-safe atomic metrics via GetAndResetMetrics() shared across parallel envs.
 *
 * See LateralPushDribbleReward_Spec.md for the full anti-farming audit, W&B alert
 * thresholds, RocketSimVis checklist, rollout plan, and open empirical questions.
 *
 * Author: NJH + Claude
 * Date: 2026-06-30
 * Revision: 1.1
 *   - Rev 1.1 (NJH): cross-field component floor (push must carry the ball toward the
 *     far-corner x-side, not straight up-field) + y-depth taper (lateral push reward
 *     decays as the ball nears the opponent goal line so we don't steer all the way
 *     into the corner from deep).
 */

#pragma once

#include "Reward.h"
#include "../Math.h"
#include "../CommonValues.h"
#include <map>
#include <atomic>
#include <cmath>
#include <algorithm>

namespace RLGC {

    namespace {
        // Smoothstep mapping x in [edge0, edge1] -> [0,1] with cubic ease.
        inline float LPD_Smoothstep(float edge0, float edge1, float x) {
            if (edge1 <= edge0) return x >= edge1 ? 1.0f : 0.0f;
            float t = RS_CLAMP((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }
        // Horizontal (x,y) length of a Vec.
        inline float LPD_HorizLen(const Vec& v) {
            return std::sqrt(v.x * v.x + v.y * v.y);
        }
    }

    // ========================================================================
    // LATERAL PUSH DRIBBLE REWARD
    // ========================================================================
    class LateralPushDribbleReward : public Reward {
    public:
        struct Config {
            // --- Grounded / flat-roll gates (requested constraints) ---
            float lowBallMaxZ      = 150.0f;   // ball must be on/near ground
            float maxBallVelZ      = 300.0f;   // |ball.vel.z| ceiling (flat roll)

            // --- "In front of car" push geometry ---
            float minPushDist      = 60.0f;    // car nose roughly on the ball
            float maxPushDist      = 320.0f;   // but it's a push, ball ahead within this
            float minForwardOffset = 30.0f;    // ball ahead of car along forward
            float maxForwardOffset = 300.0f;
            float maxLateralOffset = 160.0f;   // lateral push allows some sideways offset

            // --- Speed band (requested: < 2100) ---
            float minBallSpeed     = 350.0f;   // must actually be rolling
            float maxBallSpeed     = 2100.0f;  // hard ceiling (re-hittable)
            float softSpeedKnee    = 1600.0f;  // full credit below; ramp down above

            // --- Target geometry ---
            float cornerAimX       = 3200.0f;  // |x| of corner aim point (inside wall)
            float cornerAimYInset  = 200.0f;   // pull corner off goal line toward midfield
            float coneSlack        = 0.18f;    // extra cone tolerance beyond corner<->net (rad)
            float cornerLatchMargin = 250.0f;  // hysteresis on far-corner choice (uu)

            // --- Cross-field component floor (push must be genuinely lateral) ---
            // Fraction of ball speed that must point toward the far-corner x-side.
            // Kills straight-up-field "shots" that the wide cone would otherwise pass
            // when the ball is far from goal (corner & net subtend a small angle there).
            float minCrossFrac     = 0.18f;    // >=0.18 of speed must be cross-field
            float crossFracBand    = 0.15f;    // ramp band above the floor -> full credit

            // --- Y-depth taper (don't steer to the corner from deep) ---
            // As the ball nears the opponent goal in y, the lateral push is wrong (tight
            // angle). Decay the reward from full at yTaperStart to yTaperFloor at yTaperEnd.
            float yTaperStart      = 3800.0f;  // |y toward goal| where taper begins
            float yTaperEnd        = 4800.0f;  // |y toward goal| where taper hits the floor
            float yTaperFloor      = 0.0f;     // residual multiplier at/after yTaperEnd

            // --- Shepherd / sustain / payout ---
            float recentTouchWindow = 0.45f;   // must have touched ball within this
            float sustainFullTime   = 0.50f;   // time to reach full sustain multiplier
            float rewardScale       = 0.30f;   // shaped magnitude (0.1-0.5 band)
            float maxPerFrame       = 0.50f;   // per-frame clamp

            bool  shadowMode        = false;   // log metrics, return 0 (Machine 2)
        };

        struct Metrics {
            int    activeFrames   = 0;
            int    eligibleFrames = 0;   // on-ground + low ball (context this is possible)
            double activationRate = 0.0; // active / eligible (%)
            double avgBallSpeed   = 0.0;
            double avgDirScore    = 0.0;
            double avgVelZ        = 0.0;
            double avgCrossComp   = 0.0; // mean cross-field fraction (toward far corner)
            double avgYTaper      = 0.0; // mean y-depth taper multiplier while active
            double closureRate    = 0.0; // mean per-frame ball->aim distance closed (uu)
            double rewardSum      = 0.0;
        };

    private:
        struct PlayerState {
            float touchTimer   = 999.0f; // time since last own touch
            float sustainTime  = 0.0f;   // accumulated active hold
            float lastAimDist  = -1.0f;  // ball->aim horiz distance last frame (closure)
            int   cornerSign   = 0;      // latched far-corner x sign (-1 / +1 / 0=unset)
        };

        mutable std::map<uint32_t, PlayerState> states;
        Config config;

        // Metrics -- thread-safe atomics (shared across all parallel envs)
        inline static std::atomic<int>       s_activeFrames   { 0 };
        inline static std::atomic<int>       s_eligibleFrames { 0 };
        inline static std::atomic<long long> s_speedMilli     { 0 }; // sum speed x1000
        inline static std::atomic<long long> s_dirScoreMilli  { 0 }; // sum dirScore x1000
        inline static std::atomic<long long> s_velZMilli      { 0 }; // sum |velZ| x1000
        inline static std::atomic<long long> s_crossMilli     { 0 }; // sum crossComp x1000
        inline static std::atomic<long long> s_yTaperMilli    { 0 }; // sum yTaper x1000
        inline static std::atomic<long long> s_closureMilli   { 0 }; // sum closure x1000
        inline static std::atomic<long long> s_rewardMilli    { 0 }; // sum reward x1000

    public:
        LateralPushDribbleReward() : config() {}
        explicit LateralPushDribbleReward(const Config& cfg) : config(cfg) {}

        static Metrics GetAndResetMetrics() {
            Metrics m;
            m.activeFrames   = s_activeFrames.exchange(0);
            m.eligibleFrames = s_eligibleFrames.exchange(0);
            long long sp  = s_speedMilli.exchange(0);
            long long ds  = s_dirScoreMilli.exchange(0);
            long long vz  = s_velZMilli.exchange(0);
            long long cx  = s_crossMilli.exchange(0);
            long long yt  = s_yTaperMilli.exchange(0);
            long long cl  = s_closureMilli.exchange(0);
            long long rw  = s_rewardMilli.exchange(0);
            m.rewardSum = double(rw) / 1000.0;
            if (m.activeFrames > 0) {
                m.avgBallSpeed = double(sp) / (1000.0 * m.activeFrames);
                m.avgDirScore  = double(ds) / (1000.0 * m.activeFrames);
                m.avgVelZ      = double(vz) / (1000.0 * m.activeFrames);
                m.avgCrossComp = double(cx) / (1000.0 * m.activeFrames);
                m.avgYTaper    = double(yt) / (1000.0 * m.activeFrames);
                m.closureRate  = double(cl) / (1000.0 * m.activeFrames);
            }
            if (m.eligibleFrames > 0) {
                m.activationRate = (double(m.activeFrames) / m.eligibleFrames) * 100.0;
            }
            return m;
        }

        virtual void Reset(const GameState& initialState) override {
            states.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float dt = (state.deltaTime > 0.0f) ? state.deltaTime : CommonValues::TICK_TIME;

            PlayerState& st = states[player.carId];

            // Advance own-touch timer (shepherd gate).
            if (player.ballTouchedStep) st.touchTimer = 0.0f;
            else                        st.touchTimer += dt;

            // --- Hard gate: demoed / airborne / ball not grounded ---
            if (player.isDemoed || !player.isOnGround ||
                state.ball.pos.z > config.lowBallMaxZ) {
                st.sustainTime = 0.0f;
                st.lastAimDist = -1.0f;
                return 0.0f;
            }

            // Eligible context (mechanic is at least possible here).
            s_eligibleFrames.fetch_add(1, std::memory_order_relaxed);

            // --- Hard gate: must be actively shepherding (recent own touch) ---
            if (st.touchTimer > config.recentTouchWindow) {
                st.sustainTime = 0.0f;
                st.lastAimDist = -1.0f;
                return 0.0f;
            }

            // --- Hard gate: flat roll ---
            float ballVelZAbs = std::fabs(state.ball.vel.z);
            if (ballVelZAbs > config.maxBallVelZ) {
                st.sustainTime = 0.0f;
                return 0.0f;
            }

            // --- In-front push geometry ---
            Vec diff = state.ball.pos - player.pos;
            float forwardOffset = player.rotMat.forward.Dot(diff);
            float lateralOffset = player.rotMat.right.Dot(diff);
            float horizDist     = LPD_HorizLen(diff);

            if (forwardOffset < config.minForwardOffset ||
                forwardOffset > config.maxForwardOffset ||
                std::fabs(lateralOffset) > config.maxLateralOffset ||
                horizDist < config.minPushDist ||
                horizDist > config.maxPushDist) {
                st.sustainTime = 0.0f;
                return 0.0f;
            }

            // --- Speed band ---
            float ballSpeed = LPD_HorizLen(state.ball.vel);
            if (ballSpeed < config.minBallSpeed || ballSpeed > config.maxBallSpeed) {
                st.sustainTime = 0.0f;
                return 0.0f;
            }

            // ----------------------------------------------------------------
            // Target geometry (horizontal plane)
            // ----------------------------------------------------------------
            float goalY = (player.team == Team::BLUE) ? CommonValues::BACK_WALL_Y
                                                      : -CommonValues::BACK_WALL_Y;
            float cornerY = goalY - (goalY > 0.0f ? config.cornerAimYInset
                                                  : -config.cornerAimYInset);

            Vec ballH(state.ball.pos.x, state.ball.pos.y, 0.0f);
            Vec cornerPos(config.cornerAimX, cornerY, 0.0f);
            Vec cornerNeg(-config.cornerAimX, cornerY, 0.0f);

            float distPos = LPD_HorizLen(cornerPos - ballH);
            float distNeg = LPD_HorizLen(cornerNeg - ballH);

            // Far corner with per-player sign latch + hysteresis (anti flip-flop at x~0).
            int desiredSign = (distPos >= distNeg) ? +1 : -1;
            if (st.cornerSign == 0) {
                st.cornerSign = desiredSign;
            } else if (desiredSign != st.cornerSign) {
                // Only flip if the new far corner is clearly farther.
                float curFar  = (st.cornerSign > 0) ? distPos : distNeg;
                float newFar  = (desiredSign  > 0) ? distPos : distNeg;
                if (newFar > curFar + config.cornerLatchMargin) st.cornerSign = desiredSign;
            }
            Vec farCorner = (st.cornerSign > 0) ? cornerPos : cornerNeg;
            Vec netCenter(0.0f, goalY, 0.0f);

            Vec toCorner = farCorner - ballH;
            Vec toNet    = netCenter - ballH;
            float lenCorner = LPD_HorizLen(toCorner);
            float lenNet    = LPD_HorizLen(toNet);
            if (lenCorner < 1.0f || lenNet < 1.0f) { st.sustainTime = 0.0f; return 0.0f; }

            Vec dirCorner = toCorner / lenCorner;
            Vec dirNet    = toNet / lenNet;
            Vec bisRaw    = dirCorner + dirNet;          // points to angular "halfway"
            float bisLen  = LPD_HorizLen(bisRaw);
            if (bisLen < 1e-3f) { st.sustainTime = 0.0f; return 0.0f; }
            Vec bisector  = bisRaw / bisLen;

            // Aim point used for the closure diagnostic (geometric midpoint).
            Vec aimMid = (farCorner + netCenter) * 0.5f;
            float aimDist = LPD_HorizLen(aimMid - ballH);

            // Cone: full span corner<->net, slack added, bisector = max.
            float coneCos   = RS_CLAMP(dirCorner.Dot(dirNet), -1.0f, 1.0f);
            float halfAngle = std::acos(coneCos) * 0.5f;
            float edgeCos   = std::cos(halfAngle + config.coneSlack);

            Vec ballVelDir(state.ball.vel.x, state.ball.vel.y, 0.0f);
            ballVelDir = ballVelDir / std::max(1.0f, ballSpeed);
            float align = ballVelDir.Dot(bisector);

            float dirScore = LPD_Smoothstep(edgeCos, 1.0f, align);
            if (dirScore <= 0.0f) { st.sustainTime = 0.0f; st.lastAimDist = aimDist; return 0.0f; }

            // --- Cross-field component floor ---
            // Component of ball velocity pointing toward the far-corner x-side, as a
            // fraction of speed. Must clear minCrossFrac or the push isn't cross-field.
            float crossComp   = (state.ball.vel.x * (float)st.cornerSign) / std::max(1.0f, ballSpeed);
            float crossFactor = LPD_Smoothstep(config.minCrossFrac,
                                               config.minCrossFrac + config.crossFracBand,
                                               crossComp);
            if (crossFactor <= 0.0f) { st.sustainTime = 0.0f; st.lastAimDist = aimDist; return 0.0f; }

            // --- Y-depth taper ---
            // yToward grows from own half toward the opponent goal line (|y|=5120).
            // Full reward until yTaperStart, decaying to yTaperFloor by yTaperEnd so we
            // stop steering toward the corner once the angle gets tight near goal.
            float yToward = (player.team == Team::BLUE) ? state.ball.pos.y : -state.ball.pos.y;
            float yTaperFactor = 1.0f;
            if (yToward > config.yTaperStart) {
                float t = (yToward - config.yTaperStart) /
                          std::max(1.0f, (config.yTaperEnd - config.yTaperStart));
                t = RS_CLAMP(t, 0.0f, 1.0f);
                yTaperFactor = 1.0f - (1.0f - config.yTaperFloor) * t;
            }
            if (yTaperFactor <= 0.0f) { st.sustainTime = 0.0f; st.lastAimDist = aimDist; return 0.0f; }

            // ----------------------------------------------------------------
            // Payout factors (each [0..1])
            // ----------------------------------------------------------------
            // Speed: full below knee, ramp down to ~0.3 at ceiling; short ramp off min.
            float speedFactor;
            if (ballSpeed <= config.softSpeedKnee) {
                float lo = config.minBallSpeed + 200.0f;
                speedFactor = (ballSpeed >= lo) ? 1.0f
                            : RS_CLAMP((ballSpeed - config.minBallSpeed) / 200.0f, 0.0f, 1.0f);
            } else {
                float t = (ballSpeed - config.softSpeedKnee) /
                          std::max(1.0f, (config.maxBallSpeed - config.softSpeedKnee));
                speedFactor = 1.0f - 0.7f * RS_CLAMP(t, 0.0f, 1.0f); // -> 0.3 at ceiling
            }

            float flatFactor   = 1.0f - RS_CLAMP(ballVelZAbs / config.maxBallVelZ, 0.0f, 1.0f);
            float centerFactor = 1.0f - RS_CLAMP(std::fabs(lateralOffset) / config.maxLateralOffset, 0.0f, 1.0f);
            float proxT        = RS_CLAMP((horizDist - config.minPushDist) /
                                          std::max(1.0f, (config.maxPushDist - config.minPushDist)), 0.0f, 1.0f);
            float proxFactor   = 1.0f - proxT;

            // Sustain ramp.
            st.sustainTime += dt;
            float sustain01 = RS_CLAMP(st.sustainTime / config.sustainFullTime, 0.0f, 1.0f);
            float sustainTerm = 0.2f + 0.8f * sustain01;
            if (st.sustainTime < 0.06f) sustainTerm *= (st.sustainTime / 0.06f);

            float reward = config.rewardScale
                         * dirScore
                         * crossFactor
                         * yTaperFactor
                         * speedFactor
                         * flatFactor
                         * (0.6f + 0.4f * centerFactor)
                         * (0.6f + 0.4f * proxFactor)
                         * sustainTerm;
            reward = RS_CLAMP(reward, 0.0f, config.maxPerFrame);

            // ----------------------------------------------------------------
            // Metrics
            // ----------------------------------------------------------------
            float closure = (st.lastAimDist >= 0.0f) ? (st.lastAimDist - aimDist) : 0.0f;
            st.lastAimDist = aimDist;

            s_activeFrames.fetch_add(1, std::memory_order_relaxed);
            s_speedMilli.fetch_add((long long)(ballSpeed * 1000.0f), std::memory_order_relaxed);
            s_dirScoreMilli.fetch_add((long long)(dirScore * 1000.0f), std::memory_order_relaxed);
            s_velZMilli.fetch_add((long long)(ballVelZAbs * 1000.0f), std::memory_order_relaxed);
            s_crossMilli.fetch_add((long long)(crossComp * 1000.0f), std::memory_order_relaxed);
            s_yTaperMilli.fetch_add((long long)(yTaperFactor * 1000.0f), std::memory_order_relaxed);
            s_closureMilli.fetch_add((long long)(closure * 1000.0f), std::memory_order_relaxed);
            s_rewardMilli.fetch_add((long long)(reward * 1000.0f), std::memory_order_relaxed);

            return config.shadowMode ? 0.0f : reward;
        }
    };

} // namespace RLGC
