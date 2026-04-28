#pragma once
#include "Reward.h"
#include "../Math.h"
#include <unordered_map>
#include <algorithm>
#include <cmath>

// RecoveryExplorationReward — Phase 6+
// Four sub-rewards for creative recovery on non-standard geometries.
// Covers: ceiling wavedash, wall-to-wall wavedash, squishy save momentum, boost-to-surface.
// Complements IntelligentWavedashReward (ground/wall) and WavedashBasicReward (ground only).
// Rev 1.1 2026-04-22 NJH

namespace RLGC {

class RecoveryExplorationReward : public Reward {
public:
    static constexpr float CEILING_NEAR_Z             = 1700.f;
    static constexpr float WALL_X_THRESHOLD           = 3750.f;
    static constexpr float WALL_Y_THRESHOLD           = 5050.f;
    static constexpr float GOAL_Y_BLUE                = -5120.f;
    static constexpr float GOAL_Y_ORANGE              =  5120.f;
    static constexpr float GOAL_X_HALF                = 893.f;
    static constexpr float GOAL_Z_TOP                 = 642.f;

    static constexpr int   CEILING_HISTORY_TICKS      = 25;
    static constexpr float CEILING_MIN_SPEED          = 700.f;
    static constexpr float CEILING_SPEED_RATIO        = 0.65f;
    static constexpr float CEILING_COOLDOWN           = 5.0f;
    static constexpr float CEILING_REWARD             = 1.5f;

    static constexpr float WALL_MIN_SPEED             = 600.f;
    static constexpr float WALL_COOLDOWN              = 3.0f;
    static constexpr float WALL_REWARD                = 1.0f;

    static constexpr float SQUISHY_GOAL_MOUTH_MARGIN  = 500.f;
    static constexpr float SQUISHY_FOLLOW_WINDOW      = 0.5f;
    static constexpr float SQUISHY_MIN_EXIT_RATIO     = 0.6f;
    static constexpr float SQUISHY_COOLDOWN           = 5.0f;
    static constexpr float SQUISHY_REWARD             = 1.2f;

    static constexpr float BOOST_TO_SURFACE_WINDOW    = 0.6f;
    static constexpr float BOOST_TO_SURFACE_MIN_SPEED = 600.f;
    static constexpr float BOOST_TO_SURFACE_COOLDOWN  = 2.0f;
    static constexpr float BOOST_TO_SURFACE_REWARD    = 0.8f;

private:
    enum WallSurface { WALL_NONE = 0, SIDE_LEFT, SIDE_RIGHT, BACK_BLUE, BACK_ORANGE };

    struct PlayerState {
        int   ceilingTicksRecent   = 0;
        bool  wasFlipping          = false;
        float preWavedashSpeed     = 0.f;
        float ceilingCooldown      = 0.f;

        WallSurface prevWall       = WALL_NONE;
        WallSurface curWall        = WALL_NONE;
        float  wallPreSpeed        = 0.f;
        float  wallCooldown        = 0.f;

        bool  ballEnteredOwnGoal   = false;
        float ballGoalEnterTime    = -99.f;
        float carGoalEntrySpeed    = 0.f;
        bool  carIsInGoal          = false;
        float squishyCooldown      = 0.f;
        int   team                 = 0;

        bool  boostingAirborne     = false;
        float boostStartTime       = -99.f;
        float boostCooldown        = 0.f;
        bool  hitSurfaceAfterBoost = false;
    };

    std::unordered_map<uint32_t, PlayerState> _states;
    float _time = 0.f;

    WallSurface GetWall(const Vec& pos) const {
        if (std::abs(pos.x) > WALL_X_THRESHOLD)
            return pos.x < 0.f ? SIDE_LEFT : SIDE_RIGHT;
        if (std::abs(pos.y) > WALL_Y_THRESHOLD)
            return pos.y < 0.f ? BACK_BLUE : BACK_ORANGE;
        return WALL_NONE;
    }

    bool NearOwnGoalMouth(const Vec& ballPos, int team) const {
        float goalY = (team == 0) ? GOAL_Y_BLUE : GOAL_Y_ORANGE;
        return std::abs(ballPos.y - goalY) < SQUISHY_GOAL_MOUTH_MARGIN
            && std::abs(ballPos.x) < GOAL_X_HALF + 200.f
            && ballPos.z < GOAL_Z_TOP + 200.f;
    }

    bool InOwnGoalVolume(const Vec& pos, int team) const {
        float goalY = (team == 0) ? GOAL_Y_BLUE : GOAL_Y_ORANGE;
        bool behindLine = (team == 0) ? (pos.y < GOAL_Y_BLUE) : (pos.y > GOAL_Y_ORANGE);
        return behindLine
            && std::abs(pos.x) < GOAL_X_HALF + 150.f
            && pos.z < GOAL_Z_TOP + 150.f;
    }

    bool OnWallOrCeiling(const Player& player) const {
        if (!player.isOnGround) return false;
        if (player.pos.z > 200.f
            && (std::abs(player.pos.x) > 3500.f || std::abs(player.pos.y) > 4900.f))
            return true;
        if (player.pos.z > 1800.f) return true;
        return false;
    }

public:
    void Reset(const GameState& state) override {
        _states.clear();
        _time = 0.f;
        for (const auto& p : state.players) {
            _states[p.carId] = PlayerState{};
            _states[p.carId].team = (int)p.team;
        }
    }

    void PreStep(const GameState& state) override {
        float dt = (state.deltaTime > 0.f) ? state.deltaTime : (1.f / 120.f);
        _time += dt;

        for (const auto& p : state.players) {
            auto& s = _states[p.carId];

            s.ceilingCooldown = std::max(0.f, s.ceilingCooldown - dt);
            s.wallCooldown    = std::max(0.f, s.wallCooldown    - dt);
            s.squishyCooldown = std::max(0.f, s.squishyCooldown - dt);
            s.boostCooldown   = std::max(0.f, s.boostCooldown   - dt);

            // Sub-reward A: rolling ceiling history
            if (p.pos.z > CEILING_NEAR_Z)
                s.ceilingTicksRecent = std::min(s.ceilingTicksRecent + 1, CEILING_HISTORY_TICKS + 1);
            else if (p.isOnGround)
                s.ceilingTicksRecent = std::max(0, s.ceilingTicksRecent - 1);

            float speed = p.vel.Length();
            bool wasFlipping = s.wasFlipping;
            s.wasFlipping = p.isFlipping;

            // Detect wavedash: flip just ended AND now on ground
            if (wasFlipping && !p.isFlipping && p.isOnGround) {
                if (s.ceilingTicksRecent >= CEILING_HISTORY_TICKS / 2)
                    s.preWavedashSpeed = speed;

                WallSurface newWall = GetWall(p.pos);
                if (newWall != WALL_NONE && s.prevWall != WALL_NONE && newWall != s.prevWall)
                    s.wallPreSpeed = speed;
                s.prevWall = s.curWall;
                s.curWall  = newWall;
            }

            if (p.isOnGround)
                s.curWall = GetWall(p.pos);

            // Sub-reward C: squishy save tracking
            const Vec& bpos    = state.ball.pos;
            bool ballNearGoal  = NearOwnGoalMouth(bpos, s.team);
            bool ballInGoal    = InOwnGoalVolume(bpos, s.team);
            bool carInGoalNow  = InOwnGoalVolume(p.pos, s.team);

            if (ballNearGoal && !s.ballEnteredOwnGoal)
                s.ballEnteredOwnGoal = true;
            if (s.ballEnteredOwnGoal && ballInGoal && s.ballGoalEnterTime < 0.f)
                s.ballGoalEnterTime = _time;
            if (carInGoalNow && !s.carIsInGoal) {
                s.carGoalEntrySpeed = speed;
                s.carIsInGoal = true;
            }
            if (!carInGoalNow)
                s.carIsInGoal = false;
            if (!ballInGoal && !ballNearGoal) {
                s.ballEnteredOwnGoal = false;
                s.ballGoalEnterTime  = -99.f;
            }

            // Sub-reward D: boost-to-surface tracking
            bool boostingNow = (!p.isOnGround) && (p.prevAction.boost > 0.5f);
            if (boostingNow && !s.boostingAirborne) {
                s.boostStartTime       = _time;
                s.hitSurfaceAfterBoost = false;
            }
            s.boostingAirborne = boostingNow;
        }
    }

    float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        auto& s    = _states[player.carId];
        float speed = player.vel.Length();
        float reward = 0.f;

        // ── Sub-reward A: Ceiling Wavedash ────────────────────────────────────
        if (s.ceilingCooldown <= 0.f
            && s.ceilingTicksRecent >= CEILING_HISTORY_TICKS / 2
            && player.isOnGround
            && !player.isFlipping
            && s.preWavedashSpeed > CEILING_MIN_SPEED)
        {
            float ratio = (s.preWavedashSpeed > 0.f)
                ? std::min(speed / s.preWavedashSpeed, 1.f) : 0.f;
            if (ratio > CEILING_SPEED_RATIO) {
                reward += CEILING_REWARD * ratio;
                s.ceilingCooldown    = CEILING_COOLDOWN;
                s.ceilingTicksRecent = 0;
                s.preWavedashSpeed   = 0.f;
            }
        }

        // ── Sub-reward B: Wall-to-Wall Wavedash ───────────────────────────────
        if (s.wallCooldown <= 0.f
            && s.wallPreSpeed > WALL_MIN_SPEED
            && s.prevWall != WALL_NONE
            && s.curWall  != WALL_NONE
            && s.prevWall != s.curWall)
        {
            float ratio = (s.wallPreSpeed > 0.f)
                ? std::min(speed / s.wallPreSpeed, 1.f) : 0.f;
            reward += WALL_REWARD * ratio;
            s.wallCooldown = WALL_COOLDOWN;
            s.wallPreSpeed = 0.f;
            s.prevWall     = WALL_NONE;
        }

        // ── Sub-reward C: Squishy Save Momentum ──────────────────────────────
        if (s.squishyCooldown <= 0.f
            && s.ballEnteredOwnGoal
            && s.carGoalEntrySpeed > 300.f
            && s.ballGoalEnterTime > 0.f
            && (_time - s.ballGoalEnterTime) < SQUISHY_FOLLOW_WINDOW
            && !InOwnGoalVolume(player.pos, s.team))
        {
            float ratio = (s.carGoalEntrySpeed > 0.f)
                ? speed / s.carGoalEntrySpeed : 0.f;
            ratio = std::max(SQUISHY_MIN_EXIT_RATIO, std::min(ratio, 1.f));
            reward += SQUISHY_REWARD * ratio;
            s.squishyCooldown    = SQUISHY_COOLDOWN;
            s.ballEnteredOwnGoal = false;
            s.carGoalEntrySpeed  = 0.f;
            s.ballGoalEnterTime  = -99.f;
        }

        // ── Sub-reward D: Boost-to-Surface ───────────────────────────────────
        if (s.boostCooldown <= 0.f
            && s.boostStartTime > 0.f
            && !s.hitSurfaceAfterBoost
            && OnWallOrCeiling(player))
        {
            float timeToContact = _time - s.boostStartTime;
            if (timeToContact > 0.f
                && timeToContact < BOOST_TO_SURFACE_WINDOW
                && speed > BOOST_TO_SURFACE_MIN_SPEED)
            {
                float t = std::max(0.f, 1.f - (timeToContact / BOOST_TO_SURFACE_WINDOW));
                reward += BOOST_TO_SURFACE_REWARD * t;
                s.boostCooldown        = BOOST_TO_SURFACE_COOLDOWN;
                s.hitSurfaceAfterBoost = true;
                s.boostStartTime       = -99.f;
            }
        }

        return reward;
    }
};

} // namespace RLGC
