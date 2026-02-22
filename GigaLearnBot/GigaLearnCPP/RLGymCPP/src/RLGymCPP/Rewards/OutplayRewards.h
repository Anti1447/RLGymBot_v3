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
    // Shared: check if opponent is positioned to contest
    // -----------------------------
    static inline bool IsOpponentPositionedToContest(const Player& player, const Player& opponent, const GameState& state) {
        // Opponent must be close to the ball
        float oppDistToBall = (opponent.pos - state.ball.pos).Length();
        if (oppDistToBall > 1200.0f) return false;

        // Opponent must be between player and ball, or approaching from similar angle
        Vec playerToBall = state.ball.pos - player.pos;
        Vec oppToBall = state.ball.pos - opponent.pos;

        // Check if opponent is moving toward ball
        float oppVelTowardBall = RS_MAX(0.0f, opponent.vel.Dot(SafeNorm(oppToBall)));
        if (oppVelTowardBall < 300.0f) return false;

        // Check if opponent is positioned to intercept
        float angleToOpp = DotN(playerToBall, opponent.pos - player.pos);
        return angleToOpp > 0.3f;  // Opponent is generally in front of us
    }

    // -----------------------------
    // Shared: "on-goal" straight-line check (for shaping)
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
    // ============================================================================
    // FlickOutplayReward - REFINED
    // 2026-01-12 - Rev 3 (NJH/Claude):
    // Explicit "behind opponent" detection
    // Better quality scoring(goal = 0.9, not stacking)
    // Tighter challenge thresholds
    // Slightly longer confirm window
    // ============================================================================
    class FlickOutplayReward : public Reward {
    private:
        struct PendingFlick {
            float timeLeft = 0.0f;
            float armLeft = 0.0f;
            uint32_t oppId = 0;
            Vec ballPosAtFlick;      // NEW: Track position for "behind" check
            float quality = 0.0f;
            bool failed = false;
            bool armed = false;
        };

        mutable std::map<uint32_t, float> onCarTime;
        mutable std::map<uint32_t, PendingFlick> pending;

        float minOnCarTime = 0.22f;           // Slightly longer dribble window
        float minFlickSpeed = 1050.0f;        // Slightly higher threshold

        float confirmWindowSec = 0.16f;       // 2 frames @ 120Hz - more forgiving
        float successWindowSec = 0.35f;

        float challengeDist = 1200.0f;
        float challengeVTow = 500.0f;         // Tighter: must be actually approaching

        // NEW: "Behind opponent" detection
        float behindOpponentMinDotProduct = -0.3f;  // ~107 degrees away from their facing

    public:
        virtual void Reset(const GameState& initialState) override {
            onCarTime.clear();
            pending.clear();
        }

        // NEW HELPER: Check if flick is behind opponent (relative to their facing direction)
        float ComputeBehindScore(const Vec& ballVel, const Player& opponent) {
            // Opponent's forward direction
            Vec oppFacing = opponent.rotMat.forward;

            // How opposite is ball velocity to opponent's facing?
            float dotProduct = ballVel.Dot(oppFacing);

            // -1 = directly behind, 0 = sideways, +1 = directly in front
            // We want negative (behind), so invert and clamp to [0, 1]
            return RS_CLAMP(-dotProduct / 1000.0f, 0.0f, 1.0f);
        }

        // IMPROVED HELPER: Check if flick sends ball toward opponent's side/back
        bool IsBallDirectionGood(const Vec& ballVel, const Vec& ballPos,
            const Player& opponent, const Player& player) {
            // Goal direction for context
            Vec oppGoal = (player.team == Team::BLUE) ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
            Vec toOppGoal = SafeNorm(oppGoal - ballPos);

            // Vector from opponent to ball (point away from them)
            Vec awayFromOpp = SafeNorm(ballPos - opponent.pos);

            // For a good flick:
            // 1. Goes toward goal (component toward goal > 0)
            // 2. Goes away from opponent (component away from opponent > 0)
            // 3. Preferably behind/to the side (not directly at them)

            float towardGoal = ballVel.Dot(toOppGoal);
            float awayFromThem = ballVel.Dot(awayFromOpp);
            float behindScore = ComputeBehindScore(ballVel, opponent);

            // Goal is most important, then away, then behind
            return (towardGoal > 500.0f) && (awayFromThem > 200.0f);
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
            uint32_t id = player.carId;

            // Resolve pending outplay window
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
                    pending.erase(it);
                    return 0.0f;
                }
            }

            // Track on-car time
            bool onCar = BallOnCarSimple(player, state);
            float& tOn = onCarTime[id];

            if (onCar && player.isOnGround) tOn += dt;
            else if (!onCar) tOn = 0.0f;

            // Identify challenger
            ChallengeInfo ci = GetChallengeInfo(player, state);

            // Check if opponent is actively committing
            bool oppCommit = false;
            const Player* oppPtr = nullptr;
            if (ci.oppCarId != 0) {
                for (const auto& opp : state.players) {
                    if (opp.carId == ci.oppCarId) {
                        oppCommit = (opp.hasJumped || opp.isFlipping);
                        oppPtr = &opp;
                        break;
                    }
                }
            }

            bool challengeActive =
                (ci.oppCarId != 0) &&
                (ci.oppDistToBall < challengeDist) &&
                (ci.oppVelTowardBall > challengeVTow || oppCommit);

            // Arm a pending flick on touch
            if (player.ballTouchedStep && tOn >= minOnCarTime && challengeActive) {
                PendingFlick pf;
                pf.armLeft = confirmWindowSec;
                pf.timeLeft = successWindowSec;
                pf.oppId = ci.oppCarId;
                pf.ballPosAtFlick = state.ball.pos;
                pf.quality = 0.0f;
                pf.failed = false;
                pf.armed = false;
                pending[id] = pf;
            }

            // Confirm flick and set quality once ball leaves car
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

                    // On-goal: strongest indicator of a good flick
                    if (BallIntersectsOppGoalMouth(player, state, 5.0f)) {
                        q = 0.9f;  // Highest reward
                    }
                    else {
                        // Pop-up (ball.vel.z > 600 → ~1200+ units height potential)
                        if (state.ball.vel.z > 600.0f) q += 0.35f;

                        // Direction quality: toward goal + away from opponent
                        if (oppPtr && IsBallDirectionGood(state.ball.vel, state.ball.pos, *oppPtr, player)) {
                            q += 0.35f;

                            // Additional behind-score bonus: how much is the ball going BEHIND
                            // the opponent relative to their facing direction?
                            // ComputeBehindScore returns 0..1 (1 = directly behind their back).
                            // Previously computed but discarded — now integrated as up to +0.15 quality.
                            // Total non-on-goal budget: 0.35 + 0.35 + 0.15 = 0.85 max (clamped to 1.0).
                            float behindScore = ComputeBehindScore(state.ball.vel, *oppPtr);
                            q += 0.15f * behindScore;
                        }
                        else if (state.ball.vel.Dot(SafeNorm(pf.ballPosAtFlick - ci.oppPos)) < -200.0f) {
                            q += 0.25f;  // At least going away from their position
                        }
                    }

                    pf.quality = RS_CLAMP(q, 0.0f, 1.0f);
                    pf.armed = true;
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

        // Height constraint for flicks (stricter than default goal mouth check)
        float maxFlickHeightAtGoal = 580.0f;  // Max height at goal line (goal height ~640, leave some margin)

        // Partial shaping if not "on goal" (REDUCED - stricter requirements)
        float partialTowardGoalDot = 1200.0f; // uu/s toward opponent goal (was 900.0f)
        float partialMin = 3.0f;              // Reduced from 5.0f
        float partialMax = 7.0f;              // Reduced from 10.0f

    private:
        inline float SpeedFracFromKphUU(float ballSpeedUU) const {
            const float slowUU = RLGC::Math::KPHToVel(slowKph);
            const float fastUU = RLGC::Math::KPHToVel(fastKph);
            if (fastUU <= slowUU + 1e-3f) return 1.0f;
            return RS_CLAMP((ballSpeedUU - slowUU) / (fastUU - slowUU), 0.0f, 1.0f);
        }

        // Check if flick trajectory will be at valid height when crossing goal line
        inline bool IsFlickHeightValid(const Player& player, const GameState& state) const {
            // Determine opponent goal line Y position
            float goalLineY = (player.team == Team::BLUE) ? CommonValues::GOAL_LINE_Y : -CommonValues::GOAL_LINE_Y;

            // Check if ball is moving toward goal
            float vyTowardGoal = (player.team == Team::BLUE) ? state.ball.vel.y : -state.ball.vel.y;
            if (vyTowardGoal <= 0.0f) return false;

            // Calculate time to reach goal line
            float timeToGoal = (goalLineY - state.ball.pos.y) / state.ball.vel.y;
            if (timeToGoal <= 0.0f || timeToGoal > 2.0f) return false;

            // Calculate height at goal line (with gravity)
            float heightAtGoal = state.ball.pos.z + state.ball.vel.z * timeToGoal +
                                 0.5f * CommonValues::BALL_GRAVITY * timeToGoal * timeToGoal;

            // Check if height is within valid range
            return (heightAtGoal >= BALL_RADIUS && heightAtGoal <= maxFlickHeightAtGoal);
        }

        // Check if this is NOT an open net scenario (flicks are OK when opponent is in the way)
        // Returns false if we're closer to goal than all opponents (open net = should just shoot)
        inline bool ShouldFlickInsteadOfShoot(const Player& player, const GameState& state) const {
            // Determine opponent goal position
            Vec oppGoal = (player.team == Team::BLUE) ? CommonValues::ORANGE_GOAL_BACK
                                                      : CommonValues::BLUE_GOAL_BACK;

            float playerDistToGoal = (player.pos - oppGoal).Length();

            // Check all opponents - if ANY opponent is closer to goal than us, flick is OK
            // (they're defending, so we need fancy flick to beat them)
            for (const auto& other : state.players) {
                if (other.team == player.team) continue;  // Skip teammates

                float oppDistToGoal = (other.pos - oppGoal).Length();

                // If opponent is closer to goal than us, they're defending - flick is appropriate
                if (oppDistToGoal < playerDistToGoal - 300.0f) {  // 300 unit margin
                    return true;
                }
            }

            // No opponents between us and goal = open net = just shoot, don't waste time flicking
            return false;
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

                        // FULL CREDIT if it intersects goal, height valid, AND not an open net scenario
                        if (RLGC::RewardMath::BallIntersectsOpponentGoalMouth(player, state, 2.0f, true, true) &&
                            IsFlickHeightValid(player, state) &&
                            ShouldFlickInsteadOfShoot(player, state)) {
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
	// NEW 2026-01-30 (NJH):Rev 5.0
    // - Rewards a strong change in ball direction (cut) while challenged,
    //   moving away from challenger, with optional pop and optional powerslide bonus.
    // - Uses player.handbrakeVal as powerslide signal (RocketSim CarState). :contentReference[oaicite:4]{index=4}
    // ============================================================================
    // ============================================================================
    // OPTIMIZED CutOutplayReward - Multi-Branch Response Detection
    // Clearly distinguishes CUT, POP, and FAKE responses to opponent challenges.
    // Additive reward system: responses can combine for multi-technique execution.
    // ============================================================================
    class CutOutplayReward : public Reward {
    private:
        // Threat verification thresholds
        const float THREAT_DISTANCE = 1500.0f;      // Opponent must be within this distance
        const float THREAT_VELOCITY = 500.0f;        // Opponent velocity component toward ball (dot product)

        // Response detection thresholds
        const float MIN_BALL_SPEED = 700.0f;
        const float ANGLE_THRESHOLD_CUT = 0.40f;     // Direction change required for CUT
        const float ANGLE_THRESHOLD_FAKE = 0.30f;    // Direction change required for FAKE
        const float VELOCITY_DROP_THRESHOLD = 0.90f; // Velocity must drop to this ratio for FAKE
        const float POP_MIN_VELOCITY_Z = 300.0f;     // Minimum vertical velocity for POP
        const float POP_MIN_HEIGHT_GAIN = 20.0f;     // Minimum height gain for POP

    public:
        virtual void Reset(const GameState& initialState) override {}

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            // === VALIDATION & SETUP ===
            if (!state.prev || !player.ballTouchedStep) return 0.0f;
            if (state.ball.pos.z > 260.0f) return 0.0f;  // Ground-level only

            // === THREAT VERIFICATION ===
            // Check if opponent is actually challenging with proximity AND velocity
            ChallengeInfo ci = GetChallengeInfo(player, state);
            if (!ci.active) return 0.0f;

            // Verify opponent is close enough AND moving toward ball
            if (!IsOpponentThreatening(ci, state)) {
                return 0.0f;
            }

            // Get ball velocities
            const Vec prevV = state.prev->ball.vel;
            const Vec curV = state.ball.vel;
            const float curSpd = curV.Length();

            if (curSpd < MIN_BALL_SPEED) return 0.0f;

            // === RESPONSE TYPE DETECTION (Additive System) ===
            // Multiple responses can be detected and combined
            float totalReward = 0.0f;

            // 1. CUT RESPONSE (60% angle + 40% away-movement)
            float cutReward = DetectCut(player, state, prevV, curV, ci);
            totalReward += cutReward;

            // 2. POP RESPONSE (50% velocity + 30% height + 20% angle)
            float popReward = DetectPop(player, state, prevV, curV);
            totalReward += popReward;

            // 3. FAKE RESPONSE (30% maximum, velocity drop signature)
            float fakeReward = DetectFake(prevV, curV);
            totalReward += fakeReward;

            // Clamp final reward to 0-1.0 range
            return RS_CLAMP(totalReward, 0.0f, 1.0f);
        }

    private:
        // === THREAT VERIFICATION ===
        // Returns true if opponent is genuinely threatening (close + moving toward ball)
        bool IsOpponentThreatening(const ChallengeInfo& ci, const GameState& state) const {
            // Must be within threat distance
            if (ci.oppDistToBall > THREAT_DISTANCE) return false;

            // Must be moving toward ball with sufficient velocity
            if (ci.oppVelTowardBall < THREAT_VELOCITY) return false;

            return true;
        }

        // === RESPONSE TYPE 1: CUT ===
        // Direction change on ground, moving away from opponent
        // Reward: 0.4-0.9 based on angle + away-movement
        float DetectCut(const Player& player, const GameState& state,
            const Vec& prevV, const Vec& curV, const ChallengeInfo& ci) const {
            float ang01 = AngleChange01(prevV, curV);
            if (ang01 < ANGLE_THRESHOLD_CUT) return 0.0f;

            // Must be ground-level
            if (!player.isOnGround) return 0.0f;

            // Must be moving away from opponent
            Vec toOpp = ci.oppPos - state.ball.pos;
            float toOppLen = toOpp.Length();
            if (toOppLen < 1e-3f) return 0.0f;

            float awayComponent = RS_CLAMP((-curV.Dot(toOpp / toOppLen)) / 2000.0f, 0.0f, 1.0f);
            float angleComponent = RS_CLAMP(ang01, 0.0f, 1.0f);

            // CUT: 60% angle strength + 40% away-movement
            return (0.6f * angleComponent) + (0.4f * awayComponent);
        }

        // === RESPONSE TYPE 2: POP ===
        // Vertical velocity gain + height gain + direction change
        // Reward: 0.3-0.8 based on vertical velocity, height, and angle
        float DetectPop(const Player& player, const GameState& state,
            const Vec& prevV, const Vec& curV) const {
            float verticalVel = curV.z;
            float heightGain = state.ball.pos.z - state.prev->ball.pos.z;

            // Must show pop signature: vertical velocity OR height gain
            if (verticalVel < POP_MIN_VELOCITY_Z && heightGain < POP_MIN_HEIGHT_GAIN) {
                return 0.0f;
            }

            // Direction change must exist
            float ang01 = AngleChange01(prevV, curV);
            if (ang01 < ANGLE_THRESHOLD_CUT) return 0.0f;

            // POP: 50% vertical velocity + 30% height gain + 20% angle
            float velComponent = RS_CLAMP(verticalVel / 1500.0f, 0.0f, 1.0f);
            float heightComponent = RS_CLAMP(heightGain / 80.0f, 0.0f, 1.0f);
            float angleComponent = RS_CLAMP(ang01, 0.0f, 1.0f);

            return (0.5f * velComponent) + (0.3f * heightComponent) + (0.2f * angleComponent);
        }

        // === RESPONSE TYPE 3: FAKE ===
        // Direction change with velocity DROP (characteristic fake signature)
        // Reward: 0.2-0.3 based on velocity drop (riskier, less effective than cut/pop)
        float DetectFake(const Vec& prevV, const Vec& curV) const {
            float ang01 = AngleChange01(prevV, curV);
            if (ang01 < ANGLE_THRESHOLD_FAKE) return 0.0f;

            float prevSpd = prevV.Length();
            float curSpd = curV.Length();

            // FAKE signature: velocity must DROP after the touch
            // (fake = touch ball to change direction but slow it down)
            if (curSpd >= prevSpd * VELOCITY_DROP_THRESHOLD) return 0.0f;

            // FAKE: lower reward than cut/pop (riskier, less effective)
            // Scale based on how much velocity was lost
            float velocityDrop = 1.0f - RS_CLAMP(curSpd / (prevSpd * VELOCITY_DROP_THRESHOLD), 0.0f, 1.0f);
            return 0.3f * velocityDrop;  // Max 0.3 for FAKE
        }
    };
 //   /**
 //   * ============================================================================
 //   * CutOutplayReward - STATE MACHINE VERSION
 //   *
 //   * 2025-12-28 | Rev 3.0 | NJH
 //   *
 //   * Three-phase execution:
 //   *   PHASE 1: Detect valid cut attempt (soft possession + approaching challenger)
 //   *   PHASE 2: Validate cut mechanics (dramatic direction change, ball acceleration)
 //   *   PHASE 3: Track outcome over 0.6s (whiff, regain possession, or ball trajectory)
 //   *
 //   * Outcomes:
 //   *   - Opponent whiffs (commits but misses): Full reward
 //   *   - We regain possession or shoot: High reward
 //   *   - Ball toward opponent goal: Medium reward
 //   *   - Ball toward our goal: Low reward
 //   * ============================================================================
 //   */
 //   // ============================================================================
 //   // CutOutplayReward - REFINED
	//// 2026-01-12 - Rev 4.0 (NJH/Claude):
 //   // Prevents low-velocity angle-change false positives
 //   // Only counts first regain attempt(not repeated touches)
 //   // Explicit "ball toward opponent = bad cut" check
 //   // Better separation of outcome rewards
 //   // Lower ball height threshold for ground - based cuts
 //   // Prevents re - arming during outcome window
 //   // ============================================================================
 //   class CutOutplayReward : public Reward {
 //   private:
 //       struct PendingCut {
 //           float timeLeft = 0.0f;
 //           float whiffCheckTimeLeft = 0.0f;
 //           uint32_t oppId = 0;
 //           Vec ballPosAtCut;
 //           Vec ballVelAtCut;
 //           bool oppCommitted = false;
 //           bool oppWhiffed = false;
 //           float ballTrajectoryScore = 0.0f;
 //           int regainTouchCount = 0;  // NEW: Prevent counting multiple regains
 //       };

 //       mutable std::map<uint32_t, float> onBallTime;
 //       mutable std::map<uint32_t, bool> inActiveCut;  // NEW: Prevent re-arming during outcome
 //       mutable std::map<uint32_t, PendingCut> pending;

 //       float maxDistToBall = 500.0f;
 //       float challengerApproachDist = 850.0f;      // Tighter: must be really close
 //       float challengerVelocityThreshold = 700.0f;  // Slightly higher
 //       float minOnBallTime = 0.15f;

 //       float minPrevBallVelocity = 300.0f;         // NEW: Prevent angle-changes from slow balls
 //       float minDirectionChange01 = 0.165f;        // ~30 degrees
 //       float maxDirectionChange01 = 1.0f;

 //       float minBallSpeedAfterCut = 800.0f;

 //       // Ground-based cuts only (prevent ceiling/wall chips)
 //       float maxBallHeightAtCut = 350.0f;          // Lower = more conservative

 //       float whiffDetectionWindow = 0.25f;
 //       float outcomeEvaluationWindow = 0.60f;

 //       // Rewards refined
 //       float rewardCutSuccessfulWhiff = 12.0f;
 //       float rewardCutRegainedPossession = 9.0f;
 //       float rewardCutShotAttempt = 11.0f;         // Higher for successful shots
 //       float rewardCutBallTowardOpponentGoal = 6.0f;
 //       float rewardCutBallNeutral = 1.5f;
 //       float rewardCutBallTowardOurGoal = 0.5f;

 //   public:
 //       virtual void Reset(const GameState& initialState) override {
 //           onBallTime.clear();
 //           inActiveCut.clear();
 //           pending.clear();
 //       }

 //       // NEW HELPER: Check if ball is behind opponent's position
 //       float ComputeBehindOpponentScore(const Vec& ballVel, const Vec& ballPos,
 //           const Player& opponent) {
 //           // Opponent's facing direction
 //           Vec oppFacing = opponent.rotMat.forward;

 //           // Vector from opponent to ball
 //           Vec toBall = SafeNorm(ballPos - opponent.pos);

 //           // How much is ball going away from opponent (in opposite direction)?
 //           float oppositeFacing = -oppFacing.Dot(ballVel);
 //           float awayScore = RS_CLAMP(oppositeFacing / 1000.0f, -1.0f, 1.0f);

 //           return awayScore;
 //       }

 //       float EvaluatePostCutTrajectory(const Player& player, const GameState& state) {
 //           Vec oppGoal = (player.team == Team::BLUE) ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
 //           Vec ourGoal = (player.team == Team::BLUE) ? CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

 //           Vec toOppGoal = SafeNorm(oppGoal - state.ball.pos);
 //           Vec toOurGoal = SafeNorm(ourGoal - state.ball.pos);

 //           float towardOpp = state.ball.vel.Dot(toOppGoal);
 //           float towardOur = state.ball.vel.Dot(toOurGoal);

 //           float delta = towardOpp - towardOur;
 //           return RS_CLAMP(delta / 3000.0f, -1.0f, 1.0f);
 //       }

 //       // IMPROVED: Only count first regain attempt, not repeated touches
 //       bool HasRegainedPossessionOrShooting(const Player& player, const GameState& state,
 //           PendingCut& pc) {
 //           // Shooting at goal (primary objective)
 //           Vec oppGoal = (player.team == Team::BLUE) ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
 //           Vec toOppGoal = SafeNorm(oppGoal - state.ball.pos);
 //           float shotVelocity = state.ball.vel.Dot(toOppGoal);
 //           bool isShootingAtGoal = (shotVelocity > 1000.0f) &&
 //               RewardMath::BallIntersectsOpponentGoalMouth(player, state, 2.0f, true, true);

 //           if (isShootingAtGoal) return true;

 //           // Regained possession: ball is close AND we just touched
 //           float distToBall = (player.pos - state.ball.pos).Length();
 //           bool nearBall = (distToBall < 300.0f) && player.ballTouchedStep;

 //           // Only count if this is the first regain attempt
 //           if (nearBall && pc.regainTouchCount == 0) {
 //               pc.regainTouchCount++;
 //               return true;
 //           }

 //           return false;
 //       }

 //       virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
 //           float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
 //           uint32_t id = player.carId;

 //           // Resolve pending cuts
 //           auto pit = pending.find(id);
 //           if (pit != pending.end()) {
 //               PendingCut& pc = pit->second;

 //               // Whiff detection window
 //               if (pc.whiffCheckTimeLeft > 0.0f) {
 //                   pc.whiffCheckTimeLeft -= dt;

 //                   for (const auto& opp : state.players) {
 //                       if (opp.carId != pc.oppId) continue;

 //                       if (!pc.oppCommitted && (opp.hasJumped || opp.isFlipping)) {
 //                           pc.oppCommitted = true;
 //                       }

 //                       if (opp.ballTouchedStep) {
 //                           pc.oppWhiffed = false;
 //                       }
 //                       break;
 //                   }
 //               }

 //               pc.timeLeft -= dt;

 //               if (pc.timeLeft <= 0.0f) {
 //                   float reward = 0.0f;

 //                   if (pc.oppWhiffed && pc.oppCommitted) {
 //                       reward = rewardCutSuccessfulWhiff;
 //                   }
 //                   else if (HasRegainedPossessionOrShooting(player, state, pc)) {
 //                       reward = (pc.ballTrajectoryScore > 0.5f) ? rewardCutShotAttempt : rewardCutRegainedPossession;
 //                   }
 //                   else {
 //                       if (pc.ballTrajectoryScore > 0.5f) {
 //                           reward = rewardCutBallTowardOpponentGoal;
 //                       }
 //                       else if (pc.ballTrajectoryScore > -0.2f) {
 //                           reward = rewardCutBallNeutral;
 //                       }
 //                       else {
 //                           reward = rewardCutBallTowardOurGoal;
 //                       }
 //                   }

 //                   pending.erase(pit);
 //                   inActiveCut[id] = false;
 //                   onBallTime[id] = 0.0f;
 //                   return reward;
 //               }

 //               pc.ballTrajectoryScore = EvaluatePostCutTrajectory(player, state);
 //               return 0.0f;
 //           }

 //           // Don't re-arm during active cut outcome window
 //           if (inActiveCut[id]) return 0.0f;

 //           // Track possession
 //           float distToBall = (player.pos - state.ball.pos).Length();
 //           bool nearBall = (distToBall < maxDistToBall);

 //           auto& tOn = onBallTime[id];
 //           if (nearBall && player.isOnGround) {
 //               tOn += dt;
 //           }
 //           else if (!nearBall) {
 //               tOn = 0.0f;
 //           }

 //           if (tOn < minOnBallTime) return 0.0f;

 //           // Check challenger
 //           ChallengeInfo ci = GetChallengeInfo(player, state);
 //           if (ci.oppCarId == 0) return 0.0f;

 //           const Player* oppPtr = nullptr;
 //           for (const auto& opp : state.players) {
 //               if (opp.carId == ci.oppCarId) {
 //                   oppPtr = &opp;
 //                   break;
 //               }
 //           }

 //           if (!oppPtr) return 0.0f;

 //           bool isChallenging = (ci.oppDistToBall < challengerApproachDist) &&
 //               (ci.oppVelTowardBall > challengerVelocityThreshold);
 //           if (!isChallenging) return 0.0f;

 //           // Validate cut touch
 //           if (!player.ballTouchedStep || !state.prev) return 0.0f;

 //           const Vec prevV = state.prev->ball.vel;
 //           const Vec curV = state.ball.vel;
 //           const float prevSpd = prevV.Length();
 //           const float curSpd = curV.Length();

 //           // NEW: Previous velocity must be significant
 //           if (prevSpd < minPrevBallVelocity) return 0.0f;

 //           // Direction change validation
 //           float angleChange01 = AngleChange01(prevV, curV);
 //           if (angleChange01 < minDirectionChange01 || angleChange01 > maxDirectionChange01) {
 //               return 0.0f;
 //           }

 //           if (curSpd < minBallSpeedAfterCut) return 0.0f;

 //           // Ground-based cuts only
 //           if (state.ball.pos.z > maxBallHeightAtCut) return 0.0f;

 //           // NEW: Validate ball direction is good (behind or away from opponent)
 //           // This is the key "cut outplay" quality check
 //           float behindScore = ComputeBehindOpponentScore(curV, state.ball.pos, *oppPtr);
 //           if (behindScore < 0.0f) {
 //               // Ball going TOWARD opponent = bad cut
 //               return 0.0f;
 //           }

 //           // Arm pending cut
 //           PendingCut pc;
 //           pc.timeLeft = outcomeEvaluationWindow;
 //           pc.whiffCheckTimeLeft = whiffDetectionWindow;
 //           pc.oppId = ci.oppCarId;
 //           pc.ballPosAtCut = state.ball.pos;
 //           pc.ballVelAtCut = curV;
 //           pc.oppCommitted = false;
 //           pc.oppWhiffed = true;
 //           pc.ballTrajectoryScore = 0.0f;
 //           pc.regainTouchCount = 0;

 //           pending[id] = pc;
 //           inActiveCut[id] = true;
 //           tOn = 0.0f;

 //           return 0.0f;
 //       }
 //   };


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

    // ============================================================================
    // FlickShotReward
    // ============================================================================
    // Rewards flicks that count as shots on the opponent's net
    // Less strict than FlickOnGoalReward - focuses on offensive flicks
    // Bonus reward if opponent has low boost (vulnerable to flicks)
    // ============================================================================

    class FlickShotReward : public Reward {
    private:
        struct Config {
            // Flick detection thresholds (less strict than FlickOnGoalReward)
            float minBallSpeedIncrease = 800.0f;       // Ball must speed up by this much
            float minBallSpeedAfterFlick = 1500.0f;    // Ball must be moving this fast after flick
            float maxFlickHeight = 800.0f;             // More lenient height limit

            // Shot validation (less strict)
            float minAngleToGoal = 0.3f;               // ~73 degrees - wider angle tolerance
            float maxDistanceToGoal = 8000.0f;         // Can flick from further away

            // Rewards
            float baseFlickShotReward = 8.0f;          // Base reward for flick shot
            float lowBoostOpponentBonus = 5.0f;        // Bonus when opponent has low boost
            float lowBoostThreshold = 20.0f;           // Boost level considered "low"

            // Additional bonuses
            float speedBonus = 3.0f;                   // Bonus for high-speed flicks
            float speedBonusThreshold = 2500.0f;       // Speed required for bonus
        };

        Config config;

        /**
         * Check if opponent has low boost
         */
        bool IsOpponentLowBoost(const Player& player, const GameState& state) const {
            for (const Player& p : state.players) {
                if (p.team == player.team) continue;

                // Check if this opponent has low boost
                if (p.boost < config.lowBoostThreshold) {
                    return true;
                }
            }
            return false;
        }

        /**
         * Check if this is a valid flick shot
         */
        bool IsValidFlickShot(const Player& player, const GameState& state, float ballSpeedIncrease) const {
            if (!player.ballTouchedStep) {
                return false;
            }

            // Ball must speed up significantly
            if (ballSpeedIncrease < config.minBallSpeedIncrease) {
                return false;
            }

            // Ball must be moving fast enough after flick
            float ballSpeed = state.ball.vel.Length();
            if (ballSpeed < config.minBallSpeedAfterFlick) {
                return false;
            }

            // Ball must not be too high
            if (state.ball.pos.z > config.maxFlickHeight) {
                return false;
            }

            // Determine opponent goal
            Vec oppGoal = (player.team == Team::BLUE) ?
                Vec(0.0f, CommonValues::GOAL_LINE_Y, CommonValues::GOAL_HEIGHT * 0.5f) :
                Vec(0.0f, -CommonValues::GOAL_LINE_Y, CommonValues::GOAL_HEIGHT * 0.5f);

            // Check distance to goal
            float distToGoal = (state.ball.pos - oppGoal).Length();
            if (distToGoal > config.maxDistanceToGoal) {
                return false;
            }

            // Check angle toward goal (less strict)
            Vec dirToGoal = (oppGoal - state.ball.pos).Normalized();
            float angleToGoal = state.ball.vel.Normalized().Dot(dirToGoal);

            if (angleToGoal < config.minAngleToGoal) {
                return false;
            }

            return true;
        }

    public:
        FlickShotReward() : config() {}
        explicit FlickShotReward(const Config& cfg) : config(cfg) {}

        virtual void Reset(const GameState& initialState) override {
            // No state to reset
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            // Don't reward during kickoff
            bool isKickoff = (state.ball.pos.Length() < 300.0f && state.ball.vel.Length() < 200.0f);
            if (isKickoff) {
                return 0.0f;
            }

            // Calculate ball speed increase (need prev state)
            const Player* prevPlayer = player.prev;
            if (!prevPlayer || !prevPlayer->ballTouchedStep) {
                return 0.0f;
            }

            const GameState* prevState = state.prev;
            if (!prevState) {
                return 0.0f;
            }

            float prevBallSpeed = prevState->ball.vel.Length();
            float currBallSpeed = state.ball.vel.Length();
            float ballSpeedIncrease = currBallSpeed - prevBallSpeed;

            // Check if this is a valid flick shot
            if (!IsValidFlickShot(player, state, ballSpeedIncrease)) {
                return 0.0f;
            }

            // Base reward
            float totalReward = config.baseFlickShotReward;

            // Bonus for low opponent boost
            if (IsOpponentLowBoost(player, state)) {
                totalReward += config.lowBoostOpponentBonus;
            }

            // Speed bonus for powerful flicks
            if (currBallSpeed >= config.speedBonusThreshold) {
                float speedQuality = RS_CLAMP((currBallSpeed - config.speedBonusThreshold) / 1000.0f, 0.0f, 1.0f);
                totalReward += config.speedBonus * speedQuality;
            }

            return totalReward;
        }
    };

} // namespace RLGC
