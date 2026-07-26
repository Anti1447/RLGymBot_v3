#pragma once
// ============================================================================
// PredictiveBounceReadReward.h
// ----------------------------------------------------------------------------
// Rewards the bot for PREDICTING the post-bounce ball path and committing
// to the correct intercept position BEFORE the ball contacts the surface.
//
// DISTINCTION FROM PreBounceInterceptReward
//   PreBounceInterceptReward  — touch occurs BEFORE the bounce (early interrupt)
//   PredictiveBounceReadReward — touch occurs AFTER the bounce, but movement to
//                                 the intercept point begins BEFORE the bounce
//
// These are complementary skills. Both are valid strategies depending on field
// position, boost, and timing. Neither is always correct — this reward does NOT
// penalize pre-bounce interceptions; it simply doesn't fire on them.
//
// THE CORE IDEA
//   A bot that waits to react after the bounce always arrives late: it sees the
//   new direction, starts moving, and gets to the ball at least 0.3-0.5s after
//   an opponent who read the angle early. The pre-read skill — moving to where
//   the ball WILL be before it gets there — is the difference between a fast
//   first-touch and a desperate lunge. This reward teaches that pre-commitment.
//
// MECHANISM: THREE-PHASE STATE MACHINE (per player)
//
//   IDLE ──► PRE_BOUNCE: tNextBounce enters (minPreWindow, maxPreWindow]
//     At entry: lock in predictedReboundPos from BallPred
//     Each step:  accumulate peakAlignment = max velocity alignment toward
//                 predictedReboundPos. This is the pre-read signal.
//
//   PRE_BOUNCE ──► POST_BOUNCE: ball proximity to surface (reactive detection)
//     At entry: snapshot peakAlignment (how well bot read it pre-bounce)
//     Timeout:  MAX_POST_BOUNCE_WINDOW seconds
//     If player touches ball during PRE_BOUNCE → revert to IDLE silently
//       (PreBounceInterceptReward handles that case)
//
//   POST_BOUNCE ──► IDLE: ballTouchedStep fires → issue reward
//     Reward = baseReward × preReadMult(peakAlignment) × speedFactor × surfaceMult
//     preReadMult: 1.0 at perfect pre-read (alignment=1.0), 0.15 at pure reaction
//       — so reactive touches get a small reward; the bonus is for pre-reading
//
// WHY PEAK ALIGNMENT (NOT MEAN)
//   One good read before the bounce matters more than average. A bot that
//   briefly corrected course toward the rebound point (peak > 0.7) and then
//   accelerated is doing the right thing. Mean would penalize time spent
//   turning, suppressing the behavior we're trying to teach.
//
// PREDICTED REBOUND POSITION
//   Locked in at PRE_BOUNCE entry using BallPred::Cache.AtTime(tBounce + 0.4s).
//   Locked rather than updated each step because we want to test the quality of
//   the read at the time the bot committed, not the currently-refined prediction.
//   The 0.4s offset samples where the ball will realistically be when a bot
//   arriving at full speed would contact it after the bounce.
//
// GROUND BOUNCES EXCLUDED
//   Only WALL, CEILING, and BACKBOARD surfaces are tracked. Ground bounces occur
//   every 2-3 seconds in normal play and require no predictive positioning skill.
//   Including ground would cause Count to explode and dilute the W&B signal.
//
// OVERLAP GUARDS
//   - Does NOT fire if player touched ball during PRE_BOUNCE phase
//     → prevents overlap with PreBounceInterceptReward
//   - Cooldown 1.0s prevents multi-reward on the same bounce event
//
// W&B METRICS (GetAndResetMetrics — static, aggregates across all parallel envs)
//   PredictiveBounce/Count              — total post-bounce reads rewarded
//   PredictiveBounce/AvgPreReadAlignPct — mean peakAlignment × 100 (0-100)
//   PredictiveBounce/FullPreReadRate    — fraction with alignment > 0.7 (genuine read)
//   PredictiveBounce/ReactiveOnlyRate   — fraction with alignment < 0.15 (pure reaction)
//   PredictiveBounce/AvgRewardIssued    — diagnostic: mean reward per event
//   PredictiveBounce/Surface/Wall|Ceiling|Backboard — per-surface breakdown
//
//   KEY TRAINING SIGNAL: FullPreReadRate rising over 500M steps means the policy
//   is developing genuine ball prediction. ReactiveOnlyRate should fall in parallel.
//   If both stay flat, the base reward may be too small to create gradient.
//
// INTEGRATION
//   Phase 5 (Diamond):  weight 15.0f, not ZeroSummed
//   Phase 6 (Champion): weight 20.0f, not ZeroSummed
//   NOT ZeroSummed — individual spatial prediction skill, not competitive signal.
//   Target FullPreReadRate > 0.30 within 500M steps.
//   If FullPreReadRate stays below 0.15, raise baseReward toward 0.5f.
//   If Count explodes (>10/game), lower maxPreWindow or raise minBallSpeed.
//
// Rev 1.0 — NJH + Claude | June 2026
// Rev 1.1 — Bug fixes:
//   - _stepDt removed; now uses state.deltaTime per codebase standard (was 1/30
//     hardcoded, training uses 8-tick skip = 1/15 → all timers ran 2x fast)
//   - Ground excluded from FindNextBounce and DetectSurfaceProximity (ground
//     bounces every 2-3s would make Count explode with zero signal value)
//   - PRE_BOUNCE guard raised 0.05f -> 0.10f (at dt=0.067s the guard was
//     satisfied at step 1 before actual bounce; now holds through first step)
//   - Metrics converted to static atomics (non-static was only sampling 1/310
//     of episodes; matches WallCeilingRedirectReward aggregation pattern)
// ============================================================================

#include "Reward.h"
#include "../Math.h"
#include "BallPrediction.h"
#include "../CommonValues.h"
#include <atomic>
#include <unordered_map>
#include <cmath>
#include <algorithm>

namespace RLGC {

class PredictiveBounceReadReward : public Reward {
public:

    // Ground excluded — see header comment for rationale
    enum class SurfaceType : uint8_t { NONE, WALL, CEILING, BACKBOARD };

    // -------------------------------------------------------------------------
    // Config
    // -------------------------------------------------------------------------
    struct Config {
        // PRE_BOUNCE entry: how far ahead of a predicted bounce to start tracking
        float maxPreWindow      = 1.20f;  // (s) — window that activates PRE_BOUNCE
        float minPreWindow      = 0.10f;  // (s) — ignore if bounce is imminent (can't pre-read)

        // POST_BOUNCE window: how long after bounce the touch is still credited
        float maxPostBounceWindow = 1.20f; // (s) — touch must arrive within this

        // Rebound sample offset: how far after predicted bounce to sample intercept pos
        float reboundSampleOffset = 0.40f; // (s) — matches UnifiedDoubleTapReward convention

        // Minimum pre-read alignment to get any post-bounce reward at all
        // (below this, the bot was moving in a completely wrong direction)
        float minAlignmentGate  = -0.50f; // (-1 to 1); -0.5 allows partial pre-reads

        // Minimum ball speed to consider it a meaningful bounce
        float minBallSpeedUUs   = 350.0f;

        // Reward magnitude
        float baseReward        = 0.30f;  // at perfect pre-read + max ball speed
        float minPreReadMult    = 0.15f;  // multiplier for pure reactive touches (alignment=-1)
        float maxPreReadMult    = 1.00f;  // multiplier for perfect pre-read (alignment=+1)

        // Surface multipliers (harder to predict = more reward)
        float multWall          = 1.25f;
        float multCeiling       = 1.50f;
        float multBackboard     = 1.35f;

        // Cooldown after a rewarded event
        float cooldownSec       = 1.00f;

        // Reactive bounce detection threshold (UU gap between ball surface and arena surface)
        float surfaceProximity  = 130.0f;

        bool shadowMode         = false;
    };

    // -------------------------------------------------------------------------
    // W&B Metrics — plain struct returned from static GetAndResetMetrics()
    // -------------------------------------------------------------------------
    struct Metrics {
        uint32_t count               = 0;
        double   sumPreReadAlign     = 0.0; // sum of peakAlignment at touch events
        uint32_t fullPreReadCount    = 0;   // alignment > 0.7
        uint32_t reactiveOnlyCount   = 0;   // alignment < 0.15
        double   sumRewardIssued     = 0.0;
        uint32_t surfWall            = 0;
        uint32_t surfCeiling         = 0;
        uint32_t surfBackboard       = 0;
    };

    explicit PredictiveBounceReadReward(const Config& cfg = Config{}) : _cfg(cfg) {}

    // -------------------------------------------------------------------------
    // PreStep
    // -------------------------------------------------------------------------
    virtual void PreStep(const GameState& state) override {
        _ballCache.Update(state);
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

        // Shared: find next non-ground bounce for this step (same for all players)
        _stepBounce = FindNextBounce();

        // Per-player state updates
        for (const auto& player : state.players) {
            if (player.isDemoed) continue;

            auto& ps = _playerState[player.carId];
            if (ps.cooldown > 0.0f) ps.cooldown -= dt;

            switch (ps.phase) {

            case Phase::IDLE: {
                // Entry condition: wall/ceiling/backboard bounce predicted within window
                if (_stepBounce.surface != SurfaceType::NONE &&
                    _stepBounce.tSeconds <= _cfg.maxPreWindow &&
                    _stepBounce.tSeconds >  _cfg.minPreWindow &&
                    state.ball.vel.Length() >= _cfg.minBallSpeedUUs)
                {
                    ps.phase            = Phase::PRE_BOUNCE;
                    ps.phaseTimer       = 0.0f;
                    ps.peakAlignment    = -1.0f;
                    ps.surface          = _stepBounce.surface;
                    ps.touchedPreBounce = false;

                    // Lock in the predicted rebound position NOW
                    // (test the quality of the current read, not a future refined one)
                    if (_ballCache.valid) {
                        float t = _stepBounce.tSeconds + _cfg.reboundSampleOffset;
                        ps.predictedReboundPos = _ballCache.AtTime(t).pos;
                        ps.reboundKnown = true;
                    } else {
                        ps.reboundKnown = false;
                    }
                }
                break;
            }

            case Phase::PRE_BOUNCE: {
                ps.phaseTimer += dt;

                // Timeout: bounce didn't happen within expected window
                if (ps.phaseTimer > _cfg.maxPreWindow + 0.30f) {
                    ps.phase = Phase::IDLE;
                    break;
                }

                // Track if the player touched the ball during pre-bounce phase.
                // If so, this is PreBounceInterceptReward territory.
                if (player.ballTouchedStep) {
                    ps.touchedPreBounce = true;
                }

                // Accumulate peak alignment toward predicted rebound position
                if (ps.reboundKnown) {
                    float speed = player.vel.Length();
                    if (speed > 120.0f) {
                        Vec toRebound = (ps.predictedReboundPos - player.pos).Normalized();
                        float align   = player.vel.Normalized().Dot(toRebound);
                        ps.peakAlignment = std::max(ps.peakAlignment, align);
                    }
                }

                // Reactive bounce detection: has the ball reached the surface?
                // Guard at 0.10f: at 8-tick skip (dt=0.067s), phaseTimer=0.067 on step 1
                // which is below the guard. Ball hasn't bounced yet (minPreWindow=0.10s
                // guarantees the bounce is at least 0.10s away at PRE_BOUNCE entry).
                // At step 2 phaseTimer=0.134 > 0.10; bounce has now occurred.
                if (ps.phaseTimer > 0.10f && DetectSurfaceProximity(state.ball.pos)) {
                    if (ps.touchedPreBounce) {
                        // Player pre-empted the bounce → idle, PreBounceInterceptReward covers it
                        ps.phase = Phase::IDLE;
                    } else {
                        ps.phase      = Phase::POST_BOUNCE;
                        ps.phaseTimer = 0.0f;
                    }
                }
                break;
            }

            case Phase::POST_BOUNCE: {
                ps.phaseTimer += dt;
                if (ps.phaseTimer > _cfg.maxPostBounceWindow) {
                    // No touch arrived in time — missed the read opportunity
                    ps.phase = Phase::IDLE;
                }
                // Touch handling in GetReward
                break;
            }

            } // switch
        }
    }

    // -------------------------------------------------------------------------
    // Reset
    // -------------------------------------------------------------------------
    virtual void Reset(const GameState& initialState) override {
        _playerState.clear();
    }

    // -------------------------------------------------------------------------
    // GetReward
    // -------------------------------------------------------------------------
    virtual float GetReward(const Player& player,
                            const GameState& state,
                            bool isFinal) override
    {
        if (player.isDemoed) return 0.0f;

        auto& ps = _playerState[player.carId];

        // Only fire in POST_BOUNCE phase on a ball touch
        if (ps.phase != Phase::POST_BOUNCE) return 0.0f;
        if (!player.ballTouchedStep)         return 0.0f;
        if (ps.cooldown > 0.0f)              return 0.0f;

        // Gate: minimum alignment (bot wasn't moving completely the wrong way)
        if (ps.peakAlignment < _cfg.minAlignmentGate) {
            ps.phase = Phase::IDLE;
            return 0.0f;
        }

        // Ball speed factor: 0 at minBallSpeed, 1 at 2000 UU/s
        const float ballSpeed   = state.ball.vel.Length();
        const float speedFactor = RS_CLAMP(
            (ballSpeed - _cfg.minBallSpeedUUs) / (2000.0f - _cfg.minBallSpeedUUs),
            0.0f, 1.0f);

        // Pre-read multiplier: linear from minPreReadMult (alignment=-1) to
        // maxPreReadMult (alignment=+1)
        const float alignNorm   = RS_CLAMP((ps.peakAlignment + 1.0f) / 2.0f, 0.0f, 1.0f);
        const float preReadMult = _cfg.minPreReadMult
                                + (_cfg.maxPreReadMult - _cfg.minPreReadMult) * alignNorm;

        // Surface multiplier
        const float surfMult = SurfaceMultiplier(ps.surface);

        // Final reward
        const float reward = _cfg.baseReward * preReadMult * speedFactor * surfMult;

        // Metrics (static atomics — aggregated across all 310 parallel envs)
        s_count.fetch_add(1, std::memory_order_relaxed);
        s_sumAlignMilli.fetch_add((long long)(ps.peakAlignment * 1000.0f), std::memory_order_relaxed);
        s_rewardMilli.fetch_add((long long)(reward * 1000.0f), std::memory_order_relaxed);
        if (ps.peakAlignment > 0.70f) s_fullPreReadCount.fetch_add(1, std::memory_order_relaxed);
        if (ps.peakAlignment < 0.15f) s_reactiveOnlyCount.fetch_add(1, std::memory_order_relaxed);

        switch (ps.surface) {
            case SurfaceType::WALL:      s_surfWall.fetch_add(1, std::memory_order_relaxed);      break;
            case SurfaceType::CEILING:   s_surfCeiling.fetch_add(1, std::memory_order_relaxed);   break;
            case SurfaceType::BACKBOARD: s_surfBackboard.fetch_add(1, std::memory_order_relaxed); break;
            default: break;
        }

        // Reset state
        ps.phase    = Phase::IDLE;
        ps.cooldown = _cfg.cooldownSec;

        return _cfg.shadowMode ? 0.0f : reward;
    }

    // -------------------------------------------------------------------------
    // Metrics — static: call on any instance to get aggregate across all envs
    // -------------------------------------------------------------------------
    static Metrics GetAndResetMetrics() {
        Metrics m;
        m.count             = (uint32_t)s_count.exchange(0);
        m.sumPreReadAlign   = double(s_sumAlignMilli.exchange(0))  / 1000.0;
        m.fullPreReadCount  = (uint32_t)s_fullPreReadCount.exchange(0);
        m.reactiveOnlyCount = (uint32_t)s_reactiveOnlyCount.exchange(0);
        m.sumRewardIssued   = double(s_rewardMilli.exchange(0)) / 1000.0;
        m.surfWall          = (uint32_t)s_surfWall.exchange(0);
        m.surfCeiling       = (uint32_t)s_surfCeiling.exchange(0);
        m.surfBackboard     = (uint32_t)s_surfBackboard.exchange(0);
        return m;
    }

private:

    // -------------------------------------------------------------------------
    // State machine
    // -------------------------------------------------------------------------
    enum class Phase : uint8_t { IDLE, PRE_BOUNCE, POST_BOUNCE };

    struct PlayerState {
        Phase       phase               = Phase::IDLE;
        float       phaseTimer          = 0.0f;
        float       cooldown            = 0.0f;
        float       peakAlignment       = -1.0f;    // max velocity->rebound alignment seen
        Vec         predictedReboundPos = {};
        bool        reboundKnown        = false;
        bool        touchedPreBounce    = false;    // bot touched ball before bounce
        SurfaceType surface             = SurfaceType::NONE;
    };

    // -------------------------------------------------------------------------
    // Bounce info — computed once in PreStep, reused across all players
    // -------------------------------------------------------------------------
    struct BounceInfo {
        SurfaceType surface  = SurfaceType::NONE;
        float       tSeconds = 0.0f;
    };

    // -------------------------------------------------------------------------
    // FindNextBounce — first rising edge in BallPred slice array for
    // wall/ceiling/backboard. Ground intentionally excluded.
    // -------------------------------------------------------------------------
    BounceInfo FindNextBounce() const {
        if (!_ballCache.valid) return {};

        bool prevW = false, prevC = false, prevB = false;

        for (int i = 0; i < BallPred::NUM_SLICES; i++) {
            const auto& sl = _ballCache.slices[i];
            if (!prevW && sl.hitWall)      return { SurfaceType::WALL,      sl.t };
            if (!prevC && sl.hitCeiling)   return { SurfaceType::CEILING,   sl.t };
            if (!prevB && sl.hitBackboard) return { SurfaceType::BACKBOARD, sl.t };
            prevW = sl.hitWall;
            prevC = sl.hitCeiling;
            prevB = sl.hitBackboard;
        }
        return {};
    }

    // -------------------------------------------------------------------------
    // DetectSurfaceProximity — reactive check: is the ball near a non-ground
    // surface? Ground excluded to prevent spurious POST_BOUNCE transitions
    // during low passes and dribbles.
    // -------------------------------------------------------------------------
    bool DetectSurfaceProximity(const Vec& p) const {
        const float r   = CommonValues::BALL_RADIUS + _cfg.surfaceProximity;
        const float swx = CommonValues::SIDE_WALL_X - r;
        const float bwy = CommonValues::BACK_WALL_Y - r;
        const float cz  = CommonValues::CEILING_Z   - r;

        return std::abs(p.x) > swx ||   // side wall
               p.z           > cz  ||   // ceiling
               std::abs(p.y) > bwy;     // backboard
    }

    float SurfaceMultiplier(SurfaceType s) const {
        switch (s) {
            case SurfaceType::WALL:      return _cfg.multWall;
            case SurfaceType::CEILING:   return _cfg.multCeiling;
            case SurfaceType::BACKBOARD: return _cfg.multBackboard;
            default:                     return 1.0f;
        }
    }

    // -------------------------------------------------------------------------
    // Members
    // -------------------------------------------------------------------------
    Config                                    _cfg;
    BallPred::Cache                           _ballCache;
    BounceInfo                                _stepBounce;
    std::unordered_map<uint32_t, PlayerState> _playerState;

    // Thread-safe metrics — static so all 310 parallel environments contribute
    // to the same counters. GetAndResetMetrics() can be called on any instance.
    inline static std::atomic<int>       s_count             { 0 };
    inline static std::atomic<long long> s_sumAlignMilli     { 0 };  // peakAlignment x 1000
    inline static std::atomic<int>       s_fullPreReadCount  { 0 };
    inline static std::atomic<int>       s_reactiveOnlyCount { 0 };
    inline static std::atomic<long long> s_rewardMilli       { 0 };  // reward x 1000
    inline static std::atomic<int>       s_surfWall          { 0 };
    inline static std::atomic<int>       s_surfCeiling       { 0 };
    inline static std::atomic<int>       s_surfBackboard     { 0 };
};

} // namespace RLGC
