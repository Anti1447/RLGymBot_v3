/*
 * File: PowerShotRewards.h
 * Location: RLGymCPP/Rewards/
 *
 * Description:
 *   Power-shot rewards built around the *physics of adding momentum*, not just
 *   absolute ball speed. This is the gap left by the existing power rewards
 *   (PowerShotReward / SpeedScaledShotReward / StrongTouchReward / TouchAccelReward),
 *   all of which key on absolute ball speed or speed-toward-goal and therefore
 *   miss WHY a hit was powerful.
 *
 *   Three physics principles, each its own component so gradients stay separable:
 *
 *   1. IMPACT ANGLE (PowerShotImpactAngleReward)
 *      Measured between the car's hit direction and the ball's PRE-impact
 *      velocity direction.
 *        - 90-180 deg (hitting INTO the ball's momentum): full power band.
 *        - 0-90 deg  (hitting ALONG the ball's momentum): reduced credit, not
 *          zero — "accuracy > power, we can still do it."
 *      Fades out when the ball is slow/stationary (no meaningful prior direction).
 *
 *   2. MASS ALIGNMENT THROUGH CENTER (PowerShotMassAlignmentReward)
 *      Most momentum transfers when the car's mass drives through the ball's
 *      center. Best contact point: front-left / front-right corner; second best:
 *      nose. Derived from ballHitInfo.relativePosOnBall + car forward alignment.
 *
 *   3. DODGE TIMING AT CONTACT (PowerShotDodgeTimingReward)
 *      A dodge (flip) THROUGH the ball at the moment of contact adds power, but
 *      mistimed dodges waste it. Rewards a flip active at the touch; penalizes a
 *      flip that has already ended before contact (whiff).
 *
 *   Shot-context gating (PowerShotContextGate) restricts any inner component to
 *   one of four contexts:
 *     GROUND       — on-ground, ball z ≤ 260 UU
 *     HALF_VOLLEY  — airborne, ball z ≤ 260 UU AND ball rising (vel.z > 0)
 *     VOLLEY       — airborne, ball z > 260 UU
 *     AERIAL_NODODGE — airborne, no flip/jump available, ball z > 260 UU
 *   The AERIAL_NODODGE context intentionally suppresses the dodge-timing
 *   component (no dodge to time).
 *
 * Config wiring (RewardConfig.cpp):
 *   Multiply ImpactAngle × MassAlignment so a powerful hit needs both correct
 *   angle AND correct contact geometry. Wrap in PowerShotContextGate for
 *   per-context weight tuning. See end-of-file CONFIG NOTES for snippet.
 *
 * Watchpoint:
 *   ImpactAngle uses (ball.pos - car.pos) as hit-direction proxy. If W&B
 *   shows angle scores decorrelating from RocketSimVis geometry, swap to
 *   player.vel.Normalized() — the car's mass vector, arguably more physically
 *   accurate for momentum transfer. One variable, one rebuild.
 *
 * Author: NJH + Claude
 * Date: May 2026
 * Revision: 1.0
 */

#pragma once

#include "Reward.h"
#include "../Math.h"
#include <cmath>
#include "../CommonValues.h"

namespace RLGC {

    namespace {
        // Map an angle (radians, 0..pi) to a power factor.
        //   0–90 deg  → low band  [lowFloor, 0.5]   (hitting along ball's motion)
        //   90–180 deg → high band [0.5, 1.0]        (hitting into ball's motion)
        // lowFloor keeps the 0-90 region nonzero ("we can still do it").
        inline float AngleToPowerFactor(float angleRad, float lowFloor) {
            const float half = (float)M_PI * 0.5f;
            const float midShare = 0.5f;  // value at exactly 90 deg
            if (angleRad <= half) {
                float t = angleRad / half;  // [0, 1]
                return lowFloor + (midShare - lowFloor) * t;
            } else {
                float t = (angleRad - half) / half;  // [0, 1]
                return midShare + (1.0f - midShare) * t;
            }
        }
    }

    // =========================================================================
    // 1. IMPACT ANGLE REWARD
    //
    // Angle between the car's hit direction and the ball's PRE-impact velocity.
    // Hitting into the ball's momentum (90–180°) gives the most power.
    // Hitting along it (0–90°) gives reduced but nonzero credit (floor = 0.15).
    // Fades to zero when the ball is slow/stationary (no meaningful direction).
    // =========================================================================
    class PowerShotImpactAngleReward : public Reward {
    private:
        // Below this pre-impact ball speed there is no meaningful momentum to
        // "hit into"; reward fades to zero at and below this value.
        static constexpr float MIN_PRIOR_BALL_SPEED  = 400.0f;
        // At this speed the prior-direction signal is fully weighted.
        static constexpr float FULL_PRIOR_BALL_SPEED = 1200.0f;
        // Nonzero floor for the 0–90° region: accuracy still has value.
        static constexpr float LOW_BAND_FLOOR = 0.15f;

    public:
        virtual void Reset(const GameState& initialState) override {}

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!player.ballTouchedStep) return 0.0f;
            if (!state.prev) return 0.0f;

            // Ball velocity BEFORE we hit it.
            Vec priorBallVel = state.prev->ball.vel;
            float priorSpeed = priorBallVel.Length();
            if (priorSpeed < MIN_PRIOR_BALL_SPEED) return 0.0f;

            Vec priorDir = priorBallVel / priorSpeed;

            // Car's hit direction: direction the car is driving the ball.
            // Best cheap proxy: normalized car→ball vector at contact.
            // (See watchpoint in file header if this needs swapping.)
            Vec carToBall = state.ball.pos - player.pos;
            float ctbLen  = carToBall.Length();
            Vec hitDir    = (ctbLen > 1e-3f) ? (carToBall / ctbLen)
                                              : player.rotMat.forward;

            // Angle between hit direction and the ball's PRIOR travel direction.
            // dot ~  1  → same direction → 0° → along momentum (low power)
            // dot ~ -1  → opposite       → 180° → into momentum (full power)
            float d     = RS_CLAMP(hitDir.Dot(priorDir), -1.0f, 1.0f);
            float angle = std::acos(d);  // [0, pi]

            float powerFactor = AngleToPowerFactor(angle, LOW_BAND_FLOOR);

            // Scale by how much prior momentum there was to act on.
            float priorFrac = RS_CLAMP(
                (priorSpeed - MIN_PRIOR_BALL_SPEED) /
                (FULL_PRIOR_BALL_SPEED - MIN_PRIOR_BALL_SPEED),
                0.0f, 1.0f);

            return powerFactor * priorFrac;  // [0, 1]
        }
    };

    // =========================================================================
    // 2. MASS ALIGNMENT THROUGH CENTER REWARD
    //
    // Rewards driving the car's mass through the ball's center of mass.
    //   - Front-left / front-right corner contact: best (weight 1.0)
    //   - Nose contact: second best (weight 0.7)
    // Both multiplied by how well the car's forward vector passes through center.
    // =========================================================================
    class PowerShotMassAlignmentReward : public Reward {
    private:
        static constexpr float CORNER_WEIGHT  = 1.0f;   // front corners
        static constexpr float NOSE_WEIGHT    = 0.7f;   // nose, straight-on
        // Minimum dot product of car-forward vs car→ball to qualify as
        // "driving through center." Below this, the car is side-swiping.
        static constexpr float MIN_CENTER_DOT = 0.5f;

    public:
        virtual void Reset(const GameState& initialState) override {}

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!player.ballTouchedStep) return 0.0f;
            if (!player.prev || !player.prev->ballHitInfo.isValid) return 0.0f;

            // How well is the car's mass driving THROUGH the ball center?
            Vec carToBall = state.ball.pos - player.pos;
            float len = carToBall.Length();
            if (len < 1e-3f) return 0.0f;
            Vec toBallDir  = carToBall / len;
            float centerDot = player.rotMat.forward.Dot(toBallDir);
            if (centerDot < MIN_CENTER_DOT) return 0.0f;
            float centerQuality = RS_CLAMP(
                (centerDot - MIN_CENTER_DOT) / (1.0f - MIN_CENTER_DOT),
                0.0f, 1.0f);

            // Where on the ball did we contact?
            // relativePosOnBall is a world-space Vec from ball center to the
            // contact point (~BALL_RADIUS in magnitude). Project it into the
            // car's frame to distinguish front-corner vs nose.
            Vec rel    = player.prev->ballHitInfo.relativePosOnBall;
            float rLen = rel.Length();
            if (rLen < 1e-3f) {
                // Dead-center contact → treat as nose-quality hit.
                return NOSE_WEIGHT * centerQuality;
            }
            Vec relDir = rel / rLen;

            // How much of the contact offset is along the car's lateral axis
            // (side) vs forward axis (nose)?
            float lateral     = std::abs(relDir.Dot(player.rotMat.right));
            float forwardComp = std::abs(relDir.Dot(player.rotMat.forward));

            // Corner: meaningful lateral AND forward component.
            float cornerScore = RS_CLAMP(lateral, 0.0f, 1.0f) *
                                RS_CLAMP(forwardComp, 0.0f, 1.0f);
            // Nose: mostly forward, little lateral.
            float noseScore   = RS_CLAMP(forwardComp - lateral, 0.0f, 1.0f);

            float contactQuality = RS_MIN(
                CORNER_WEIGHT * cornerScore + NOSE_WEIGHT * noseScore, 1.0f);

            return contactQuality * centerQuality;  // [0, 1]
        }
    };

    // =========================================================================
    // 3. DODGE TIMING AT CONTACT REWARD
    //
    // A flip THROUGH the ball at the moment of contact adds extra power.
    // A flip whose torque phase ended BEFORE contact is a whiff — penalised.
    // Neutral (0) when no dodge is involved so it doesn't pollute non-flip hits.
    // =========================================================================
    class PowerShotDodgeTimingReward : public Reward {
    private:
        static constexpr float GOOD_TIMING_REWARD = 1.0f;
        static constexpr float WHIFF_PENALTY      = -0.3f;

    public:
        virtual void Reset(const GameState& initialState) override {}

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!player.ballTouchedStep) return 0.0f;
            if (!player.prev) return 0.0f;

            bool flippingNow  = player.isFlipping;
            bool flippingPrev = player.prev->isFlipping;

            if (flippingNow) {
                // Flip is actively torquing through the touch → good timing.
                return GOOD_TIMING_REWARD;
            }
            if (flippingPrev && !flippingNow) {
                // Flip ended the frame before contact → dodge whiffed early.
                return WHIFF_PENALTY;
            }
            // No dodge involved this touch → neutral.
            return 0.0f;
        }
    };

    // =========================================================================
    // 4. SHOT CONTEXT GATE
    //
    // Wrapper that restricts an inner Reward* to one specific shot context so
    // each context can carry a different weight in RewardConfig.cpp.
    //
    //   GROUND        — car on ground, ball z ≤ 260 UU
    //   HALF_VOLLEY   — car airborne, ball z ≤ 260 UU AND ball.vel.z > 0
    //                   (ball actively rising — classic bouncing half-volley)
    //   VOLLEY        — car airborne, ball z > 260 UU
    //   AERIAL_NODODGE — car airborne, no flip/jump available, ball z > 260 UU
    //
    // Takes ownership of innerReward and deletes it in the destructor.
    // =========================================================================
    class PowerShotContextGate : public Reward {
    public:
        enum Context { GROUND, VOLLEY, HALF_VOLLEY, AERIAL_NODODGE };

    private:
        Reward*  inner;
        Context  ctx;

        // Ball height thresholds (z, ball center).
        static constexpr float LOW_BALL_MAX_Z = 260.0f;   // ground / half-volley band
        static constexpr float HIGH_BALL_MIN_Z = 260.0f;  // volley / aerial band

    public:
        PowerShotContextGate(Reward* innerReward, Context context)
            : inner(innerReward), ctx(context) {}

        virtual ~PowerShotContextGate() { delete inner; }

        virtual void Reset(const GameState& initialState) override {
            if (inner) inner->Reset(initialState);
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!player.ballTouchedStep) return 0.0f;

            float ballZ          = state.ball.pos.z;
            float ballVelZ       = state.ball.vel.z;
            bool  onGround       = player.isOnGround;
            bool  dodgeAvailable = player.HasFlipOrJump();

            bool match = false;
            switch (ctx) {
                case GROUND:
                    match = onGround && ballZ <= LOW_BALL_MAX_Z;
                    break;
                case HALF_VOLLEY:
                    // Airborne hit on a low rising ball (classic half-volley).
                    match = !onGround && ballZ <= LOW_BALL_MAX_Z && ballVelZ > 0.0f;
                    break;
                case VOLLEY:
                    match = !onGround && ballZ > HIGH_BALL_MIN_Z;
                    break;
                case AERIAL_NODODGE:
                    match = !onGround && !dodgeAvailable && ballZ > HIGH_BALL_MIN_Z;
                    break;
            }

            if (!match) return 0.0f;
            return inner ? inner->GetReward(player, state, isFinal) : 0.0f;
        }
    };

} // namespace RLGC

// =============================================================================
// CONFIG NOTES (RewardConfig.cpp)
// =============================================================================
//
// Multiply ImpactAngle × MassAlignment: a hit needs BOTH correct angle AND
// correct contact geometry, preventing either signal from being farmed alone.
//
// Per-context wiring for Phase 3 (conservative weights):
//
//   // Ground shots — angle × alignment
//   WeightedReward(new ZeroSumReward(
//       new PowerShotContextGate(
//           new MathRewardWrapper({ new PowerShotImpactAngleReward(),
//                                   new PowerShotMassAlignmentReward() },
//                                 MathOp::MULTIPLY),
//           PowerShotContextGate::GROUND), 1.0f), 3.0f),
//
//   // Half-volley (airborne, low rising ball) — angle × alignment
//   WeightedReward(new ZeroSumReward(
//       new PowerShotContextGate(
//           new MathRewardWrapper({ new PowerShotImpactAngleReward(),
//                                   new PowerShotMassAlignmentReward() },
//                                 MathOp::MULTIPLY),
//           PowerShotContextGate::HALF_VOLLEY), 1.0f), 3.0f),
//
//   // Volley (airborne, ball high) — angle × alignment
//   WeightedReward(new ZeroSumReward(
//       new PowerShotContextGate(
//           new MathRewardWrapper({ new PowerShotImpactAngleReward(),
//                                   new PowerShotMassAlignmentReward() },
//                                 MathOp::MULTIPLY),
//           PowerShotContextGate::VOLLEY), 1.0f), 4.0f),
//
//   // Aerial no-dodge (hardest; no dodge-timing term below) — angle × alignment
//   WeightedReward(new ZeroSumReward(
//       new PowerShotContextGate(
//           new MathRewardWrapper({ new PowerShotImpactAngleReward(),
//                                   new PowerShotMassAlignmentReward() },
//                                 MathOp::MULTIPLY),
//           PowerShotContextGate::AERIAL_NODODGE), 1.0f), 4.0f),
//
//   // Dodge timing (suppress for AERIAL_NODODGE — no dodge to time there)
//   WeightedReward(new ZeroSumReward(
//       new PowerShotContextGate(new PowerShotDodgeTimingReward(),
//           PowerShotContextGate::GROUND), 1.0f), 1.5f),
//   WeightedReward(new ZeroSumReward(
//       new PowerShotContextGate(new PowerShotDodgeTimingReward(),
//           PowerShotContextGate::HALF_VOLLEY), 1.0f), 1.5f),
//   WeightedReward(new ZeroSumReward(
//       new PowerShotContextGate(new PowerShotDodgeTimingReward(),
//           PowerShotContextGate::VOLLEY), 1.0f), 1.5f),
//
// Keep these well below your shot/goal terms — they shape HOW the bot strikes,
// they should not outvote the scoring signal.
// =============================================================================
