/*
 * File: PowerslideTurnRewards.h
 * Location: RLGymCPP/Rewards/
 *
 * Description:
 *   Minimal, gradient-friendly rewards nudging the bot toward handbrake-based
 *   turning mechanics. Magnitudes intentionally kept in the 0.1-0.5 band to
 *   match PowerSlideControlReward and avoid dominating shaped-reward gradients.
 *
 *   Covers checklist items:
 *     - Sharper turns with handbrake          -> PowerslideSharpTurnReward
 *     - 180 turn, forward throttle            -> Powerslide180ForwardReward
 *     - 360 turn                              -> Powerslide360Reward
 *     - 180 turn, reverse throttle            -> Powerslide180ReverseReward
 *
 *   "Landing to conserve momentum" is already COMPLETE (AdvancedRecoveryReward /
 *   PowerslideJumpFlipReward in RecoveryRewards.h) and is NOT duplicated here.
 *
 * Design notes:
 *   - All event-gated on slide release (not paid per-frame), so total reward per
 *     maneuver is bounded and won't snowball with episode length.
 *   - Fail-fast ordered gates: require handbrake held + on-ground + minimum speed
 *     + actual heading change. No partial credit for incomplete slides.
 *   - Yaw accumulated from rotMat.forward heading delta per frame (sign-aware),
 *     so a slide that wobbles back and forth nets near zero rather than farming.
 *   - Wrap in ZeroSumReward only if you want these competitive; for pure mechanic
 *     acquisition leave them as raw self-rewards.
 *
 * Author: NJH + Claude
 * Date: May 2026
 * Revision: 1.0
 */

#pragma once

#include "Reward.h"
#include "../Math.h"
#include <map>
#include <cmath>
#include "../CommonValues.h"

namespace RLGC {

    namespace {
        // Signed heading (radians) of the car's forward vector in the ground plane.
        inline float GroundHeading(const Vec& forward) {
            return std::atan2(forward.y, forward.x);
        }

        // Smallest signed angular difference a-b wrapped to [-pi, pi].
        inline float WrapPi(float a) {
            while (a > M_PI)  a -= 2.0f * (float)M_PI;
            while (a < -M_PI) a += 2.0f * (float)M_PI;
            return a;
        }
    }

    // ========================================================================
    // POWERSLIDE SHARP TURN REWARD
    // Nudges using handbrake to tighten a turn: while sliding on the ground at
    // speed, reward angular velocity that exceeds what a non-slide turn yields.
    // Paid as a tiny per-frame trickle (capped) because "sharp turning" is a
    // continuous quality, not a discrete event.
    // ========================================================================
    class PowerslideSharpTurnReward : public Reward {
    private:
        // Above this ground angular speed (rad/s) we consider the turn "sharp".
        static constexpr float SHARP_ANGVEL = 1.8f;
        static constexpr float MIN_SPEED    = 500.0f;
        static constexpr float PER_FRAME_CAP = 0.05f;  // tiny

    public:
        virtual void Reset(const GameState& initialState) override {}

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!player.isOnGround) return 0.0f;
            if (player.prevAction.handbrake <= 0.5f) return 0.0f;

            float speed = player.vel.Length();
            if (speed < MIN_SPEED) return 0.0f;

            // Yaw-rate magnitude about the world up axis.
            float yawRate = std::abs(player.angVel.z);
            if (yawRate <= SHARP_ANGVEL) return 0.0f;

            float over = (yawRate - SHARP_ANGVEL) / SHARP_ANGVEL;   // [0, ~1+]
            float speedFrac = RS_MIN(1.0f, speed / 1500.0f);
            float r = PER_FRAME_CAP * RS_MIN(1.0f, over) * speedFrac;
            return r;
        }
    };

    // ========================================================================
    // SHARED BASE: accumulate signed heading change during a continuous slide,
    // pay out once on slide release if the target rotation band is hit while
    // throttle sign matches. Subclasses set the target window + throttle gate.
    // ========================================================================
    class PowerslideRotationRewardBase : public Reward {
    protected:
        struct SlideState {
            bool sliding = false;
            float accumYaw = 0.0f;       // signed, radians
            float lastHeading = 0.0f;
            float minSpeedSeen = 1e9f;
            int throttleSign = 0;        // dominant throttle sign during slide
            bool airborneViolation = false; // left the ground mid-slide
        };
        mutable std::map<uint32_t, SlideState> states;

        static constexpr float MIN_SPEED = 350.0f;

        // Subclass contract:
        virtual float TargetLow()  const = 0;   // radians, inclusive lower bound
        virtual float TargetHigh() const = 0;   // radians, inclusive upper bound
        virtual int   RequiredThrottleSign() const = 0; // +1 fwd, -1 rev, 0 any
        virtual float BaseReward() const = 0;

    public:
        virtual void Reset(const GameState& initialState) override { states.clear(); }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            uint32_t id = player.carId;
            auto& s = states[id];

            bool handbrakeDown = player.prevAction.handbrake > 0.5f;
            bool sliding = player.isOnGround && handbrakeDown;
            float heading = GroundHeading(player.rotMat.forward);

            // Airborne while a slide is in progress invalidates the whole maneuver
            // (we require the car to stay grounded throughout). Latch the violation
            // so it can't be cleared by touching back down before releasing.
            if (s.sliding && !player.isOnGround) {
                s.airborneViolation = true;
            }

            // --- Slide start ---
            if (sliding && !s.sliding) {
                s.sliding = true;
                s.accumYaw = 0.0f;
                s.lastHeading = heading;
                s.minSpeedSeen = player.vel.Length();
                s.throttleSign = 0;
                s.airborneViolation = false;
            }
            // --- Slide ongoing (only accumulate while genuinely on the ground) ---
            else if (sliding && s.sliding) {
                s.accumYaw += WrapPi(heading - s.lastHeading);
                s.lastHeading = heading;
                s.minSpeedSeen = RS_MIN(s.minSpeedSeen, player.vel.Length());

                float thr = player.prevAction.throttle;
                if (thr > 0.3f)      s.throttleSign += 1;
                else if (thr < -0.3f) s.throttleSign -= 1;
            }
            // --- Slide release: evaluate (handbrake let go, or went airborne) ---
            else if (s.sliding && !sliding) {
                s.sliding = false;

                // Gate: stayed grounded for the entire slide.
                if (s.airborneViolation) return 0.0f;

                // Gate: maintained speed through the slide (not a near-stop spin).
                if (s.minSpeedSeen < MIN_SPEED) return 0.0f;

                // Gate: throttle direction matched the maneuver definition.
                int req = RequiredThrottleSign();
                if (req != 0) {
                    int sign = (s.throttleSign > 0) ? 1 : (s.throttleSign < 0 ? -1 : 0);
                    if (sign != req) return 0.0f;
                }

                float mag = std::abs(s.accumYaw);
                if (mag >= TargetLow() && mag <= TargetHigh()) {
                    return BaseReward();
                }
                return 0.0f;
            }

            return 0.0f;
        }
    };

    // ========================================================================
    // 180 TURN WITH FORWARD THROTTLE
    // ~PI rotation (band 150-210 deg) while driving forward.
    // ========================================================================
    class Powerslide180ForwardReward : public PowerslideRotationRewardBase {
    protected:
        float TargetLow()  const override { return 2.618f; } // 150 deg
        float TargetHigh() const override { return 3.665f; } // 210 deg
        int   RequiredThrottleSign() const override { return +1; }
        float BaseReward() const override { return 0.4f; }
    };

    // ========================================================================
    // 180 TURN WITH REVERSE-THEN-FORWARD THROTTLE
    // The reverse half-flip-style turn: reverse throttle through the first ~90
    // degrees, then forward throttle through the second ~90 degrees, all while
    // staying grounded and sliding. Implemented as a dedicated two-phase state
    // machine rather than the throttle-sign base class.
    // ========================================================================
    class Powerslide180ReverseReward : public Reward {
    private:
        enum Phase { IDLE, PHASE_REVERSE, PHASE_FORWARD };

        struct State {
            Phase phase = IDLE;
            float accumYaw = 0.0f;      // signed total since slide start
            float phaseSwitchYaw = 0.0f; // |accumYaw| captured at rev->fwd switch
            float lastHeading = 0.0f;
            float minSpeedSeen = 1e9f;
            bool airborneViolation = false;
            bool throttleViolation = false; // wrong throttle sign for current phase
        };
        mutable std::map<uint32_t, State> states;

        static constexpr float MIN_SPEED      = 350.0f;
        static constexpr float HALF_TARGET     = 1.571f; // 90 deg, phase switch point
        static constexpr float HALF_TOLERANCE  = 0.524f; // +/-30 deg window on the switch
        static constexpr float FULL_LOW        = 2.618f; // 150 deg
        static constexpr float FULL_HIGH       = 3.665f; // 210 deg
        static constexpr float BASE_REWARD     = 0.4f;

    public:
        virtual void Reset(const GameState& initialState) override { states.clear(); }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            uint32_t id = player.carId;
            auto& s = states[id];

            bool handbrakeDown = player.prevAction.handbrake > 0.5f;
            bool sliding = player.isOnGround && handbrakeDown;
            float heading = GroundHeading(player.rotMat.forward);
            float thr = player.prevAction.throttle;

            if (s.phase != IDLE && !player.isOnGround) {
                s.airborneViolation = true;
            }

            // --- Slide start ---
            if (sliding && s.phase == IDLE) {
                s.phase = PHASE_REVERSE;
                s.accumYaw = 0.0f;
                s.phaseSwitchYaw = 0.0f;
                s.lastHeading = heading;
                s.minSpeedSeen = player.vel.Length();
                s.airborneViolation = false;
                s.throttleViolation = false;
            }
            // --- Slide ongoing ---
            else if (sliding && s.phase != IDLE) {
                s.accumYaw += WrapPi(heading - s.lastHeading);
                s.lastHeading = heading;
                s.minSpeedSeen = RS_MIN(s.minSpeedSeen, player.vel.Length());

                float mag = std::abs(s.accumYaw);

                if (s.phase == PHASE_REVERSE) {
                    // Must be applying reverse throttle through the first half.
                    if (thr > 0.3f) s.throttleViolation = true;

                    // Switch to forward phase once we've turned ~90 degrees.
                    if (mag >= HALF_TARGET) {
                        s.phase = PHASE_FORWARD;
                        s.phaseSwitchYaw = mag;
                    }
                }
                else if (s.phase == PHASE_FORWARD) {
                    // Must be applying forward throttle through the second half.
                    if (thr < -0.3f) s.throttleViolation = true;
                }
            }
            // --- Slide release: evaluate ---
            else if (s.phase != IDLE && !sliding) {
                Phase endPhase = s.phase;
                float accum = std::abs(s.accumYaw);
                float switchYaw = s.phaseSwitchYaw;
                bool air = s.airborneViolation;
                bool badThrottle = s.throttleViolation;
                float minSpeed = s.minSpeedSeen;
                s.phase = IDLE;

                if (air) return 0.0f;
                if (badThrottle) return 0.0f;
                if (minSpeed < MIN_SPEED) return 0.0f;

                // Must have actually completed the reverse->forward transition.
                if (endPhase != PHASE_FORWARD) return 0.0f;

                // The phase switch must have happened near the 90-degree mark.
                if (std::abs(switchYaw - HALF_TARGET) > HALF_TOLERANCE) return 0.0f;

                // And the full turn must land in the 180-degree band.
                if (accum >= FULL_LOW && accum <= FULL_HIGH) {
                    return BASE_REWARD;
                }
                return 0.0f;
            }

            return 0.0f;
        }
    };

    // ========================================================================
    // 360 TURN
    // ~2*PI rotation (band 320-400 deg) in a single continuous slide.
    // Throttle direction unconstrained. Slightly higher payout, still minimal.
    // ========================================================================
    class Powerslide360Reward : public PowerslideRotationRewardBase {
    protected:
        float TargetLow()  const override { return 5.585f; } // 320 deg
        float TargetHigh() const override { return 6.981f; } // 400 deg
        int   RequiredThrottleSign() const override { return 0; }
        float BaseReward() const override { return 0.5f; }
    };

} // namespace RLGC
