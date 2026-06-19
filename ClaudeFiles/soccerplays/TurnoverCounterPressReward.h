// ============================================================================
// TurnoverCounterPressReward.h
// Rev 1.0 (2026-06-12, NJH)
//
// CONCEPT: Gegenpressing — reward immediate high-speed closure on the ball
// carrier in the 2–3s window after your team loses possession.
//
// SOCCER ANALOGY:
//   When you lose the ball, the opponent is momentarily disorganized —
//   receivers aren't positioned, the carrier is adjusting to a new touch.
//   Pressing in this window disrupts their counter before it can form.
//   In RL: same principle. The 2–3s after a turnover are when the opponent
//   is most likely to mis-touch, be off-axis, or have no clear pass option.
//
// STRUCTURE:
//   Two-tier reward — event spike on press entry, then ramp-up shaping
//   while the press is sustained:
//
//     t=0 (turnover detected)  →  press window opens
//     t=0..ENTRY_WINDOW_SEC    →  ONE-SHOT entry reward (ENTRY_REWARD) fires
//                                 the first frame gates are satisfied
//     t=entry..window_end      →  continuous shaping reward ramps up linearly
//                                 from SHAPING_MIN to SHAPING_MAX over
//                                 SHAPING_RAMP_SEC, capped thereafter
//
// GATE CONDITIONS (all must be true simultaneously):
//   1. Press window is open (opponent gained possession via HasPossession())
//   2. At least 2 players per team in the episode (2v2+, 1v1 excluded)
//   3. This player is the closest teammate to the ball (first-man press role)
//   4. Player closing speed on ball >= MIN_CLOSING_SPEED UU/s
//   5. Player speed >= MIN_PRESS_SPEED UU/s
//   6. Not in kickoff phase
//   7. Player not demoed
//
// ANTI-FARMING:
//   - Entry reward fires once per press window per player (one-shot flag)
//   - Per-player cooldown (COOLDOWN_SEC) after window closes / reward ends
//   - Closest-player gate ensures max one presser per team per step
//   - Both speed gates prevent passive positioning near ball
//   - Window cannot be refreshed until cooldown expires
//
// POSSESSION MODEL:
//   Mirrors CompleteMechanicsRewards.h HasPossession():
//     - ballTouchedStep == true → immediate possession
//     - dist < POSSESSION_DIST AND vel alignment > VEL_ALIGN_DOT → possession
//   Team possession is evaluated once in PreStep; window opens on transition
//   from "our team had possession" → "opponent has possession".
//
// W&B METRICS:
//   CounterPress/windows_opened       — turnover events that opened a press window
//   CounterPress/entry_rewards_fired  — one-shot entry rewards paid
//   CounterPress/shaping_frames       — frames of continuous shaping reward paid
//   CounterPress/avg_closing_speed    — mean closing speed at shaping reward frames
//   CounterPress/window_expired_rate  — fraction of windows that expired unrewarded
//
// PHASE:   5+ (requires 2v2+ team structure to be meaningful)
// ZERO SUM: Yes — recommended. Opponent concedes contested space.
// SHADOW:  true by default — set shadowMode=false to go live
//
// SUGGESTED WEIGHTS:
//   Shadow (M2, Phase 5):  0.0f (shadowMode=true, log only)
//   Live Phase 5:          6.0f
//   Live Phase 6:          10.0f
// ============================================================================

#pragma once
#include "Reward.h"
#include "../CommonValues.h"
#include <unordered_map>
#include <atomic>

namespace RLGC {

class TurnoverCounterPressReward : public Reward {
public:
    // -----------------------------------------------------------------------
    // Config — all tuneable thresholds in one place
    // -----------------------------------------------------------------------
    struct Config {
        // Press window
        float pressWindowSec    = 2.5f;   // How long the window stays open after turnover

        // Entry (one-shot) reward
        float entryReward       = 0.5f;   // Spike paid once on first qualifying press frame
        float entryWindowSec    = 0.75f;  // Must enter press within this time of turnover
                                          // to qualify for the entry spike

        // Continuous shaping ramp
        float shapingMin        = 0.05f;  // Reward/frame at start of ramp
        float shapingMax        = 0.25f;  // Reward/frame at end of ramp (and beyond)
        float shapingRampSec    = 1.5f;   // Time to ramp from shapingMin → shapingMax

        // Speed gates
        float minPressSpeed     = 600.0f; // UU/s — car must be actively moving
        float minClosingSpeed   = 350.0f; // UU/s — must be closing distance on ball
                                          //         (relative to ball velocity)

        // Possession model (mirrors CompleteMechanicsRewards.h)
        float possessionDist    = 250.0f; // UU — distance threshold for possession
        float velAlignDot       = 0.3f;   // dot product threshold, player·ball vel alignment

        // Per-player cooldown after a press window ends (prevents immediate re-trigger)
        float cooldownSec       = 2.0f;

        // 1v1 exclusion: minimum teammates required (1 = need at least one, so 2v2+)
        int   minTeammates      = 1;

        bool  shadowMode        = true;   // true → log metrics but return 0.0f
    };

private:
    Config cfg;

    // -----------------------------------------------------------------------
    // Per-player press state
    // ALL writes happen in PreStep. GetReward is effectively read-only
    // except for the one-shot entry flag consumed on the firing frame.
    // -----------------------------------------------------------------------
    struct PressState {
        // Window tracking
        float pressWindowRemaining = 0.0f;  // counts down from pressWindowSec → 0
        float timeInPress          = 0.0f;  // time spent satisfying all gates this window
        float windowElapsed        = 0.0f;  // total time elapsed since window opened

        // One-shot entry reward
        bool  entryFired           = false; // true once entry reward has been paid

        // Possession tracking
        bool  teamHadPossession    = false; // did our team have possession last step?

        // Cooldown
        float cooldownRemaining    = 0.0f;

        // Metrics support
        bool  windowEverRewarded   = false; // did any reward fire this window?
    };

    std::unordered_map<uint32_t, PressState> _states;
    float _dt = 1.0f / 120.0f;

    // Team-level possession cache, computed once per PreStep
    // true = team currently has possession per HasPossession() logic
    bool _bluePossession   = false;
    bool _orangePossession = false;

    // -----------------------------------------------------------------------
    // Metrics — atomic for thread safety across parallel envs
    // -----------------------------------------------------------------------
    static std::atomic<int>       s_windowsOpened;
    static std::atomic<int>       s_entryFired;
    static std::atomic<int>       s_shapingFrames;
    static std::atomic<long long> s_closingSpeedMilli;   // sum × 1000
    static std::atomic<int>       s_windowsExpired;      // window closed, never rewarded
    static std::atomic<int>       s_windowsTotal;        // denominator for expired rate

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------
    static bool IsKickoff(const GameState& state) {
        return state.ball.pos.Length() < 300.0f &&
               state.ball.vel.Length() < 200.0f;
    }

    // Count living teammates (not self, not demoed)
    static int CountLivingTeammates(const Player& player, const GameState& state) {
        int n = 0;
        for (const auto& p : state.players) {
            if (p.team == player.team && p.carId != player.carId && !p.isDemoed)
                ++n;
        }
        return n;
    }

    // Mirrors HasPossession() from CompleteMechanicsRewards.h exactly.
    // Returns true if `p` currently has possession of the ball.
    bool HasPossession(const Player& p, const GameState& state) const {
        float dist = (p.pos - state.ball.pos).Length();
        if (dist > cfg.possessionDist) return false;

        // Criterion 1: just touched this step
        if (p.ballTouchedStep) return true;

        // Criterion 2: moving in same direction as ball (control without touch)
        if (p.vel.Length() > 100.0f && state.ball.vel.Length() > 100.0f) {
            Vec pDir = p.vel.Normalized();
            Vec bDir = state.ball.vel.Normalized();
            if (pDir.Dot(bDir) > cfg.velAlignDot) return true;
        }

        return false;
    }

    // Returns true if this player is the closest teammate to the ball.
    // Ties broken by lowest carId to guarantee determinism.
    static bool IsFirstManToBall(const Player& player, const GameState& state) {
        float myDist = (player.pos - state.ball.pos).Length();
        for (const auto& p : state.players) {
            if (p.team != player.team) continue;
            if (p.carId == player.carId) continue;
            if (p.isDemoed) continue;
            float d = (p.pos - state.ball.pos).Length();
            if (d < myDist) return false;
            // tie-break: lower carId wins
            if (d == myDist && p.carId < player.carId) return false;
        }
        return true;
    }

    // Closing speed: component of (player vel - ball vel) toward the ball.
    // Positive = closing, negative = opening.
    static float ClosingSpeed(const Player& player, const GameState& state) {
        Vec toBall = state.ball.pos - player.pos;
        float dist = toBall.Length();
        if (dist < 1.0f) return 0.0f;
        Vec dirToBall = toBall * (1.0f / dist);
        Vec relVel    = player.vel - state.ball.vel;
        return relVel.Dot(dirToBall);
    }

    // Evaluate team possession for both teams from the full player list.
    // Called once per PreStep; results cached in _bluePossession/_orangePossession.
    void UpdateTeamPossession(const GameState& state) {
        _bluePossession   = false;
        _orangePossession = false;
        for (const auto& p : state.players) {
            if (p.isDemoed) continue;
            if (HasPossession(p, state)) {
                if (p.team == Team::BLUE)   _bluePossession   = true;
                else                         _orangePossession = true;
            }
        }
    }

public:
    explicit TurnoverCounterPressReward(Config c = Config{}) : cfg(c) {}

    // -----------------------------------------------------------------------
    // Reset — episode start
    // -----------------------------------------------------------------------
    void Reset(const GameState& state) override {
        _states.clear();
        _bluePossession   = false;
        _orangePossession = false;
        for (const auto& p : state.players)
            _states[p.carId] = PressState{};
    }

    // -----------------------------------------------------------------------
    // PreStep — one call per step, before all GetReward() calls.
    // Owns all state transitions and window lifecycle.
    // -----------------------------------------------------------------------
    void PreStep(const GameState& state) override {
        _dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

        if (IsKickoff(state)) {
            // Reset possession tracking during kickoff so stale state
            // doesn't open a bogus press window on kickoff contact.
            for (auto& [id, st] : _states) {
                st.teamHadPossession    = false;
                st.pressWindowRemaining = 0.0f;
                st.cooldownRemaining    = RS_MAX(0.0f, st.cooldownRemaining - _dt);
            }
            return;
        }

        // Evaluate possession for both teams this step (one pass, all players)
        UpdateTeamPossession(state);

        for (const auto& p : state.players) {
            auto& st = _states[p.carId];

            // Tick down timers
            st.cooldownRemaining    = RS_MAX(0.0f, st.cooldownRemaining - _dt);
            st.pressWindowRemaining = RS_MAX(0.0f, st.pressWindowRemaining - _dt);

            if (st.pressWindowRemaining > 0.0f)
                st.windowElapsed += _dt;

            // ----------------------------------------------------------------
            // Possession transition detection: did we JUST lose possession?
            // ----------------------------------------------------------------
            bool myTeamHasPossNow = (p.team == Team::BLUE) ? _bluePossession
                                                            : _orangePossession;
            bool oppHasPossNow    = (p.team == Team::BLUE) ? _orangePossession
                                                            : _bluePossession;

            bool justLostPossession = st.teamHadPossession      // we had it last step
                                   && oppHasPossNow              // opponent has it now
                                   && !myTeamHasPossNow          // we don't still have it
                                   && st.cooldownRemaining <= 0.0f;

            if (justLostPossession && st.pressWindowRemaining <= 0.0f) {
                // Open a fresh press window
                st.pressWindowRemaining = cfg.pressWindowSec;
                st.windowElapsed        = 0.0f;
                st.timeInPress          = 0.0f;
                st.entryFired           = false;
                st.windowEverRewarded   = false;
                s_windowsOpened.fetch_add(1);
                s_windowsTotal.fetch_add(1);
            }

            // Track expiry (window just hit zero without ever being rewarded)
            if (st.pressWindowRemaining <= 0.0f && st.windowElapsed > 0.0f
                && !st.windowEverRewarded) {
                s_windowsExpired.fetch_add(1);
                st.windowElapsed = 0.0f; // reset so we don't double-count
            }

            // Update possession memory for next step
            st.teamHadPossession = myTeamHasPossNow;
        }
    }

    // -----------------------------------------------------------------------
    // GetReward — read-only from _states (except consuming the entry flag).
    // -----------------------------------------------------------------------
    float GetReward(const Player& player, const GameState& state,
                    bool /*isFinal*/) override {

        // Hard exclusions
        if (IsKickoff(state))    return 0.0f;
        if (player.isDemoed)     return 0.0f;

        // 1v1 exclusion — need at least cfg.minTeammates living teammates
        if (CountLivingTeammates(player, state) < cfg.minTeammates) return 0.0f;

        auto it = _states.find(player.carId);
        if (it == _states.end()) return 0.0f;
        PressState& st = it->second;  // intentionally non-const: entry flag write

        // Press window must be open
        if (st.pressWindowRemaining <= 0.0f) return 0.0f;

        // Cooldown guard (shouldn't trigger mid-window, but safety check)
        if (st.cooldownRemaining > 0.0f) return 0.0f;

        // Must be first-man (closest to ball on team)
        if (!IsFirstManToBall(player, state)) return 0.0f;

        // Speed gates
        if (player.vel.Length() < cfg.minPressSpeed) return 0.0f;

        float closing = ClosingSpeed(player, state);
        if (closing < cfg.minClosingSpeed) return 0.0f;

        // ---- All gates passed ----

        float reward = 0.0f;

        // Tier 1: one-shot entry reward (fires once, within entryWindowSec of open)
        if (!st.entryFired && st.windowElapsed <= cfg.entryWindowSec) {
            reward += cfg.entryReward;
            st.entryFired = true;
            s_entryFired.fetch_add(1);
        }

        // Tier 2: continuous shaping reward, ramps up over shapingRampSec
        float rampT   = RS_CLAMP(st.timeInPress / cfg.shapingRampSec, 0.0f, 1.0f);
        float shaping = cfg.shapingMin + rampT * (cfg.shapingMax - cfg.shapingMin);
        reward += shaping;

        // Accumulate press time for ramp
        st.timeInPress += _dt;
        st.windowEverRewarded = true;

        // Metrics
        s_shapingFrames.fetch_add(1);
        s_closingSpeedMilli.fetch_add(static_cast<long long>(closing * 1000.0f));

        if (cfg.shadowMode) return 0.0f;

        return reward;
    }

    // -----------------------------------------------------------------------
    // CollectMetrics — call once per logging interval, resets accumulators
    // -----------------------------------------------------------------------
    void CollectMetrics(std::unordered_map<std::string, float>& out) const {
        int windows  = s_windowsTotal.load();
        int expired  = s_windowsExpired.load();
        int shaping  = s_shapingFrames.load();

        out["CounterPress/windows_opened"]      = static_cast<float>(s_windowsOpened.load());
        out["CounterPress/entry_rewards_fired"] = static_cast<float>(s_entryFired.load());
        out["CounterPress/shaping_frames"]      = static_cast<float>(shaping);
        out["CounterPress/avg_closing_speed"]   =
            shaping > 0
            ? static_cast<float>(s_closingSpeedMilli.load()) / shaping / 1000.0f
            : 0.0f;
        out["CounterPress/window_expired_rate"] =
            windows > 0
            ? static_cast<float>(expired) / static_cast<float>(windows)
            : 0.0f;

        // Reset all accumulators
        s_windowsOpened   = 0;
        s_entryFired      = 0;
        s_shapingFrames   = 0;
        s_closingSpeedMilli = 0;
        s_windowsExpired  = 0;
        s_windowsTotal    = 0;
    }
};

// -----------------------------------------------------------------------
// Static member definitions
// -----------------------------------------------------------------------
inline std::atomic<int>       TurnoverCounterPressReward::s_windowsOpened{0};
inline std::atomic<int>       TurnoverCounterPressReward::s_entryFired{0};
inline std::atomic<int>       TurnoverCounterPressReward::s_shapingFrames{0};
inline std::atomic<long long> TurnoverCounterPressReward::s_closingSpeedMilli{0};
inline std::atomic<int>       TurnoverCounterPressReward::s_windowsExpired{0};
inline std::atomic<int>       TurnoverCounterPressReward::s_windowsTotal{0};

} // namespace RLGC
