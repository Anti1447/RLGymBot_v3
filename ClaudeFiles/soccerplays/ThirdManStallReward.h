// ============================================================================
// ThirdManStallReward.h
// Rev 1.0 (2026-06-12, NJH)
//
// CONCEPT: Sweeper Time-Buy / Stall
//
// SOCCER ANALOGY:
//   When a team wins the ball back deep in their own half while teammates are
//   still recovering from an attacking push, the last defender's job is NOT
//   to immediately clear or shoot. They absorb time: dribbling laterally,
//   inviting the press, shielding — consuming 2–4 seconds until runners are
//   back in shape and passing lanes open. The value is entirely relative to
//   teammate position. Same action, wrong team shape = wasted possession.
//
// TRIGGER CONTEXT:
//   All five gates must be true simultaneously (anti-farming by design):
//     1. FURTHEST role (third man / last defender) via ThirdManContext
//     2. Ball in OWN half (player's defensive territory)
//     3. Player has possession (ballTouchedStep OR within possession distance)
//     4. At least one teammate is in opponent half (needs recovery time)
//        — sourced from ThirdManContext.teammatesRecovering
//     5. Opponent is closing on the ball at >= MIN_PRESSURE_SPEED UU/s
//        — sourced from ThirdManContext.opponentClosingOnBall
//
// REWARD STRUCTURE:
//   Continuous shaping reward per frame while all gates hold.
//   Scales with:
//     - Opponent pressure (higher pressure = more valuable to stall)
//     - Lateral movement component (shielding / moving perpendicular to threat)
//   Capped at SHAPING_MAX per frame. No event spike — pure shaping.
//
// EXIT CONDITIONS (reward turns off, no penalty):
//   A. All teammates return to own half (stall no longer needed)
//   B. Opponent overcommits: closing speed drops below MIN_PRESSURE_SPEED
//      AND a clear passing lane opens (reward switches off; counter expected)
//   C. Player loses possession (ball too far or no recent touch)
//   D. ThirdManContext.counterLaunchFiredThisStep == true for this team
//      (teammate launched a counter — stall phase is over)
//
// GRADIENT CONFLICT MITIGATIONS:
//   vs PossessionControlReward:
//     Stall only fires when FURTHEST role + own half + teammate recovering.
//     PossessionControlReward fires for any player in any half. Overlap zone
//     is narrow (FURTHEST + own half + possession) but real. Mitigated by
//     keeping stall weight low (4–6f) vs PossessionControl (15f) so the
//     signals are distinguishably different in magnitude.
//
//   vs RetreatAfterTouchReward:
//     RetreatAfterTouchReward checks ballTouchedStep and rewards moving AWAY.
//     Stall rewards staying near ball. Direct opposition.
//     MITIGATION: ThirdManContext.stallActiveThisStep[team] is set to true
//     when this reward fires. RetreatAfterTouchReward should check this flag
//     and return 0.0f if set. See RetreatAfterTouchReward integration note.
//
//   vs BigClearReward:
//     BigClearReward fires on hard clears in own half. Stall fires on soft
//     possession hold in own half. They are mutually exclusive by ball speed:
//     BigClearReward requires ballVel >= 300 UU/s; stall fires on slow/held
//     ball (speed < STALL_MAX_BALL_SPEED). No explicit flag needed.
//
//   vs SimpleOvercommitmentPenalty Gate 3 (stranded in opp half):
//     Gate 3 fires when in OPP half with low boost. Stall fires in OWN half.
//     No overlap.
//
// W&B METRICS:
//   ThirdManStall/frames_active         — frames stall reward fired
//   ThirdManStall/avg_pressure          — mean opponent closing speed during stall
//   ThirdManStall/avg_lateral_component — mean lateral movement quality
//   ThirdManStall/teammate_recovery_rate— fraction of stall windows that ended
//                                         with all teammates recovered
//   ThirdManStall/possession_lost_rate  — fraction ended by possession loss
//
// PHASE:     5+ (needs FURTHEST role detection, 2v2+)
// ZERO SUM:  No — this is self-contained third-man behavior, not directly
//            competitive. Add ZeroSum wrapper only if win-correlation is weak.
// SHADOW:    true by default
//
// SUGGESTED WEIGHTS:
//   Shadow Phase 5:  0.0f (shadowMode=true)
//   Live Phase 5:    4.0f
//   Live Phase 6:    6.0f
//
// INTEGRATION NOTE for RetreatAfterTouchReward:
//   In RotationRewards.h, RetreatAfterTouchReward::GetReward(), add at top:
//
//     // Don't penalize deliberate stall by third man
//     extern ThirdManContext* g_thirdManCtx;  // or pass by ref
//     int ti = (player.team == Team::BLUE) ? 0 : 1;
//     if (g_thirdManCtx && g_thirdManCtx->stallActiveThisStep[ti]) return 0.0f;
//
//   Alternatively, wire via the shared context pointer in RewardConfig.cpp.
// ============================================================================

#pragma once
#include "Reward.h"
#include "ThirdManContext.h"
#include "../CommonValues.h"
#include <unordered_map>
#include <atomic>

namespace RLGC {

class ThirdManStallReward : public Reward {
public:
    struct Config {
        // Possession detection (mirrors CompleteMechanicsRewards.h)
        float possessionDist    = 250.0f;  // UU — max dist to ball
        float velAlignDot       = 0.3f;    // player·ball vel alignment threshold

        // Pressure gate — opponent must be closing at this speed to count
        float minPressureSpeed  = 300.0f;  // UU/s (fairly lenient — early press counts)

        // Ball speed cap — if ball is moving this fast, it's a clear not a stall
        float stallMaxBallSpeed = 800.0f;  // UU/s

        // Shaping reward parameters
        float shapingBase       = 0.04f;   // reward/frame at minimum pressure
        float shapingMax        = 0.12f;   // reward/frame at max pressure
        float pressureScale     = 0.001f;  // additional reward per UU/s above min
        float lateralBonus      = 0.03f;   // bonus when moving laterally (shielding)
        float lateralDotMax     = 0.4f;    // |dot(vel, toOpponent)| < this = lateral

        // Per-player cooldown after stall window ends
        float cooldownSec       = 1.5f;

        bool  shadowMode        = true;
    };

private:
    ThirdManContext& _ctx;
    Config           _cfg;

    struct StallState {
        float  cooldownRemaining    = 0.0f;
        bool   wasStalling          = false;
        float  stallDuration        = 0.0f;
        // Exit tracking for metrics
        bool   endedByRecovery      = false;
        bool   endedByPossLoss      = false;
    };
    std::unordered_map<uint32_t, StallState> _states;
    float _dt = 1.0f / 120.0f;

    // Metrics
    static std::atomic<int>       s_framesActive;
    static std::atomic<long long> s_pressureMilli;
    static std::atomic<long long> s_lateralMilli;
    static std::atomic<int>       s_recoveryEnds;
    static std::atomic<int>       s_possLossEnds;
    static std::atomic<int>       s_totalEnds;

    // ── Helpers ───────────────────────────────────────────────────────────────

    static bool IsKickoff(const GameState& s) {
        return s.ball.pos.Length() < 300.0f && s.ball.vel.Length() < 200.0f;
    }

    bool HasPossession(const Player& p, const GameState& s) const {
        float dist = (p.pos - s.ball.pos).Length();
        if (dist > _cfg.possessionDist) return false;
        if (p.ballTouchedStep)          return true;
        if (p.vel.Length() > 100.0f && s.ball.vel.Length() > 100.0f) {
            if (p.vel.Normalized().Dot(s.ball.vel.Normalized()) > _cfg.velAlignDot)
                return true;
        }
        return false;
    }

    bool InOwnHalf(const Player& p) const {
        return (p.team == Team::BLUE) ? (p.pos.y < 0.0f) : (p.pos.y > 0.0f);
    }

    // Lateral movement quality: how perpendicular is the player's velocity
    // to the line toward the nearest opponent? 1.0 = fully lateral, 0.0 = charging.
    static float LateralComponent(const Player& p, const GameState& s) {
        if (p.vel.Length() < 100.0f) return 0.0f;
        float bestDot = 1.0f;
        for (const auto& opp : s.players) {
            if (opp.team == p.team || opp.isDemoed) continue;
            Vec toOpp = (opp.pos - p.pos);
            float d = toOpp.Length();
            if (d < 1.0f) continue;
            float dot = std::fabsf(p.vel.Normalized().Dot(toOpp / d));
            if (dot < bestDot) bestDot = dot;
        }
        // dot=0 → fully lateral, dot=1 → fully toward opponent
        return 1.0f - bestDot;
    }

public:
    explicit ThirdManStallReward(ThirdManContext& ctx, Config cfg = Config{})
        : _ctx(ctx), _cfg(cfg) {}

    void Reset(const GameState& state) override {
        _states.clear();
        for (const auto& p : state.players)
            _states[p.carId] = StallState{};
    }

    // PreStep: update shared context (only this reward calls Update)
    void PreStep(const GameState& state) override {
        _dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
        _ctx.Update(state);  // ThirdManContext owned here; CounterLaunch reads it

        for (auto& [id, st] : _states)
            st.cooldownRemaining = RS_MAX(0.0f, st.cooldownRemaining - _dt);
    }

    float GetReward(const Player& player, const GameState& state,
                    bool /*isFinal*/) override {

        if (IsKickoff(state))    return 0.0f;
        if (player.isDemoed)     return 0.0f;

        // Third-man (FURTHEST) role only
        auto roleIt = _ctx.isThirdMan.find(player.carId);
        if (roleIt == _ctx.isThirdMan.end() || !roleIt->second) return 0.0f;

        int ti = (player.team == Team::BLUE) ? 0 : 1;

        // Mutual exclusion: counter-launch already fired this step
        if (_ctx.counterLaunchFiredThisStep[ti]) return 0.0f;

        auto& st = _states[player.carId];
        if (st.cooldownRemaining > 0.0f) return 0.0f;

        // ── Gate 1: own half ────────────────────────────────────────────────
        if (!InOwnHalf(player)) return 0.0f;

        // ── Gate 2: has possession ──────────────────────────────────────────
        if (!HasPossession(player, state)) {
            if (st.wasStalling) {
                // Possession lost — end stall window
                st.endedByPossLoss  = true;
                st.cooldownRemaining = _cfg.cooldownSec;
                st.wasStalling       = false;
                st.stallDuration     = 0.0f;
                s_possLossEnds.fetch_add(1);
                s_totalEnds.fetch_add(1);
            }
            return 0.0f;
        }

        // ── Gate 3: ball not moving too fast (it's a hold, not a clear) ─────
        if (state.ball.vel.Length() > _cfg.stallMaxBallSpeed) return 0.0f;

        // ── Gate 4: teammate still needs recovery time ──────────────────────
        if (!_ctx.teammatesRecovering[ti]) {
            // All teammates back — stall no longer needed, end cleanly
            if (st.wasStalling) {
                st.endedByRecovery   = true;
                st.cooldownRemaining = _cfg.cooldownSec;
                st.wasStalling       = false;
                st.stallDuration     = 0.0f;
                s_recoveryEnds.fetch_add(1);
                s_totalEnds.fetch_add(1);
            }
            return 0.0f;
        }

        // ── Gate 5: opponent is actually pressing ───────────────────────────
        float pressure = _ctx.opponentClosingOnBall[ti];
        if (pressure < _cfg.minPressureSpeed) return 0.0f;

        // ── All gates passed ─────────────────────────────────────────────────
        st.wasStalling    = true;
        st.stallDuration += _dt;

        // Pressure-scaled shaping
        float pressureBonus = RS_CLAMP(
            (pressure - _cfg.minPressureSpeed) * _cfg.pressureScale,
            0.0f,
            _cfg.shapingMax - _cfg.shapingBase);
        float shaping = _cfg.shapingBase + pressureBonus;

        // Lateral movement bonus
        float lateral   = LateralComponent(player, state);
        float latBonus  = (lateral > 1.0f - _cfg.lateralDotMax) ? _cfg.lateralBonus : 0.0f;

        float reward = RS_MIN(shaping + latBonus, _cfg.shapingMax);

        // Mark mutual exclusion flag
        _ctx.stallActiveThisStep[ti] = true;

        // Metrics
        s_framesActive.fetch_add(1);
        s_pressureMilli.fetch_add(static_cast<long long>(pressure * 1000.0f));
        s_lateralMilli.fetch_add(static_cast<long long>(lateral  * 1000.0f));

        if (_cfg.shadowMode) return 0.0f;
        return reward;
    }

    void CollectMetrics(std::unordered_map<std::string, float>& out) const {
        int frames = s_framesActive.load();
        int ends   = s_totalEnds.load();
        out["ThirdManStall/frames_active"]          = static_cast<float>(frames);
        out["ThirdManStall/avg_pressure"]           =
            frames > 0 ? static_cast<float>(s_pressureMilli.load()) / frames / 1000.0f : 0.0f;
        out["ThirdManStall/avg_lateral_component"]  =
            frames > 0 ? static_cast<float>(s_lateralMilli.load()) / frames / 1000.0f : 0.0f;
        out["ThirdManStall/teammate_recovery_rate"] =
            ends > 0 ? static_cast<float>(s_recoveryEnds.load()) / ends : 0.0f;
        out["ThirdManStall/possession_lost_rate"]   =
            ends > 0 ? static_cast<float>(s_possLossEnds.load()) / ends : 0.0f;

        s_framesActive = 0; s_pressureMilli = 0; s_lateralMilli = 0;
        s_recoveryEnds = 0; s_possLossEnds  = 0; s_totalEnds    = 0;
    }
};

inline std::atomic<int>       ThirdManStallReward::s_framesActive{0};
inline std::atomic<long long> ThirdManStallReward::s_pressureMilli{0};
inline std::atomic<long long> ThirdManStallReward::s_lateralMilli{0};
inline std::atomic<int>       ThirdManStallReward::s_recoveryEnds{0};
inline std::atomic<int>       ThirdManStallReward::s_possLossEnds{0};
inline std::atomic<int>       ThirdManStallReward::s_totalEnds{0};

} // namespace RLGC
