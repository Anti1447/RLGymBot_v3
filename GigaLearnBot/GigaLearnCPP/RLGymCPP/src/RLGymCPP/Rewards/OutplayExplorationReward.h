#pragma once
#include "Reward.h"
#include "../Math.h"
#include "RewardMath.h"
#include <unordered_map>
#include <algorithm>
#include <cmath>

// OutplayExplorationReward — Phase 6+
// Rewards outplay variety by position/height context.
// Existing rewards cover ground flicks (FlickOutplayReward) and ground
// cuts/pops/fakes (CutOutplayReward). This fills the aerial, wall, and
// pure-geometry gaps.
//
// OutplayCreativityReward — Phase 6+
// Rewards unusual car state (inverted/sideways, low-boost) during a
// successful redirect away from a challenging opponent.
//
// Rev 1.0 2026-04-24 NJH

namespace RLGC {

// ─────────────────────────────────────────────────────────────────────────────
// Shared helpers
// ─────────────────────────────────────────────────────────────────────────────

// True if any opponent is within distGate UU and either moving toward the
// ball faster than speedGate UU/s (with >0.35 alignment) or currently flipping.
static inline bool IsOpponentThreateningBall(const Player& player, const GameState& state,
                                              float distGate = 1800.f, float speedGate = 450.f)
{
    for (const auto& opp : state.players) {
        if (opp.team == player.team) continue;
        Vec toBall = state.ball.pos - opp.pos;
        if (toBall.Length() > distGate) continue;
        float oppSpeed = opp.vel.Length();
        if (oppSpeed > speedGate && opp.vel.Normalized().Dot(toBall.Normalized()) > 0.35f)
            return true;
        if (opp.isFlipping)
            return true;
    }
    return false;
}

// How well the post-touch ball direction diverges from the nearest moving
// opponent's current movement direction.  Range [-1, 1].  +1 = ball going
// directly opposite to opponent momentum (perfect outplay geometry).
// Returns 0 if no opponent is moving fast enough to score.
static inline float GetOutplayRedirectScore(const Player& player, const GameState& state)
{
    float ballSpeed = state.ball.vel.Length();
    if (ballSpeed < 400.f) return 0.f;
    Vec ballDir = state.ball.vel.Normalized();

    float best = -2.f;
    for (const auto& opp : state.players) {
        if (opp.team == player.team) continue;
        float oppSpeed = opp.vel.Length();
        if (oppSpeed < 200.f) continue;
        float s = -ballDir.Dot(opp.vel.Normalized());
        if (s > best) best = s;
    }
    return (best < -1.5f) ? 0.f : best;
}


// =============================================================================
// OutplayExplorationReward
// Three additive channels, each with independent cooldown.
//
//  A  Aerial outplay   — airborne touch redirecting ball away from challenger
//  B  Wall outplay     — wall-positioned touch redirecting ball away from challenger
//  C  Sharp redirect   — any touch with exceptional redirect geometry (score > 0.65)
// =============================================================================
class OutplayExplorationReward : public Reward {
public:
    // Channel A — aerial outplay
    static constexpr float AERIAL_MIN_Z        = 200.f;
    static constexpr float AERIAL_REDIRECT_MIN = 0.25f;
    static constexpr float AERIAL_BONUS_BASE   = 0.4f;
    static constexpr float AERIAL_BONUS_MAX    = 0.8f;
    static constexpr float AERIAL_COOLDOWN     = 2.5f;

    // Channel B — wall outplay
    static constexpr float WALL_Z_THRESH       = 300.f;
    static constexpr float WALL_X_THRESH       = 3400.f;
    static constexpr float WALL_REDIRECT_MIN   = 0.2f;
    static constexpr float WALL_BONUS          = 0.5f;
    static constexpr float WALL_COOLDOWN       = 2.5f;

    // Channel C — sharp redirect geometry
    static constexpr float SHARP_REDIRECT_MIN  = 0.65f;
    static constexpr float SHARP_BONUS_MAX     = 0.6f;
    static constexpr float SHARP_COOLDOWN      = 2.0f;

private:
    struct PlayerState {
        float aerialCooldown = 0.f;
        float wallCooldown   = 0.f;
        float sharpCooldown  = 0.f;
    };
    std::unordered_map<uint32_t, PlayerState> _states;

    bool IsOnWall(const Player& p) const {
        return p.isOnGround
            && (p.pos.z > WALL_Z_THRESH || std::abs(p.pos.x) > WALL_X_THRESH);
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
            s.aerialCooldown = std::max(0.f, s.aerialCooldown - dt);
            s.wallCooldown   = std::max(0.f, s.wallCooldown   - dt);
            s.sharpCooldown  = std::max(0.f, s.sharpCooldown  - dt);
        }
    }

    float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.f;
        if (!IsOpponentThreateningBall(player, state)) return 0.f;

        auto& s     = _states[player.carId];
        float redir = GetOutplayRedirectScore(player, state);
        float reward = 0.f;

        // ── Channel A: Aerial outplay ─────────────────────────────────────────
        if (s.aerialCooldown <= 0.f
            && !player.isOnGround
            && state.ball.pos.z > AERIAL_MIN_Z
            && redir > AERIAL_REDIRECT_MIN)
        {
            float t = std::min((redir - AERIAL_REDIRECT_MIN) / (1.f - AERIAL_REDIRECT_MIN), 1.f);
            reward += AERIAL_BONUS_BASE + t * (AERIAL_BONUS_MAX - AERIAL_BONUS_BASE);
            s.aerialCooldown = AERIAL_COOLDOWN;
        }

        // ── Channel B: Wall outplay ───────────────────────────────────────────
        if (s.wallCooldown <= 0.f
            && IsOnWall(player)
            && redir > WALL_REDIRECT_MIN)
        {
            reward += WALL_BONUS;
            s.wallCooldown = WALL_COOLDOWN;
        }

        // ── Channel C: Sharp redirect geometry ───────────────────────────────
        if (s.sharpCooldown <= 0.f && redir > SHARP_REDIRECT_MIN) {
            float t = std::min((redir - SHARP_REDIRECT_MIN) / (1.f - SHARP_REDIRECT_MIN), 1.f);
            reward += t * SHARP_BONUS_MAX;
            s.sharpCooldown = SHARP_COOLDOWN;
        }

        return reward;
    }
};


// =============================================================================
// OutplayCreativityReward
// Two additive channels, each with independent cooldown.
// Both require IsOpponentThreateningBall + redirect score > REDIRECT_MIN.
//
//  A  Unusual car state — inverted or sideways car at outplay touch moment
//  B  Low-boost commit  — player.boost < 30 during outplay touch
// =============================================================================
class OutplayCreativityReward : public Reward {
public:
    // Channel A — car orientation at outplay
    static constexpr float INVERTED_UP_Z   = -0.3f;
    static constexpr float SIDEWAYS_UP_Z   = 0.3f;
    static constexpr float INVERTED_BONUS  = 0.7f;
    static constexpr float SIDEWAYS_BONUS  = 0.4f;
    static constexpr float ORIENT_COOLDOWN = 2.5f;

    // Channel B — low-boost commitment
    static constexpr float LOW_BOOST_THRESH = 30.f;
    static constexpr float LOW_BOOST_MAX    = 0.5f;
    static constexpr float BOOST_COOLDOWN   = 2.5f;

    // Shared gate
    static constexpr float REDIRECT_MIN = 0.2f;

private:
    struct PlayerState {
        float orientCooldown = 0.f;
        float boostCooldown  = 0.f;
    };
    std::unordered_map<uint32_t, PlayerState> _states;

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
            s.orientCooldown = std::max(0.f, s.orientCooldown - dt);
            s.boostCooldown  = std::max(0.f, s.boostCooldown  - dt);
        }
    }

    float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.f;
        if (!IsOpponentThreateningBall(player, state)) return 0.f;

        float redir = GetOutplayRedirectScore(player, state);
        if (redir < REDIRECT_MIN) return 0.f;

        auto& s = _states[player.carId];
        float reward = 0.f;

        // ── Channel A: Unusual orientation ───────────────────────────────────
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

        // ── Channel B: Low-boost commitment ──────────────────────────────────
        if (s.boostCooldown <= 0.f && player.boost < LOW_BOOST_THRESH) {
            float t = 1.f - (player.boost / LOW_BOOST_THRESH);
            reward += t * LOW_BOOST_MAX;
            s.boostCooldown = BOOST_COOLDOWN;
        }

        return reward;
    }
};

} // namespace RLGC
