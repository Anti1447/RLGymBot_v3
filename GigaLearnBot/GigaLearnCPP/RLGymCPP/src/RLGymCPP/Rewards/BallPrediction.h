#pragma once
// ============================================================================
// BallPrediction.h
// Centralized ball trajectory prediction for use across all rewards.
//
// Rev 1.0 (2026-02-18, NJH)
//
// Usage:
//   class MyReward : public Reward {
//   private:
//       BallPred::Cache _ballCache;
//   public:
//       void PreStep(const GameState& state) override { _ballCache.Update(state); }
//       float GetReward(const Player& player, const GameState& state, bool isFinal) override {
//           // Slice at ~1 second out
//           const BallPred::BallSlice& s = _ballCache.AtTime(1.0f);
//           Vec futureBallPos = s.pos;
//           bool willHitWall  = s.hitWall;
//
//           // Or iterate all slices:
//           for (int i = 0; i < BallPred::NUM_SLICES; i++) {
//               const auto& slice = _ballCache.slices[i];
//               // slice.t        — seconds from now
//               // slice.pos      — predicted ball position
//               // slice.vel      — predicted ball velocity
//               // slice.hitGround / hitWall / hitCeiling / hitBackboard
//           }
//       }
//   };
//
// Simulation parameters:
//   - 15 Hz sample rate → one slice every 8 physics ticks (~66.7ms)
//   - 4-second horizon  → 60 slices (indices 0-59)
//   - Physics: gravity, drag, ground/ceiling/wall/backboard bounces
//   - Bounce coefficient: 0.6 (matches Rocket League ball physics)
//   - Drag coefficient: 0.03 per second
// ============================================================================

#include "../CommonValues.h"

namespace RLGC {
namespace BallPred {

    // -------------------------------------------------------------------------
    // Simulation constants
    // -------------------------------------------------------------------------
    constexpr int   SAMPLE_HZ        = 15;                        // output slices per second
    constexpr int   PHYSICS_HZ       = 120;                       // integration rate
    constexpr int   TICKS_PER_SLICE  = PHYSICS_HZ / SAMPLE_HZ;   // 8 ticks per slice
    constexpr float DT               = 1.0f / PHYSICS_HZ;
    constexpr float SLICE_DT         = 1.0f / SAMPLE_HZ;         // ~0.0667s per slice
    constexpr float DURATION_SEC     = 4.0f;
    constexpr int   NUM_SLICES       = (int)(DURATION_SEC * SAMPLE_HZ);  // 60 slices

    // Physics coefficients
    constexpr float DRAG_COEF        = 0.03f;   // fractional speed loss per second
    constexpr float BOUNCE_COEF      = 0.6f;    // velocity retention on surface bounce

    // -------------------------------------------------------------------------
    // BallSlice — predicted ball state at one point in time
    //
    // Surface flags are STICKY: once set true for a slice, all later slices
    // carry that flag forward too. This lets callers ask "will the ball hit a
    // wall at any point in the next N seconds" by checking slice[N-1].hitWall.
    // -------------------------------------------------------------------------
    struct BallSlice {
        Vec   pos;           // ball center position
        Vec   vel;           // ball velocity
        float t;             // seconds from now
        bool  hitGround;     // ground bounce occurred at or before this slice
        bool  hitWall;       // side-wall bounce occurred at or before this slice
        bool  hitCeiling;    // ceiling bounce occurred at or before this slice
        bool  hitBackboard;  // back-wall bounce occurred at or before this slice
    };

    // -------------------------------------------------------------------------
    // Cache — per-reward prediction buffer.
    //
    // Owns a fixed-size array of BallSlice (stack-allocated, no heap).
    // Call Update() once per game step (from PreStep), then read slices freely
    // in GetReward().
    // -------------------------------------------------------------------------
    struct Cache {
        BallSlice slices[NUM_SLICES];
        bool      valid        = false;
        int       groundBounces = 0;  // total ground bounces across the 4-second window

        // Populate the slice array from the current game state.
        // Should be called exactly once per step, from PreStep().
        void Update(const GameState& state) {
            const float r = CommonValues::BALL_RADIUS;

            Vec pos = state.ball.pos;
            Vec vel = state.ball.vel;

            bool sGround    = false;
            bool sWall      = false;
            bool sCeiling   = false;
            bool sBackboard = false;
            groundBounces   = 0;

            for (int slice = 0; slice < NUM_SLICES; slice++) {
                // Integrate TICKS_PER_SLICE physics ticks before recording this slice
                for (int tick = 0; tick < TICKS_PER_SLICE; tick++) {
                    // Gravity
                    vel.z += CommonValues::BALL_GRAVITY * DT;

                    // Drag
                    vel = vel * (1.0f - DRAG_COEF * DT);

                    // Integrate position
                    pos = pos + vel * DT;

                    // Ground bounce
                    if (pos.z < r) {
                        pos.z = r;
                        vel.z = -vel.z * BOUNCE_COEF;
                        sGround = true;
                        groundBounces++;
                    }

                    // Ceiling bounce
                    if (pos.z > CommonValues::CEILING_Z - r) {
                        pos.z = CommonValues::CEILING_Z - r;
                        vel.z = -vel.z * BOUNCE_COEF;
                        sCeiling = true;
                    }

                    // Side-wall bounce (X axis)
                    if (std::abs(pos.x) > CommonValues::SIDE_WALL_X - r) {
                        pos.x = RS_CLAMP(pos.x,
                            -(CommonValues::SIDE_WALL_X - r),
                              (CommonValues::SIDE_WALL_X - r));
                        vel.x = -vel.x * BOUNCE_COEF;
                        sWall = true;
                    }

                    // Back-wall / backboard bounce (Y axis)
                    if (std::abs(pos.y) > CommonValues::BACK_WALL_Y - r) {
                        pos.y = RS_CLAMP(pos.y,
                            -(CommonValues::BACK_WALL_Y - r),
                              (CommonValues::BACK_WALL_Y - r));
                        vel.y = -vel.y * BOUNCE_COEF;
                        sBackboard = true;
                    }
                }

                // Record slice (surface flags are sticky)
                slices[slice].pos         = pos;
                slices[slice].vel         = vel;
                slices[slice].t           = (slice + 1) * SLICE_DT;
                slices[slice].hitGround   = sGround;
                slices[slice].hitWall     = sWall;
                slices[slice].hitCeiling  = sCeiling;
                slices[slice].hitBackboard = sBackboard;
            }

            valid = true;
        }

        // Return the slice whose time is closest to `seconds` (clamped to bounds).
        // Example: AtTime(1.0f) → slice at index 14 (t ≈ 0.933s) or 15 (t = 1.0s).
        const BallSlice& AtTime(float seconds) const {
            int idx = (int)(seconds * SAMPLE_HZ);
            if (idx < 0)            idx = 0;
            if (idx >= NUM_SLICES)  idx = NUM_SLICES - 1;
            return slices[idx];
        }

        // Returns true if ANY surface bounce (wall, ceiling, or backboard) occurs
        // anywhere in the full 4-second prediction window.
        bool HitAnySurface() const {
            const BallSlice& last = slices[NUM_SLICES - 1];
            return last.hitWall || last.hitCeiling || last.hitBackboard;
        }

        // Returns true if a ground bounce occurs within the given time window.
        bool HitGroundBy(float seconds) const {
            const BallSlice& s = AtTime(seconds);
            return s.hitGround;
        }

        // Returns true if a wall/ceiling/backboard bounce occurs within the given
        // time window.
        bool HitSurfaceBy(float seconds) const {
            const BallSlice& s = AtTime(seconds);
            return s.hitWall || s.hitCeiling || s.hitBackboard;
        }
    };

} // namespace BallPred
} // namespace RLGC
