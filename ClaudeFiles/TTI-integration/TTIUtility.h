// TTIUtility.h
// Time-To-Intercept (TTI) utility: Euclidean baseline vs. Dubins/Reed-Shepp
// approximation with vertical jump & boost height components.
//
// Consumed by TTIPossessionControlReward (below) and any other reward
// that wants a shared, physics-aware reachability estimate.
//
// Design goals
//  - Single source of truth for TTI across all rewards (no more per-reward
//    custom CalculateTimeToReach() reimplementations)
//  - Exposes both Euclidean TTI and Dubins TTI so callers can compare or
//    choose whichever suits their gradient budget
//  - Caches per-frame results keyed on carId so multiple rewards calling
//    the utility in the same step pay the cost only once
//  - All physics constants pulled from CommonValues.h – no magic numbers
//
// Place in: GigaLearnCPP/GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/
//
// 2026-03-03 | Rev 1.0 | NJH
// ============================================================================

#pragma once
#include "Reward.h"
#include "../Math.h"
#include "../CommonValues.h"
#include <map>
#include <cmath>
#include <algorithm>
#include <limits>

namespace RLGC {

// ============================================================================
// SECTION 1 – PHYSICS CONSTANTS
// All values sourced from CommonValues.h / RLConst.
// ============================================================================
namespace TTIPhysics {
    constexpr float GRAVITY          = -650.0f;               // UU/s²
    constexpr float JUMP_IMPULSE     = CommonValues::JUMP_IMMEDIATE_FORCE; // 292 UU/s
    constexpr float JUMP_HOLD_FORCE  = CommonValues::JUMP_HOLD_FORCE;      // 1458 UU/s²
    constexpr float JUMP_HOLD_MAX    = CommonValues::JUMP_MAX_DURATION;    // 0.2 s
    constexpr float BOOST_ACCEL_AIR  = CommonValues::BOOST_ACCEL_AIR;      // 1058.333 UU/s²
    constexpr float BOOST_RATE       = CommonValues::BOOST_CONSUMPTION_RATE;// 33.3 /s
    constexpr float CAR_MAX_SPEED    = CommonValues::CAR_MAX_SPEED;         // 2300
    constexpr float CAR_MAX_SPEED_NB = CommonValues::CAR_MAX_SPEED_NO_BOOST;// 1410
    constexpr float CAR_MAX_ANG_VEL  = CommonValues::CAR_MAX_ANG_VEL;       // 5.5 rad/s

    // Minimum turning radius at a given forward speed v:
    //   r_min = v / omega_max
    // At supersonic (2300 UU/s): r ≈ 418 UU
    // At mid speed  (1500 UU/s): r ≈ 273 UU
    // At slow       ( 500 UU/s): r ≈  91 UU
    inline float MinTurnRadius(float speed) {
        float spd = std::max(speed, 50.0f);
        return spd / CAR_MAX_ANG_VEL;
    }
}

// ============================================================================
// SECTION 2 – AERIAL BRANCH ENUM
// Mirrors the Gemini doc's AerialBranch but tied to your car state so the
// solver can pick the cheapest branch automatically.
// ============================================================================
enum class AerialBranch : uint8_t {
    GROUND,       // Car stays on ground / wall
    SINGLE_JUMP,  // One jump, no double jump
    DOUBLE_JUMP,  // Jump + double-jump (height ≈ 2× single)
    FAST_AERIAL   // Jump + boost from the jump, max height ceiling
};

// ============================================================================
// SECTION 3 – VERTICAL HEIGHT PREDICTION
//
// What Gemini proposed (simplified):
//   z += vz*t + 0.5*g*t²  (+impulse per branch)
//
// What we add:
//   - Correct held-jump force integral over JUMP_HOLD_MAX
//   - Boost height contribution only for as long as car has boost
//   - Clamp to arena ceiling
// ============================================================================
struct VerticalProfile {
    float reachableZ;   // max Z attainable at time t
    float arrivalTime;  // time when car can be at that Z (may differ from t)
};

inline VerticalProfile PredictVerticalProfile(
    float t,
    const Player& car,
    AerialBranch branch,
    float boostAvailable)           // 0-100
{
    float z  = car.pos.z;
    float vz = car.vel.z;

    // ---- Apply jump impulse -------------------------------------------------
    if (branch == AerialBranch::SINGLE_JUMP ||
        branch == AerialBranch::FAST_AERIAL  ||
        branch == AerialBranch::DOUBLE_JUMP)
    {
        vz += TTIPhysics::JUMP_IMPULSE;

        // Integrate held-jump force (clamped to max hold duration)
        float holdT = std::min(t, TTIPhysics::JUMP_HOLD_MAX);
        vz += TTIPhysics::JUMP_HOLD_FORCE * holdT;
    }

    // ---- Double jump: second impulse at midpoint ----------------------------
    if (branch == AerialBranch::DOUBLE_JUMP) {
        float dj_t = std::min(t * 0.5f, 0.5f); // rough timing of 2nd jump
        vz += TTIPhysics::JUMP_IMPULSE;          // no hold on double jump
        (void)dj_t;                               // timing handled via vz already
    }

    // ---- Gravity -----------------------------------------------------------
    z += vz * t + 0.5f * TTIPhysics::GRAVITY * t * t;

    // ---- Boost vertical contribution (FAST_AERIAL only) --------------------
    // Boost provides acceleration in the car's forward direction; for a fast
    // aerial the car pitches back ~90° so most boost goes vertical.
    // We approximate: effective vertical boost accel ≈ 80% of BOOST_ACCEL_AIR
    if (branch == AerialBranch::FAST_AERIAL && boostAvailable > 0.0f) {
        float boostTime  = std::min(t, boostAvailable / TTIPhysics::BOOST_RATE);
        float boostDeltaZ = 0.8f * TTIPhysics::BOOST_ACCEL_AIR * 0.5f * boostTime * boostTime;
        z += boostDeltaZ;
    }

    // ---- Clamp to floor / ceiling ------------------------------------------
    z = std::max(17.0f, z);
    z = std::min(CommonValues::CEILING_Z, z);

    return { z, t };
}

// ============================================================================
// SECTION 4 – EUCLIDEAN TTI (current approach, kept for comparison)
//
// What the old CalculateTimeToReach() did:
//   - Straight-line distance / effective speed
//   - Turn penalty as a scalar multiplier on alignment
//   - No vertical component
//
// Kept here verbatim so TTIPossessionControlReward can expose the delta
// between Euclidean and Dubins to W&B for analysis.
// ============================================================================
inline float EuclideanTTI(const Player& car, const Vec& targetPos)
{
    Vec toTarget    = targetPos - car.pos;
    float dist      = toTarget.Length();
    if (dist < 1.0f) return 0.0f;

    Vec dirToTarget = toTarget.Normalized();
    float speedToward = std::max(0.0f, car.vel.Dot(dirToTarget));
    bool  hasBoost    = car.boost > 5.0f;

    // Effective speed estimate
    float effSpeed = hasBoost
        ? std::min(speedToward + 1000.0f, TTIPhysics::CAR_MAX_SPEED)
        : std::min(speedToward +  500.0f, TTIPhysics::CAR_MAX_SPEED_NB);
    effSpeed = std::max(effSpeed, 300.0f);

    // Turn penalty: same as before (alignment scalar)
    float alignment  = car.rotMat.forward.Dot(dirToTarget);
    float turnFactor = (alignment < 0.7f)
        ? 1.5f + (1.0f - alignment) * 0.5f
        : 1.0f;

    return (dist / effSpeed) * turnFactor;
}

// ============================================================================
// SECTION 5 – DUBINS TTI (new, horizontal plane)
//
// A Dubins path is the shortest path for a car with a minimum turning radius
// that must move forward only.  For RL purposes we use the LSL/RSR arc-arc
// length approximation; full Dubins path computation is expensive so we use
// the upper-bound from the tangent-arc formula:
//
//   L_dubins ≈ 2*r*θ + straight_segment
//
// where θ is the heading change required to face the target.
//
// Compared to Euclidean: Dubins correctly penalises large heading errors
// (especially at high speed where turn radius is large) instead of using
// the crude turnFactor scalar.
//
// Vertical component: after solving the horizontal Dubins time, check whether
// the ball's Z is reachable via the cheapest aerial branch. If not, add the
// extra time to pitch up and boost.
// ============================================================================
struct DubinsTTIResult {
    float   tti;            // total estimated time to intercept (seconds)
    AerialBranch branch;    // aerial branch selected (GROUND if ball is low)
    float   horizontalTime; // time for horizontal arc + straight
    float   verticalPad;    // extra time added for vertical reach
    float   euclideanTTI;   // baseline for comparison (same call)
};

inline DubinsTTIResult DubinsTTI(const Player& car, const Vec& targetPos, float boostAvailable)
{
    // ---- Horizontal Dubins approximation -----------------------------------
    Vec delta2D  = Vec(targetPos.x - car.pos.x, targetPos.y - car.pos.y, 0.0f);
    float dist2D = delta2D.Length();

    float speed = car.vel.Length();
    speed = std::max(speed, 200.0f); // avoid division by zero at standstill
    speed = std::min(speed, TTIPhysics::CAR_MAX_SPEED);

    float rMin = TTIPhysics::MinTurnRadius(speed);

    // Heading difference in horizontal plane
    Vec carFwd2D = Vec(car.rotMat.forward.x, car.rotMat.forward.y, 0.0f);
    float fwdLen = carFwd2D.Length();
    float headingDelta = 0.0f;
    if (fwdLen > 0.01f && dist2D > 1.0f) {
        carFwd2D = carFwd2D * (1.0f / fwdLen);
        Vec dirToTarget2D = delta2D * (1.0f / dist2D);
        float cosA = std::max(-1.0f, std::min(1.0f, carFwd2D.Dot(dirToTarget2D)));
        headingDelta = std::acos(cosA); // [0, pi]
    }

    // Arc length = r * theta; straight = max(0, dist2D - 2*r*sin(theta/2))
    float arcLen      = rMin * headingDelta;
    float chordOffset = 2.0f * rMin * std::sin(headingDelta * 0.5f);
    float straightLen = std::max(0.0f, dist2D - chordOffset);

    // Effective horizontal speed (boost budget in horizontal motion)
    bool  hasBoost   = boostAvailable > 5.0f;
    float horizSpeed = hasBoost
        ? std::min(speed + 600.0f, TTIPhysics::CAR_MAX_SPEED)
        : std::min(speed + 200.0f, TTIPhysics::CAR_MAX_SPEED_NB);
    horizSpeed = std::max(horizSpeed, 300.0f);

    float hTime = (arcLen + straightLen) / horizSpeed;

    // ---- Vertical reach check ----------------------------------------------
    float ballZ = targetPos.z;
    float vertPad = 0.0f;
    AerialBranch bestBranch = AerialBranch::GROUND;

    if (ballZ > 200.0f) { // Ball is meaningfully off the ground
        // Try branches cheapest-first; pick the first that reaches ballZ within hTime
        const AerialBranch branches[] = {
            AerialBranch::SINGLE_JUMP,
            AerialBranch::DOUBLE_JUMP,
            AerialBranch::FAST_AERIAL
        };
        bool reached = false;
        for (auto b : branches) {
            // Can we even attempt this branch?
            if (b == AerialBranch::FAST_AERIAL && boostAvailable < 15.0f) continue;
            if ((b == AerialBranch::SINGLE_JUMP || b == AerialBranch::DOUBLE_JUMP)
                && car.hasDoubleJumped) continue;

            VerticalProfile vp = PredictVerticalProfile(hTime, car, b, boostAvailable);
            if (vp.reachableZ >= ballZ - 50.0f) { // 50 UU tolerance
                bestBranch = b;
                reached    = true;
                break;
            }
        }
        if (!reached) {
            // Ball is too high for any branch within hTime; estimate extra time
            // needed by iterating t upward until a fast aerial can reach it.
            // Simple binary search: max extra 3 seconds.
            float lo = 0.0f, hi = 3.0f;
            for (int i = 0; i < 12; ++i) {
                float mid = (lo + hi) * 0.5f;
                VerticalProfile vp = PredictVerticalProfile(hTime + mid, car,
                                        AerialBranch::FAST_AERIAL, boostAvailable);
                if (vp.reachableZ >= ballZ - 50.0f) hi = mid;
                else                                 lo = mid;
            }
            vertPad    = (lo + hi) * 0.5f;
            bestBranch = AerialBranch::FAST_AERIAL;
        }
    }

    float totalTTI = hTime + vertPad;

    // ---- Euclidean baseline for W&B delta logging --------------------------
    float euclid = EuclideanTTI(car, targetPos);

    return { totalTTI, bestBranch, hTime, vertPad, euclid };
}

// ============================================================================
// SECTION 6 – TTI SNAPSHOT (per-frame cache entry)
//
// Computed once per player per step, shared across rewards.
// ============================================================================
struct TTISnapshot {
    float selfTTI;          // Dubins TTI for this player
    float selfTTI_euclid;   // Euclidean TTI for same player (comparison)
    float bestAllyTTI;      // lowest Dubins TTI among teammates (inf if 1v1)
    float bestOppTTI;       // lowest Dubins TTI among opponents
    float possession;       // (bestOppTTI - selfTTI) / normaliser ∈ [-1, 1]
    float teamPossession;   // (bestOppTTI - min(selfTTI, bestAllyTTI)) / norm
    AerialBranch selfBranch;
    AerialBranch bestOppBranch;

    // Deltas relative to previous frame (for fake/bait detection)
    float dSelfTTI;         // selfTTI - prevSelfTTI  (negative = closing faster)
    float dBestOppTTI;      // bestOppTTI - prevBestOppTTI (positive = opp falling behind)
};

// ============================================================================
// SECTION 7 – TTI CACHE
//
// Lightweight singleton-style cache.  Call TTICache::Compute() once at the
// start of each reward's GetReward(); subsequent rewards in the same step
// get the cached result instantly.
//
// NOTE: RocketSim ticks are monotonically increasing (lastTickCount).
//       We key on that + carId.
// ============================================================================
class TTICache {
public:
    // Call at the top of GetReward() for any reward that uses TTI.
    // Returns the snapshot for 'player'; all other players are also computed
    // and cached so this is O(n) once and O(1) for subsequent callers.
    static const TTISnapshot& Get(const Player& player, const GameState& state) {
        // If cache is stale (new tick), rebuild entirely
        if (state.lastTickCount != s_lastTick) {
            s_lastTick = state.lastTickCount;
            s_snapshots.clear();

            // Compute snapshots for every player in the game
            for (const auto& p : state.players) {
                if (p.isDemoed) {
                    TTISnapshot snap{};
                    snap.selfTTI        = 99.0f;
                    snap.selfTTI_euclid = 99.0f;
                    snap.bestAllyTTI    = 99.0f;
                    snap.bestOppTTI     = 99.0f;
                    snap.possession     = -1.0f;
                    snap.teamPossession = -1.0f;
                    s_snapshots[p.carId] = snap;
                    continue;
                }

                DubinsTTIResult self = DubinsTTI(p, state.ball.pos, p.boost);

                float bestAlly = std::numeric_limits<float>::infinity();
                float bestOpp  = std::numeric_limits<float>::infinity();
                AerialBranch oppBranch = AerialBranch::GROUND;

                for (const auto& other : state.players) {
                    if (other.carId == p.carId) continue;
                    if (other.isDemoed)          continue;

                    DubinsTTIResult res = DubinsTTI(other, state.ball.pos, other.boost);

                    if (other.team == p.team) {
                        bestAlly = std::min(bestAlly, res.tti);
                    } else {
                        if (res.tti < bestOpp) {
                            bestOpp    = res.tti;
                            oppBranch  = res.branch;
                        }
                    }
                }

                // Normalise to [-1, 1]; 2 seconds = full possession swing
                constexpr float NORM = 2.0f;
                float selfMin  = std::min(self.tti, bestAlly);
                float poss     = std::min( 1.0f, std::max(-1.0f, (bestOpp - self.tti)  / NORM));
                float teamPoss = std::min( 1.0f, std::max(-1.0f, (bestOpp - selfMin)   / NORM));

                // Deltas from previous frame
                float dSelf = 0.0f, dOpp = 0.0f;
                auto prev = s_prevSnapshots.find(p.carId);
                if (prev != s_prevSnapshots.end()) {
                    dSelf = self.tti           - prev->second.selfTTI;
                    dOpp  = bestOpp            - prev->second.bestOppTTI;
                }

                TTISnapshot snap;
                snap.selfTTI        = self.tti;
                snap.selfTTI_euclid = self.euclideanTTI;
                snap.bestAllyTTI    = (bestAlly == std::numeric_limits<float>::infinity())
                                      ? 99.0f : bestAlly;
                snap.bestOppTTI     = (bestOpp  == std::numeric_limits<float>::infinity())
                                      ? 99.0f : bestOpp;
                snap.possession     = poss;
                snap.teamPossession = teamPoss;
                snap.selfBranch     = self.branch;
                snap.bestOppBranch  = oppBranch;
                snap.dSelfTTI       = dSelf;
                snap.dBestOppTTI    = dOpp;

                s_snapshots[p.carId] = snap;
            }

            // Rotate prev ← current
            s_prevSnapshots = s_snapshots;
        }

        return s_snapshots.at(player.carId);
    }

private:
    static inline uint64_t                          s_lastTick      = 0;
    static inline std::map<uint32_t, TTISnapshot>   s_snapshots;
    static inline std::map<uint32_t, TTISnapshot>   s_prevSnapshots;
};

// ============================================================================
// SECTION 8 – TTI POSSESSION CONTROL REWARD
//
// Drop-in replacement / upgrade for TimeToBalPossessionReward and
// MultiAgentPossessionReward.  Uses TTICache so adding this reward has
// negligible cost if other TTI-aware rewards already call the cache.
//
// Reward structure:
//
//   R = w_possess  * possession_score          (individual TTI advantage)
//     + w_team     * team_possession_delta     (team TTI advantage gained)
//     + w_duration * duration_bonus            (holding possession over time)
//     + w_dubins   * dubins_accuracy_bonus     (reward for using better paths;
//                                               Dubins > Euclidean TTI → more
//                                               accurate read, higher weight)
//     + w_closing  * closing_rate_bonus        (dSelfTTI < 0 = closing fast)
//     + w_opening  * opp_diverge_bonus         (dBestOppTTI > 0 = opp falling
//                                               behind after a fake/cut)
//
// The last two terms make this reward directly sense faking outcomes without
// a separate "DetectFake" reward – when the bot executes a fake the opponent's
// TTI suddenly jumps (positive dBestOppTTI) while the bot's own TTI drops.
// ============================================================================
class TTIPossessionControlReward : public Reward {
public:
    // ---- Tuneable weights --------------------------------------------------
    struct Config {
        float wPossess       = 1.0f;  // individual possession
        float wTeam          = 0.8f;  // team possession (2v2/3v3 bonus)
        float wDuration      = 0.4f;  // duration bonus (diminishing returns)
        float wDubinsAccuracy= 0.2f;  // reward Dubins > Euclidean accuracy
        float wClosing       = 0.3f;  // reward closing rate on ball
        float wOppDiverge    = 0.5f;  // reward opponent diverging (fake effect)

        float possThreshold  = 0.15f; // minimum possession score to accumulate duration
        float durationCap    = 5.0f;  // seconds before diminishing returns plateau

        // Presets
        static Config Conservative() {
            Config c; c.wPossess=0.6f; c.wTeam=0.5f; c.wDuration=0.2f;
            c.wDubinsAccuracy=0.1f; c.wClosing=0.2f; c.wOppDiverge=0.3f; return c;
        }
        static Config Balanced()     { return Config{}; }
        static Config Aggressive() {
            Config c; c.wPossess=1.4f; c.wTeam=1.0f; c.wDuration=0.5f;
            c.wDubinsAccuracy=0.3f; c.wClosing=0.5f; c.wOppDiverge=0.8f; return c;
        }
    };

    explicit TTIPossessionControlReward(Config cfg = Config::Balanced())
        : _cfg(cfg) {}

    virtual void Reset(const GameState& initialState) override {
        _duration.clear();
        _prevTeamPoss.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (player.isDemoed) return 0.0f;

        const TTISnapshot& snap = TTICache::Get(player, state);
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
        float reward = 0.0f;

        // ---- 1. Individual possession score --------------------------------
        // snap.possession ∈ [-1, 1]: positive = we arrive first
        float poss = snap.possession;
        reward += _cfg.wPossess * poss;

        // ---- 2. Team possession score (only meaningful in 2v2/3v3) ---------
        float teamPoss = snap.teamPossession;
        reward += _cfg.wTeam * teamPoss;

        // ---- 3. Duration bonus ---------------------------------------------
        auto& dur = _duration[player.carId];
        if (poss > _cfg.possThreshold) {
            dur += dt;
            float durBonus = std::min(dur, _cfg.durationCap) / _cfg.durationCap;
            reward += _cfg.wDuration * durBonus;
        } else {
            dur = 0.0f;
        }

        // ---- 4. Dubins accuracy bonus --------------------------------------
        // When Dubins TTI < Euclidean TTI the bot is taking a better path
        // than the naive straight-line estimate predicted; reward that.
        // When Euclidean < Dubins (e.g. near-straight path) the bonus is ~0.
        float dubinsGain = snap.selfTTI_euclid - snap.selfTTI; // positive = Dubins is better
        if (dubinsGain > 0.05f) {
            // Normalise: 0.5 s gain → full bonus
            float normGain = std::min(dubinsGain / 0.5f, 1.0f);
            reward += _cfg.wDubinsAccuracy * normGain;
        }

        // ---- 5. Closing rate bonus -----------------------------------------
        // dSelfTTI < 0 means this player is reaching the ball faster than last frame
        if (snap.dSelfTTI < -0.01f) {
            float closeBonus = std::min(-snap.dSelfTTI / 0.1f, 1.0f); // 0.1 s/frame = full
            reward += _cfg.wClosing * closeBonus;
        }

        // ---- 6. Opponent diverge bonus (fake/bait signal) ------------------
        // dBestOppTTI > 0 means the fastest opponent is now further from the
        // ball than last frame – this is exactly what a successful fake produces.
        // We gate it: must have had positive possession in the previous frame so
        // we don't reward random opponent wandering.
        auto prevIt = _prevTeamPoss.find(player.carId);
        bool hadPoss = (prevIt != _prevTeamPoss.end()) && (prevIt->second > 0.1f);
        if (hadPoss && snap.dBestOppTTI > 0.02f) {
            float divergeBonus = std::min(snap.dBestOppTTI / 0.3f, 1.0f);
            reward += _cfg.wOppDiverge * divergeBonus;
        }
        _prevTeamPoss[player.carId] = poss;

        return reward;
    }

private:
    Config _cfg;
    std::map<uint32_t, float> _duration;
    std::map<uint32_t, float> _prevTeamPoss;
};

} // namespace RLGC
