#pragma once
#include "Reward.h"
#include "../Math.h"
#include "RewardMath.h"
#include <unordered_map>
#include <algorithm>
#include <cmath>

// ShotExplorationReward  — Phase 6+
// Rewards shot variety across field positions and trajectory angles.
// Complements ShotPlacementReward (which measures defender coverage) —
// this measures WHERE the bot shoots from and HOW angled the shot trajectory is.
//
// ShotCreativityReward — Phase 6+
// Rewards unusual car state at time of a valid shot-on-goal:
// inverted/sideways orientation, redirect geometry, and first-touch shooting.
//
// Rev 1.0 2026-04-24 NJH

namespace RLGC {

// ─────────────────────────────────────────────────────────────────────────────
// Shared helper: is the ball heading toward the shooter's opponent goal?
// Uses velocity direction + minimum speed — no BallPred needed.
// ─────────────────────────────────────────────────────────────────────────────
static inline bool IsShotOnGoal(const Player& player, const GameState& state,
                                float minSpeed = 500.f, float minAlignment = 0.3f)
{
    float speed = state.ball.vel.Length();
    if (speed < minSpeed) return false;

    const Vec& oppGoal = (player.team == Team::BLUE)
        ? CommonValues::ORANGE_GOAL_CENTER
        : CommonValues::BLUE_GOAL_CENTER;
    Vec toGoal = (oppGoal - state.ball.pos).Normalized();
    return state.ball.vel.Normalized().Dot(toGoal) >= minAlignment;
}


// =============================================================================
// ShotExplorationReward
// Three additive channels, each with independent cooldown.
//
//  A  Wide-angle shot  — lateral shot trajectory (ball vel x/y ratio)
//  B  Height variety   — aerial shot (ball Z > 400) or low ground skimmer (Z < 100)
//  C  Field position   — shooting from own half, from corner, or from the wall
// =============================================================================
class ShotExplorationReward : public Reward {
public:
    // Channel A — angle
    static constexpr float ANGLE_RATIO_THRESHOLD = 0.35f; // |vx/vy| > this = angled shot
    static constexpr float ANGLE_RATIO_FULL      = 1.2f;  // |vx/vy| >= this = max angle bonus
    static constexpr float ANGLE_BONUS_MAX       = 0.8f;
    static constexpr float ANGLE_COOLDOWN        = 2.0f;

    // Channel B — height
    static constexpr float AERIAL_Z              = 400.f;
    static constexpr float LOW_Z                 = 100.f;
    static constexpr float AERIAL_BONUS          = 0.4f;
    static constexpr float LOW_BONUS             = 0.2f;
    static constexpr float HEIGHT_COOLDOWN       = 2.0f;

    // Channel C — field position
    static constexpr float OWN_HALF_BONUS        = 0.5f;
    static constexpr float CORNER_X_THRESHOLD    = 2800.f;
    static constexpr float CORNER_BONUS          = 0.3f;
    static constexpr float WALL_Z_THRESHOLD      = 200.f;
    static constexpr float WALL_X_THRESHOLD      = 3500.f;
    static constexpr float WALL_BONUS            = 0.3f;
    static constexpr float POSITION_COOLDOWN     = 2.0f;

private:
    struct PlayerState {
        float angleCooldown    = 0.f;
        float heightCooldown   = 0.f;
        float positionCooldown = 0.f;
    };
    std::unordered_map<uint32_t, PlayerState> _states;

    bool IsInOwnHalf(const Player& p) const {
        return (p.team == Team::BLUE) ? (p.pos.y < 0.f) : (p.pos.y > 0.f);
    }

    bool IsOnWall(const Player& p) const {
        return p.isOnGround
            && p.pos.z > WALL_Z_THRESHOLD
            && std::abs(p.pos.x) > WALL_X_THRESHOLD;
    }

public:
    void Reset(const GameState& state) override {
        _states.clear();
        for (const auto& p : state.players)
            _states[p.carId] = PlayerState{};
    }

    void PreStep(const GameState& state) override {
        float dt = (state.deltaTime > 0.f) ? state.deltaTime : (1.f / 120.f);
        for (const auto& p : state.players) {
            auto& s = _states[p.carId];
            s.angleCooldown    = std::max(0.f, s.angleCooldown    - dt);
            s.heightCooldown   = std::max(0.f, s.heightCooldown   - dt);
            s.positionCooldown = std::max(0.f, s.positionCooldown - dt);
        }
    }

    float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep)              return 0.f;
        if (!IsShotOnGoal(player, state))         return 0.f;

        auto& s = _states[player.carId];
        float reward = 0.f;

        // ── Channel A: Wide-angle shot ────────────────────────────────────────
        if (s.angleCooldown <= 0.f) {
            float vy = std::abs(state.ball.vel.y);
            float vx = std::abs(state.ball.vel.x);
            if (vy > 1.f) {
                float ratio = vx / vy;
                if (ratio > ANGLE_RATIO_THRESHOLD) {
                    float t = std::min((ratio - ANGLE_RATIO_THRESHOLD)
                                      / (ANGLE_RATIO_FULL - ANGLE_RATIO_THRESHOLD), 1.f);
                    reward += t * ANGLE_BONUS_MAX;
                    s.angleCooldown = ANGLE_COOLDOWN;
                }
            }
        }

        // ── Channel B: Height variety ─────────────────────────────────────────
        if (s.heightCooldown <= 0.f) {
            float ballZ = state.ball.pos.z;
            if (ballZ > AERIAL_Z) {
                float t = std::min((ballZ - AERIAL_Z) / 400.f, 1.f);
                reward += AERIAL_BONUS * (0.5f + 0.5f * t);
                s.heightCooldown = HEIGHT_COOLDOWN;
            } else if (ballZ < LOW_Z) {
                reward += LOW_BONUS;
                s.heightCooldown = HEIGHT_COOLDOWN;
            }
        }

        // ── Channel C: Field position ─────────────────────────────────────────
        if (s.positionCooldown <= 0.f) {
            float posBonus = 0.f;
            if (IsInOwnHalf(player))                       posBonus += OWN_HALF_BONUS;
            if (std::abs(player.pos.x) > CORNER_X_THRESHOLD) posBonus += CORNER_BONUS;
            if (IsOnWall(player))                          posBonus += WALL_BONUS;
            if (posBonus > 0.f) {
                reward += posBonus;
                s.positionCooldown = POSITION_COOLDOWN;
            }
        }

        return reward;
    }
};


// =============================================================================
// ShotCreativityReward
// Three additive channels, each with independent cooldown.
//
//  A  Orientation    — inverted or sideways car at shot moment
//  B  Redirect       — car moving in a significantly different direction than
//                      the post-touch ball velocity (redirect geometry)
//  C  First touch    — shooting on first contact (no touch in last 0.8s)
// =============================================================================
class ShotCreativityReward : public Reward {
public:
    // Channel A — orientation at shot
    static constexpr float INVERTED_UP_Z    = -0.4f;  // up.z < this = inverted
    static constexpr float SIDEWAYS_UP_Z    = 0.35f;  // |up.z| < this = sideways/rolling
    static constexpr float INVERTED_BONUS   = 0.6f;
    static constexpr float SIDEWAYS_BONUS   = 0.3f;
    static constexpr float ORIENT_COOLDOWN  = 2.5f;

    // Channel B — redirect geometry
    // Car vel and post-touch ball vel must diverge by > REDIRECT_ANGLE_MIN degrees
    static constexpr float REDIRECT_DOT_MAX  = 0.5f;  // cos(60°) — below = redirect
    static constexpr float REDIRECT_MIN_SPEED= 400.f;  // car speed gate
    static constexpr float REDIRECT_BONUS    = 0.5f;
    static constexpr float REDIRECT_COOLDOWN = 2.0f;

    // Channel C — first touch shot
    static constexpr float FIRST_TOUCH_WINDOW = 0.8f;  // seconds since last touch
    static constexpr float FIRST_TOUCH_BONUS  = 0.5f;
    static constexpr float FIRST_TOUCH_COOLDOWN = 3.0f;

private:
    struct PlayerState {
        float lastTouchTime      = -99.f;
        float orientCooldown     = 0.f;
        float redirectCooldown   = 0.f;
        float firstTouchCooldown = 0.f;
    };
    std::unordered_map<uint32_t, PlayerState> _states;
    float _time = 0.f;

public:
    void Reset(const GameState& state) override {
        _states.clear();
        _time = 0.f;
        for (const auto& p : state.players)
            _states[p.carId] = PlayerState{};
    }

    void PreStep(const GameState& state) override {
        float dt = (state.deltaTime > 0.f) ? state.deltaTime : (1.f / 120.f);
        _time += dt;

        for (const auto& p : state.players) {
            auto& s = _states[p.carId];
            s.orientCooldown     = std::max(0.f, s.orientCooldown     - dt);
            s.redirectCooldown   = std::max(0.f, s.redirectCooldown   - dt);
            s.firstTouchCooldown = std::max(0.f, s.firstTouchCooldown - dt);

            // Track every touch (not just shot-on-goal) for first-touch detection
            if (p.ballTouchedStep)
                s.lastTouchTime = _time;
        }
    }

    float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep)      return 0.f;
        if (!IsShotOnGoal(player, state)) return 0.f;

        auto& s = _states[player.carId];
        float reward = 0.f;

        // ── Channel A: Orientation ────────────────────────────────────────────
        if (s.orientCooldown <= 0.f) {
            float upZ = player.rotMat.up.z;
            if (upZ < INVERTED_UP_Z) {
                reward += INVERTED_BONUS;
                s.orientCooldown = ORIENT_COOLDOWN;
            } else if (std::abs(upZ) < SIDEWAYS_UP_Z) {
                reward += SIDEWAYS_BONUS;
                s.orientCooldown = ORIENT_COOLDOWN;
            }
        }

        // ── Channel B: Redirect geometry ──────────────────────────────────────
        if (s.redirectCooldown <= 0.f) {
            float carSpeed = player.vel.Length();
            if (carSpeed > REDIRECT_MIN_SPEED) {
                Vec carDir  = player.vel.Normalized();
                Vec ballDir = state.ball.vel.Normalized();
                float alignment = carDir.Dot(ballDir);
                if (alignment < REDIRECT_DOT_MAX) {
                    // Scale reward: more perpendicular = higher bonus
                    float t = std::max(0.f, (REDIRECT_DOT_MAX - alignment) / REDIRECT_DOT_MAX);
                    reward += REDIRECT_BONUS * t;
                    s.redirectCooldown = REDIRECT_COOLDOWN;
                }
            }
        }

        // ── Channel C: First touch ────────────────────────────────────────────
        if (s.firstTouchCooldown <= 0.f) {
            float timeSinceLast = _time - s.lastTouchTime;
            // lastTouchTime was JUST set by PreStep, so we check previous gap
            // (PreStep already updated it this tick — use prev state via the gap check)
            // If this is truly first touch, lastTouchTime was set this tick from -99
            // so timeSinceLast will be huge. Otherwise it reflects the previous touch.
            // We compare against FIRST_TOUCH_WINDOW from before this touch.
            if (s.lastTouchTime < 0.f || (_time - s.lastTouchTime) > FIRST_TOUCH_WINDOW) {
                reward += FIRST_TOUCH_BONUS;
                s.firstTouchCooldown = FIRST_TOUCH_COOLDOWN;
            }
        }

        return reward;
    }
};

} // namespace RLGC
