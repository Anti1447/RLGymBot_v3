#pragma once
// ============================================================================
// FlipResetApproachReward.h
//
// Rev 1.0 - 2026-03-31 (NJH)
//
// PURPOSE:
//   Dense shaping reward that teaches the bot to approach the ball correctly
//   for flip reset acquisition. Designed to be used *before* or *alongside*
//   FlipResetReward — this handles the approach phase, FlipResetReward handles
//   the acquisition and productive use phase.
//
// DESIGN PHILOSOPHY:
//   Four gate conditions must ALL be true before any reward fires:
//     1. Car is airborne (not on ground)
//     2. Car is not on a wall or ceiling (free-flight aerial only)
//     3. Car is within approach range of the ball (not too far, not touching)
//     4. Car is moving toward the ball (not orbiting or retreating)
//
//   When all gates pass, two reward components are active:
//     A. Velocity component  — small reward proportional to approach speed
//     B. Alignment component — main reward for pointing wheels at the ball
//        (-rotMat.up is the car underside/wheels normal vector)
//     C. Combined bonus      — fires when BOTH A and B are well-satisfied
//
//   This layered structure gives the bot a clear gradient:
//     "Get airborne + near ball → approach it → point your wheels at it"
//
// FARMING ANALYSIS:
//   - Gate 1+2: Eliminates ground/wall play entirely
//   - Gate 3 (distance ceiling): Bot can't sit far away and farm
//   - Gate 3 (distance floor): Prevents farming by pressing into the ball
//   - Gate 4 (velocity toward ball): Must be actively approaching;
//     orbiting at constant distance yields near-zero velToBall
//   - Alignment reward scales linearly 0→1 with dot product, so the bot
//     must continuously maintain correct orientation to receive reward
//
// ANTI-FARMING NOTE:
//   This IS a per-frame reward (like VelocityBallToGoalReward). Keep its
//   weight modest (5–15) relative to acquisition event rewards. The goal is
//   a smooth gradient into the correct approach posture, not a dominant signal.
//
// ============================================================================

#include "Reward.h"
#include "CommonValues.h"
#include <cmath>
#include <algorithm>

namespace RLGC {

class FlipResetApproachReward : public Reward {
public:

    // =========================================================================
    // CONFIG
    // =========================================================================
    struct Config {
        // --- Gate thresholds ---

        // Maximum distance from car to ball to be considered "in approach range".
        // 900 UU ≈ 4.5x car lengths. Close enough to matter, far enough to
        // give the bot room to reorient before contact.
        float maxApproachDist = 900.0f;

        // Minimum distance from car to ball. Below this the car is essentially
        // on the ball — we don't want to reward that, FlipResetReward handles it.
        // A ball radius is ~93 UU; car half-height ~26 UU; combined ~120 UU.
        // 180 UU gives a small buffer above contact.
        float minApproachDist = 180.0f;

        // Minimum car height off the ground. Prevents rewarding low-altitude
        // "almost on ground" cases that aren't real aerial approaches.
        float minCarHeight = 250.0f;

        // Distance from any wall/ceiling surface to consider the car "on wall".
        // Car width/length ~80-117 UU; 220 UU margin safely catches wall-riding.
        float wallMargin = 220.0f;

        // Minimum velocity component toward the ball (UU/s) for the velocity
        // gate to pass. 50 UU/s is a gentle crawl — low enough to allow
        // soft controlled approaches, high enough to reject backwards drift.
        float minApproachSpeed = 50.0f;

        // --- Reward magnitudes (before RewardConfig outer weight is applied) ---

        // Per-frame reward for the velocity component, at full approach speed.
        // Scales linearly from 0 at minApproachSpeed to this value at
        // approachSpeedScale UU/s.
        float velocityReward = 0.05f;

        // UU/s at which velocityReward reaches its maximum.
        // ~500 UU/s ≈ fast controlled approach without boost.
        float approachSpeedScale = 500.0f;

        // Per-frame reward for the alignment component. This is the primary
        // learning signal — scales 0→alignmentReward as the wheels normal
        // swings from perpendicular (0.0 dot) to pointing directly at ball (1.0).
        float alignmentReward = 0.15f;

        // Minimum alignment dot product for the combined bonus to fire.
        // 0.7 ≈ wheels pointing within ~45° of the ball — good enough orientation.
        float combinedBonusThreshold = 0.7f;

        // Bonus added when BOTH velocity gate is clearly satisfied AND alignment
        // is above threshold. Rewards committing to the correct full approach posture.
        float combinedBonus = 0.05f;
    };

    Config config;

    FlipResetApproachReward() : config() {}
    explicit FlipResetApproachReward(const Config& cfg) : config(cfg) {}

    virtual void Reset(const GameState& initialState) override {
        // Stateless reward — no per-player tracking needed
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {

        // =====================================================================
        // GATE 1: Car must be airborne
        // =====================================================================
        if (player.isOnGround) {
            return 0.0f;
        }

        // =====================================================================
        // GATE 2: Car must not be on a wall or ceiling
        //
        // We use position-based detection against CommonValues field boundaries.
        // The car is "on wall" if its center is within wallMargin of any surface.
        //
        // Walls:  |x| > SIDE_WALL_X - wallMargin
        //          |y| > BACK_WALL_Y - wallMargin  (back walls, excl. goal mouth)
        // Ceiling: z > CEILING_Z - wallMargin
        //
        // Corner check: RocketSim's corner planes run diagonally. The standard
        // approximation is |x| + |y| > CORNER_AXIS_INTERCEPT - wallMargin.
        // =====================================================================
        {
            float ax = fabsf(player.pos.x);
            float ay = fabsf(player.pos.y);
            float az = player.pos.z;

            bool nearSideWall  = ax > (CommonValues::SIDE_WALL_X - config.wallMargin);
            bool nearBackWall  = ay > (CommonValues::BACK_WALL_Y - config.wallMargin);
            bool nearCeiling   = az > (CommonValues::CEILING_Z   - config.wallMargin);
            bool nearCorner    = (ax + ay) > (CommonValues::CORNER_AXIS_INTERCEPT - config.wallMargin);

            if (nearSideWall || nearBackWall || nearCeiling || nearCorner) {
                return 0.0f;
            }
        }

        // =====================================================================
        // GATE 3: Car height and distance to ball
        // =====================================================================
        if (player.pos.z < config.minCarHeight) {
            return 0.0f;
        }

        Vec toBall = state.ball.pos - player.pos;
        float distToBall = toBall.Length();

        if (distToBall > config.maxApproachDist || distToBall < config.minApproachDist) {
            return 0.0f;
        }

        // Safe to normalize now — distToBall is bounded away from 0
        Vec toBallNorm = toBall * (1.0f / distToBall);

        // =====================================================================
        // GATE 4: Car velocity must have a positive component toward the ball
        //
        // velToBall is the scalar projection of car velocity onto the toBall
        // direction. Positive = approaching. We gate on > minApproachSpeed so
        // that near-stationary or retreating cars don't get rewarded.
        // =====================================================================
        float velToBall = player.vel.Dot(toBallNorm);

        if (velToBall < config.minApproachSpeed) {
            return 0.0f;
        }

        // =====================================================================
        // All gates passed — compute reward components
        // =====================================================================

        // --- Component A: Velocity reward ---
        // Linear scale from minApproachSpeed to approachSpeedScale.
        // At minApproachSpeed the value is 0; at approachSpeedScale it is 1.
        float velFactor = RS_CLAMP(
            (velToBall - config.minApproachSpeed) / (config.approachSpeedScale - config.minApproachSpeed),
            0.0f, 1.0f
        );
        float velReward = config.velocityReward * velFactor;

        // --- Component B: Alignment reward ---
        //
        // The car's underside (wheels) normal is the NEGATIVE of rotMat.up.
        // rotMat.up points from the car's floor toward the roof.
        // -rotMat.up points from the roof toward the wheels, i.e. downward
        // when the car is upright, and UPWARD when the car is inverted.
        //
        // For a flip reset, the car approaches the ball from below while
        // inverted, so -rotMat.up should point from the car toward the ball.
        //
        // dot(-rotMat.up, toBallNorm):
        //   +1.0 = wheels pointing directly at ball (perfect inversion)
        //    0.0 = wheels perpendicular to ball direction
        //   -1.0 = roof pointing at ball (completely wrong orientation)
        //
        // We only reward positive alignment (wheels toward ball).
        Vec negUp = Vec(-player.rotMat.up.x, -player.rotMat.up.y, -player.rotMat.up.z);
        float alignmentDot = negUp.Dot(toBallNorm);

        float alignmentReward = 0.0f;
        if (alignmentDot > 0.0f) {
            alignmentReward = config.alignmentReward * alignmentDot;
        }

        // --- Component C: Combined bonus ---
        // Fires only when the bot is both approaching and well-oriented.
        // This is the sweet spot we most want to reinforce.
        float combinedBonus = 0.0f;
        if (velToBall > config.minApproachSpeed && alignmentDot > config.combinedBonusThreshold) {
            combinedBonus = config.combinedBonus;
        }

        return velReward + alignmentReward + combinedBonus;
    }
};

} // namespace RLGC
