// ============================================================================
// SecondBallAnticipationReward.h
// Rev 1.0 (2026-06-12, NJH)
//
// CONCEPT: Second Ball / Rebound Anticipation
//
// SOCCER ANALOGY:
//   After a contested first touch (headed duel, 50/50, goalkeeper punch,
//   or defensive clear), elite players are already moving to where the ball
//   will land — not chasing where it is. The player who wins the second ball
//   gets a clean touch in space while opponents are still recovering from the
//   first contact. This is a deliberate, trained skill: read the trajectory,
//   hold position in the predicted landing zone, arrive at speed.
//
//   In RL: the SECOND-MAN (MIDDLE role player) should not chase the first
//   contact. Instead, they should position toward the predicted ball landing
//   zone (1.0–1.5s ahead on the BallPred trajectory) while the FIRST-MAN
//   contests. When the ball arrives in that zone, the second-man is already
//   moving and gets a quality touch.
//
// ─────────────────────────────────────────────────────────────────
// TRIGGER CONTEXTS (either opens a second-ball window):
//
//   A. CONTESTED 50/50
//      Both a teammate AND an opponent touched the ball within CONTESTED_DIST
//      UU of each other within a CONTESTED_WINDOW_SEC time window.
//      OR: opponent touches while a teammate was within CONTESTED_DIST at
//      contact time (teammate was first-man challenging).
//
//   B. DEFENSIVE CLEAR
//      Any teammate touch in own defensive half where:
//        - Ball speed after touch >= CLEAR_MIN_SPEED UU/s
//        - Ball velocity directed away from own goal (dot product gate)
//      Mirrors BigClearReward detection logic for consistency.
//
// ─────────────────────────────────────────────────────────────────
// REWARD STRUCTURE — two tiers active during the window:
//
//   TIER 1 — TRAJECTORY SHAPING (continuous, light)
//     Active the entire window while player vel direction points within
//     TRAJECTORY_CONE_DOT of the predicted landing zone centroid.
//     Provides dense gradient during the approach phase.
//     Scale: TRAJ_SHAPING_MAX per frame, falls off linearly with angle error.
//
//   TIER 2 — ZONE ARRIVAL (event + proximity, heavier)
//     Fires when player enters ARRIVAL_RADIUS of the predicted landing zone.
//     One-shot per window per player.
//     Scale: ARRIVAL_BASE + speed bonus (rewards arriving fast, not camping).
//     Speed bonus: player speed at arrival / CAR_MAX_SPEED * ARRIVAL_SPEED_BONUS
//
// ─────────────────────────────────────────────────────────────────
// PREDICTED LANDING ZONE:
//   Centroid of BallPred::Cache slices from PRED_NEAR_SEC to PRED_FAR_SEC.
//   Using a centroid across 1.0–1.5s of trajectory rather than a single
//   point gives a more robust target that handles wall bounces gracefully.
//   Computed once in PreStep via _ballCache; reused per-player in GetReward.
//
// ─────────────────────────────────────────────────────────────────
// ROLE GATE:
//   Only fires for MIDDLE role (second-man) via DetermineRoleCommon().
//   Uses the same velocity-adjusted distance as RoleAwarenessReward for
//   consistency. 1v1 excluded (no MIDDLE role exists without teammates).
//
// ─────────────────────────────────────────────────────────────────
// ANTI-FARMING:
//   - Role gate: only MIDDLE player rewards per step (max 1 in 2v2, 1 in 3v3)
//   - One-shot zone arrival per window
//   - Arrival radius tight enough to require real convergence (not camping)
//   - Arrival speed bonus penalises parking in the zone
//   - Per-player cooldown after window closes
//   - Window cannot re-open while cooldown is active
//   - Trajectory shaping capped per-frame (TRAJ_SHAPING_MAX)
//
// ─────────────────────────────────────────────────────────────────
// W&B METRICS:
//   SecondBall/windows_opened        — contested/clear events that opened a window
//   SecondBall/context_50_50         — windows from 50/50 context
//   SecondBall/context_clear         — windows from defensive clear context
//   SecondBall/arrival_rewards_fired — zone arrival events paid
//   SecondBall/traj_shaping_frames   — frames of trajectory shaping paid
//   SecondBall/avg_arrival_speed     — mean player speed at zone arrival
//   SecondBall/window_expired_rate   — fraction of windows closed without arrival
//
// PHASE:        5+ (requires team structure + BallPred accuracy)
// ZERO SUM:     Yes — second-ball positioning is a zero-sum advantage
// SHADOW:       true by default
//
// SUGGESTED WEIGHTS:
//   Shadow (M2, Phase 5):  0.0f (shadowMode=true)
//   Live Phase 5:          6.0f
//   Live Phase 6:          10.0f
// ============================================================================

#pragma once
#include "Reward.h"
#include "BallPrediction.h"
#include "../CommonValues.h"
#include "RotationRewards.h"   // DetermineRoleCommon, RoleType
#include <unordered_map>
#include <atomic>

namespace RLGC {

class SecondBallAnticipationReward : public Reward {
public:
    // -----------------------------------------------------------------------
    // Config
    // -----------------------------------------------------------------------
    struct Config {
        // ── Predicted landing zone ──────────────────────────────────────────
        float predNearSec       = 1.0f;   // Start of zone centroid window (sec)
        float predFarSec        = 1.5f;   // End of zone centroid window (sec)

        // ── Contested 50/50 detection ────────────────────────────────────────
        float contestedDist     = 1200.0f; // UU — opp within this of teammate at touch = contested
        float contestedWindowSec= 0.25f;  // sec — window to detect mutual touch (50/50)

        // ── Defensive clear detection ─────────────────────────────────────────
        float clearMinSpeed     = 800.0f;  // UU/s — min ball speed after touch to count as clear
        // Ball must be in own defensive half (checked via team + ball.pos.y sign)

        // ── Window duration ───────────────────────────────────────────────────
        float windowSec         = 2.5f;   // How long the second-ball window stays open

        // ── Tier 1: Trajectory shaping ───────────────────────────────────────
        float trajConeDot       = 0.6f;   // cos(~53°) — player vel must point within this of zone
        float trajShadingMax    = 0.08f;  // max shaping reward per frame at perfect alignment
        // Shaping scales linearly: 0 at dot=trajConeDot, trajShadingMax at dot=1.0

        // ── Tier 2: Zone arrival ──────────────────────────────────────────────
        float arrivalRadius     = 600.0f;  // UU — player must enter this sphere around centroid
        float arrivalBase       = 0.45f;   // base payout on zone entry
        float arrivalSpeedBonus = 0.25f;  // added at full CAR_MAX_SPEED (linear scale)

        // ── Cooldown ──────────────────────────────────────────────────────────
        float cooldownSec       = 2.5f;

        bool  shadowMode        = true;
    };

private:
    Config cfg;

    // -----------------------------------------------------------------------
    // Per-player state
    // -----------------------------------------------------------------------
    struct SecondBallState {
        float windowRemaining   = 0.0f;
        float windowElapsed     = 0.0f;
        float cooldownRemaining = 0.0f;
        bool  arrivalFired      = false;   // one-shot zone arrival this window
        bool  windowEverRewarded= false;

        // Touch tracking for 50/50 detection (per player)
        float timeSinceTeamTouch = 999.0f; // time since nearest teammate touched ball
        float timeSinceOppTouch  = 999.0f; // time since nearest opponent touched ball
    };
    std::unordered_map<uint32_t, SecondBallState> _states;

    // Predicted landing zone centroid — computed in PreStep, shared across players
    Vec  _zoneCentroid     = {};
    bool _zoneValid        = false;
    BallPred::Cache _ballCache;

    float _dt = 1.0f / 120.0f;

    // -----------------------------------------------------------------------
    // Metrics
    // -----------------------------------------------------------------------
    static std::atomic<int>       s_windowsOpened;
    static std::atomic<int>       s_context5050;
    static std::atomic<int>       s_contextClear;
    static std::atomic<int>       s_arrivalFired;
    static std::atomic<int>       s_trajFrames;
    static std::atomic<long long> s_arrivalSpeedMilli;
    static std::atomic<int>       s_windowsExpired;
    static std::atomic<int>       s_windowsTotal;

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------
    static bool IsKickoff(const GameState& state) {
        return state.ball.pos.Length() < 300.0f &&
               state.ball.vel.Length() < 200.0f;
    }

    // Returns true if `p` is in own defensive half
    static bool InOwnHalf(const Player& p, const GameState& state) {
        // BLUE defends negative Y, ORANGE defends positive Y
        return (p.team == Team::BLUE)
            ? (state.ball.pos.y < 0.0f)
            : (state.ball.pos.y > 0.0f);
    }

    // Closest opponent distance to the ball at this moment
    static float ClosestOppDistToBall(const Player& player, const GameState& state) {
        float best = 1e9f;
        for (const auto& p : state.players) {
            if (p.team == player.team || p.isDemoed) continue;
            float d = (p.pos - state.ball.pos).Length();
            if (d < best) best = d;
        }
        return best;
    }

    // Is this player the MIDDLE role (second-man) per DetermineRoleCommon?
    static bool IsSecondMan(const Player& player, const GameState& state) {
        // Need at least one teammate — no second-man in 1v1
        bool hasTeammate = false;
        for (const auto& p : state.players) {
            if (p.team == player.team && p.carId != player.carId && !p.isDemoed) {
                hasTeammate = true;
                break;
            }
        }
        if (!hasTeammate) return false;
        return DetermineRoleCommon(player, state) == RoleType::MIDDLE;
    }

    // Compute the zone centroid: mean position of BallPred slices in [nearSec, farSec]
    Vec ComputeZoneCentroid() const {
        if (!_ballCache.valid) return {};

        Vec sum   = {};
        int count = 0;

        for (int i = 0; i < BallPred::NUM_SLICES; ++i) {
            const auto& sl = _ballCache.slices[i];
            if (sl.t < cfg.predNearSec) continue;
            if (sl.t > cfg.predFarSec)  break;
            sum.x += sl.pos.x;
            sum.y += sl.pos.y;
            sum.z += sl.pos.z;
            ++count;
        }

        if (count == 0) return {};
        return Vec(sum.x / count, sum.y / count, sum.z / count);
    }

    // Detect whether a teammate touch this step constitutes a "contested" event:
    // either (a) an opponent was within contestedDist at touch time, or
    // (b) a reciprocal opp touch happened within contestedWindowSec.
    // Uses per-player timeSinceOppTouch tracked in PreStep.
    bool IsContested(const Player& firstMan, const GameState& state,
                     const SecondBallState& fmState) const {
        // Gate A: opponent within contestedDist of first-man at contact
        float oppDist = ClosestOppDistToBall(firstMan, state);
        if (oppDist < cfg.contestedDist) return true;

        // Gate B: opponent touched within the window (mutual 50/50)
        if (fmState.timeSinceOppTouch < cfg.contestedWindowSec) return true;

        return false;
    }

    // Detect whether a teammate touch is a defensive clear.
    // Mirrors BigClearReward: in own half, high ball speed, away from own goal.
    bool IsDefensiveClear(const Player& firstMan, const GameState& state) const {
        if (!InOwnHalf(firstMan, state)) return false;

        float ballSpeed = state.ball.vel.Length();
        if (ballSpeed < cfg.clearMinSpeed) return false;

        Vec ownGoal = (firstMan.team == Team::BLUE)
            ? CommonValues::BLUE_GOAL_BACK
            : CommonValues::ORANGE_GOAL_BACK;
        Vec awayFromGoal = (state.ball.pos - ownGoal).Normalized();
        Vec ballDir      = state.ball.vel.Normalized();

        return ballDir.Dot(awayFromGoal) > 0.3f; // roughly aimed away from own goal
    }

public:
    explicit SecondBallAnticipationReward(Config c = Config{}) : cfg(c) {}

    // -----------------------------------------------------------------------
    // Reset
    // -----------------------------------------------------------------------
    void Reset(const GameState& state) override {
        _states.clear();
        _zoneValid = false;
        for (const auto& p : state.players)
            _states[p.carId] = SecondBallState{};
    }

    // -----------------------------------------------------------------------
    // PreStep — owns all state transitions
    // -----------------------------------------------------------------------
    void PreStep(const GameState& state) override {
        _dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

        // Update ball prediction cache first
        _ballCache.Update(state);

        // Recompute zone centroid every step (ball trajectory changes after each touch)
        if (_ballCache.valid) {
            _zoneCentroid = ComputeZoneCentroid();
            _zoneValid    = true;
        } else {
            _zoneValid = false;
        }

        if (IsKickoff(state)) {
            for (auto& [id, st] : _states) {
                st.timeSinceTeamTouch  = 999.0f;
                st.timeSinceOppTouch   = 999.0f;
                st.cooldownRemaining   = RS_MAX(0.0f, st.cooldownRemaining - _dt);
                st.windowRemaining     = 0.0f;
            }
            return;
        }

        // ── Phase 1: Tick all timers ─────────────────────────────────────────
        for (auto& [id, st] : _states) {
            st.cooldownRemaining  = RS_MAX(0.0f, st.cooldownRemaining - _dt);
            st.windowRemaining    = RS_MAX(0.0f, st.windowRemaining   - _dt);
            st.timeSinceTeamTouch += _dt;
            st.timeSinceOppTouch  += _dt;

            if (st.windowRemaining > 0.0f)
                st.windowElapsed += _dt;

            // Track window expiry
            if (st.windowRemaining <= 0.0f && st.windowElapsed > 0.0f
                && !st.windowEverRewarded) {
                s_windowsExpired.fetch_add(1);
                st.windowElapsed = 0.0f;
            }
        }

        // ── Phase 2: Record touches this step ────────────────────────────────
        // We need to know, for each player, whether a teammate or opponent touched.
        // Record touches first, then evaluate window openings below.
        for (const auto& p : state.players) {
            if (!p.ballTouchedStep) continue;

            // Broadcast this touch to all other players on the same / opposite team
            for (auto& [id, st] : _states) {
                if (id == p.carId) continue;

                // Find the player with this id to check team
                for (const auto& q : state.players) {
                    if (q.carId != id) continue;
                    if (q.team == p.team) st.timeSinceTeamTouch = 0.0f;
                    else                   st.timeSinceOppTouch  = 0.0f;
                    break;
                }
            }
        }

        // ── Phase 3: Evaluate window-opening events ───────────────────────────
        // A window opens for SECOND-MAN players when a FIRST-MAN teammate makes
        // a qualifying touch (contested OR defensive clear).
        for (const auto& firstMan : state.players) {
            if (!firstMan.ballTouchedStep) continue;  // only on touch frames
            if (firstMan.isDemoed)         continue;

            // Determine context
            auto& fmState = _states[firstMan.carId];
            bool contested = IsContested(firstMan, state, fmState);
            bool defClear  = IsDefensiveClear(firstMan, state);

            if (!contested && !defClear) continue;

            // Open a window for this player's MIDDLE-role teammates
            // (the second-man players who should be anticipating)
            for (const auto& p : state.players) {
                if (p.team  != firstMan.team) continue;
                if (p.carId == firstMan.carId) continue;
                if (p.isDemoed) continue;

                // Only open for the MIDDLE role player
                if (DetermineRoleCommon(p, state) != RoleType::MIDDLE) continue;

                auto& st = _states[p.carId];
                if (st.cooldownRemaining > 0.0f) continue;
                if (st.windowRemaining   > 0.0f) continue; // already open

                st.windowRemaining    = cfg.windowSec;
                st.windowElapsed      = 0.0f;
                st.arrivalFired       = false;
                st.windowEverRewarded = false;
                s_windowsOpened.fetch_add(1);
                s_windowsTotal.fetch_add(1);
                if (contested) s_context5050.fetch_add(1);
                if (defClear)  s_contextClear.fetch_add(1);
            }
        }
    }

    // -----------------------------------------------------------------------
    // GetReward — read-only except one-shot arrival flag
    // -----------------------------------------------------------------------
    float GetReward(const Player& player, const GameState& state,
                    bool /*isFinal*/) override {

        if (IsKickoff(state))  return 0.0f;
        if (player.isDemoed)   return 0.0f;
        if (!_zoneValid)       return 0.0f;

        // Second-man role gate (also excludes 1v1)
        if (!IsSecondMan(player, state)) return 0.0f;

        auto it = _states.find(player.carId);
        if (it == _states.end()) return 0.0f;
        SecondBallState& st = it->second;  // non-const: arrival flag write

        if (st.windowRemaining  <= 0.0f) return 0.0f;
        if (st.cooldownRemaining >  0.0f) return 0.0f;

        float reward = 0.0f;
        Vec   toZone = _zoneCentroid - player.pos;
        float distToZone = toZone.Length();

        // ── Tier 1: Trajectory shaping ─────────────────────────────────────
        // Reward while velocity points roughly toward the zone centroid.
        {
            float speed = player.vel.Length();
            if (speed > 100.0f) {
                Vec  velDir  = player.vel * (1.0f / speed);
                Vec  dirToZone = (distToZone > 1.0f)
                               ? toZone * (1.0f / distToZone)
                               : velDir;
                float dot = velDir.Dot(dirToZone);

                if (dot > cfg.trajConeDot) {
                    // Linear scale: 0 at cone edge, max at dot=1.0
                    float t = (dot - cfg.trajConeDot) / (1.0f - cfg.trajConeDot);
                    float shaping = t * cfg.trajShadingMax;
                    reward += shaping;
                    s_trajFrames.fetch_add(1);
                }
            }
        }

        // ── Tier 2: Zone arrival (one-shot) ────────────────────────────────
        if (!st.arrivalFired && distToZone < cfg.arrivalRadius) {
            float speed      = player.vel.Length();
            float speedFrac  = RS_CLAMP(speed / CommonValues::CAR_MAX_SPEED, 0.0f, 1.0f);
            float arrival    = cfg.arrivalBase + speedFrac * cfg.arrivalSpeedBonus;
            reward += arrival;

            st.arrivalFired       = true;
            st.windowEverRewarded = true;
            s_arrivalFired.fetch_add(1);
            s_arrivalSpeedMilli.fetch_add(static_cast<long long>(speed * 1000.0f));

            // Start cooldown once arrival fires
            st.cooldownRemaining = cfg.cooldownSec;
        }

        if (st.windowRemaining > 0.0f && reward > 0.0f)
            st.windowEverRewarded = true;

        if (cfg.shadowMode) return 0.0f;

        return reward;
    }

    // -----------------------------------------------------------------------
    // Metrics
    // -----------------------------------------------------------------------
    void CollectMetrics(std::unordered_map<std::string, float>& out) const {
        int total   = s_windowsTotal.load();
        int expired = s_windowsExpired.load();
        int arrival = s_arrivalFired.load();

        out["SecondBall/windows_opened"]        = static_cast<float>(s_windowsOpened.load());
        out["SecondBall/context_50_50"]         = static_cast<float>(s_context5050.load());
        out["SecondBall/context_clear"]         = static_cast<float>(s_contextClear.load());
        out["SecondBall/arrival_rewards_fired"] = static_cast<float>(arrival);
        out["SecondBall/traj_shaping_frames"]   = static_cast<float>(s_trajFrames.load());
        out["SecondBall/avg_arrival_speed"]     =
            arrival > 0
            ? static_cast<float>(s_arrivalSpeedMilli.load()) / arrival / 1000.0f
            : 0.0f;
        out["SecondBall/window_expired_rate"]   =
            total > 0
            ? static_cast<float>(expired) / static_cast<float>(total)
            : 0.0f;

        s_windowsOpened = s_context5050 = s_contextClear = 0;
        s_arrivalFired  = s_trajFrames  = 0;
        s_arrivalSpeedMilli = 0;
        s_windowsExpired = s_windowsTotal = 0;
    }
};

// -----------------------------------------------------------------------
// Static member definitions
// -----------------------------------------------------------------------
inline std::atomic<int>       SecondBallAnticipationReward::s_windowsOpened{0};
inline std::atomic<int>       SecondBallAnticipationReward::s_context5050{0};
inline std::atomic<int>       SecondBallAnticipationReward::s_contextClear{0};
inline std::atomic<int>       SecondBallAnticipationReward::s_arrivalFired{0};
inline std::atomic<int>       SecondBallAnticipationReward::s_trajFrames{0};
inline std::atomic<long long> SecondBallAnticipationReward::s_arrivalSpeedMilli{0};
inline std::atomic<int>       SecondBallAnticipationReward::s_windowsExpired{0};
inline std::atomic<int>       SecondBallAnticipationReward::s_windowsTotal{0};

} // namespace RLGC
