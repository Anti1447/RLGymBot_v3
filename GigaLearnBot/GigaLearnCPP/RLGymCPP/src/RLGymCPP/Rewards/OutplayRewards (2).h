#pragma once
// ============================================================================
// OutplayRewards.h
//
// Rev 1 - 2025-12-15 (NJH): Add 4 outplay rewards to reduce predictable 50/50 bias.
//   1) FlickOutplayReward
//   2) CutOutplayReward (powerslide optional via handbrakeVal)
//   3) FakeChallengeOffenseReward (replaces old FakeChallengeReward)
//   4) FakeChallengeDefenseReward
//
// Depends on RLGC::Player inheriting RocketSim::CarState (handbrakeVal, isFlipping, hasJumped). 
// ============================================================================

#include <glm/glm.hpp>
#include <map>
#include <cstdint>
#include <cmath>
#include "RewardMath.h"
#include <algorithm>



namespace RLGC {

    /**
    * ============================================================================
    * FLIP RESET REWARD SYSTEM - HEADER ONLY
    * ============================================================================
    *
    * Validates flip resets using:
    * 1. Car & ball airborne requirement (using CarState methods)
    * 2. All 4 wheels contact via normal vector parallelism check
    * 3. Productive use (clear, shot, delayed shot)
    *
    * ============================================================================
    */
    
    // Physical constants
    constexpr float GOAL_LINE_Y = 5120.0f;
    constexpr float OWN_GOAL_Y = -5120.0f;
    constexpr float CEILING_HEIGHT = 2044.0f;
    constexpr float GOAL_HEIGHT = 642.775f;
    constexpr float NET_WIDTH = 888.0f;
    constexpr float BALL_RADIUS = 91.25f;
    constexpr float GOAL_WIDTH_FROM_CENTER = 892.755f;

    // Flip reset validation thresholds
    constexpr float NORMAL_PARALLELISM_THRESHOLD = 0.85f;  // dot(car_normal, ball_normal) > 0.85 = valid wheels
    constexpr float NORMAL_REJECTION_THRESHOLD = -0.3f;    // dot < -0.3 = hood contact (reject)
    constexpr float MIN_AIRTIME_FOR_FLIP = 0.1f;           // Minimum airtime before flip reset can occur
    constexpr float BALL_ON_CAR_HEIGHT_MIN = 40.0f;        // Min height for ball on car
    constexpr float BALL_ON_CAR_HEIGHT_MAX = 150.0f;       // Max height for ball on car
    constexpr float BALL_ON_CAR_HORIZ_MAX = 120.0f;        // Max horizontal distance for ball on car

    // Challenge detection
    constexpr float OPPONENT_CLOSE_DISTANCE = 900.0f;      // Opponent within this = close
    constexpr float OPPONENT_FAST_TOWARD_SPEED = 700.0f;   // Opponent moving toward ball this fast = threat

    /**
     * ============================================================================
     * UTILITY FUNCTIONS (from OutplayRewards.h pattern)
     * ============================================================================
     */

    // -----------------------------
    // Small math helpers
    // -----------------------------
    static inline float DotN(const Vec& a, const Vec& b) {
        return SafeNorm(a).Dot(SafeNorm(b));
    }

    // 0=no change, 1=reverse
    static inline float AngleChange01(const Vec& prevV, const Vec& curV) {
        float a = prevV.Length();
        float b = curV.Length();
        if (a < 1e-3f || b < 1e-3f) return 0.0f;
        float d = RS_CLAMP(DotN(prevV, curV), -1.0f, 1.0f);
        return 0.5f * (1.0f - d);
    }

    // -----------------------------
    // Shared: ball-on-car (same vibe as your FlickReward)
    // -----------------------------
    static inline bool BallOnCarSimple(const Player& player, const GameState& state) {
        Vec rel = state.ball.pos - player.pos;
        float height = rel.z;
        float horiz = Vec(rel.x, rel.y, 0).Length();
        return (height > 40.0f && height < 150.0f && horiz < 120.0f);
    }

    // -----------------------------
    // Shared: opponent challenge detection (closest opponent to ball)
    // Uses distance + vel toward ball (and a mild “closing” proxy).
    // -----------------------------
    struct ChallengeInfo {
        bool active = false;
        uint32_t oppCarId = 0;
        float oppDistToBall = 1e9f;
        float oppVelTowardBall = 0.0f;
        Vec oppPos;
        Vec oppVel;
    };

    static inline ChallengeInfo GetChallengeInfo(const Player& player, const GameState& state) {
        ChallengeInfo ci;

        for (const auto& opp : state.players) {
            if (opp.team == player.team) continue;

            float dNow = (opp.pos - state.ball.pos).Length();
            Vec dirToBall = SafeNorm(state.ball.pos - opp.pos);
            float vTow = RS_MAX(0.0f, opp.vel.Dot(dirToBall));

            if (dNow < ci.oppDistToBall) {
                ci.oppDistToBall = dNow;
                ci.oppCarId = opp.carId;
                ci.oppVelTowardBall = vTow;
                ci.oppPos = opp.pos;
                ci.oppVel = opp.vel;
            }
        }

        if (ci.oppCarId != 0) {
            const bool close = (ci.oppDistToBall < 900.0f);
            const bool fastToward = (ci.oppVelTowardBall > 700.0f);
            ci.active = close && fastToward;
        }

        return ci;
    }

    // -----------------------------
    // Shared: “on-goal” straight-line check (for shaping)
    // -----------------------------
    static inline bool BallIntersectsOppGoalMouth(const Player& player, const GameState& state, float tMaxSec = 5.0f) {
        const float goalY = (player.team == Team::BLUE) ? CommonValues::BACK_WALL_Y : -CommonValues::BACK_WALL_Y;

        const float vy = state.ball.vel.y;
        const float dy = goalY - state.ball.pos.y;

        if (fabs(vy) < 1e-3f) return false;
        if (dy * vy <= 0.0f) return false;

        const float t = dy / vy;
        if (t <= 0.0f || t > tMaxSec) return false;

        const Vec hit = state.ball.pos + state.ball.vel * t;

        const float extraWidth = 250.0f;
        const float extraHeight = 250.0f;

        const float xLimit = CommonValues::GOAL_WIDTH_FROM_CENTER + CommonValues::BALL_RADIUS + extraWidth;
        const float zMin = 0.0f - extraHeight;
        const float zMax = CommonValues::GOAL_HEIGHT + CommonValues::BALL_RADIUS + extraHeight;

        return (fabs(hit.x) <= xLimit) && (hit.z >= zMin) && (hit.z <= zMax);
    }

    // ============================================================================
    // FlickOutplayReward
    // 2025-12-15 - Rev 2 (NJH):
    //  - Pending-flick detection (no need !onCar on same tick as touch).
    //  - Looser/realistic "challenge" detection: dist + (vTow OR commit flags).
    //  - Shorter success window to measure beating initial challenge.
    // ============================================================================
    class FlickOutplayReward : public Reward {
    private:
        struct PendingFlick {
            float timeLeft = 0.0f;   // window to see if opponent touches
            float armLeft = 0.0f;    // window to confirm flick after touch
            uint32_t oppId = 0;
            float quality = 0.0f;
            bool failed = false;
            bool armed = false;
        };

        mutable std::map<uint32_t, float> onCarTime;
        mutable std::map<uint32_t, PendingFlick> pending;

        float minOnCarTime = 0.20f;
        float minFlickSpeed = 1000.0f;

        float confirmWindowSec = 0.12f;   // time after touch to confirm off-car flick
        float successWindowSec = 0.35f;   // time opponent must NOT touch (initial outplay)

        // Challenge thresholds
        float challengeDist = 1200.0f;
        float challengeVTow = 450.0f;

    public:
        virtual void Reset(const GameState& initialState) override {
            onCarTime.clear();
            pending.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
            uint32_t id = player.carId;

            // Resolve pending outplay window (opponent touch check)
            auto it = pending.find(id);
            if (it != pending.end()) {
                PendingFlick& pf = it->second;

                // During success window: if tracked opponent touches -> fail
                if (pf.armed && pf.timeLeft > 0.0f) {
                    for (const auto& opp : state.players) {
                        if (opp.carId == pf.oppId && opp.ballTouchedStep) {
                            pf.failed = true;
                            break;
                        }
                    }

                    pf.timeLeft -= dt;
                    if (pf.timeLeft <= 0.0f) {
                        float out = pf.failed ? 0.0f : (10.0f + 10.0f * RS_CLAMP(pf.quality, 0.0f, 1.0f));
                        pending.erase(it);
                        return out;
                    }
                }

                // While confirming flick after touch
                if (!pf.armed && pf.armLeft > 0.0f) {
                    pf.armLeft -= dt;
                }
                else if (!pf.armed) {
                    // never confirmed -> discard
                    pending.erase(it);
                    return 0.0f;
                }
            }

            // Track on-car time
            bool onCar = BallOnCarSimple(player, state);
            float& tOn = onCarTime[id];

            if (onCar && player.isOnGround) tOn += dt;
            else if (!onCar) tOn = 0.0f;

            // Identify challenger (closest opponent)
            ChallengeInfo ci = GetChallengeInfo(player, state);

            // Make "active" more realistic using commit flags
            bool oppCommit = false;
            if (ci.oppCarId != 0) {
                for (const auto& opp : state.players) {
                    if (opp.carId == ci.oppCarId) {
                        oppCommit = (opp.hasJumped || opp.isFlipping);
                        break;
                    }
                }
            }

            bool challengeActive =
                (ci.oppCarId != 0) &&
                (ci.oppDistToBall < challengeDist) &&
                (ci.oppVelTowardBall > challengeVTow || oppCommit);

            // Arm a pending flick on touch (if we were dribbling and being challenged)
            if (player.ballTouchedStep && tOn >= minOnCarTime && challengeActive) {
                PendingFlick pf;
                pf.armLeft = confirmWindowSec;
                pf.timeLeft = successWindowSec;
                pf.oppId = ci.oppCarId;
                pf.quality = 0.0f;
                pf.failed = false;
                pf.armed = false;
                pending[id] = pf;
            }

            // Confirm flick and set quality once ball leaves on-car region
            auto it2 = pending.find(id);
            if (it2 != pending.end()) {
                PendingFlick& pf = it2->second;
                if (!pf.armed && pf.armLeft > 0.0f && !onCar) {
                    float spd = state.ball.vel.Length();
                    tOn = 0.0f;

                    if (spd < minFlickSpeed) {
                        pending.erase(it2);
                        return 0.0f;
                    }

                    float q = 0.0f;

                    // On-goal best
                    if (BallIntersectsOppGoalMouth(player, state, 5.0f)) q += 0.7f;

                    // Pop-up helps beat low challenge
                    if (state.ball.vel.z > 600.0f) q += 0.35f;

                    // Away from opponent position
                    Vec toOpp = ci.oppPos - state.ball.pos;
                    if (state.ball.vel.Dot(toOpp) < -200.0f) q += 0.35f;

                    pf.quality = RS_CLAMP(q, 0.0f, 1.0f);
                    pf.armed = true;

                    // Now we start the success window countdown (already set)
                }
            }

            return 0.0f;
        }
    };


    // 2025-12-18 | Rev 4.0 | NJH
// ============================================================================
// FlickOnGoalReward (TURNKEY)
// Requires:
//  - Ball on car (top, close) for >= minOnCarTime
//  - Then a touch that produces a flick (ball leaves car + minimum speed)
// Rewards:
//  - Full credit if ball trajectory intersects opponent goal mouth (RewardMath helper)
//  - Scales by speed: <= slowKph -> minReward, >= fastKph -> maxReward
//  - Optional partial credit if strongly toward opponent goal
// ============================================================================
    class FlickOnGoalReward : public Reward {
    private:
        struct Pending {
            float timeLeft = 0.0f;
            bool armed = false;
        };

        mutable std::map<uint32_t, float> ballOnCarTime;
        mutable std::map<uint32_t, Pending> pending;

        // Dribble gate
        float minOnCarTime = 0.25f;

        // "Ball on car" geometry (match your FlickReward)
        float minHeightAboveCar = 40.0f;
        float maxHeightAboveCar = 155.0f;
        float maxHorizDist = 120.0f;

        // Flick gate
        float minFlickSpeed = 600.0f;      // uu/s
        float pendingWindowSec = 0.12f;

        // Speed scaling for on-goal flicks
        float slowKph = 60.0f;
        float fastKph = 120.0f;
        float minOnGoalReward = 9.0f;
        float maxOnGoalReward = 18.0f;

        // Partial shaping if not "on goal"
        float partialTowardGoalDot = 900.0f; // uu/s toward opponent goal
        float partialMin = 5.0f;
        float partialMax = 10.0f;

    private:
        inline float SpeedFracFromKphUU(float ballSpeedUU) const {
            const float slowUU = RLGC::Math::KPHToVel(slowKph);
            const float fastUU = RLGC::Math::KPHToVel(fastKph);
            if (fastUU <= slowUU + 1e-3f) return 1.0f;
            return RS_CLAMP((ballSpeedUU - slowUU) / (fastUU - slowUU), 0.0f, 1.0f);
        }

    public:
        virtual void Reset(const GameState& initialState) override {
            ballOnCarTime.clear();
            pending.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            const float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
            const uint32_t id = player.carId;

            // --- Ball-on-car detection ---
            const Vec relBallPos = state.ball.pos - player.pos;
            const float heightAboveCar = relBallPos.z;
            const float horizDist = Vec(relBallPos.x, relBallPos.y, 0.0f).Length();

            const bool ballOnCar =
                (heightAboveCar > minHeightAboveCar &&
                    heightAboveCar < maxHeightAboveCar &&
                    horizDist < maxHorizDist);

            auto& tOnCar = ballOnCarTime[id];
            if (ballOnCar && player.isOnGround) {
                tOnCar += dt;
            }
            else if (!ballOnCar) {
                tOnCar = 0.0f;
            }

            // --- Arm pending on the touch step if we're dribbling ---
            if (player.ballTouchedStep && tOnCar >= minOnCarTime) {
                pending[id] = Pending{ pendingWindowSec, true };
            }

            // --- Resolve pending (must happen shortly after) ---
            auto it = pending.find(id);
            if (it != pending.end()) {
                Pending& p = it->second;
                p.timeLeft -= dt;

                // Resolve only once the ball is no longer "on car"
                if (!ballOnCar) {
                    const float ballSpeed = state.ball.vel.Length();

                    // Must be a meaningful flick pop
                    if (ballSpeed >= minFlickSpeed) {

                        // FULL CREDIT if it intersects opponent goal mouth
                        if (RLGC::RewardMath::BallIntersectsOpponentGoalMouth(player, state, 2.0f, true, true)) {
                            const float frac = SpeedFracFromKphUU(ballSpeed);
                            const float reward = minOnGoalReward + (maxOnGoalReward - minOnGoalReward) * frac;

                            pending.erase(it);
                            tOnCar = 0.0f;
                            return reward;
                        }

                        // Partial credit if still strongly toward opponent goal
                        const Vec oppGoal = (player.team == Team::BLUE) ? CommonValues::ORANGE_GOAL_BACK
                            : CommonValues::BLUE_GOAL_BACK;
                        const Vec toGoalN = SafeNorm(oppGoal - state.ball.pos);
                        const float toward = state.ball.vel.Dot(toGoalN);

                        pending.erase(it);
                        tOnCar = 0.0f;

                        if (toward > partialTowardGoalDot) {
                            const float f = RS_CLAMP((toward - partialTowardGoalDot) / 1200.0f, 0.0f, 1.0f);
                            return partialMin + (partialMax - partialMin) * f;
                        }
                        return 0.0f;
                    }

                    // Ball left car but wasn't a flick-speed pop
                    pending.erase(it);
                    tOnCar = 0.0f;
                    return 0.0f;
                }

                // Timeout
                if (p.timeLeft <= 0.0f) {
                    pending.erase(it);
                }
            }

            return 0.0f;
        }
    };




    // ============================================================================
    // 2) CutOutplayReward
    // - Rewards a strong change in ball direction (cut) while challenged,
    //   moving away from challenger, with optional pop and optional powerslide bonus.
    // - Uses player.handbrakeVal as powerslide signal (RocketSim CarState). :contentReference[oaicite:4]{index=4}
    // ============================================================================
    class CutOutplayReward : public Reward {
    private:
        float minBallSpeedAfter = 700.0f;

    public:
        virtual void Reset(const GameState& initialState) override {}

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!state.prev) return 0.0f;
            if (!player.ballTouchedStep) return 0.0f;

            // Cuts are usually ground-ish
            if (state.ball.pos.z > 260.0f) return 0.0f;

            ChallengeInfo ci = GetChallengeInfo(player, state);
            if (!ci.active) return 0.0f;

            const Vec prevV = state.prev->ball.vel;
            const Vec curV = state.ball.vel;
            const float curSpd = curV.Length();
            if (curSpd < minBallSpeedAfter) return 0.0f;

            // Direction change strength
            float ang01 = AngleChange01(prevV, curV);
            if (ang01 < 0.20f) return 0.0f;

            // Moving away from opponent
            Vec toOpp = ci.oppPos - state.ball.pos;
            float away = 0.0f;
            float toOppLen = toOpp.Length();
            if (toOppLen > 1e-3f) {
                away = RS_CLAMP((-curV.Dot(toOpp / toOppLen)) / 2000.0f, 0.0f, 1.0f);
            }

            // Pop/chip shaping (optional)
            float pop = 0.0f;
            float dz = state.ball.pos.z - state.prev->ball.pos.z;
            if (curV.z > 250.0f) pop += RS_CLAMP(curV.z / 1500.0f, 0.0f, 1.0f) * 0.6f;
            if (dz > 5.0f)      pop += RS_CLAMP(dz / 80.0f, 0.0f, 1.0f) * 0.4f;
            pop = RS_CLAMP(pop, 0.0f, 1.0f);

            // Powerslide bonus (optional)
            float slideBonus = 0.0f;
            if (player.isOnGround && player.handbrakeVal > 0.55f) {
                slideBonus = 0.25f;
            }

            float score =
                (0.55f * RS_CLAMP(ang01 * 1.25f, 0.0f, 1.0f)) +
                (0.35f * away) +
                (0.25f * pop) +
                slideBonus;

            score = RS_CLAMP(score, 0.0f, 1.25f);

            return 10.0f * score;
        }
    };

    // ============================================================================
    // 3) FakeChallengeOffenseReward
    // Replace your old fake reward with something that requires:
    // - soft possession
    // - “threaten touch” then “abort” without touching
    // - opponent COMMIT (hasJumped OR isFlipping)
    // - after window, we’re closer to ball than opponent (kept possession / gained space)
    // ============================================================================
    class FakeChallengeOffenseReward : public Reward {
    private:
        struct PendingFake {
            float timeLeft = 0.0f;
            uint32_t oppId = 0;
            bool oppCommitted = false;
            float startSep = 0.0f; // (oppDist - myDist) at start
        };

        mutable std::map<uint32_t, bool> wasThreatening;
        mutable std::map<uint32_t, PendingFake> pending;

    public:
        virtual void Reset(const GameState& initialState) override {
            wasThreatening.clear();
            pending.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
            uint32_t id = player.carId;

            // Resolve pending
            auto pit = pending.find(id);
            if (pit != pending.end()) {
                PendingFake& pf = pit->second;

                // detect commit
                for (const auto& opp : state.players) {
                    if (opp.carId != pf.oppId) continue;
                    if (opp.hasJumped || opp.isFlipping) pf.oppCommitted = true; // CarState flags :contentReference[oaicite:5]{index=5}
                    break;
                }

                pf.timeLeft -= dt;
                if (pf.timeLeft <= 0.0f) {
                    // reward if opp committed AND we end up closer than opp
                    float myDist = (player.pos - state.ball.pos).Length();
                    float oppDist = 1e9f;
                    for (const auto& opp : state.players) {
                        if (opp.carId == pf.oppId) {
                            oppDist = (opp.pos - state.ball.pos).Length();
                            break;
                        }
                    }

                    float out = 0.0f;
                    if (pf.oppCommitted && myDist + 120.0f < oppDist) {
                        float sepEnd = (oppDist - myDist);
                        float sepGain01 = RS_CLAMP((sepEnd - pf.startSep) / 600.0f, 0.0f, 1.0f);
                        out = 8.0f + 6.0f * sepGain01;
                    }

                    pending.erase(pit);
                    return out;
                }
            }

            ChallengeInfo ci = GetChallengeInfo(player, state);
            if (ci.oppCarId == 0) return 0.0f;

            float myDist = (player.pos - state.ball.pos).Length();
            float oppDist = ci.oppDistToBall;

            // soft possession: we’re close and not clearly beaten
            bool haveSoftPossession = (myDist < 900.0f) && (myDist <= oppDist + 80.0f);
            if (!haveSoftPossession) return 0.0f;

            // threaten touch: strong approach toward ball (but not necessarily touching)
            Vec dirToBall = SafeNorm(state.ball.pos - player.pos);
            float vTow = player.vel.Dot(dirToBall);

            bool threatening = (vTow > 650.0f) && (myDist < 1100.0f);
            bool prevThreat = wasThreatening[id];
            wasThreatening[id] = threatening;

            // abort: stop threatening without touching
            bool abort = prevThreat && !threatening && !player.ballTouchedStep;

            // only if opponent is actually near
            if (abort && oppDist < 900.0f) {
                PendingFake pf;
                pf.timeLeft = 0.45f;
                pf.oppId = ci.oppCarId;
                pf.oppCommitted = false;
                pf.startSep = (oppDist - myDist);
                pending[id] = pf;
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // 4) FakeChallengeDefenseReward
    // Context:
    // - opponent has possession (they’re clearly closer)
    // - we are goal-side (between ball and our goal)
    // Behavior:
    // - we “sell” a challenge (fast approach), then abort without touching
    // - opponent commits (jump/flip), AND ball threat decreases (less toward our goal)
    // ============================================================================
    class FakeChallengeDefenseReward : public Reward {
    private:
        struct PendingDefFake {
            float timeLeft = 0.0f;
            uint32_t oppId = 0;
            bool oppCommitted = false;
            float startThreat = 0.0f;
        };

        mutable std::map<uint32_t, bool> wasSelling;
        mutable std::map<uint32_t, PendingDefFake> pending;

        static inline Vec MyGoalBack(const Player& player) {
            return (player.team == Team::BLUE) ? CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;
        }

        static inline float ThreatTowardMyGoal(const Player& player, const GameState& state) {
            Vec myGoal = MyGoalBack(player);
            Vec toGoal = SafeNorm(myGoal - state.ball.pos);
            return state.ball.vel.Dot(toGoal); // positive = dangerous
        }

    public:
        virtual void Reset(const GameState& initialState) override {
            wasSelling.clear();
            pending.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
            uint32_t id = player.carId;

            // Resolve pending
            auto pit = pending.find(id);
            if (pit != pending.end()) {
                PendingDefFake& pf = pit->second;

                for (const auto& opp : state.players) {
                    if (opp.carId != pf.oppId) continue;
                    if (opp.hasJumped || opp.isFlipping) pf.oppCommitted = true; // CarState flags :contentReference[oaicite:6]{index=6}
                    break;
                }

                pf.timeLeft -= dt;
                if (pf.timeLeft <= 0.0f) {
                    float threatNow = ThreatTowardMyGoal(player, state);

                    float out = 0.0f;
                    if (pf.oppCommitted) {
                        float dThreat = pf.startThreat - threatNow; // positive is good
                        float red01 = RS_CLAMP(dThreat / 1200.0f, 0.0f, 1.0f);

                        Vec myGoal = MyGoalBack(player);
                        float myDistGoal = (player.pos - myGoal).Length();
                        float ballDistGoal = (state.ball.pos - myGoal).Length();
                        float goalSide01 = (myDistGoal < ballDistGoal) ? 1.0f : 0.0f;

                        out = (6.0f * red01) + (2.0f * goalSide01);
                    }

                    pending.erase(pit);
                    return out;
                }
            }

            ChallengeInfo ci = GetChallengeInfo(player, state);
            if (ci.oppCarId == 0) return 0.0f;

            float myDist = (player.pos - state.ball.pos).Length();
            float oppDist = ci.oppDistToBall;

            // opponent has possession
            bool oppHasPossession = (oppDist + 120.0f < myDist);
            if (!oppHasPossession) return 0.0f;

            // goal-side (shadow)
            Vec myGoal = MyGoalBack(player);
            float myDistGoal = (player.pos - myGoal).Length();
            float ballDistGoal = (state.ball.pos - myGoal).Length();
            bool goalSide = (myDistGoal < ballDistGoal);
            if (!goalSide) return 0.0f;

            // sell challenge: fast approach
            Vec dirToBall = SafeNorm(state.ball.pos - player.pos);
            float vTow = player.vel.Dot(dirToBall);

            bool selling = (vTow > 900.0f) && (myDist < 1600.0f);
            bool prevSelling = wasSelling[id];
            wasSelling[id] = selling;

            bool abort = prevSelling && !selling && !player.ballTouchedStep;
            if (abort) {
                PendingDefFake pf;
                pf.timeLeft = 0.60f;
                pf.oppId = ci.oppCarId;
                pf.oppCommitted = false;
                pf.startThreat = ThreatTowardMyGoal(player, state);
                pending[id] = pf;
            }

            return 0.0f;
        }
    };

    /**
 * ============================================================================
 * FLIP RESET REWARD SYSTEM - HEADER ONLY
 * ============================================================================
 *
 * Validates flip resets using:
 * 1. Car & ball airborne requirement
 * 2. All 4 wheels contact via normal vector parallelism check
 * 3. Productive use (clear, shot, delayed shot)
 *
 * ============================================================================
 */

 // Physical constants
    constexpr float GOAL_LINE_Y = 5120.0f;
    constexpr float OWN_GOAL_Y = -5120.0f;
    constexpr float CEILING_HEIGHT = 2044.0f;
    constexpr float GOAL_HEIGHT = 642.775f;
    constexpr float NET_WIDTH = 888.0f;
    constexpr float BALL_RADIUS = 91.25f;

    // Flip reset validation thresholds
    constexpr float NORMAL_PARALLELISM_THRESHOLD = 0.85f;  // dot(car_normal, ball_normal) > 0.85 = valid
    constexpr float NORMAL_REJECTION_THRESHOLD = -0.3f;    // dot < -0.3 = hood contact (reject)
    constexpr float CAR_BALL_CONTACT_DISTANCE = 200.0f;    // Maximum distance for wheel contact
    constexpr float MIN_AIRTIME_FOR_FLIP = 0.1f;           // Minimum airtime before flip reset can occur

    /**
     * ============================================================================
     * CORE FLIP RESET VALIDATION
     * ============================================================================
     */

    class FlipResetValidator {
    public:
        /**
         * REQUIREMENT #1 & #2: Validate flip reset occurred with proper mechanics
         *
         * Checks:
         * - Car is airborne (no ground contact)
         * - Ball is airborne (z > ball_radius)
         * - All 4 wheels contacted ball via normal vector parallelism
         * - Not a hood contact (normal vectors pointing in same direction)
         * - Jump capability was restored (flip reset flag)
         */
        static bool IsValidFlipReset(
            bool car_was_airborne,
            bool ball_is_airborne,
            bool all_wheels_had_contact,
            float car_normal_dot_ball_normal,  // Result of dot product between normals
            bool jump_capability_restored,
            float time_in_air_before_contact
        ) {
            // Requirement #1: Both must be airborne
            if (!car_was_airborne || !ball_is_airborne) {
                return false;
            }

            // Requirement #2: Check normal vector parallelism for wheel contact
            // Valid: normals parallel and pointing same direction (0.85 < dot < 1.0)
            // Invalid: Hood contact would have opposite direction (dot < -0.3)
            if (car_normal_dot_ball_normal < NORMAL_PARALLELISM_THRESHOLD) {
                return false;  // Normals not parallel enough
            }

            if (car_normal_dot_ball_normal < NORMAL_REJECTION_THRESHOLD) {
                return false;  // Hood contact (normals pointing opposite)
            }

            // All wheels must have contacted the ball
            if (!all_wheels_had_contact) {
                return false;
            }

            // Jump capability must have been restored
            if (!jump_capability_restored) {
                return false;
            }

            // Reasonable airtime (not the instant we left ground)
            if (time_in_air_before_contact < MIN_AIRTIME_FOR_FLIP) {
                return false;
            }

            return true;
        }

        /**
         * Calculate the normal vector dot product for validation
         * This is what you'll call to check parallelism
         */
        static float CalculateNormalParallelism(
            const glm::vec3& car_normal_up,      // Car's upward normal (hood to sky direction)
            const glm::vec3& ball_normal_at_contact  // Ball's surface normal at contact point
        ) {
            glm::vec3 car_norm = glm::normalize(car_normal_up);
            glm::vec3 ball_norm = glm::normalize(ball_normal_at_contact);
            return glm::dot(car_norm, ball_norm);
        }

        /**
         * Determine if normals are parallel and in same direction (valid wheel contact)
         */
        static bool AreNormalsParallel(float dot_product) {
            // Parallel and same direction: 0.85 to 1.0
            return dot_product > NORMAL_PARALLELISM_THRESHOLD;
        }

        /**
         * Determine if this is hood contact (reject)
         */
        static bool IsHoodContact(float dot_product) {
            // Opposite direction: < -0.3
            return dot_product < NORMAL_REJECTION_THRESHOLD;
        }
    };

    /**
     * ============================================================================
     * PRODUCTIVE USE VALIDATION
     * ============================================================================
     */

    class ProductiveUseValidator {
    public:
        enum UsageType {
            INVALID = 0,
            OFFENSIVE_SHOT = 1,      // Shooting on opponent goal
            DEFENSIVE_CLEAR = 2,     // Clearing away from own goal
            DELAYED_SHOT = 3,        // Shot that occurs > 1s after flip reset
            NEUTRAL_REDIRECT = 4     // Ball deflection in neutral zone
        };

        /**
         * REQUIREMENT #3: Detect productive use of flip reset
         *
         * Validates that the flip was used for:
         * - Clear (ball moving away from own goal)
         * - Shot (ball moving toward opponent goal)
         * - Delayed shot (shot occurs after flip reset with time gap)
         */
        static UsageType ClassifyUsage(
            const glm::vec3& ball_pos_at_flip,
            const glm::vec3& ball_pos_after_action,
            const glm::vec3& ball_vel_after_action,
            int team_id,
            float time_since_flip_reset
        ) {
            // Determine goal directions
            float own_goal_y = (team_id == 0) ? OWN_GOAL_Y : GOAL_LINE_Y;
            float opponent_goal_y = (team_id == 0) ? GOAL_LINE_Y : OWN_GOAL_Y;

            // Check if ball is moving toward opponent goal
            bool moving_toward_opponent = (opponent_goal_y > 0) ?
                (ball_vel_after_action.y > 100.0f) :
                (ball_vel_after_action.y < -100.0f);

            // Check if ball is moving toward own goal
            bool moving_toward_own = (own_goal_y > 0) ?
                (ball_vel_after_action.y > 100.0f) :
                (ball_vel_after_action.y < -100.0f);

            // Determine zone
            float distance_to_own = std::abs(ball_pos_after_action.y - own_goal_y);
            float distance_to_opponent = std::abs(ball_pos_after_action.y - opponent_goal_y);

            // Check for delayed action (>1s after flip reset)
            bool is_delayed = time_since_flip_reset > 1.0f;

            // Classify usage
            if (distance_to_opponent < distance_to_own) {
                // Near opponent goal
                if (moving_toward_opponent) {
                    return is_delayed ? DELAYED_SHOT : OFFENSIVE_SHOT;
                }
            }

            if (distance_to_own < distance_to_opponent) {
                // Near own goal
                if (moving_toward_own && moving_toward_opponent) {
                    // Ambiguous, treat as neutral
                    return NEUTRAL_REDIRECT;
                }
                if (!moving_toward_own) {
                    // Clear away from goal
                    return DEFENSIVE_CLEAR;
                }
            }

            // Midfield or ambiguous
            if (moving_toward_opponent) {
                return is_delayed ? DELAYED_SHOT : OFFENSIVE_SHOT;
            }

            return INVALID;
        }

        /**
         * Check if usage is productive (not invalid)
         */
        static bool IsProductive(UsageType usage) {
            return usage != INVALID;
        }
    };

    /**
     * ============================================================================
     * FLIP RESET REWARD CALCULATOR
     * ============================================================================
     */

    class FlipResetReward {
    public:
        struct Config {
            // Acquisition phase
            float flip_reset_trigger_reward = 0.3f;
            float wheel_contact_bonus = 0.1f;

            // Productive use rewards
            float offensive_shot_reward = 1.0f;
            float offensive_shot_wide_reward = 0.5f;
            float defensive_clear_reward = 0.8f;
            float delayed_shot_bonus = 0.2f;
            float neutral_redirect_reward = 0.3f;

            // Penalties
            float invalid_usage_penalty = -0.1f;
            float failed_clear_penalty = -0.2f;
        };

        Config config;

        /**
         * Calculate reward for flip reset acquisition
         */
        float CalculateFlipResetAcquisitionReward(
            bool is_valid_flip_reset,
            float normal_parallelism_dot
        ) const {
            if (!is_valid_flip_reset) {
                return 0.0f;
            }

            float reward = config.flip_reset_trigger_reward;

            // Bonus for perfect normal alignment (>0.95)
            if (normal_parallelism_dot > 0.95f) {
                reward += config.wheel_contact_bonus;
            }

            return reward;
        }

        /**
         * Calculate reward for productive use
         */
        float CalculateProductiveUseReward(
            ProductiveUseValidator::UsageType usage_type,
            const glm::vec3& ball_pos,
            const glm::vec3& goal_center,
            int team_id
        ) const {
            switch (usage_type) {
            case ProductiveUseValidator::OFFENSIVE_SHOT: {
                // Check if shot is on target
                float x_distance = std::abs(ball_pos.x - goal_center.x);
                if (x_distance < NET_WIDTH / 2.0f) {
                    return config.offensive_shot_reward;
                }
                else if (x_distance < NET_WIDTH / 2.0f + 300.0f) {
                    return config.offensive_shot_wide_reward;
                }
                return config.invalid_usage_penalty;
            }

            case ProductiveUseValidator::DELAYED_SHOT: {
                // Delayed shot gets bonus
                float x_distance = std::abs(ball_pos.x - goal_center.x);
                if (x_distance < NET_WIDTH / 2.0f) {
                    return config.offensive_shot_reward + config.delayed_shot_bonus;
                }
                else if (x_distance < NET_WIDTH / 2.0f + 300.0f) {
                    return config.offensive_shot_wide_reward + config.delayed_shot_bonus;
                }
                return config.invalid_usage_penalty;
            }

            case ProductiveUseValidator::DEFENSIVE_CLEAR: {
                return config.defensive_clear_reward;
            }

            case ProductiveUseValidator::NEUTRAL_REDIRECT: {
                return config.neutral_redirect_reward;
            }

            case ProductiveUseValidator::INVALID:
            default: {
                return config.invalid_usage_penalty;
            }
            }
        }

        /**
         * Complete flip reset reward calculation
         * Call after flip reset occurs and is validated, then after productive use
         */
        float CalculateTotalFlipResetReward(
            bool is_valid_flip_reset,
            float normal_parallelism_dot,
            ProductiveUseValidator::UsageType usage_type,
            const glm::vec3& ball_pos,
            const glm::vec3& goal_center,
            int team_id
        ) const {
            // First: reward for acquisition
            float acquisition_reward = CalculateFlipResetAcquisitionReward(
                is_valid_flip_reset,
                normal_parallelism_dot
            );

            if (acquisition_reward <= 0.0f) {
                return 0.0f;  // Invalid flip reset, no further reward
            }

            // Second: reward for productive use
            float usage_reward = CalculateProductiveUseReward(
                usage_type,
                ball_pos,
                goal_center,
                team_id
            );

            // Combine rewards (acquisition + usage)
            return acquisition_reward + usage_reward;
        }
    };

} // namespace RLGC
