// CompleteMechanicsRewards.h
// Complete reward set for all mechanics from Bronze to Grand Champion + Advanced Mechanics
// Place in: GigaLearnCPP/GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/

#pragma once
#include "Reward.h"
#include "../Math.h"
#include <map>
#include <unordered_map>
#include <deque>
#include <vector>
#include <atomic>
#include "RewardMath.h"
#include "AirDribbleAccuracyReward.h"
#include "BallPrediction.h"
#include "PositionAnalysis.h"

namespace RLGC {
    // Helpers
    // Helper: directional multiplier for dribble-like rewards
    // Returns:
    //   1.0  -> ball moving toward opponent goal
    //   0.75 -> ball moving toward opponent half (but not clearly at goal)
    //   0.5  -> neither condition
    static inline float ComputeDribbleDirectionMultiplier(const Player& player, const GameState& state) {
        // Defensive guards
        if (!state.prev && state.ball.vel.Length() < 1e-3f) {
            // no reliable velocity; fall back to player facing
        }

        // Opponent goal and vector from ball -> opponent goal
        Vec opponentGoal = (player.team == Team::BLUE) ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        Vec toOpp = opponentGoal - state.ball.pos;
        float toOppLen = toOpp.Length();

        float ballSpeed = state.ball.vel.Length();

        float towardOpp = 0.0f;
        if (ballSpeed > 1e-3f && toOppLen > 1e-5f) {
            towardOpp = state.ball.vel.Normalized().Dot(toOpp / toOppLen);
        }
        else if (toOppLen > 1e-5f) {
            towardOpp = player.rotMat.forward.Dot(toOpp / toOppLen); // fallback
        }

        if (towardOpp > 0.1f) {
            return 1.0f; // clearly moving toward opponent goal
        }

        // Check "toward opponent half" direction
        // For BLUE team opponent half is +Y, for ORANGE it's -Y
        Vec halfDir = Vec(0.0f, (player.team == Team::BLUE) ? 1.0f : -1.0f, 0.0f);

        float towardHalf = 0.0f;
        if (ballSpeed > 1e-3f) {
            towardHalf = state.ball.vel.Normalized().Dot(halfDir);
        }
        else {
            towardHalf = player.rotMat.forward.Dot(halfDir); // fallback to player facing
        }

        if (towardHalf > 0.1f) {
            return 0.75f; // moving toward opponent half but not specifically the goal
        }

        return 0.5f; // neither condition
    }

// ============================================================================
// BRONZE TIER REWARDS
// ============================================================================

// Basic turning radius - smoother turns
class TurningRadiusReward : public Reward {
private:
    mutable std::map<uint32_t, Vec> lastForward;

public:
    virtual void Reset(const GameState& initialState) override {
        lastForward.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.isOnGround) return 0.0f;
        
        Vec curForward = player.rotMat.forward;
        auto it = lastForward.find(player.carId);
        
        if (it == lastForward.end()) {
            lastForward[player.carId] = curForward;
            return 0.0f;
        }
        
        // Reward smooth turning (not jerky movements)
        float turnAngle = acos(RS_CLAMP(curForward.Dot(it->second), -1.0f, 1.0f));
        lastForward[player.carId] = curForward;
        
        // Small reward for controlled turning
        if (turnAngle > 0.01f && turnAngle < 0.3f) {
            return 0.5f;
        }
        return 0.0f;
    }
};

// Rev 2.2 (2026-03-04, NJH)
// Landing on at least 2 wheels after aerial on any surface (ground, wall, ceiling).
//
// Improvements over Rev 1.0:
//  - Minimum airtime gate (0.2s) — micro-hops are ignored.
//  - Airtime scaling: payout scales 1.0x→2.0x from 0.2s to 3.0s+ airtime.
//  - Wall nose-down bonus: +0.3 when far from ball (>1000 UU) and nose is
//    pointing downward (rotMat.forward.z < -0.5) — ideal recovery posture.
//  - PreStep tracks landing events cleanly; GetReward is read-only.
//
// Rev 2.1 changes (ceiling):
//  - Ceiling wheel-check gate: <3 wheels on ceiling → -1.0 * scale penalty.
//    Catches roof/hood smashes and glancing ceiling contacts.
//  - Raised ceiling rewards: perfect alignment pays 3.0 * scale.
//  - Ceiling misalignment always penalizes: up to -1.5 * scale.
//
// Rev 2.2 changes (code review pass):
//  - Zero-normal fallback now uses car position to estimate surface type
//    (ceiling / wall / ground) before choosing the fallback formula. Previously
//    it always used carUp.z, giving 0 reward to proper ceiling landings when
//    contactNormal was zero on the landing frame.
//  - Ceiling reward is now a single continuous curve across [-1,+1] alignment.
//    Previously had a hard jump from 0→-0.5 at alignment=0.5 (0.56/step
//    discontinuity). Now: alignment≥0 → 0→+3.0, alignment<0 → 0→-1.5.
//  - Wall misalignment penalty is now a smooth curve: 0 at alignment=0.3,
//    -0.5 at alignment=-1.0. Previously a flat -0.3 for all alignment≤0.3.
//  - Ground misaligned landings now penalized: smooth 0→-0.3 curve for
//    alignment < 0.3 (consistent with wall behavior; previously returned 0).
class ProperLandingReward : public Reward {
private:
    static constexpr float MIN_AIR_TIME       = 0.2f;   // ignore hops shorter than 200ms
    static constexpr float AIR_TIME_SCALE_MAX = 10.0f;   // cap scaling at 10s airtime

    std::unordered_map<uint32_t, bool>  _wasAirborne;
    std::unordered_map<uint32_t, bool>  _hasLanding;
    std::unordered_map<uint32_t, float> _airTimeAtLanding;

public:
    virtual void Reset(const GameState& initialState) override {
        _wasAirborne.clear();
        _hasLanding.clear();
        _airTimeAtLanding.clear();
    }

    virtual void PreStep(const GameState& state) override {
        for (const auto& player : state.players) {
            uint32_t id = player.carId;
            bool nowAir = !player.isOnGround;

            if (_wasAirborne[id] && !nowAir) {
                // Just landed — snapshot the airtime from the previous frame
                _airTimeAtLanding[id] = (player.prev != nullptr)
                    ? player.prev->airTime
                    : player.airTime;
                _hasLanding[id] = true;
            } else {
                _hasLanding[id] = false;
            }

            _wasAirborne[id] = nowAir;
        }
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!_hasLanding[player.carId])
            return 0.0f;

        float airTime = _airTimeAtLanding[player.carId];
        if (airTime < MIN_AIR_TIME)
            return 0.0f; // trivial hop — ignore

        // Require 2+ wheels for a proper landing (isOnGround can be true with only 2 wheels
        // on glancing/diagonal landings — matching on 3 discards real landings)
        int wheels = 0;
        for (int i = 0; i < 4; i++)
            if (player.wheelsWithContact[i]) wheels++;
        if (wheels < 2)
            return 0.0f;
        // Note: worldContact.hasContact gate removed — on the exact landing frame in
        // RocketSim's 8-tick step, worldContact may not be populated yet even though
        // isOnGround flipped to true. The hasContact gate silently dropped valid landings.

        // Airtime scale: 1.0x at MIN_AIR_TIME, up to 2.0x at AIR_TIME_SCALE_MAX
        float airtimeScale = 1.0f + RS_CLAMP(
            (airTime - MIN_AIR_TIME) / (AIR_TIME_SCALE_MAX - MIN_AIR_TIME),
            0.0f, 1.0f
        );

        Vec  surfaceNormal  = player.worldContact.contactNormal;
        Vec  carUp          = player.rotMat.up;
        float alignment     = carUp.Dot(surfaceNormal);

        // Zero-normal guard: on the exact landing frame in RocketSim's 8-tick sub-step,
        // contactNormal may not be populated yet even though isOnGround flipped to true.
        // Use car position to estimate the likely surface instead of blindly assuming ground.
        if (surfaceNormal.Length() < 0.1f) {
            const float dx    = CommonValues::SIDE_WALL_X - std::abs(player.pos.x);
            const float dy    = CommonValues::BACK_WALL_Y - std::abs(player.pos.y);
            const float dCeil = CommonValues::CEILING_Z   - player.pos.z;
            const float near  = 150.0f; // UU — close enough to be on that surface

            bool nearCeiling = (dCeil < near);
            bool nearWall    = !nearCeiling && (dx < near || dy < near);

            if (nearCeiling) {
                // Ceiling fallback: carUp.z ≈ -1 for proper inversion
                float alignScore = RS_CLAMP((-carUp.z - 0.3f) / 0.7f, 0.0f, 1.0f);
                return alignScore * 1.5f * airtimeScale;
            } else if (nearWall) {
                // Wall fallback: horizontal carUp magnitude ≈ 1.0 for a proper wall landing
                float hLen       = std::sqrt(carUp.x*carUp.x + carUp.y*carUp.y);
                float alignScore = RS_CLAMP((hLen - 0.3f) / 0.7f, 0.0f, 1.0f);
                return alignScore * 0.8f * airtimeScale;
            } else {
                // Ground fallback: carUp.z ≈ +1 for a right-side-up landing
                float alignScore = RS_CLAMP((carUp.z - 0.3f) / 0.7f, 0.0f, 1.0f);
                return alignScore * 1.0f * airtimeScale;
            }
        }

        // Surface type detection via contact normal
        bool isCeilingContact = surfaceNormal.z < -0.5f;
        bool isWallContact    = !isCeilingContact && (std::abs(surfaceNormal.z) < 0.5f);

        if (isCeilingContact) {
            // ---------------------------------------------------------------
            // CEILING CONTACT — car must be upside-down (wheels on ceiling)
            // ---------------------------------------------------------------
            // Ceiling contactNormal ≈ {0,0,-1} (points INTO arena, downward).
            // Properly inverted car: rotMat.up.z ≈ -1 (wheels face ceiling, roof faces floor).
            // alignment = carUp.Dot(ceilingNormal) = (-1)*(-1) = +1.0 for perfect inversion.
            // So alignment > 0 = good (inverted), alignment < 0 = bad (right-side-up smash).
            //
            // Wheel check first: if 3+ wheels are NOT on the ceiling (wheelsWithContact),
            // this is not a proper ceiling ride — penalize it.
            if (wheels < 3) {
                // Car is touching ceiling but wheels aren't planted.
                // This is a roof/hood smash or a glancing angle — penalize.
                return -1.0f * airtimeScale;
            }

            // Continuous curve across full [-1, +1] alignment range (no hard step).
            // Positive alignment = good inversion → up to +3.0 reward.
            // Negative alignment = right-side-up smash → up to -1.5 penalty.
            if (alignment >= 0.0f) {
                float alignScore = RS_CLAMP(alignment, 0.0f, 1.0f);
                return alignScore * 3.0f * airtimeScale;
            } else {
                float penaltyScore = RS_CLAMP(-alignment, 0.0f, 1.0f);
                return -penaltyScore * 1.5f * airtimeScale;
            }

        } else if (isWallContact) {
            // ---------------------------------------------------------------
            // WALL CONTACT — wheels should face away from the wall surface
            // ---------------------------------------------------------------
            if (alignment <= 0.3f) {
                // Smooth penalty: 0 at alignment=0.3, -0.5 at alignment=-1.0
                float penaltyScore = RS_CLAMP((0.3f - alignment) / 1.3f, 0.0f, 1.0f);
                return -penaltyScore * 0.5f * airtimeScale;
            }
            // Smooth curve: 0 at alignment=0.3, 1.0 at alignment=1.0
            float baseReward = RS_CLAMP((alignment - 0.3f) / 0.7f, 0.0f, 1.0f);
            return baseReward * airtimeScale;

        } else {
            // ---------------------------------------------------------------
            // GROUND CONTACT
            // ---------------------------------------------------------------
            // Smooth curve: 0 at alignment=0.3, 1.0 at alignment=1.0
            float alignScore = RS_CLAMP((alignment - 0.3f) / 0.7f, 0.0f, 1.0f);
            if (alignScore > 0.0f)
                return alignScore * 1.0f * airtimeScale;
            // Smooth penalty for sideways/inverted ground landings: 0 at 0.3, -0.3 at -1.0
            float penaltyScore = RS_CLAMP((0.3f - alignment) / 1.3f, 0.0f, 1.0f);
            return -penaltyScore * 0.3f * airtimeScale;
        }

        return 0.0f;
    }
};

// ============================================================================
// SILVER TIER REWARDS
// ============================================================================

// Half-flip execution
// 2025-12-07 - Rev 2 - Require starting backwards and ending forwards,
//                      not just 180° orientation. (NJH)
class HalfFlipReward : public Reward {
private:
    mutable std::map<uint32_t, Vec>  flipStartForward;
    mutable std::map<uint32_t, Vec>  flipStartVel;
    mutable std::map<uint32_t, bool> isFlipping;
    mutable std::map<uint32_t, bool> startedFromBackwards;

public:
    virtual void Reset(const GameState& initialState) override {
        flipStartForward.clear();
        flipStartVel.clear();
        isFlipping.clear();
        startedFromBackwards.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        bool wasFlipping = isFlipping[player.carId];
        isFlipping[player.carId] = player.isFlipping;

        // Flip just started
        if (!wasFlipping && player.isFlipping) {
            flipStartForward[player.carId] = player.rotMat.forward;
            flipStartVel[player.carId] = player.vel;

            bool backwardsMotion = false;

            // Check motion relative to facing (moving backwards)
            float startSpeed = player.vel.Length();
            if (startSpeed > 300.0f) {
                Vec velDir = player.vel / startSpeed;
                float velAlign = velDir.Dot(player.rotMat.forward);
                if (velAlign < -0.5f) {
                    backwardsMotion = true;
                }
            }

            // Also accept explicit backflip input (stick pitched back)
            if (player.prevAction.pitch < -0.5f) {
                backwardsMotion = true;
            }

            startedFromBackwards[player.carId] = backwardsMotion;
            return 0.0f;
        }

        // Flip just ended
        if (wasFlipping && !player.isFlipping) {
            auto itFwd = flipStartForward.find(player.carId);
            auto itVel = flipStartVel.find(player.carId);
            if (itFwd == flipStartForward.end() || itVel == flipStartVel.end())
                return 0.0f;

            Vec startForward = itFwd->second;
            Vec endForward = player.rotMat.forward;

            // Orientation should be roughly 180° apart
            float forwardAlignment = startForward.Dot(endForward);
            if (forwardAlignment > -0.7f)
                return 0.0f;

            // Must have actually started from a backwards state
            if (!startedFromBackwards[player.carId])
                return 0.0f;

            // End state: moving forwards relative to facing
            float endSpeed = player.vel.Length();
            if (endSpeed < 300.0f)
                return 0.0f;

            Vec endVelDir = player.vel / endSpeed;
            float endVelAlign = endVelDir.Dot(endForward);
            if (endVelAlign < 0.5f)
                return 0.0f;

            // Successful half-flip (backwards → forwards with 180° rotation)
            return 10.0f;
        }

        return 0.0f;
    }
};


// Power slide usage for better control
// 2026-05-21 - Rev 3 - Event-on-release, brevity bonus, yaw gate, one-shot penalty. (NJH)
class PowerSlideControlReward : public Reward {
private:
    mutable std::unordered_map<uint32_t, float> slideTime;
    mutable std::unordered_map<uint32_t, bool>  wasSliding;
    mutable std::unordered_map<uint32_t, float> slideYawAccum;
    mutable std::unordered_map<uint32_t, float> slideSpeedMax;

    float maxTapDuration = 0.25f;

public:
    virtual void Reset(const GameState& initialState) override {
        slideTime.clear();
        wasSliding.clear();
        slideYawAccum.clear();
        slideSpeedMax.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
        uint32_t id = player.carId;

        bool currentlySliding = player.isOnGround && player.prevAction.handbrake > 0.5f;
        bool prevSliding = wasSliding[id]; // lazy-inits false
        wasSliding[id] = currentlySliding;

        if (!currentlySliding && !prevSliding)
            return 0.0f;

        if (currentlySliding && !prevSliding) {
            // Slide just started
            slideTime[id]     = 0.0f;
            slideYawAccum[id] = 0.0f;
            slideSpeedMax[id] = player.vel.Length();
            return 0.0f;
        }

        if (currentlySliding) {
            // Mid-slide: accumulate
            slideTime[id]     += dt;
            float spd = player.vel.Length();
            if (spd > slideSpeedMax[id]) slideSpeedMax[id] = spd;
            slideYawAccum[id] += std::abs(player.angVel.z) * dt;
            return 0.0f;
        }

        // Handbrake just released — fire single event
        float t        = slideTime[id];
        float maxSpeed = slideSpeedMax[id];
        float yawAcc   = slideYawAccum[id];
        slideTime[id]     = 0.0f;
        slideYawAccum[id] = 0.0f;

        if (maxSpeed < 300.0f)
            return 0.0f;

        // Yaw gate: require actual turning (anti-farming)
        if (yawAcc < 0.1f)
            return 0.0f;

        if (t <= maxTapDuration) {
            float speedFrac = RS_MIN(1.0f, maxSpeed / 1500.0f);
            float brevity   = 1.0f - (t / maxTapDuration); // shorter tap → higher bonus
            float yawFrac   = RS_MIN(1.0f, yawAcc / 0.5f);
            return 0.5f * (0.5f + 0.5f * brevity) * speedFrac * yawFrac;
        }

        // Long hold: one-shot penalty on release, capped at 3x
        float over = t - maxTapDuration;
        float penaltyScale = RS_MIN(3.0f, over / maxTapDuration);
        return -0.5f * penaltyScale;
    }
};


// ============================================================================
// GOLD TIER REWARDS
// ============================================================================

// Wall hits - touching ball near walls
class WallHitReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;

        // Check if ball is near wall
        float distToWallX = RS_MIN(
            abs(state.ball.pos.x - CommonValues::SIDE_WALL_X),
            abs(state.ball.pos.x + CommonValues::SIDE_WALL_X)
        );
        float distToWallY = RS_MIN(
            abs(state.ball.pos.y - CommonValues::BACK_WALL_Y),
            abs(state.ball.pos.y + CommonValues::BACK_WALL_Y)
        );
        
        float minWallDist = RS_MIN(distToWallX, distToWallY);
        
        if (minWallDist < 500.0f) {
            // Ball is near wall, reward touch
            float proximity = 1.0f - (minWallDist / 500.0f);
            return proximity * 2.0f;
        }
        
        return 0.0f;
    }
};

// ============================================================================
// WallPopReward
// 2025-12-19 | Rev 1.0 | NJH
// - Rewards popping the ball upward after a wall touch
// - Requires: recent wall contact + ball touch + positive Z ball velocity
// ============================================================================
class WallPopReward : public Reward {
private:
    // Wall proximity
    float wallX = CommonValues::SIDE_WALL_X;
    float wallTolerance = 180.0f;

    // Timing
    float recentWallWindow = 0.25f; // seconds

    // Ball pop requirement
    float minBallZVel = 200.0f; // uu/s upward

    // State
    mutable std::map<uint32_t, float> timeSinceOnWall;

public:
    virtual void Reset(const GameState& initialState) override {
        timeSinceOnWall.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        const float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
        const uint32_t id = player.carId;

        // Detect wall contact (not ground)
        const bool nearSideWall =
            fabs(player.pos.x) > (wallX - wallTolerance);

        const bool onWall =
            nearSideWall && !player.isOnGround;

        auto& tSinceWall = timeSinceOnWall[id];

        if (onWall) {
            tSinceWall = 0.0f;
        }
        else {
            tSinceWall += dt;
        }

        // Must touch the ball shortly after wall contact
        if (!player.ballTouchedStep) return 0.0f;
        if (tSinceWall > recentWallWindow) return 0.0f;

        // Ball must be popped upward
        if (state.ball.vel.z < minBallZVel) return 0.0f;

        // Small shaping by pop strength
        float zFrac = RS_CLAMP((state.ball.vel.z - minBallZVel) / 800.0f, 0.0f, 1.0f);
        return 4.0f + 4.0f * zFrac; // 4..8
    }
};

// ============================================================================
// WallShotReward
// 2025-12-19 | Rev 1.0 | NJH
// - Rewards shots on target that originate from wall play
// - Requires recent wall contact + ball touch + on-goal trajectory
// ============================================================================
//class WallShotReward : public Reward {
//private:
//    float wallX = CommonValues::SIDE_WALL_X;
//    float wallTolerance = 180.0f;
//
//    float recentWallWindow = 0.35f; // slightly longer than pop
//
//    mutable std::map<uint32_t, float> timeSinceOnWall;
//
//public:
//    virtual void Reset(const GameState& initialState) override {
//        timeSinceOnWall.clear();
//    }
//
//    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
//        const float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
//        const uint32_t id = player.carId;
//
//        const bool nearSideWall =
//            fabs(player.pos.x) > (wallX - wallTolerance);
//
//        const bool onWall =
//            nearSideWall && !player.isOnGround;
//
//        auto& tSinceWall = timeSinceOnWall[id];
//
//        if (onWall) {
//            tSinceWall = 0.0f;
//        }
//        else {
//            tSinceWall += dt;
//        }
//
//        if (!player.ballTouchedStep) return 0.0f;
//        if (tSinceWall > recentWallWindow) return 0.0f;
//
//        // Must be a real shot on goal
//        if (!RLGC::RewardMath::BallIntersectsOpponentGoalMouth(
//            player,
//            state,
//            2.5f,   // tMaxSec
//            true,   // require back-net confirmation
//            true    // gravity
//        )) {
//            return 0.0f;
//        }
//
//        // Scale by shot speed
//        const float speed = state.ball.vel.Length();
//        float speedFrac = RS_CLAMP((speed - 1000.0f) / 2500.0f, 0.0f, 1.0f);
//
//        return 10.0f + 10.0f * speedFrac; // 10..20
//    }
//};



// Corner clears - hitting ball to corner from defensive position
// Rev 2.0 2026-07-20 NJH — added min ball-speed gate (matches BigClearReward's
// hardness bar) and a per-player cooldown (CLAUDE.md §7C pattern) to close a
// farming vector: soft dribble touches that merely nudged the ball toward a
// corner previously paid the full reward on every touched tick. Also switched
// the alignment check to horizontal-only (XY) — the target corner points are
// flat (z=0), so dotting against the ball's full 3D velocity previously scored
// elevated, hard clears *worse* than flat ones as height increased.
// Rev 3.0 2026-07-21 NJH — added opponent-target-awareness scaling against the
// specific corner point being aimed for, matching BigClearReward Rev 4.0.
class CornerClearReward : public Reward {
private:
    static constexpr float MIN_BALL_SPEED = 700.0f; // UU/s — matches BigClearReward's hardness gate
    static constexpr float SAFE_DIST   = 2500.0f;   // UU, opponent this far from corner = full credit
    static constexpr float DANGER_DIST = 800.0f;    // UU, opponent this close = floor credit
    static constexpr float MIN_TARGET_SAFETY = 0.2f; // never fully zero a clear

    float cooldownSeconds;
    mutable std::unordered_map<uint32_t, float> _cooldownRemaining;

public:
    CornerClearReward(float cooldownMs = 1000.0f) : cooldownSeconds(cooldownMs / 1000.0f) {}

    virtual void Reset(const GameState& initialState) override {
        _cooldownRemaining.clear();
    }

    virtual void PreStep(const GameState& state) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
        for (const auto& p : state.players) {
            float& cd = _cooldownRemaining[p.carId];
            if (cd > 0.0f) cd = RS_MAX(0.0f, cd - dt);
        }
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;

        float& cd = _cooldownRemaining[player.carId]; // lazy-init to 0.0f
        if (cd > 0.0f) return 0.0f;

        if (state.ball.vel.Length() < MIN_BALL_SPEED) return 0.0f;

        Vec myGoal = (player.team == Team::BLUE) ?
            CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

        // Check if ball was in defensive zone
        float ballDistToGoal = (state.ball.pos - myGoal).Length();
        if (ballDistToGoal > 3000.0f) return 0.0f; // Not defensive

        // Check if ball is now moving toward corners (horizontal-only — ball
        // height shouldn't distort alignment against a flat target point)
        Vec ballVelNorm = Vec(state.ball.vel.x, state.ball.vel.y, 0.0f).Normalized();

        // Target corner positions
        Vec corners[] = {
            Vec(CommonValues::SIDE_WALL_X, myGoal.y + 2000.0f, 0),
            Vec(-CommonValues::SIDE_WALL_X, myGoal.y + 2000.0f, 0)
        };

        for (auto& corner : corners) {
            Vec toCorner = Vec(corner.x - state.ball.pos.x, corner.y - state.ball.pos.y, 0.0f).Normalized();
            float alignment = ballVelNorm.Dot(toCorner);

            if (alignment > 0.7f) {
                cd = cooldownSeconds;
                float nearestOpp = PositionAnalysis::ClosestOpponentDistance(player, corner, state.players);
                float targetSafety = RS_CLAMP(
                    (nearestOpp - DANGER_DIST) / (SAFE_DIST - DANGER_DIST),
                    MIN_TARGET_SAFETY, 1.0f);
                return 5.0f * targetSafety; // Good defensive clear to corner, scaled by opponent proximity
            }
        }

        return 0.0f;
    }
};

// Air roll shot: airborne + genuine air-roll input + 20-160° roll orientation +
// underside ball contact (with hitbox tolerance) + meaningful ball redirect.
// Rev 3.0 2026-07-07 NJH — replaced the isFlipping/dodge-recency gate (which detected
// dodges, not air-roll input, and was the primary cause of ~0% activation) with
// player.prevAction.roll, matching the AirRollMultiplierGate/WallDashInputReward
// pattern. Widened roll band, added underside tolerance, loosened redirect angle.
// See Reward_Change_Log.md.
class AirRollShotReward : public Reward {
private:
    static constexpr float MIN_HEIGHT      = 300.0f;  // UU — minimum car height
    static constexpr float MIN_BALL_Z      = 150.0f;  // UU — ball must be off ground
    static constexpr float ROLL_DOT_MIN    = -0.940f; // cos(160°) — exclude near-inverted-flat
    static constexpr float ROLL_DOT_MAX    = 0.940f;  // cos(20°) — exclude near-upright
    static constexpr float ROLL_INPUT_MIN  = 0.5f;    // matches AirRollMultiplierGate::ROLL_THRESHOLD
    static constexpr float COOLDOWN_SEC       = 1.0f;    // prevent multi-tick double-count
    static constexpr float MIN_PRE_TOUCH_SPEED  = 100.0f;  // UU/s — skip dir check if ball was stationary
    static constexpr float MIN_POST_TOUCH_SPEED = 200.0f;  // UU/s — ball must move after contact
    static constexpr float REDIRECT_COS_MAX     = 0.906f;  // cos(25°) — dot > this = <25° change → reject
    static constexpr float UNDERSIDE_TOLERANCE_UU = 40.0f; // ~43% of BALL_RADIUS (92.75uu) for hitbox extents

    std::unordered_map<uint32_t, float> cooldownRemaining;

public:
    virtual void Reset(const GameState&) override {
        cooldownRemaining.clear();
    }

    virtual void PreStep(const GameState& state) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
        for (const auto& p : state.players) {
            float& cd = cooldownRemaining[p.carId];
            if (cd > 0.0f) cd = RS_MAX(0.0f, cd - dt);
        }
    }

    virtual float GetReward(const Player& player, const GameState& state, bool) override {
        if (!player.ballTouchedStep)        return 0.0f;
        if (player.isOnGround)              return 0.0f;
        if (player.pos.z < MIN_HEIGHT)      return 0.0f;
        if (state.ball.pos.z < MIN_BALL_Z)  return 0.0f;

        // Cooldown guard
        auto cdIt = cooldownRemaining.find(player.carId);
        if (cdIt != cooldownRemaining.end() && cdIt->second > 0.0f) return 0.0f;

        // Roll orientation: 20-160° from upright (works for both left and right roll)
        float rollDot = player.rotMat.up.Dot(Vec(0.0f, 0.0f, 1.0f));
        if (rollDot < ROLL_DOT_MIN || rollDot > ROLL_DOT_MAX) return 0.0f;

        // Genuine air-roll input at the moment of touch — proof of player intent,
        // distinct from the orientation check above (which only proves the result)
        if (fabsf(player.prevAction.roll) < ROLL_INPUT_MIN) return 0.0f;

        // Underside contact: car center must be below ball center (with hitbox tolerance)
        if (player.pos.z >= state.ball.pos.z + UNDERSIDE_TOLERANCE_UU) return 0.0f;

        // Ball direction must change significantly — guards against grazes and non-redirecting contact
        {
            Vec curBallVel = state.ball.vel;
            float curSpeed = curBallVel.Length();
            if (curSpeed < MIN_POST_TOUCH_SPEED) return 0.0f;

            if (state.prev) {
                Vec prevBallVel = state.prev->ball.vel;
                float prevSpeed = prevBallVel.Length();
                if (prevSpeed >= MIN_PRE_TOUCH_SPEED) {
                    float dirDot = prevBallVel.Normalized().Dot(curBallVel.Normalized());
                    if (dirDot > REDIRECT_COS_MAX) return 0.0f; // <25° direction change — not a redirect
                }
            }
        }

        cooldownRemaining[player.carId] = COOLDOWN_SEC;
        return 1.0f;
    }
};

// ============================================================================
// DemoAwarenessReward - Rev 2.0 (2026-05-30)
// ============================================================================
// Teaches the bot to avoid incoming demo and bump threats from ALL cars
// (opponents AND teammates).
//
// Three tiers:
//   1. Continuous proximity penalty when any car is close + closing fast
//   2. Bump event penalty (-2.0) — any car, any source
//   3. Demo event penalty (-5.0) — any car, any source
//
// The former demo reward (+10) is removed — DemoReward (ZeroSum) already
// covers incentivizing demos. This class is purely about avoidance.
// ============================================================================
class DemoAwarenessReward : public Reward {
private:
    static constexpr float DANGER_RANGE      = 600.0f;  // UU — threat detection radius
    static constexpr float MIN_CLOSING_SPEED = 300.0f;  // UU/s — min approach speed to trigger
    static constexpr float PROXIMITY_SCALE   = 0.15f;   // per-step penalty scale per threat
    static constexpr float MAX_PROXIMITY_PEN = 0.5f;    // cap total continuous penalty per step
    static constexpr float BUMP_PENALTY      = 2.0f;    // event penalty for being bumped
    static constexpr float DEMO_PENALTY      = 5.0f;    // event penalty for being demoed

public:
    virtual void Reset(const GameState& /*initialState*/) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool /*isFinal*/) override {
        if (player.isDemoed) return 0.0f;

        // Tier 3: demo event — highest priority, return immediately
        if (player.eventState.demoed)
            return -DEMO_PENALTY;

        // Tier 2: bump event — return immediately
        if (player.eventState.bumped)
            return -BUMP_PENALTY;

        // Tier 1: continuous proximity danger from any car (opponents AND teammates)
        float totalPenalty = 0.0f;
        for (const auto& other : state.players) {
            if (other.carId == player.carId || other.isDemoed) continue;

            Vec  toPlayer = player.pos - other.pos;
            float dist    = toPlayer.Length();
            if (dist > DANGER_RANGE || dist < 1.0f) continue;

            // How fast is the other car closing toward this player?
            Vec  toPlayerDir  = toPlayer / dist;
            float closingSpeed = other.vel.Dot(-toPlayerDir);  // positive = approaching
            if (closingSpeed < MIN_CLOSING_SPEED) continue;

            float closingFraction   = RS_CLAMP(closingSpeed / 1500.0f, 0.0f, 1.0f);
            float proximityFraction = 1.0f - (dist / DANGER_RANGE);
            totalPenalty += PROXIMITY_SCALE * proximityFraction * closingFraction;
        }

        return -RS_CLAMP(totalPenalty, 0.0f, MAX_PROXIMITY_PEN);
    }
};

// ============================================================================
// PLATINUM TIER REWARDS
// ============================================================================

// ============================================================================
// BackboardReboundReward
//  - Track last time ball was near opponent backboard
//  - If player then follows up within a short timeout and
//    hits a strong shot toward goal from near net, reward.
// 2025-12-09 - Rev 1 (NJH)
// 2026-02-23 - Rev 2 (NJH) — BallPrediction.h integration
//   Two new reward layers on top of the existing reactive touch reward:
//
//   Layer 1 — Predictive Positioning (continuous, no touch required):
//     Scans the 4-second ball trajectory for an upcoming backboard bounce.
//     If one is predicted within PRED_HORIZON (1.5s), finds the earliest
//     slice the bot can physically reach (dist <= CAR_MAX_SPEED * timeAvail)
//     and gives an exponential proximity reward toward that intercept point:
//     PRED_PROX_REWARD * exp(-dist / PRED_DIST_SCALE) * dt.
//     Only fires when the intercept point is in the attacking half. Teaches
//     the bot to anticipate and chase backboard rebounds proactively.
//
//   Layer 2 — Trajectory Quality Multiplier (on touch reward only):
//     After all existing gates pass, checks where the ball will be 0.75s
//     from now. If it's heading toward goal → up to 1.5x multiplier.
//     If drifting away → down to 0.5x. Validates shot trajectory, not
//     just instantaneous velocity.
//
//   All original gates (backboard timer, scoring zone, speed, alignment)
//   are unchanged. RewardConfig.h/cpp require no changes.
//
// 2026-06-26 - Rev 3 (NJH) — intercept calc + timer bug fix
//   Bug fix: currentTime was accumulated in GetReward(), which runs once
//   per player per step. In 3v3 (6 players) the 2.5s backboard timeout
//   was expiring in ~0.4s real time. Moved accumulation to PreStep().
//
//   Layer 1: replaced fixed POST_BOUNCE_SEC (0.4s) sample with a physical
//   intercept calculation. Iterates slices from bounceSlice forward and
//   takes the first where dist <= CAR_MAX_SPEED * timeAvail. Adapts to
//   bot distance and ball speed; also skips the reward automatically when
//   the bot cannot reach the rebound at all.
//
//   Layer 1: rebound alignment gate lowered 0.4 → 0.0. The velocity at
//   bounceSlice is confirmed post-bounce (Y-reflected inside the tick loop
//   before the slice is recorded). 0.4 was discarding valid sideways
//   rebounds that are still redirectable; 0.0 only rejects rebounds heading
//   straight back toward own goal. The inAttackHalf check on the intercept
//   position handles the real quality filtering.
//
// 2026-08-28 - Rev 4 (NJH) — real bounce detection + metrics
//   Bug fix: the reactive touch layer only checked ball PROXIMITY to the
//   backboard (within 300uu of the wall), never an actual bounce. A ball
//   drifting near the wall without contacting it could still arm the touch
//   bonus. Replaced with a real bounce detector in PreStep(): ball
//   Y-velocity sign flip between state.prev->ball.vel and state.ball.vel
//   while within range of a back wall — an actual physical event instead
//   of a proximity guess.
//
//   Bug fix: attribution was dead code. `boardStates` was keyed by the
//   OBSERVING player's own carId, so `bs.lastBackboardPlayerId != player.carId`
//   could never be false for that player's own entry — every player who
//   merely witnessed the ball near their opponent's backboard could claim
//   the touch bonus on their own later qualifying touch, regardless of who
//   actually set up the rebound. Replaced the per-player `boardStates` map
//   with a single ball-level BoardState (there is only one backboard and
//   one active bounce sequence at a time), gated by which team's attack the
//   bounce serves. The first touch that clears the full scoring-zone/speed/
//   alignment gate now consumes the bounce, so one physical rebound can't
//   be double-credited across sequential touches.
//
//   Added BackboardReboundMetricsCollector (BackboardReboundMetricsCollector.h)
//   so both layers' health (predicted-vs-actual bounce accuracy, touch/goal
//   conversion) is visible in W&B, and BallPredictionValidator
//   (BallPredictionValidator.h) so the underlying BallPred::Cache physics
//   model itself is checked against reality rather than assumed correct.
// ============================================================================
class BackboardReboundReward : public Reward {
private:
    float currentTime = 0.0f;

    // ---- Layer 1: predictive positioning constants ----
    static constexpr float PRED_HORIZON     = 1.5f;    // max lookahead for backboard bounce (s)
    static constexpr float PRED_PROX_REWARD = 1.2f;    // per-second reward at zero distance
    static constexpr float PRED_DIST_SCALE  = 900.0f;  // exponential falloff distance (UU)
    static constexpr float PRED_SHAPING_CAP = 0.3f;    // max total shaping per player per bounce sequence

    // ---- Layer 2: trajectory quality multiplier constants ----
    static constexpr float TRAJ_CHECK_TIME  = 0.75f;   // seconds ahead to sample ball position
    static constexpr float TRAJ_SCALE       = 1000.0f; // UU of goal-approach for full ±0.5 factor

    // ---- Reactive touch layer: real-bounce detection constants (Rev 4) ----
    static constexpr float BOUNCE_WALL_MARGIN = 300.0f; // max dist from back wall to count as a bounce (UU)
    static constexpr float BOUNCE_VEL_EPS     = 50.0f;  // min |vel.y| on each side of the flip, rejects noise (UU/s)
    static constexpr float BOUNCE_TIMEOUT     = 2.5f;   // window to follow up with a qualifying touch (s)

    // Ball-level bounce state. There is only ever one backboard and one
    // active bounce sequence at a time, so this is not per-player.
    struct BoardState {
        float lastBounceTime = -1.0f;
        Team  lastBounceTeam = Team::BLUE;  // which team's attack this bounce serves
    };
    BoardState _boardState;

    // ---- Layer 1 shaping cap state (per bounce sequence) ----
    struct ShapingState { int seqId = -1; float spent = 0.0f; };
    std::unordered_map<uint32_t, ShapingState> _shapingState;
    int   _shapingSeqId       = 0;
    float _prevPredBounceTime = -1.0f;  // updated in PreStep to detect new sequences

    BallPred::Cache _ballCache;  // Updated once per step in PreStep()

    bool IsInScoringZone(const Vec& pos, Team playerTeam) const {
        float goalY = (playerTeam == Team::BLUE) ? CommonValues::BACK_WALL_Y
            : -CommonValues::BACK_WALL_Y;
        // Widened X constraint to allow off-angle shots from wall bounce setups
        return (std::fabs(pos.x) < 2000.0f && std::fabs(pos.y - goalY) < 2000.0f);
    }

public:
    virtual void Reset(const GameState& initialState) override {
        currentTime = 0.0f;
        _boardState = BoardState();
        _shapingState.clear();
        _shapingSeqId = 0;
        _prevPredBounceTime = -1.0f;
    }

    virtual void PreStep(const GameState& state) override {
        currentTime += (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
        _ballCache.Update(state);

        // Detect new bounce prediction sequence so per-player shaping caps reset.
        // A new sequence starts when the predicted bounce time jumps up (old bounce
        // passed and a new future one appeared) or prediction reappears after dropout.
        float curBounceTime = -1.0f;
        if (_ballCache.valid) {
            for (int i = 0; i < BallPred::NUM_SLICES; i++) {
                if (_ballCache.slices[i].hitBackboard) {
                    curBounceTime = _ballCache.slices[i].t;
                    break;
                }
            }
        }
        if (curBounceTime > _prevPredBounceTime + 0.5f)
            _shapingSeqId++;
        _prevPredBounceTime = curBounceTime;

        // Real (not merely predicted) backboard bounce detection (Rev 4): the
        // ball's Y-velocity flips sign while within range of a back wall. This
        // is ball-level and computed once per step here, not per-player in
        // GetReward — same reasoning as the Rev 3 currentTime fix.
        if (state.prev) {
            float prevVelY = state.prev->ball.vel.y;
            float curVelY  = state.ball.vel.y;
            bool velocityReversed = (prevVelY > BOUNCE_VEL_EPS && curVelY < -BOUNCE_VEL_EPS)
                                 || (prevVelY < -BOUNCE_VEL_EPS && curVelY > BOUNCE_VEL_EPS);
            bool nearBackWall = std::fabs(std::fabs(state.ball.pos.y) - CommonValues::BACK_WALL_Y) < BOUNCE_WALL_MARGIN;
            bool aboveGoalHeight = state.ball.pos.z > (CommonValues::GOAL_HEIGHT - CommonValues::BALL_RADIUS);
            if (velocityReversed && nearBackWall && aboveGoalHeight) {
                _boardState.lastBounceTime = currentTime;
                // A bounce at +Y is behind Orange's goal (Blue's attacking target);
                // a bounce at -Y is behind Blue's goal (Orange's attacking target).
                _boardState.lastBounceTeam = (state.ball.pos.y > 0.0f) ? Team::BLUE : Team::ORANGE;
            }
        }
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

        Team playerTeam = player.team;

        // ----------------------------------------------------------------
        // Layer 1: Predictive positioning reward
        // Scans ball prediction for an upcoming backboard bounce.
        // If predicted within PRED_HORIZON, rewards proximity to the
        // post-bounce rebound landing position. No touch required.
        // ----------------------------------------------------------------
        float positioningReward = 0.0f;
        if (_ballCache.valid) {
            // Find first slice where the sticky hitBackboard flag turns true
            int bounceSlice = -1;
            for (int i = 0; i < BallPred::NUM_SLICES; i++) {
                if (_ballCache.slices[i].hitBackboard) { bounceSlice = i; break; }
            }
            if (bounceSlice >= 0) {
                float bounceTime = _ballCache.slices[bounceSlice].t;
                if (bounceTime <= PRED_HORIZON) {
                    Vec bouncePos = _ballCache.slices[bounceSlice].pos;

                    // FILTER: Only reward backboard bounces ABOVE goal height
                    if (bouncePos.z < (CommonValues::GOAL_HEIGHT - CommonValues::BALL_RADIUS)) {
                        return positioningReward;  // Low bounce, skip
                    }

                    // FILTER: Check if rebound is heading TOWARD goal (not away)
                    Vec reboundVel = _ballCache.slices[bounceSlice].vel;
                    Vec oppGoal = (playerTeam == Team::BLUE)
                        ? CommonValues::ORANGE_GOAL_BACK
                        : CommonValues::BLUE_GOAL_BACK;
                    Vec toGoal = (oppGoal - bouncePos).Normalized();
                    float reboundAlignment = reboundVel.Normalized().Dot(toGoal);

                    if (reboundAlignment < 0.0f) {
                        return positioningReward;  // Rebound heading straight back toward own goal, skip
                    }

                    // Find the earliest slice after the bounce that the bot can physically reach.
                    // This replaces a fixed 0.4s sample: it adapts to both bot position and ball speed.
                    const float CAR_MAX_SPEED = 2300.0f;
                    Vec interceptPos = {};
                    bool foundIntercept = false;
                    for (int j = bounceSlice; j < BallPred::NUM_SLICES; j++) {
                        float timeAvail  = _ballCache.slices[j].t;
                        float distToSlice = (player.pos - _ballCache.slices[j].pos).Length();
                        if (distToSlice <= CAR_MAX_SPEED * timeAvail) {
                            interceptPos = _ballCache.slices[j].pos;
                            foundIntercept = true;
                            break;
                        }
                    }
                    if (!foundIntercept) return positioningReward;

                    // Only reward if the intercept point is in the attacking half
                    bool inAttackHalf = (playerTeam == Team::BLUE) ? (interceptPos.y > 0.0f)
                                                                    : (interceptPos.y < 0.0f);
                    if (inAttackHalf) {
                        float dist = (player.pos - interceptPos).Length();

                        // BONUS: Height multiplier — higher backboard bounces are more valuable
                        float heightBonus = (bouncePos.z - CommonValues::GOAL_HEIGHT) / 1500.0f;
                        heightBonus = RS_CLAMP(heightBonus, 0.0f, 1.0f);
                        float heightMult = 1.0f + 0.5f * heightBonus;  // 1.0x to 1.5x based on height

                        float raw = PRED_PROX_REWARD * expf(-dist / PRED_DIST_SCALE) * dt * heightMult;

                        // Per-sequence cap: prevent parking at the rebound point indefinitely.
                        // Resets when a new backboard bounce is predicted (seqId changes in PreStep).
                        auto& ss = _shapingState[player.carId];
                        if (ss.seqId != _shapingSeqId) { ss.seqId = _shapingSeqId; ss.spent = 0.0f; }
                        if (ss.spent < PRED_SHAPING_CAP) {
                            raw = RS_MIN(raw, PRED_SHAPING_CAP - ss.spent);
                            ss.spent += raw;
                            positioningReward = raw;
                        }
                    }
                }
            }
        }

        // ----------------------------------------------------------------
        // Reactive touch-based reward — requires a REAL bounce (Rev 4)
        // ----------------------------------------------------------------
        if (!player.ballTouchedStep)
            return positioningReward;

        // Require a recent, real backboard bounce
        if (_boardState.lastBounceTime < 0.0f || (currentTime - _boardState.lastBounceTime) > BOUNCE_TIMEOUT)
            return positioningReward;

        // Only the team the bounce serves (their attacking backboard) can claim it
        if (_boardState.lastBounceTeam != playerTeam)
            return positioningReward;

        // Player touch after backboard: check if near scoring zone
        if (!IsInScoringZone(state.ball.pos, playerTeam))
            return positioningReward;

        Vec targetGoal = (playerTeam == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_BACK
            : CommonValues::BLUE_GOAL_BACK;

        Vec toGoal = (targetGoal - state.ball.pos).Normalized();
        float speed = state.ball.vel.Length();
        if (speed < 1200.0f)  // Threshold: 1200 UU/s (unchanged)
            return positioningReward;

        Vec dir = state.ball.vel / speed;
        float align = dir.Dot(toGoal);
        if (align < 0.6f)
            return positioningReward;

        float alignScore = RS_CLAMP((align - 0.6f) / 0.4f, 0.0f, 1.0f);
		float speedScore = RS_CLAMP(speed / (2300.0f * 1.2f), 0.0f, 1.0f); // 2300 is max car speed, 1.2x so it can be slightly faster than max car speed (boosted) = 2760.0f

        float reward = 4.0f * alignScore * speedScore;

        // ----------------------------------------------------------------
        // Layer 2: Trajectory quality multiplier
        // Checks where the ball is predicted to be TRAJ_CHECK_TIME seconds
        // from now. Heading toward goal → up to 1.5x; drifting away → 0.5x.
        // ----------------------------------------------------------------
        if (_ballCache.valid) {
            Vec futurePos     = _ballCache.AtTime(TRAJ_CHECK_TIME).pos;
            float currentDist = (state.ball.pos - targetGoal).Length();
            float futureDist  = (futurePos - targetGoal).Length();
            float improvement = currentDist - futureDist;   // positive = ball heading toward goal
            float trajFactor  = RS_CLAMP(improvement / TRAJ_SCALE, -0.5f, 0.5f);
            reward *= (1.0f + trajFactor);                  // range [0.5x … 1.5x]
        }

        // Goal bonus (unchanged)
        if (state.goalScored) {
            reward += 3.0f;
        }

        // Consume the bounce (Rev 4): this physical rebound has now been
        // credited, so a later touch within BOUNCE_TIMEOUT can't claim it again.
        _boardState.lastBounceTime = -1.0f;

        return positioningReward + RS_CLAMP(reward, 0.0f, 8.0f);
    }
};

// NOTE: HookShotReward is now implemented in HookshotReward.h
// Use #include <RLGymCPP/Rewards/HookshotReward.h> instead


// 2025-12-17 | Rev 5 | NJH
// - Allow opponent-half credit at 1/4 reward (still learn "win 50s" upfield)
// - Keep max payout near own goal (danger sphere)

class FiftyFiftyReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;

        bool isFiftyFifty = false;
        for (const auto& opponent : state.players) {
            if (opponent.team == player.team) continue;
            if (opponent.ballTouchedStep) { isFiftyFifty = true; break; }
        }
        if (!isFiftyFifty) return 0.0f;

        const Vec myGoalBack = (player.team == Team::BLUE) ? CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;
        const Vec myGoalCenter = (player.team == Team::BLUE) ? CommonValues::BLUE_GOAL_CENTER : CommonValues::ORANGE_GOAL_CENTER;

        const bool ballInOwnHalf = (player.team == Team::BLUE) ? (state.ball.pos.y <= 0.0f) : (state.ball.pos.y >= 0.0f);

        Vec goalToBall = state.ball.pos - myGoalBack;
        float distGB = goalToBall.Length();
        if (distGB < 1e-3f) return 0.0f;

        Vec goalToPlayer = player.pos - myGoalBack;
        Vec lineDir = goalToBall / distGB;

        float projPlayer = goalToPlayer.Dot(lineDir);
        Vec lateral = goalToPlayer - lineDir * projPlayer;
        float lateralDist = lateral.Length();

        if (projPlayer < 0.0f || projPlayer > distGB + 500.0f) return 0.0f;

        float alignmentScore = RS_CLAMP(1.0f - lateralDist / 1500.0f, 0.0f, 1.0f);
        float depthScore = RS_CLAMP(projPlayer / distGB, 0.0f, 1.0f);

        float positionalScore = alignmentScore * depthScore;
        if (positionalScore <= 0.0f) return 0.0f;

        const float distBallToMyGoalCenter = (state.ball.pos - myGoalCenter).Length();

        float zoneMult = 1.0f;
        if (distBallToMyGoalCenter <= 2000.0f) {
            zoneMult = 1.0f;
            positionalScore = 1.0f;
        }
        else {
            zoneMult = ballInOwnHalf ? 0.75f : 0.25f; // <<< key change
        }

        const float baseReward = 5.0f;
        return baseReward * positionalScore * zoneMult;
    }
};

// ============================================================================
// SingleJumpLow5050Reward
// 2025-01-12 | Rev 1 | NJH
//
// Rewards winning low 50/50 challenges with a single jump (no flip/dodge).
// Key behaviors:
//  - Both player and opponent must touch ball (50/50)
//  - Player uses only single jump (no flip, no dodge)
//  - Ball contact must be low (z < 300 units)
//  - Reward based on positioning and ownership after contact
// 2026-06-19 | Rev 2 (NJH):
// Simplified code and removed some redundant gates. The core logic is unchanged, but it's now more concise and easier to read.
// ============================================================================

class SingleJumpLow5050Reward : public Reward {
    float maxBallHeight = 300.0f;
    float baseReward = 8.0f;

public:
    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;
        if (state.ball.pos.z > maxBallHeight) return 0.0f;
        if (!player.hasJumped || player.isOnGround) return 0.0f;
        if (player.isFlipping || player.hasDoubleJumped) return 0.0f;

        bool isChallenge = false;
        for (const auto& opp : state.players)
            if (opp.team != player.team && opp.ballTouchedStep) { isChallenge = true; break; }
        if (!isChallenge) return 0.0f;

        Vec oppGoal = (player.team == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_BACK
            : CommonValues::BLUE_GOAL_BACK;
        float quality = RS_CLAMP(
            state.ball.vel.Dot(SafeNorm(oppGoal - state.ball.pos)) / 2000.0f,
            0.1f, 1.0f);

        return baseReward * quality;
    }
};


// ============================================================================
// EmergencySaveCommitReward
// 2025-12-16 - Rev 1 (NJH):
//  - When ball is threatening our net, reward committing to the save:
//    * closing speed to ball
//    * positioning between ball and own goal
//  - Idea: makes “do anything to save” outscore low-value mechanic farming.
// ============================================================================
class EmergencySaveCommitReward : public Reward {
private:
    float dangerDist = 2600.0f;     // how close ball is to our goal to trigger
    float minThreatDot = 0.25f;     // ball moving toward our goal

public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        const Vec myGoal = (player.team == Team::BLUE) ? CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

        Vec ballToGoal = (myGoal - state.ball.pos);
        float distBallGoal = ballToGoal.Length();
        if (distBallGoal > dangerDist) return 0.0f;

        Vec ballVelN = SafeNorm(state.ball.vel);
        Vec toGoalN = SafeNorm(ballToGoal);

        float threat = ballVelN.Dot(toGoalN);
        if (threat < minThreatDot) return 0.0f;

        // Reward: close on ball (positive speed toward ball)
        Vec toBallN = SafeNorm(state.ball.pos - player.pos);
        float speedToBall = player.vel.Dot(toBallN); // uu/s

        // Reward: “between ball and goal” geometry
        // player should be closer to goal than ball is
        float distPlayerGoal = (player.pos - myGoal).Length();
        float between = (distPlayerGoal < distBallGoal) ? 1.0f : 0.0f;

        // Normalize speed-to-ball into [0..1] using car max speed
        float speedFrac = RS_CLAMP(speedToBall / CommonValues::CAR_MAX_SPEED, 0.0f, 1.0f);

        // Urgency multiplier increases as ball gets closer
        float urgency = RS_CLAMP(1.0f - (distBallGoal / dangerDist), 0.0f, 1.0f);

        // Small continuous reward per tick (keep it modest; it’s always-on in danger)
        return (0.6f * speedFrac + 0.4f * between) * (0.5f + 0.5f * urgency);
    }
};

// ============================================================================
// ShadowDefenseVsPossessionReward
// 2025-12-16 - Rev 1 (NJH):
//  - Detect opponent possession (dribble-ish or close control).
//  - Reward shadowing: sit on line between ball and our goal at a safe distance,
//    and match lateral (x) as ball moves.
// 2026-06-03 - Rev 2 (NJH): align with dignitas shadow-defense guides.
//  - Shadow distance is now DYNAMIC (500–1700 UU), scaled by how fast the play
//    is developing (fast push → more space to react; slow dribble → sit closer).
//  - Added VELOCITY MATCHING: reward moving WITH the possessor (mirrors speed and
//    lateral movement in one term), per "move at the same speed as your opponent."
//  - Added FACING term (car pointed at the play, ready to react).
//  - Replaced the weak 0.1 "between" term with SOFT anti-overcommit multipliers:
//    goal-side ramp (don't drift past the ball) + closing-speed cap (don't blitz).
//  - Added W&B metrics (ShadowDefense/*).
// ============================================================================
class ShadowDefenseVsPossessionReward : public Reward {
private:
    // ---- Dynamic shadow distance ----
    static constexpr float SHADOW_MIN      = 500.0f;   // UU — slow play, sit close
    static constexpr float SHADOW_MAX      = 1700.0f;  // UU — fast play, more space to react
    static constexpr float PLAY_REF_SPEED  = 1800.0f;  // UU/s — speed at which we want max spacing
    // ---- Position band falloff ----
    static constexpr float BAND_PLATEAU    = 250.0f;   // UU — full credit within this of target
    static constexpr float BAND_FALLOFF    = 1100.0f;  // UU — zero credit beyond this
    // ---- Velocity match ----
    static constexpr float VEL_MATCH_SCALE = 1500.0f;  // UU/s — relative-speed scale for mirroring
    // ---- Soft overcommit multipliers ----
    static constexpr float GOALSIDE_RANGE  = 1200.0f;  // UU — margin range for goal-side ramp
    static constexpr float CLOSE_CAP       = 900.0f;   // UU/s — closing speed allowed before penalty
    static constexpr float CLOSE_RANGE     = 1200.0f;  // UU/s — falloff range above the cap
    static constexpr float CLOSE_FLOOR     = 0.2f;     // min closing multiplier (don't fully zero)

    inline static std::atomic<int>   s_fireCount     { 0 };
    inline static std::atomic<float> s_rewardSum     { 0.0f };
    inline static std::atomic<float> s_targetDistSum { 0.0f };
    inline static std::atomic<float> s_velMatchSum   { 0.0f };

    static void AtomicAdd(std::atomic<float>& a, float v) {
        float prev = a.load();
        while (!a.compare_exchange_weak(prev, prev + v)) {}
    }

    bool OppHasPossession(const Player& me, const GameState& state, Player& outOpp) const {
        // Pick the “best” opponent possessor
        float bestScore = 0.0f;
        bool found = false;

        for (auto& opp : state.players) {
            if (opp.team == me.team) continue;
            if (opp.isDemoed) continue;

            Vec rel = state.ball.pos - opp.pos;
            float h = rel.z;
            float horiz = Vec(rel.x, rel.y, 0).Length();

            bool ballOnCar = (h > 25.0f && h < 210.0f && horiz < 155.0f && opp.isOnGround);

            float distOppBall = rel.Length();
            float ballSpeed = state.ball.vel.Length();

            // Also count “soft control” even if not perfectly on roof
            bool closeControl = (distOppBall < 420.0f && ballSpeed < 1400.0f && opp.isOnGround);

            if (ballOnCar || closeControl) {
                float score = ballOnCar ? 2.0f : 1.0f;
                score += RS_CLAMP(1.0f - distOppBall / 600.0f, 0.0f, 1.0f);

                if (score > bestScore) {
                    bestScore = score;
                    outOpp = opp;
                    found = true;
                }
            }
        }
        return found;
    }

public:
    struct Metrics { int fireCount = 0; float avgReward = 0.0f; float avgTargetDist = 0.0f; float avgVelMatch = 0.0f; };
    static Metrics GetAndResetMetrics() {
        int   count   = s_fireCount.exchange(0);
        float rSum    = s_rewardSum.exchange(0.0f);
        float tdSum   = s_targetDistSum.exchange(0.0f);
        float vmSum   = s_velMatchSum.exchange(0.0f);
        float inv     = count > 0 ? 1.0f / count : 0.0f;
        return { count, rSum * inv, tdSum * inv, vmSum * inv };
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        Player possessor;
        if (!OppHasPossession(player, state, possessor)) return 0.0f;

        const Vec myGoal = (player.team == Team::BLUE) ? CommonValues::BLUE_GOAL_CENTER : CommonValues::ORANGE_GOAL_CENTER;

        // Shadow defense applies while the ball is in our half or near midfield, so the
        // bot can pick up the shadow early during the retreat ("gradually close in").
        const bool ballInOwnHalf = (player.team == Team::BLUE) ? (state.ball.pos.y <= 1500.0f) : (state.ball.pos.y >= -1500.0f);
        if (!ballInOwnHalf) return 0.0f;

        Vec ballToGoalN = SafeNorm(myGoal - state.ball.pos);

        // 1) DYNAMIC shadow point: distance scales with how fast the play is developing.
        //    Faster opponent/ball → sit further back for reaction time; slower → tighten up.
        float playSpeed  = RS_MAX(possessor.vel.Length(), state.ball.vel.Length());
        float speedFrac  = RS_CLAMP(playSpeed / PLAY_REF_SPEED, 0.0f, 1.0f);
        float targetDist = SHADOW_MIN + speedFrac * (SHADOW_MAX - SHADOW_MIN);   // 500..1700
        Vec   shadowPt   = state.ball.pos + ballToGoalN * targetDist;
        float distToPt   = (player.pos - shadowPt).Length();
        float posBand    = RS_CLAMP(1.0f - (distToPt - BAND_PLATEAU) / (BAND_FALLOFF - BAND_PLATEAU), 0.0f, 1.0f);

        // 2) VELOCITY MATCH: move WITH the possessor (mirrors speed and lateral movement).
        float relVel     = (player.vel - possessor.vel).Length();
        float velMatch   = RS_CLAMP(1.0f - relVel / VEL_MATCH_SCALE, 0.0f, 1.0f);

        // 3) FACING the play (ready to react/challenge).
        float facing     = RS_CLAMP(player.rotMat.forward.Dot(SafeNorm(state.ball.pos - player.pos)), 0.0f, 1.0f);

        // 4) SOFT anti-overcommit multipliers.
        float distPlayerGoal = (player.pos - myGoal).Length();
        float distBallGoal   = (state.ball.pos - myGoal).Length();
        float margin     = distBallGoal - distPlayerGoal;                  // + = goal-side of ball (good)
        float goalSide   = RS_CLAMP(0.5f + margin / GOALSIDE_RANGE, 0.1f, 1.0f);  // soft ramp, floor 0.1
        float closing    = player.vel.Dot(SafeNorm(state.ball.pos - player.pos)); // + = charging the ball
        float closeMult  = RS_CLAMP(1.0f - (closing - CLOSE_CAP) / CLOSE_RANGE, CLOSE_FLOOR, 1.0f);

        float base   = 0.55f * posBand + 0.30f * velMatch + 0.15f * facing;
        float reward = base * goalSide * closeMult;   // max ~1.0

        s_fireCount.fetch_add(1);
        AtomicAdd(s_rewardSum, reward);
        AtomicAdd(s_targetDistSum, targetDist);
        AtomicAdd(s_velMatchSum, velMatch);

        return reward;
    }
};


// ============================================================================
// CatchBallReward
// Rewards soft "catch" touches that slow a fast ball and keep it close & in
// front of the car, promoting possession instead of booming it away.
// 2025-12-08 - Rev 2 - Added positional & speed-shaping. (NJH)
// ============================================================================
class CatchBallReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep)
            return 0.0f;
        if (!state.prev)
            return 0.0f;

        float prevBallSpeed = state.prev->ball.vel.Length();
        float ballSpeed = state.ball.vel.Length();

        // Only treat as "catch" if ball was moving reasonably fast
        if (prevBallSpeed < 800.0f)
            return 0.0f;

        // Slowdown factor: bigger slowdown = better catch
        float slowdown = RS_CLAMP((prevBallSpeed - ballSpeed) / prevBallSpeed, 0.0f, 1.0f);
        if (slowdown <= 0.0f)
            return 0.0f;

        // Ball should be near and in front of the car
        Vec rel = state.ball.pos - player.pos;
        float dist = rel.Length();
        if (dist > 600.0f)
            return 0.0f;

        Vec forward = player.rotMat.forward;
        float frontAlign = forward.Dot(rel.Normalized()); // 1 = directly in front
        if (frontAlign < 0.3f)
            return 0.0f;

        // Penalize if ball leaves too fast even after "catch"
        float baseCap = 1200.0f;
        float speedPenaltyFactor = 1.0f;
        if (ballSpeed > baseCap) {
            speedPenaltyFactor = RS_CLAMP(1.0f - (ballSpeed - baseCap) / 1000.0f, 0.0f, 1.0f);
        }

        float alignFactor = 0.5f + 0.5f * RS_CLAMP(frontAlign, 0.0f, 1.0f);

        // Final catch reward: good slowdown, good alignment, and ball not flying away
        float reward = 4.0f * slowdown * alignFactor * speedPenaltyFactor;
        return RS_CLAMP(reward, 0.0f, 8.0f);
    }
};

// CatchBallOnCarReward
// Rewards soft "catch" touches that slow a fast ball and keep it close & in
// front of the car, promoting possession instead of booming it away.
// 2025-12-08 - Rev 2 - Added positional & speed-shaping. (NJH)
// 2025-12-14 - Rev (tuned) - emphasize catches that end up "on top" of the car:
//   * tighter on-top geometry (height + horiz radius)
//   * larger bonus when ball is effectively carried (low relative velocity)
//   * more weight to slowdown while retaining alignment & speed penalties
class CatchBallOnCarReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep)
            return 0.0f;
        if (!state.prev)
            return 0.0f;

        // Previous and current ball speeds
        float prevBallSpeed = state.prev->ball.vel.Length();
        float ballSpeed = state.ball.vel.Length();

        // Only treat as "catch" if ball was moving reasonably fast (but allow somewhat lower)
        if (prevBallSpeed < 600.0f)
            return 0.0f;

        // Slowdown factor: bigger slowdown = better catch (0..1)
        float slowdown = RS_CLAMP((prevBallSpeed - ballSpeed) / prevBallSpeed, 0.0f, 1.0f);
        if (slowdown <= 0.0f)
            return 0.0f;

        // Ball relative to car
        Vec rel = state.ball.pos - player.pos;
        float horizDist = Vec(rel.x, rel.y, 0).Length();
        float heightAboveCar = rel.z;

        // Must be roughly in front of the car
        Vec forward = player.rotMat.forward;
        float frontAlign = forward.Dot(rel.Normalized()); // 1 = directly in front
        if (frontAlign < 0.4f)
            return 0.0f; // require stronger frontal alignment than before

        // Signed forward projection of the ball relative to car centre.
        // Positive  = ball is over the hood/front half.
        // Negative  = ball is over the trunk/rear half.
        // ~60 UU is roughly half the Octane hitbox length.
        float forwardComp = forward.Dot(rel);

        // On-top: tight geometry AND ball must be on the front half of the roof
        bool isOnTop = (heightAboveCar > 50.0f && heightAboveCar < 140.0f
                     && horizDist < 100.0f && forwardComp > 0.0f);

        // Near-top: slightly looser — allow up to 60 UU behind centre (mid-roof) but not trunk
        bool isNearTop = (heightAboveCar > 30.0f && heightAboveCar < 200.0f
                       && horizDist < 220.0f && forwardComp > -60.0f);

        // Penalize if ball leaves too fast even after "catch"
        float baseCap = 1200.0f;
        float speedPenaltyFactor = 1.0f;
        if (ballSpeed > baseCap) {
            speedPenaltyFactor = RS_CLAMP(1.0f - (ballSpeed - baseCap) / 1000.0f, 0.0f, 1.0f);
            if (speedPenaltyFactor <= 0.0f) return 0.0f;
        }

        // Alignment factor: prefer catches directly in front
        float alignFactor = 0.6f + 0.4f * RS_CLAMP(frontAlign, 0.0f, 1.0f); // [0.6..1.0]

        // Relative velocity between ball and car: small => ball is effectively "on" the car
        Vec relVel = state.ball.vel - player.vel;
        float relSpeed = relVel.Length();

        // Front-half scale: linearly ramps from 0.5x at car centre (forwardComp=0)
        // to 1.5x at ~120 UU ahead (well over the hood). Rear-half is already gated
        // out by isOnTop/isNearTop, but clamp to [0.5, 1.5] for safety.
        float frontHalfScale = RS_CLAMP(0.5f + forwardComp / 120.0f, 0.5f, 1.5f);

        // On-top carry bonus if relative speed is small, scaled by front-half position
        float carryBonus = 0.0f;
        if (isOnTop && relSpeed < 250.0f) {
            carryBonus = 4.0f * frontHalfScale; // up to 6.0 for hood catches, 2.0 at centre
        }
        else if (isNearTop && relSpeed < 350.0f) {
            carryBonus = 2.0f * frontHalfScale; // up to 3.0 for front near-top
        }

        // Base catch reward: emphasize slowdown but scale by alignment and speed penalty
        float baseReward = 5.0f * slowdown * alignFactor * speedPenaltyFactor; // up to ~5

        // Extra small bonus for being on-top even if slowdown modest
        float topProximityBonus = isOnTop ? 1.0f : (isNearTop ? 0.4f : 0.0f);

        float reward = baseReward + carryBonus + topProximityBonus;

        // Final clamp: allow bigger payouts for exceptional on-top catches but bounded
        float maxReward = isOnTop ? 15.0f : 8.0f;
        return RS_CLAMP(reward, 0.0f, maxReward);
    }
};



// Directional air roll - using air roll for car control
class DirectionalAirRollReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (player.isOnGround) return 0.0f;

        // Reward for using roll during aerials
        if (abs(player.prevAction.roll) > 0.5f) {
            // Check if moving meaningfully in air
            if (player.vel.z > 300.0f || abs(player.angVel.Length()) > 2.0f) {
                return 0.5f;
            }
        }
        
        return 0.0f;
    }
};

// ============================================================================
// DIAMOND TIER REWARDS
// ============================================================================

// ============================================================================
// EDITED: ShotAccuracyReward - Now returns multiplier values (1.0 base, 1.5 bonus)
// ============================================================================

// Shot accuracy - aiming for corners of goal
// NOTE: Returns multiplier values (1.0 = no bonus, 1.5 = bonus)
// Use as a multiplier for shot rewards via MathRewardWrapper::MULTIPLY
class ShotAccuracyReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 1.0f; // Base multiplier, no bonus

        Vec targetGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        // Check if ball is moving toward goal
        Vec ballToGoal = (targetGoal - state.ball.pos).Normalized();
        float alignment = state.ball.vel.Normalized().Dot(ballToGoal);

        if (alignment < 0.5f) return 1.0f; // Not shooting at goal - base multiplier

        // Calculate where ball is heading relative to goal
        // Top corners are best
        float goalCenterY = targetGoal.y;
        float goalCenterZ = CommonValues::GOAL_HEIGHT / 2.0f;

        // Target corners
        Vec topLeftCorner = Vec(-CommonValues::GOAL_WIDTH_FROM_CENTER + 200.0f,
            goalCenterY,
            CommonValues::GOAL_HEIGHT - 100.0f);
        Vec topRightCorner = Vec(CommonValues::GOAL_WIDTH_FROM_CENTER - 200.0f,
            goalCenterY,
            CommonValues::GOAL_HEIGHT - 100.0f);

        // Check if ball trajectory aims near corners
        Vec ballVelDir = state.ball.vel.Normalized();
        float distToLeftCorner = (topLeftCorner - state.ball.pos).Normalized().Dot(ballVelDir);
        float distToRightCorner = (topRightCorner - state.ball.pos).Normalized().Dot(ballVelDir);

        float bestCornerAlignment = RS_MAX(distToLeftCorner, distToRightCorner);

        if (bestCornerAlignment > 0.9f) {
            return 1.5f; // Excellent corner shot - 1.5x multiplier
        }
        else if (bestCornerAlignment > 0.7f) {
            return 1.5f; // Good shot placement - 1.5x multiplier
        }

        return 1.0f; // Inaccurate shot - base multiplier
    }
};

// ============================================================================
// Shot accuracy - checking if shot is within goal boundaries (ball radius adjusted)
// ============================================================================

// ============================================================================
// ShotAccuracyInBoundsReward
// 2026-04-18 | Rev 2.0 | NJH / Claude
// Predictive multiplier for shot rewards (via MathRewardWrapper::MULTIPLY).
//
// v1 only fired on the goal-scored frame, which almost never coincides with the
// shot-event frame produced by SpeedScaledShot / BallRedirect / etc. — so the
// multiplier was effectively 1.0 on every shot event. Wrapped rewards were
// paid a flat amount regardless of placement.
//
// v2 extrapolates ball trajectory to the opponent goal plane every step
// (velocity + gravity, same ballistic model as ShotPlacementReward) and
// returns:
//   2.0f  -> predicted to enter the goal mouth within in-bounds box
//   0.5f  -> heading toward goal plane but predicted to miss
//   1.0f  -> not heading at goal (neutral — no effect on multiplier partner)
// Goal-scored frame keeps the v1 placement check as a fallback.
// ============================================================================
class ShotAccuracyInBoundsReward : public Reward {
private:
    mutable std::map<uint32_t, Vec> lastBallPos;

    static constexpr float MIN_BALL_SPEED   = 500.0f;   // UU/s — below this, not a shot
    static constexpr float GRAVITY          = 650.0f;   // RocketSim ~650 UU/s^2
    static constexpr float MAX_PREDICT_TIME = 4.0f;     // seconds — matches BallPrediction horizon
    static constexpr float MIN_TOWARD_VY    = 50.0f;    // UU/s — reject near-lateral shots
    static constexpr float MISS_MARGIN      = 400.0f;   // UU around goal mouth to count as "near miss"

    static inline bool IsInBounds(float x, float z) {
        const float IN_BOUNDS_W = CommonValues::GOAL_WIDTH_FROM_CENTER - CommonValues::BALL_RADIUS - 40.0f;
        const float IN_BOUNDS_H = CommonValues::GOAL_HEIGHT - CommonValues::BALL_RADIUS - 40.0f;
        return fabsf(x) <= IN_BOUNDS_W && z >= CommonValues::BALL_RADIUS && z <= IN_BOUNDS_H;
    }

public:
    virtual void Reset(const GameState& initialState) override {
        lastBallPos.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        lastBallPos[player.carId] = state.ball.pos;

        // Fallback: trust the actual goal classification when it fires this frame
        if (state.goalScored) {
            bool orangeScored = (state.ball.pos.y > 0);
            bool playerTeamScored = (player.team == Team::ORANGE && orangeScored) ||
                                    (player.team == Team::BLUE && !orangeScored);
            if (!playerTeamScored) return 1.0f;

            Vec p = lastBallPos[player.carId];
            return IsInBounds(p.x, p.z) ? 2.0f : 0.5f;
        }

        // Predictive path: ballistic extrapolation to opponent goal plane
        if (state.ball.vel.Length() < MIN_BALL_SPEED) return 1.0f;

        float goalY = (player.team == Team::BLUE) ?  CommonValues::BACK_WALL_Y
                                                  : -CommonValues::BACK_WALL_Y;

        bool movingToGoal = (player.team == Team::BLUE)
                            ? (state.ball.vel.y >  MIN_TOWARD_VY)
                            : (state.ball.vel.y < -MIN_TOWARD_VY);
        if (!movingToGoal) return 1.0f;

        float tToGoal = (goalY - state.ball.pos.y) / state.ball.vel.y;
        if (tToGoal < 0.0f || tToGoal > MAX_PREDICT_TIME) return 1.0f;

        float xAtGoal = state.ball.pos.x + state.ball.vel.x * tToGoal;
        float zAtGoal = state.ball.pos.z + state.ball.vel.z * tToGoal
                         - 0.5f * GRAVITY * tToGoal * tToGoal;

        if (IsInBounds(xAtGoal, zAtGoal)) return 2.0f;

        // Heading at goal plane but predicted to miss — still a shot attempt, half credit
        bool nearMiss = fabsf(xAtGoal) <= (CommonValues::GOAL_WIDTH_FROM_CENTER + MISS_MARGIN)
                     && zAtGoal >= -MISS_MARGIN
                     && zAtGoal <= (CommonValues::GOAL_HEIGHT + MISS_MARGIN);
        return nearMiss ? 0.5f : 1.0f;
    }
};

// ============================================================================
// REWARD CONFIGURATION: SpeedScaledShot multiplied by ShotAccuracy
// ============================================================================

// This replaces your existing:
// WeightedReward(new ZeroSumReward(new SpeedScaledShotReward(), 1.0f), 38.0f),

// New configuration with MathRewardWrapper multiply:
//WeightedReward(
//    new ZeroSumReward(
//        new MathRewardWrapper(
//            {
//                new SpeedScaledShotReward(),
//                new ShotAccuracyReward()
//            },
//            MathOp::MULTIPLY
//        ),
//        1.0f
//    ),
//    38.0f
//)


// ============================================================================
// HOW THIS WORKS:
// ============================================================================
// 1. SpeedScaledShotReward outputs: shot_reward (varies based on speed)
// 2. ShotAccuracyReward outputs: multiplier (1.0 or 1.5)
// 3. MathRewardWrapper multiplies them: final_reward = shot_reward * multiplier
// 4. ZeroSumReward wraps the result for zero-sum game dynamics
// 5. WeightedReward scales final result by 38.0f
//
// Result: Fast, accurate shots get 1.5x bonus. Fast but inaccurate shots get 1.0x (no bonus).

// BackboardDefenseReward (improved)
// - Hybrid behavior:
//   * Continuous shaping: small reward when the ball's distance from our goal increases (i.e. cleared away).
//   * Discrete on-touch rewards:
//       - strong clear toward corner (fast, well aligned) => immediate reward
//       - soft close touch (possession-preserving) => small immediate reward + stores for follow-up
//       - follow-up aerial within timeout after a soft touch => bonus (encourages setting up aerials / airdribble)
// - Works robustly with and without state.prev

// ============================================================================
// OffBallAerialPenalty
// 2026-04-19 | Rev 1.0 | NJH / Claude
//
// Direct counterpressure against off-ball aerial commitment. Fires a small
// continuous penalty each step the player is:
//   1. Airborne (!isOnGround)
//   2. Ball distance > BALL_DIST_THRESHOLD (3000 UU)
//   3. Airborne for longer than GRACE_PERIOD (0.5s) without touching the ball
//
// The grace period avoids penalizing normal jumps or brief wall hops. Only
// sustained aerial commitment far from the ball triggers the penalty.
//
// Do NOT ZeroSum — this is a per-player behavioral penalty, not a competitive
// outcome. ZeroSumming would reward the opponent for your bot's bad aerial,
// which is a separate signal already covered by goal and possession rewards.
//
// W&B health check: Watch flip_reset_acquisition_rate — if it drops >30%,
// penalty weight is too high and suppressing legitimate aerial attempts near ball.
// ============================================================================
class OffBallAerialPenalty : public Reward {
private:
    mutable std::map<uint32_t, float> airtimeWithoutTouch;

    static constexpr float BALL_DIST_THRESHOLD    = 3000.0f;  // UU
    static constexpr float GRACE_PERIOD           = 0.5f;     // seconds before penalty starts
    static constexpr float PENALTY_PER_SECOND     = 0.15f;    // penalty rate while off-ball-aerial
    // 2026-04-20 | Boost-scaling: low boost makes off-ball aerials 2x as costly.
    static constexpr float BOOST_LOW_THRESHOLD    = 30.0f;    // below this = "low boost"
    static constexpr float LOW_BOOST_PENALTY_SCALE = 2.0f;   // multiplier when boost < threshold

public:
    virtual void Reset(const GameState& initialState) override {
        airtimeWithoutTouch.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

        if (player.isOnGround || player.ballTouchedStep) {
            airtimeWithoutTouch[player.carId] = 0.0f;
            return 0.0f;
        }

        float& airtime = airtimeWithoutTouch[player.carId];
        airtime += dt;

        if (airtime <= GRACE_PERIOD) return 0.0f;

        float ballDist = (player.pos - state.ball.pos).Length();
        if (ballDist <= BALL_DIST_THRESHOLD) return 0.0f;

        float boostScale = (player.boost < BOOST_LOW_THRESHOLD) ? LOW_BOOST_PENALTY_SCALE : 1.0f;
        return -PENALTY_PER_SECOND * boostScale * dt;
    }
};

// SecondaryAerialPenalty: discourages non-closest teammate from committing to an aerial.
// Fires when: ball height > 250 UU (aerial situation), player is airborne, AND at least
// one teammate is closer to ball than this player by more than DIST_SLACK UU.
// Grace period prevents penalizing quick reactions. 1v1: no teammates = no penalty ever.
// Not ZeroSummed: per-player behavioral penalty.
class SecondaryAerialPenalty : public Reward {
private:
    mutable std::map<uint32_t, float> secondaryAirtime;

    static constexpr float BALL_HEIGHT_THRESHOLD = 250.0f;   // aerial is "active" above this
    static constexpr float GRACE_PERIOD          = 0.3f;     // seconds before penalty starts
    static constexpr float PENALTY_PER_SECOND    = 0.10f;    // penalty rate (gentler than OffBallAerial)
    static constexpr float DIST_SLACK            = 150.0f;   // UU slack to avoid penalizing near-ties

public:
    virtual void Reset(const GameState& initialState) override {
        secondaryAirtime.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

        // No aerial situation or player already on ground -> reset and skip
        if (state.ball.pos.z < BALL_HEIGHT_THRESHOLD || player.isOnGround) {
            secondaryAirtime[player.carId] = 0.0f;
            return 0.0f;
        }

        // Check if any teammate is meaningfully closer to the ball
        float myDist = (player.pos - state.ball.pos).Length();
        bool teammateCloser = false;
        for (const auto& p : state.players) {
            if (p.team == player.team && p.carId != player.carId) {
                float tDist = (p.pos - state.ball.pos).Length();
                if (tDist + DIST_SLACK < myDist) {
                    teammateCloser = true;
                    break;
                }
            }
        }

        if (!teammateCloser) {
            secondaryAirtime[player.carId] = 0.0f;
            return 0.0f;
        }

        // Accumulate airtime as secondary aerial
        float& airtime = secondaryAirtime[player.carId];
        airtime += dt;

        if (airtime <= GRACE_PERIOD) return 0.0f;

        return -PENALTY_PER_SECOND * dt;
    }
};

class BackboardDefenseReward : public Reward {
private:
    mutable std::map<uint32_t, float> lastSoftTouchTime;
    mutable std::map<uint32_t, Vec>   lastSoftTouchPos;

    // config
    const float BACKBOARD_Y_MARGIN = 500.0f;   // how close to back wall to consider "on backboard"
    const float MIN_HEIGHT = 500.0f;           // z threshold to consider high backboard
    const float SCORING_ZONE_DIST = 3000.0f;   // within this distance to goal we care more
    const float STRONG_CLEAR_SPEED = 900.0f;   // min resulting ball speed for "strong" clear
    const float STRONG_CLEAR_ALIGN = 0.6f;     // alignment of ball vel -> goal for strong clear
    const float SOFT_TOUCH_SPEED_DROP = 300.0f;// speed drop threshold to consider a soft touch
    const float SOFT_TOUCH_NEAR = 400.0f;      // distance from car for soft-touch detection
    const float FOLLOWUP_TIMEOUT = 2.0f;       // seconds to reward follow-up aerial
    const float MAX_CONT_SHAPE = 6.0f;         // cap for continuous shaping per frame

public:
    virtual void Reset(const GameState& initialState) override {
        lastSoftTouchTime.clear();
        lastSoftTouchPos.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Only meaningful on touches or when state.prev exists for shaping
        float reward = 0.0f;

        // Identify own goal
        Vec myGoal = (player.team == Team::BLUE) ? CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

        // Continuous shaping: reward increases in distance of ball -> our goal (clearing away)
        if (state.prev) {
            float prevDist = (state.prev->ball.pos - myGoal).Length();
            float curDist = (state.ball.pos - myGoal).Length();

            float distDelta = curDist - prevDist; // positive = ball moved away from our goal
            if (distDelta > 0.0f) {
                // scale by closeness to goal (more reward when ball was near)
                float prox = RS_CLAMP(1.0f - (prevDist / SCORING_ZONE_DIST), 0.0f, 1.0f);
                // small shaping reward, capped
                float shape = RS_CLAMP(distDelta * 0.0025f * (0.5f + 0.5f * prox), 0.0f, MAX_CONT_SHAPE);
                reward += shape;
            }
        }

        // Only evaluate touch-based discrete rewards on touch frames
        if (!player.ballTouchedStep) return reward;

        // Ball is high on our defensive backboard?
        float distToBackWall = abs(abs(state.ball.pos.y) - CommonValues::BACK_WALL_Y);
        bool onDefBackboard = (distToBackWall < BACKBOARD_Y_MARGIN && state.ball.pos.z > MIN_HEIGHT);

        // ---------- Strong clear to side/corner ----------
        if (onDefBackboard) {
            // reward strong clears away from backboard toward side corners
            float speed = state.ball.vel.Length();
            if (speed > STRONG_CLEAR_SPEED && state.prev) {
                Vec targetCornerLeft = Vec(-CommonValues::SIDE_WALL_X, myGoal.y + 2000.0f, 0);
                Vec targetCornerRight = Vec(CommonValues::SIDE_WALL_X, myGoal.y + 2000.0f, 0);
                Vec toLeft = (targetCornerLeft - state.ball.pos).Normalized();
                Vec toRight = (targetCornerRight - state.ball.pos).Normalized();
                Vec velDir = state.ball.vel.Normalized();

                float alignL = velDir.Dot(toLeft);
                float alignR = velDir.Dot(toRight);
                float bestAlign = RS_MAX(alignL, alignR);

                // Prefer sideways component too (less straight out the field)
                float sideways = fabsf(velDir.x);

                if (bestAlign > STRONG_CLEAR_ALIGN || sideways > 0.6f) {
                    // Strong clear to side — reward more when it increases distance from goal
                    float prevDist = (state.prev->ball.pos - myGoal).Length();
                    float curDist = (state.ball.pos - myGoal).Length();
                    float distGain = RS_MAX(0.0f, curDist - prevDist);

                    // base for strong clear, scaled by distGain and speed
                    float strongBase = 8.0f + RS_CLAMP(speed / 600.0f, 0.0f, 6.0f);
                    float strongBonus = strongBase * RS_CLAMP(distGain / 1000.0f, 0.0f, 1.0f);

                    reward += RS_CLAMP(strongBonus, 0.0f, 20.0f);
                    return reward; // strong clear is decisive; avoid double-paying soft-touch logic
                }
            }
        }

        // ---------- Soft touch (possession-preserving) ----------
        // Detect a soft touch: touch that tends to slow the ball and leaves it near car.
        if (state.prev) {
            float prevSpeed = state.prev->ball.vel.Length();
            float curSpeed = state.ball.vel.Length();
            float speedDrop = prevSpeed - curSpeed;

            float distToBall = (state.ball.pos - player.pos).Length();

            if (speedDrop > SOFT_TOUCH_SPEED_DROP && distToBall < SOFT_TOUCH_NEAR) {
                // Register soft touch and pay small immediate reward (encourages possession)
                lastSoftTouchTime[player.carId] = (state.prev) ? ((state.prev->deltaTime > 0.0f) ? 0.0f : 0.0f) : 0.0f;
                // store absolute clock-like timestamp using a simple frame-accumulator in map
                // there's no global time here, so use lastSoftTouchTime as seconds relative by accumulating delta
                // We'll store current frame time by reusing a negative marker then increment below. Simpler:
                lastSoftTouchTime[player.carId] = 0.0f; // reset counter stored in map; we'll increment it next frames
                lastSoftTouchPos[player.carId] = state.ball.pos;

                // immediate soft-touch reward
                reward += 6.0f;

                // set an initial small bonus for maintaining possession near goal
                return reward;
            }
        }

        // ---------- Follow-up aerial after a registered soft touch ----------
        // We maintain lastSoftTouchTime as "age" in seconds — increment ages each non-touch call.
        // Because we can't reliably store a global time inside this method without global state,
        // we'll approximate ages by using delta stored on next calls: increment all ages here.
        for (auto& kv : lastSoftTouchTime) {
            // if value == 0.0f that means it was just registered; convert to small epsilon
            if (kv.second == 0.0f) kv.second = state.deltaTime;
            else kv.second += state.deltaTime;
        }

        // now check follow-up: if player touches in air and had a recent soft-touch
        bool isAerialTouch = !player.isOnGround && player.ballTouchedStep;
        if (isAerialTouch) {
            float age = lastSoftTouchTime[player.carId];
            if (age > 0.0f && age <= FOLLOWUP_TIMEOUT) {
                Vec softPos = lastSoftTouchPos[player.carId];
                float followDist = (state.ball.pos - softPos).Length();
                // require follow-up to be reasonably close to the original soft touch area
                if (followDist < 2500.0f) {
                    // reward follow-up aerial (encourages setup → aerial / airdribble)
                    reward += 12.0f;

                    // clear stored soft touch data
                    lastSoftTouchTime[player.carId] = -1.0f;
                    lastSoftTouchPos[player.carId] = Vec(0, 0, 0);
                    return reward;
                }
            }
        }

        // If nothing else, return accumulated shaping + any small immediate touch reward
        return reward;
    }
};

// Fake challenge - not committing to challenge
class FakeChallengeReward : public Reward {
private:
    mutable std::map<uint32_t, float> lastDistToBall;
    mutable std::map<uint32_t, bool> wasApproaching;

public:
    virtual void Reset(const GameState& initialState) override {
        lastDistToBall.clear();
        wasApproaching.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float curDist = (player.pos - state.ball.pos).Length();
        float prevDist = lastDistToBall[player.carId];
        lastDistToBall[player.carId] = curDist;
        
        if (prevDist == 0.0f) return 0.0f;
        
        bool isApproaching = (curDist < prevDist);
        bool prevApproaching = wasApproaching[player.carId];
        wasApproaching[player.carId] = isApproaching;
        
        // Fake = was approaching, then retreated without touching
        if (prevApproaching && !isApproaching && !player.ballTouchedStep && curDist < 1000.0f) {
            // Check if opponent is nearby (fake worked)
            for (auto& opp : state.players) {
                if (opp.team == player.team) continue;
                
                float oppDist = (opp.pos - state.ball.pos).Length();
                if (oppDist < 800.0f) {
                    // Opponent was close, we faked them out
                    return 8.0f;
                }
            }
        }
        
        return 0.0f;
    }
};

// OutplayReward
// - Rewards a touch that gets the ball past the closest opponent toward the opponent goal.
// - Uses distance-to-goal crossing (previous frame vs current) with margins and minimal speed/alignment checks.
// - Provides simple thread-safe metrics via GetAndResetOutplayMetrics().
class OutplayReward : public Reward {
private:
    const float MIN_BALL_SPEED = 200.0f;   // require some meaningful ball speed after touch
    const float DIST_MARGIN = 50.0f;       // margin to avoid tiny flips being rewarded
    const float BASE_REWARD = 12.0f;       // base reward, scaled by how far past opponent we got

public:
    struct OutplayMetrics {
        int attempts = 0;
        int successes = 0;
        double rewardSum = 0.0;
    };

    inline static std::atomic<int> s_attempts{ 0 };
    inline static std::atomic<int> s_successes{ 0 };
    inline static std::atomic<long long> s_rewardMilli{ 0 };

    static OutplayMetrics GetAndResetOutplayMetrics() {
        OutplayMetrics m;
        m.attempts = s_attempts.exchange(0);
        m.successes = s_successes.exchange(0);
        long long r = s_rewardMilli.exchange(0);
        m.rewardSum = double(r) / 1000.0;
        return m;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // require touch and previous state
        if (!player.ballTouchedStep || !state.prev) return 0.0f;

        // find closest opponent
        const Player* closestOpp = nullptr;
        float minOppDist = FLT_MAX;
        for (const auto& p : state.players) {
            if (p.team == player.team) continue;
            float d = (p.pos - state.ball.pos).Length();
            if (d < minOppDist) {
                minOppDist = d;
                closestOpp = &p;
            }
        }
        if (!closestOpp) return 0.0f;

        s_attempts.fetch_add(1);

        // target = opponent goal (the goal we are attacking)
        Vec targetGoal = (player.team == Team::BLUE) ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        // distances to goal
        float oppDist = (closestOpp->pos - targetGoal).Length();
        float prevBallDist = (state.prev->ball.pos - targetGoal).Length();
        float curBallDist = (state.ball.pos - targetGoal).Length();

        // require meaningful speed and roughly toward-goal motion
        float ballSpeed = state.ball.vel.Length();
        if (ballSpeed < MIN_BALL_SPEED) return 0.0f;

        Vec toGoalNow = (targetGoal - state.ball.pos);
        float toGoalLen = toGoalNow.Length();
        if (toGoalLen < 1e-4f) return 0.0f;
        float align = state.ball.vel.Normalized().Dot(toGoalNow / toGoalLen);
        if (align < 0.12f) return 0.0f; // not meaningfully toward the goal

        // OUTPLAY condition: ball crosses from behind (or near) opponent to past them toward goal
        // strict crossing with margin to avoid farming tiny changes
        if (prevBallDist >= oppDist + DIST_MARGIN && curBallDist <= oppDist - DIST_MARGIN) {
            // scale reward by how much closer to goal we are relative to opponent (clamped)
            float gap = RS_MAX(0.0f, oppDist - curBallDist);
            float scale = RS_CLAMP(gap / RS_MAX(1.0f, oppDist), 0.0f, 1.0f);
            float reward = BASE_REWARD * scale;

            s_successes.fetch_add(1);
            s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));
            return reward;
        }

        return 0.0f;
    }
};

// ============================================================================
// CEILING SHOT REWARD (Unified - Rev 2.0)
// ============================================================================
// Comprehensive reward for ceiling shot mechanics with continuous gradient signals.
//
// Three-stage progression:
// 1. Setup (0-1.0): Ball apex near ceiling, player jumps toward ball
// 2. Contact & Alignment (1.0-3.0): True ceiling contact, velocity matching, positioning
// 3. Execution (3.0-5.0+): Flip execution with goal direction and speed bonuses
//
// Key improvements over original two-reward system:
// - Uses actual contact normal for ceiling detection (not just position)
// - Validates velocity alignment (both direction AND magnitude)
// - Enforces proximity constraints (±400 UU in all directions)
// - Provides continuous graduated rewards for smooth learning
// - Includes explicit metrics for state detection verification
// ============================================================================
class CeilingShotReward : public Reward {
private:
    // Stage tracking (per player)
    mutable std::map<uint32_t, float> stageProgress;           // Current cumulative reward
    mutable std::map<uint32_t, bool>  completeSetupValid;      // Did setup complete?
    mutable std::map<uint32_t, bool>  onCeilingPreviousFrame;  // Ceiling state tracking

    // Explicit metrics for verification (per player)
    mutable std::map<uint32_t, uint64_t> truelyOnCeilingFrameCount;    // Frames with actual contact
    mutable std::map<uint32_t, uint64_t> onCeilingWithBallNearbyCount; // Frames ceiling + ball in range

    // ========================================================================
    // HELPER METHODS
    // ========================================================================

    bool IsTruelyOnCeiling(const Player& player) const {
        // Verify actual wheel contact via surface normal, not just position
        return player.worldContact.hasContact &&
               player.worldContact.contactNormal.z < -0.5f &&
               !player.isOnGround;
    }

    bool IsBallProperlyPositioned(const Player& player, const GameState& state) const {
        // Ball must be within 400 UU in ALL directions for proper ceiling shot setup
        float horizDist = GetHorizontalDistance(player, state);
        float vertDist = GetVerticalDistance(player, state);
        return horizDist <= 400.0f && fabs(vertDist) <= 400.0f;
    }

    float GetHorizontalDistance(const Player& player, const GameState& state) const {
        // Distance in x-y plane (ground projection)
        float dx = player.pos.x - state.ball.pos.x;
        float dy = player.pos.y - state.ball.pos.y;
        return sqrt(dx * dx + dy * dy);
    }

    float GetVerticalDistance(const Player& player, const GameState& state) const {
        // Positive = player below ball, negative = player above ball
        return player.pos.z - state.ball.pos.z;
    }

    float GetVelocityAlignment(const Player& player, const GameState& state) const {
        // CRITICAL: Check BOTH direction AND magnitude for proper ceiling shot
        // Direction: normalized dot product (are we falling in same direction?)
        Vec ballVelDir = state.ball.vel.Normalized();
        Vec playerVelDir = player.vel.Normalized();
        float directionAlignment = playerVelDir.Dot(ballVelDir);  // 1.0 = perfect match

        // Magnitude: speed ratio similarity (are we falling at similar speed?)
        float ballSpeed = state.ball.vel.Length();
        float playerSpeed = player.vel.Length();
        float speedRatio = (ballSpeed > 0.1f) ? playerSpeed / ballSpeed : 1.0f;
        float magnitudeSimilarity = 1.0f - RS_CLAMP(fabs(1.0f - speedRatio), 0.0f, 1.0f);

        // Average both: ready to flip only when BOTH are >= 0.8
        return (directionAlignment + magnitudeSimilarity) * 0.5f;
    }

    bool IsOnWall(const Player& player) const {
        // Player is on/near wall if close to SIDE_WALL_X or BACK_WALL_Y
        float distX = fabs(fabs(player.pos.x) - CommonValues::SIDE_WALL_X);
        float distY = fabs(fabs(player.pos.y) - CommonValues::BACK_WALL_Y);
        const float WALL_THRESHOLD = 500.0f;
        return (distX < WALL_THRESHOLD || distY < WALL_THRESHOLD);
    }

    bool IsBallElevated(const GameState& state) const {
        // Ball apex near ceiling (within 200 UU below ceiling)
        return state.ball.pos.z > (CommonValues::CEILING_Z - 200.0f);
    }

    bool IsPlayerMovingTowardBall(const Player& player, const GameState& state) const {
        // After wall hit, player should jump/boost toward ball
        Vec toBall = (state.ball.pos - player.pos).Normalized();
        float velocityTowardBall = player.vel.Dot(toBall);
        return velocityTowardBall > 500.0f;  // Significant movement toward ball
    }

    // ========================================================================
    // STAGE EVALUATORS
    // ========================================================================

    float EvaluateSetup(const Player& player, const GameState& state) const {
        // STAGE 1 (0-1.0): Setup completion
        // - Player on wall
        // - Ball elevated near ceiling
        // - Player jumping/boosting toward ball

        if (!IsOnWall(player) || !IsBallElevated(state)) {
            return 0.0f;
        }

        // Reward scales based on movement toward ball
        if (IsPlayerMovingTowardBall(player, state)) {
            return 1.0f;  // Setup complete
        }

        // Partial reward for being in position even if not moving toward yet
        return 0.3f;
    }

    float EvaluateContact(const Player& player, const GameState& state) const {
        // STAGE 2 (1.0-3.0): Contact and alignment
        // - True ceiling contact via wheel contact
        // - Ball proximity (±400 UU)
        // - Velocity alignment (direction + magnitude)

        if (!IsTruelyOnCeiling(player)) {
            return 0.0f;  // Must have true contact
        }

        float contactReward = 1.0f;  // Base for being on ceiling

        // Check ball proximity
        if (IsBallProperlyPositioned(player, state)) {
            contactReward += 0.5f;  // Proximity bonus
        }

        // Check velocity alignment
        float alignmentScore = GetVelocityAlignment(player, state);
        if (alignmentScore > 0.8f) {
            // Ready to flip: both direction and magnitude aligned
            contactReward += RS_CLAMP(alignmentScore - 0.8f, 0.0f, 1.0f);  // Scale 0-1.0 for alignment
        }

        // Bonus if flip is still available (not used yet)
        if (player.HasFlipOrJump()) {
            contactReward += 0.3f;
        }

        return RS_CLAMP(contactReward, 1.0f, 3.0f);  // Clamp to stage 2 range
    }

    float EvaluateExecution(const Player& player, const GameState& state) const {
        // STAGE 3 (3.0-5.0+): Execution and goal direction
        // - Flip was used (!HasFlipOrJump)
        // - Ball touch verified
        // - Ball directed toward opponent goal

        if (player.isOnGround || !player.ballTouchedStep) {
            return 0.0f;  // Must be in air and just touched ball
        }

        float executionReward = 3.0f;  // Base for being in execution phase

        // Bonus for flip usage
        if (!player.HasFlipOrJump()) {
            executionReward += 1.5f;  // Flip was used
        }

        // Check goal direction
        Vec targetGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        Vec ballToGoal = (targetGoal - state.ball.pos).Normalized();
        float goalAlignment = state.ball.vel.Normalized().Dot(ballToGoal);

        if (goalAlignment > 0.6f) {
            // Shot is on target toward goal
            executionReward += 0.5f;

            // Extra bonus for excellent alignment
            if (goalAlignment > 0.85f) {
                executionReward += 0.5f;
            }
        }

        // Bonuses for ball speed
        float ballSpeed = state.ball.vel.Length();
        if (ballSpeed > 1500.0f) {
            executionReward += 0.25f;
        }
        if (ballSpeed > 2000.0f) {
            executionReward += 0.25f;
        }

        return executionReward;
    }

public:
    virtual void Reset(const GameState& initialState) override {
        stageProgress.clear();
        completeSetupValid.clear();
        onCeilingPreviousFrame.clear();
        truelyOnCeilingFrameCount.clear();
        onCeilingWithBallNearbyCount.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        uint32_t id = player.carId;

        // Initialize tracking for new player
        if (stageProgress.find(id) == stageProgress.end()) {
            stageProgress[id] = 0.0f;
            completeSetupValid[id] = false;
            onCeilingPreviousFrame[id] = false;
        }

        // Track explicit metrics for state detection verification
        if (IsTruelyOnCeiling(player)) {
            truelyOnCeilingFrameCount[id]++;

            if (IsBallProperlyPositioned(player, state)) {
                onCeilingWithBallNearbyCount[id]++;
            }
        }

        float reward = 0.0f;

        // Evaluate each stage and accumulate rewards
        float setupReward = EvaluateSetup(player, state);
        if (setupReward > 0.0f) {
            completeSetupValid[id] = true;
        }

        // Only advance through stages if previous conditions are met
        if (completeSetupValid[id]) {
            float contactReward = EvaluateContact(player, state);
            float executionReward = 0.0f;

            // Only allow execution if contact is established
            if (contactReward > 1.0f) {
                executionReward = EvaluateExecution(player, state);
            }

            // Use highest-stage reward achieved (continuous accumulation)
            if (executionReward > 0.0f) {
                reward = executionReward;
            } else if (contactReward > 0.0f) {
                reward = contactReward;
            } else if (setupReward > 0.0f) {
                reward = setupReward;
            }
        } else {
            // Still trying to set up
            reward = setupReward;
        }

        // Reset tracking if player lands or falls off ceiling
        if (player.isOnGround) {
            completeSetupValid[id] = false;
            stageProgress[id] = 0.0f;
        }

        onCeilingPreviousFrame[id] = IsTruelyOnCeiling(player);

        return reward;
    }

    // Metrics accessors for monitoring training
    uint64_t GetTruelyOnCeilingFrames(const Player& player) const {
        auto it = truelyOnCeilingFrameCount.find(player.carId);
        return (it != truelyOnCeilingFrameCount.end()) ? it->second : 0;
    }

    uint64_t GetCeilingWithBallNearbyFrames(const Player& player) const {
        auto it = onCeilingWithBallNearbyCount.find(player.carId);
        return (it != onCeilingWithBallNearbyCount.end()) ? it->second : 0;
    }
};

// 2025-12-20 | Rev 4.0 | NJH
// - MASSIVELY increased acceleration sensitivity
// - Added directional bonuses (upward + forward acceleration)
// - Rewards "saucy" flicks with high combined acceleration vectors
class FlickReward : public Reward {
private:
    mutable std::map<uint32_t, float> ballOnCarTime;
    float minOnCarTime = 0.25f;

    // Geometry gates
    float minHeightAboveCar = 40.0f;
    float maxHeightAboveCar = 155.0f;
    float maxHorizDist = 120.0f;

    // Acceleration-to-reward mapping (uu/s^2) - MUCH MORE AGGRESSIVE
    float minAccel = 15000.0f;      // Lower threshold (was 20k)
    float goodAccel = 80000.0f;     // Good flick
    float greatAccel = 140000.0f;   // Great flick (was fullAccel 120k)
    float insaneAccel = 200000.0f;  // Insane flick

    float maxFlickReward = 25.0f;   // Increased from 15.0f

public:
    virtual void Reset(const GameState& initialState) override {
        ballOnCarTime.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

        Vec relBallPos = state.ball.pos - player.pos;
        float heightAboveCar = relBallPos.z;
        float horizDist = Vec(relBallPos.x, relBallPos.y, 0).Length();

        bool ballOnCar =
            (heightAboveCar > minHeightAboveCar && heightAboveCar < maxHeightAboveCar && horizDist < maxHorizDist);

        auto& tOnCar = ballOnCarTime[player.carId];
        if (ballOnCar && player.isOnGround) {
            tOnCar += dt;
        }
        else {
            if (!ballOnCar) tOnCar = 0.0f;
        }

        // Flick trigger
        if (player.ballTouchedStep && !ballOnCar && tOnCar >= minOnCarTime) {
            if (!state.prev) { tOnCar = 0.0f; return 0.0f; }

            // Calculate acceleration vector
            Vec dv = state.ball.vel - state.prev->ball.vel;
            float totalAccel = dv.Length() / RS_MAX(dt, 1e-3f);

            // Decompose acceleration into components
            Vec carForward = player.rotMat.forward;
            Vec upVector = Vec(0, 0, 1);

            float forwardAccel = dv.Dot(carForward) / RS_MAX(dt, 1e-3f);
            float upwardAccel = dv.Dot(upVector) / RS_MAX(dt, 1e-3f);

            // Base acceleration reward (progressive scaling)
            float accelReward = 0.0f;
            if (totalAccel >= insaneAccel) {
                accelReward = maxFlickReward; // Full reward for insane flicks
            }
            else if (totalAccel >= greatAccel) {
                float t = (totalAccel - greatAccel) / (insaneAccel - greatAccel);
                accelReward = maxFlickReward * (0.75f + 0.25f * t); // 75-100%
            }
            else if (totalAccel >= goodAccel) {
                float t = (totalAccel - goodAccel) / (greatAccel - goodAccel);
                accelReward = maxFlickReward * (0.45f + 0.30f * t); // 45-75%
            }
            else if (totalAccel >= minAccel) {
                float t = (totalAccel - minAccel) / (goodAccel - minAccel);
                accelReward = maxFlickReward * (0.15f + 0.30f * t); // 15-45%
            }

            // SAUCINESS MULTIPLIERS
            float sauceMultiplier = 1.0f;

            // Forward acceleration bonus (power flicks)
            if (forwardAccel > 50000.0f) {
                float forwardBonus = RS_CLAMP((forwardAccel - 50000.0f) / 80000.0f, 0.0f, 0.4f);
                sauceMultiplier += forwardBonus; // Up to +40%
            }

            // Upward acceleration bonus (pop flicks)
            if (upwardAccel > 40000.0f) {
                float upwardBonus = RS_CLAMP((upwardAccel - 40000.0f) / 60000.0f, 0.0f, 0.35f);
                sauceMultiplier += upwardBonus; // Up to +35%
            }

            // Combined directional bonus (diagonal power)
            if (forwardAccel > 40000.0f && upwardAccel > 30000.0f) {
                sauceMultiplier += 0.3f; // +30% for combined power
            }

            float reward = accelReward * sauceMultiplier;

            // Speed gate (require actual speed increase)
            float prevSpeed = state.prev->ball.vel.Length();
            float curSpeed = state.ball.vel.Length();
            float dSpeed = curSpeed - prevSpeed;
            float speedGate = RS_CLAMP(dSpeed / 600.0f, 0.0f, 1.0f); // Lowered threshold (was 800)

            reward *= (0.25f + 0.75f * speedGate); // Less harsh gating (was 0.35 + 0.65)

            tOnCar = 0.0f;
            return reward;
        }

        // Dribble sustain shaping (minimal)
        if (ballOnCar && player.isOnGround && tOnCar > 0.05f) {
            return 0.12f; // Slightly reduced (was 0.15)
        }

        return 0.0f;
    }
};

// 2025-01-04 | Advanced Flick Reward | NJH
// - Targets high-speed flicks (>2200 UU/s, ideally 2500+)
// - Rewards 45° flicks (diagonal), 90° flicks (sideways), and mixed angles
// - Heavy emphasis on ball speed and directional control
// - Penalizes low-speed flicks to force aggressive play

class AdvancedFlickReward : public Reward {
private:
    mutable std::map<uint32_t, float> ballOnCarTime;
    float minOnCarTime = 0.25f;

    // Geometry gates
    float minHeightAboveCar = 40.0f;
    float maxHeightAboveCar = 155.0f;
    float maxHorizDist = 120.0f;

    // Speed thresholds (UU/s)
    float minFlickSpeed = 1600.0f;      // Minimum viable flick
    float goodFlickSpeed = 2200.0f;     // Good flick target
    float greatFlickSpeed = 2500.0f;    // Great flick
    float insaneFlickSpeed = 3000.0f;   // Insane flick

    float maxFlickReward = 35.0f;       // Increased for high-speed emphasis

    // Angle targets (degrees from forward)
    // 45° = diagonal, 90° = sideways, 135° = back-diagonal
    float angleTolerance = 30.0f;       // ±30° tolerance around target angles

public:
    virtual void Reset(const GameState& initialState) override {
        ballOnCarTime.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

        Vec relBallPos = state.ball.pos - player.pos;
        float heightAboveCar = relBallPos.z;
        float horizDist = Vec(relBallPos.x, relBallPos.y, 0).Length();

        bool ballOnCar =
            (heightAboveCar > minHeightAboveCar && heightAboveCar < maxHeightAboveCar && horizDist < maxHorizDist);

        auto& tOnCar = ballOnCarTime[player.carId];
        if (ballOnCar && player.isOnGround) {
            tOnCar += dt;
        }
        else {
            if (!ballOnCar) tOnCar = 0.0f;
        }

        // Flick trigger
        if (player.ballTouchedStep && !ballOnCar && tOnCar >= minOnCarTime) {
            if (!state.prev) { tOnCar = 0.0f; return 0.0f; }

            // Ball velocity AFTER flick
            Vec ballVel = state.ball.vel;
            float ballSpeed = ballVel.Length();

            // ACCELERATION: Calculate velocity change imparted to ball (vector magnitude, not scalar diff)
            // Scalar (ballSpeed - prevBallSpeed) = 0 for side flicks at constant speed — use |Δvel| instead
            Vec dv = state.ball.vel - state.prev->ball.vel;
            float velocityChange = dv.Length();

            // SPEED GATE: Reward based on acceleration, not absolute final speed
            if (velocityChange < 200.0f) {
                // Weak flick - minimal acceleration
                tOnCar = 0.0f;
                return 0.0f; // No penalty, just no reward for weak acceleration
            }

            // Acceleration-based reward (prioritize how much speed was ADDED)
            float speedReward = 0.0f;
            const float minAccel = 200.0f;     // Minimum acceleration to reward
            const float goodAccel = 1000.0f;   // Good acceleration
            const float greatAccel = 1500.0f;  // Great acceleration
            const float insaneAccel = 2000.0f; // Insane acceleration

            if (velocityChange >= insaneAccel) {
                speedReward = maxFlickReward;
            }
            else if (velocityChange >= greatAccel) {
                float t = (velocityChange - greatAccel) / (insaneAccel - greatAccel);
                speedReward = maxFlickReward * (0.70f + 0.30f * t); // 70-100%
            }
            else if (velocityChange >= goodAccel) {
                float t = (velocityChange - goodAccel) / (greatAccel - goodAccel);
                speedReward = maxFlickReward * (0.45f + 0.25f * t); // 45-70%
            }
            else {
                // minAccel to goodAccel
                float t = (velocityChange - minAccel) / (goodAccel - minAccel);
                speedReward = maxFlickReward * (0.20f + 0.25f * t); // 20-45%
            }

            // DIRECTIONAL ANALYSIS
            Vec carForward = player.rotMat.forward;
            Vec carRight = player.rotMat.right;
            Vec upVector = Vec(0, 0, 1);

            // Normalize ball velocity direction
            Vec ballDir = (ballSpeed > 0.1f) ? ballVel / ballSpeed : carForward;

            // Calculate angles from car orientation
            float forwardComponent = ballDir.Dot(carForward);
            float rightComponent = ballDir.Dot(carRight);
            float upComponent = ballDir.Dot(upVector);

            // Angle from forward (0° = straight, 90° = sideways, 180° = backward)
            float horizontalAngle = std::atan2(rightComponent, forwardComponent) * 180.0f / 3.14159f;
            float verticalAngle = std::atan2(upComponent,
                std::sqrt(forwardComponent * forwardComponent + rightComponent * rightComponent)) * 180.0f / 3.14159f;

            // TARGET ANGLE REWARDS
            // Target angles: 45° (diagonal), 90° (sideways), 135° (back-diagonal)
            float angleBonus = 0.0f;

            auto CheckAngle = [&](float targetAngle) -> float {
                float diff = std::abs(std::abs(horizontalAngle) - std::abs(targetAngle));
                if (diff > 180.0f) diff = 360.0f - diff;

                if (diff < angleTolerance) {
                    return 1.0f - (diff / angleTolerance) * 0.3f; // 100% to 70%
                }
                return 0.0f;
                };

            // Reward for hitting target angles
            float angle45 = CheckAngle(45.0f);      // Diagonal
            float angle90 = CheckAngle(90.0f);      // Sideways
            float angle135 = CheckAngle(135.0f);    // Back-diagonal

            angleBonus = RS_MAX(RS_MAX(angle45, angle90), angle135);

            // Upward angle bonus (pop flicks)
            if (upComponent > 0.3f) {
                float popBonus = RS_CLAMP(upComponent, 0.0f, 1.0f) * 0.25f;
                angleBonus += popBonus; // Up to +25%
            }

            // Apply angle bonus to speed reward
            float directionMultiplier = 1.0f + angleBonus; // 1.0x to 1.25x+

            float reward = speedReward * directionMultiplier;

            // GOAL DIRECTION MULTIPLIER
            // Slightly higher scale than DribbleFlickRewards (0.30f) since AdvancedFlick targets harder shots
            Vec oppGoalPos = (player.team == Team::BLUE)
                ? Vec(0.0f, CommonValues::BACK_WALL_Y, CommonValues::BALL_RADIUS)
                : Vec(0.0f, -CommonValues::BACK_WALL_Y, CommonValues::BALL_RADIUS);
            Vec toGoal = (oppGoalPos - state.ball.pos).Normalized();
            float goalAlign = RS_MAX(0.0f, ballDir.Dot(toGoal));
            const float GOAL_DIR_SCALE = 0.40f;
            reward *= (1.0f + GOAL_DIR_SCALE * goalAlign);

            // OPPONENT EVASION MULTIPLIER
            // Rewards flicking ball away from opponents — the harder they are to reach it, the better
            float totalEvasion = 0.0f;
            int numOpps = 0;
            for (const auto& opp : state.players) {
                if (opp.team == player.team || opp.isDemoed) continue;
                Vec ballToOpp = (opp.pos - state.ball.pos).Normalized();
                float evasion = RS_MAX(0.0f, -ballDir.Dot(ballToOpp)); // positive = ball moving away from opp
                totalEvasion += evasion;
                numOpps++;
            }
            if (numOpps > 0) {
                float avgEvasion = totalEvasion / numOpps;
                const float EVASION_SCALE = 0.35f;
                reward *= (1.0f + EVASION_SCALE * avgEvasion);
            }

            // Consistency bonus (if ball is moving cleanly without spin)
            float angVelMag = state.ball.angVel.Length();
            if (angVelMag < 2.0f) {
                reward *= 1.15f; // +15% for clean flicks
            }

            tOnCar = 0.0f;
            return reward;
        }

        // Dribble sustain (minimal)
        if (ballOnCar && player.isOnGround && tOnCar > 0.05f) {
            return 0.05f; // Small reward to encourage ball possession
        }

        return 0.0f;
    }
};

// AirDribbleReward (TUNED: more permissive, height-arc aware)
// - Relaxed directional requirements to allow more varied air dribbles
// - Added height progression bonuses (arc-style air dribbles get multipliers)
// - Distinguishes "air drags" (flat) from "air arcs" (upward trajectory)
// - Softer decay with entry/second-touch bonuses
class AirDribbleReward : public Reward {
private:
    mutable std::map<uint32_t, int> consecutiveAirTouches;
    float minBallHeight = 250.0f;

    // Per-sequence state to limit farming
    mutable std::map<uint32_t, float> seqAccumReward;
    mutable std::map<uint32_t, float> seqTime;
    mutable std::map<uint32_t, int>   seqTouchCount;
    mutable std::map<uint32_t, bool>  seqExhausted;

    // Height tracking for arc detection
    mutable std::map<uint32_t, float> seqStartHeight;
    mutable std::map<uint32_t, float> seqMaxHeight;
    mutable std::map<uint32_t, bool>  heightBonusApplied;

    // ============================================================================
    // TUNABLE CONSTANTS - Adjust these to change reward behavior
    // ============================================================================

    // --- CORE REWARD SCALING ---
    const float PROGRESS_SCALE = 0.013f;
    // What it does: Reward per unit of progress toward goal (uu/s)
    // More aggressive: Increase to 0.015-0.020 for higher per-touch rewards
    // More conservative: Decrease to 0.008-0.010 for lower rewards

    const float PER_TOUCH_MAX = 10.0f;
    // What it does: Maximum reward a single touch can yield
    // More aggressive: Increase to 12.0-15.0 to allow bigger single-touch payouts
    // More conservative: Decrease to 7.0-8.0 to cap individual touches lower

    const float SEQ_REWARD_CAP = 80.0f;
    // What it does: Total maximum reward for an entire air dribble sequence
    // More aggressive: Increase to 60.0-80.0 for longer, more rewarding sequences
    // More conservative: Decrease to 35.0-40.0 to limit total sequence value

    const float MAX_SEQ_DURATION = 12.0f;
    // What it does: Maximum time (seconds) a sequence can last before exhausting
    // More aggressive: Increase to 12.0-15.0 for longer allowable sequences
    // More conservative: Decrease to 7.0-8.0 to force quicker completions

    const int MAX_TOUCHES = 50;
    // What it does: Maximum number of touches before sequence exhausts
    // More aggressive: Increase to 40-50 for more touch allowance
    // More conservative: Decrease to 20-25 to limit farming

    const float MIN_PROGRESS_UU = 8.0f;
    // What it does: Minimum forward progress required to award anything
    // More aggressive: Decrease to 8.0-10.0 to reward smaller progress
    // More conservative: Increase to 15.0-20.0 to require more meaningful progress

    // --- DECAY CURVE ---
    const float DECAY_EXPONENT = 0.25f;
    // What it does: Controls how quickly rewards diminish (0-1 range, lower = softer)
    // More aggressive: Decrease to 0.20-0.25 for even softer early decay
    // More conservative: Increase to 0.45-0.60 for steeper diminishing returns

    // --- BONUS REWARDS ---
    const float ENTRY_BONUS = 1.0f;
    // What it does: One-time bonus when first qualifying touch occurs
    // More aggressive: Increase to 1.0-1.5 to encourage starting sequences
    // More conservative: Decrease to 0.3-0.4 for minimal starting bonus

    const float SECOND_TOUCH_BONUS = 1.2f;
    // What it does: Bonus when second qualifying touch occurs
    // More aggressive: Increase to 1.2-1.5 to encourage continuation
    // More conservative: Decrease to 0.4-0.6 for minimal continuation bonus

    // --- DIRECTIONAL REQUIREMENTS ---
    const float MIN_DIR_MULTIPLIER = 0.3f;
    // What it does: Minimum directional alignment multiplier (was stricter)
    // More aggressive: Decrease to 0.2-0.3 to allow more off-angle dribbles
    // More conservative: Increase to 0.5-0.7 to require better goal alignment

    const float GOOD_DIR_THRESHOLD = 0.5f;
    // What it does: Alignment threshold for "good" direction (dot product)
    // More aggressive: Decrease to 0.3-0.4 to be more lenient
    // More conservative: Increase to 0.6-0.75 to require tighter alignment

    // --- HEIGHT ARC BONUSES ---
    const float HEIGHT_BONUS_THRESHOLD_1 = 250.0f;
    // What it does: Height gain (uu) required for first height bonus multiplier
    // More aggressive: Decrease to 250.0-300.0 for easier threshold
    // More conservative: Increase to 400.0-500.0 for harder threshold

    const float HEIGHT_BONUS_MULTIPLIER_1 = 1.7f;
    // What it does: Reward multiplier when HEIGHT_BONUS_THRESHOLD_1 is reached
    // More aggressive: Increase to 1.75-2.0 for bigger arc rewards
    // More conservative: Decrease to 1.3-1.4 for smaller bonuses

    const float HEIGHT_BONUS_THRESHOLD_2 = 600.0f;
    // What it does: Height gain (uu) required for second (larger) height bonus
    // More aggressive: Decrease to 550.0-650.0 for easier threshold
    // More conservative: Increase to 800.0-1000.0 for harder threshold

    const float HEIGHT_BONUS_MULTIPLIER_2 = 2.5f;
    // What it does: Reward multiplier when HEIGHT_BONUS_THRESHOLD_2 is reached
    // More aggressive: Increase to 2.5-3.0 for massive arc rewards
    // More conservative: Decrease to 1.75-1.85 for smaller bonuses

    // ============================================================================

    const float EPS_REWARD_SKIP = 1e-7f;

    float ComputeDribbleDirectionMultiplier(const Player& player, const GameState& state) const {
        Vec oppGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        Vec ballToGoal = (oppGoal - state.ball.pos).Normalized();
        Vec ballVelDir = state.ball.vel.Normalized();

        float alignment = ballVelDir.Dot(ballToGoal);

        // More lenient directional requirements
        // Allow more off-angle dribbles (air drags, setups, etc.)
        if (alignment < 0.0f) {
            // Moving away from goal - minimal reward
            return MIN_DIR_MULTIPLIER * 0.5f;
        }
        else if (alignment < GOOD_DIR_THRESHOLD) {
            // Somewhat toward goal - moderate reward
            float t = alignment / GOOD_DIR_THRESHOLD;
            return MIN_DIR_MULTIPLIER + (1.0f - MIN_DIR_MULTIPLIER) * t * 0.7f;
        }
        else {
            // Good alignment - full to bonus reward
            float t = (alignment - GOOD_DIR_THRESHOLD) / (1.0f - GOOD_DIR_THRESHOLD);
            return 0.7f + 0.3f * t; // 0.7 to 1.0
        }
    }

    float ComputeHeightBonus(uint32_t id, float currentHeight) {
        // Track maximum height reached
        if (seqMaxHeight.find(id) == seqMaxHeight.end() || currentHeight > seqMaxHeight[id]) {
            seqMaxHeight[id] = currentHeight;
        }

        float startHeight = seqStartHeight[id];
        float maxHeightGain = seqMaxHeight[id] - startHeight;

        // Check if we've reached height thresholds
        if (maxHeightGain >= HEIGHT_BONUS_THRESHOLD_2) {
            return HEIGHT_BONUS_MULTIPLIER_2; // Big arc bonus
        }
        else if (maxHeightGain >= HEIGHT_BONUS_THRESHOLD_1) {
            return HEIGHT_BONUS_MULTIPLIER_1; // Medium arc bonus
        }

        return 1.0f; // No height bonus (flat air drag)
    }

public:
    struct AirDribbleMetrics {
        int attempts = 0;
        int onTarget = 0;
        int arcDribbles = 0;        // NEW: Count of dribbles that achieved height bonus
        double rewardSum = 0.0;
    };

    inline static std::atomic<int> s_attempts{ 0 };
    inline static std::atomic<int> s_onTarget{ 0 };
    inline static std::atomic<int> s_arcDribbles{ 0 };  // NEW
    inline static std::atomic<long long> s_rewardMilli{ 0 };

    static AirDribbleMetrics GetAndResetAirDribbleMetrics() {
        AirDribbleMetrics m;
        m.attempts = s_attempts.exchange(0);
        m.onTarget = s_onTarget.exchange(0);
        m.arcDribbles = s_arcDribbles.exchange(0);  // NEW
        long long r = s_rewardMilli.exchange(0);
        m.rewardSum = double(r) / 1000.0;
        return m;
    }

    virtual void Reset(const GameState& initialState) override {
        consecutiveAirTouches.clear();
        seqAccumReward.clear();
        seqTime.clear();
        seqTouchCount.clear();
        seqExhausted.clear();
        seqStartHeight.clear();
        seqMaxHeight.clear();
        heightBonusApplied.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        uint32_t id = player.carId;
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

        // Reset sequence on grounded / invalid conditions
        if (player.isOnGround || state.ball.pos.z < minBallHeight || player.pos.z >= state.ball.pos.z) {
            consecutiveAirTouches[id] = 0;
            seqAccumReward.erase(id);
            seqTime.erase(id);
            seqTouchCount.erase(id);
            seqExhausted.erase(id);
            seqStartHeight.erase(id);
            seqMaxHeight.erase(id);
            heightBonusApplied.erase(id);
            return 0.0f;
        }

        if (!state.prev)
            return 0.0f;

        if (!player.ballTouchedStep)
            return 0.0f;

        // Initialize sequence state if absent
        if (seqTime.find(id) == seqTime.end()) {
            seqTime[id] = 0.0f;
            seqAccumReward[id] = 0.0f;
            seqTouchCount[id] = 0;
            seqExhausted[id] = false;
            consecutiveAirTouches[id] = 0;
            seqStartHeight[id] = state.ball.pos.z;
            seqMaxHeight[id] = state.ball.pos.z;
            heightBonusApplied[id] = false;
        }

        seqTime[id] += dt;

        if (seqExhausted[id]) {
            return 0.0f;
        }

        if (seqTime[id] > MAX_SEQ_DURATION || seqTouchCount[id] >= MAX_TOUCHES) {
            seqExhausted[id] = true;
            return 0.0f;
        }

        s_attempts.fetch_add(1);

        int& consTouches = consecutiveAirTouches[id];
        int prevTouches = consTouches;
        consTouches++;

        // Compute measurable progress
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        float prevDist = (state.prev->ball.pos - opponentGoal).Length();
        float curDist = (state.ball.pos - opponentGoal).Length();

        float progress = RS_MAX(0.0f, prevDist - curDist);

        if (progress < MIN_PROGRESS_UU) {
            return 0.0f;
        }

        // Base per-touch reward
        float perTouch = progress * PROGRESS_SCALE;
        perTouch = RS_MIN(PER_TOUCH_MAX, perTouch);

        // Softer diminishing returns
        float usedFraction = RS_CLAMP(seqAccumReward[id] / SEQ_REWARD_CAP, 0.0f, 1.0f);
        float remainingFraction = RS_MAX(0.0f, 1.0f - usedFraction);
        float decay = powf(remainingFraction, DECAY_EXPONENT);

        float remainingCap = RS_MAX(0.0f, SEQ_REWARD_CAP - seqAccumReward[id]);
        if (remainingCap <= 0.0f) {
            seqExhausted[id] = true;
            return 0.0f;
        }

        float rewardBeforeDir = RS_MIN(perTouch * decay, remainingCap);

        // MORE LENIENT directional multiplier
        float dirMul = ComputeDribbleDirectionMultiplier(player, state);

        float reward = rewardBeforeDir * dirMul;

        // Entry/second-touch bonuses
        if (prevTouches == 0) {
            reward += ENTRY_BONUS * RS_MIN(1.0f, dirMul);
        }
        else if (prevTouches == 1) {
            reward += SECOND_TOUCH_BONUS * RS_MIN(1.0f, dirMul);
        }

        // HEIGHT ARC BONUS
        float heightBonus = ComputeHeightBonus(id, state.ball.pos.z);
        if (heightBonus > 1.0f && !heightBonusApplied[id]) {
            // First time reaching height threshold - count metric
            s_arcDribbles.fetch_add(1);
            heightBonusApplied[id] = true;
        }

        // Apply height multiplier to reward
        reward *= heightBonus;

        if (reward <= EPS_REWARD_SKIP) {
            return 0.0f;
        }

        // Commit reward
        seqAccumReward[id] += reward;
        seqTouchCount[id] += 1;

        if (seqAccumReward[id] >= SEQ_REWARD_CAP - 1e-3f) {
            seqExhausted[id] = true;
        }

        s_onTarget.fetch_add(1);
        s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));

        return reward;
    }
};

// ============================================================================
// UnifiedAirDribbleReward (2025-02-17)
// Consolidates three separate air dribble rewards into one unified reward
// with mode detection (offensive, defensive, general)
//
// Rev 3.0 changes (2026-02-24):
//  - Added BallPred::Cache _ballCache + PreStep() override
//  - Per-touch trajectory quality multiplier (0.7x–1.5x) applied in all three
//    Evaluate functions based on predicted ball position at 0.75s ahead.
//    Offensive/General: reward * (1 + clamp((curDist - futureDist)/1200, -0.3, 0.5))
//    Defensive: reward * (1 + clamp((futureDist - curDist)/1200, -0.3, 0.5))
//    where dist is to opponent goal (off/gen) or own goal (def).
//
// Rev 5.0 changes (2026-08-01):
//  - Added behindMul: a horizontal contact-alignment multiplier (0.8x-1.2x)
//    scoped to the first two touches of a sequence (seq.touchCount <= 1).
//    Rewards contacting the ball on the side opposite the intended travel
//    direction (i.e. behind it, not just underneath), matching real-world
//    air dribble technique for converting height into forward distance on
//    the pop touch. Applied alongside undersideMul in GetReward().
// ============================================================================
class UnifiedAirDribbleReward : public Reward {
private:
    // Unified per-sequence state
    struct SequenceState {
        float accumReward = 0.0f;
        float timeElapsed = 0.0f;
        int touchCount = 0;
        bool exhausted = false;
        float startHeight = 0.0f;
        bool firstTouchProcessed = false;
        float boostAtStart     = -1.0f;  // boost level when sequence opened (-1 = unset)
        Vec   playerPosAtStart = {};     // player position when sequence opened

        // Velocity tracking (distance-dependent requirements)
        float initialBallVelocity = 0.0f;       // Ball speed when sequence started
        float distanceToOpponentGoal = 0.0f;    // Distance from start position to opponent goal

        // Ball travel integral since last touch (height × distance; adapted from BallTravelReward)
        float travelIntegralAccum = 0.0f;

        // Rev 4.0 — outcome-gated resolution
        bool  endedByOpponentTouch = false;   // set in PreStep, consumed+resolved in the owner's next GetReward
        bool  scoringTouchPaid     = false;   // one-shot latch, guards the goal-mouth resolution path
        float bestOutplayMul       = 1.0f;    // max ComputeOutplayMultiplier() seen this sequence
    };

    mutable std::unordered_map<uint32_t, SequenceState> sequenceStates;
    float minBallHeight = 250.0f;

    // Consolidated metrics
    mutable std::atomic<uint64_t> offensiveAttempts{ 0 };
    mutable std::atomic<uint64_t> offensiveCompletions{ 0 };

    const float EPS_REWARD_SKIP = 1e-7f;

    BallPred::Cache _ballCache;  // Updated once per step in PreStep(); read in EvaluateOffensive()/IsHeadingToGoalMouth()
    mutable Vec  _prevBallPos       = {};     // For ball travel integral (accumulated in PreStep)
    mutable bool _prevBallPosValid  = false;

    // ========================================================================
    // PRE-FLIP SETUP MECHANIC (Rev 4.0) — a gentle, diagonal flip touch off a
    // wall/backboard jump on a slow, distant, airborne ball, used to set up an
    // air dribble from range instead of the close-range underside-contact path.
    // ========================================================================
    mutable std::unordered_map<uint32_t, bool> _wasFlipping;        // flip-start edge tracking
    mutable std::unordered_map<uint32_t, bool> _lastJumpFromWall;   // true if the current airborne excursion began with a jump off a wall/backboard

    struct PendingPreFlip {
        float timeRemaining = 0.0f;
    };
    mutable std::unordered_map<uint32_t, PendingPreFlip> pendingPreFlip;

    const float PREFLIP_MAX_BALL_SPEED = 1400.0f;  // "slow" — roughly a car's unboosted top speed
    const float PREFLIP_MIN_GOAL_DIST  = 5100.0f;  // must be far from the net — this is a setup, not a finish
    const float PREFLIP_MIN_HIT_FORCE  = 200.0f;   // floor: touch must actually redirect the ball
    const float PREFLIP_MAX_HIT_FORCE  = 800.0f;   // ceiling: well under ROUGH_TOUCH_THRESHOLD=1200, stays "gentle"
    const float PREFLIP_WINDOW_SECONDS = 0.5f;     // follow-through window length
    const float PREFLIP_VEL_ALIGN_DOT  = 0.5f;     // cos(60°) — generous chase-angle tolerance
    const float PREFLIP_SETUP_BONUS    = 0.35f;    // < ENTRY_BONUS (0.45) — separate skill, separate payout
    const float WALL_UP_Z_THRESHOLD    = 0.2f;     // matches WallDashInputReward's UP_Z_THRESHOLD

    // ========================================================================
    // SEQUENCE GATE + RESOLUTION (Rev 4.0) — continuous angle taper (no hard
    // cliff, no time-based grace), and outcome-gated travel-bonus release.
    // ========================================================================
    const float HARD_FAIL_UNDERSIDE_Z = 0.00f;   // ~90° — only truly bad angles end the sequence
    const float IDEAL_UNDERSIDE_Z     = 0.30f;   // old 70° line — now just "full quality," not pass/fail
    const float MAX_CONTROL_DIST      = 1100.0f;  // UU — beyond this, no plausible dribble contact remains
    const float SCORING_TOUCH_BONUS   = 3.5f;    // flat bonus when a cap-exhausted sequence's scoring touch is bypassed through

    // ========================================================================
    // BEHIND-BALL HORIZONTAL ALIGNMENT (Rev 5.0) — first-two-touches-only
    // refinement on top of undersideMul: also reward contacting the ball on
    // the side opposite the intended travel direction (behind, not just under).
    // ========================================================================
    const float BEHIND_ALIGN_TOLERANCE = 0.25f;  // dot-product slack before behind-quality starts climbing
    const float BEHIND_MUL_MIN         = 0.8f;  // floor: car in front of / orthogonal to travel direction
    const float BEHIND_MUL_MAX         = 1.2f;  // ceiling: contact directly behind, opposite travel direction

    // ========================================================================
    // VELOCITY REQUIREMENT HELPERS (NEW - Distance-dependent)
    // ========================================================================

    float GetDistanceToOpponentGoal(const Player& player, const GameState& state) const {
        Vec oppGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        return (player.pos - oppGoal).Length();
    }

    float GetMinVelocityForDistance(float distanceToGoal) const {
        // Distance-based minimum velocity requirements (scaled for 10240 UU field)
        if (distanceToGoal >= 9000.0f) return 1800.0f;      // Very far (own corner ~9000+ UU)
        if (distanceToGoal >= 7000.0f) return 1700.0f;      // Far
        if (distanceToGoal >= 5000.0f) return 1500.0f;      // Medium
        if (distanceToGoal >= 3000.0f) return 1200.0f;      // Close
        return 0.0f;                                         // Very close - no minimum
    }

    float GetMaxBonusVelocity(float distanceToGoal) const {
        // Optimal velocity ranges for bonuses (scaled for 10240 UU field)
        if (distanceToGoal >= 9000.0f) return 2300.0f;
        if (distanceToGoal >= 7000.0f) return 2200.0f;
        if (distanceToGoal >= 5000.0f) return 2000.0f;
        if (distanceToGoal >= 3000.0f) return 1800.0f;
        return 1500.0f;
    }

    float GetInitialVelocityBonus(float ballVelocity, float distanceToGoal) const {
        // Higher bonus for faster starts when far from goal
        float minVelocity = GetMinVelocityForDistance(distanceToGoal);

        if (ballVelocity < minVelocity && minVelocity > 0.0f) {
            // Penalize slow starts from far away
            float penalty = RS_CLAMP(ballVelocity / minVelocity, 0.5f, 1.0f);
            return penalty;
        }

        // Bonus for exceeding minimum (up to 1.2x)
        float maxVelocity = GetMaxBonusVelocity(distanceToGoal);
        float bonus = RS_CLAMP((ballVelocity - minVelocity) / (maxVelocity - minVelocity), 0.0f, 0.2f);
        return 1.0f + bonus;  // Range [1.0, 1.2x]
    }

    // ========================================================================
    // OUTPLAY / BOOST-CONSERVATION HELPERS
    // ========================================================================

    // Returns a multiplier based on whether the ball is moving past an actively-challenging
    // opponent.  1.0x = no challenger (or ball heading into them), 1.15x = 50/50 contest,
    // 1.45x = ball clearly redirected past them (outplay).
    float ComputeOutplayMultiplier(const Player& player, const GameState& state) const {
        constexpr float CHALLENGE_DIST      = 900.0f;   // UU from ball — "in a challenge"
        constexpr float MIN_VEL_TOWARD_BALL = 250.0f;   // min approach speed to count
        constexpr float OUTPLAY_BONUS       = 1.45f;    // ball going past challenger
        constexpr float CONTEST_BONUS       = 1.15f;    // 50/50 or sideways redirect

        float bestDist = 1e9f;
        const Player* challenger = nullptr;
        for (const auto& opp : state.players) {
            if (opp.team == player.team) continue;
            float distToBall = (opp.pos - state.ball.pos).Length();
            if (distToBall >= CHALLENGE_DIST) continue;
            Vec oppToBall = (state.ball.pos - opp.pos).Normalized();
            float velTowardBall = RS_MAX(0.0f, opp.vel.Dot(oppToBall));
            if (velTowardBall < MIN_VEL_TOWARD_BALL) continue;
            if (distToBall < bestDist) {
                bestDist = distToBall;
                challenger = &opp;
            }
        }

        if (!challenger) return 1.0f;  // nobody actively challenging

        // awayScore: +1 = ball moving directly away from challenger (outplay),
        //            -1 = ball heading straight into challenger
        Vec ballToChallenger = (challenger->pos - state.ball.pos).Normalized();
        float ballLen = state.ball.vel.Length();
        float awayScore = (ballLen > 1.0f)
            ? -(state.ball.vel * (1.0f / ballLen)).Dot(ballToChallenger)
            : 0.0f;

        if (awayScore >  0.3f) return OUTPLAY_BONUS;
        if (awayScore > -0.3f) return CONTEST_BONUS;
        return 1.0f;  // heading into challenger, no bonus
    }

    // Returns true if BallPred shows the ball crossing the offensive goal plane
    // through the actual goal opening (accounting for ball radius).
    // Used to waive the boost-conservation penalty when committing all boost is correct.
    bool IsHeadingToGoalMouth(const Player& player) const {
        if (!_ballCache.valid) return false;
        constexpr float BALL_R      = 91.25f;
        constexpr float MOUTH_W     = 892.755f - BALL_R;   // 801.505 UU half-width
        // 2026-08-28 (NJH): lowered 200 UU (was 642.775-BALL_R=551.525, right under the
        // crossbar) — bot was consistently air-dribbling into the crossbar/just over the
        // goal line because trajectories aimed that high were already fully credited as
        // "goal-bound" by this check. 351.525 now requires aiming meaningfully lower for
        // max ComputeOutcomeScore()/boost-conservation-waiver credit.
        constexpr float MOUTH_H_MAX = 642.775f - BALL_R - 200.0f;   // 351.525 UU top
        float goalPlaneY = (player.team == Team::BLUE)
                            ?  CommonValues::BACK_WALL_Y
                            : -CommonValues::BACK_WALL_Y;
        for (int i = 0; i < BallPred::NUM_SLICES; i++) {
            const auto& sl = _ballCache.slices[i];
            bool pastPlane = (player.team == Team::BLUE)
                              ? (sl.pos.y >= goalPlaneY)
                              : (sl.pos.y <= goalPlaneY);
            if (pastPlane) {
                return fabsf(sl.pos.x) < MOUTH_W &&
                       sl.pos.z > BALL_R          &&
                       sl.pos.z < MOUTH_H_MAX;
            }
        }
        return false;
    }

    // ========================================================================
    // SEQUENCE RESOLUTION HELPERS (Rev 4.0)
    // Graduated outcome scoring — not all good air dribbles score a goal, so
    // the travel-integral bonus is released scaled by how the sequence ended:
    // net progress toward the opponent's goal as the baseline, maxed out by a
    // goal-mouth-bound trajectory, floored by having beaten a defender.
    // ========================================================================

    float ComputeOutcomeScore(const Player& player, const GameState& state, const SequenceState& seq) const {
        Vec oppGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        float endDist = (state.ball.pos - oppGoal).Length();
        float netProgress = RS_MAX(0.0f, seq.distanceToOpponentGoal - endDist);

        constexpr float PROGRESS_SCALE_FOR_OUTCOME = 3500.0f;  // UU of net progress for a "great" baseline (1.0)
        float outcomeScore = RS_CLAMP(netProgress / PROGRESS_SCALE_FOR_OUTCOME, 0.0f, 1.0f);

        if (IsHeadingToGoalMouth(player))
            outcomeScore = 1.0f;                       // goal-bound trajectory always maxes the score
        if (seq.bestOutplayMul >= 1.45f)                // matches OUTPLAY_BONUS in ComputeOutplayMultiplier
            outcomeScore = RS_MAX(outcomeScore, 0.6f);  // beating a defender floors the score at 0.6

        return outcomeScore;
    }

    // Releases the banked ball-travel-distance bonus, scaled by outcome quality, plus
    // (only when bypassing an already-exhausted cap on the scoring touch) a flat bonus.
    // Called from every sequence-ending path so outcome scoring is computed identically
    // regardless of whether it ended via opponent touch, disconnect, or a scoring touch.
    float ResolveSequence(const Player& player, const GameState& state, SequenceState& seq, bool wasCapBypass) const {
        float outcomeScore = ComputeOutcomeScore(player, state, seq);
        constexpr float TRAVEL_INTEGRAL_NORM = 1.91e-7f;
        constexpr float TRAVEL_BONUS_SCALE   = 8.0f;
        constexpr float TRAVEL_BONUS_MAX     = 2.0f;
        float travelBonus = RS_CLAMP(
            seq.travelIntegralAccum * TRAVEL_INTEGRAL_NORM * TRAVEL_BONUS_SCALE,
            0.0f, TRAVEL_BONUS_MAX);
        seq.travelIntegralAccum = 0.0f;

        float released = travelBonus * outcomeScore + (wasCapBypass ? SCORING_TOUCH_BONUS : 0.0f);
        seq.accumReward += released;
        return released;
    }

    // ========================================================================
    // HORIZONTAL VELOCITY CAP HELPER
    // ========================================================================

    // Soft penalty when the ball's horizontal (XY) speed exceeds 2300 uu/s —
    // at that speed the car can't keep up and the air dribble collapses.
    // Waived when the ball is within 3000 UU of the opposing goal (hard shot intent).
    float GetHorizVelPenaltyMul(const Player& player, const GameState& state) const {
        constexpr float HORIZ_VEL_LIMIT = 2300.0f;  // penalty ramp starts here
        constexpr float HORIZ_VEL_MAX   = 3000.0f;  // 0.5x floor at this speed
        constexpr float NEAR_GOAL_DIST  = 3000.0f;  // uu from goal_back — waive penalty
        constexpr float MIN_MUL         = 0.5f;

        Vec oppGoal = (player.team == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_BACK
            : CommonValues::BLUE_GOAL_BACK;
        if ((state.ball.pos - oppGoal).Length() <= NEAR_GOAL_DIST)
            return 1.0f;

        float horizSpeed = sqrtf(state.ball.vel.x * state.ball.vel.x +
                                 state.ball.vel.y * state.ball.vel.y);
        if (horizSpeed <= HORIZ_VEL_LIMIT)
            return 1.0f;

        float t = RS_CLAMP((horizSpeed - HORIZ_VEL_LIMIT) / (HORIZ_VEL_MAX - HORIZ_VEL_LIMIT),
                           0.0f, 1.0f);
        return 1.0f - (1.0f - MIN_MUL) * t;  // [1.0, 0.5]
    }

    // ========================================================================
    // OFFENSIVE MODE (based on AirDribbleAccuracyReward)
    // ========================================================================

    float EvaluateOffensive(const Player& player, const GameState& state, SequenceState& seq) const {
        // Constants for offensive mode
        // Rev 2.1: reduced per-touch reward, increased duration/touch limits
        // to encourage sustained multi-touch sequences over single-touch spikes.
        const float PROGRESS_SCALE = 0.007f;      // was 0.0105 — 33% less per unit of progress
        const float PER_TOUCH_MAX = 4.5f;         // was 7.0 — lower per-touch ceiling
        const float SEQ_REWARD_CAP = 65.0f;       // unchanged — same total ceiling, just more touches to reach it
        const float MAX_SEQ_DURATION = 18.0f;     // was 13.0 — 5s longer
        const int MAX_TOUCHES = 65;               // was 40 — 25 more touches allowed
        const float MIN_PROGRESS_UU = 8.0f;       // was 12.0 — softer touches qualify
        const float DECAY_EXPONENT = 0.35f;       // was 0.5 — slower decay, later touches still pay
        const float ENTRY_BONUS = 0.45f;          // was 0.65 — reduced to match lower per-touch scale
        const float SECOND_TOUCH_BONUS = 0.65f;   // was 1.0 — reduced to match
        const float MIN_DIR_MULTIPLIER = 0.20f;  // tightened: stronger penalty for off-goal touches
        const float GOOD_DIR_THRESHOLD = 0.65f;  // tightened: cos⁻¹(0.65)≈49° required for top tier
        const float ROUGH_TOUCH_THRESHOLD = 1200.0f;
        const float ROUGH_TOUCH_PENALTY = 0.7f;
        const float SCORING_ZONE_MIN = 250.0f;
        const float SCORING_ZONE_MAX = 630.0f;
        const float SCORING_ZONE_BONUS = 1.3f;
        const float MAX_BALL_Z_FOR_GOAL = 640.0f;
        const float HEIGHT_CONSTRAINT_PENALTY = 0.5f;

        offensiveAttempts.fetch_add(1);

        // Check sequence constraints
        if (seq.timeElapsed > MAX_SEQ_DURATION || seq.touchCount >= MAX_TOUCHES) {
            seq.exhausted = true;
            return 0.0f;
        }

        // Calculate progress toward opponent goal
        Vec oppGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        float prevDist = (state.prev->ball.pos - oppGoal).Length();
        float curDist = (state.ball.pos - oppGoal).Length();
        float progress = RS_MAX(0.0f, prevDist - curDist);

        if (progress < MIN_PROGRESS_UU) {
            return 0.0f;
        }

        // Base reward
        float perTouch = RS_MIN(PER_TOUCH_MAX, progress * PROGRESS_SCALE);
        float usedFraction = RS_CLAMP(seq.accumReward / SEQ_REWARD_CAP, 0.0f, 1.0f);
        float decay = powf(1.0f - usedFraction, DECAY_EXPONENT);
        float remainingCap = RS_MAX(0.0f, SEQ_REWARD_CAP - seq.accumReward);
        float rewardBeforeDir = RS_MIN(perTouch * decay, remainingCap);

        // Directional multiplier — aim at goal mouth center (Z≈321), not the back floor,
        // so high-arc dribbles are correctly aligned toward the actual opening.
        constexpr float GOAL_MOUTH_Z = 321.4f;  // 642.775 / 2
        Vec oppGoalCenter = (player.team == Team::BLUE)
            ? Vec(0.0f,  CommonValues::BACK_WALL_Y, GOAL_MOUTH_Z)
            : Vec(0.0f, -CommonValues::BACK_WALL_Y, GOAL_MOUTH_Z);
        Vec ballToGoal = (oppGoalCenter - state.ball.pos).Normalized();
        Vec ballVelDir = state.ball.vel.Normalized();
        float alignment = ballVelDir.Dot(ballToGoal);
        float dirMul = (alignment < 0.0f) ? MIN_DIR_MULTIPLIER * 0.4f :
                       (alignment < GOOD_DIR_THRESHOLD) ?
                       MIN_DIR_MULTIPLIER + (1.0f - MIN_DIR_MULTIPLIER) * (alignment / GOOD_DIR_THRESHOLD) * 0.8f :
                       0.8f + 0.2f * ((alignment - GOOD_DIR_THRESHOLD) / (1.0f - GOOD_DIR_THRESHOLD));

        float reward = rewardBeforeDir * dirMul;

        // Entry/second-touch bonuses (touchCount = qualifying touches rewarded so far;
        // consecutiveAirTouches was already incremented before Evaluate* is called, so
        // checking it against 0 never fired — touchCount is the correct gate here)
        if (seq.touchCount == 0) {
            reward += ENTRY_BONUS * RS_MIN(1.0f, dirMul);
        } else if (seq.touchCount == 1) {
            reward += SECOND_TOUCH_BONUS * RS_MIN(1.0f, dirMul);
        }

        // Height penalties in finishing zone
        if (state.ball.pos.z > MAX_BALL_Z_FOR_GOAL) {
            reward *= HEIGHT_CONSTRAINT_PENALTY;
        }

        // Scoring zone bonus
        if (state.ball.pos.z >= SCORING_ZONE_MIN && state.ball.pos.z <= SCORING_ZONE_MAX) {
            reward *= SCORING_ZONE_BONUS;
        }

        // First touch quality check
        if (!seq.firstTouchProcessed && state.prev) {
            float prevSpeed = state.prev->ball.vel.Length();
            float currSpeed = state.ball.vel.Length();
            float velocityChange = currSpeed - prevSpeed;
            if (velocityChange > ROUGH_TOUCH_THRESHOLD) {
                reward *= ROUGH_TOUCH_PENALTY;
            }
            seq.firstTouchProcessed = true;
        }

        // Trajectory quality multiplier: boost touches that predict ball approaching goal.
        // Combines Y-approach (Euclidean distance reduction) with X-centering (converging
        // toward goal centerline). 0.7x drifting away/wide, 1.0x neutral, up to 1.5x on target.
        {
            const float TRAJ_CHECK_TIME = 0.75f;    // seconds ahead to sample
            const float TRAJ_SCALE      = 1200.0f;  // UU of approach for full factor
            const float TRAJ_MIN_FACTOR = -0.3f;
            const float TRAJ_MAX_FACTOR =  0.5f;
            if (_ballCache.valid) {
                Vec futurePos    = _ballCache.AtTime(TRAJ_CHECK_TIME).pos;
                float futureDist = (futurePos - oppGoal).Length();
                float improvement = curDist - futureDist;  // positive = ball moving toward goal
                float trajFactor = RS_CLAMP(improvement / TRAJ_SCALE, TRAJ_MIN_FACTOR, TRAJ_MAX_FACTOR);

                // X-centering: reward trajectory converging toward goal center (X=0),
                // penalize drifting toward the corner. Capped at ±0.1 to not dominate.
                float curLateral    = fabsf(state.ball.pos.x);
                float futureLateral = fabsf(futurePos.x);
                float lateralFactor = RS_CLAMP((curLateral - futureLateral) / 1000.0f, -0.1f, 0.1f);
                trajFactor = RS_CLAMP(trajFactor + lateralFactor, TRAJ_MIN_FACTOR, TRAJ_MAX_FACTOR);

                reward *= (1.0f + trajFactor);
            }
        }

        // (Boost efficiency gate removed — boostAtStart was set at first-touch frame,
        //  making boostConsumed = 0 on touch 1, so the gate always killed the first touch.
        //  Quality is already filtered by directional multiplier + BallPred trajectory.)

        // Outplay multiplier: when an opponent is actively challenging, reward redirecting
        // the ball past them. Neutral (1.0x) when no challenger is present. Tracks the best
        // multiplier seen this sequence for ComputeOutcomeScore's outplay-based outcome floor.
        float outplayMul = ComputeOutplayMultiplier(player, state);
        seq.bestOutplayMul = RS_MAX(seq.bestOutplayMul, outplayMul);
        reward *= outplayMul;

        // Boost conservation: soft penalty for holding fewer than 20 boost (bot needs it for
        // recovery after the dribble ends). Waived if trajectory is already heading into the goal.
        if (player.boost < 20.0f && !IsHeadingToGoalMouth(player)) {
            float boostFactor = RS_CLAMP(player.boost / 20.0f, 0.5f, 1.0f);
            reward *= boostFactor;
        }

        // Goal mouth bonus: if BallPred predicts the ball crossing the goal plane through the
        // actual opening, apply a big bonus. Reuses IsHeadingToGoalMouth() (also used by
        // GetReward()'s scoring-touch resolution) instead of duplicating the BallPred slice scan.
        if (IsHeadingToGoalMouth(player)) {
            constexpr float GOAL_BONUS = 3.0f;   // was 1.5f — strong on-target signal
            reward *= GOAL_BONUS;
        }

        reward *= GetInitialVelocityBonus(seq.initialBallVelocity, seq.distanceToOpponentGoal);

        if (reward > EPS_REWARD_SKIP) {
            seq.accumReward += reward;
            seq.touchCount++;
            offensiveCompletions.fetch_add(1);
            if (seq.accumReward >= SEQ_REWARD_CAP - 1e-3f) {
                seq.exhausted = true;
            }
            return reward;
        }
        return 0.0f;
    }

public:
    virtual void PreStep(const GameState& state) override {
        _ballCache.Update(state);

        // Opponent-touch end-trigger (Rev 4.0): find who touched the ball this step (if
        // anyone), then flag any active sequence whose owner is on the OTHER team as
        // defended-away. Resolved (payout + erase) in the owner's next GetReward call,
        // since PreStep itself can't return a reward.
        for (const auto& toucher : state.players) {
            if (!toucher.ballTouchedStep) continue;
            for (auto& [ownerId, seq] : sequenceStates) {
                if (ownerId == toucher.carId) continue;
                for (const auto& owner : state.players) {
                    if (owner.carId == ownerId && owner.team != toucher.team) {
                        seq.endedByOpponentTouch = true;
                    }
                }
            }
            break;  // only need the first toucher this step
        }

        // Ball travel integral: accumulate height × step_distance once per frame
        // for all active sequences. PreStep runs once per frame (not per player),
        // so this avoids per-player inflation in 2v2/3v3.
        // Adapted from BallTravelReward (rlgym-tools, do_integral=True mode):
        // https://github.com/RLGym/rlgym-tools/blob/main/rlgym_tools/rocket_league/reward_functions/ball_travel_reward.py
        if (_prevBallPosValid && state.ball.pos.z > minBallHeight) {
            float step_dist = (state.ball.pos - _prevBallPos).Length();
            float avg_z     = (state.ball.pos.z + _prevBallPos.z) * 0.5f;
            float step_integral = step_dist * avg_z;
            if (step_integral > 0.0f) {
                for (auto& [id, seq] : sequenceStates)
                    if (!seq.exhausted)
                        seq.travelIntegralAccum += step_integral;
            }
        }
        _prevBallPos      = state.ball.pos;
        _prevBallPosValid = true;
    }

    virtual void Reset(const GameState& initialState) override {
        sequenceStates.clear();
        offensiveAttempts = 0;
        offensiveCompletions = 0;
        _prevBallPos      = {};
        _prevBallPosValid = false;
        _wasFlipping.clear();
        _lastJumpFromWall.clear();
        pendingPreFlip.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        uint32_t id = player.carId;
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

        Vec  carFromBall  = player.pos - state.ball.pos;
        float carBallDist = carFromBall.Length();

        // ====================================================================
        // Wall-jump-origin tracking (pre-flip mechanic, Rev 4.0) — runs every
        // step, independent of whether a sequence is currently open. isOnGround
        // is pure wheel-contact and stays true while driving on a wall, so a
        // horizontal up-vector (matches WallDashInputReward's on-wall check) is
        // what actually distinguishes a wall/backboard jump from a ground jump.
        // ====================================================================
        if (state.prev) {
            const Player& prevPlayer = state.prev->players[player.index];
            if (prevPlayer.isOnGround && !player.isOnGround) {
                _lastJumpFromWall[id] = fabsf(prevPlayer.rotMat.up.z) < WALL_UP_Z_THRESHOLD;
            }
            if (player.isOnGround) {
                _lastJumpFromWall[id] = false;  // landed — next flip is a new excursion
            }
        }

        bool wasFlipping = _wasFlipping[id];
        _wasFlipping[id] = player.isFlipping;

        bool hasActiveSequence = sequenceStates.find(id) != sequenceStates.end();

        // ====================================================================
        // Pre-flip setup mechanic (Rev 4.0) — only applies when no sequence is
        // open yet. A gentle diagonal flip off a wall/backboard jump on a slow,
        // distant, airborne ball opens a follow-through window; a good chase
        // (still below the ball, moving with it) opens a real sequence.
        // ====================================================================
        if (!hasActiveSequence) {
            auto pendIt = pendingPreFlip.find(id);
            if (pendIt != pendingPreFlip.end()) {
                PendingPreFlip& pend = pendIt->second;
                pend.timeRemaining -= dt;

                bool underBall = (carBallDist > 0.1f) && ((carFromBall.z / carBallDist) < -IDEAL_UNDERSIDE_Z);
                bool velAligned = false;
                if (player.vel.Length() > 50.0f && state.ball.vel.Length() > 50.0f) {
                    velAligned = player.vel.Normalized().Dot(state.ball.vel.Normalized()) > PREFLIP_VEL_ALIGN_DOT;
                }

                if (underBall && velAligned) {
                    pendingPreFlip.erase(pendIt);
                    sequenceStates[id] = SequenceState();
                    SequenceState& newSeq = sequenceStates[id];
                    newSeq.startHeight            = state.ball.pos.z;
                    newSeq.boostAtStart           = player.boost;
                    newSeq.playerPosAtStart       = player.pos;
                    newSeq.initialBallVelocity    = state.ball.vel.Length();
                    newSeq.distanceToOpponentGoal = GetDistanceToOpponentGoal(player, state);
                    return PREFLIP_SETUP_BONUS;
                }
                if (pend.timeRemaining <= 0.0f) {
                    pendingPreFlip.erase(pendIt);
                }
            } else if (!wasFlipping && player.isFlipping && player.ballTouchedStep && state.prev
                       && _lastJumpFromWall[id]) {
                Vec oppGoal = (player.team == Team::BLUE) ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
                bool ballAirborne = state.ball.pos.z >= minBallHeight;
                bool ballSlow      = state.ball.vel.Length() < PREFLIP_MAX_BALL_SPEED;
                bool ballFar       = (state.ball.pos - oppGoal).Length() >= PREFLIP_MIN_GOAL_DIST;
                float pitch = player.prevAction.pitch, yaw = player.prevAction.yaw;
                bool diagonalFlip = (pitch > 0.5f && fabsf(yaw) > 0.2f && fabsf(yaw) < 0.8f);
                float hitForce = (state.ball.vel - state.prev->ball.vel).Length();
                bool gentleTouch = (hitForce >= PREFLIP_MIN_HIT_FORCE && hitForce <= PREFLIP_MAX_HIT_FORCE);

                if (ballAirborne && ballSlow && ballFar && diagonalFlip && gentleTouch) {
                    pendingPreFlip[id] = PendingPreFlip{ PREFLIP_WINDOW_SECONDS };
                }
            }
        }

        // ====================================================================
        // Generic entry (restored) — Rev 4.0's refactor accidentally deleted
        // the only path that could open a sequence from an ordinary underside
        // touch, leaving the pre-flip wall-jump mechanic as the sole entry
        // point. This restores the general case (ground pops, aerial touches,
        // any non-wall-jump setup) as an additional path alongside pre-flip,
        // using the same IDEAL_UNDERSIDE_Z bar the reward used before Rev 4.0.
        // ====================================================================
        if (!hasActiveSequence && player.ballTouchedStep && state.prev
            && !player.isOnGround && state.ball.pos.z >= minBallHeight) {
            bool entryAngleOk = (carBallDist > 0.1f) && ((carFromBall.z / carBallDist) < -IDEAL_UNDERSIDE_Z);
            if (entryAngleOk) {
                sequenceStates[id] = SequenceState();
                SequenceState& newSeq = sequenceStates[id];
                newSeq.startHeight            = state.ball.pos.z;
                newSeq.boostAtStart           = player.boost;
                newSeq.playerPosAtStart       = player.pos;
                newSeq.initialBallVelocity    = state.ball.vel.Length();
                newSeq.distanceToOpponentGoal = GetDistanceToOpponentGoal(player, state);
            }
        }

        hasActiveSequence = sequenceStates.find(id) != sequenceStates.end();  // pre-flip or generic entry may have just opened one
        if (!hasActiveSequence) {
            return 0.0f;
        }

        SequenceState& seq = sequenceStates.at(id);

        // ====================================================================
        // Opponent-touch resolution (Rev 4.0) — resolve+erase before anything
        // else if PreStep flagged this sequence as defended-away last frame.
        // ====================================================================
        if (seq.endedByOpponentTouch) {
            float released = ResolveSequence(player, state, seq, /*wasCapBypass=*/false);
            sequenceStates.erase(id);
            return (released > EPS_REWARD_SKIP) ? released : 0.0f;
        }

        seq.timeElapsed += dt;

        // ====================================================================
        // Retuned gate (Rev 4.0): continuous angle taper, minimal (distance-
        // only) erase condition — no time-based grace window needed. Old 70°
        // hard cutoff is now just the "full quality" line (IDEAL_UNDERSIDE_Z);
        // a marginal-angle touch pays less instead of wiping the sequence.
        // ====================================================================
        bool angleOk = (carBallDist > 0.1f) && ((carFromBall.z / carBallDist) < -HARD_FAIL_UNDERSIDE_Z);
        bool disconnected = carBallDist > MAX_CONTROL_DIST;

        if (player.isOnGround || state.ball.pos.z < minBallHeight || !angleOk || disconnected) {
            float released = ResolveSequence(player, state, seq, /*wasCapBypass=*/false);
            sequenceStates.erase(id);
            return (released > EPS_REWARD_SKIP) ? released : 0.0f;
        }

        if (!player.ballTouchedStep || !state.prev) {
            return 0.0f;
        }

        // Underside contact quality: 0 at the new wide edge, 1 at/beyond the old 70° "ideal" line
        float undersideNormZ   = carFromBall.z / carBallDist;
        float undersideQuality = RS_CLAMP(
            (-undersideNormZ - HARD_FAIL_UNDERSIDE_Z) / (IDEAL_UNDERSIDE_Z - HARD_FAIL_UNDERSIDE_Z),
            0.0f, 1.0f);
        float undersideMul = 0.2f + 0.8f * undersideQuality;

        // Behind-ball horizontal alignment (Rev 5.0) — on the first two touches only
        // (the pop that sets the dribble's height + direction), also reward contact
        // on the side of the ball opposite the intended travel direction. Hitting
        // low AND slightly behind is what converts height into forward distance
        // instead of a straight-up juggle.
        float behindMul = 1.0f;
        if (seq.touchCount <= 1) {
            Vec oppGoal = (player.team == Team::BLUE) ?
                CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
            float toGoalX = oppGoal.x - state.ball.pos.x;
            float toGoalY = oppGoal.y - state.ball.pos.y;
            float toGoalLen = sqrtf(toGoalX * toGoalX + toGoalY * toGoalY);
            float offsetLen = sqrtf(carFromBall.x * carFromBall.x + carFromBall.y * carFromBall.y);
            if (toGoalLen > 1.0f && offsetLen > 1.0f) {
                // Negative dot = car offset points opposite the travel direction (behind the ball) — what we want.
                float dot = (carFromBall.x * toGoalX + carFromBall.y * toGoalY) / (offsetLen * toGoalLen);
                float behindQuality = RS_CLAMP((-dot + BEHIND_ALIGN_TOLERANCE) / (1.0f + BEHIND_ALIGN_TOLERANCE), 0.0f, 1.0f);
                behindMul = BEHIND_MUL_MIN + (BEHIND_MUL_MAX - BEHIND_MUL_MIN) * behindQuality;
            }
        }

        // ====================================================================
        // Normal touch scoring + goal-mouth resolution (Rev 4.0: reworked from
        // a flat per-touch travel bonus into the outcome-gated ResolveSequence)
        // ====================================================================
        bool isScoringTouch = IsHeadingToGoalMouth(player);

        float reward = 0.0f;
        if (!seq.exhausted) {
            reward = EvaluateOffensive(player, state, seq);
            if (reward > EPS_REWARD_SKIP) {
                // Underside quality multiplier — rewards more vertical (beneath-ball) approach
                reward *= undersideMul;
                // Behind-ball quality multiplier — rewards contact opposite the travel direction on early touches
                reward *= behindMul;
                // Horizontal speed cap: penalize ball horiz speed > 2300 uu/s (car can't keep up)
                reward *= GetHorizVelPenaltyMul(player, state);
            }
        }

        if (isScoringTouch && !seq.scoringTouchPaid) {
            seq.scoringTouchPaid = true;
            reward += ResolveSequence(player, state, seq, /*wasCapBypass=*/seq.exhausted);
            // Deliberately NOT erased here — IsHeadingToGoalMouth is a BallPred prediction,
            // not a guaranteed goal; the ball can still be intercepted before crossing, so
            // the sequence stays alive for a possible later opponent-touch/disconnect resolution.
        }

        return (reward > EPS_REWARD_SKIP) ? reward : 0.0f;
    }

    // Metrics accessors for W&B monitoring
    uint64_t GetOffensiveAttempts() const { return offensiveAttempts; }
    uint64_t GetOffensiveCompletions() const { return offensiveCompletions; }
};

// 2025-12-20 | Rev 1.0 | NJH
// - Rewards recognizing and capitalizing on air dribble opportunities
// - Detects: ball on car, slow bouncing ball, wall roll situations
// - Applies opponent proximity penalty
class AirDribbleOpportunityAwarenessReward : public Reward {
private:
    mutable std::map<uint32_t, bool> opportunityDetected;
    mutable std::map<uint32_t, float> opportunityStartTime;
    mutable std::map<uint32_t, float> elapsedTime; // Track time manually
    mutable std::map<uint32_t, bool> airDribbleStarted;

    // Opportunity detection thresholds
    float slowBallSpeed = 600.0f;
    float bouncingUpwardVel = 200.0f;
    float wallProximity = 200.0f;
    float ballOnCarHeight = 40.0f;
    float ballOnCarMaxHeight = 200.0f;

    // Air dribble detection
    float airDribbleMinHeight = 150.0f;
    float airDribbleMaxDist = 250.0f;

    // Opponent proximity penalty
    float opponentThreatDist = 800.0f;
    float proximityPenalty = 0.25f;

    bool IsOpportunity(const Player& player, const GameState& state) const {
        Vec relBallPos = state.ball.pos - player.pos;
        float ballSpeed = state.ball.vel.Length();
        float ballHeight = state.ball.pos.z;

        // Opportunity 1: Ball on car
        float heightAboveCar = relBallPos.z;
        float horizDist = Vec(relBallPos.x, relBallPos.y, 0).Length();
        bool ballOnCar = (heightAboveCar > ballOnCarHeight &&
            heightAboveCar < ballOnCarMaxHeight &&
            horizDist < 150.0f);

        // Opportunity 2: Slow bouncing ball
        bool slowBouncing = (ballSpeed < slowBallSpeed &&
            state.ball.vel.z > bouncingUpwardVel &&
            ballHeight < 400.0f);

        // Opportunity 3: Ball rolling up wall
        bool nearWall = (fabsf(state.ball.pos.x) > (CommonValues::SIDE_WALL_X - wallProximity) ||
            fabsf(state.ball.pos.y) > (CommonValues::BACK_WALL_Y - wallProximity));

        bool wallRoll = (nearWall &&
            state.ball.vel.z > 150.0f &&
            ballSpeed < 900.0f &&
            ballHeight > 100.0f && ballHeight < 600.0f);

        return (ballOnCar || slowBouncing || wallRoll);
    }

    bool IsAirDribbling(const Player& player, const GameState& state) const {
        if (player.isOnGround) return false;

        float ballHeight = state.ball.pos.z;
        if (ballHeight < airDribbleMinHeight) return false;

        Vec relBallPos = state.ball.pos - player.pos;
        float distToBall = relBallPos.Length();

        return (distToBall < airDribbleMaxDist);
    }

    float GetOpponentProximityFactor(const Player& player, const GameState& state) const {
        float closestOpponentDist = 99999.0f;

        for (int i = 0; i < state.players.size(); i++) {
            const Player& other = state.players[i];
            if (other.team == player.team) continue;

            float dist = (other.pos - player.pos).Length();
            if (dist < closestOpponentDist) {
                closestOpponentDist = dist;
            }
        }

        // If opponent is close, reduce reward
        if (closestOpponentDist < opponentThreatDist) {
            float threatFactor = closestOpponentDist / opponentThreatDist;
            return (1.0f - proximityPenalty) + (proximityPenalty * threatFactor);
        }

        return 1.0f;
    }

public:
    virtual void Reset(const GameState& initialState) override {
        opportunityDetected.clear();
        opportunityStartTime.clear();
        elapsedTime.clear();
        airDribbleStarted.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
        uint32_t id = player.carId;

        // Update elapsed time
        elapsedTime[id] += dt;

        bool isOpportunityNow = IsOpportunity(player, state);
        bool wasOpportunity = opportunityDetected[id];
        bool hasStartedAirDribble = airDribbleStarted[id];

        float reward = 0.0f;

        // Detect new opportunity
        if (isOpportunityNow && !wasOpportunity) {
            opportunityDetected[id] = true;
            opportunityStartTime[id] = elapsedTime[id];
        }

        // Check if player capitalized on opportunity
        if (wasOpportunity && IsAirDribbling(player, state) && !hasStartedAirDribble) {
            airDribbleStarted[id] = true;

            // Time window bonus (faster recognition = better)
            float timeSinceOpportunity = elapsedTime[id] - opportunityStartTime[id];
            float timeBonus = RS_CLAMP(1.0f - (timeSinceOpportunity / 2.0f), 0.5f, 1.0f);

            // Base reward for recognizing and starting air dribble
            float baseReward = 18.0f * timeBonus;

            // Apply opponent proximity penalty
            float proximityFactor = GetOpponentProximityFactor(player, state);
            reward = baseReward * proximityFactor;

            // Bonus if ball was touched during transition
            if (player.ballTouchedStep) {
                reward += 5.0f * proximityFactor;
            }
        }

        // Reset tracking if opportunity lost or air dribble ended
        if (!isOpportunityNow && !IsAirDribbling(player, state)) {
            opportunityDetected[id] = false;
            airDribbleStarted[id] = false;
        }

        return reward;
    }
};


// ============================================================================
// SpeedFlipReward
// Detects a forward-going speed flip with:
//  - Diagonal flip input at start (forward + yaw)
//  - Flip cancel via opposite pitch within 0.3s
//  - Air roll or powerslide used around landing to land cleanly
//  - Car remains facing roughly same direction and gains forward speed
// 2025-12-09 - Rev 3 (NJH)
// ============================================================================
class SpeedFlipReward : public Reward {
private:
    struct FlipState {
        Vec  startForward;
        Vec  startVel;
        bool wasFlipping = false;
        bool startedForward = false;
        bool diagonalInputAtStart = false;
        bool cancelDetected = false;
        bool landingCorrection = false;
        float cancelWindow = 0.0f;
        float landingWindow = 0.0f;
        bool wasOnGround = true;
    };

    mutable std::map<uint32_t, FlipState> flipStates;

public:
    virtual void Reset(const GameState& initialState) override {
        flipStates.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
        auto& fs = flipStates[player.carId];

        bool wasFlipping = fs.wasFlipping;
        fs.wasFlipping = player.isFlipping;

        // Track ground / landing for correction detection
        bool wasOnGround = fs.wasOnGround;
        fs.wasOnGround = player.isOnGround;

        // Update windows
        if (fs.cancelWindow > 0.0f)
            fs.cancelWindow = RS_MAX(0.0f, fs.cancelWindow - dt);
        if (fs.landingWindow > 0.0f)
            fs.landingWindow = RS_MAX(0.0f, fs.landingWindow - dt);

        // Detect flip cancel: opposite pitch within cancel window
        if (fs.cancelWindow > 0.0f) {
            // Initial flip is forward (pitch > 0), so cancel is negative pitch
            if (player.prevAction.pitch < -0.5f) {
                fs.cancelDetected = true;
            }
        }

        // Detect landing correction: air roll or powerslide around landing
        if (!wasOnGround && player.isOnGround && fs.landingWindow > 0.0f) {
            bool usingCorrection =
                (fabsf(player.prevAction.roll) > 0.3f) || (player.prevAction.handbrake > 0.5f);
            if (usingCorrection) {
                fs.landingCorrection = true;
            }
        }

        // Flip just started
        if (!wasFlipping && player.isFlipping) {
            fs.startForward = player.rotMat.forward;
            fs.startVel = player.vel;

            float startSpeed = player.vel.Length();
            fs.startedForward = false;
            if (startSpeed > 300.0f) {
                Vec velDir = player.vel / startSpeed;
                float velAlign = velDir.Dot(player.rotMat.forward);
                if (velAlign > 0.5f) {
                    fs.startedForward = true;
                }
            }

            // Diagonal speed-flip input: forward pitch + sideways yaw
            fs.diagonalInputAtStart =
                (player.prevAction.pitch > 0.4f) &&
                (fabsf(player.prevAction.yaw) > 0.3f);

            // Require both forward motion and diagonal input
            if (fs.startedForward && fs.diagonalInputAtStart) {
                fs.cancelDetected = false;
                fs.landingCorrection = false;
                fs.cancelWindow = 0.3f;   // window to see flip cancel
                fs.landingWindow = 0.6f;  // landing correction around touch-down
            }
            else {
                fs.cancelWindow = 0.0f;
                fs.landingWindow = 0.0f;
            }

            return 0.0f;
        }

        // Flip just ended
        if (wasFlipping && !player.isFlipping) {
            // Only reward if we started with a valid forward diagonal flip
            if (!fs.startedForward || !fs.diagonalInputAtStart)
                return 0.0f;

            // Need a flip cancel and landing correction
            if (!fs.cancelDetected || !fs.landingCorrection)
                return 0.0f;

            Vec startForward = fs.startForward;
            Vec endForward = player.rotMat.forward;

            // Must remain roughly facing same direction (not a half-flip)
            float forwardAlignment = startForward.Dot(endForward);
            if (forwardAlignment < 0.7f)
                return 0.0f;

            float startSpeed = fs.startVel.Length();
            float endSpeed = player.vel.Length();
            if (endSpeed < 800.0f)
                return 0.0f;

            Vec endVelDir = (endSpeed > 1e-3f) ? (player.vel / endSpeed) : endForward;
            float endVelAlign = endVelDir.Dot(endForward);

            // Must be moving strongly forward after flip
            if (endVelAlign < 0.7f)
                return 0.0f;

            // Require meaningful speed gain
            if (endSpeed <= startSpeed + 400.0f)
                return 0.0f;

            float gain = RS_CLAMP((endSpeed - startSpeed) / 800.0f, 0.0f, 1.0f);
            return 10.0f * gain;
        }

        return 0.0f;
    }
};



// Advanced recovery - landing with powerslide on any surface (ground, walls, ceiling)
// 2026-05-22 - Rev 2 - Generalized surface detection via InferSurfaceNormal. (NJH)
// Previously used rotMat.up.z > threshold which only rewarded flat-ground landings.
// Now uses up.Dot(surfaceNormal) so the alignment check works identically on walls
// and the ceiling.
class AdvancedRecoveryReward : public Reward {
private:
    mutable std::unordered_map<uint32_t, bool> wasAirborne;

    // Returns the outward surface normal for whichever surface the car is closest to.
    // Ground  (z=0):     (0, 0,  1)
    // Ceiling (z=2044):  (0, 0, -1)
    // Right wall  (+x):  (-1, 0, 0)
    // Left wall   (-x):  ( 1, 0, 0)
    // Orange wall (+y):  (0, -1, 0)
    // Blue wall   (-y):  (0,  1, 0)
    // A perfect landing on any surface has rotMat.up.Dot(normal) == 1.0.
    static Vec InferSurfaceNormal(const Player& player) {
        using namespace CommonValues;
        float px = player.pos.x;
        float py = player.pos.y;
        float pz = player.pos.z;

        float dFloor   = pz;                  // dist from z = 0
        float dCeiling = CEILING_Z - pz;      // dist from z = 2044
        float dWallPX  = SIDE_WALL_X - px;    // dist from x = +4096
        float dWallNX  = SIDE_WALL_X + px;    // dist from x = -4096
        float dWallPY  = BACK_WALL_Y - py;    // dist from y = +5120
        float dWallNY  = BACK_WALL_Y + py;    // dist from y = -5120

        float minDist = dFloor;
        Vec   normal  = Vec(0, 0, 1);

        if (dCeiling < minDist) { minDist = dCeiling; normal = Vec(0, 0, -1); }
        if (dWallPX  < minDist) { minDist = dWallPX;  normal = Vec(-1, 0, 0); }
        if (dWallNX  < minDist) { minDist = dWallNX;  normal = Vec(1,  0, 0); }
        if (dWallPY  < minDist) { minDist = dWallPY;  normal = Vec(0, -1, 0); }
        if (dWallNY  < minDist) {                      normal = Vec(0,  1, 0); }

        return normal;
    }

public:
    virtual void Reset(const GameState& initialState) override {
        wasAirborne.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        bool wasInAir = wasAirborne[player.carId];
        wasAirborne[player.carId] = !player.isOnGround;

        if (!wasInAir || !player.isOnGround)
            return 0.0f;

        // alignment == 1.0 → wheels flat on surface (perfect)
        // alignment == 0.0 → car sideways on landing
        // alignment  < 0.0 → car inverted / bad landing
        Vec   surfaceNormal  = InferSurfaceNormal(player);
        float alignment      = player.rotMat.up.Dot(surfaceNormal);
        bool  usingPowerslide = (player.prevAction.handbrake > 0.5f);

        if (alignment > 0.8f && usingPowerslide)
            return 5.0f;   // Perfect landing on any surface + powerslide
        if (alignment > 0.7f)
            return 2.0f;   // Good landing on any surface

        return 0.0f;
    }
};

// Dribbling awareness - recognizing when opponent is dribbling
class DribbleAwarenessReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Check if any opponent is dribbling
        for (auto& opp : state.players) {
            if (opp.team == player.team) continue;

            // Check if ball is on opponent's car
            Vec relBallPos = state.ball.pos - opp.pos;
            float heightAboveCar = relBallPos.z;
            float horizDist = Vec(relBallPos.x, relBallPos.y, 0).Length();
            
            bool oppDribbling = (heightAboveCar > 30.0f && heightAboveCar < 200.0f && 
                                horizDist < 150.0f && opp.isOnGround);
            
            if (oppDribbling) {
                // Opponent is dribbling - reward defensive positioning
                float distToOpp = (player.pos - opp.pos).Length();
                
                if (distToOpp > 500.0f && distToOpp < 1500.0f) {
                    // Good spacing against dribbler
                    return 3.0f;
                }
            }
        }
        
        return 0.0f;
    }
};

// ============================================================================
// ShotOnGoalReward - OPTIMIZED FOR AGGRESSIVE SHOOTING
// 2025-12-23 | Rev 2.0 | Heavily rewards shots on goal, especially open nets
// ============================================================================
class ShotOnGoalReward : public Reward {
private:
    float baseReward = 15.0f;  // Increased from 10.0f

    bool scaleBySpeed = true;
    float slowKph = 40.0f;   // Lowered threshold - reward slower shots too
    float fastKph = 100.0f;  // Lowered - don't require supersonic

public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Only pay on shot event
        if (!player.eventState.shot) return 0.0f;

        // Must be going toward opponent goal
        if (!RLGC::RewardMath::BallIntersectsOpponentGoalMouth(player, state, 2.0f, true, true)) {
            return 0.0f;
        }

        // Speed scaling
        float speedScale = 1.0f;
        if (scaleBySpeed) {
            const float kph = state.ball.vel.Length() * 0.036f;
            if (kph <= slowKph) {
                speedScale = 0.6f;  // Still give decent reward for slower shots
            }
            else if (kph >= fastKph) {
                speedScale = 1.0f;
            }
            else {
                const float t = (kph - slowKph) / (fastKph - slowKph);
                speedScale = 0.6f + 0.4f * t;
            }
        }

        return baseReward * speedScale;
    }
};

// ============================================================================
// LongShotReward - ELITE TIER
// Rewards shooting on goal from distance with high velocity
// Emphasizes accurate aiming and powerful shots from far away
// 2025-01-04 | Rev 1.0 | NJH
// ============================================================================

// =========================================================================
// LONG SHOT REWARD - Simplified Stateless Implementation
// =========================================================================
// Rewards fast shots directed toward opponent's goal.
//
// Core Mechanic:
// - Ball traveling toward goal (dot > 0.5, ±60° off-center)
// - Ball speed >= 1500 uu/s
// - Reward scales proportionally with speed: (speed - 1500) / 1000, clamped 0-1
//
// 2025-01-28 - Optimized: Removed 4-phase state machine, ~65% code reduction
// =========================================================================

class LongShotReward : public Reward {
public:

	// =====================================================================
	// CONFIGURATION
	// =====================================================================

	float minBallSpeed = 1500.0f;        // Minimum speed threshold
	float goalAlignment = 0.5f;          // Dot product threshold (±60° off-center)

	// =====================================================================
	// METRICS STRUCTURE
	// =====================================================================

	struct LongShotMetrics {
		int attempts = 0;
		int successful = 0;
		float totalRewardSum = 0.0f;

		void Reset() {
			attempts = 0;
			successful = 0;
			totalRewardSum = 0.0f;
		}

		float GetSuccessRate() const {
			return attempts > 0 ? float(successful) / float(attempts) : 0.0f;
		}

		float GetAverageReward() const {
			return attempts > 0 ? totalRewardSum / float(attempts) : 0.0f;
		}
	};

	// =====================================================================
	// INITIALIZATION
	// =====================================================================

	void Reset(const GameState& state) override {
		// No state to reset (stateless design)
	}

	// =====================================================================
	// REWARD COMPUTATION
	// =====================================================================

	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
		// Only reward on actual ball touches
		if (!player.ballTouchedStep || !state.prev)
			return 0.0f;

		// Get ball velocity
		Vec curVel = state.ball.vel;

		// Calculate ball speed (horizontal component for consistency)
		float ballSpeed = Vec(curVel.x, curVel.y, 0.0f).Length();

		// Must exceed speed threshold
		if (ballSpeed < minBallSpeed)
			return 0.0f;

		// Get goal position for this player's team
		Vec targetGoal = (player.team == Team::BLUE)
			? CommonValues::ORANGE_GOAL_CENTER
			: CommonValues::BLUE_GOAL_CENTER;

		// Check direction toward goal
		if (ballSpeed < 1e-3f)
			return 0.0f;

		Vec ballDir = Vec(curVel.x, curVel.y, 0.0f) / ballSpeed;
		Vec toGoal = (targetGoal - state.ball.pos).Normalized();
		float goalDot = ballDir.Dot(toGoal);

		if (goalDot < goalAlignment)
			return 0.0f;

		// Proportional reward based on speed
		// Scale from 0 at minBallSpeed to 1.0 at minBallSpeed + 1000
		float speedBonus = RS_CLAMP((ballSpeed - minBallSpeed) / 1000.0f, 0.0f, 1.0f);

		// Update metrics
		metrics.attempts++;
		if (speedBonus > 0.0f) {
			metrics.successful++;
		}
		metrics.totalRewardSum += speedBonus;

		return speedBonus;
	}

public:

	inline static LongShotMetrics metrics;

	static LongShotMetrics GetAndResetMetrics() {
		LongShotMetrics m = metrics;
		metrics.Reset();
		return m;
	}
};

// ============================================================================
// OpenNetShotReward - UPDATED
// Massively rewards shooting when net is open (no defenders in way)
// Now also checks positioning: player and ball should be closer to opponent goal
// Adds bonus for opponents being out of position
// 2025-01-10 | Rev 2.0 (NJH)
// ============================================================================
class OpenNetShotReward : public Reward {
private:
    float baseReward = 25.0f;  // HUGE reward for open net shots
    float defenderCheckRadius = 2500.0f;  // How far to check for defenders
    float outOfPositionBonus = 15.0f;  // Extra reward when opponent is out of position
    float velocityToGoalBonus = 10.0f;  // Extra reward for ball velocity toward goal when opponent out of position

    // Check if player is closer to opponent goal than all opponents
    bool IsPlayerCloserToGoalThanOpponents(const Player& player, const GameState& state, const Vec& oppGoal) const {
        float playerDistToGoal = (player.pos - oppGoal).Length();

        for (const Player& p : state.players) {
            if (p.team == player.team) continue;  // Skip teammates

            float oppDistToGoal = (p.pos - oppGoal).Length();

            // If any opponent is closer to goal than us, we're not in ideal position
            if (oppDistToGoal < playerDistToGoal - 300.0f) {  // 300 unit margin
                return false;
            }
        }

        return true;
    }

    // Check if ball is closer to opponent goal than all opponents
    bool IsBallCloserToGoalThanOpponents(const GameState& state, const Vec& oppGoal, Team playerTeam) const {
        float ballDistToGoal = (state.ball.pos - oppGoal).Length();

        for (const Player& p : state.players) {
            if (p.team == playerTeam) continue;  // Skip our team

            float oppDistToGoal = (p.pos - oppGoal).Length();

            // If any opponent is closer to goal than ball, they're defending well
            if (oppDistToGoal < ballDistToGoal - 300.0f) {  // 300 unit margin
                return false;
            }
        }

        return true;
    }

    // Check if opponent is out of position (we're closer to both goal and ball)
    bool IsOpponentOutOfPosition(const Player& player, const GameState& state, const Vec& oppGoal) const {
        float playerDistToBall = (player.pos - state.ball.pos).Length();
        float playerDistToGoal = (player.pos - oppGoal).Length();

        for (const Player& p : state.players) {
            if (p.team == player.team) continue;  // Skip teammates

            float oppDistToBall = (p.pos - state.ball.pos).Length();
            float oppDistToGoal = (p.pos - oppGoal).Length();

            // Opponent is in good position if they're closer to ball OR closer to goal
            if (oppDistToBall < playerDistToBall - 200.0f || oppDistToGoal < playerDistToGoal - 300.0f) {
                return false;
            }
        }

        return true;  // We're closer to both ball and goal than all opponents
    }

    bool IsNetOpen(const Player& player, const GameState& state) const {
        Vec oppGoal = (player.team == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_BACK
            : CommonValues::BLUE_GOAL_BACK;

        Vec ballToGoal = oppGoal - state.ball.pos;
        float distToGoal = ballToGoal.Length();
        if (distToGoal < 1e-3f) return true;

        Vec dirToGoal = ballToGoal / distToGoal;

        // Check if any opponent is between ball and goal
        for (const Player& p : state.players) {
            if (p.team == player.team) continue;  // Skip teammates
            if (p.carId == player.carId) continue;

            Vec ballToPlayer = p.pos - state.ball.pos;
            float distToPlayer = ballToPlayer.Length();

            // Is this player in the shooting lane?
            if (distToPlayer < distToGoal) {
                float proj = ballToPlayer.Dot(dirToGoal);

                // Player is ahead of ball toward goal
                if (proj > 0.0f && proj < distToGoal) {
                    Vec perpendicular = ballToPlayer - dirToGoal * proj;
                    float perpDist = perpendicular.Length();

                    // Defender is blocking the shot
                    if (perpDist < defenderCheckRadius) {
                        return false;
                    }
                }
            }
        }

        return true;  // No defenders blocking
    }

public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Only on shot events
        if (!player.eventState.shot) return 0.0f;

        // Must be on target
        if (!RLGC::RewardMath::BallIntersectsOpponentGoalMouth(player, state, 2.0f, true, true)) {
            return 0.0f;
        }

        Vec oppGoal = (player.team == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_BACK
            : CommonValues::BLUE_GOAL_BACK;

        // NEW: Check positioning - both player and ball should be closer to goal than opponents
        bool playerCloserToGoal = IsPlayerCloserToGoalThanOpponents(player, state, oppGoal);
        bool ballCloserToGoal = IsBallCloserToGoalThanOpponents(state, oppGoal, player.team);

        // Require at least one positioning advantage
        if (!playerCloserToGoal && !ballCloserToGoal) {
            return 0.0f;  // Not in advantageous position
        }

        // Check if net is open
        if (!IsNetOpen(player, state)) {
            return 0.0f;
        }

        float totalReward = baseReward;
        float distToGoal = (state.ball.pos - oppGoal).Length();

        // Bonus for close range open nets
        float distanceBonus = 1.0f;
        if (distToGoal < 3000.0f) {
            distanceBonus = 1.0f + 0.5f * (1.0f - distToGoal / 3000.0f);  // Up to 1.5x
        }

        // Speed bonus
        float kph = state.ball.vel.Length() * 0.036f;
        float speedBonus = RS_CLAMP(kph / 80.0f, 0.5f, 1.5f);

        totalReward *= distanceBonus * speedBonus;

        // NEW: Out of position bonus - opponent is further from both goal and ball
        if (IsOpponentOutOfPosition(player, state, oppGoal)) {
            totalReward += outOfPositionBonus;

            // Additional bonus for ball velocity toward goal when opponent is out of position
            Vec dirToGoal = (oppGoal - state.ball.pos).Normalized();
            float velTowardGoal = RS_MAX(0.0f, state.ball.vel.Dot(dirToGoal));
            float velBonus = RS_CLAMP(velTowardGoal / 2300.0f, 0.0f, 1.0f);  // Normalize by max speed

            totalReward += velocityToGoalBonus * velBonus;
        }

        return totalReward;
    }
};

// ============================================================================
// ShotQualityReward - NEW
// Rewards shot quality: placement, power, angle
// Encourages taking good shots over dribbling
// 2025-12-23 | Rev 1.1 - Fixed with correct CommonValues references
// ============================================================================
class ShotQualityReward : public Reward {
private:
    float baseReward = 8.0f;

    float GetPlacementQuality(const Player& player, const GameState& state) const {
        Vec oppGoal = (player.team == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_BACK
            : CommonValues::BLUE_GOAL_BACK;

        // Goal corners (harder to save)
        // Using GOAL_WIDTH_FROM_CENTER (half-width from center)
        Vec topLeft = oppGoal + Vec(-CommonValues::GOAL_WIDTH_FROM_CENTER, 0.0f, CommonValues::GOAL_HEIGHT * 0.8f);
        Vec topRight = oppGoal + Vec(CommonValues::GOAL_WIDTH_FROM_CENTER, 0.0f, CommonValues::GOAL_HEIGHT * 0.8f);
        Vec bottomLeft = oppGoal + Vec(-CommonValues::GOAL_WIDTH_FROM_CENTER, 0.0f, CommonValues::GOAL_HEIGHT * 0.2f);
        Vec bottomRight = oppGoal + Vec(CommonValues::GOAL_WIDTH_FROM_CENTER, 0.0f, CommonValues::GOAL_HEIGHT * 0.2f);

        // Project where ball will hit goal plane
        Vec ballVelNorm = state.ball.vel.Normalized();
        float t = fabsf((oppGoal - state.ball.pos).Dot(Vec(0.0f, (player.team == Team::BLUE) ? 1.0f : -1.0f, 0.0f))
            / state.ball.vel.Dot(Vec(0.0f, (player.team == Team::BLUE) ? 1.0f : -1.0f, 0.0f)));
        Vec hitPoint = state.ball.pos + ballVelNorm * (state.ball.vel.Length() * t);

        // Distance to nearest corner
        float minCornerDist = RS_MIN(
            RS_MIN((hitPoint - topLeft).Length(), (hitPoint - topRight).Length()),
            RS_MIN((hitPoint - bottomLeft).Length(), (hitPoint - bottomRight).Length())
        );

        // Closer to corners = better
        float maxCornerDist = CommonValues::GOAL_TOTAL_WIDTH;  // Use full width for normalization
        return RS_CLAMP(1.0f - (minCornerDist / maxCornerDist), 0.3f, 1.0f);
    }

public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.eventState.shot) return 0.0f;
        if (!RLGC::RewardMath::BallIntersectsOpponentGoalMouth(player, state, 2.0f, true, true)) {
            return 0.0f;
        }

        // Power component
        float kph = state.ball.vel.Length() * 0.036f;
        float powerQuality = RS_CLAMP(kph / 100.0f, 0.3f, 1.2f);

        // Placement component
        float placementQuality = GetPlacementQuality(player, state);

        // Angle component - reward shots from difficult angles
        Vec oppGoal = (player.team == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_BACK
            : CommonValues::BLUE_GOAL_BACK;
        Vec ballToGoal = (oppGoal - state.ball.pos).Normalized();
        Vec carToGoal = (oppGoal - player.pos).Normalized();
        float angleAlignment = carToGoal.Dot(ballToGoal);
        float angleQuality = 0.7f + 0.3f * RS_CLAMP(angleAlignment, 0.0f, 1.0f);

        return baseReward * powerQuality * placementQuality * angleQuality;
    }
};

// ============================================================================
// AntiDribblingReward - NEW
// Penalizes slow ball possession near opponent goal
// Encourages shooting instead of dribbling when close to goal
// 2025-12-23 | Rev 1.0
// ============================================================================
class AntiDribblingReward : public Reward {
private:
    float penaltyPerSecond = -0.3f;  // Small continuous penalty
    float minDistToGoal = 4500.0f;   // Only penalize when close to goal
    float maxBallSpeed = 800.0f;     // Ball must be moving slowly
    float possessionRadius = 350.0f; // How close is "possession"

    mutable std::map<uint32_t, float> possessionTime;

public:
    virtual void Reset(const GameState& initialState) override {
        possessionTime.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        Vec oppGoal = (player.team == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_BACK
            : CommonValues::BLUE_GOAL_BACK;

        float distToGoal = (state.ball.pos - oppGoal).Length();
        float ballSpeed = state.ball.vel.Length();
        float distToBall = (player.pos - state.ball.pos).Length();

        // Are we in "dribbling" state near goal?
        bool isDribblingNearGoal = (distToGoal < minDistToGoal)
            && (ballSpeed < maxBallSpeed)
            && (distToBall < possessionRadius);

        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

        if (isDribblingNearGoal) {
            possessionTime[player.carId] += dt;

            // Penalty scales with time spent dribbling
            float timePenalty = RS_MIN(possessionTime[player.carId], 2.0f);  // Cap at 2 seconds
            return penaltyPerSecond * timePenalty * dt;
        }
        else {
            possessionTime[player.carId] = 0.0f;
        }

        return 0.0f;
    }
};

// ============================================================================
// CHAMPION TIER REWARDS
// ============================================================================

// Rewards exploration of advanced flicks (anything other than front/diagonal flips)
class FlickExplorationReward : public Reward {
private:
    mutable std::map<uint32_t, bool> ballWasOnCar;
    mutable std::map<uint32_t, Vec> lastCarForward;
    mutable std::map<uint32_t, Vec> lastCarUp;
    mutable std::map<uint32_t, float> lastCarPitch;
    mutable std::map<uint32_t, float> lastCarYaw;

public:
    virtual void Reset(const GameState& initialState) override {
        ballWasOnCar.clear();
        lastCarForward.clear();
        lastCarUp.clear();
        lastCarPitch.clear();
        lastCarYaw.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        Vec relBallPos = state.ball.pos - player.pos;
        float heightAboveCar = relBallPos.z;
        float horizDist = Vec(relBallPos.x, relBallPos.y, 0).Length();

        // Ball is on car
        bool ballOnCar = (heightAboveCar > 30.0f && heightAboveCar < 150.0f && horizDist < 120.0f);
        bool wasPrev = ballWasOnCar[player.carId];
        ballWasOnCar[player.carId] = ballOnCar;

        // Flick detected: ball was on car, player touched ball, ball no longer on car
        if (wasPrev && player.ballTouchedStep && !ballOnCar) {
            Vec prevForward = lastCarForward[player.carId];
            Vec prevUp = lastCarUp[player.carId];
            Vec curForward = player.rotMat.forward;
            Vec curUp = player.rotMat.up;

            float forwardAlignment = prevForward.Dot(curForward);
            float upAlignment = prevUp.Dot(curUp);

            // Calculate angular change
            float pitchChange = std::abs(player.angVel.x - lastCarPitch[player.carId]);
            float yawChange = std::abs(player.angVel.z - lastCarYaw[player.carId]);

            float ballSpeed = state.ball.vel.Length();
            float reward = 0.0f;

            // Check for standard front flip flick (forward alignment ~1.0, minimal yaw)
            bool isFrontFlip = (forwardAlignment > 0.85f && yawChange < 1.5f && pitchChange > 2.0f);

            // Check for standard diagonal flick (some forward alignment, moderate yaw)
            bool isDiagonalFlip = (forwardAlignment > 0.6f && forwardAlignment <= 0.85f &&
                yawChange > 1.5f && yawChange < 3.5f);

            // Musty flick: backward flip (forwardAlignment < 0)
            bool isMusty = (forwardAlignment < -0.3f && pitchChange > 2.5f);

            // Side flick: mostly yaw rotation, minimal forward alignment
            bool isSideFlick = (std::abs(forwardAlignment) < 0.5f && yawChange > 3.0f);

            // Tornado flick: high yaw + pitch change
            bool isTornado = (yawChange > 4.0f && pitchChange > 3.0f);

            // Upside-down flicks: car inverted during flick
            bool isInverted = (upAlignment < -0.3f);

            // Reward structure
            if (isFrontFlip || isDiagonalFlip) {
                // Minimal/no reward for standard flicks
                reward = 0.5f;
            }
            else if (isMusty) {
                reward = 30.0f * (ballSpeed / 2000.0f); // Scale with ball speed
            }
            else if (isSideFlick) {
                reward = 25.0f * (ballSpeed / 2000.0f);
            }
            else if (isTornado) {
                reward = 35.0f * (ballSpeed / 2000.0f);
            }
            else if (isInverted) {
                reward = 28.0f * (ballSpeed / 2000.0f);
            }
            else {
                // Any other non-standard flick
                reward = 20.0f * (ballSpeed / 2000.0f);
            }

            // Bonus for high-speed flicks
            if (ballSpeed > 2200.0f && !isFrontFlip && !isDiagonalFlip) {
                reward += 10.0f;
            }

            lastCarForward[player.carId] = curForward;
            lastCarUp[player.carId] = curUp;
            lastCarPitch[player.carId] = player.angVel.x;
            lastCarYaw[player.carId] = player.angVel.z;

            return reward;
        }

        // Update tracking
        lastCarForward[player.carId] = player.rotMat.forward;
        lastCarUp[player.carId] = player.rotMat.up;
        lastCarPitch[player.carId] = player.angVel.x;
        lastCarYaw[player.carId] = player.angVel.z;

        return 0.0f;
    }
};

// Chip shot + shoot combination
class ChipShotReward : public Reward {
private:
    mutable std::map<uint32_t, bool> justChipped;

public:
    virtual void Reset(const GameState& initialState) override {
        justChipped.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) {
            justChipped[player.carId] = false;
            return 0.0f;
        }
        
        bool wasChipped = justChipped[player.carId];
        
        // Check if this is a chip (ball goes up without much forward velocity)
        if (!wasChipped && state.ball.vel.z > 500.0f) {
            float horizSpeed = Vec(state.ball.vel.x, state.ball.vel.y, 0).Length();
            if (horizSpeed < 800.0f) {
                // This is a chip
                justChipped[player.carId] = true;
                return 5.0f;
            }
        }
        
        // If we just chipped, now shooting = combo
        if (wasChipped && state.ball.vel.Length() > 1500.0f) {
            return 15.0f; // Chip + shoot combo!
        }
        
        return 0.0f;
    }
};

// Dribble control - encouraging scoring-oriented dribbles
// 2025-12-08 - Rev 3 - Removed invalid PlayerEventState::lastTouch usage.
//                     Uses recent-touch timer for possession, and
//                     rewards long, forward, goal-directed dribbles. (NJH)
class DribbleControlReward : public Reward {
private:
    mutable std::map<uint32_t, float> dribbleTime;
    mutable std::map<uint32_t, Vec>   lastBallVel;
    mutable std::map<uint32_t, float> recentTouchTimer; // seconds since last touch

public:
    virtual void Reset(const GameState& initialState) override {
        dribbleTime.clear();
        lastBallVel.clear();
        recentTouchTimer.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!state.prev)
            return 0.0f;

        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

        // Track recent touches to approximate possession
        if (player.ballTouchedStep) {
            // Consider possession valid for a short window after a touch
            recentTouchTimer[player.carId] = 0.5f; // 0.5s of "we last touched it"
        }
        else {
            float t = recentTouchTimer[player.carId];
            t = RS_MAX(0.0f, t - dt);
            recentTouchTimer[player.carId] = t;
        }

        bool hasPossession = (recentTouchTimer[player.carId] > 0.0f);

        Vec prevBallVel = state.prev->ball.vel;
        Vec curBallVel = state.ball.vel;
        lastBallVel[player.carId] = curBallVel;

        // Opponent goal (target for attack)
        Vec enemyGoal = (player.team == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_BACK
            : CommonValues::BLUE_GOAL_BACK;

        // Ball position relative to car
        Vec rel = state.ball.pos - player.pos;
        float horizDist = Vec(rel.x, rel.y, 0).Length();
        float heightAboveCar = rel.z;

        // "Ball on car" definition
        bool ballOnCar =
            player.isOnGround &&
            heightAboveCar > 30.0f &&
            heightAboveCar < 200.0f &&
            horizDist < 140.0f;

        if (!ballOnCar || !hasPossession) {
            dribbleTime[player.carId] = 0.0f;
            return 0.0f;
        }

        // We are dribbling with (approximate) possession → accumulate dribble time
        dribbleTime[player.carId] += dt;
        float time = dribbleTime[player.carId];

        // Direction toward enemy goal & ball motion alignment
        Vec dirToGoal = (enemyGoal - state.ball.pos).Normalized();
        float curAlign = curBallVel.Normalized().Dot(dirToGoal);
        float prevAlign = prevBallVel.Normalized().Dot(dirToGoal);
        float velImprovement = curAlign - prevAlign; // >0 => more toward goal

        // Only reward dribbles that actually move ball toward goal
        if (curAlign < 0.1f) {
            return 0.0f;
        }

        // Proximity factor: closer to enemy goal → higher threat
        float distToGoal = (enemyGoal - state.ball.pos).Length();
        float goalProx = RS_CLAMP(1.0f - distToGoal / 6000.0f, 0.0f, 1.0f);

        // Time factor: longer successful dribble → more reward (up to a cap)
        float timeFactor = 1.0f + 0.25f * time;      // grows with time
        timeFactor = RS_CLAMP(timeFactor, 1.0f, 3.0f); // cap so it can't go insane

        // Combine contributions:
        //  - strong alignment of ball velocity toward goal (curAlign)
        //  - positive improvement over last frame (velImprovement)
        //  - being closer to goal (goalProx)
        //  - sustaining the dribble (timeFactor)
        float reward =
            (curAlign * 1.2f +
                RS_MAX(0.0f, velImprovement) * 0.8f) *
            timeFactor *
            (0.5f + 0.5f * goalProx);

        return RS_CLAMP(reward, 0.0f, 5.0f);
    }
};



// Ground to air dribble transition
class GroundToAirDribbleReward : public Reward {
private:
    mutable std::map<uint32_t, bool> wasGroundDribbling;
    mutable std::map<uint32_t, int> airDribbleTouchCount;
    mutable std::map<uint32_t, float> lastBallHeight;

public:
    virtual void Reset(const GameState& initialState) override {
        wasGroundDribbling.clear();
        airDribbleTouchCount.clear();
        lastBallHeight.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        Vec relBallPos = state.ball.pos - player.pos;
        Vec ballToCar = player.pos - state.ball.pos;
        float heightAboveCar = relBallPos.z;
        float horizDist = Vec(relBallPos.x, relBallPos.y, 0).Length();
        float ballHeight = state.ball.pos.z;

        // Ball on car detection (ground dribble)
        bool ballOnCar = (heightAboveCar > 30.0f && heightAboveCar < 200.0f &&
            horizDist < 150.0f && player.isOnGround);

        // Ball bouncing up detection
        bool ballBouncingUp = (state.ball.vel.z > 200.0f && ballHeight < 300.0f &&
            ballHeight > 100.0f);

        // Speed matching (within 100 uu/s in XY plane)
        Vec ballVelXY = Vec(state.ball.vel.x, state.ball.vel.y, 0);
        Vec playerVelXY = Vec(player.vel.x, player.vel.y, 0);
        float speedDiff = (ballVelXY - playerVelXY).Length();
        bool speedMatched = (speedDiff < 150.0f); // Slightly more lenient

        bool wasGround = wasGroundDribbling[player.carId];
        bool isAirDribbling = false;
        float reward = 0.0f;

        // Check if currently air dribbling
        if (!player.isOnGround &&
            ballHeight > 150.0f &&
            horizDist < 200.0f &&
            heightAboveCar > -50.0f && heightAboveCar < 250.0f &&
            speedMatched) {
            isAirDribbling = true;
        }

        // Reward transition from ground to air
        if (wasGround && isAirDribbling) {
            reward += 25.0f; // Initial transition reward
        }

        // Reward sustained air dribbling touches
        if (isAirDribbling && player.ballTouchedStep) {
            // Check if it's a "light touch" (not launching the ball away)
            float ballAccel = state.ball.vel.Length() -
                (lastBallHeight[player.carId] > 0 ?
                    std::sqrt(2 * 9.8f * (ballHeight - lastBallHeight[player.carId])) : 0);

            bool isLightTouch = (ballAccel < 500.0f); // Ball doesn't accelerate too much

            if (isLightTouch) {
                int touchCount = airDribbleTouchCount[player.carId];
                airDribbleTouchCount[player.carId] = touchCount + 1;

                // Exponentially reward multiple air touches
                reward += 10.0f * std::min(touchCount + 1, 5); // Cap at 5 touches
            }
            else {
                // Reset if it's a hard touch (shooting)
                airDribbleTouchCount[player.carId] = 0;
            }
        }

        // Reset air dribble count if back on ground
        if (player.isOnGround && !ballOnCar) {
            airDribbleTouchCount[player.carId] = 0;
        }

        // Update state tracking
        wasGroundDribbling[player.carId] = ballOnCar || (ballBouncingUp && speedMatched && player.isOnGround);
        lastBallHeight[player.carId] = ballHeight;

        return reward;
    }
};

// Rewards demolitions in opponent's half
class DemoOffenseReward : public Reward {
private:
    mutable std::map<uint32_t, int> lastDemolishCount;

public:
    virtual void Reset(const GameState& initialState) override {
        lastDemolishCount.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        int currentDemos = player.eventState.demo;
        int lastDemos = lastDemolishCount[player.carId];

        // New demo detected
        if (currentDemos > lastDemos) {
            lastDemolishCount[player.carId] = currentDemos;

            // Determine which half of field based on team
            // Assuming blue team defends negative X, orange defends positive X
            bool isBlueTeam = ((int)player.team == 0);
            float fieldHalf = isBlueTeam ? 1.0f : -1.0f;

            // Check if demo occurred in opponent's half
            if (player.pos.x * fieldHalf > 0.0f) {
                // Scale reward based on how deep in opponent's half
                float depthRatio = std::abs(player.pos.x) / 4096.0f; // Field half-length
                float baseReward = 30.0f;

                // Bonus for demos near opponent's goal
                if (std::abs(player.pos.x) > 3500.0f) {
                    baseReward += 15.0f; // Extra reward for goal-area demos
                }

                return baseReward * depthRatio;
            }
        }

        lastDemolishCount[player.carId] = currentDemos;
        return 0.0f;
    }
};

// ============================================================================
// DemoBumpOutplayReward - ELITE TIER
// Rewards outplaying opponents through strategic demos and bumps
// High-speed demos, bump chains, and positioning punish
// 2025-01-04 | Rev 1.0 | NJH
// ============================================================================

class DemoBumpOutplayReward : public Reward {
private:
    struct OutplayConfig {
        // Demo thresholds
        float minDemoSpeed = 1200.0f;              // Minimum speed for "powerful" demo
        float goodDemoSpeed = 1800.0f;             // Good demo speed
        float greatDemoSpeed = 2300.0f;            // Great demo speed

        // Bump thresholds
        float minBumpSpeed = 800.0f;
        float goodBumpSpeed = 1400.0f;

        // Positioning
        float enemyGoalDistance = 2000.0f;         // Reward demos near enemy goal
        float maxDemoDistance = 6000.0f;           // How far away demo counts

        // Timing
        float demoCooldown = 5.0f;                 // Seconds before reward same opponent again
        float bumpChainWindow = 1.0f;              // Time to chain bumps for bonus
    };

    enum OutplayPhase {
        PHASE_IDLE,
        PHASE_TRACKING,     // Closing in on opponent
        PHASE_CONTACT,      // Made contact
        PHASE_FOLLOWUP      // Chaining hits
    };

    mutable std::map<uint32_t, int> lastDemoCount;
    mutable std::map<uint32_t, int> lastBumpCount;
    mutable std::map<uint32_t, std::map<uint32_t, float>> enemyDemoCooldown;  // carId -> enemyId -> cooldown
    mutable std::map<uint32_t, OutplayPhase> outplayPhase;
    mutable std::map<uint32_t, float> chainTimer;
    mutable std::map<uint32_t, uint32_t> lastHitEnemyId;
    mutable std::map<uint32_t, Vec> enemyPosAtContact;

    OutplayConfig config;

    float ComputeCarCollisionSpeed(const Player& player, const Player& enemy) const {
        // Relative velocity magnitude at collision
        Vec relVel = player.vel - enemy.vel;
        return relVel.Length();
    }

    Vec GetEnemyGoalPosition(Team enemyTeam) const {
        if (enemyTeam == Team::BLUE) {
            return Vec(0.0f, -5120.0f, 321.3875f);
        }
        else {
            return Vec(0.0f, 5120.0f, 321.3875f);
        }
    }

public:
    DemoBumpOutplayReward() : config() {}

    explicit DemoBumpOutplayReward(const OutplayConfig& cfg) : config(cfg) {}

    virtual void Reset(const GameState& initialState) override {
        lastDemoCount.clear();
        lastBumpCount.clear();
        enemyDemoCooldown.clear();
        outplayPhase.clear();
        chainTimer.clear();
        lastHitEnemyId.clear();
        enemyPosAtContact.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Initialize
        if (lastDemoCount.find(player.carId) == lastDemoCount.end()) {
            lastDemoCount[player.carId] = player.eventState.demo;
            lastBumpCount[player.carId] = player.eventState.bump;
            outplayPhase[player.carId] = PHASE_IDLE;
            chainTimer[player.carId] = 0.0f;
            return 0.0f;
        }

        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
        Vec enemyGoalPos = GetEnemyGoalPosition(player.team == Team::BLUE ? Team::ORANGE : Team::BLUE);
        float reward = 0.0f;

        // =====================================================================
        // DEMO REWARD - New demolition detected
        // =====================================================================
        int currentDemos = player.eventState.demo;
        int lastDemos = lastDemoCount[player.carId];

        if (currentDemos > lastDemos) {
            lastDemoCount[player.carId] = currentDemos;

            // Find which enemy was demoed (use state to determine)
            // For now, assume the closest enemy was hit
            const Player* demoed_enemy = nullptr;
            float minDist = 1e9f;

            for (const auto& enemy : state.players) {
                if (enemy.team != player.team) {
                    float dist = player.pos.Dist(enemy.pos);
                    if (dist < minDist && dist < 300.0f) {
                        minDist = dist;
                        demoed_enemy = &enemy;
                    }
                }
            }

            if (demoed_enemy) {
                lastHitEnemyId[player.carId] = demoed_enemy->carId;
                enemyPosAtContact[player.carId] = demoed_enemy->pos;

                // ========== SPEED REWARD ==========
                float carSpeed = player.vel.Length();
                float demoSpeedReward = 0.0f;

                if (carSpeed >= config.greatDemoSpeed) {
                    demoSpeedReward = 1.0f;
                }
                else if (carSpeed >= config.goodDemoSpeed) {
                    float t = (carSpeed - config.goodDemoSpeed) / (config.greatDemoSpeed - config.goodDemoSpeed);
                    demoSpeedReward = 0.6f + 0.4f * t;
                }
                else if (carSpeed >= config.minDemoSpeed) {
                    float t = (carSpeed - config.minDemoSpeed) / (config.goodDemoSpeed - config.minDemoSpeed);
                    demoSpeedReward = 0.3f + 0.3f * t;
                }

                // ========== POSITIONING REWARD ==========
                float distToEnemyGoal = demoed_enemy->pos.Dist(enemyGoalPos);
                float positioningReward = 0.0f;

                if (distToEnemyGoal < config.enemyGoalDistance) {
                    // Demo near enemy goal
                    float depth = 1.0f - (distToEnemyGoal / config.enemyGoalDistance);
                    positioningReward = depth * 0.4f;
                }

                // ========== COOLDOWN CHECK ==========
                float demoReward = (demoSpeedReward * 0.7f + positioningReward * 0.3f) * 50.0f;

                // Apply cooldown so we don't reward same enemy repeatedly
                if (enemyDemoCooldown[player.carId][demoed_enemy->carId] <= 0.0f) {
                    reward = demoReward;
                    enemyDemoCooldown[player.carId][demoed_enemy->carId] = config.demoCooldown;
                }

                outplayPhase[player.carId] = PHASE_CONTACT;
                chainTimer[player.carId] = 0.0f;
            }
        }

        // =====================================================================
        // BUMP REWARD - Bumps as part of outplay chain
        // =====================================================================
        int currentBumps = player.eventState.bump;
        int lastBumps = lastBumpCount[player.carId];

        if (currentBumps > lastBumps) {
            lastBumpCount[player.carId] = currentBumps;

            // Find bumped enemy
            const Player* bumped_enemy = nullptr;
            float minDist = 1e9f;

            for (const auto& enemy : state.players) {
                if (enemy.team != player.team) {
                    float dist = player.pos.Dist(enemy.pos);
                    if (dist < minDist && dist < 400.0f) {
                        minDist = dist;
                        bumped_enemy = &enemy;
                    }
                }
            }

            if (bumped_enemy) {
                float carSpeed = player.vel.Length();
                float bumpSpeedReward = 0.0f;

                if (carSpeed >= config.goodBumpSpeed) {
                    float t = (carSpeed - config.goodBumpSpeed) / 1000.0f;
                    bumpSpeedReward = 0.4f + RS_CLAMP(0.3f * t, 0.0f, 0.3f);
                }
                else if (carSpeed >= config.minBumpSpeed) {
                    float t = (carSpeed - config.minBumpSpeed) / (config.goodBumpSpeed - config.minBumpSpeed);
                    bumpSpeedReward = 0.15f + 0.25f * t;
                }

                float bumpReward = bumpSpeedReward * 20.0f;

                // Bump chain bonus (if chaining hits on same/nearby enemies)
                if (outplayPhase[player.carId] == PHASE_CONTACT && chainTimer[player.carId] < config.bumpChainWindow) {
                    bumpReward *= 1.5f;  // +50% for chaining
                    outplayPhase[player.carId] = PHASE_FOLLOWUP;
                }

                reward += bumpReward;
                chainTimer[player.carId] = 0.0f;
            }
        }

        // Tick down cooldowns
        for (auto& [enemyId, cooldown] : enemyDemoCooldown[player.carId]) {
            cooldown -= dt;
        }

        // Update chain timer
        chainTimer[player.carId] += dt;
        if (chainTimer[player.carId] > config.bumpChainWindow) {
            outplayPhase[player.carId] = PHASE_IDLE;
        }

        return reward;
    }

    void SetMinDemoSpeed(float speed) { config.minDemoSpeed = speed; }
    void SetGoodDemoSpeed(float speed) { config.goodDemoSpeed = speed; }
    void SetMinBumpSpeed(float speed) { config.minBumpSpeed = speed; }
};


// ============================================================================
// DemoChasingReward - ELITE TIER
// Rewards chasing down and intercepting opponent cars at high speed
// Encourages aggressive pursuit and interception mechanics
// 2025-01-04 | Rev 1.0 | NJH
// ============================================================================

class DemoChasingReward : public Reward {
private:
    struct ChasingConfig {
        // Distance thresholds (UU)
        float chasingRangeMin = 500.0f;            // Start chasing within 500 UU
        float chasingRangeMax = 3000.0f;           // Max chasing distance

        // Speed thresholds (UU/s)
        float minChasingSpeed = 1000.0f;           // Minimum speed to chase
        float goodChasingSpeed = 1600.0f;          // Good chase speed
        float greatChasingSpeed = 2000.0f;         // Great chase speed
        float insaneChasingSpeed = 2400.0f;        // Insane chase speed (near max car speed)

        // Interception accuracy
        float maxInterceptionAngle = 0.3927f;      // ±22.5 degrees to intercept vector
        float maxInterceptionDistance = 200.0f;    // How close before "interception"

        // Boost efficiency
        float boostBonusPerSecond = 0.05f;         // Small bonus for maintaining boost
    };

    enum ChasingPhase {
        PHASE_IDLE,
        PHASE_PURSUING,     // Moving toward enemy
        PHASE_INTERCEPTING, // On collision course
        PHASE_CONTACT       // Made contact
    };

    mutable std::map<uint32_t, ChasingPhase> chasingPhase;
    mutable std::map<uint32_t, float> chasingTimer;
    mutable std::map<uint32_t, uint32_t> targetEnemyId;
    mutable std::map<uint32_t, float> boostUptime;

    ChasingConfig config;

    const Player* FindNearestEnemy(const Player& player, const GameState& state) const {
        const Player* nearest = nullptr;
        float minDist = config.chasingRangeMax;

        for (const auto& enemy : state.players) {
            if (enemy.team != player.team) {
                float dist = player.pos.Dist(enemy.pos);
                if (dist < minDist) {
                    minDist = dist;
                    nearest = &enemy;
                }
            }
        }
        return nearest;
    }

    float ComputeInterceptionAngle(const Player& player, const Player& enemy) const {
        // Angle between player's direction and interception vector
        Vec carVel = player.vel;
        Vec toEnemy = (enemy.pos - player.pos);

        if (carVel.Length() < 100.0f || toEnemy.Length() < 10.0f) {
            return 3.14159f;  // 180 degrees (opposite)
        }

        float dotProduct = carVel.Normalized().Dot(toEnemy.Normalized());
        dotProduct = RS_CLAMP(dotProduct, -1.0f, 1.0f);
        return acos(dotProduct);
    }

public:
    DemoChasingReward() : config() {}

    explicit DemoChasingReward(const ChasingConfig& cfg) : config(cfg) {}

    virtual void Reset(const GameState& initialState) override {
        chasingPhase.clear();
        chasingTimer.clear();
        targetEnemyId.clear();
        boostUptime.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Initialize
        if (chasingPhase.find(player.carId) == chasingPhase.end()) {
            chasingPhase[player.carId] = PHASE_IDLE;
            chasingTimer[player.carId] = 0.0f;
            boostUptime[player.carId] = 0.0f;
            return 0.0f;
        }

        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
        float carSpeed = player.vel.Length();
        bool isBootsing = player.boost > 0;

        float reward = 0.0f;

        // =====================================================================
        // PHASE 1: PURSUING - Finding and moving toward enemy
        // =====================================================================
        ChasingPhase& phase = chasingPhase[player.carId];
        float& timer = chasingTimer[player.carId];

        const Player* enemy = FindNearestEnemy(player, state);

        if (!enemy) {
            phase = PHASE_IDLE;
            timer = 0.0f;
            return 0.0f;
        }

        float distToEnemy = player.pos.Dist(enemy->pos);

        if (phase == PHASE_IDLE || phase == PHASE_PURSUING) {
            if (distToEnemy < config.chasingRangeMax && carSpeed >= config.minChasingSpeed) {
                // ========== SPEED REWARD ==========
                float speedReward = 0.0f;

                if (carSpeed >= config.insaneChasingSpeed) {
                    speedReward = 1.0f;
                }
                else if (carSpeed >= config.greatChasingSpeed) {
                    float t = (carSpeed - config.greatChasingSpeed) / (config.insaneChasingSpeed - config.greatChasingSpeed);
                    speedReward = 0.7f + 0.3f * t;
                }
                else if (carSpeed >= config.goodChasingSpeed) {
                    float t = (carSpeed - config.goodChasingSpeed) / (config.greatChasingSpeed - config.goodChasingSpeed);
                    speedReward = 0.4f + 0.3f * t;
                }
                else {
                    float t = (carSpeed - config.minChasingSpeed) / (config.goodChasingSpeed - config.minChasingSpeed);
                    speedReward = 0.15f + 0.25f * t;
                }

                // ========== DISTANCE PENALTY ==========
                float distancePenalty = RS_CLAMP(distToEnemy / config.chasingRangeMax, 0.0f, 1.0f);
                float distanceReward = (1.0f - distancePenalty) * 0.5f;

                // ========== BOOST BONUS ==========
                float boostBonus = 0.0f;
                if (isBootsing) {
                    boostUptime[player.carId] += dt;
                    boostBonus = RS_CLAMP(boostUptime[player.carId] * config.boostBonusPerSecond, 0.0f, 0.2f);
                }
                else {
                    boostUptime[player.carId] = 0.0f;
                }

                reward = (speedReward * 0.6f + distanceReward * 0.3f + boostBonus * 0.1f) * 15.0f;

                phase = PHASE_PURSUING;
                timer = 0.0f;
                targetEnemyId[player.carId] = enemy->carId;
            }
        }

        // =====================================================================
        // PHASE 2: INTERCEPTING - On collision course with high accuracy
        // =====================================================================
        if (phase == PHASE_PURSUING) {
            float interceptionAngle = ComputeInterceptionAngle(player, *enemy);

            if (distToEnemy < config.maxInterceptionDistance &&
                interceptionAngle < config.maxInterceptionAngle &&
                carSpeed >= config.goodChasingSpeed) {

                // ========== INTERCEPTION ACCURACY ==========
                float angleScore = 1.0f - (interceptionAngle / config.maxInterceptionAngle);
                float speedScore = RS_CLAMP((carSpeed - config.goodChasingSpeed) / 800.0f, 0.0f, 1.0f);
                float distScore = 1.0f - (distToEnemy / config.maxInterceptionDistance);

                reward = (angleScore * 0.5f + speedScore * 0.3f + distScore * 0.2f) * 25.0f;

                phase = PHASE_INTERCEPTING;
                timer = 0.0f;
            }
        }

        // =====================================================================
        // PHASE 3: CONTACT - Collision detected
        // =====================================================================
        if (phase == PHASE_INTERCEPTING) {
            if (player.ballTouchedStep || distToEnemy < 150.0f) {
                // Contact made - large reward
                reward = 30.0f;
                phase = PHASE_CONTACT;
                timer = 0.0f;
            }
            // Timeout
            else if (timer > 2.0f) {
                phase = PHASE_PURSUING;
                timer = 0.0f;
            }
        }

        timer += dt;

        return reward;
    }

    void SetChasingRangeMax(float range) { config.chasingRangeMax = range; }
    void SetMinChasingSpeed(float speed) { config.minChasingSpeed = speed; }
    void SetGoodChasingSpeed(float speed) { config.goodChasingSpeed = speed; }
    void SetGreatChasingSpeed(float speed) { config.greatChasingSpeed = speed; }
    void SetInterceptionParams(float angle, float distance) {
        config.maxInterceptionAngle = angle;
        config.maxInterceptionDistance = distance;
    }
};

// Flip reset detection
class FlipResetDetectionReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!state.prev) return 0.0f;

        if (player.isOnGround || state.prev->players[player.index].isOnGround) {
            return 0.0f;
        }
        
        bool hadFlip = state.prev->players[player.index].HasFlipOrJump();
        bool HasFlipOrJump = player.HasFlipOrJump();
        
        // Regained flip in air = flip reset
        if (!hadFlip && HasFlipOrJump) {
            // Check if ball is nearby (likely touched ball for reset)
            float distToBall = (player.pos - state.ball.pos).Length();
            if (distToBall < 300.0f) {
                return 30.0f; // Flip reset!
            }
        }
        
        return 0.0f;
    }
};

// Passing plays - setting up teammates
class PassingPlayReward : public Reward {
private:
    mutable std::map<uint32_t, uint32_t> lastTouchedBy;

public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        uint32_t lastTouch = lastTouchedBy[0]; // Global tracker

        if (player.ballTouchedStep) {
            lastTouchedBy[0] = player.carId;
        }
        
        // If teammate scores after our touch = assist
        if (state.goalScored && lastTouch == player.carId) {
            // Check if a teammate scored (not us)
            for (auto& teammate : state.players) {
                if (teammate.team != player.team) continue;
                if (teammate.carId == player.carId) continue;
                
                if (teammate.ballTouchedStep) {
                    return 25.0f; // Passing assist!
                }
            }
        }
        
        return 0.0f;
    }
};

// Redirect - changing ball direction mid-air
class RedirectReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep || player.isOnGround) return 0.0f;
        if (!state.prev) return 0.0f;

        // Check if ball direction changed significantly
        Vec prevBallDir = state.prev->ball.vel.Normalized();
        Vec curBallDir = state.ball.vel.Normalized();
        
        float dirChange = acos(RS_CLAMP(prevBallDir.Dot(curBallDir), -1.0f, 1.0f));
        
        // Significant direction change = redirect
        if (dirChange > 0.5f) { // ~30+ degrees
            Vec targetGoal = (player.team == Team::BLUE) ?
                CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
            
            Vec ballToGoal = (targetGoal - state.ball.pos).Normalized();
            float alignment = curBallDir.Dot(ballToGoal);
            
            if (alignment > 0.7f) {
                return 20.0f; // Redirect toward goal!
            }
            return 10.0f; // Any redirect
        }
        
        return 0.0f;
    }
};

// ============================================================================
// WALL PINCH REWARD
// Rewards pinching ball against side/back wall to create speed.
// Key: matching ball velocity + sending ball outward + directional control
// ============================================================================
class WallPinchReward : public Reward {
private:
    mutable std::map<uint32_t, float> prevBallSpeed;

    static constexpr float WALL_DISTANCE_THRESHOLD = 200.0f;  // More lenient
    static constexpr float MIN_PINCH_SPEED_GAIN = 500.0f;     // Speed increase needed
    static constexpr float MIN_RESULT_SPEED = 2000.0f;        // Minimum ball speed after
    static constexpr float VELOCITY_MATCH_BONUS = 0.15f;      // Reward for matching ball speed
    static constexpr float BASE_PINCH_REWARD = 0.4f;
    static constexpr float DODGE_BONUS = 0.3f;

public:
    virtual void Reset(const GameState& initialState) override {
        prevBallSpeed.clear();
    }

    bool is_near_wall(const Vec& ballPos) const {
        float distToWallX = RS_MIN(
            abs(ballPos.x - CommonValues::SIDE_WALL_X),
            abs(ballPos.x + CommonValues::SIDE_WALL_X)
        );
        float distToWallY = RS_MIN(
            abs(ballPos.y - CommonValues::BACK_WALL_Y),
            abs(ballPos.y + CommonValues::BACK_WALL_Y)
        );

        float minWallDist = RS_MIN(distToWallX, distToWallY);
        return minWallDist < WALL_DISTANCE_THRESHOLD;
    }

    bool is_ball_moving_away_from_wall(const Vec& ballPos, const Vec& ballVel) const {
        // Check X direction (side walls)
        float distToLeftWall = abs(ballPos.x + CommonValues::SIDE_WALL_X);
        float distToRightWall = abs(ballPos.x - CommonValues::SIDE_WALL_X);

        if (distToLeftWall < distToRightWall) {
            // Closer to left wall, should move right (positive X)
            if (ballVel.x < -100.0f) return false;  // Moving wrong direction
        }
        else {
            // Closer to right wall, should move left (negative X)
            if (ballVel.x > 100.0f) return false;
        }

        // Check Y direction (back walls)
        float distToBackY = abs(ballPos.y - CommonValues::BACK_WALL_Y);
        float distToOwnY = abs(ballPos.y + CommonValues::BACK_WALL_Y);

        if (distToBackY < distToOwnY) {
            // Near back wall, should move toward own goal
            if (ballVel.y > 100.0f) return false;
        }
        else {
            // Near own goal line, should move toward back wall
            if (ballVel.y < -100.0f) return false;
        }

        return true;
    }

    float compute_velocity_match_bonus(const Player& player, const GameState& state) const {
        // Reward matching ball velocity (critical for pinch setup)
        float playerSpeed = player.vel.Length();
        float ballSpeed = state.ball.vel.Length();

        // Perfect match = 5% of ball speed as bonus
        float speedDiff = abs(playerSpeed - ballSpeed);
        if (speedDiff < 200.0f) {
            // Close match
            return VELOCITY_MATCH_BONUS;
        }
        else if (speedDiff < 500.0f) {
            // Partial match
            return VELOCITY_MATCH_BONUS * 0.5f;
        }

        return 0.0f;
    }

    float compute_goal_direction_multiplier(const Player& player, const GameState& state) const {
        // Get opponent goal position
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_LINE_CENTER :
            CommonValues::BLUE_GOAL_LINE_CENTER;

        // Direction from ball to opponent goal
        Vec toGoal = opponentGoal - state.ball.pos;
        Vec goalDir = toGoal.Normalized();

        // Ball velocity direction
        float ballSpeed = state.ball.vel.Length();
        if (ballSpeed < 100.0f) return 1.0f;  // Stationary, no multiplier

        Vec ballDir = state.ball.vel / ballSpeed;

        // Dot product: how aligned is ball velocity with goal direction?
        float alignment = ballDir.Dot(goalDir);

        // 2.0x if directly toward goal (alignment > 0.7)
        if (alignment > 0.7f) {
            return 2.0f;
        }
        // 1.5x if general direction toward goal (alignment > 0.3)
        else if (alignment > 0.3f) {
            return 1.5f;
        }
        // 1.0x if neutral or wrong direction
        else {
            return 1.0f;
        }
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep || !state.prev) return 0.0f;

        // Check if ball is near wall
        if (!is_near_wall(state.ball.pos)) {
            prevBallSpeed[player.carId] = state.ball.vel.Length();
            return 0.0f;
        }

        float prevSpeed = prevBallSpeed[player.carId];
        float curSpeed = state.ball.vel.Length();
        float speedGain = curSpeed - prevSpeed;

        // Detect pinch: significant speed increase
        if (speedGain < MIN_PINCH_SPEED_GAIN || curSpeed < MIN_RESULT_SPEED) {
            prevBallSpeed[player.carId] = curSpeed;
            return 0.0f;
        }

        // Validate ball is moving AWAY from wall
        if (!is_ball_moving_away_from_wall(state.ball.pos, state.ball.vel)) {
            prevBallSpeed[player.carId] = curSpeed;
            return 0.0f;
        }

        // Successful wall pinch
        float reward = BASE_PINCH_REWARD;

        // Bonus for velocity matching
        reward += compute_velocity_match_bonus(player, state);

        // Bonus for using dodge/flip
        if (player.isFlipping) {
            reward += DODGE_BONUS;
        }

        // Multiplier for shooting toward opponent goal
        float goalMultiplier = compute_goal_direction_multiplier(player, state);
        reward *= goalMultiplier;

        prevBallSpeed[player.carId] = curSpeed;
        return reward;
    }
};

// ============================================================================
// GROUND PINCH REWARD
// Rewards pinching ball on ground between car and opponent/wall.
// Requires: low car, matching speed, directional control, explosive power
// ============================================================================
class GroundPinchReward : public Reward {
private:
    mutable std::map<uint32_t, float> prevBallSpeed;

    static constexpr float GROUND_HEIGHT_THRESHOLD = 150.0f;    // Ball near ground
    static constexpr float MIN_PINCH_SPEED_GAIN = 600.0f;       // Need good speed
    static constexpr float MIN_RESULT_SPEED = 2200.0f;
    static constexpr float VELOCITY_MATCH_BONUS = 0.15f;
    static constexpr float BASE_PINCH_REWARD = 0.5f;
    static constexpr float DODGE_BONUS = 0.35f;

public:
    virtual void Reset(const GameState& initialState) override {
        prevBallSpeed.clear();
    }

    bool is_ball_on_ground(const Vec& ballPos) const {
        return ballPos.z < GROUND_HEIGHT_THRESHOLD;
    }

    float compute_velocity_match_bonus(const Player& player, const GameState& state) const {
        float playerSpeed = player.vel.Length();
        float ballSpeed = state.ball.vel.Length();
        float speedDiff = abs(playerSpeed - ballSpeed);

        if (speedDiff < 200.0f) {
            return VELOCITY_MATCH_BONUS;
        }
        else if (speedDiff < 500.0f) {
            return VELOCITY_MATCH_BONUS * 0.5f;
        }

        return 0.0f;
    }

    float compute_goal_direction_multiplier(const Player& player, const GameState& state) const {
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_LINE_CENTER :
            CommonValues::BLUE_GOAL_LINE_CENTER;

        Vec toGoal = opponentGoal - state.ball.pos;
        Vec goalDir = toGoal.Normalized();

        float ballSpeed = state.ball.vel.Length();
        if (ballSpeed < 100.0f) return 1.0f;

        Vec ballDir = state.ball.vel / ballSpeed;
        float alignment = ballDir.Dot(goalDir);

        if (alignment > 0.7f) {
            return 2.0f;
        }
        else if (alignment > 0.3f) {
            return 1.5f;
        }
        else {
            return 1.0f;
        }
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep || !state.prev) return 0.0f;

        if (!is_ball_on_ground(state.ball.pos)) {
            prevBallSpeed[player.carId] = state.ball.vel.Length();
            return 0.0f;
        }

        float prevSpeed = prevBallSpeed[player.carId];
        float curSpeed = state.ball.vel.Length();
        float speedGain = curSpeed - prevSpeed;

        // Detect pinch
        if (speedGain < MIN_PINCH_SPEED_GAIN || curSpeed < MIN_RESULT_SPEED) {
            prevBallSpeed[player.carId] = curSpeed;
            return 0.0f;
        }

        // Successful ground pinch
        float reward = BASE_PINCH_REWARD;

        reward += compute_velocity_match_bonus(player, state);

        if (player.isFlipping) {
            reward += DODGE_BONUS;
        }

        // Multiplier for shooting toward opponent goal
        float goalMultiplier = compute_goal_direction_multiplier(player, state);
        reward *= goalMultiplier;

        prevBallSpeed[player.carId] = curSpeed;
        return reward;
    }
};

// ============================================================================
// CEILING PINCH REWARD
// Rewards pinching ball against ceiling for aerial plays.
// Hardest pinch - requires ceiling control + matching speed + flip
// ============================================================================
class CeilingPinchReward : public Reward {
private:
    mutable std::map<uint32_t, float> prevBallSpeed;

    static constexpr float CEILING_HEIGHT_THRESHOLD = 4050.0f;  // Near ceiling
    static constexpr float MIN_PINCH_SPEED_GAIN = 700.0f;       // Highest bar
    static constexpr float MIN_RESULT_SPEED = 2400.0f;
    static constexpr float VELOCITY_MATCH_BONUS = 0.2f;         // Higher bonus for aerial
    static constexpr float BASE_PINCH_REWARD = 0.6f;            // Higher base
    static constexpr float DODGE_BONUS = 0.4f;                  // Higher dodge bonus

public:
    virtual void Reset(const GameState& initialState) override {
        prevBallSpeed.clear();
    }

    bool is_ball_on_ceiling(const Vec& ballPos) const {
        return ballPos.z > CEILING_HEIGHT_THRESHOLD;
    }

    float compute_velocity_match_bonus(const Player& player, const GameState& state) const {
        float playerSpeed = player.vel.Length();
        float ballSpeed = state.ball.vel.Length();
        float speedDiff = abs(playerSpeed - ballSpeed);

        if (speedDiff < 150.0f) {
            // Tight match needed for ceiling
            return VELOCITY_MATCH_BONUS;
        }
        else if (speedDiff < 400.0f) {
            return VELOCITY_MATCH_BONUS * 0.5f;
        }

        return 0.0f;
    }

    float compute_goal_direction_multiplier(const Player& player, const GameState& state) const {
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_LINE_CENTER :
            CommonValues::BLUE_GOAL_LINE_CENTER;

        Vec toGoal = opponentGoal - state.ball.pos;
        Vec goalDir = toGoal.Normalized();

        float ballSpeed = state.ball.vel.Length();
        if (ballSpeed < 100.0f) return 1.0f;

        Vec ballDir = state.ball.vel / ballSpeed;
        float alignment = ballDir.Dot(goalDir);

        if (alignment > 0.7f) {
            return 2.0f;
        }
        else if (alignment > 0.3f) {
            return 1.5f;
        }
        else {
            return 1.0f;
        }
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep || !state.prev) return 0.0f;

        if (!is_ball_on_ceiling(state.ball.pos)) {
            prevBallSpeed[player.carId] = state.ball.vel.Length();
            return 0.0f;
        }

        float prevSpeed = prevBallSpeed[player.carId];
        float curSpeed = state.ball.vel.Length();
        float speedGain = curSpeed - prevSpeed;

        // Detect pinch
        if (speedGain < MIN_PINCH_SPEED_GAIN || curSpeed < MIN_RESULT_SPEED) {
            prevBallSpeed[player.carId] = curSpeed;
            return 0.0f;
        }

        // Successful ceiling pinch
        float reward = BASE_PINCH_REWARD;

        reward += compute_velocity_match_bonus(player, state);

        if (player.isFlipping) {
            reward += DODGE_BONUS;
        }

        // Multiplier for shooting toward opponent goal
        float goalMultiplier = compute_goal_direction_multiplier(player, state);
        reward *= goalMultiplier;

        prevBallSpeed[player.carId] = curSpeed;
        return reward;
    }
};

// ============================================================================
// GRAND CHAMPION TIER REWARDS
// ============================================================================

// Advanced bounce dribbles - maintaining control on bounce
class BounceDribbleReward : public Reward {
private:
    mutable std::map<uint32_t, int> bounceTouchCount;
    mutable std::map<uint32_t, float> lastBounceTime;

public:
    virtual void Reset(const GameState& initialState) override {
        bounceTouchCount.clear();
        lastBounceTime.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!state.prev) return 0.0f;
        
        // Detect ball bounce (ball Z velocity changes from negative to positive)
        bool ballBounced = (state.prev->ball.vel.z < -200.0f && state.ball.vel.z > 0.0f);
        
        if (ballBounced && player.ballTouchedStep) {
            int& count = bounceTouchCount[player.carId];
            count++;
            lastBounceTime[player.carId] = 1.0f;
            
            if (count >= 2) {
                return 15.0f; // Bounce dribble mastery!
            }
            return 5.0f;
        }
        
        // Reset if too much time passed
        if (lastBounceTime[player.carId] > 0.0f) {
            if (!player.ballTouchedStep) {
                bounceTouchCount[player.carId] = 0;
            }
        }
        
        return 0.0f;
    }
};




// ============================================================================
// DEPRECATED: OLD AirDribbleBumpReward - REPLACED BY NEW AIRDRIBBLEBUMPREWARD.H
// ============================================================================
// This implementation is NO LONGER USED. It has been replaced by a new
// AirDribbleBumpReward class in a separate file (AirDribbleBumpReward.h).
//
// Issues with this old implementation:
// - Did not properly validate ball trajectory toward goal
// - Had thread-safe state management issues
// - No BallPrediction.h usage for trajectory validation
// - Overly simplistic reward model
//
// NEW IMPLEMENTATION FEATURES (in AirDribbleBumpReward.h):
// - Uses BallPrediction.h for proper trajectory validation
// - Validates ball will intersect goal mouth before bouncing
// - Thread-safe per-player state tracking
// - Proper multiplier system (trajectory quality × timing bonus × boost efficiency)
// - Wraps in ZeroSumReward for competitive balance
// ============================================================================
//
// CODE BELOW IS DISABLED - DO NOT EDIT
//
// class AirDribbleBumpReward_DEPRECATED : public Reward {
// private:
//     struct PlayerState {
//         bool isAirDribbling = false;
//         int airTouchCount = 0;
//         float airDribbleStartTime = 0.0f;
//         bool hasBumpedDuringDribble = false;
//         bool hasPositionedInFront = false;
//         bool rewardPaid = false;
//         float lastTouchTime = 0.0f;
//     };
//
//     mutable std::map<uint32_t, PlayerState> playerStates;
//     mutable float gameTime = 0.0f;
//
//     struct Config {
//         float minAirTouchCount = 2.0f;
//         float minBallHeight = 200.0f;
//         float maxDistToBall = 300.0f;
//         float touchTimeout = 1.5f;
//         float bumpProximity = 250.0f;
//         float minBumpSpeed = 1800.0f;
//         float inFrontThreshold = 200.0f;
//         float maxLateralDistance = 800.0f;
//         float airDribbleStartReward = 1.0f;
//         float bumpDuringDribbleReward = 15.0f;
//         float demoDuringDribbleReward = 25.0f;
//         float positionInFrontReward = 5.0f;
//         float maxSequenceDuration = 5.0f;
//     };
//
//     Config config;
//
//     bool IsAirDribbling(const Player& player, const GameState& state, PlayerState& ps) const;
//     float CheckBumpOrDemo(const Player& player, const GameState& state) const;
//     bool IsPositionedInFrontOfBall(const Player& player, const GameState& state) const;
//
// public:
//     AirDribbleBumpReward() : config() {}
//     explicit AirDribbleBumpReward(const Config& cfg) : config(cfg) {}
//
//     virtual void Reset(const GameState& initialState) override {
//         playerStates.clear();
//         gameTime = 0.0f;
//     }
//
//     virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
//         // Implementation disabled - use new AirDribbleBumpReward.h instead
//         return 0.0f;
//     }
// };
// ============================================================================
// END DEPRECATED AirDribbleBumpReward_DEPRECATED

// ============================================================================
// DEPRECATED: AirBumpDemoReward - REPLACED BY NEW AIRDRIBBLEBUMPREWARD.H
// ============================================================================
// This was an earlier attempt at a general "bump/demo while airborne" reward.
// NOT USED - DISABLED
//
// Issues with this implementation:
// - Did not validate ball trajectory toward goal (no BallPrediction.h usage)
// - Overly simple: reward for ANY bump/demo while in air (no goal context)
// - No trajectory validation for ground bounces
// - Not weighted toward actual goal-scoring opportunities
//
// REPLACED WITH: AirDribbleBumpReward.h (new implementation)
// - Uses BallPrediction.h for proper trajectory validation
// - Validates ball will intersect goal mouth before bouncing
// - Requires air dribble setup toward goal (context-aware)
// - Thread-safe per-player state tracking
// ============================================================================
//
// CODE BELOW IS DISABLED - DO NOT EDIT
//
// class AirBumpDemoReward : public Reward {
// private:
//     mutable std::map<uint32_t, float> airborneTime;
//     mutable std::map<uint32_t, bool>  paidThisAir;
//     const float MIN_AIRBORNE_SEC = 1.0f;
//     const float BUMP_PROXIMITY_RADIUS = 1200.0f;
//     const float CONTACT_RADIUS = 350.0f;
//     const float BASE_REWARD = 20.0f;
//     const float BUMP_MULT = 0.75f;
//
// public:
//     virtual void Reset(const GameState& initialState) override {
//         airborneTime.clear();
//         paidThisAir.clear();
//     }
//
//     virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
//         // Implementation disabled - use new AirDribbleBumpReward.h instead
//         return 0.0f;
//     }
// };
// ============================================================================
// END DEPRECATED AirBumpDemoReward

// Ceiling ground pinch - pinching from ceiling to ground
class CeilingGroundPinchReward : public Reward {
private:
    mutable std::map<uint32_t, bool> ballWasAtCeiling;

public:
    virtual void Reset(const GameState& initialState) override {
        ballWasAtCeiling.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        bool ballHighNow = (state.ball.pos.z > 1500.0f);
        bool wasHigh = ballWasAtCeiling[player.carId];
        ballWasAtCeiling[player.carId] = ballHighNow;
        
        if (wasHigh && !ballHighNow && player.ballTouchedStep) {
            // Ball came down from ceiling, we touched it
            float ballSpeed = state.ball.vel.Length();
            
            if (ballSpeed > 3000.0f) {
                return 35.0f; // Ceiling pinch!
            }
        }
        
        return 0.0f;
    }
};

// Musty flick (wall, ceiling variations)
class MustyVariationReward : public Reward {
private:
    mutable std::map<uint32_t, bool> wasOnWallOrCeiling;

public:
    virtual void Reset(const GameState& initialState) override {
        wasOnWallOrCeiling.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Check if on wall or ceiling
        bool onSpecialSurface = false;
        
        // On ceiling
        if (player.pos.z > (CommonValues::CEILING_Z - 100.0f)) {
            onSpecialSurface = true;
        }
        
        // On wall
        float distToWallX = RS_MIN(
            abs(player.pos.x - CommonValues::SIDE_WALL_X),
            abs(player.pos.x + CommonValues::SIDE_WALL_X)
        );
        if (distToWallX < 100.0f) {
            onSpecialSurface = true;
        }
        
        bool wasOnSurface = wasOnWallOrCeiling[player.carId];
        wasOnWallOrCeiling[player.carId] = onSpecialSurface;
        
        // Musty-style flick from special surface
        if (wasOnSurface && player.ballTouchedStep && !onSpecialSurface) {
            float ballSpeed = state.ball.vel.Length();
            if (ballSpeed > 2000.0f) {
                return 40.0f; // Advanced musty variation!
            }
        }
        
        return 0.0f;
    }
};

// Perfecting basics - consistency in fundamental mechanics
class ConsistencyReward : public Reward {
private:
    mutable std::map<uint32_t, std::deque<bool>> recentTouches;

public:
    virtual void Reset(const GameState& initialState) override {
        recentTouches.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        auto& touches = recentTouches[player.carId];
        touches.push_back(player.ballTouchedStep);
        
        // Keep last 20 steps
        if (touches.size() > 20) {
            touches.pop_front();
        }
        
        if (touches.size() >= 20) {
            int touchCount = 0;
            for (bool touched : touches) {
                if (touched) touchCount++;
            }
            
            // High touch consistency
            float consistency = (float)touchCount / 20.0f;
            if (consistency > 0.7f) {
                return 5.0f; // Very consistent ball touches
            }
        }
        
        return 0.0f;
    }
};

// ============================================================================
// ADVANCED MECHANICS REWARDS (NEW)
// ============================================================================

// Enhanced Double Tap - with surface detection
class EnhancedDoubleTapReward : public Reward {
private:
    struct TouchData {
        uint32_t carId;
        float time;
        Vec ballPos;
        Vec ballVel;
        bool wasAerial;
    };
    
    mutable std::vector<TouchData> recentTouches;
    mutable std::map<uint32_t, bool> lastBallWallContact;
    mutable std::map<uint32_t, bool> lastBallBackboardContact;
    mutable std::map<uint32_t, bool> lastBallCeilingContact;
    mutable float currentTime;
    
    const float TOUCH_TIMEOUT = 3.0f;
    const float MIN_AERIAL_HEIGHT = 300.0f;
    
    bool IsNearWall(const Vec& pos) const {
        return abs(pos.x) > CommonValues::SIDE_WALL_X - 300.0f;
    }
    
    bool IsNearBackboard(const Vec& pos) const {
        return (abs(pos.y) > CommonValues::BACK_WALL_Y - 300.0f && pos.z > 1952.0f - 300.0f);
    }
    
    bool IsNearCeiling(const Vec& pos) const {
        return pos.z > CommonValues::CEILING_Z - 300.0f;
    }
    
    bool IsGoal(const Vec& pos, Team team) const {
        float targetY = (team == Team::BLUE) ? CommonValues::BACK_WALL_Y : -CommonValues::BACK_WALL_Y;
        return (abs(pos.x) < 893.0f && 
                pos.z < 642.0f && 
                ((team == Team::BLUE && pos.y > targetY) || (team == Team::ORANGE && pos.y < targetY)));
    }

public:
    virtual void Reset(const GameState& initialState) override {
        recentTouches.clear();
        lastBallWallContact.clear();
        lastBallBackboardContact.clear();
        lastBallCeilingContact.clear();
        currentTime = 0.0f;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        currentTime += state.deltaTime;
        float reward = 0.0f;
        
        // Check if ball touched wall/backboard/ceiling
        bool ballWallContact = IsNearWall(state.ball.pos);
        bool ballBackboardContact = IsNearBackboard(state.ball.pos);
        bool ballCeilingContact = IsNearCeiling(state.ball.pos);
        
        // Detect if player just touched ball
        float distToBall = (state.ball.pos - player.pos).Length();
        bool playerTouchingBall = player.ballTouchedStep;
        
        if (playerTouchingBall) {
            bool isAerial = !player.isOnGround && player.pos.z > MIN_AERIAL_HEIGHT;
            
            // Check if this is second touch in double tap sequence
            if (!recentTouches.empty()) {
                TouchData& lastTouch = recentTouches.back();
                
                // Same player, within timeout
                if (lastTouch.carId == player.carId && 
                    (currentTime - lastTouch.time) < TOUCH_TIMEOUT) {
                    
                    bool surfaceContact = false;
                    float surfaceBonus = 1.0f;
                    
                    // Check which surface was contacted
                    if (ballBackboardContact || lastBallBackboardContact[player.carId]) {
                        surfaceContact = true;
                        surfaceBonus = 2.5f;  // Backboard most valuable
                    } else if (ballWallContact || lastBallWallContact[player.carId]) {
                        surfaceContact = true;
                        surfaceBonus = 2.0f;
                    } else if (ballCeilingContact || lastBallCeilingContact[player.carId]) {
                        surfaceContact = true;
                        surfaceBonus = 2.2f;
                    }
                    
                    if (surfaceContact) {
                        // Check if ball going toward opponent goal
                        Vec targetGoal = (player.team == Team::BLUE) ?
                            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
                        Vec toGoal = (targetGoal - state.ball.pos).Normalized();
                        float towardGoal = state.ball.vel.Normalized().Dot(toGoal);
                        
                        if (towardGoal > 0.3f) {
                            // Base double tap reward
                            reward += 0.8f * surfaceBonus;
                            
                            // Bonus for aerial second touch
                            if (isAerial) {
                                reward += 0.5f;
                            }
                            
                            // Bonus for both touches being aerial
                            if (isAerial && lastTouch.wasAerial) {
                                reward += 0.7f;
                            }
                            
                            // Bonus for speed
                            float speed = state.ball.vel.Length();
                            reward += RS_MIN(speed / 2300.0f, 1.0f) * 0.4f;
                            
                            // Major bonus if it scores
                            Team oppTeam = (player.team == Team::BLUE) ? Team::ORANGE : Team::BLUE;
                            if (IsGoal(state.ball.pos, oppTeam)) {
                                reward += 3.0f * surfaceBonus;
                            }
                        }
                    }
                }
            }
            
            // Record this touch
            TouchData touch;
            touch.carId = player.carId;
            touch.time = currentTime;
            touch.ballPos = state.ball.pos;
            touch.ballVel = state.ball.vel;
            touch.wasAerial = isAerial;
            recentTouches.push_back(touch);
            
            // Clean old touches
            recentTouches.erase(
                std::remove_if(recentTouches.begin(), recentTouches.end(),
                    [this](const TouchData& t) {
                        return (currentTime - t.time) > TOUCH_TIMEOUT;
                    }),
                recentTouches.end()
            );
        }
        
        // Update surface contact tracking
        lastBallWallContact[player.carId] = ballWallContact;
        lastBallBackboardContact[player.carId] = ballBackboardContact;
        lastBallCeilingContact[player.carId] = ballCeilingContact;

        return reward;
    }
};

// ============================================================================
// UnifiedDoubleTapReward — Rev 1.0 (2026-02-24, NJH)
//
// Rewards aerial double-tap mechanics: aerial first touch → ball contacts
// backboard/sidewall/corner → aerial rebound touch WITHOUT landing on the
// floor between first and finish touch.
//
// Surfaces: backboard (Y-axis ±5120), side wall (X-axis ±4096), corner (both).
// Does NOT cover ceiling (separate skill).
//
// Architecture:
//   PreStep() computes all state transitions + pending rewards (once per step).
//   GetReward() is read-only — consumes pending rewards from PreStep map.
//   Per-player state in std::unordered_map<uint32_t, DTState> keyed by carId.
//
// BallPred::Cache uses:
//   1. First touch:      FindPredictedSurface() — classify surface + get rebound pos
//   2. AWAITING_FINISH:  per-frame proximity shaping toward predicted rebound pos
//   3. Finish touch:     trajectory quality multiplier (0.7x–1.5x) via AtTime(0.75s)
//
// Fixes timer inflation in AdvancedDoubleTapReward + EnhancedDoubleTapReward:
//   phaseTimer advances in PreStep() once per step, not in GetReward() once per player.
//
// Floor-touch invalidation: isOnGround && pos.z < FLOOR_Z_THRESHOLD.
//   This correctly handles RocketSim where isOnGround=true on wall AND floor.
// ============================================================================
class UnifiedDoubleTapReward : public Reward {
private:
    enum class DTPhase   : uint8_t { IDLE, AWAITING_SURFACE, AWAITING_FINISH };
    enum class DTSurface : uint8_t { NONE, BACKBOARD, SIDEWALL, CORNER };

    struct DTState {
        DTPhase   phase        = DTPhase::IDLE;
        DTSurface surface      = DTSurface::NONE;
        float     phaseTimer   = 0.0f;
        Vec       reboundPos   = {};
        bool      reboundKnown = false;
        float     posReward    = 0.0f;    // per-frame positioning shaping; set in PreStep
        float     finishReward = 0.0f;    // finish touch reward; set in PreStep, consumed in GetReward
    };

    std::unordered_map<uint32_t, DTState> _states;
    BallPred::Cache _ballCache;

    // Surface proximity
    static constexpr float SURFACE_DIST       = 300.0f;   // UU — ball near wall = surface contact
    static constexpr float FLOOR_Z_THRESHOLD  = 100.0f;   // Z below this = player on floor (not wall)
    static constexpr float MIN_FLIGHT_TIME    = 0.1f;     // min sec in AWAITING_SURFACE before checking contact

    // Prediction
    static constexpr float PRED_HORIZON       = 2.0f;     // max lookahead for surface detection (sec)
    static constexpr float POST_BOUNCE_SEC    = 0.35f;    // sec after predicted bounce to sample rebound pos

    // Timeouts
    static constexpr float MAX_FLIGHT_TO_SURFACE = 2.0f;  // AWAITING_SURFACE phase timeout
    static constexpr float MAX_SURFACE_TO_FINISH = 1.8f;  // AWAITING_FINISH phase timeout

    // Positioning shaping (per-frame, AWAITING_FINISH)
    static constexpr float PROX_REWARD_PER_SEC = 0.8f;   // reward/sec at dist=0
    static constexpr float PROX_DIST_SCALE     = 900.0f; // exponential falloff (UU)

    // Finish reward
    static constexpr float BASE_FINISH_REWARD  = 3.0f;
    static constexpr float SPEED_BONUS_MAX     = 1.0f;   // added at full ball speed
    static constexpr float ALIGN_BONUS_MAX     = 1.0f;   // added at full toward-goal alignment
    static constexpr float SURF_MULT_BACKBOARD = 1.5f;
    static constexpr float SURF_MULT_CORNER    = 1.3f;
    static constexpr float SURF_MULT_SIDEWALL  = 1.1f;

    // Trajectory multiplier (BallPred at finish touch)
    static constexpr float TRAJ_CHECK_TIME = 0.75f;
    static constexpr float TRAJ_SCALE      = 1200.0f;
    static constexpr float TRAJ_MIN_FACTOR = -0.3f;
    static constexpr float TRAJ_MAX_FACTOR =  0.5f;

    // Minimum elapsed time in AWAITING_FINISH before a finish touch is recognized.
    // Prevents pinch contacts (ball barely left the wall) from scoring as double-taps.
    static constexpr float MIN_FINISH_DELAY = 0.25f;

    bool IsOnFloor(const Player& player) const {
        return player.isOnGround && player.pos.z < FLOOR_Z_THRESHOLD;
    }

    DTSurface FindPredictedSurface(int& outBounceSlice) const {
        outBounceSlice = -1;
        for (int i = 0; i < BallPred::NUM_SLICES; i++) {
            const auto& sl = _ballCache.slices[i];
            if (sl.t > PRED_HORIZON) break;
            bool newWall = sl.hitWall      && (i == 0 || !_ballCache.slices[i-1].hitWall);
            bool newBack = sl.hitBackboard && (i == 0 || !_ballCache.slices[i-1].hitBackboard);
            if (newWall || newBack) {
                outBounceSlice = i;
                bool wallNear = false, backNear = false;
                int  jEnd = RS_MIN(BallPred::NUM_SLICES - 1, i + 2);
                for (int j = i; j <= jEnd; j++) {
                    if (_ballCache.slices[j].hitWall)      wallNear = true;
                    if (_ballCache.slices[j].hitBackboard) backNear = true;
                }
                if (wallNear && backNear) return DTSurface::CORNER;
                if (backNear) return DTSurface::BACKBOARD;
                return DTSurface::SIDEWALL;
            }
        }
        return DTSurface::NONE;
    }

    DTSurface DetectSurfaceContact(const Vec& ballPos) const {
        float distBack = fabsf(fabsf(ballPos.y) - CommonValues::BACK_WALL_Y);
        float distSide = fabsf(fabsf(ballPos.x) - CommonValues::SIDE_WALL_X);
        bool nearBack = distBack < SURFACE_DIST;
        bool nearSide = distSide < SURFACE_DIST;
        if (nearBack && nearSide) return DTSurface::CORNER;
        if (nearBack) return DTSurface::BACKBOARD;
        if (nearSide) return DTSurface::SIDEWALL;
        return DTSurface::NONE;
    }

    float GetSurfaceMultiplier(DTSurface surf) const {
        switch (surf) {
            case DTSurface::BACKBOARD: return SURF_MULT_BACKBOARD;
            case DTSurface::CORNER:   return SURF_MULT_CORNER;
            case DTSurface::SIDEWALL: return SURF_MULT_SIDEWALL;
            default: return 1.0f;
        }
    }

public:
    virtual void Reset(const GameState&) override {
        _states.clear();
    }

    virtual void PreStep(const GameState& state) override {
        _ballCache.Update(state);
        float dt = state.deltaTime > 0.0f ? state.deltaTime : (1.0f / 120.0f);

        for (const auto& player : state.players) {
            uint32_t  id = player.carId;
            DTState&  s  = _states[id];  // default-constructs IDLE if absent

            // Reset per-step pending rewards
            s.posReward    = 0.0f;
            s.finishReward = 0.0f;

            // ---- IDLE: watch for aerial first touch ----
            if (s.phase == DTPhase::IDLE) {
                if (player.ballTouchedStep && !IsOnFloor(player)) {
                    int bounceSlice = -1;
                    s.surface      = _ballCache.valid ? FindPredictedSurface(bounceSlice) : DTSurface::NONE;
                    s.reboundKnown = false;
                    if (bounceSlice >= 0 && _ballCache.valid) {
                        float bounceTime = _ballCache.slices[bounceSlice].t;
                        s.reboundPos   = _ballCache.AtTime(bounceTime + POST_BOUNCE_SEC).pos;
                        s.reboundKnown = true;
                    }
                    s.phase      = DTPhase::AWAITING_SURFACE;
                    s.phaseTimer = 0.0f;
                }
                continue;
            }

            // Non-IDLE: advance timer; floor touch invalidates entire sequence
            s.phaseTimer += dt;
            if (IsOnFloor(player)) {
                s.phase = DTPhase::IDLE;
                continue;
            }

            // ---- AWAITING_SURFACE ----
            if (s.phase == DTPhase::AWAITING_SURFACE) {
                if (s.phaseTimer > MAX_FLIGHT_TO_SURFACE) {
                    s.phase = DTPhase::IDLE;
                    continue;
                }
                // Reactive surface detection (skip first MIN_FLIGHT_TIME to avoid false positives)
                if (s.phaseTimer > MIN_FLIGHT_TIME) {
                    DTSurface reactSurf = DetectSurfaceContact(state.ball.pos);
                    if (reactSurf != DTSurface::NONE) {
                        if (s.surface == DTSurface::NONE) s.surface = reactSurf;
                        if (!s.reboundKnown && _ballCache.valid) {
                            s.reboundPos   = _ballCache.AtTime(POST_BOUNCE_SEC).pos;
                            s.reboundKnown = true;
                        }
                        s.phase      = DTPhase::AWAITING_FINISH;
                        s.phaseTimer = 0.0f;
                    }
                }
                // Pre-positioning shaping: nudge bot toward predicted rebound before ball hits surface.
                // Half strength vs AWAITING_FINISH since prediction may drift.
                if (s.reboundKnown) {
                    float dist  = (player.pos - s.reboundPos).Length();
                    s.posReward = 0.5f * PROX_REWARD_PER_SEC * expf(-dist / PROX_DIST_SCALE) * dt;
                }
                continue;
            }

            // ---- AWAITING_FINISH ----
            if (s.phase == DTPhase::AWAITING_FINISH) {
                if (s.phaseTimer > MAX_SURFACE_TO_FINISH) {
                    s.phase = DTPhase::IDLE;
                    continue;
                }

                // Per-frame positioning shaping toward predicted rebound
                if (s.reboundKnown) {
                    float dist  = (player.pos - s.reboundPos).Length();
                    s.posReward = PROX_REWARD_PER_SEC * expf(-dist / PROX_DIST_SCALE) * dt;

                    // Velocity-toward-rebound bonus: explicitly trains boosting toward intercept point.
                    // Only counts when meaningfully far (>200 UU) to avoid noise at contact.
                    if (dist > 200.0f) {
                        Vec   toReb     = (s.reboundPos - player.pos).Normalized();
                        float velToward = RS_MAX(0.0f, player.vel.Dot(toReb));
                        s.posReward    += 0.15f * RS_CLAMP(velToward / CommonValues::CAR_MAX_SPEED, 0.0f, 1.0f) * dt;
                    }
                }

                // Aerial rebound touch → issue finish reward
                // Guard: ignore touches arriving < MIN_FINISH_DELAY after wall contact.
                // Bot stays in AWAITING_FINISH and may attempt a valid follow-up touch.
                if (player.ballTouchedStep && !IsOnFloor(player) &&
                    s.phaseTimer >= MIN_FINISH_DELAY) {
                    Vec oppGoal = (player.team == Team::BLUE)
                        ? Vec(0.0f,  CommonValues::BACK_WALL_Y, CommonValues::GOAL_HEIGHT / 2.0f)
                        : Vec(0.0f, -CommonValues::BACK_WALL_Y, CommonValues::GOAL_HEIGHT / 2.0f);

                    float ballSpeed  = state.ball.vel.Length();
                    float speedBonus = SPEED_BONUS_MAX * RS_CLAMP(ballSpeed / CommonValues::BALL_MAX_SPEED, 0.0f, 1.0f);

                    Vec   ballToGoal = (oppGoal - state.ball.pos).Normalized();
                    float alignScore = RS_MAX(0.0f, state.ball.vel.Normalized().Dot(ballToGoal));
                    float alignBonus = ALIGN_BONUS_MAX * alignScore;

                    float reward = (BASE_FINISH_REWARD + speedBonus + alignBonus)
                                 * GetSurfaceMultiplier(s.surface);

                    // Trajectory quality multiplier: 0.7x–1.5x based on ball heading toward goal
                    if (_ballCache.valid) {
                        Vec   futurePos   = _ballCache.AtTime(TRAJ_CHECK_TIME).pos;
                        float curDist     = (state.ball.pos - oppGoal).Length();
                        float futureDist  = (futurePos - oppGoal).Length();
                        float improvement = curDist - futureDist;
                        float trajFactor  = RS_CLAMP(improvement / TRAJ_SCALE, TRAJ_MIN_FACTOR, TRAJ_MAX_FACTOR);
                        reward *= (1.0f + trajFactor);
                    }

                    s.finishReward = reward;
                    s.phase        = DTPhase::IDLE;
                }
            }
        }
    }

    virtual float GetReward(const Player& player, const GameState&, bool) override {
        auto it = _states.find(player.carId);
        if (it == _states.end()) return 0.0f;
        DTState& s   = it->second;
        float    out = s.posReward + s.finishReward;
        s.finishReward = 0.0f;   // consume; posReward zeroed at top of next PreStep
        return out;
    }
};

// Reverse Aerial Reward
class ReverseAerialReward : public Reward {
private:
    struct AerialState {
        bool inReverseAerial;
        float startTime;
        Vec initialForward;
    };
    
    mutable std::map<uint32_t, AerialState> aerialStates;
    mutable float currentTime;
    
    const float MIN_AERIAL_HEIGHT = 250.0f;

public:
    virtual void Reset(const GameState& initialState) override {
        aerialStates.clear();
        currentTime = 0.0f;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        currentTime += state.deltaTime;
        float reward = 0.0f;
        
        // Initialize tracking
        if (aerialStates.find(player.carId) == aerialStates.end()) {
            aerialStates[player.carId] = {false, 0.0f, Vec()};
        }
        
        auto& aerialState = aerialStates[player.carId];
        
        Vec toBall = (state.ball.pos - player.pos).Normalized();
        float forwardDot = player.rotMat.forward.Dot(toBall);
        
        bool isAerial = !player.isOnGround && player.pos.z > MIN_AERIAL_HEIGHT;
        bool facingAway = forwardDot < -0.4f;
        
        // Detect start of reverse aerial
        if (isAerial && facingAway && !aerialState.inReverseAerial) {
            aerialState.inReverseAerial = true;
            aerialState.startTime = currentTime;
            aerialState.initialForward = player.rotMat.forward;
        }
        
        // Check if still in reverse aerial
        if (aerialState.inReverseAerial) {
            if (!isAerial) {
                aerialState.inReverseAerial = false;
            } else {
                // Check if hit ball
                float distToBall = (state.ball.pos - player.pos).Length();
                bool hitBall = player.ballTouchedStep;
                
                if (hitBall) {
                    float duration = currentTime - aerialState.startTime;
                    
                    // Calculate how reversed the initial jump was
                    Vec initialToBall = (state.ball.pos - player.pos).Normalized();
                    float reverseAmount = -aerialState.initialForward.Dot(initialToBall);
                    reverseAmount = RS_MAX(0.0f, reverseAmount);
                    
                    // Base reward
                    reward += 0.8f * reverseAmount;
                    
                    // Bonus for duration
                    reward += RS_MIN(duration / 2.0f, 1.0f) * 0.4f;
                    
                    // Bonus for height
                    float heightRatio = (player.pos.z - MIN_AERIAL_HEIGHT) / 1000.0f;
                    reward += RS_MIN(heightRatio, 1.0f) * 0.3f;
                    
                    // Check if toward goal
                    Vec targetGoal = (player.team == Team::BLUE) ?
                        CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
                    Vec toGoal = (targetGoal - state.ball.pos).Normalized();
                    float towardGoal = state.ball.vel.Normalized().Dot(toGoal);
                    
                    if (towardGoal > 0.5f) {
                        reward += 0.5f;
                        
                        // Bonus if scores
                        if ((player.team == Team::BLUE && state.ball.pos.y > CommonValues::BACK_WALL_Y) ||
                            (player.team == Team::ORANGE && state.ball.pos.y < -CommonValues::BACK_WALL_Y)) {
                            reward += 2.0f;
                        }
                    }
                    
                    aerialState.inReverseAerial = false;
                }
            }
        }
        
        return reward;
    }
};

// Field Spacing Reward
class FieldSpacingReward : public Reward {
private:
    struct CoverageArea {
        Vec center;
        float radius;
        float coverageValue;
    };
    
    CoverageArea CalculateCoverageCone(const Player& player, const Vec& ballPos) const {
        CoverageArea area;
        
        Vec toBall = ballPos - player.pos;
        float distToBall = toBall.Length();
        
        // Base radius on distance and boost
        float baseRadius = 800.0f + (player.boost * 5.0f);
        
        // Coverage is stronger when facing ball
        Vec toBallNorm = toBall.Normalized();
        float facingBall = RS_MAX(0.0f, player.rotMat.forward.Dot(toBallNorm));
        
        area.center = player.pos + toBall * 0.6f;
        area.radius = baseRadius * (0.5f + 0.5f * facingBall);
        
        // Value based on position
        float valueMultiplier = 1.0f;
        
        Vec myGoal = (player.team == Team::BLUE) ?
            CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;
        
        bool betweenBallAndGoal = 
            (player.team == Team::BLUE && player.pos.y < ballPos.y) ||
            (player.team == Team::ORANGE && player.pos.y > ballPos.y);
        
        if (betweenBallAndGoal) {
            valueMultiplier *= 1.3f;
        }
        
        area.coverageValue = valueMultiplier;
        
        return area;
    }
    
    float CalculateOverlap(const CoverageArea& a1, const CoverageArea& a2) const {
        float dist = (a1.center - a2.center).Length();
        float combinedRadius = a1.radius + a2.radius;
        
        if (dist >= combinedRadius) {
            return 0.0f;
        }
        
        float overlap = (combinedRadius - dist) / combinedRadius;
        return overlap;
    }

public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float reward = 0.0f;
        
        // Calculate coverage area for this player
        CoverageArea myCoverage = CalculateCoverageCone(player, state.ball.pos);
        
        // Check overlap with teammates
        for (const auto& other : state.players) {
            if (other.carId == player.carId) continue;
            if (other.team != player.team) continue;
            
            CoverageArea otherCoverage = CalculateCoverageCone(other, state.ball.pos);
            float overlap = CalculateOverlap(myCoverage, otherCoverage);
            
            if (overlap < 0.2f) {
                reward += 0.1f;  // Good spacing
            } else if (overlap >= 0.2f && overlap < 0.5f) {
                reward -= 0.15f * overlap;  // Slight overlap
            } else if (overlap >= 0.5f && overlap < 0.8f) {
                reward -= 0.4f * overlap;  // Medium overlap
            } else {
                reward -= 0.8f * overlap;  // Severe overlap
            }
        }
        
        // Reward good positioning relative to ball
        float distToBall = (state.ball.pos - player.pos).Length();
        
        int teammatesCloser = 0;
        for (const auto& other : state.players) {
            if (other.carId == player.carId) continue;
            if (other.team != player.team) continue;
            
            float otherDist = (state.ball.pos - other.pos).Length();
            if (otherDist < distToBall) {
                teammatesCloser++;
            }
        }
        
        // Reward based on role
        if (teammatesCloser == 0 && distToBall < 1000.0f) {
            reward += 0.15f;  // 1st man
        } else if (teammatesCloser == 1 && distToBall > 1500.0f && distToBall < 3500.0f) {
            reward += 0.2f;  // 2nd man
        } else if (teammatesCloser >= 2 && distToBall > 3000.0f) {
            reward += 0.15f;  // 3rd man
        }
        
        return reward;
    }
};

// Simple kickoff reward for back player (3v3) to encourage boost grabbing
class KickoffBackPlayerBoostReward : public Reward {
private:
    mutable std::map<uint32_t, bool> isKickoff;
    mutable std::map<uint32_t, bool> hasGrabbedBoost;
    mutable std::map<uint32_t, float> prevBoost;

    // Check if we're at kickoff (ball at center, players near spawn positions)
    bool IsKickoffState(const GameState& state) const {
        // Ball must be at center and stationary
        if (state.ball.pos.Dist(CommonValues::KICKOFF_BALL_CENTER) > 200.0f) {
            return false;
        }
        if (state.ball.vel.Length() > 50.0f) {
            return false;
        }
        return true;
    }

    // Find the furthest player from ball on the team (the back player)
    bool IsBackPlayer(const Player& player, const GameState& state) const {
        float myDist = player.pos.Dist(state.ball.pos);

        // Count how many teammates are further from ball
        int teammatesFurther = 0;
        int teammatesTotal = 0;

        for (const auto& p : state.players) {
            if (p.team == player.team && p.carId != player.carId) {
                teammatesTotal++;
                float theirDist = p.pos.Dist(state.ball.pos);
                if (theirDist > myDist) {
                    teammatesFurther++;
                }
            }
        }

        // For 3v3, we should have 2 teammates, and we should be furthest
        return (teammatesTotal == 2 && teammatesFurther == 0);
    }

public:
    virtual void Reset(const GameState& initialState) override {
        isKickoff.clear();
        hasGrabbedBoost.clear();
        prevBoost.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        auto& kickoffFlag = isKickoff[player.carId];
        auto& grabbedBoost = hasGrabbedBoost[player.carId];
        auto& lastBoost = prevBoost[player.carId];

        float reward = 0.0f;

        // Check if kickoff just started
        if (IsKickoffState(state)) {
            if (!kickoffFlag) {
                // Kickoff just started
                kickoffFlag = true;
                grabbedBoost = false;
                lastBoost = player.boost;
            }

            // If we're the back player during kickoff
            if (IsBackPlayer(player, state)) {
                // Detect boost grab (boost increased)
                if (player.boost > lastBoost + 10.0f) {
                    if (!grabbedBoost) {
                        // First boost grab during this kickoff
                        grabbedBoost = true;
                        reward = 2.0f; // Big reward for grabbing boost
                    }
                }

                // Small continuous reward for moving towards corner boost
                if (!grabbedBoost) {
                    // Find nearest corner boost (big boost pads)
                    float closestDist = 999999.0f;
                    Vec closestBoost;

                    // Corner boost pad indices (from BOOST_LOCATIONS array):
                    // 3, 4 (back corners), 8, 9 (mid corners), 15, 18 (center line corners)
                    int cornerBoostIndices[] = {3, 4, 8, 9, 15, 18};

                    for (int idx : cornerBoostIndices) {
                        Vec boostPos = CommonValues::BOOST_LOCATIONS[idx];
                        float dist = player.pos.Dist(boostPos);
                        if (dist < closestDist) {
                            closestDist = dist;
                            closestBoost = boostPos;
                        }
                    }

                    // Reward for moving towards corner boost
                    Vec toBoost = (closestBoost - player.pos).Normalized();
                    Vec velocity = player.vel.Normalized();
                    float alignment = velocity.Dot(toBoost);

                    if (alignment > 0.5f && closestDist < 2000.0f) {
                        reward += 0.1f * alignment; // Small shaping reward
                    }
                }
            }
        } else {
            // Kickoff ended
            kickoffFlag = false;
            grabbedBoost = false;
        }

        lastBoost = player.boost;
        return reward;
    }
};

// ============================================================================
// PossessionControlReward
// Rewards possession quality, penalises giveaways, credits team support.
// Rev 2.0: per-player + GlobalStepState architecture; bug fix for giveaway
// detection; PossessionControlConfig; static-atomic metrics.
// Run 2 flags (enableFieldZoneWeight, enableDenial) compiled in, disabled
// by default — flip them after Run 1 is validated on W&B.
// ============================================================================

struct PossessionControlConfig {
    // --- possession detection ---
    float possessionDistance      = 250.0f;
    float possessionVelAlignment  = 0.30f;
    float baseTouchTimeout        = 2.0f;
    // --- duration shaping (continuous) ---
    float teamPossDurationGate    = 0.5f;
    float teamPossDurationFull    = 5.0f;
    float teamDurationWeight      = 0.05f;
    float indivPossDurationGate   = 0.3f;
    float indivPossDurationFull   = 3.0f;
    float indivDurationWeight     = 0.08f;
    // --- giveaway penalties (event) ---
    float giveawayPenalty         = -1.5f;
    float dangerGiveawayPenalty   = -2.5f;
    float dangerBallSpeed         = 1000.0f;
    float dangerDirThreshold      = -0.30f;
    // --- smart-play bonuses ---
    float forwardProgressThresh   = 500.0f;
    float forwardProgressBonus    = 0.10f;
    float tightControlDist        = 150.0f;
    float tightControlBonus       = 0.05f;
    // --- team support ---
    float supportMinDist          = 800.0f;
    float supportMaxDist          = 2000.0f;
    float supportBonus            = 0.03f;
    // --- [R2] field-zone weighting ---
    bool  enableFieldZoneWeight   = false;
    float defHalfPossMultiplier   = 0.40f;
    float atkHalfPossMultiplier   = 1.30f;
    // --- [R2] turnover denial ---
    bool  enableDenial            = false;
    float denialReward            = 0.6f;
    float denialBallSpeedDelta    = 700.0f;
    float denialChallengeDist     = 400.0f;
    float denialCooldownSec       = 1.5f;
};

class PossessionControlReward : public Reward {
private:
    PossessionControlConfig _cfg;

    // Per-player state — written in PreStep, read-only in GetReward
    struct PlayerPossState {
        float timeSinceLastTouch = 999.0f;
        float indivPossDuration  = 0.0f;
        Vec   lastTouchBallPos;
        float denialCooldown     = 0.0f;
    };
    std::map<uint32_t, PlayerPossState> _perPlayer;

    // Global step state — written once in PreStep, read-only in GetReward.
    // prevPossTeam/prevPlayerId hold the PREVIOUS step's values so giveaway
    // detection can compare prev vs curr correctly.
    struct GlobalStepState {
        Team     prevPossTeam   = Team::BLUE;
        Team     currPossTeam   = Team::BLUE;
        uint32_t prevPlayerId   = 0;
        uint32_t currPlayerId   = 0;
        Vec      prevBallVel;
        float    teamPossDuration[2] = {};  // [0]=BLUE, [1]=ORANGE
        bool     denialFired    = false;
        uint32_t denialPlayerId = 0;
    };
    GlobalStepState _globalStep;

    // Static atomic metrics — follow AirDribbleAccuracyReward idiom
    inline static std::atomic<long long> s_activationFrames{ 0 };
    inline static std::atomic<long long> s_totalFrames{ 0 };
    inline static std::atomic<long long> s_rewardMilli{ 0 };
    inline static std::atomic<long long> s_teamPossDurMilli{ 0 };
    inline static std::atomic<long long> s_teamPossFrames{ 0 };
    inline static std::atomic<long long> s_giveawayEvents{ 0 };
    inline static std::atomic<long long> s_dangerGiveawayEvents{ 0 };
    inline static std::atomic<long long> s_denialEvents{ 0 };
    inline static std::atomic<long long> s_atkHalfPossFrames{ 0 };
    inline static std::atomic<long long> s_possFramesTotal{ 0 };

    bool HasPossession(const Player& player, const GameState& state) const {
        if (player.pos.Dist(state.ball.pos) > _cfg.possessionDistance)
            return false;
        if (player.ballTouchedStep)
            return true;
        if (player.vel.Length() > 100.0f && state.ball.vel.Length() > 100.0f)
            if (player.vel.Normalized().Dot(state.ball.vel.Normalized()) > _cfg.possessionVelAlignment)
                return true;
        if (player.vel.Length() < 100.0f && state.ball.vel.Length() < 200.0f)
            return true;
        return false;
    }

    float GetDynamicTouchTimeout(const PlayerPossState& ps, const GameState& state) const {
        float dist = ps.lastTouchBallPos.Dist(state.ball.pos);
        return _cfg.baseTouchTimeout + RS_MIN(dist / 500.0f * 0.5f, 2.0f);
    }

    // Only called from PreStep — fallback carries forward previous step's team
    Team GetPossessingTeam(const GameState& state, uint32_t& outPlayerId) const {
        Team     bestTeam   = Team::BLUE;
        uint32_t bestPlayer = 0;
        float    bestDist   = 999999.0f;
        bool     anyPoss    = false;
        for (const auto& p : state.players) {
            if (HasPossession(p, state)) {
                float d = p.pos.Dist(state.ball.pos);
                if (d < bestDist) { bestDist = d; bestTeam = p.team; bestPlayer = p.carId; anyPoss = true; }
            }
        }
        outPlayerId = bestPlayer;
        return anyPoss ? bestTeam : _globalStep.currPossTeam;
    }

public:
    struct Metrics {
        double    activationRate;
        double    meanValue;
        double    teamPossDurationAvg;
        long long giveawayEvents;
        double    dangerGiveawayRatio;
        long long denialEvents;
        double    atkHalfPossShare;
    };

    static Metrics GetAndResetMetrics() {
        long long activation = s_activationFrames.exchange(0);
        long long total      = s_totalFrames.exchange(0);
        long long rewardM    = s_rewardMilli.exchange(0);
        long long possDurM   = s_teamPossDurMilli.exchange(0);
        long long possFrames = s_teamPossFrames.exchange(0);
        long long giveaways  = s_giveawayEvents.exchange(0);
        long long danger     = s_dangerGiveawayEvents.exchange(0);
        long long denial     = s_denialEvents.exchange(0);
        long long atkHalf    = s_atkHalfPossFrames.exchange(0);
        long long possTotal  = s_possFramesTotal.exchange(0);
        Metrics m{};
        m.activationRate      = total      > 0 ? (double)activation / total          : 0.0;
        m.meanValue           = total      > 0 ? (rewardM / 1000.0) / total          : 0.0;
        m.teamPossDurationAvg = possFrames > 0 ? (possDurM / 1000.0) / possFrames    : 0.0;
        m.giveawayEvents      = giveaways;
        m.dangerGiveawayRatio = giveaways  > 0 ? (double)danger / giveaways          : 0.0;
        m.denialEvents        = denial;
        m.atkHalfPossShare    = possTotal  > 0 ? (double)atkHalf / possTotal         : 0.0;
        return m;
    }

    PossessionControlReward(PossessionControlConfig cfg = {}) : _cfg(cfg) {}

    virtual void Reset(const GameState& initialState) override {
        _perPlayer.clear();
        _globalStep = GlobalStepState{};
    }

    virtual void PreStep(const GameState& state) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

        // STEP 1: Capture previous step's possession BEFORE overwriting (core bug fix)
        _globalStep.prevPossTeam = _globalStep.currPossTeam;
        _globalStep.prevPlayerId  = _globalStep.currPlayerId;

        // STEP 2: Compute this step's possession
        uint32_t currPlayerId = 0;
        Team currTeam = GetPossessingTeam(state, currPlayerId);
        _globalStep.currPossTeam = currTeam;
        _globalStep.currPlayerId  = currPlayerId;

        // STEP 3: Update team possession duration
        int idx = (currTeam == Team::BLUE ? 0 : 1);
        _globalStep.teamPossDuration[idx]     += dt;
        _globalStep.teamPossDuration[1 - idx]  = 0.0f;

        // STEP 4: Update per-player state
        for (const auto& player : state.players) {
            auto& ps = _perPlayer[player.carId];
            if (player.ballTouchedStep) {
                ps.timeSinceLastTouch = 0.0f;
                ps.lastTouchBallPos   = state.ball.pos;
            } else {
                ps.timeSinceLastTouch += dt;
            }
            if (HasPossession(player, state)) ps.indivPossDuration += dt;
            else                              ps.indivPossDuration  = 0.0f;
            ps.denialCooldown = RS_MAX(0.0f, ps.denialCooldown - dt);  // tick unconditionally
        }

        // STEP 5: Turnover denial detection (Run 2 — no-op when disabled)
        _globalStep.denialFired    = false;
        _globalStep.denialPlayerId = 0;
        if (_cfg.enableDenial && _globalStep.prevPossTeam != currTeam) {
            for (const auto& player : state.players) {
                if (player.team == _globalStep.prevPossTeam) continue;
                auto& ps = _perPlayer[player.carId];
                if (ps.denialCooldown > 0.0f) continue;
                bool touch      = player.ballTouchedStep;
                bool speedDelta = (state.ball.vel - _globalStep.prevBallVel).Length() >= _cfg.denialBallSpeedDelta;
                bool withinDist = player.pos.Dist(state.ball.pos) <= _cfg.denialChallengeDist;
                if (touch || (speedDelta && withinDist)) {
                    _globalStep.denialFired    = true;
                    _globalStep.denialPlayerId = player.carId;
                    ps.denialCooldown          = _cfg.denialCooldownSec;
                    s_denialEvents.fetch_add(1);
                    break;
                }
            }
        }

        // STEP 6: Save ball velocity for next step's denial detection
        _globalStep.prevBallVel = state.ball.vel;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        auto psIt = _perPlayer.find(player.carId);
        if (psIt == _perPlayer.end()) return 0.0f;
        const PlayerPossState& ps = psIt->second;

        float reward = 0.0f;

        // Read-only over PreStep state
        Team  currPossTeam  = _globalStep.currPossTeam;
        bool  playerHasPoss = HasPossession(player, state);
        float ballSpeed     = state.ball.vel.Length();
        float speedMod      = RS_MAX(0.3f, 1.0f - (ballSpeed / 8000.0f));
        int   teamIdx       = (player.team == Team::BLUE ? 0 : 1);
        float teamPossDur   = _globalStep.teamPossDuration[teamIdx];

        // [R2] Field-zone multiplier (1.0 when disabled)
        float zoneMult        = 1.0f;
        float attackProgress  = 0.0f;
        if (_cfg.enableFieldZoneWeight) {
            float rawY   = (player.team == Team::BLUE) ? state.ball.pos.y : -state.ball.pos.y;
            attackProgress = RS_CLAMP(rawY / 5120.0f, -1.0f, 1.0f);
            float t  = (attackProgress + 1.0f) / 2.0f;
            zoneMult = _cfg.defHalfPossMultiplier + t * (_cfg.atkHalfPossMultiplier - _cfg.defHalfPossMultiplier);
        }

        // ====================================================================
        // 1. POSSESSION DURATION (team + individual)
        // ====================================================================
        if (currPossTeam == player.team && teamPossDur > _cfg.teamPossDurationGate) {
            float dur = RS_CLAMP(teamPossDur / _cfg.teamPossDurationFull, 0.0f, 1.0f);
            reward += _cfg.teamDurationWeight * dur * speedMod * zoneMult;
        }
        if (playerHasPoss && ps.indivPossDuration > _cfg.indivPossDurationGate) {
            float dur = RS_CLAMP(ps.indivPossDuration / _cfg.indivPossDurationFull, 0.0f, 1.0f);
            reward += _cfg.indivDurationWeight * dur * speedMod * zoneMult;
        }

        // ====================================================================
        // 2. GIVEAWAY DETECTION (possession flipped to opponent this step)
        // prevPossTeam vs currPossTeam are preserved from the PREVIOUS step,
        // so this comparison is now correct (was always false before Rev 2.0).
        // ====================================================================
        if (_globalStep.prevPossTeam != currPossTeam &&
            _globalStep.prevPlayerId != 0 &&
            _globalStep.prevPlayerId == player.carId) {
            if (ps.timeSinceLastTouch < GetDynamicTouchTimeout(ps, state)) {
                Vec oppGoal = (player.team == Team::BLUE)
                    ? CommonValues::ORANGE_GOAL_CENTER
                    : CommonValues::BLUE_GOAL_CENTER;
                float ballDirQuality = state.ball.vel.Dot((oppGoal - state.ball.pos).Normalized());
                if (ballDirQuality <= 0.0f) {
                    float penalty = _cfg.giveawayPenalty;
                    bool  isDanger = (ballSpeed > _cfg.dangerBallSpeed && ballDirQuality < _cfg.dangerDirThreshold);
                    if (isDanger) penalty = _cfg.dangerGiveawayPenalty;
                    reward += penalty;
                    s_giveawayEvents.fetch_add(1);
                    if (isDanger) s_dangerGiveawayEvents.fetch_add(1);
                }
            }
        }

        // ====================================================================
        // 3. SMART PLAY BONUSES (possession + forward movement / tight control)
        // ====================================================================
        if (playerHasPoss && ps.indivPossDuration > 0.5f) {
            Vec oppGoal = (player.team == Team::BLUE)
                ? CommonValues::ORANGE_GOAL_CENTER
                : CommonValues::BLUE_GOAL_CENTER;
            if (player.vel.Dot((oppGoal - player.pos).Normalized()) > _cfg.forwardProgressThresh)
                reward += _cfg.forwardProgressBonus * speedMod;
            if (player.pos.Dist(state.ball.pos) < _cfg.tightControlDist)
                reward += _cfg.tightControlBonus * speedMod;
        }

        // ====================================================================
        // 4. TEAM SUPPORT (flat bonus — off-ball, support band, ahead of ball)
        // ====================================================================
        if (!playerHasPoss && currPossTeam == player.team) {
            float distToBall = player.pos.Dist(state.ball.pos);
            if (distToBall > _cfg.supportMinDist && distToBall < _cfg.supportMaxDist) {
                Vec oppGoal = (player.team == Team::BLUE)
                    ? CommonValues::ORANGE_GOAL_CENTER
                    : CommonValues::BLUE_GOAL_CENTER;
                if (player.pos.Dist(oppGoal) < state.ball.pos.Dist(oppGoal))
                    reward += _cfg.supportBonus;
            }
        }

        // ====================================================================
        // 5. [R2] TURNOVER DENIAL (event-gated, one-shot per disruptor)
        // ====================================================================
        if (_cfg.enableDenial && _globalStep.denialFired &&
            _globalStep.denialPlayerId == player.carId) {
            reward += _cfg.denialReward;
        }

        // Metrics
        s_totalFrames.fetch_add(1);
        if (fabsf(reward) > 1e-3f) s_activationFrames.fetch_add(1);
        s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));
        if (currPossTeam == player.team && teamPossDur > 0.0f) {
            s_teamPossDurMilli.fetch_add(static_cast<long long>(teamPossDur * 1000.0f));
            s_teamPossFrames.fetch_add(1);
            if (_cfg.enableFieldZoneWeight) {
                s_possFramesTotal.fetch_add(1);
                if (attackProgress > 0.0f) s_atkHalfPossFrames.fetch_add(1);
            }
        }

        return reward;
    }
};

// ============================================================================
// PossessionControlRewardv2  (threat-gated possession)
// Copy of PossessionControlReward with two design changes:
//   * Duration channels (team + individual) and the forward-progress bonus now
//     ONLY pay while BallThreatActive() is true — i.e. the ball is being carried
//     at pace into the attacking third, toward the opponent goal. This kills the
//     "hold / shuffle side-to-side for reward" stalling pattern.
//   * The unconditional tight-control bonus is DELETED (it paid regardless of
//     what the car was doing with the ball — the single biggest stall farm).
// Giveaway detection, team-support, and denial channels are unchanged.
// Architecture (PreStep/GetReward split, per-player + GlobalStepState, static
// atomic metrics) is identical to v1. Do NOT edit the v1 class above.
// ============================================================================

struct PossessionControlV2Config {
    // --- possession detection ---
    float possessionDistance      = 250.0f;
    float possessionVelAlignment  = 0.30f;
    float baseTouchTimeout        = 2.0f;
    // --- duration shaping (continuous, THREAT-GATED in v2) ---
    float teamPossDurationGate    = 0.5f;
    float teamPossDurationFull    = 5.0f;
    float teamDurationWeight      = 0.05f;
    float indivPossDurationGate   = 0.3f;
    float indivPossDurationFull   = 3.0f;
    float indivDurationWeight     = 0.08f;
    // --- giveaway penalties (event) ---
    float giveawayPenalty         = -1.5f;
    float dangerGiveawayPenalty   = -2.5f;
    float dangerBallSpeed         = 1000.0f;
    float dangerDirThreshold      = -0.30f;
    // --- smart-play forward-progress bonus (folded into the threat gate in v2) ---
    float forwardProgressBonus    = 0.10f;
    // (tightControlDist / tightControlBonus intentionally removed — deleted channel)
    // --- team support ---
    float supportMinDist          = 800.0f;
    float supportMaxDist          = 2000.0f;
    float supportBonus            = 0.03f;
    // --- ball-threat gate (v2) ---
    float attackZoneDist          = 4000.0f;  // dist from opp GOAL_BACK; ~attacking-third-and-in
    float ballThreatMinSpeed      = 600.0f;   // uu/s — real pace, not drift
    float ballThreatMinAlignment  = 0.30f;    // matches possessionVelAlignment / IsShotOnGoal convention
    // --- field-zone weighting (kept from v1, disabled by default) ---
    bool  enableFieldZoneWeight   = false;
    float defHalfPossMultiplier   = 0.40f;
    float atkHalfPossMultiplier   = 1.30f;
    // --- turnover denial (kept from v1, disabled by default) ---
    bool  enableDenial            = false;
    float denialReward            = 0.6f;
    float denialBallSpeedDelta    = 700.0f;
    float denialChallengeDist     = 400.0f;
    float denialCooldownSec       = 1.5f;
};

class PossessionControlRewardv2 : public Reward {
private:
    PossessionControlV2Config _cfg;

    // Per-player state — written in PreStep, read-only in GetReward
    struct PlayerPossState {
        float timeSinceLastTouch = 999.0f;
        float indivPossDuration  = 0.0f;
        Vec   lastTouchBallPos;
        float denialCooldown     = 0.0f;
    };
    std::map<uint32_t, PlayerPossState> _perPlayer;

    // Global step state — written once in PreStep, read-only in GetReward.
    struct GlobalStepState {
        Team     prevPossTeam   = Team::BLUE;
        Team     currPossTeam   = Team::BLUE;
        uint32_t prevPlayerId   = 0;
        uint32_t currPlayerId   = 0;
        Vec      prevBallVel;
        float    teamPossDuration[2] = {};  // [0]=BLUE, [1]=ORANGE
        bool     threatActive   = false;    // v2: computed once per tick
        bool     denialFired    = false;
        uint32_t denialPlayerId = 0;
    };
    GlobalStepState _globalStep;

    // Static atomic metrics — follow AirDribbleAccuracyReward idiom
    inline static std::atomic<long long> s_activationFrames{ 0 };
    inline static std::atomic<long long> s_totalFrames{ 0 };
    inline static std::atomic<long long> s_rewardMilli{ 0 };
    inline static std::atomic<long long> s_giveawayEvents{ 0 };
    inline static std::atomic<long long> s_dangerGiveawayEvents{ 0 };
    inline static std::atomic<long long> s_denialEvents{ 0 };
    // v2-specific: threat gate + gated duration channel
    inline static std::atomic<long long> s_possControlFrames{ 0 };  // denom: frames where player's team possesses
    inline static std::atomic<long long> s_threatActiveFrames{ 0 }; // of those, how many had threatActive
    inline static std::atomic<long long> s_durBonusMilli{ 0 };      // sum of the (gated) duration-channel reward

    bool HasPossession(const Player& player, const GameState& state) const {
        if (player.pos.Dist(state.ball.pos) > _cfg.possessionDistance)
            return false;
        if (player.ballTouchedStep)
            return true;
        if (player.vel.Length() > 100.0f && state.ball.vel.Length() > 100.0f)
            if (player.vel.Normalized().Dot(state.ball.vel.Normalized()) > _cfg.possessionVelAlignment)
                return true;
        if (player.vel.Length() < 100.0f && state.ball.vel.Length() < 200.0f)
            return true;
        return false;
    }

    float GetDynamicTouchTimeout(const PlayerPossState& ps, const GameState& state) const {
        float dist = ps.lastTouchBallPos.Dist(state.ball.pos);
        return _cfg.baseTouchTimeout + RS_MIN(dist / 500.0f * 0.5f, 2.0f);
    }

    // Only called from PreStep — fallback carries forward previous step's team
    Team GetPossessingTeam(const GameState& state, uint32_t& outPlayerId) const {
        Team     bestTeam   = Team::BLUE;
        uint32_t bestPlayer = 0;
        float    bestDist   = 999999.0f;
        bool     anyPoss    = false;
        for (const auto& p : state.players) {
            if (HasPossession(p, state)) {
                float d = p.pos.Dist(state.ball.pos);
                if (d < bestDist) { bestDist = d; bestTeam = p.team; bestPlayer = p.carId; anyPoss = true; }
            }
        }
        outPlayerId = bestPlayer;
        return anyPoss ? bestTeam : _globalStep.currPossTeam;
    }

    // v2: is the ball being carried at pace into a dangerous position?
    // Team-relative (uses the possessing team's opponent goal), so it applies
    // the same regardless of which teammate is touching.
    bool BallThreatActive(Team possessingTeam, const GameState& state) const {
        Vec oppGoalBack = (possessingTeam == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_BACK
            : CommonValues::BLUE_GOAL_BACK;
        float distToOppGoal = (state.ball.pos - oppGoalBack).Length();
        if (distToOppGoal > _cfg.attackZoneDist) return false;
        float ballSpeed = state.ball.vel.Length();
        if (ballSpeed < _cfg.ballThreatMinSpeed) return false;
        Vec toOppGoal = (oppGoalBack - state.ball.pos).Normalized();
        float alignment = state.ball.vel.Normalized().Dot(toOppGoal);
        return alignment >= _cfg.ballThreatMinAlignment;
    }

public:
    struct Metrics {
        double    activationRate;
        double    meanValue;
        double    threatActiveRate;   // fraction of possession-ticks with threatActive
        double    durationBonusMean;  // mean gated duration-channel reward per possession-tick
        long long giveawayEvents;
        double    dangerGiveawayRatio;
        long long denialEvents;
    };

    static Metrics GetAndResetMetrics() {
        long long activation = s_activationFrames.exchange(0);
        long long total      = s_totalFrames.exchange(0);
        long long rewardM    = s_rewardMilli.exchange(0);
        long long giveaways  = s_giveawayEvents.exchange(0);
        long long danger     = s_dangerGiveawayEvents.exchange(0);
        long long denial     = s_denialEvents.exchange(0);
        long long possCtrl   = s_possControlFrames.exchange(0);
        long long threatAct  = s_threatActiveFrames.exchange(0);
        long long durBonusM  = s_durBonusMilli.exchange(0);
        Metrics m{};
        m.activationRate      = total    > 0 ? (double)activation / total       : 0.0;
        m.meanValue           = total    > 0 ? (rewardM / 1000.0) / total        : 0.0;
        m.threatActiveRate    = possCtrl > 0 ? (double)threatAct / possCtrl       : 0.0;
        m.durationBonusMean   = possCtrl > 0 ? (durBonusM / 1000.0) / possCtrl    : 0.0;
        m.giveawayEvents      = giveaways;
        m.dangerGiveawayRatio = giveaways > 0 ? (double)danger / giveaways        : 0.0;
        m.denialEvents        = denial;
        return m;
    }

    PossessionControlRewardv2(PossessionControlV2Config cfg = {}) : _cfg(cfg) {}

    virtual void Reset(const GameState& initialState) override {
        _perPlayer.clear();
        _globalStep = GlobalStepState{};
    }

    virtual void PreStep(const GameState& state) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

        // STEP 1: Capture previous step's possession BEFORE overwriting
        _globalStep.prevPossTeam = _globalStep.currPossTeam;
        _globalStep.prevPlayerId  = _globalStep.currPlayerId;

        // STEP 2: Compute this step's possession
        uint32_t currPlayerId = 0;
        Team currTeam = GetPossessingTeam(state, currPlayerId);
        _globalStep.currPossTeam = currTeam;
        _globalStep.currPlayerId  = currPlayerId;

        // STEP 3: Update team possession duration
        int idx = (currTeam == Team::BLUE ? 0 : 1);
        _globalStep.teamPossDuration[idx]     += dt;
        _globalStep.teamPossDuration[1 - idx]  = 0.0f;

        // STEP 4: Update per-player state
        for (const auto& player : state.players) {
            auto& ps = _perPlayer[player.carId];
            if (player.ballTouchedStep) {
                ps.timeSinceLastTouch = 0.0f;
                ps.lastTouchBallPos   = state.ball.pos;
            } else {
                ps.timeSinceLastTouch += dt;
            }
            if (HasPossession(player, state)) ps.indivPossDuration += dt;
            else                              ps.indivPossDuration  = 0.0f;
            ps.denialCooldown = RS_MAX(0.0f, ps.denialCooldown - dt);
        }

        // STEP 5: Turnover denial detection (no-op when disabled)
        _globalStep.denialFired    = false;
        _globalStep.denialPlayerId = 0;
        if (_cfg.enableDenial && _globalStep.prevPossTeam != currTeam) {
            for (const auto& player : state.players) {
                if (player.team == _globalStep.prevPossTeam) continue;
                auto& ps = _perPlayer[player.carId];
                if (ps.denialCooldown > 0.0f) continue;
                bool touch      = player.ballTouchedStep;
                bool speedDelta = (state.ball.vel - _globalStep.prevBallVel).Length() >= _cfg.denialBallSpeedDelta;
                bool withinDist = player.pos.Dist(state.ball.pos) <= _cfg.denialChallengeDist;
                if (touch || (speedDelta && withinDist)) {
                    _globalStep.denialFired    = true;
                    _globalStep.denialPlayerId = player.carId;
                    ps.denialCooldown          = _cfg.denialCooldownSec;
                    s_denialEvents.fetch_add(1);
                    break;
                }
            }
        }

        // STEP 6: v2 — compute the ball-threat gate once for the possessing team
        _globalStep.threatActive = BallThreatActive(currTeam, state);

        // STEP 7: Save ball velocity for next step's denial detection
        _globalStep.prevBallVel = state.ball.vel;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        auto psIt = _perPlayer.find(player.carId);
        if (psIt == _perPlayer.end()) return 0.0f;
        const PlayerPossState& ps = psIt->second;

        float reward = 0.0f;

        Team  currPossTeam  = _globalStep.currPossTeam;
        bool  playerHasPoss = HasPossession(player, state);
        bool  threatActive  = _globalStep.threatActive;
        float ballSpeed     = state.ball.vel.Length();
        float speedMod      = RS_MAX(0.3f, 1.0f - (ballSpeed / 8000.0f));
        int   teamIdx       = (player.team == Team::BLUE ? 0 : 1);
        float teamPossDur   = _globalStep.teamPossDuration[teamIdx];

        // Field-zone multiplier (1.0 when disabled)
        float zoneMult        = 1.0f;
        float attackProgress  = 0.0f;
        if (_cfg.enableFieldZoneWeight) {
            float rawY   = (player.team == Team::BLUE) ? state.ball.pos.y : -state.ball.pos.y;
            attackProgress = RS_CLAMP(rawY / 5120.0f, -1.0f, 1.0f);
            float t  = (attackProgress + 1.0f) / 2.0f;
            zoneMult = _cfg.defHalfPossMultiplier + t * (_cfg.atkHalfPossMultiplier - _cfg.defHalfPossMultiplier);
        }

        // ====================================================================
        // 1. POSSESSION DURATION (team + individual) — v2: THREAT-GATED
        // ====================================================================
        float durationBonus = 0.0f;
        if (threatActive) {
            if (currPossTeam == player.team && teamPossDur > _cfg.teamPossDurationGate) {
                float dur = RS_CLAMP(teamPossDur / _cfg.teamPossDurationFull, 0.0f, 1.0f);
                durationBonus += _cfg.teamDurationWeight * dur * speedMod * zoneMult;
            }
            if (playerHasPoss && ps.indivPossDuration > _cfg.indivPossDurationGate) {
                float dur = RS_CLAMP(ps.indivPossDuration / _cfg.indivPossDurationFull, 0.0f, 1.0f);
                durationBonus += _cfg.indivDurationWeight * dur * speedMod * zoneMult;
            }
        }
        reward += durationBonus;

        // ====================================================================
        // 2. GIVEAWAY DETECTION (unchanged from v1)
        // ====================================================================
        if (_globalStep.prevPossTeam != currPossTeam &&
            _globalStep.prevPlayerId != 0 &&
            _globalStep.prevPlayerId == player.carId) {
            if (ps.timeSinceLastTouch < GetDynamicTouchTimeout(ps, state)) {
                Vec oppGoal = (player.team == Team::BLUE)
                    ? CommonValues::ORANGE_GOAL_CENTER
                    : CommonValues::BLUE_GOAL_CENTER;
                float ballDirQuality = state.ball.vel.Dot((oppGoal - state.ball.pos).Normalized());
                if (ballDirQuality <= 0.0f) {
                    float penalty = _cfg.giveawayPenalty;
                    bool  isDanger = (ballSpeed > _cfg.dangerBallSpeed && ballDirQuality < _cfg.dangerDirThreshold);
                    if (isDanger) penalty = _cfg.dangerGiveawayPenalty;
                    reward += penalty;
                    s_giveawayEvents.fetch_add(1);
                    if (isDanger) s_dangerGiveawayEvents.fetch_add(1);
                }
            }
        }

        // ====================================================================
        // 3. FORWARD-PROGRESS BONUS — v2: folded into the same threat gate.
        //    (v1's separate player-velocity check and the tight-control bonus
        //     are gone. One gate, one meaning.)
        // ====================================================================
        if (playerHasPoss && threatActive) {
            reward += _cfg.forwardProgressBonus * speedMod;
        }

        // ====================================================================
        // 4. TEAM SUPPORT (unchanged from v1)
        // ====================================================================
        if (!playerHasPoss && currPossTeam == player.team) {
            float distToBall = player.pos.Dist(state.ball.pos);
            if (distToBall > _cfg.supportMinDist && distToBall < _cfg.supportMaxDist) {
                Vec oppGoal = (player.team == Team::BLUE)
                    ? CommonValues::ORANGE_GOAL_CENTER
                    : CommonValues::BLUE_GOAL_CENTER;
                if (player.pos.Dist(oppGoal) < state.ball.pos.Dist(oppGoal))
                    reward += _cfg.supportBonus;
            }
        }

        // ====================================================================
        // 5. TURNOVER DENIAL (unchanged from v1, disabled by default)
        // ====================================================================
        if (_cfg.enableDenial && _globalStep.denialFired &&
            _globalStep.denialPlayerId == player.carId) {
            reward += _cfg.denialReward;
        }

        // Metrics
        s_totalFrames.fetch_add(1);
        if (fabsf(reward) > 1e-3f) s_activationFrames.fetch_add(1);
        s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));
        if (currPossTeam == player.team) {
            s_possControlFrames.fetch_add(1);
            if (threatActive) s_threatActiveFrames.fetch_add(1);
            s_durBonusMilli.fetch_add(static_cast<long long>(durationBonus * 1000.0f));
        }

        return reward;
    }
};

// ============================================================================
// HandbrakeAerialReward
// Simple reward for using handbrake while touching the ball in the air (z > 300)
// ============================================================================
class HandbrakeAerialReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Check conditions:
        // 1. Ball was touched this step
        // 2. Player is in the air (z > 300)
        // 3. Handbrake is being held (handbrakeVal > 0)

        if (!player.ballTouchedStep) {
            return 0.0f;
        }

        if (player.pos.z <= 300.0f) {
            return 0.0f;
        }

        if (player.handbrakeVal <= 0.0f) {
            return 0.0f;
        }

        // All conditions met: reward for handbrake aerial touch
        // Scale reward by how hard handbrake is being held (0.0 to 1.0)
        float handbrakeIntensity = RS_CLAMP(player.handbrakeVal, 0.0f, 1.0f);

        // Scale by how high in the air (higher = more skillful)
        float heightBonus = RS_CLAMP((player.pos.z - 300.0f) / 1000.0f, 0.0f, 1.0f);

        // Base reward of 2.0 + modulation by height
        float reward = 2.0f * handbrakeIntensity * (1.0f + 0.5f * heightBonus);

        return reward;
    }
};

// ============================================================================
// AerialControlledBallCatchReward
// Rewards catching and controlling airborne balls close to the car
// - Triggers when ball velocity drops >500 UU/s (soft touch)
// - Maintains control while ball stays <200 UU from car, relative velocity <300 UU/s
// - Height bonus for higher catches (up to 1300 UU)
// ============================================================================
class AerialControlledBallCatchReward : public Reward {
private:
    struct CatchMemory {
        float lastBallVelocity = 0.0f;       // Previous frame's ball speed
        float velocityReductionTime = -1.0f; // When last velocity drop occurred
        bool hasCaughtRecently = false;      // Within 0.5s window
    };

    mutable std::map<uint32_t, CatchMemory> catchMemory;

    // Catch detection thresholds
    static constexpr float MIN_CATCH_HEIGHT = 300.0f;           // Ball must be in air
    static constexpr float MAX_CATCH_DISTANCE = 200.0f;         // Closeness to ball
    static constexpr float MAX_RELATIVE_VELOCITY = 300.0f;      // Velocity sync
    static constexpr float MIN_BALL_VELOCITY_REDUCTION = 500.0f; // Soft touch threshold
    static constexpr float VELOCITY_REDUCTION_WINDOW = 0.5f;    // Window to confirm catch
    static constexpr float BASE_REWARD = 2.0f;

public:
    virtual void Reset(const GameState& initialState) override {
        catchMemory.clear();
    }

    virtual void PreStep(const GameState& state) override {
        // Track ball velocity for each player to detect velocity reduction
        // This enables catch detection in GetReward()
        float ballVelocity = state.ball.vel.Length();

        for (const auto& player : state.players) {
            if (!catchMemory.count(player.carId)) {
                catchMemory[player.carId] = CatchMemory();
            }

            auto& mem = catchMemory[player.carId];

            // Detect velocity reduction (soft touch / catch event)
            if (mem.lastBallVelocity > 0.0f) {
                float velocityDrop = mem.lastBallVelocity - ballVelocity;
                if (velocityDrop >= MIN_BALL_VELOCITY_REDUCTION) {
                    mem.velocityReductionTime = state.deltaTime > 0.0f ? state.deltaTime : 0.016f;
                    mem.hasCaughtRecently = true;
                }
            }

            // Decay catch window over time
            if (mem.hasCaughtRecently && mem.velocityReductionTime >= 0.0f) {
                mem.velocityReductionTime += state.deltaTime > 0.0f ? state.deltaTime : 0.016f;
                if (mem.velocityReductionTime > VELOCITY_REDUCTION_WINDOW) {
                    mem.hasCaughtRecently = false;
                    mem.velocityReductionTime = -1.0f;
                }
            }

            mem.lastBallVelocity = ballVelocity;
        }
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Condition 1: Ball must be in the air
        if (state.ball.pos.z < MIN_CATCH_HEIGHT) {
            return 0.0f;
        }

        auto& mem = catchMemory[player.carId];

        // Condition 2: Must have caught recently (velocity drop detected)
        if (!mem.hasCaughtRecently) {
            return 0.0f;
        }

        // Condition 3: Ball must be close to car
        float distToBall = (player.pos - state.ball.pos).Length();
        if (distToBall > MAX_CATCH_DISTANCE * 2.0f) {  // Allow 400 UU max for early-stage control
            return 0.0f;
        }

        // Condition 4: Relative velocity must be low (synchronized movement)
        Vec relVel = state.ball.vel - player.vel;
        float relVelMag = relVel.Length();
        if (relVelMag > MAX_RELATIVE_VELOCITY * 1.5f) {  // Allow up to 450 UU/s
            return 0.0f;
        }

        // All conditions met: calculate control quality score

        // Proximity score: 1.0 at <200 UU, 0.0 at >400 UU
        float proximityScore = RS_CLAMP(1.0f - (distToBall / MAX_CATCH_DISTANCE), 0.0f, 1.0f);

        // Velocity sync score: 1.0 at <50 UU/s, 0.0 at >300 UU/s
        float velScore = RS_CLAMP(1.0f - (relVelMag / MAX_RELATIVE_VELOCITY), 0.0f, 1.0f);

        // Height bonus: higher catches are more difficult (up to 1300 UU)
        float heightBonus = RS_CLAMP((state.ball.pos.z - MIN_CATCH_HEIGHT) / 1000.0f, 0.0f, 1.0f);
        float heightMult = 1.0f + 0.5f * heightBonus;

        // Combine all factors
        float controlQuality = proximityScore * velScore;
        float reward = BASE_REWARD * controlQuality * heightMult;

        return reward;
    }
};

} // namespace RLGC
