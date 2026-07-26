#pragma once
#include "Reward.h"
#include "RewardMath.h"
#include "../CommonValues.h"
#include <unordered_map>
#include <cmath>

// DoomseeDisshShotReward Rev 2.0 | NJH | 2026-06-05
//
// Doomsee Dish: ball rolling on own back wall (height 150-1500) → player touches
// it from the wall (ball drops with z-vel < -100) → player jumps off wall and
// tracks ball arc (150-800 UU from wall) → ball enters goal mouth rectangle →
// player flips into it for conversion.
//
// 4-phase sequence: WALL_APPROACH → WALL_TOUCH → WALL_ESCAPE → FLIP_CONVERSION
// Per-tick clamp ±0.5f; goal bonus (2.0f) passes through unclamped.

namespace RLGC {

class DoomseeDisshShotReward : public Reward {
public:
    struct DoomseeState {
        enum Phase : uint8_t {
            IDLE            = 0,
            WALL_APPROACH   = 1,
            WALL_TOUCH      = 2,
            WALL_ESCAPE     = 3,
            FLIP_CONVERSION = 4
        };

        Phase phase             = IDLE;
        float phaseStartTime    = 0.0f;
        float sequenceStartTime = 0.0f;
        float timeAccum         = 0.0f;  // per-player clock — avoids globalTime × N-players bug

        float dishQuality       = 0.0f;
        bool  touchRewardPaid   = false;
        bool  escapeEntryPaid   = false;
        bool  flipTouchPaid     = false;
    };

private:
    mutable std::unordered_map<uint32_t, DoomseeState> playerStates;

    // --- Geometry ---
    static constexpr float BACK_WALL_PROXIMITY  = 300.0f;
    static constexpr float BALL_APPROACH_DIST   = 700.0f;
    static constexpr float BALL_WALL_HEIGHT_MIN = 150.0f;
    static constexpr float BALL_WALL_HEIGHT_MAX = 1500.0f;
    static constexpr float BALL_SPEED_MAX       = 1800.0f;
    static constexpr float BALL_SPEED_MIN       = 100.0f;
    static constexpr float DROP_Z_VEL_MIN       = -100.0f;
    static constexpr float DROP_Z_VEL_SATURATE  = -600.0f;
    static constexpr float DROP_Y_TOLERANCE     = 300.0f;
    static constexpr float DROP_HEIGHT_MAX      = 850.0f;
    static constexpr float TRACK_X_REWARD_BAND  = 400.0f;
    static constexpr float TRACK_X_EXIT_BAND    = 700.0f;
    static constexpr float TRACK_Z_REWARD_BAND  = 350.0f;
    static constexpr float TRACK_Z_EXIT_BAND    = 600.0f;
    static constexpr float TRACK_Y_MIN_GAP      = 150.0f;
    static constexpr float TRACK_Y_MAX_GAP      = 800.0f;
    static constexpr float FLIP_RANGE           = 500.0f;
    static constexpr float GOAL_MOUTH_Y_TOL     = 300.0f;

    // --- Phase timeouts (seconds) ---
    static constexpr float TIMEOUT_APPROACH     = 4.0f;
    static constexpr float TIMEOUT_WALL_TOUCH   = 0.5f;
    static constexpr float TIMEOUT_WALL_ESCAPE  = 3.5f;
    static constexpr float TIMEOUT_FLIP_CONV    = 3.0f;
    static constexpr float TIMEOUT_SEQUENCE_MAX = 12.0f;

    // --- Reward magnitudes ---
    static constexpr float REWARD_APPROACH_MAX  = 0.08f;
    static constexpr float REWARD_TOUCH_BASE    = 0.30f;
    static constexpr float REWARD_DISH_QUALITY  = 0.40f;
    static constexpr float REWARD_ESCAPE_ENTRY  = 0.20f;
    static constexpr float REWARD_TRACKING_MAX  = 0.12f;
    static constexpr float REWARD_FLIP_TOUCH    = 0.35f;
    static constexpr float REWARD_SHOT_QUALITY  = 0.40f;
    static constexpr float REWARD_GOAL_BONUS    = 2.00f;
    static constexpr float PENALTY_HARD_CLEAR   = -0.20f;

    // ------------------------------------------------------------------ helpers

    // Distance from pos to the player's OWN back wall (Blue: y=-5120, Orange: y=+5120).
    float DistToBackWall(const Vec& pos, Team team) const {
        return (team == Team::BLUE)
            ? fabsf(pos.y + CommonValues::BACK_WALL_Y)
            : fabsf(pos.y - CommonValues::BACK_WALL_Y);
    }

    bool IsOnOrNearBackWall(const Vec& pos, Team team) const {
        return DistToBackWall(pos, team) < BACK_WALL_PROXIMITY;
    }

    bool BallValidForApproach(const BallState& ball) const {
        float spd = ball.vel.Length();
        return ball.pos.z >= BALL_WALL_HEIGHT_MIN
            && ball.pos.z <= BALL_WALL_HEIGHT_MAX
            && spd >= BALL_SPEED_MIN
            && spd <= BALL_SPEED_MAX;
    }

    // Quality of the wall touch: did the ball drop (neg z-vel) and land centered?
    float ComputeDishQuality(const BallState& ball) const {
        if (ball.vel.z >= DROP_Z_VEL_MIN) return 0.0f;   // not dropping
        if (fabsf(ball.pos.x) >= CommonValues::GOAL_WIDTH_FROM_CENTER) return 0.0f;
        if (fabsf(ball.pos.y) < CommonValues::BACK_WALL_Y - DROP_Y_TOLERANCE) return 0.0f;
        if (ball.pos.z > DROP_HEIGHT_MAX) return 0.0f;

        float xQ = 1.0f - RS_CLAMP(fabsf(ball.pos.x) / CommonValues::GOAL_WIDTH_FROM_CENTER, 0.0f, 1.0f);
        float zQ = RS_CLAMP(-ball.vel.z / fabsf(DROP_Z_VEL_SATURATE), 0.0f, 1.0f);
        return xQ * zQ;
    }

    // Team-agnostic: both back walls are at |y| = BACK_WALL_Y.
    bool IsBallInGoalMouthWindow(const BallState& ball) const {
        return fabsf(ball.pos.x) < CommonValues::GOAL_WIDTH_FROM_CENTER
            && ball.pos.z < CommonValues::GOAL_HEIGHT
            && ball.pos.z > 0.0f
            && fabsf(ball.pos.y) > CommonValues::BACK_WALL_Y - GOAL_MOUTH_Y_TOL;
    }

    float ComputeTrackingQuality(const Player& player, const BallState& ball) const {
        float playerWallDist = DistToBackWall(player.pos, player.team);
        float ballWallDist   = DistToBackWall(ball.pos,   player.team);

        // Hard Y gates — no gradient outside these bands
        if (playerWallDist < TRACK_Y_MIN_GAP || playerWallDist > TRACK_Y_MAX_GAP) return 0.0f;
        if (playerWallDist <= ballWallDist) return 0.0f;  // player must be further from wall than ball

        float xQ = 1.0f - RS_CLAMP(fabsf(player.pos.x - ball.pos.x) / TRACK_X_REWARD_BAND, 0.0f, 1.0f);
        float zQ = 1.0f - RS_CLAMP(fabsf(player.pos.z - ball.pos.z) / TRACK_Z_REWARD_BAND, 0.0f, 1.0f);
        return xQ * zQ;
    }

    // -------------------------------------------------------- phase rewards

    float GetWallApproachReward(const Player& player, const BallState& ball) const {
        float dist = (player.pos - ball.pos).Length();
        float proximity = 1.0f - RS_CLAMP(dist / BALL_APPROACH_DIST, 0.0f, 1.0f);
        return REWARD_APPROACH_MAX * proximity;
    }

    float GetWallTouchReward(const Player& player, const GameState& state, DoomseeState& dState) const {
        if (player.ballTouchedStep && !dState.touchRewardPaid) {
            dState.touchRewardPaid = true;
            if (state.ball.vel.Length() > 1400.0f) return PENALTY_HARD_CLEAR;
            return REWARD_TOUCH_BASE + REWARD_DISH_QUALITY * dState.dishQuality;
        }
        return 0.0f;
    }

    float GetWallEscapeReward(const Player& player, const BallState& ball, DoomseeState& dState) const {
        float reward = 0.0f;
        if (!dState.escapeEntryPaid) {
            dState.escapeEntryPaid = true;
            reward += REWARD_ESCAPE_ENTRY;
        }
        reward += REWARD_TRACKING_MAX * ComputeTrackingQuality(player, ball);
        return reward;
    }

    float GetFlipConversionReward(const Player& player, const GameState& state, DoomseeState& dState) const {
        float reward = 0.0f;
        if (player.ballTouchedStep && !dState.flipTouchPaid) {
            dState.flipTouchPaid = true;
            reward += REWARD_FLIP_TOUCH;
            Vec oppGoal = (player.team == Team::BLUE)
                ? CommonValues::ORANGE_GOAL_CENTER
                : CommonValues::BLUE_GOAL_CENTER;
            float shotDot = RS_CLAMP(SafeNorm(state.ball.vel).Dot(SafeNorm(oppGoal - state.ball.pos)), 0.0f, 1.0f);
            reward += REWARD_SHOT_QUALITY * shotDot;
        }
        // Only award the goal bonus when the ball entered the OPPONENT's net.
        // goalScored is a plain bool with no team info, so we infer from ball position.
        bool scoredInOppGoal = (player.team == Team::BLUE)
            ? (state.ball.pos.y > 0.0f)
            : (state.ball.pos.y < 0.0f);
        if (state.goalScored && scoredInOppGoal) reward += REWARD_GOAL_BONUS;
        return reward;
    }

    void ResetToIdle(DoomseeState& dState) const {
        dState.phase = DoomseeState::IDLE;
    }

public:
    virtual void Reset(const GameState& initialState) override {
        playerStates.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
        auto& dState = playerStates[player.carId];
        dState.timeAccum += dt;

        // ── IDLE: try to enter sequence ──────────────────────────────────────
        if (dState.phase == DoomseeState::IDLE) {
            if (IsOnOrNearBackWall(state.ball.pos, player.team)
                && BallValidForApproach(state.ball)
                && (player.pos - state.ball.pos).Length() < BALL_APPROACH_DIST)
            {
                dState.phase             = DoomseeState::WALL_APPROACH;
                dState.phaseStartTime    = dState.timeAccum;
                dState.sequenceStartTime = dState.timeAccum;
                dState.touchRewardPaid   = false;
                dState.escapeEntryPaid   = false;
                dState.flipTouchPaid     = false;
                dState.dishQuality       = 0.0f;
            }
            return 0.0f;
        }

        float phaseElapsed    = dState.timeAccum - dState.phaseStartTime;
        float sequenceElapsed = dState.timeAccum - dState.sequenceStartTime;

        // ── Global sequence exits ─────────────────────────────────────────────
        if (sequenceElapsed > TIMEOUT_SEQUENCE_MAX) { ResetToIdle(dState); return 0.0f; }
        // goalScored in pre-Phase4 = sequence over, no bonus
        if (state.goalScored && dState.phase != DoomseeState::FLIP_CONVERSION) { ResetToIdle(dState); return 0.0f; }
        // Ball traveled too far from back wall or went too high
        if (fabsf(state.ball.pos.y) < CommonValues::BACK_WALL_Y - 1500.0f) { ResetToIdle(dState); return 0.0f; }
        if (state.ball.pos.z > 1800.0f)                                      { ResetToIdle(dState); return 0.0f; }

        float phaseReward   = 0.0f;
        bool  goalBonusTick = false;

        switch (dState.phase) {

        // ── Phase 1: WALL_APPROACH ────────────────────────────────────────────
        case DoomseeState::WALL_APPROACH: {
            if (phaseElapsed > TIMEOUT_APPROACH) { ResetToIdle(dState); return 0.0f; }

            bool touchOnWall = player.ballTouchedStep
                && IsOnOrNearBackWall(player.pos, player.team)
                && IsOnOrNearBackWall(state.ball.pos, player.team);

            if (touchOnWall) {
                dState.dishQuality   = ComputeDishQuality(state.ball);
                dState.phase         = DoomseeState::WALL_TOUCH;
                dState.phaseStartTime = dState.timeAccum;
                // Fire touch reward on the same tick as the touch (ballTouchedStep is one-tick)
                phaseReward = GetWallTouchReward(player, state, dState);
            } else {
                phaseReward = GetWallApproachReward(player, state.ball);
            }
            break;
        }

        // ── Phase 2: WALL_TOUCH (linger up to 0.5s for player to jump off) ───
        case DoomseeState::WALL_TOUCH: {
            if (phaseElapsed > TIMEOUT_WALL_TOUCH) { ResetToIdle(dState); return 0.0f; }

            float wallDist = DistToBackWall(player.pos, player.team);
            bool playerLeftWall = !player.isOnGround
                && player.pos.z > 100.0f
                && wallDist > TRACK_Y_MIN_GAP;

            if (playerLeftWall) {
                dState.phase          = DoomseeState::WALL_ESCAPE;
                dState.phaseStartTime = dState.timeAccum;
                // Entry bonus fires on this same tick
                phaseReward = GetWallEscapeReward(player, state.ball, dState);
            }
            // else: no per-tick reward while waiting for the jump
            break;
        }

        // ── Phase 3: WALL_ESCAPE (player tracks ball arc off the wall) ────────
        case DoomseeState::WALL_ESCAPE: {
            if (phaseElapsed > TIMEOUT_WALL_ESCAPE) { ResetToIdle(dState); return 0.0f; }

            // Exit if player drifts too far from ball in X or Z
            if (fabsf(player.pos.x - state.ball.pos.x) > TRACK_X_EXIT_BAND
             || fabsf(player.pos.z - state.ball.pos.z) > TRACK_Z_EXIT_BAND)
            {
                ResetToIdle(dState); return 0.0f;
            }

            bool ballInMouth = IsBallInGoalMouthWindow(state.ball);
            bool canFlip     = !player.isOnGround
                && (player.pos - state.ball.pos).Length() < FLIP_RANGE;

            if (ballInMouth && canFlip) {
                dState.phase          = DoomseeState::FLIP_CONVERSION;
                dState.phaseStartTime = dState.timeAccum;
                phaseReward = GetFlipConversionReward(player, state, dState);
                bool scoredInOppGoal = (player.team == Team::BLUE)
                    ? (state.ball.pos.y > 0.0f)
                    : (state.ball.pos.y < 0.0f);
                if (state.goalScored && scoredInOppGoal) { goalBonusTick = true; ResetToIdle(dState); }
            } else {
                phaseReward = GetWallEscapeReward(player, state.ball, dState);
            }
            break;
        }

        // ── Phase 4: FLIP_CONVERSION ──────────────────────────────────────────
        case DoomseeState::FLIP_CONVERSION: {
            if (phaseElapsed > TIMEOUT_FLIP_CONV) { ResetToIdle(dState); return 0.0f; }

            phaseReward = GetFlipConversionReward(player, state, dState);
            bool scoredInOppGoal = (player.team == Team::BLUE)
                ? (state.ball.pos.y > 0.0f)
                : (state.ball.pos.y < 0.0f);
            if (state.goalScored && scoredInOppGoal) { goalBonusTick = true; ResetToIdle(dState); }
            break;
        }

        default: break;
        }

        // Goal bonus passes through unclamped; everything else clamped per-tick
        if (goalBonusTick) return phaseReward;
        return RS_CLAMP(phaseReward, -0.5f, 0.5f);
    }
};

} // namespace RLGC
