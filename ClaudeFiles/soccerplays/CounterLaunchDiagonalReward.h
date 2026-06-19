// ============================================================================
// CounterLaunchDiagonalReward.h
// Rev 1.0 (2026-06-12, NJH)
//
// CONCEPT: Third-Man Counter-Launch / Diagonal Switch
//
// SOCCER ANALOGY:
//   When the third man senses both forward teammates are upfield and ahead of
//   the opponent's defensive line, their job is to launch the ball to the open
//   side of the field immediately — bypassing the central press before defenders
//   recover. The diagonal switch is played into the runner's path, not to their
//   feet: the ball lands where the runner will be, not where they are now.
//   The window is 2–4 seconds; hesitation collapses the space.
//
// TRIGGER CONTEXT (all must be true simultaneously):
//   1. FURTHEST role (third man) via ThirdManContext
//   2. Player in own half at time of touch
//   3. ballTouchedStep this frame (event-based, not continuous)
//   4. All living teammates in opponent half AND moving forward
//      — ThirdManContext.teammatesUpfield == true
//   5. Ball velocity after touch >= LAUNCH_MIN_SPEED UU/s
//      (must be a meaningful hit, not a tap)
//   6. BallPred landing zone (1.0–1.5s) has openFieldScore >= OPEN_FIELD_MIN
//      (the ball is going somewhere the opponent isn't)
//   7. ThirdManContext.stallActiveThisStep == false
//      (stall and launch are mutually exclusive by design)
//
// REWARD STRUCTURE:
//   Single one-shot event reward on the qualifying touch frame.
//   Base: LAUNCH_BASE_REWARD
//   Bonus: openFieldScore bonus (0 → OPEN_FIELD_BONUS at score=1.0)
//   Bonus: lateral X-component bonus — ball going to the side, not straight
//          up the middle (bypasses the press more effectively)
//   Bonus: ball speed bonus — harder launch into more space
//
//   Total max: LAUNCH_BASE_REWARD + OPEN_FIELD_BONUS + LATERAL_BONUS + SPEED_BONUS
//
// MUTUAL EXCLUSION vs BigClearReward:
//   BigClearReward fires on ballTouchedStep + own half + ballVel >= 300 + away
//   from goal. CounterLaunchDiagonal fires on a superset of those conditions
//   with the additional teammate-upfield + open-field gates. When CounterLaunch
//   fires, it sets ThirdManContext.counterLaunchFiredThisStep[team] = true.
//
//   TO PREVENT DOUBLE-COUNTING in BigClearReward, add this at the top of
//   BigClearReward::GetReward():
//
//     extern ThirdManContext* g_thirdManCtx;
//     int ti = (player.team == Team::BLUE) ? 0 : 1;
//     if (g_thirdManCtx && g_thirdManCtx->counterLaunchFiredThisStep[ti])
//         return 0.0f;
//
//   This suppresses the BigClear payout when CounterLaunch already paid.
//   Without this, a perfect counter-launch pays both rewards simultaneously.
//
// MUTUAL EXCLUSION vs ForwardPassReward / PassingSetupReward:
//   A diagonal counter-launch landing near a teammate can satisfy pass rewards.
//   These will co-fire. Acceptable: the pass reward confirms the social utility
//   of the launch (teammate gets it). The launch reward confirms the tactical
//   context (upfield runners, open field). They measure different things.
//   Keep weights small enough that co-firing doesn't dominate gradient.
//
// MUTUAL EXCLUSION vs EnhancedVelocityBallToGoalReward:
//   Any ball hit toward opponent goal satisfies this. Will co-fire with a
//   goal-directed counter-launch. Acceptable at low weight — EnhancedVBTG is
//   a baseline shaping signal, CounterLaunch is a contextual bonus on top.
//
// OPEN FIELD SCORE COMPUTATION:
//   Uses BallPred::Cache at [predNearSec, predFarSec] centroid.
//   Opponent density: for each opponent, compute a Gaussian proximity score
//   centered on the zone centroid. openFieldScore = 1.0 - clamp(sum, 0, 1).
//   This gives 1.0 when no opponents are near the predicted landing zone,
//   and decays toward 0 as opponents converge on it.
//
// W&B METRICS:
//   CounterLaunch/launches_fired        — qualifying touch events paid
//   CounterLaunch/avg_open_field_score  — mean openFieldScore at launch
//   CounterLaunch/avg_ball_speed        — mean ball speed at launch
//   CounterLaunch/lateral_launch_rate   — fraction with significant X component
//   CounterLaunch/teammate_upfield_gate_failures — frames where touch was good
//                                                  but teammate gate blocked it
//
// PHASE:     5+ (needs FURTHEST role + BallPred, 2v2+)
// ZERO SUM:  Yes — counter-launch is directly competitive
// SHADOW:    true by default
//
// SUGGESTED WEIGHTS:
//   Shadow Phase 5:  0.0f (shadowMode=true)
//   Live Phase 5:    6.0f
//   Live Phase 6:    10.0f
// ============================================================================

#pragma once
#include "Reward.h"
#include "BallPrediction.h"
#include "ThirdManContext.h"
#include "../CommonValues.h"
#include <unordered_map>
#include <atomic>
#include <cmath>

namespace RLGC {

class CounterLaunchDiagonalReward : public Reward {
public:
    struct Config {
        // Predicted landing zone window (mirrors SecondBallAnticipationReward)
        float predNearSec       = 1.0f;
        float predFarSec        = 1.5f;

        // Open field scoring
        float openFieldMin      = 0.35f;   // minimum score to qualify for launch
        float openFieldGaussian = 1500.0f; // UU — Gaussian spread for opponent density
        float openFieldBonus    = 0.3f;    // max bonus at openFieldScore=1.0

        // Launch speed gate
        float launchMinSpeed    = 1000.0f; // UU/s — must be a real hit, not a tap
        float speedBonusMax     = 0.2f;    // max bonus at CAR_MAX_SPEED ball vel

        // Lateral bonus — rewards X-component (diagonal, not straight up middle)
        float lateralMinFrac    = 0.25f;   // |vel.x| / |vel| must exceed this
        float lateralBonus      = 0.25f;   // bonus when lateral threshold met

        // Base event reward
        float launchBase        = 0.5f;

        // Per-player cooldown after a launch (prevents chaining taps)
        float cooldownSec       = 3.0f;

        // Teammate gate failure tracking window (for metrics only)
        float gateFailWindowSec = 2.0f;

        bool shadowMode         = true;
    };

private:
    ThirdManContext& _ctx;
    Config           _cfg;
    BallPred::Cache  _ballCache;

    struct LaunchState {
        float cooldownRemaining       = 0.0f;
        float gateFailTimer           = 0.0f;  // time since last gate-blocked touch
    };
    std::unordered_map<uint32_t, LaunchState> _states;
    float _dt = 1.0f / 120.0f;

    // Cached open field score, computed in PreStep
    float _openFieldScore = 0.0f;
    Vec   _zoneCentroid   = {};
    bool  _zoneValid      = false;

    // Metrics
    static std::atomic<int>       s_launchesFired;
    static std::atomic<long long> s_openFieldMilli;
    static std::atomic<long long> s_ballSpeedMilli;
    static std::atomic<int>       s_lateralLaunches;
    static std::atomic<int>       s_gateFailures;

    // ── Helpers ───────────────────────────────────────────────────────────────

    static bool IsKickoff(const GameState& s) {
        return s.ball.pos.Length() < 300.0f && s.ball.vel.Length() < 200.0f;
    }

    bool InOwnHalf(const Player& p) const {
        return (p.team == Team::BLUE) ? (p.pos.y < 0.0f) : (p.pos.y > 0.0f);
    }

    // Compute zone centroid from BallPred slices in [nearSec, farSec]
    Vec ComputeZoneCentroid() const {
        if (!_ballCache.valid) return {};
        Vec sum = {}; int count = 0;
        for (int i = 0; i < BallPred::NUM_SLICES; ++i) {
            const auto& sl = _ballCache.slices[i];
            if (sl.t < _cfg.predNearSec) continue;
            if (sl.t > _cfg.predFarSec)  break;
            sum.x += sl.pos.x; sum.y += sl.pos.y; sum.z += sl.pos.z;
            ++count;
        }
        if (count == 0) return {};
        return Vec(sum.x / count, sum.y / count, sum.z / count);
    }

    // Open field score: 1.0 = no opponents near landing zone, 0.0 = fully covered.
    // Uses Gaussian proximity density centered on zone centroid.
    float ComputeOpenFieldScore(const GameState& state, const Vec& centroid,
                                Team launchTeam) const {
        float densitySum = 0.0f;
        for (const auto& p : state.players) {
            if (p.team == launchTeam || p.isDemoed) continue;
            float dist = (p.pos - centroid).Length();
            // Gaussian: e^(-dist^2 / (2*sigma^2))
            float sigma = _cfg.openFieldGaussian;
            float g = std::expf(-(dist * dist) / (2.0f * sigma * sigma));
            densitySum += g;
        }
        return 1.0f - RS_CLAMP(densitySum, 0.0f, 1.0f);
    }

public:
    explicit CounterLaunchDiagonalReward(ThirdManContext& ctx, Config cfg = Config{})
        : _ctx(ctx), _cfg(cfg) {}

    void Reset(const GameState& state) override {
        _states.clear();
        _zoneValid = false;
        for (const auto& p : state.players)
            _states[p.carId] = LaunchState{};
    }

    // PreStep: ThirdManContext already updated by ThirdManStallReward.
    // Only update BallPred cache and open field score here.
    void PreStep(const GameState& state) override {
        _dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

        _ballCache.Update(state);
        if (_ballCache.valid) {
            _zoneCentroid   = ComputeZoneCentroid();
            _zoneValid      = true;
        } else {
            _zoneValid = false;
        }

        for (auto& [id, st] : _states) {
            st.cooldownRemaining = RS_MAX(0.0f, st.cooldownRemaining - _dt);
            if (st.gateFailTimer > 0.0f)
                st.gateFailTimer = RS_MAX(0.0f, st.gateFailTimer - _dt);
        }
    }

    float GetReward(const Player& player, const GameState& state,
                    bool /*isFinal*/) override {

        if (IsKickoff(state))  return 0.0f;
        if (player.isDemoed)   return 0.0f;
        if (!_zoneValid)       return 0.0f;

        // Must be a touch frame — event-based only
        if (!player.ballTouchedStep) return 0.0f;

        // Third-man role gate
        auto roleIt = _ctx.isThirdMan.find(player.carId);
        if (roleIt == _ctx.isThirdMan.end() || !roleIt->second) return 0.0f;

        int ti = (player.team == Team::BLUE) ? 0 : 1;

        // Stall mutual exclusion
        if (_ctx.stallActiveThisStep[ti]) return 0.0f;

        auto& st = _states[player.carId];
        if (st.cooldownRemaining > 0.0f) return 0.0f;

        // ── Gate: own half ──────────────────────────────────────────────────
        if (!InOwnHalf(player)) return 0.0f;

        // ── Gate: teammates fully upfield ───────────────────────────────────
        if (!_ctx.teammatesUpfield[ti]) {
            // Touch was good but teammate gate blocked — track for metrics
            st.gateFailTimer = _cfg.gateFailWindowSec;
            s_gateFailures.fetch_add(1);
            return 0.0f;
        }

        // ── Gate: ball speed after touch ────────────────────────────────────
        float ballSpeed = state.ball.vel.Length();
        if (ballSpeed < _cfg.launchMinSpeed) return 0.0f;

        // ── Gate: open field score ───────────────────────────────────────────
        float openScore = ComputeOpenFieldScore(state, _zoneCentroid, player.team);
        _ctx.openFieldScore = openScore; // share with context for diagnostics
        if (openScore < _cfg.openFieldMin) return 0.0f;

        // ── All gates passed — compute reward ────────────────────────────────

        // Open field bonus
        float openBonus = openScore * _cfg.openFieldBonus;

        // Lateral X-component bonus (diagonal switch, not straight up the middle)
        float velMag = state.ball.vel.Length();
        float lateralFrac = (velMag > 1.0f)
            ? std::fabsf(state.ball.vel.x) / velMag
            : 0.0f;
        bool  isLateral  = (lateralFrac > _cfg.lateralMinFrac);
        float latBonus   = isLateral ? _cfg.lateralBonus : 0.0f;

        // Speed bonus
        float speedFrac  = RS_CLAMP(ballSpeed / CommonValues::CAR_MAX_SPEED, 0.0f, 1.0f);
        float speedBonus = speedFrac * _cfg.speedBonusMax;

        float reward = _cfg.launchBase + openBonus + latBonus + speedBonus;

        // Set mutual exclusion flag so BigClearReward doesn't double-pay
        _ctx.counterLaunchFiredThisStep[ti] = true;

        // Start cooldown
        st.cooldownRemaining = _cfg.cooldownSec;

        // Metrics
        s_launchesFired.fetch_add(1);
        s_openFieldMilli.fetch_add(static_cast<long long>(openScore * 1000.0f));
        s_ballSpeedMilli.fetch_add(static_cast<long long>(ballSpeed * 1000.0f));
        if (isLateral) s_lateralLaunches.fetch_add(1);

        if (_cfg.shadowMode) return 0.0f;
        return reward;
    }

    void CollectMetrics(std::unordered_map<std::string, float>& out) const {
        int fired = s_launchesFired.load();
        out["CounterLaunch/launches_fired"]              = static_cast<float>(fired);
        out["CounterLaunch/avg_open_field_score"]        =
            fired > 0 ? static_cast<float>(s_openFieldMilli.load()) / fired / 1000.0f : 0.0f;
        out["CounterLaunch/avg_ball_speed"]              =
            fired > 0 ? static_cast<float>(s_ballSpeedMilli.load()) / fired / 1000.0f : 0.0f;
        out["CounterLaunch/lateral_launch_rate"]         =
            fired > 0 ? static_cast<float>(s_lateralLaunches.load()) / fired : 0.0f;
        out["CounterLaunch/teammate_upfield_gate_failures"] =
            static_cast<float>(s_gateFailures.load());

        s_launchesFired = 0; s_openFieldMilli = 0; s_ballSpeedMilli = 0;
        s_lateralLaunches = 0; s_gateFailures = 0;
    }
};

inline std::atomic<int>       CounterLaunchDiagonalReward::s_launchesFired{0};
inline std::atomic<long long> CounterLaunchDiagonalReward::s_openFieldMilli{0};
inline std::atomic<long long> CounterLaunchDiagonalReward::s_ballSpeedMilli{0};
inline std::atomic<int>       CounterLaunchDiagonalReward::s_lateralLaunches{0};
inline std::atomic<int>       CounterLaunchDiagonalReward::s_gateFailures{0};

} // namespace RLGC
