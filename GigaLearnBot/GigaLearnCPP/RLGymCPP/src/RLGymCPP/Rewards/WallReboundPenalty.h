#pragma once
// WallReboundPenalty.h
//
// Penalises poorly placed side-wall rebound shots — specifically touches where
// the bot hits the ball into a side wall and the ball bounces to a position
// where the opponent reaches it first (possession giveaway).
//
// Three-stage detection pipeline:
//   Stage 1: Player touches ball near/toward a side wall (x-axis only).
//            If an opponent touches the ball mid-window the track is cancelled.
//            The nearest-to-ball touching player is recorded.
//   Stage 2: Within TOUCH_WINDOW seconds, ball x-velocity sign flips while near wall
//            (standard prev-state sign-flip bounce detection).
//   Stage 3: After the bounce, the opponent team's min-TimeToReach < our team's
//            by ADVANTAGE_MARGIN → fire PENALTY once on the recorded toucher.
//
// Design notes:
//   - Back wall (BACK_WALL_Y = 5120) is the goal LINE, NOT a physics wall.
//     Checking |pos.y| > 4720 would fire on every on-goal shot in flight.
//     Side walls (x-axis) are the sole focus.
//   - No ZeroSumReward wrapper needed — fires only on the specific toucher.
//   - Cannot be farmed (it is a penalty).

#include "Reward.h"
#include "../Math.h"
#include <unordered_map>
#include <limits>

namespace RLGC {

class PoorWallReboundPenalty : public Reward {
private:
    static constexpr float WALL_DIST        = 400.0f;   // UU from side wall plane
    static constexpr float WALL_VEL_TOWARD  = 500.0f;   // UU/s toward wall to trigger tracking
    static constexpr float WALL_VEL_POS_MIN = 2000.0f;  // ball must be past this |x| for vel trigger
    static constexpr float TOUCH_WINDOW     = 0.8f;     // seconds (~12 steps @ 8-tick skip)
    static constexpr float ADVANTAGE_MARGIN = 0.85f;    // opp must be ≥15% faster than friendly
    static constexpr float PENALTY          = -1.0f;

    float    _timeSinceTouch   = 0.0f;
    uint32_t _lastToucherCarId = 0;
    Team     _lastToucherTeam  = Team::BLUE;
    bool     _fired            = false;
    std::unordered_map<uint32_t, float> _stepPenalties;

    // Side wall only — BACK_WALL_Y is the goal line, NOT a physics wall
    static bool BallNearSideWall(const Vec& pos) {
        return fabsf(pos.x) > CommonValues::SIDE_WALL_X - WALL_DIST;
    }

    // Post-touch ball velocity has a strong component toward the nearest side wall
    static bool BallGoingTowardSideWall(const Vec& pos, const Vec& vel) {
        float vx = (pos.x > 0.0f) ? vel.x : -vel.x;  // positive = toward wall
        return vx > WALL_VEL_TOWARD && fabsf(pos.x) > WALL_VEL_POS_MIN;
    }

    // x-velocity sign flip while ball is near a side wall → side-wall bounce
    static bool SideWallBounceOccurred(const GameState& state) {
        if (!state.prev) return false;
        if (!BallNearSideWall(state.ball.pos)) return false;
        return state.prev->ball.vel.x * state.ball.vel.x < 0.0f;
    }

    // Simple time-to-reach estimate: distance / CAR_MAX_SPEED (from AdvancedRewards.h pattern)
    static float TimeToReach(const Vec& from, const Vec& to) {
        return (from - to).Length() / CommonValues::CAR_MAX_SPEED;
    }

public:
    virtual void Reset(const GameState& initialState) override {
        _timeSinceTouch   = 0.0f;
        _lastToucherCarId = 0;
        _fired            = false;
        _stepPenalties.clear();
    }

    virtual void PreStep(const GameState& state) override {
        _stepPenalties.clear();
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

        // ── Stage 1: scan all touches this step ─────────────────────────────
        // Rules:
        //   • Opponent touch while mid-window → cancel (they deflected the ball first)
        //   • Own-team touch near/toward side wall → record nearest-to-ball toucher
        const Player* bestToucher = nullptr;
        float bestDist = std::numeric_limits<float>::max();
        bool cancelled = false;

        for (const auto& p : state.players) {
            if (!p.ballTouchedStep) continue;

            // Opponent touch mid-window cancels current tracking
            if (_lastToucherCarId != 0 && p.team != _lastToucherTeam) {
                cancelled = true;
                break;
            }

            // Qualify: ball near side wall OR post-touch velocity toward side wall
            if (!BallNearSideWall(state.ball.pos) &&
                !BallGoingTowardSideWall(state.ball.pos, state.ball.vel))
                continue;

            // Select the nearest-to-ball toucher (deterministic in multi-touch steps)
            float dist = (p.pos - state.ball.pos).Length();
            if (dist < bestDist) { bestDist = dist; bestToucher = &p; }
        }

        if (cancelled) {
            _lastToucherCarId = 0;  // opponent deflected it — reset
        } else if (bestToucher) {
            _lastToucherCarId = bestToucher->carId;
            _lastToucherTeam  = bestToucher->team;
            _timeSinceTouch   = 0.0f;
            _fired            = false;
        }

        // Advance timer only while actively tracking
        if (_lastToucherCarId != 0)
            _timeSinceTouch += dt;

        // ── Stages 2+3: bounce + possession check ───────────────────────────
        if (_lastToucherCarId != 0 && !_fired
            && _timeSinceTouch <= TOUCH_WINDOW
            && SideWallBounceOccurred(state)) {

            float friendly = std::numeric_limits<float>::max();
            float opp      = std::numeric_limits<float>::max();
            for (const auto& p : state.players) {
                if (p.isDemoed) continue;
                float t = TimeToReach(p.pos, state.ball.pos);
                if (p.team == _lastToucherTeam) friendly = RS_MIN(friendly, t);
                else                            opp      = RS_MIN(opp, t);
            }

            // Opponent reaches ball significantly faster than our team → bad wall shot
            if (opp < friendly * ADVANTAGE_MARGIN) {
                _stepPenalties[_lastToucherCarId] = PENALTY;
                _fired = true;
            }
        }

        // Expire tracking window
        if (_timeSinceTouch > TOUCH_WINDOW)
            _lastToucherCarId = 0;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (player.isDemoed) return 0.0f;
        auto it = _stepPenalties.find(player.carId);
        return (it != _stepPenalties.end()) ? it->second : 0.0f;
    }
};

} // namespace RLGC
