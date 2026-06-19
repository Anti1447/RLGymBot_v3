#pragma once
// ============================================================================
// WallCeilingRedirectReward.h — Rev 1.0 (2026-06-12, NJH)
//
// Rewards reading a wall/ceiling/corner bounce and converting the rebound
// with an AERIAL touch — either a redirect shot on goal or a controlled
// aerial continuation. Complements (does not duplicate) existing rewards:
//
//   - BackboardReboundReward   → owns backboard rebounds (this reward
//                                 EXCLUDES backboard by default)
//   - UnifiedDoubleTapReward   → owns own-setup → surface → finish chains
//                                 (self-setup here pays selfSetupMultiplier,
//                                  default 0.5x, configurable to 0)
//   - CeilingShot*Reward       → owns self-carried ceiling plays
//
// Physics grounding (ball: r=92.75uu in CommonValues, restitution 0.6):
//   - The bounce strips 40% of the velocity component normal to the surface.
//     BallPred::Cache models this exactly (BOUNCE_COEF = 0.6, incl. 45°
//     corner planes), so predicted rebound positions are physically correct.
//   - MIN_NORMAL_SPEED gate: a bounce only opens a sequence if the ball
//     carried ≥400 uu/s INTO the surface — glancing contacts barely deflect
//     and aren't a redirect opportunity.
//   - Power restoration bonus: because the bounce sapped energy, the redirect
//     touch is rewarded for the goalward speed it ADDS (Δ up to 1200 uu/s
//     → up to +powerBonusMax).
//
// State machine (global bounce tracking in PreStep, per-player payout):
//   1. PREDICT:   BallPred shows a wall/ceiling/corner hit within predHorizon,
//                 ball airborne until then, rebound stays airborne → per-player
//                 anticipation shaping toward predicted rebound point (capped
//                 per sequence per player).
//   2. BOUNCE:    actual contact confirmed (proximity + normal-velocity flip
//                 + min normal speed) → intercept window opens.
//   3. INTERCEPT: first ballTouchedStep consumes the window. Aerial touch →
//                 payout (shot redirect OR aerial continuation). Grounded
//                 touch, floor contact, or timeout → window closes, no pay.
//
// Timer correctness: _currentTime advances in PreStep() once per step
// (UnifiedDoubleTapReward fix — never advance time in GetReward).
//
// Anti-farming:
//   - One payout per bounce sequence (consumed by first toucher).
//   - Aerial gate on the payout touch (isOnGround == false, z ≥ minAerialZ).
//   - Shaping capped per sequence per player (shapingCapPerSeq).
//   - Multiplicative shot payout (align × speed) — no single-dimension farm.
//   - Own-goal-direction redirects pay 0.
//
// W&B metrics (CollectMetrics → GetAndResetMetrics):
//   PredSequences, BouncesDetected, AerialIntercepts, GroundedIntercepts,
//   ShotRedirects, Continuations, OwnGoalRedirects, AvgShotAlign,
//   AvgPowerRestore, AvgEnergyRetained (diagnostic — should sit near 0.6–1.0
//   depending on incidence angle), RewardSum, ShapingSum.
// ============================================================================

#include "Reward.h"
#include "BallPrediction.h"
#include "../CommonValues.h"

#include <map>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace RLGC {

class WallCeilingRedirectReward : public Reward {
public:
    // ------------------------------------------------------------------
    // Config — all tunables in one place (house convention)
    // ------------------------------------------------------------------
    struct Config {
        // --- Sequence detection ---
        float predHorizon          = 1.5f;    // sec — lookahead for upcoming surface hit
        float minNormalSpeed       = 400.0f;  // uu/s into surface required to open a sequence
        float surfacePad           = 60.0f;   // uu beyond ball radius for proximity checks
        float interceptWindow      = 1.6f;    // sec after bounce to land the touch
        float reboundSampleOffset  = 0.35f;   // sec after predicted bounce to sample rebound pos
        float minReboundHeight     = 250.0f;  // uu — predicted rebound must be ≥ this (playable aerially)
        bool  includeBackboard     = false;   // default off — BackboardReboundReward owns it
        bool  includeCeiling       = true;
        bool  includeSideWall      = true;    // also covers corners (45° planes)

        // --- Touch gates ---
        float minAerialZ           = 300.0f;  // uu — player height for an "aerial" intercept
        float selfSetupLookback    = 1.5f;    // sec — own touch within this before bounce = self-setup
        float selfSetupMultiplier  = 0.5f;    // partial credit on self-setup (0 = hard exclude)

        // --- Shot redirect payout ---
        float minShotAlign         = 0.5f;    // post-touch vel · toGoal
        float minShotSpeed         = 1100.0f; // uu/s post-touch
        float baseShotReward       = 3.0f;
        float powerBonusMax        = 0.8f;    // at +1200 uu/s goalward speed added
        float powerRestoreScale    = 1200.0f; // uu/s Δ for full power bonus
        float goalBonus            = 2.0f;    // if state.goalScored on payout frame
        float surfMultCeiling      = 1.5f;    // hardest read (rebound comes straight down)
        float surfMultCorner       = 1.3f;
        float surfMultSideWall     = 1.0f;

        // --- Aerial continuation payout ---
        float baseContinuation     = 1.2f;
        float minContinuationSpeed = 500.0f;  // uu/s — no dead touches
        float minAirtime           = 0.6f;    // sec — kinematic post-touch hang time
        float fullAirtime          = 1.2f;    // sec — airtime for full controlScore
        float maxOwnGoalAlign      = 0.3f;    // continuation must not feed own net

        // --- Safety / shaping ---
        float ownGoalAlignCutoff   = 0.4f;    // shot path: align to own goal above this → 0
        float shapingPerSec        = 0.6f;    // reward/sec at dist = 0 toward rebound point
        float shapingDistScale     = 1000.0f; // uu — exponential falloff
        float shapingCapPerSeq     = 0.4f;    // max shaping per player per sequence
        float maxPayout            = 6.0f;

        bool  shadowMode           = false;   // log metrics, return 0.0f to optimizer
    };

    struct Metrics {
        int    predSequences     = 0;
        int    bouncesDetected   = 0;
        int    aerialIntercepts  = 0;
        int    groundedIntercepts= 0;
        int    shotRedirects     = 0;
        int    continuations     = 0;
        int    ownGoalRedirects  = 0;
        double avgShotAlign      = 0.0;
        double avgPowerRestore   = 0.0;
        double avgEnergyRetained = 0.0;  // postBounceSpeed / preBounceSpeed
        double rewardSum         = 0.0;
        double shapingSum        = 0.0;
    };

private:
    enum class Surface : uint8_t { NONE, SIDEWALL, CORNER, CEILING, BACKBOARD };

    // ---- Global (one ball) sequence state, owned by PreStep ----
    struct BounceState {
        bool     active          = false;   // intercept window open
        bool     consumed        = false;   // first post-bounce touch happened
        Surface  surface         = Surface::NONE;
        float    bounceTime      = -1.0f;
        Vec      bouncePos       = {};
        float    preBounceSpeed  = 0.0f;
        uint32_t setupToucherId  = 0;       // 0 = none / not self-setup
    };

    struct PredState {
        bool    active     = false;
        Surface surface    = Surface::NONE;
        Vec     reboundPos = {};
    };

    BallPred::Cache _ballCache;
    BounceState     _bounce;
    PredState       _pred;

    float    _currentTime      = 0.0f;
    Vec      _prevBallVel      = {};
    bool     _prevValid        = false;
    uint32_t _lastTouchId      = 0;
    float    _lastTouchTime    = -1000.0f;
    uint64_t _seqId            = 0;        // bumps on each new sequence (pred or bounce)

    // Per-player shaping budget for the current sequence
    struct PlayerSeq {
        uint64_t seqId        = 0;
        float    shapingSpent = 0.0f;
    };
    mutable std::map<uint32_t, PlayerSeq> _playerSeq;

    Config config;

    // ---- Metrics — thread-safe atomics (shared across parallel envs) ----
    inline static std::atomic<int>       s_predSequences     { 0 };
    inline static std::atomic<int>       s_bouncesDetected   { 0 };
    inline static std::atomic<int>       s_aerialIntercepts  { 0 };
    inline static std::atomic<int>       s_groundedIntercepts{ 0 };
    inline static std::atomic<int>       s_shotRedirects     { 0 };
    inline static std::atomic<int>       s_continuations     { 0 };
    inline static std::atomic<int>       s_ownGoalRedirects  { 0 };
    inline static std::atomic<long long> s_shotAlignSum      { 0 };  // ×1000
    inline static std::atomic<long long> s_powerRestoreSum   { 0 };  // ×1000
    inline static std::atomic<long long> s_energyRetainedSum { 0 };  // ×1000
    inline static std::atomic<long long> s_rewardMilli       { 0 };
    inline static std::atomic<long long> s_shapingMilli      { 0 };

    // ------------------------------------------------------------------
    // Surface helpers
    // ------------------------------------------------------------------
    bool SurfaceEnabled(Surface s) const {
        switch (s) {
            case Surface::CEILING:   return config.includeCeiling;
            case Surface::SIDEWALL:
            case Surface::CORNER:    return config.includeSideWall;
            case Surface::BACKBOARD: return config.includeBackboard;
            default:                 return false;
        }
    }

    float SurfMult(Surface s) const {
        switch (s) {
            case Surface::CEILING:  return config.surfMultCeiling;
            case Surface::CORNER:   return config.surfMultCorner;
            case Surface::SIDEWALL: return config.surfMultSideWall;
            default:                return 1.0f;
        }
    }

    // Detect an actual bounce event this step from prev→curr ball velocity.
    // Returns surface type and writes the pre-bounce normal speed.
    Surface DetectBounce(const GameState& state, float& normalSpeedOut) const {
        const float r   = CommonValues::BALL_RADIUS;
        const float pad = config.surfacePad;
        const Vec&  p   = state.ball.pos;
        const Vec&  v   = state.ball.vel;
        const Vec&  pv  = _prevBallVel;

        // Corner first — corner region also satisfies wall/backboard proximity.
        {
            float cornerSum = std::fabs(p.x) + std::fabs(p.y);
            if (cornerSum > CommonValues::CORNER_AXIS_INTERCEPT - r - 2.0f * pad) {
                float sx = (p.x >= 0.0f) ? 1.0f : -1.0f;
                float sy = (p.y >= 0.0f) ? 1.0f : -1.0f;
                const float inv = 0.70710678f;  // 1/sqrt(2)
                float prevN = (pv.x * sx + pv.y * sy) * inv;  // into-surface = positive
                float currN = (v.x  * sx + v.y  * sy) * inv;
                if (prevN > config.minNormalSpeed && currN <= 0.0f) {
                    normalSpeedOut = prevN;
                    return Surface::CORNER;
                }
            }
        }
        // Ceiling
        if (p.z > CommonValues::CEILING_Z - r - pad &&
            pv.z > config.minNormalSpeed && v.z <= 0.0f) {
            normalSpeedOut = pv.z;
            return Surface::CEILING;
        }
        // Side wall (X)
        if (std::fabs(p.x) > CommonValues::SIDE_WALL_X - r - pad) {
            float sx = (p.x >= 0.0f) ? 1.0f : -1.0f;
            if (pv.x * sx > config.minNormalSpeed && v.x * sx <= 0.0f) {
                normalSpeedOut = pv.x * sx;
                return Surface::SIDEWALL;
            }
        }
        // Backboard (Y) — classified so we can EXCLUDE it explicitly
        if (std::fabs(p.y) > CommonValues::BACK_WALL_Y - r - pad) {
            float sy = (p.y >= 0.0f) ? 1.0f : -1.0f;
            if (pv.y * sy > config.minNormalSpeed && v.y * sy <= 0.0f) {
                normalSpeedOut = pv.y * sy;
                return Surface::BACKBOARD;
            }
        }
        return Surface::NONE;
    }

    // Predicted surface classification: which non-ground surface fires first
    // in the BallPred window, and does the ball stay airborne until then?
    Surface ClassifyPredictedSurface(float& hitTimeOut) const {
        if (!_ballCache.valid) return Surface::NONE;
        bool prevW = false, prevC = false, prevB = false;
        for (int i = 0; i < BallPred::NUM_SLICES; i++) {
            const auto& s = _ballCache.slices[i];
            if (s.t > config.predHorizon) break;
            if (s.hitGround && !(s.hitWall || s.hitCeiling || s.hitBackboard))
                return Surface::NONE;  // grounds before any surface → not our play
            if (s.hitCeiling && !prevC && !prevW && !prevB) {
                hitTimeOut = s.t; return Surface::CEILING;
            }
            if (s.hitWall && !prevW && !prevC && !prevB) {
                hitTimeOut = s.t; return Surface::SIDEWALL;  // corners fold into wall flag
            }
            if (s.hitBackboard && !prevB && !prevW && !prevC) {
                hitTimeOut = s.t; return Surface::BACKBOARD;
            }
            prevW = s.hitWall; prevC = s.hitCeiling; prevB = s.hitBackboard;
        }
        return Surface::NONE;
    }

    // Kinematic hang time: seconds until ball center falls to BALL_RADIUS,
    // given current z and vz under gravity. (g = |BALL_GRAVITY| = 650)
    static float EstimateAirtime(float z, float vz) {
        const float g = -CommonValues::BALL_GRAVITY;  // 650, positive
        float h = z - CommonValues::BALL_RADIUS;
        if (h <= 0.0f) return 0.0f;
        float disc = vz * vz + 2.0f * g * h;
        if (disc < 0.0f) return 0.0f;
        return (vz + std::sqrt(disc)) / g;
    }

public:
    WallCeilingRedirectReward() : config() {}
    explicit WallCeilingRedirectReward(const Config& cfg) : config(cfg) {}

    static Metrics GetAndResetMetrics() {
        Metrics m;
        m.predSequences      = s_predSequences.exchange(0);
        m.bouncesDetected    = s_bouncesDetected.exchange(0);
        m.aerialIntercepts   = s_aerialIntercepts.exchange(0);
        m.groundedIntercepts = s_groundedIntercepts.exchange(0);
        m.shotRedirects      = s_shotRedirects.exchange(0);
        m.continuations      = s_continuations.exchange(0);
        m.ownGoalRedirects   = s_ownGoalRedirects.exchange(0);
        long long a = s_shotAlignSum.exchange(0);
        long long p = s_powerRestoreSum.exchange(0);
        long long e = s_energyRetainedSum.exchange(0);
        m.rewardSum  = double(s_rewardMilli.exchange(0))  / 1000.0;
        m.shapingSum = double(s_shapingMilli.exchange(0)) / 1000.0;
        if (m.shotRedirects > 0) {
            m.avgShotAlign    = double(a) / (1000.0 * m.shotRedirects);
            m.avgPowerRestore = double(p) / (1000.0 * m.shotRedirects);
        }
        if (m.bouncesDetected > 0)
            m.avgEnergyRetained = double(e) / (1000.0 * m.bouncesDetected);
        return m;
    }

    virtual void Reset(const GameState& initialState) override {
        _bounce       = BounceState();
        _pred         = PredState();
        _currentTime  = 0.0f;
        _prevValid    = false;
        _lastTouchId  = 0;
        _lastTouchTime= -1000.0f;
        _seqId        = 0;
        _playerSeq.clear();
        _ballCache.valid = false;
    }

    virtual void PreStep(const GameState& state) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
        _currentTime += dt;

        _ballCache.Update(state);

        // ---- Actual bounce detection (needs valid prev velocity) ----
        if (_prevValid && !_bounce.active) {
            float normalSpeed = 0.0f;
            Surface s = DetectBounce(state, normalSpeed);
            if (s != Surface::NONE && SurfaceEnabled(s)) {
                _bounce.active         = true;
                _bounce.consumed       = false;
                _bounce.surface        = s;
                _bounce.bounceTime     = _currentTime;
                _bounce.bouncePos      = state.ball.pos;
                _bounce.preBounceSpeed = _prevBallVel.Length();

                // Self-setup attribution: last toucher shortly before bounce
                _bounce.setupToucherId =
                    ((_currentTime - _lastTouchTime) <= config.selfSetupLookback)
                        ? _lastTouchId : 0;

                _seqId++;
                s_bouncesDetected.fetch_add(1, std::memory_order_relaxed);
                if (_bounce.preBounceSpeed > 1.0f) {
                    float retained = state.ball.vel.Length() / _bounce.preBounceSpeed;
                    s_energyRetainedSum.fetch_add(
                        (long long)(RS_CLAMP(retained, 0.0f, 1.0f) * 1000.0f),
                        std::memory_order_relaxed);
                }
            }
        }

        // ---- Intercept window invalidation ----
        if (_bounce.active) {
            bool timeout    = (_currentTime - _bounce.bounceTime) > config.interceptWindow;
            bool hitFloor   = state.ball.pos.z < CommonValues::BALL_RADIUS + 25.0f;
            if (timeout || hitFloor || _bounce.consumed)
                _bounce = BounceState();
        }

        // ---- Anticipation prediction (only while no live window) ----
        if (!_bounce.active) {
            float hitT = 0.0f;
            Surface s = ClassifyPredictedSurface(hitT);
            bool ok = (s != Surface::NONE) && SurfaceEnabled(s);
            if (ok) {
                const auto& rb = _ballCache.AtTime(hitT + config.reboundSampleOffset);
                ok = rb.pos.z >= config.minReboundHeight;  // rebound must be playable aerially
                if (ok) {
                    if (!_pred.active) {
                        _seqId++;
                        s_predSequences.fetch_add(1, std::memory_order_relaxed);
                    }
                    _pred.active     = true;
                    _pred.surface    = s;
                    _pred.reboundPos = rb.pos;
                }
            }
            if (!ok) _pred.active = false;
        } else {
            // Live window: chase target = predicted ball pos a beat from now
            _pred.active     = true;
            _pred.surface    = _bounce.surface;
            _pred.reboundPos = _ballCache.AtTime(0.3f).pos;
        }

        _prevBallVel = state.ball.vel;
        _prevValid   = true;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (player.isDemoed) return 0.0f;

        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
        float reward = 0.0f;

        // ---- Touch bookkeeping (for self-setup attribution) ----
        // NOTE: recorded here, consumed by NEXT step's PreStep bounce detection.
        bool touchedThisStep = player.ballTouchedStep;

        // ---- Anticipation shaping (capped per sequence per player) ----
        if (_pred.active) {
            auto& ps = _playerSeq[player.carId];
            if (ps.seqId != _seqId) { ps.seqId = _seqId; ps.shapingSpent = 0.0f; }

            if (ps.shapingSpent < config.shapingCapPerSeq) {
                float dist = (player.pos - _pred.reboundPos).Length();
                float shaped = config.shapingPerSec * std::exp(-dist / config.shapingDistScale) * dt;
                shaped = RS_MIN(shaped, config.shapingCapPerSeq - ps.shapingSpent);
                ps.shapingSpent += shaped;
                reward += shaped;
                s_shapingMilli.fetch_add((long long)(shaped * 1000.0f),
                                         std::memory_order_relaxed);
            }
        }

        // ---- Payout: first touch inside a live intercept window ----
        if (touchedThisStep && _bounce.active && !_bounce.consumed) {
            _bounce.consumed = true;  // one payout per bounce, first toucher

            bool aerial = !player.isOnGround && player.pos.z >= config.minAerialZ;
            if (!aerial) {
                s_groundedIntercepts.fetch_add(1, std::memory_order_relaxed);
            } else {
                s_aerialIntercepts.fetch_add(1, std::memory_order_relaxed);

                Vec oppGoal = (player.team == Team::BLUE)
                    ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
                Vec ownGoal = (player.team == Team::BLUE)
                    ? CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

                const Vec& postVel = state.ball.vel;       // post-touch (this step)
                // Pre-touch velocity = ball velocity at the END of the previous
                // step. NOTE: _prevBallVel was already overwritten to THIS step's
                // velocity in PreStep() (PreStep runs once per step BEFORE all
                // GetReward calls — see EnvSet.cpp), so reading it here would make
                // the power-restoration Δ identically zero. state.prev holds the
                // previous GameState; its ball.vel is the correct pre-touch value.
                Vec preVel = state.prev ? state.prev->ball.vel : postVel;
                float speed = postVel.Length();

                Vec toGoal    = (oppGoal - state.ball.pos).Normalized();
                Vec toOwnGoal = (ownGoal - state.ball.pos).Normalized();
                Vec dir       = (speed > 1.0f) ? postVel / speed : Vec(0, 0, 0);
                float align    = dir.Dot(toGoal);
                float ownAlign = dir.Dot(toOwnGoal);

                float payout = 0.0f;

                if (ownAlign > config.ownGoalAlignCutoff) {
                    // Redirecting toward own net — never reward
                    s_ownGoalRedirects.fetch_add(1, std::memory_order_relaxed);
                }
                else if (align >= config.minShotAlign && speed >= config.minShotSpeed) {
                    // -------- Outcome A: redirect shot --------
                    float alignScore = RS_CLAMP(
                        (align - config.minShotAlign) / (1.0f - config.minShotAlign),
                        0.0f, 1.0f);
                    float speedScore = RS_CLAMP(speed / 2300.0f, 0.0f, 1.0f);

                    // Power restoration: goalward speed ADDED by the touch.
                    // The bounce stripped 40% of the normal component, so the
                    // touch must re-power the ball toward goal.
                    float dGoalward = postVel.Dot(toGoal) - preVel.Dot(toGoal);
                    float powerRestore = RS_CLAMP(
                        dGoalward / config.powerRestoreScale, 0.0f, 1.0f);

                    payout = (config.baseShotReward * alignScore * speedScore
                              + config.powerBonusMax * powerRestore)
                             * SurfMult(_bounce.surface);

                    if (state.goalScored)
                        payout += config.goalBonus;

                    s_shotRedirects.fetch_add(1, std::memory_order_relaxed);
                    s_shotAlignSum.fetch_add((long long)(align * 1000.0f),
                                             std::memory_order_relaxed);
                    s_powerRestoreSum.fetch_add((long long)(powerRestore * 1000.0f),
                                                std::memory_order_relaxed);
                }
                else {
                    // -------- Outcome B: aerial continuation --------
                    float airtime = EstimateAirtime(state.ball.pos.z, postVel.z);
                    bool controlled =
                        speed   >= config.minContinuationSpeed &&
                        airtime >= config.minAirtime &&
                        ownAlign < config.maxOwnGoalAlign;

                    if (controlled) {
                        float controlScore = RS_CLAMP(
                            airtime / config.fullAirtime, 0.0f, 1.0f);
                        payout = config.baseContinuation * controlScore
                                 * SurfMult(_bounce.surface);
                        s_continuations.fetch_add(1, std::memory_order_relaxed);
                    }
                }

                // Self-setup partial credit (UnifiedDoubleTapReward owns
                // the full own-setup chain; avoid double-paying it here)
                if (_bounce.setupToucherId == player.carId)
                    payout *= config.selfSetupMultiplier;

                reward += RS_CLAMP(payout, 0.0f, config.maxPayout);
            }
        }

        // Record touch for next step's bounce attribution
        if (touchedThisStep) {
            _lastTouchId   = player.carId;
            _lastTouchTime = _currentTime;
        }

        s_rewardMilli.fetch_add((long long)(reward * 1000.0f),
                                std::memory_order_relaxed);

        return config.shadowMode ? 0.0f : reward;
    }
};

} // namespace RLGC
