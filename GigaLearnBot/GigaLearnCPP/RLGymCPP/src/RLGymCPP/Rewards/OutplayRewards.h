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




    /**
    * ============================================================================
    * CutOutplayReward - STATE MACHINE VERSION
    *
    * 2025-12-28 | Rev 3.0 | NJH
    *
    * Three-phase execution:
    *   PHASE 1: Detect valid cut attempt (soft possession + approaching challenger)
    *   PHASE 2: Validate cut mechanics (dramatic direction change, ball acceleration)
    *   PHASE 3: Track outcome over 0.6s (whiff, regain possession, or ball trajectory)
    *
    * Outcomes:
    *   - Opponent whiffs (commits but misses): Full reward
    *   - We regain possession or shoot: High reward
    *   - Ball toward opponent goal: Medium reward
    *   - Ball toward our goal: Low reward
    * ============================================================================
    */
    class CutOutplayReward : public Reward {
    private:
        struct PendingCut {
            float timeLeft = 0.0f;              // Outcome evaluation window (0.6s)
            float whiffCheckTimeLeft = 0.0f;    // Whiff detection window (0.25s)
            uint32_t oppId = 0;                 // Challenger car ID
            Vec ballPosAtCut;                   // Ball position at moment of cut
            Vec ballVelAtCut;                   // Ball velocity at cut moment
            bool oppCommitted = false;          // Did opponent jump/flip?
            bool oppWhiffed = false;            // Did they miss the ball?
            float ballTrajectoryScore = 0.0f;   // Goal-relative trajectory post-contact (-1 to 1)
        };

        mutable std::map<uint32_t, float> onBallTime;    // Possession tracking
        mutable std::map<uint32_t, PendingCut> pending;

        // ========================================================================
        // CONFIGURATION
        // ========================================================================

        float maxDistToBall = 500.0f;
        float challengerApproachDist = 800.0f;
        float challengerVelocityThreshold = 650.0f;
        float minOnBallTime = 0.15f;                    // Soft possession before cut

        float minDirectionChange01 = 0.165f;            // ~30 degrees
        float maxDirectionChange01 = 1.0f;              // 180 degrees
        float minBallSpeedAfterCut = 800.0f;

        float whiffDetectionWindow = 0.25f;             // 4 frames at 120Hz
        float outcomeEvaluationWindow = 0.60f;          // Total outcome window

        // ========================================================================
        // REWARD VALUES
        // ========================================================================
        float rewardCutSuccessfulWhiff = 12.0f;
        float rewardCutRegainedPossession = 10.0f;
        float rewardCutShotAttempt = 9.0f;
        float rewardCutBallTowardOpponentGoal = 7.0f;
        float rewardCutBallNeutral = 2.0f;
        float rewardCutBallTowardOurGoal = 1.5f;

        // ========================================================================
        // HELPER: Evaluate post-cut ball trajectory (goal-relative)
        // ========================================================================

        float EvaluatePostCutTrajectory(const Player& player, const GameState& state) {
            Vec oppGoal = (player.team == Team::BLUE) ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
            Vec ourGoal = (player.team == Team::BLUE) ? CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

            Vec toOppGoal = SafeNorm(oppGoal - state.ball.pos);
            Vec toOurGoal = SafeNorm(ourGoal - state.ball.pos);

            float towardOpp = state.ball.vel.Dot(toOppGoal);
            float towardOur = state.ball.vel.Dot(toOurGoal);

            float delta = towardOpp - towardOur;
            return RS_CLAMP(delta / 3000.0f, -1.0f, 1.0f);
        }

        // ========================================================================
        // HELPER: Check if we've regained possession or are shooting
        // ========================================================================

        bool HasRegainedPossessionOrShooting(const Player& player, const GameState& state, const Vec& ballPosAtCut) {
            // Regained possession: ball is close to us
            float distToBall = (player.pos - state.ball.pos).Length();
            bool nearBall = (distToBall < 300.0f);

            // Are we shooting at goal?
            Vec oppGoal = (player.team == Team::BLUE) ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
            Vec toOppGoal = SafeNorm(oppGoal - state.ball.pos);
            float shotVelocity = state.ball.vel.Dot(toOppGoal);
            bool isShootingAtGoal = (shotVelocity > 1000.0f) &&
                RewardMath::BallIntersectsOpponentGoalMouth(player, state, 2.0f, true, true);

            // Check for recent touch (last 0.2s should cover regain attempt)
            bool recentTouch = player.ballTouchedStep;

            return nearBall || isShootingAtGoal || recentTouch;
        }

    public:
        virtual void Reset(const GameState& initialState) override {
            onBallTime.clear();
            pending.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            uint32_t id = player.carId;

            // ====================================================================
            // PHASE 3: Resolve pending cut outcomes
            // ====================================================================

            auto pit = pending.find(id);
            if (pit != pending.end()) {
                PendingCut& pc = pit->second;

                // --- Whiff detection phase ---
                if (pc.whiffCheckTimeLeft > 0.0f) {
                    pc.whiffCheckTimeLeft -= dt;

                    for (const auto& opp : state.players) {
                        if (opp.carId != pc.oppId) continue;

                        // Did opponent commit?
                        if (!pc.oppCommitted && (opp.hasJumped || opp.isFlipping)) {
                            pc.oppCommitted = true;
                        }

                        // Did they touch?
                        if (opp.ballTouchedStep) {
                            pc.oppWhiffed = false;
                        }

                        break;
                    }
                }

                // --- Main outcome window ---
                pc.timeLeft -= dt;

                if (pc.timeLeft <= 0.0f) {
                    float reward = 0.0f;

                    if (pc.oppWhiffed && pc.oppCommitted) {
                        reward = rewardCutSuccessfulWhiff;
                    }
                    else if (HasRegainedPossessionOrShooting(player, state, pc.ballPosAtCut)) {
                        reward = (pc.ballTrajectoryScore > 0.5f) ? rewardCutShotAttempt : rewardCutRegainedPossession;
                    }
                    else {
                        if (pc.ballTrajectoryScore > 0.5f) {
                            reward = rewardCutBallTowardOpponentGoal;
                        }
                        else if (pc.ballTrajectoryScore > -0.2f) {
                            reward = rewardCutBallNeutral;
                        }
                        else {
                            reward = rewardCutBallTowardOurGoal;
                        }
                    }

                    pending.erase(pit);
                    onBallTime[id] = 0.0f;
                    return reward;
                }

                // Update trajectory score on contact
                if (!pc.oppWhiffed && state.prev) {
                    pc.ballTrajectoryScore = EvaluatePostCutTrajectory(player, state);
                }

                return 0.0f;
            }

            // ====================================================================
            // PHASE 1 & 2: Detect and validate cut attempt
            // ====================================================================

            // Track possession window
            float distToBall = (player.pos - state.ball.pos).Length();
            bool nearBall = (distToBall < maxDistToBall);

            auto& tOn = onBallTime[id];
            if (nearBall && player.isOnGround) {
                tOn += dt;
            }
            else if (!nearBall) {
                tOn = 0.0f;
            }

            if (tOn < minOnBallTime) return 0.0f;

            // Validate challenger
            ChallengeInfo ci = GetChallengeInfo(player, state);
            if (ci.oppCarId == 0) return 0.0f;

            // Find opponent and check positioning
            const Player* oppPtr = nullptr;
            for (const auto& opp : state.players) {
                if (opp.carId == ci.oppCarId) {
                    oppPtr = &opp;
                    break;
                }
            }

            if (!oppPtr || !IsOpponentPositionedToContest(player, *oppPtr, state)) {
                return 0.0f;
            }

            // Is challenger actually threatening?
            bool isChallenging = (ci.oppDistToBall < challengerApproachDist) &&
                (ci.oppVelTowardBall > challengerVelocityThreshold);
            if (!isChallenging) return 0.0f;

            // Validate cut touch
            if (!player.ballTouchedStep || !state.prev) return 0.0f;

            const Vec prevV = state.prev->ball.vel;
            const Vec curV = state.ball.vel;
            const float curSpd = curV.Length();

            // Direction change must be dramatic (30-180°)
            float angleChange01 = AngleChange01(prevV, curV);
            if (angleChange01 < minDirectionChange01 || angleChange01 > maxDirectionChange01) {
                return 0.0f;
            }

            // Ball must be accelerated
            if (curSpd < minBallSpeedAfterCut) {
                return 0.0f;
            }

            // Ground-based cuts (not high pops)
            if (state.ball.pos.z > 400.0f) {
                return 0.0f;
            }

            // ====================================================================
            // ARM PENDING CUT
            // ====================================================================

            PendingCut pc;
            pc.timeLeft = outcomeEvaluationWindow;
            pc.whiffCheckTimeLeft = whiffDetectionWindow;
            pc.oppId = ci.oppCarId;
            pc.ballPosAtCut = state.ball.pos;
            pc.ballVelAtCut = curV;
            pc.oppCommitted = false;
            pc.oppWhiffed = true;
            pc.ballTrajectoryScore = 0.0f;

            pending[id] = pc;
            tOn = 0.0f;

            return 0.0f;
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
