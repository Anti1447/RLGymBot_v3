// AdvancedRewards.h
// Advanced reward functions for high-level Rocket League play
// Place in: GigaLearnCPP/GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/

#pragma once
#include "Reward.h"
#include "../Math.h"
#include "CommonRewards.h"
#include "BallPrediction.h"

namespace RLGC {

    // ============================================================================
    // BALL CONTROL REWARDS
    // ============================================================================
	// TODO: Fix this reward... it needs work.
    // Liu Distance Reward - rewards being in optimal position relative to ball
    class LiuDistanceReward : public Reward {
    public:
        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            // Liu distance: optimal positioning between ball and goal
            Vec targetGoal = (player.team == Team::BLUE) ?
                CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

            Vec ballToGoal = targetGoal - state.ball.pos;
            float distBallToGoal = ballToGoal.Length();

            if (distBallToGoal < 1e-5f) return 0.0f;

            Vec optimalPos = state.ball.pos - ballToGoal.Normalized() * 1500.0f; // 1500uu behind ball
            float distToOptimal = (player.pos - optimalPos).Length();

            // Reward decreases with distance from optimal position
            float reward = 1.0f / (1.0f + distToOptimal / 1000.0f);
            return reward;
        }
    };

    // Liu-style distance reward for ball relative to opponent goal.
    // Reward is higher when ball is closer to opponent's net.
    // 2025-12-08 - Rev 1 (NJH)
    class LiuDistanceBallToGoalReward : public Reward {
    public:
        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {

            Vec targetGoal = (player.team == Team::BLUE)
                ? CommonValues::ORANGE_GOAL_BACK
                : CommonValues::BLUE_GOAL_BACK;

            Vec ballToGoal = targetGoal - state.ball.pos;
            float dist = ballToGoal.Length();

            if (dist < 1e-5f)
                return 1.0f;

            // Same falloff profile as original Liu reward:
            //   Reward = 1 / (1 + dist / scale)
            // A scale of 5000uu gives a very smooth approach-to-goal shaping.
            return 1.0f / (1.0f + dist / 5000.0f);
        }
    };


    // ============================================================================
    // LiuPlayerBallDistanceReward
    // Rewards being closer to the ball (simple inverse-distance form).
    // 2025-12-07 - Rev 1 - Added player-ball distance reward. (NJH)
    // ============================================================================
    class LiuPlayerBallDistanceReward : public Reward {
    public:
        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            Vec diff = player.pos - state.ball.pos;
            float dist = diff.Length();
    
            if (dist < 1e-5f)
                return 1.0f; // practically "on" the ball
    
            // Similar form to LiuDistanceReward: smooth inverse distance
            return 1.0f / (1.0f + dist / 1000.0f);
        }
    };


    // ============================================================================
    // AirTouchReward (REWORKED)
    // Strong aerial power touch reward:
    //  - In the air
    //  - Ball high (>= minHeight, default 350uu)
    //  - Not near side walls (ignore wall hits)
    //  - Uses StrongTouchReward as base
    //  - Scaled by height and relative speed between player & ball
    // 2025-12-07 - Rev 2 - Replaced simple height-based version with
    //                      strong-air-touch variant. (NJH)
    // ============================================================================
    class AirTouchReward : public Reward {
    private:
        float minHeight;
        StrongTouchReward strongTouch; // reuse hitForce logic (ball vel delta)

    public:
        explicit AirTouchReward(float minHeightIn = 150.0f)
            : minHeight(minHeightIn),
            strongTouch(15.0f, 130.0f) // same defaults as StrongTouchReward
        {
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            // Must be a ball touch and player must be airborne
            if (!player.ballTouchedStep)
                return 0.0f;
            if (player.isOnGround)
                return 0.0f;

            // Height requirement
            float height = state.ball.pos.z;
            if (height < minHeight)
                return 0.0f;

            // Ignore side-wall aerials: treat those as wall hits instead
            float distToSideWall = CommonValues::SIDE_WALL_X - fabsf(state.ball.pos.x);
            if (distToSideWall <= 100.0f)
                return 0.0f;

            // Base "power" from StrongTouchReward (change in ball velocity)
            float strongBase = strongTouch.GetReward(player, state, isFinal);
            if (strongBase <= 0.0f)
                return 0.0f;

            // Relative speed between player and ball at touch
            Vec relVel = state.ball.vel - player.vel;
            float relSpeed = relVel.Length();

            float relNorm = 0.0f;
            if (CommonValues::CAR_MAX_SPEED > 1e-5f) {
                // Scale relative speed into [0,1], higher = more "power"
                relNorm = RS_MIN(1.0f, relSpeed / CommonValues::CAR_MAX_SPEED);
            }

            // Height factor: extra reward for higher aerials
            float heightRatio = RS_MIN(1.0f, (height - minHeight) / 1000.0f);

            // Combine:
            //  - strongBase in [0,1] from StrongTouchReward
            //  - (0.5..1.0) from height
            //  - (0.3..1.0) from rel speed to avoid totally killing soft touches,
            //    but still strongly prefer high relative speed
            float heightFactor = 0.5f + 0.5f * heightRatio;      // [0.5,1.0]
            float relSpeedFactor = 0.3f + 0.7f * relNorm;        // [0.3,1.0]

            float reward = strongBase * heightFactor * relSpeedFactor;

            // Keep bounded. strongBase ≤ 1, factors ≤ 1 → reward ≤ 1
            return reward;
        }
    };

    // ============================================================================
    // AerialFirstTouchReward
    // 2025-12-16 | Rev 1.0 | NJH
    // - Track "aerial phase" when ball.z > threshold (not kickoff).
    // - First touch during aerial phase gets paid once when aerial ends.
    // - Reward scales with height + how quickly you touched.
    //
    // Rev 2.0 (2026-02-18, NJH)
    // - Fix: moved aerial timer + first-touch detection into PreStep() so they
    //   run ONCE per step rather than once per player per step (fixes 2-6x
    //   time inflation in 2v2/3v3).
    // - Fix: raised height cap 1200 -> 1600 for better gradient across typical
    //   aerial heights (600-1500 uu).
    // ============================================================================
    class AerialFirstTouchReward : public Reward {
    private:
        // Aerial session state — updated in PreStep, read in GetReward
        bool     aerialActive       = false;
        float    aerialTime         = 0.0f;
        bool     firstTouchRecorded = false;
        uint32_t firstTouchCarId    = 0;
        float    firstTouchTime     = 0.0f;
        float    firstTouchHeight   = 0.0f;
        std::unordered_map<uint32_t, bool> paidThisAerial;
        // Track which players were close enough at aerial session start to be eligible.
        // Players > maxAerialLaunchDist UU from the ball when the aerial begins are excluded.
        std::unordered_map<uint32_t, bool> eligibleThisAerial;

        // Tunables
        float aerialZ              = 400.0f;   // ball must be above this to count as "aerial" (raised from 250: filters casual bounces)
        float minPayoutHeight      = 500.0f;   // first touch must occur above this height to pay out
        float kickoffPos           = 200.0f;
        float kickoffVel           = 150.0f;
        float maxAerialTrackSec    = 5.0f;     // don't keep state forever
        float timeScoreWindow      = 1.8f;     // earlier than this = full time score
        float minReward            = 0.6f;
        float maxReward            = 2.2f;
        float maxAerialLaunchDist  = 3500.0f;  // UU — car must be within this at session start

    public:
        virtual void Reset(const GameState& initialState) override {
            aerialActive       = false;
            aerialTime         = 0.0f;
            firstTouchRecorded = false;
            firstTouchCarId    = 0;
            firstTouchTime     = 0.0f;
            firstTouchHeight   = 0.0f;
            paidThisAerial.clear();
            eligibleThisAerial.clear();
        }

        // All per-step state updates live here so they run once per step,
        // not once per player per step.
        virtual void PreStep(const GameState& state) override {
            float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

            bool isKickoff   = (state.ball.pos.Length() < kickoffPos && state.ball.vel.Length() < kickoffVel);
            bool isAerialNow = (!isKickoff && state.ball.pos.z > aerialZ);

            // Aerial START
            if (isAerialNow && !aerialActive) {
                aerialActive       = true;
                aerialTime         = 0.0f;
                firstTouchRecorded = false;
                firstTouchCarId    = 0;
                firstTouchTime     = 0.0f;
                firstTouchHeight   = 0.0f;
                paidThisAerial.clear();
                // Record eligibility at session start based on car-ball distance.
                // Players beyond maxAerialLaunchDist will not be rewarded for this session.
                eligibleThisAerial.clear();
                for (const auto& p : state.players) {
                    float dist = (p.pos - state.ball.pos).Length();
                    eligibleThisAerial[p.carId] = (dist <= maxAerialLaunchDist);
                }
            }

            if (aerialActive) {
                aerialTime += dt;  // ticks exactly once per step

                // Detect first AERIAL touch: player must be airborne via jump (not grounded).
                // hasJumped guards against cars that were launched without actively jumping.
                if (!firstTouchRecorded) {
                    for (const auto& p : state.players) {
                        if (p.ballTouchedStep && state.ball.pos.z > aerialZ
                            && !p.isOnGround && p.hasJumped) {
                            firstTouchRecorded = true;
                            firstTouchCarId    = p.carId;
                            firstTouchTime     = aerialTime;
                            firstTouchHeight   = state.ball.pos.z;
                            break;
                        }
                    }
                }

                // End conditions
                if (!isAerialNow || aerialTime >= maxAerialTrackSec) {
                    aerialActive = false;
                }
            }
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            // Payout fires on the same step PreStep sets aerialActive = false.
            if (aerialActive) return 0.0f;
            if (!firstTouchRecorded) return 0.0f;
            if (firstTouchHeight < minPayoutHeight) return 0.0f;

            auto pit = paidThisAerial.find(player.carId);
            if (pit != paidThisAerial.end() && pit->second) return 0.0f;

            // Only reward players who were within maxAerialLaunchDist of the ball
            // when the aerial session started. Skip players who committed from far away.
            auto eit = eligibleThisAerial.find(player.carId);
            if (eit == eligibleThisAerial.end() || !eit->second) return 0.0f;

            paidThisAerial[player.carId] = true;

            if (player.carId != firstTouchCarId) return 0.0f;

            // Height score — cap at 2000 UU (near field ceiling ~2044) for full gradient
            float hFrac = RS_CLAMP((firstTouchHeight - aerialZ) / (2000.0f - aerialZ), 0.0f, 1.0f);

            // Time score — earlier touch = higher score
            float tFrac = RS_CLAMP(1.0f - (firstTouchTime / timeScoreWindow), 0.0f, 1.0f);

            float frac = 0.55f * hFrac + 0.45f * tFrac;
            return minReward + (maxReward - minReward) * frac;
        }
    };


    // ============================================================================
// JumpTouchReward (REWORKED)
// "Bridge" reward between ground jump touches and full aerial touches.
//  - Requires airborne ball touch at low/mid heights
//  - Uses a mild StrongTouchReward component so stronger hits pay more
//  - Height band overlaps with AirTouchReward's region
// 2025-12-09 - Rev 2 - Added overlap with AirTouch and power scaling. (NJH)
// ============================================================================
    class JumpTouchReward : public Reward {
    private:
        float minCarZ = 50.0f;   // slightly above ground
        float maxCarZ = 450.0f;  // beyond this, treat as "true aerial"
        float maxBallZForJump = 700.0f; // above this, AirTouchReward should handle it

        StrongTouchReward strongTouch;  // reuse power-based touch metric

    public:
        JumpTouchReward()
            : strongTouch(10.0f, 80.0f)  // softer than AirTouch's 15/130
        {
        }

        virtual float GetReward(const Player& player,
            const GameState& state,
            bool isFinal) override {
            if (!player.ballTouchedStep)
                return 0.0f;
            if (player.isOnGround)
                return 0.0f;

            float carZ = player.pos.z;
            float ballZ = state.ball.pos.z;

            // Filter to "jump height" region, but allow overlap up into low aerials
            if (carZ < minCarZ || carZ > maxCarZ)
                return 0.0f;

            // If the ball is very high, let AirTouchReward handle it
            if (ballZ > maxBallZForJump)
                return 0.0f;

            // Height-based scaling: low jumps give less, higher jumps (up to ~350)
            // give more and overlap with the lower end of AirTouch's region.
            float heightNorm = RS_CLAMP((carZ - minCarZ) / (350.0f - minCarZ), 0.0f, 1.0f);

            // Touch "power" using StrongTouchReward (0..1)
            float power = strongTouch.GetReward(player, state, isFinal);
            power = RS_CLAMP(power, 0.0f, 1.0f);

            // Base reward: small at low jumps, larger at nice jump height
            float base = 0.4f + 0.6f * heightNorm;     // [0.4,1.0]

            // Power factor: weak touches still get something, strong hits get more
            float powerFactor = 0.3f + 0.7f * power;   // [0.3,1.0]

            return base * powerFactor; // up to ~1.0
        }
    };

    // ============================================================================
// WallTouchReward (REWORKED)
// Reward strong, meaningful wall touches:
//  - Requires ball touch
//  - Car + ball must be near a side/back wall
//  - Ignores low, ground-level contacts
//  - Scales with wall proximity, ball height, and hit power
// 2025-12-09 - Rev 2 - Replaced simple proximity-only version with
//                      power + height + wall-distance shaping. (NJH)
// ============================================================================
    class WallTouchReward : public Reward {
    private:
        float wallDistanceThreshold = 350.0f;   // how close to count as "on wall"
        float minBallHeight = 120.0f;   // ignore ground-level contacts
        float maxBallHeight = 1800.0f;  // clamp scaling at ceiling-ish

        StrongTouchReward strongTouch;          // reuse ball-vel delta power

    public:
        WallTouchReward()
            : strongTouch(15.0f, 130.0f)        // similar scale to AirTouchReward
        {
        }

        virtual float GetReward(const Player& player,
            const GameState& state,
            bool isFinal) override {
            // Must touch ball this step
            if (!player.ballTouchedStep)
                return 0.0f;

            // Use ball position to define wall context (where the play is happening)
            const Vec& ballPos = state.ball.pos;
            float ballZ = ballPos.z;

            // Ignore very low touches; treat those as ground plays
            if (ballZ < minBallHeight)
                return 0.0f;

            // Distance to side walls (±X)
            float distToSide = CommonValues::SIDE_WALL_X - fabsf(ballPos.x);
            // Distance to back walls (±Y)
            float distToBack = CommonValues::BACK_WALL_Y - fabsf(ballPos.y);

            float minWallDist = RS_MIN(distToSide, distToBack);

            // If ball isn't near any wall, this isn't a wall touch
            if (minWallDist > wallDistanceThreshold)
                return 0.0f;

            // Base "power" from StrongTouchReward, in [0,1]
            float power = strongTouch.GetReward(player, state, isFinal);
            if (power <= 0.0f)
                return 0.0f;

            // Wall proximity factor: closer to wall → more reward
            float wallProximity = 1.0f - RS_CLAMP(minWallDist / wallDistanceThreshold, 0.0f, 1.0f);
            // Avoid tiny numerical noise
            if (wallProximity <= 0.0f)
                return 0.0f;

            // Height factor: higher ball → more "true" wall play
            float clampedZ = RS_CLAMP(ballZ, minBallHeight, maxBallHeight);
            float heightNorm = (clampedZ - minBallHeight) / (maxBallHeight - minBallHeight); // 0..1

            // Shape factors:
            //  - wallFactor in [0.4, 1.0]
            //  - heightFactor in [0.5, 1.0]
            float wallFactor = 0.4f + 0.6f * wallProximity;
            float heightFactor = 0.5f + 0.5f * heightNorm;

            float reward = power * wallFactor * heightFactor;

            // Bound stays ≤ 1.0 since all pieces are ≤ 1.0
            return reward;
        }
    };

    // ============================================================================
    // HighBallTouchReward
    // Simple linear reward for touching the ball higher in the air.
    //  - Requires ballTouchedStep
    //  - Scales linearly from minHeight (0 reward) to maxHeight (full reward)
    // 2025-12-09 | Rev 1.0 | NJH
    // ============================================================================
    class HighBallTouchReward : public Reward {
    private:
        float minHeight;  // height where reward = 0
        float maxHeight;  // height where reward = 1

    public:
        HighBallTouchReward(float minH = (CommonValues::BALL_RADIUS+400.0f),
            float maxH = CommonValues::CEILING_Z - 200.0f)
            : minHeight(minH), maxHeight(maxH) {
        }

        virtual float GetReward(const Player& player,
            const GameState& state,
            bool isFinal) override
        {
            if (!player.ballTouchedStep)
                return 0.0f;

            float z = state.ball.pos.z;

            if (z <= minHeight)
                return 0.0f;
            if (z >= maxHeight)
                return 1.0f;

            float t = (z - minHeight) / (maxHeight - minHeight); // linear 0–1
            return t;
        }
    };

    // ============================================================================
    // UpwardBoostReward
    // Reward boosting while the car's nose is pointed upward.
    //  - Checks player.prevAction.boost
    //  - Uses linear upwardPitch = clamp(pitch, 0..1)
    // 2025-12-09 | Rev 1.0 | NJH
    // ============================================================================
    class UpwardBoostReward : public Reward {
    private:
        float minPitch;   // pitch angle to start rewarding (nose slightly up)
        float maxPitch;   // pitch angle for full reward (nose strongly up)

    public:
        // pitch values are assumed in [-1,1] where -1 = nose up, +1 = nose down
        // Many RL envs define pitch as: -1 = forward/up, +1 = backward/down.
        // Adjust if your pitch convention differs.
        UpwardBoostReward(float minP = -0.2f, float maxP = -0.9f)
            : minPitch(minP), maxPitch(maxP) {
        }

        virtual float GetReward(const Player& player,
            const GameState& state,
            bool isFinal) override
        {
            // Must be boosting
            if (player.prevAction.boost < 0.5f)
                return 0.0f;

            float p = player.prevAction.pitch;  // [-1..1]

            // If nose is not pitched upward enough, no reward
            if (p > minPitch)
                return 0.0f;

            // Clamp pitch between minPitch and maxPitch
            float clamped = p;
            if (clamped < maxPitch) clamped = maxPitch;  // cap strong-upward
            if (clamped > minPitch) clamped = minPitch;  // cap weak-upward

            // Convert pitch to linear 0..1:
            //   p = minPitch  -> 0
            //   p = maxPitch  -> 1
            float t = (minPitch - clamped) / (minPitch - maxPitch); // linear 0–1

            return t;  // 0 to 1
        }
    };


    // ============================================================================
    // TouchHeightWallScalarReward
    // "Open-space aerial scalar" for ball touches.
    //  - Requires ballTouchedStep.
    //  - Returns a scalar in [0,1] combining:
    //      * Quadratic height from ground -> near ceiling
    //      * Quadratic distance from nearest wall
    //  - Hits near ground OR near walls are weaker.
    //  - High, mid-field aerial touches are strongest.
    // 2025-12-09 | Rev 1.0 | NJH
    // ============================================================================
    class TouchHeightWallScalarReward : public Reward {
    private:
        float minZ;
        float maxZ;
        float maxWallDist;

    public:
        TouchHeightWallScalarReward(
            float minZIn = CommonValues::BALL_RADIUS,
            float maxZIn = CommonValues::CEILING_Z - 200.0f,
            float maxWallDistIn = 2000.0f
        )
            : minZ(minZIn),
            maxZ(maxZIn),
            maxWallDist(maxWallDistIn)
        {
        }

        virtual float GetReward(const Player& player,
            const GameState& state,
            bool isFinal) override {
            if (!player.ballTouchedStep)
                return 0.0f;

            const Vec& bp = state.ball.pos;

            // --- Height quadratic: ground -> ceiling ---
            float z = bp.z;
            float heightScalar = 0.0f;
            if (z <= minZ) {
                heightScalar = 0.0f;
            }
            else if (z >= maxZ) {
                heightScalar = 1.0f;
            }
            else {
                float t = (z - minZ) / (maxZ - minZ);  // 0..1
                heightScalar = t * t;                 // quadratic
            }

            // --- Wall-distance quadratic: wall -> center ---
            float distToSide = CommonValues::SIDE_WALL_X - fabsf(bp.x);
            float distToBack = CommonValues::BACK_WALL_Y - fabsf(bp.y);
            float distToWall = RS_MAX(0.0f, RS_MIN(distToSide, distToBack));

            float wallScalar = 0.0f;
            if (distToWall >= maxWallDist) {
                wallScalar = 1.0f;
            }
            else {
                float u = distToWall / maxWallDist;   // 0 at wall, 1 at "far enough"
                wallScalar = u * u;                  // ramps quickly off the wall
            }

            // Combine:
            //  - if either is low, result is low,
            //  - maximum is 1 when high & far from walls.
            float combined = heightScalar * wallScalar;

            return combined; // scalar in [0,1]
        }
    };




    // ============================================================================
    // DribbleMasteryReward (UPDATED)
    // - Only counts dribbles when ball is near arena ground (absolute Z)
    // - If dribble is NOT moving toward opponent's net, reduce reward by half
    // ============================================================================
    class DribbleMasteryReward : public Reward {
    private:
        // Geometry / thresholds
        float maxDribbleDistanceXY = 105.0f;  // horizontal radius around car
        float maxLateralOffset = 100.0f;  // sideways tolerance (unused here but available)
        float minBallHeightAboveCar = 40.0f;   // above car roof
        float maxBallHeightAboveCar = 240.0f;  // can count pops relative to car
        float minForwardDot = 0.15f;    // ball must be somewhat in front
        float minSpeed = 800.0f;  // must be moving

        // NEW: absolute arena-ground threshold (only count dribbles when ball is low)
        float groundMaxZ = 600.0f;   // ball.z must be <= this to count as a ground dribble

        float sustainRefSec = 2.f;    // ~1s "full" dribble reference
        float minActiveTime = 0.40f;   // < this: almost no shaping
        float entryBonus = 0.1f;   // smaller one-off bonus at start
        float rewardScale = 1.0f;    // global multiplier

        // NEW: opponent proximity penalty for risky dribbles
        float opponentDangerRange = 600.0f;   // penalty zone for dribbling near opponents
        float maxOpponentPenalty = 2.0f;      // max penalty amount (reduces reward by 50%)

        mutable std::map<uint32_t, float> sustainedTime;
        mutable std::map<uint32_t, bool>  wasOnCar;

    public:
        virtual void Reset(const GameState& initialState) override {
            sustainedTime.clear();
            wasOnCar.clear();
        }

        virtual float GetReward(const Player& player,
            const GameState& state,
            bool isFinal) override {
            uint32_t id = player.carId;
            float    dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

            // Only ground dribbles: ball must be near arena ground
            if (state.ball.pos.z > groundMaxZ) {
                sustainedTime.erase(id);
                wasOnCar[id] = false;
                return 0.0f;
            }

            // If not on ground, we keep it simple: end dribble streak.
            if (!player.isOnGround) {
                sustainedTime.erase(id);
                wasOnCar[id] = false;
                return 0.0f;
            }

            Vec diff = state.ball.pos - player.pos;
            Vec diffXY = diff;
            diffXY.z = 0.0f;

            float distXY = diffXY.Length();
            float height = diff.z;
            float speed = player.vel.Length();

            // Geometric on-car zone checks (still relative to car)
            if (distXY > maxDribbleDistanceXY ||
                height < minBallHeightAboveCar ||
                height > maxBallHeightAboveCar) {
                sustainedTime.erase(id);
                wasOnCar[id] = false;
                return 0.0f;
            }

            float forwardDot = player.rotMat.forward.Dot(diff.Normalized());
            if (forwardDot < minForwardDot) {
                sustainedTime.erase(id);
                wasOnCar[id] = false;
                return 0.0f;
            }

            if (speed < minSpeed) {
                sustainedTime.erase(id);
                wasOnCar[id] = false;
                return 0.0f;
            }

            // We are in a valid "ball on car" state
            bool  nowOnCar = true;
            float reward = 0.0f;

            // Partial 1: entering the dribble zone (small, one-time bump)
            if (nowOnCar && !wasOnCar[id]) {
                reward += entryBonus;
                sustainedTime[id] = 0.0f;
            }
            wasOnCar[id] = nowOnCar;

            // Accumulate dribble time
            sustainedTime[id] += dt;
            float t = sustainedTime[id];

            // If the dribble is extremely short, don't give much shaping yet.
            if (t < minActiveTime) {
                // Just the tiny entry bonus above; nothing more this step.
                return reward * rewardScale;
            }

            // Reuse the geometric shaping from the old version
            float proximity = 1.0f - RS_MIN(1.0f, distXY / maxDribbleDistanceXY);

            float forwardFactor = (forwardDot - minForwardDot) / (1.0f - minForwardDot);
            forwardFactor = RS_MAX(0.0f, RS_MIN(1.0f, forwardFactor));

            // Speed bell-curve: peaks at 1.0x in the optimal dribble range [1300, 1600] UU/s,
            // falls to 0.5x at both extremes (0 UU/s stationary, 2300 UU/s supersonic).
            // Previously used linear speedNorm (faster = more reward) which drove supersonic dribbles.
            static constexpr float DRIB_OPT_LOW  = 1300.0f;
            static constexpr float DRIB_OPT_HIGH = 1600.0f;
            float speedMult;
            if (speed >= DRIB_OPT_LOW && speed <= DRIB_OPT_HIGH) {
                speedMult = 1.0f;
            } else if (speed < DRIB_OPT_LOW) {
                speedMult = 0.5f + 0.5f * (speed / DRIB_OPT_LOW);
            } else {
                float excess = speed - DRIB_OPT_HIGH;
                float range  = CommonValues::CAR_MAX_SPEED - DRIB_OPT_HIGH;
                speedMult = 0.5f + 0.5f * (1.0f - RS_CLAMP(excess / range, 0.0f, 1.0f));
            }
            speedMult = RS_CLAMP(speedMult, 0.5f, 1.0f);

            float base = proximity * forwardFactor * speedMult;  // 0..1

            // Duration emphasis:
            //  - t/sustainRefSec in [0, ~2] for up to 2 seconds
            //  - square it so 1.0s >> 0.5s and 2.0s is much larger still
            float sustainNorm = RS_MIN(2.0f, t / sustainRefSec); // 0..2
            float durationFactor = sustainNorm * sustainNorm;       // 0..4

            // Base contribution is small unless durationFactor grows
            float baseContribution = base * 0.25f * durationFactor; // up to ~1.0

            // Long-dribble bonuses: scaled by dt so they don't explode GAE at once
            float longDribbleBoostPerSec = 0.0f;
            if (t > 2.0f) longDribbleBoostPerSec = 3.0f;
            else if (t > 1.0f) longDribbleBoostPerSec = 1.5f;
            else if (t > 0.6f) longDribbleBoostPerSec = 0.7f;

            float longDribbleBoost = longDribbleBoostPerSec * dt;

            reward += baseContribution + longDribbleBoost;

            // --- Directional scaling: prefer dribbles that move toward opponent net
            Vec opponentGoal = (player.team == Team::BLUE) ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
            Vec toOpp = opponentGoal - state.ball.pos;
            float toOppLen = toOpp.Length();

            float towardOpp = 0.0f;
            float ballSpeed = state.ball.vel.Length();
            if (toOppLen > 1e-5f) {
                Vec toOppN = toOpp / toOppLen;
                if (ballSpeed > 1e-3f) {
                    towardOpp = state.ball.vel.Normalized().Dot(toOppN);
                }
                else {
                    // If ball is nearly static, use player facing as proxy
                    towardOpp = player.rotMat.forward.Dot(toOppN);
                }
            }

            // If not sufficiently toward opponent (small threshold), decrease by half
            float dirMultiplier = (towardOpp > 0.1f) ? 1.0f : 0.5f;

            reward *= dirMultiplier;

            // --- NEW: Opponent proximity penalty for risky dribbles ---
            // Penalize dribbling when opponents are close (< 500 UU away)
            // This discourages risky dribbling under pressure
            float closestOpponentDist = 999999.0f;
            for (const auto& opponent : state.players) {
                if (opponent.team == player.team) continue;  // Ignore teammates

                float distToOpp = (opponent.pos - player.pos).Length();
                closestOpponentDist = RS_MIN(closestOpponentDist, distToOpp);
            }

            // Apply penalty if opponent is within danger range
            if (closestOpponentDist < opponentDangerRange) {
                // Graduated penalty: closer opponent = harsher penalty
                // At 0 UU: full penalty (50% reduction)
                // At 500 UU: no penalty (0% reduction)
                float opponentProximityFactor = closestOpponentDist / opponentDangerRange;  // 0..1
                float opponentPenalty = maxOpponentPenalty * (1.0f - opponentProximityFactor);  // 0..0.5

                reward *= (1.0f - opponentPenalty);  // Reduce reward by penalty amount
            }

            return reward * rewardScale;
        }
    };


    
    // ============================================================================

    // ============================================================================
    // Rev 2 - 2025-12-15 (NJH): Make DribbleReward detect true "carry on top"
    //   - Require ball over roof area (small forward + lateral offsets in car-local frame)
    //   - Remove "ball in front" gating (that rewards pushing)
    //   - Add relative-velocity similarity (carrying vs bouncing/pushing)
    //   - Add upper height band to avoid rewarding pop-ups
    // ============================================================================

    class DribbleReward : public Reward {
    private:
        // --- "On top of car" geometry (tighter than before) ---
        float maxForwardOffset = 150.0f;       // ball center not too far in front of car center
        float minForwardOffset = -110.0f;       // allow slightly behind center (still on roof)
        float maxLateralOffset = 120.0f;       // ball should be near the center line
        float maxDistanceXY = 130.0f;          // overall horizontal radius (tight)

        // height band relative to car position
        float minBallHeightAboveCar = 55.0f;   // ball must be above roof-ish
        float maxBallHeightAboveCar = 220.0f;  // too high = juggling/pop, not dribble carry

        // carrying tends to have similar velocities
        float maxRelativeSpeed = 800.0f;       // |ballVel - carVel| must be under this
        float minCarSpeed = 500.0f;            // still require some movement

        float requiredSustainSec = 0.25f;
        float rewardScale = 1.0f;

        const float NO_PREV_SHAPE = 0.02f;

        mutable std::map<uint32_t, float> sustainedTime;

    public:
        explicit DribbleReward(
            float maxFwd = 150.0f,
            float minFwd = -110.0f,
            float maxLat = 120.0f,
            float maxXY = 130.0f,
            float minH = 55.0f,
            float maxH = 220.0f,
            float relSpd = 800.0f,
            float minSpd = 500.0f,
            float sustain = 0.25f,
            float scale = 1.0f)
            : maxForwardOffset(maxFwd)
            , minForwardOffset(minFwd)
            , maxLateralOffset(maxLat)
            , maxDistanceXY(maxXY)
            , minBallHeightAboveCar(minH)
            , maxBallHeightAboveCar(maxH)
            , maxRelativeSpeed(relSpd)
            , minCarSpeed(minSpd)
            , requiredSustainSec(sustain)
            , rewardScale(scale)
        {
        }

        virtual void Reset(const GameState& initialState) override {
            sustainedTime.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            uint32_t id = player.carId;
            float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

            if (!player.isOnGround) {
                sustainedTime.erase(id);
                return 0.0f;
            }

            // Ball relative position in world
            Vec diff = state.ball.pos - player.pos;

            // Height band: must be above car but not too high
            float dz = diff.z;
            if (dz < minBallHeightAboveCar || dz > maxBallHeightAboveCar) {
                sustainedTime.erase(id);
                return 0.0f;
            }

            // Horizontal distance tight
            Vec diffXY = diff; diffXY.z = 0.0f;
            float distXY = diffXY.Length();
            if (distXY > maxDistanceXY) {
                sustainedTime.erase(id);
                return 0.0f;
            }

            // Car-local offsets (this is the key)
            float forwardOffset = player.rotMat.forward.Dot(diff);
            float lateralOffset = player.rotMat.right.Dot(diff);

            // If ball is too far forward -> likely pushing, not carrying
            if (forwardOffset > maxForwardOffset || forwardOffset < minForwardOffset) {
                sustainedTime.erase(id);
                return 0.0f;
            }

            if (fabs(lateralOffset) > maxLateralOffset) {
                sustainedTime.erase(id);
                return 0.0f;
            }

            // Movement check
            float carSpeed = player.vel.Length();
            if (carSpeed < minCarSpeed) {
                sustainedTime.erase(id);
                return 0.0f;
            }

            // Carry check: ball and car velocities should be somewhat similar
            float relSpeed = (state.ball.vel - player.vel).Length();
            if (relSpeed > maxRelativeSpeed) {
                sustainedTime.erase(id);
                return 0.0f;
            }

            // If no previous state, give tiny shaping to get learning signal started
            if (!state.prev) {
                sustainedTime[id] += dt;
                return NO_PREV_SHAPE * rewardScale;
            }

            // Sustain accumulation
            sustainedTime[id] += dt;
            float sustain01 = RS_MIN(1.0f, sustainedTime[id] / requiredSustainSec);

            // Shaping: prefer centered ball + velocity match + slight forward (roof/hood)
            float centeredLat = 1.0f - RS_MIN(1.0f, fabs(lateralOffset) / maxLateralOffset);
            float centeredFwd = 1.0f;

            // Encourage ball near the roof center / slightly forward, but not "in front"
            // Map forwardOffset into a peak around ~40uu forward.
            float peak = 40.0f;
            float span = (maxForwardOffset - minForwardOffset) * 0.5f;
            float fwdErr = fabs(forwardOffset - peak);
            centeredFwd = 1.0f - RS_MIN(1.0f, fwdErr / RS_MAX(1.0f, span));

            float prox = 1.0f - RS_MIN(1.0f, distXY / maxDistanceXY);

            float rel01 = 1.0f - RS_MIN(1.0f, relSpeed / maxRelativeSpeed);

            // Speed bell-curve: peaks at 1.0x in [1300, 1600] UU/s, falls to 0.5x at both
            // extremes (0 stationary, 2300 supersonic). Replaces linear speed01 which
            // previously incentivized dribbling at max speed.
            static constexpr float DRIB_OPT_LOW  = 1300.0f;
            static constexpr float DRIB_OPT_HIGH = 1600.0f;
            float speedMult;
            if (carSpeed >= DRIB_OPT_LOW && carSpeed <= DRIB_OPT_HIGH) {
                speedMult = 1.0f;
            } else if (carSpeed < DRIB_OPT_LOW) {
                speedMult = 0.5f + 0.5f * (carSpeed / DRIB_OPT_LOW);
            } else {
                float excess = carSpeed - DRIB_OPT_HIGH;
                float range  = CommonValues::CAR_MAX_SPEED - DRIB_OPT_HIGH;
                speedMult = 0.5f + 0.5f * (1.0f - RS_CLAMP(excess / range, 0.0f, 1.0f));
            }
            speedMult = RS_CLAMP(speedMult, 0.5f, 1.0f);

            // Combine. Gate with sustain so you don't get instant payout.
            float base = prox * centeredLat * centeredFwd * rel01;
            float reward = base * (0.2f + 0.8f * sustain01) * speedMult;

            // Tiny floor for very short holds
            if (sustainedTime[id] < 0.06f) {
                reward *= (sustainedTime[id] / 0.06f);
            }

            return RS_CLAMP(reward * rewardScale, 0.0f, 4.0f);
        }
    };

    // ============================================================================
// DribbleSetupReward - ELITE TIER (Rev 2.0)
// Rewards executing dribble catch mechanics: rolling ball, matching speed,
// cutting at DIAGONAL angle, popping ball up (+50z), then catching
// 2026-01-04 | Rev 2.0 | NJH
// ============================================================================

    class DribbleSetupReward : public Reward {
    private:
        struct DribbleConfig {
            // Slow ball handling: reward small hits to get ball rolling
            float slowBallThreshold = 500.0f;          // Ball moving slower than this
            float slowBallHitReward = 0.6f;            // Reward for hitting slow ball
            float slowBallSpeedGainBonus = 0.3f;       // Bonus if ball speeds up after hit
            float minSpeedGainForBonus = 200.0f;       // Minimum speed increase to get bonus

            // Rolling phase: ball should move slowly and predictably
            float maxBallRollSpeed = 1200.0f;
            float minBallRollSpeed = 600.0f;

            // Matching phase: car should match ball speed closely
            float maxSpeedDelta = 1000.0f;
            float minSpeedDelta = 100.0f;

            // CUTTING phase: car approaches at DIAGONAL angle (45-90 degrees)
            // NOT perpendicular, NOT straight-on, but diagonal
            float minCutAngle = 0.53f;               // ~30 degrees
            float maxCutAngle = 1.52f;                // ~90 degrees (was 90)
            float carBallDistanceForCut = 300.0f;
            float carBallDistanceTolerance = 80.0f;    // Tighter tolerance

            // POP phase: ball should gain significant height
            float minPopHeight = 50.0f;                // Ball must gain at least 50 units
            float maxPopHeight = 300.0f;               // But not too high
            float popDetectionWindow = 0.5f;           // Time window to detect pop (seconds)

            // Catching phase: car underneath ball
            float ballHeightForCatch = 100.0f;
            float ballHeightCatchTolerance = 40.0f;

            // Overall constraints
            float maxGroundBallHeight = 150.0f;
            float maxPhaseTransitionTime = 3.0f;

            // Anti-circling: car must move a minimum distance between phases
            float minCarDistanceMoved = 150.0f;        // Car must move at least 150 units
            float maxAllowedCarVelocity = 1200.0f;    // Don't reward slow shuffling
        };

        enum DribblePhase {
            PHASE_IDLE,
            PHASE_ROLLING,
            PHASE_MATCHING,
            PHASE_CUTTING,
            PHASE_POPPING,      // NEW: detect the pop
            PHASE_CATCHING
        };

        mutable std::map<uint32_t, DribblePhase> currentPhase;
        mutable std::map<uint32_t, float> phaseTimer;
        mutable std::map<uint32_t, Vec> lastBallPos;
        mutable std::map<uint32_t, Vec> lastCarPos;
        mutable std::map<uint32_t, float> ballHeightAtCut;
        mutable std::map<uint32_t, float> lastBallSpeed;

        DribbleConfig config;

        float ComputeCarBallAngle(const Player& player, const GameState& state) const {
            Vec carVel = player.vel;
            Vec carToBall = (state.ball.pos - player.pos);

            if (carVel.Length() < 100.0f || carToBall.Length() < 10.0f) {
                return 0.0f;
            }

            float dotProduct = carVel.Normalized().Dot(carToBall.Normalized());
            dotProduct = RS_CLAMP(dotProduct, -1.0f, 1.0f);
            return acos(dotProduct);
        }

        float ComputeSpeedDelta(const Player& player, const GameState& state) const {
            float carSpeed = player.vel.Length();
            float ballSpeed = state.ball.vel.Length();
            return fabsf(carSpeed - ballSpeed);
        }

        bool IsBallOnGround(const GameState& state) const {
            return state.ball.pos.z <= config.maxGroundBallHeight;
        }

        bool IsBallOnCar(const Player& player, const GameState& state) const {
            float dist = player.pos.Dist(state.ball.pos);
            if (dist < 150.0f && state.ball.pos.z < player.pos.z + 150.0f) {
                return true;
            }
            return false;
        }

    public:
        DribbleSetupReward() : config() {}

        explicit DribbleSetupReward(const DribbleConfig& cfg) : config(cfg) {}

        virtual void Reset(const GameState& initialState) override {
            currentPhase.clear();
            phaseTimer.clear();
            lastBallPos.clear();
            lastCarPos.clear();
            ballHeightAtCut.clear();
            lastBallSpeed.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            // Guard conditions
            if (!IsBallOnGround(state)) {
                if (currentPhase.count(player.carId)) {
                    currentPhase[player.carId] = PHASE_IDLE;
                    phaseTimer[player.carId] = 0.0f;
                }
                return 0.0f;
            }

            if (IsBallOnCar(player, state)) {
                currentPhase[player.carId] = PHASE_IDLE;
                phaseTimer[player.carId] = 0.0f;
                return 0.0f;
            }

            // Initialize state
            if (currentPhase.find(player.carId) == currentPhase.end()) {
                currentPhase[player.carId] = PHASE_IDLE;
                phaseTimer[player.carId] = 0.0f;
                lastBallPos[player.carId] = state.ball.pos;
                lastCarPos[player.carId] = player.pos;
                lastBallSpeed[player.carId] = state.ball.vel.Length();
                ballHeightAtCut[player.carId] = 0.0f;
                return 0.0f;
            }

            DribblePhase& phase = currentPhase[player.carId];
            float& timer = phaseTimer[player.carId];
            timer += CommonValues::TICK_TIME;

            // Check timeout
            if (timer > config.maxPhaseTransitionTime) {
                phase = PHASE_IDLE;
                timer = 0.0f;
                return 0.0f;
            }

            float ballSpeed = state.ball.vel.Length();
            float carSpeed = player.vel.Length();
            float speedDelta = ComputeSpeedDelta(player, state);
            float carBallAngle = ComputeCarBallAngle(player, state);
            float carBallDist = player.pos.Dist(state.ball.pos);
            float carDistMoved = lastCarPos[player.carId].Dist(player.pos);

            float reward = 0.0f;

            // =====================================================================
            // SLOW BALL HANDLING: Reward small hits when ball is moving slowly
            // =====================================================================
            if (ballSpeed < config.slowBallThreshold && player.ballTouchedStep) {
                // Reward hitting a slow ball to get it rolling
                reward += config.slowBallHitReward;

                // Bonus if ball speed increased significantly
                float prevBallSpeed = lastBallSpeed[player.carId];
                float speedGain = ballSpeed - prevBallSpeed;

                if (speedGain > config.minSpeedGainForBonus) {
                    float speedGainQuality = RS_CLAMP(speedGain / 300.0f, 0.0f, 1.0f);
                    reward += config.slowBallSpeedGainBonus * speedGainQuality;
                }

                lastBallSpeed[player.carId] = ballSpeed;
                return reward;
            }

            // =====================================================================
            // PHASE 1: ROLLING - Ball moves slowly and steadily
            // =====================================================================
            if (phase == PHASE_IDLE || phase == PHASE_ROLLING) {
                if (ballSpeed > config.minBallRollSpeed &&
                    ballSpeed < config.maxBallRollSpeed) {

                    float rollQuality = 1.0f - (ballSpeed / config.maxBallRollSpeed);
                    reward = rollQuality * 0.5f;

                    phase = PHASE_ROLLING;
                    timer = 0.0f;
                    lastBallSpeed[player.carId] = ballSpeed;
                    lastCarPos[player.carId] = player.pos;

                    return reward;
                }
            }

            // =====================================================================
            // PHASE 2: MATCHING - Car matches ball speed and position
            // =====================================================================
            if (phase == PHASE_ROLLING) {
                if (speedDelta < config.maxSpeedDelta &&
                    speedDelta > config.minSpeedDelta &&
                    carBallDist < 400.0f &&
                    carSpeed > 500.0f) {  // Ensure car is actually moving, not shuffling

                    float matchQuality = 1.0f - (speedDelta / config.maxSpeedDelta);
                    float distQuality = RS_CLAMP(1.0f - (carBallDist / 400.0f), 0.3f, 1.0f);

                    reward = matchQuality * distQuality * 0.75f;

                    phase = PHASE_MATCHING;
                    timer = 0.0f;
                    lastCarPos[player.carId] = player.pos;

                    return reward;
                }
                else if (ballSpeed > config.maxBallRollSpeed * 1.5f) {
                    phase = PHASE_IDLE;
                    timer = 0.0f;
                    return 0.0f;
                }
            }

            // =====================================================================
            // PHASE 3: CUTTING - Car approaches at DIAGONAL angle (45-75°)
            // =====================================================================
            if (phase == PHASE_MATCHING) {
                bool distanceGood = fabsf(carBallDist - config.carBallDistanceForCut)
                    < config.carBallDistanceTolerance;

                // STRICT diagonal angle: must be between 45-75 degrees
                bool angleGood = carBallAngle >= config.minCutAngle &&
                    carBallAngle <= config.maxCutAngle;

                // Anti-circling: car must have moved significant distance since matching started
                bool movedEnough = carDistMoved >= config.minCarDistanceMoved;

                if (distanceGood && angleGood && movedEnough) {
                    // Reward diagonal cut
                    float angleScore = 1.0f - (fabsf(carBallAngle - 0.9599f) / 0.5236f); // 0.9599 = 55° (ideal)
                    float distScore = 1.0f - (fabsf(carBallDist - config.carBallDistanceForCut)
                        / config.carBallDistanceTolerance);
                    float moveScore = RS_CLAMP(carDistMoved / 300.0f, 0.5f, 1.0f);

                    reward = (angleScore * 0.5f + distScore * 0.35f + moveScore * 0.15f) * 1.2f;

                    phase = PHASE_CUTTING;
                    timer = 0.0f;
                    ballHeightAtCut[player.carId] = state.ball.pos.z;
                    lastCarPos[player.carId] = player.pos;

                    return reward;
                }
            }

            // =====================================================================
            // PHASE 4: POPPING - Detect ball height gain
            // =====================================================================
            if (phase == PHASE_CUTTING) {
                float heightGain = state.ball.pos.z - ballHeightAtCut[player.carId];

                // Ball must pop up at least 50 units
                if (heightGain >= config.minPopHeight && heightGain <= config.maxPopHeight) {
                    // Reward successful pop
                    float popQuality = RS_CLAMP((heightGain - config.minPopHeight) / 100.0f, 0.0f, 1.0f);
                    reward = popQuality * 1.5f;

                    phase = PHASE_POPPING;
                    timer = 0.0f;

                    return reward;
                }
                // If ball isn't popping, wait a bit longer
                else if (timer < config.popDetectionWindow) {
                    return 0.1f;  // Small reward for attempting
                }
                // Timeout - cut didn't generate pop
                else {
                    phase = PHASE_IDLE;
                    timer = 0.0f;
                    return -0.3f;  // Penalize failed cuts
                }
            }

            // =====================================================================
            // PHASE 5: CATCHING - Car drives underneath and catches ball
            // =====================================================================
            if (phase == PHASE_POPPING) {
                float ballHeightDiff = fabsf(state.ball.pos.z - config.ballHeightForCatch);
                bool heightGood = ballHeightDiff < config.ballHeightCatchTolerance;
                bool carBellowBall = player.pos.z < state.ball.pos.z;
                bool closeToBall = carBallDist < 200.0f;

                if (heightGood && carBellowBall && closeToBall) {
                    float heightScore = 1.0f - (ballHeightDiff / config.ballHeightCatchTolerance);
                    float distScore = RS_CLAMP(1.0f - (carBallDist / 200.0f), 0.0f, 1.0f);

                    reward = (heightScore * 0.6f + distScore * 0.4f) * 1.5f;

                    phase = PHASE_CATCHING;
                    timer = 0.0f;

                    return reward;
                }
            }

            return 0.0f;
        }

        // Getters/Setters
        void SetCutAngleRange(float minAngle, float maxAngle) {
            config.minCutAngle = minAngle;
            config.maxCutAngle = maxAngle;
        }
        void SetMinPopHeight(float height) { config.minPopHeight = height; }
        void SetCarBallDistanceForCut(float dist, float tolerance) {
            config.carBallDistanceForCut = dist;
            config.carBallDistanceTolerance = tolerance;
        }
        void SetMinCarDistanceMoved(float dist) { config.minCarDistanceMoved = dist; }
        void SetMaxPhaseTransitionTime(float time) { config.maxPhaseTransitionTime = time; }
    };

    // ============================================================================
    // BallControlAngleReward - ELITE TIER
    // Rewards controlling the ball when it's NOT on the car
    // Encourages sharp angle changes and precise ball redirection
    // Useful for: redirects, flick setups, aerial ball control
    // 2025-12-23 | Rev 1.0 | NJH
    // ============================================================================
    class BallControlAngleReward : public Reward {
    private:
        mutable std::map<uint32_t, Vec> lastBallVel;
        mutable std::map<uint32_t, float> ballControlTimer;

        // Minimum distance from car to ball to NOT be considered "on car"
        static constexpr float MIN_BALL_DISTANCE = 150.0f;

        // Bonus threshold - angle changes above this trigger bonus reward
        static constexpr float SHARP_ANGLE_THRESHOLD = 0.5f; // radians (~29 degrees)

        float GetSharpnessBonus(float angleChange) const {
            // Returns bonus multiplier based on how sharp the angle change is
            // 0-30°: 1.0x (baseline)
            // 30-60°: 1.5x (good)
            // 60-90°: 2.0x (excellent)
            // 90°+: 2.5x (exceptional)

            if (angleChange < SHARP_ANGLE_THRESHOLD) {
                return 1.0f;
            }
            else if (angleChange < 1.047f) { // ~60 degrees
                return 1.0f + (angleChange - SHARP_ANGLE_THRESHOLD) / (1.047f - SHARP_ANGLE_THRESHOLD);
            }
            else if (angleChange < 1.571f) { // ~90 degrees
                return 1.5f + (angleChange - 1.047f) / (1.571f - 1.047f) * 0.5f;
            }
            else {
                return 2.5f;
            }
        }

        float ComputeAngleChange(const Vec& oldVel, const Vec& newVel) const {
            float oldLen = oldVel.Length();
            float newLen = newVel.Length();

            // Need minimum velocity to evaluate changes
            if (oldLen < 50.0f || newLen < 50.0f) {
                return 0.0f;
            }

            // Compute angle between velocity vectors
            float dotProduct = oldVel.Normalized().Dot(newVel.Normalized());
            dotProduct = RS_CLAMP(dotProduct, -1.0f, 1.0f);

            return acos(dotProduct);
        }

        bool IsBallOnCar(const Player& player, const GameState& state) const {
            // Ball is on car if within certain distance and relative position
            float ballDist = player.pos.Dist(state.ball.pos);

            // Crude check - more refined version would check if ball is above/on car
            if (ballDist < MIN_BALL_DISTANCE) {
                // Check if ball is above the car (not in front of it)
                Vec carToball = state.ball.pos - player.pos;

                // If ball is mostly above car (high Z relative to car), it's "on car"
                if (state.ball.pos.z < player.pos.z + 150.0f) {
                    return true;
                }
            }

            return false;
        }

        float GetDirectionBonus(const Player& player, const GameState& state, const Vec& ballVel) const {
            // Bonus for directing ball toward opponent goal
            Vec opponentGoal = (player.team == Team::BLUE)
                ? CommonValues::ORANGE_GOAL_CENTER
                : CommonValues::BLUE_GOAL_CENTER;

            Vec toGoal = (opponentGoal - state.ball.pos).Normalized();
            float alignment = ballVel.Normalized().Dot(toGoal);

            // Only reward if ball is moving somewhat toward goal
            if (alignment > 0.2f) {
                return RS_CLAMP(alignment, 0.0f, 1.0f) * 0.5f; // Up to 0.5 bonus
            }

            return 0.0f;
        }

        float GetHeightBonus(const GameState& state) const {
            // Bonus for controlling ball at difficult heights
            // Higher = harder to control
            float heightAboveGround = state.ball.pos.z;

            if (heightAboveGround < 100.0f) {
                return 0.0f; // Ball on ground, no bonus
            }
            else if (heightAboveGround < 300.0f) {
                return 0.25f; // Slight bonus for aerial control
            }
            else if (heightAboveGround < 700.0f) {
                return 0.5f; // Good bonus for mid-air
            }
            else {
                return 0.75f; // Excellent bonus for high ball control
            }
        }

    public:
        virtual void Reset(const GameState& initialState) override {
            lastBallVel.clear();
            ballControlTimer.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            // Only reward when ball is NOT on car
            if (IsBallOnCar(player, state)) {
                ballControlTimer[player.carId] = 0.0f;
                return 0.0f;
            }

            // Track how long ball has been controlled
            ballControlTimer[player.carId] += CommonValues::TICK_TIME;

            Vec currentBallVel = state.ball.vel;
            auto it = lastBallVel.find(player.carId);

            // Initialize on first tick
            if (it == lastBallVel.end()) {
                lastBallVel[player.carId] = currentBallVel;
                return 0.0f;
            }

            Vec lastVel = it->second;
            lastBallVel[player.carId] = currentBallVel;

            // Compute angle change in ball trajectory
            float angleChange = ComputeAngleChange(lastVel, currentBallVel);

            float baseReward = 0.0f;

            // REWARD 1: Angle change (main component)
            if (angleChange > 0.05f) { // At least ~3 degrees
                // Scale reward by how sharp the angle is
                float sharpness = GetSharpnessBonus(angleChange);
                baseReward = RS_CLAMP(angleChange / 1.571f, 0.0f, 1.0f) * sharpness * 2.0f;
            }

            // REWARD 2: Direction bonus (toward opponent goal)
            if (baseReward > 0.1f) {
                float directionBonus = GetDirectionBonus(player, state, currentBallVel);
                baseReward += directionBonus;
            }

            // REWARD 3: Height bonus (harder to control at height)
            if (baseReward > 0.1f) {
                float heightBonus = GetHeightBonus(state);
                baseReward += heightBonus;
            }

            // REWARD 4: Consistency bonus (sustained control)
            if (ballControlTimer[player.carId] > 0.1f && baseReward > 0.1f) {
                float consistencyBonus = RS_CLAMP(ballControlTimer[player.carId] / 1.0f, 0.0f, 0.5f);
                baseReward += consistencyBonus * 0.25f;
            }

            return baseReward;
        }
    };


    // ============================================================================
    // BallRedirectReward - ELITE TIER
    // Specialized reward for sharp redirects from high-speed ball contact
    // Rewards changing ball direction by large angles with ball speed maintained
    // Useful for: advanced redirects, flip resets, high-angle saves
    // 2025-12-23 | Rev 1.0 | NJH
	// 2026-01-29 | Rev 1.1 | NJH - Added speed retention check
    // ============================================================================
    class BallRedirectReward : public Reward {
    private:
        static constexpr float HIGH_SPEED_THRESHOLD     = 1500.0f;
        static constexpr float SPEED_RETENTION_MIN      = 0.85f;
        static constexpr float ANGLE_THRESHOLD          = 0.349f;
        static constexpr float GOAL_ALIGNMENT_THRESHOLD = 0.3f;

        float cooldownSeconds;
        mutable std::unordered_map<uint32_t, float> _cooldownRemaining;
        mutable uint32_t _lastToucherCarId = 0;

    public:
        BallRedirectReward(float cooldownMs = 500.0f)
            : cooldownSeconds(cooldownMs / 1000.0f) {}

        virtual void Reset(const GameState& initialState) override {
            _cooldownRemaining.clear();
            _lastToucherCarId = 0;
        }

        // Track last toucher once per frame (PreStep runs once, not once per player)
        virtual void PreStep(const GameState& state) override {
            for (const auto& p : state.players) {
                if (p.ballTouchedStep) {
                    _lastToucherCarId = p.carId;
                    break;
                }
            }
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!state.prev) return 0.0f;

            float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            float& cd = _cooldownRemaining[player.carId];
            if (cd > 0.0f) { cd = RS_MAX(0.0f, cd - dt); return 0.0f; }

            if (!player.ballTouchedStep) return 0.0f;

            const Vec& prevBallVel = state.prev->ball.vel;
            const Vec& curBallVel  = state.ball.vel;
            float lastSpeed = prevBallVel.Length();
            float curSpeed  = curBallVel.Length();

            if (lastSpeed < HIGH_SPEED_THRESHOLD) return 0.0f;

            float speedRetention = curSpeed / RS_MAX(lastSpeed, 1.0f);
            if (speedRetention < SPEED_RETENTION_MIN) return 0.0f;

            float dotProduct = RS_CLAMP(prevBallVel.Normalized().Dot(curBallVel.Normalized()), -1.0f, 1.0f);
            float angleChange = acos(dotProduct);
            if (angleChange < ANGLE_THRESHOLD) return 0.0f;

            Vec opponentGoal = (player.team == Team::BLUE)
                ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
            float goalAlignment = curBallVel.Normalized().Dot((opponentGoal - state.ball.pos).Normalized());
            if (goalAlignment < GOAL_ALIGNMENT_THRESHOLD) return 0.0f;

            float sharpness  = RS_CLAMP(angleChange / 1.571f, 0.0f, 1.0f);
            float speedBonus = RS_CLAMP(speedRetention - SPEED_RETENTION_MIN, 0.0f, 0.35f);
            float baseReward = sharpness * (2.0f + speedBonus);
            float goalMult   = (goalAlignment > 0.7f) ? 2.0f : 1.5f;

            bool lastTouchWasTeammate = false;
            if (_lastToucherCarId != player.carId) {
                for (const auto& other : state.players) {
                    if (other.carId == _lastToucherCarId && other.team == player.team) {
                        lastTouchWasTeammate = true;
                        break;
                    }
                }
            }
            float teamMult = lastTouchWasTeammate ? 2.0f : 1.0f;

            float result = baseReward * goalMult * teamMult;
            if (result > 0.0f) cd = cooldownSeconds;
            return result;
        }
    };



    // ============================================================================
    // BallFlickSetupReward - ELITE TIER
    // Rewards positioning ball for flick mechanics
    // Encourages ball placement that's hard to defend against
    // Useful for: setting up flicks, aerial flicks, flip reset setups
    // 2025-12-23 | Rev 1.0 | NJH
    // ============================================================================
    class BallFlickSetupReward : public Reward {
    private:
        static constexpr float IDEAL_FLICK_HEIGHT = 60.0f;        // Optimal ball height for flick
        static constexpr float IDEAL_FLICK_HEIGHT_RANGE = 40.0f;  // ±40 units acceptable
        static constexpr float IDEAL_DISTANCE = 120.0f;           // Ideal distance from car nose to ball
        static constexpr float IDEAL_DISTANCE_RANGE = 30.0f;      // ±30 units acceptable

    public:
        virtual void Reset(const GameState& initialState) override {
            // No state tracking needed
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            // Ball must be close to car but not ON car
            float ballDist = player.pos.Dist(state.ball.pos);
            if (ballDist < 50.0f || ballDist > 300.0f) {
                return 0.0f;
            }

            // Compute vector from car to ball
            Vec carToBall = state.ball.pos - player.pos;

            // Ball should be mostly in front of car (along forward axis)
            float alignmentWithForward = player.rotMat.forward.Dot(carToBall.Normalized());
            if (alignmentWithForward < 0.5f) {
                return 0.0f; // Ball is behind or to the side
            }

            float heightDiff = fabsf(state.ball.pos.z - IDEAL_FLICK_HEIGHT);
            float distDiff = fabsf(ballDist - IDEAL_DISTANCE);

            // Reward for proper height positioning
            float heightScore = 0.0f;
            if (heightDiff < IDEAL_FLICK_HEIGHT_RANGE) {
                heightScore = 1.0f - (heightDiff / IDEAL_FLICK_HEIGHT_RANGE);
            }

            // Reward for proper distance positioning
            float distScore = 0.0f;
            if (distDiff < IDEAL_DISTANCE_RANGE) {
                distScore = 1.0f - (distDiff / IDEAL_DISTANCE_RANGE);
            }

            // Forward alignment bonus
            float alignmentBonus = RS_CLAMP(alignmentWithForward - 0.5f, 0.0f, 0.5f);

            float reward = (heightScore * 0.4f + distScore * 0.4f + alignmentBonus * 0.2f) * 1.5f;

            return reward;
        }
    };

    // ============================================================================
// ORBIT BALL PUNISHMENT
// Punishes circling the ball without making progress
// ============================================================================

    class OrbitBallPunishment : public Reward {
    private:
        float minDistance = 600.f;          // too close is usually dribble / challenge
        float maxDistance = 3000.f;         // beyond this is rotation
        float minLateralSpeed = 300.f;      // must actually be orbiting
        float maxRadialSpeed = 150.f;       // not closing or leaving much
        float punishment = -0.002f;         // per-step penalty

    public:
        virtual float GetReward(const Player& player,
            const GameState& state,
            bool isFinal) override
        {
            Vec toBall = state.ball.pos - player.pos;
            float dist = toBall.Length();

            if (dist < minDistance || dist > maxDistance)
                return 0.f;

            Vec toBallDir = toBall.Normalized();
            Vec carVel = player.vel;

            float radialSpeed = carVel.Dot(toBallDir);  // + toward ball
            Vec tangentialVel = carVel - toBallDir * radialSpeed;
            float lateralSpeed = tangentialVel.Length();

            // Orbiting condition
            if (fabs(radialSpeed) < maxRadialSpeed &&
                lateralSpeed > minLateralSpeed)
            {
                return punishment;
            }

            return 0.f;
        }
    };

    // ============================================================================
    // LOW BOOST DRIBBLE PUNISHMENT
    // Punishes trying to dribble with <15 boost
    // ============================================================================

    class LowBoostDribblePunishment : public Reward {
    private:
        float maxDribbleDistXY = 350.f;      // ball close horizontally
        float minBallHeightAboveCar = 25.f;  // ball slightly above car
        float boostThreshold = 15.f;
        float punishment = -0.004f;

    public:
        virtual float GetReward(const Player& player,
            const GameState& state,
            bool isFinal) override
        {
            if (player.boost > boostThreshold)
                return 0.f;

            Vec diff = state.ball.pos - player.pos;

            float distXY = std::sqrt(diff.x * diff.x + diff.y * diff.y);

            // Approximate car height (typical hitbox height is around 36-40)
            float carHeight = 36.0f;
            float heightAboveCar = diff.z - carHeight;

            bool dribbleLike =
                distXY < maxDribbleDistXY &&
                heightAboveCar > minBallHeightAboveCar &&
                state.ball.vel.z > -200.f;  // not falling

            if (dribbleLike)
                return punishment;

            return 0.f;
        }
    };

    // ------------------------------------------------------------
    // Reward having boost on offense, punish low boost on offense
    // Offense = (in opponent half) OR (soft possession).
    // ------------------------------------------------------------
    class BoostOnOffenseReward : public Reward {
    public:
        float rewardScale = 0.01f;     // positive when boosted on offense
        float punishScale = 0.02f;     // negative when <20 on offense
        float lowBoostPunishThresh = 20.0f;

        // possession heuristics
        float possessionDist = 750.0f;

        virtual void Reset(const GameState& initialState) override {}

        static inline bool InOpponentHalf(const Player& p) {
            // Typical RL coords: Orange goal at +Y, Blue goal at -Y.
            // Blue team attacks +Y, Orange attacks -Y.
            if (p.team == Team::BLUE) return p.pos.y > 0.0f;  // BLUE
            else                      return p.pos.y < 0.0f;  // ORANGE
        }

        float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            const bool inOppHalf = InOpponentHalf(player);

            // "Soft possession": closest on your team and within possessionDist.
            float myDist = (state.ball.pos - player.pos).Length();
            bool closestOnTeam = true;
            for (const Player& other : state.players) {
                if (other.team != player.team) continue;
                if (other.carId == player.carId) continue;
                float d = (state.ball.pos - other.pos).Length();
                if (d + 1e-3f < myDist) { closestOnTeam = false; break; }
            }
            const bool hasPossession = closestOnTeam && (myDist <= possessionDist);

            const bool onOffense = inOppHalf || hasPossession;
            if (!onOffense) return 0.0f;

            const float boost01 = RS_CLAMP(player.boost / 100.0f, 0.0f, 1.0f);

            float r = rewardScale * boost01;

            if (player.boost < lowBoostPunishThresh) {
                const float low01 = 1.0f - RS_CLAMP(player.boost / lowBoostPunishThresh, 0.0f, 1.0f);
                r -= punishScale * low01;
            }

            return r;
        }
    };


    // ============================================================================
    // DEFENSIVE REWARDS
    // ============================================================================

    // Reward for being between ball and own goal (defensive positioning)
    // - Decreases if opponents are far from ball/you
    // - Increases if nearby opponents are moving toward the ball/you (threat)
    // - Decreases if our player is on the opponent's half (we prefer offense there)
    class DefensivePositionReward : public Reward {
    public:
        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            Vec myGoal = (player.team == Team::BLUE) ?
                CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

            Vec ballToGoal = myGoal - state.ball.pos;
            float distBallToGoal = ballToGoal.Length();

            if (distBallToGoal < 1e-5f) return 0.0f;

            Vec goalDir = ballToGoal.Normalized();
            Vec ballToPlayer = player.pos - state.ball.pos;

            // Check if player is between ball and goal
            float alignment = ballToPlayer.Normalized().Dot(goalDir);

            // P0: Soft penalty for wrong side (gradient instead of hard zero)
            // Map [-0.3, 0] to [0, 0.15] reward for overshooting goal line
            // Beyond -0.3: clamp to -0.3 (don't penalize harshly for far overshoots)
            if (alignment < 0.0f) {
                alignment = RS_CLAMP(alignment, -0.3f, 0.0f) * 0.5f;  // Result in [-0.15, 0]
            }

            // Reward for being in defensive position (distance to defensive line)
            float distToDefensiveLine = fabs(ballToPlayer.Dot(goalDir));

            // P1: Tighter bell-curve with optimal zone 1800-2200 UU
            // Provides clearer feedback on positioning accuracy
            static constexpr float optimalMin = 1800.0f;
            static constexpr float optimalMax = 2200.0f;
            float distanceReward;

            if (distToDefensiveLine >= optimalMin && distToDefensiveLine <= optimalMax) {
                distanceReward = 1.0f;  // Full reward in sweet spot
            } else {
                // Smooth decay outside optimal range (0.67% penalty per 100 UU)
                float distFromOptimal = (distToDefensiveLine < optimalMin) ?
                    (optimalMin - distToDefensiveLine) :
                    (distToDefensiveLine - optimalMax);

                // Decay from 1.0 to 0.2 over 1500 UU beyond optimal
                distanceReward = 1.0f - RS_CLAMP(distFromOptimal / 1500.0f, 0.0f, 0.8f);
            }

            // Base score
            float baseScore = alignment * distanceReward;

            // --- Opponent proximity modifier ---
            float opponentFarThreshold = 2500.0f;    // if opponents are farther than this, downweight
            float nearestOppToBall = FLT_MAX;
            float nearestOppToPlayer = FLT_MAX;

            for (const auto& opp : state.players) {
                if (opp.team == player.team) continue;
                float dBall = (opp.pos - state.ball.pos).Length();
                float dPlayer = (opp.pos - player.pos).Length();
                nearestOppToBall = RS_MIN(nearestOppToBall, dBall);
                nearestOppToPlayer = RS_MIN(nearestOppToPlayer, dPlayer);
            }

            float nearestOpp = RS_MIN(nearestOppToBall, nearestOppToPlayer);

            // If opponents are far, reduce the defensive reward to avoid over-defending empty threats
            float proximityFactor = 1.0f;
            if (nearestOpp > opponentFarThreshold) {
                // Linearly reduce to 0.35 when very far (2x threshold)
                float t = RS_CLAMP((nearestOpp - opponentFarThreshold) / opponentFarThreshold, 0.0f, 1.0f);
                proximityFactor = 1.0f - 0.65f * t; // in [1.0 .. 0.35]
            }

            // --- Opponent movement threat boost (P1 improvements) ---
            // P1: Adaptive threat radius based on game mode (fewer opponents = tighter detection)
            int numEnemies = 0;
            for (const auto& p : state.players) {
                if (p.team != player.team && !p.isDemoed) numEnemies++;
            }
            float opponentInfluenceRadius = (numEnemies == 1) ? 1000.0f :  // 1v1: tight
                                             (numEnemies == 2) ? 1500.0f :  // 2v2: medium
                                             2000.0f;  // 3v3: wide threat detection

            float threatSum = 0.0f;
            int threatCount = 0;

            for (const auto& opp : state.players) {
                if (opp.team == player.team) continue;

                // Consider threat if within influence radius of ball or player
                float dToBall = (opp.pos - state.ball.pos).Length();
                float dToPlayer = (opp.pos - player.pos).Length();
                float minD = RS_MIN(dToBall, dToPlayer);
                if (minD > opponentInfluenceRadius) continue;

                // P1: Skip stationary opponents (no threat)
                float vlen = opp.vel.Length();
                if (vlen < 1e-4f) continue;

                // Check movement toward ball and/or toward player
                Vec velN = opp.vel.Normalized();
                Vec toBallN = (state.ball.pos - opp.pos).Normalized();
                Vec toPlayerN = (player.pos - opp.pos).Normalized();
                float alignmentToBall = RS_MAX(0.0f, velN.Dot(toBallN));
                float alignmentToPlayer = RS_MAX(0.0f, velN.Dot(toPlayerN));

                float align = RS_MAX(alignmentToBall, alignmentToPlayer); // 0..1

                // Scale by closeness (closer → stronger)
                float closeness = 1.0f - RS_CLAMP(minD / opponentInfluenceRadius, 0.0f, 1.0f);

                float contrib = align * closeness;
                // P1: ACCUMULATE threat from multiple opponents (not just max)
                // Correctly handles multiple simultaneous threats
                threatSum += contrib;
                threatCount++;
            }

            // P1: Scale threat - cap at 2.0 for 2.0x multiplier (up from 1.75x)
            threatSum = RS_MIN(threatSum, 2.0f);
            float threatMultiplier = 1.0f + 0.5f * threatSum;  // [1.0, 2.0] range

            // --- Half-field penalty: if we're on the opponent's half, reduce defensive weighting ---
            bool onOpponentHalf = (player.team == Team::BLUE && player.pos.y > 0.0f) ||
                (player.team == Team::ORANGE && player.pos.y < 0.0f);

            float halfPenalty = onOpponentHalf ? 0.5f : 1.0f;

            float finalScore = baseScore * proximityFactor * threatMultiplier * halfPenalty;

            // Clamp to reasonable range (avoid large spikes)
            return RS_MAX(0.0f, finalScore);
        }
    };

    // Reward for saving ball from going in goal
    class SaveAttemptReward : public Reward {
    private:
        float dangerZone = 1500.0f;

    public:
        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            Vec myGoal = (player.team == Team::BLUE) ?
                CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

            float distBallToGoal = (state.ball.pos - myGoal).Length();

            // Only reward if ball is near goal
            if (distBallToGoal > dangerZone) return 0.0f;

            // Only reward if ball is moving toward goal
            Vec ballToGoal = (myGoal - state.ball.pos).Normalized();
            float ballGoalAlignment = state.ball.vel.Normalized().Dot(ballToGoal);

            if (ballGoalAlignment < 0.3f) return 0.0f; // Not really going toward goal

            // Reward touch that changes ball direction away from goal
            if (!player.ballTouchedStep) return 0.0f;

            // Check if ball is now moving away from goal
            if (!state.prev) return 0.0f;

            Vec prevBallToGoal = (myGoal - state.prev->ball.pos).Normalized();
            float prevAlignment = state.prev->ball.vel.Normalized().Dot(prevBallToGoal);
            float curAlignment = state.ball.vel.Normalized().Dot(ballToGoal);

            float alignmentChange = prevAlignment - curAlignment;
            if (alignmentChange > 0.0f) {
                // Successfully deflected ball away from goal
                return alignmentChange * 2.0f;
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // MECHANICAL SKILL REWARDS
    // ============================================================================

    //// Reward for flip resets (regaining flip in air)
    //class FlipResetReward : public Reward {
    //public:
    //    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
    //        if (!state.prev) return 0.0f;

    //        // Check if we just regained flip while in air
    //        if (player.isOnGround || state.prev->players[player.index].isOnGround) {
    //            return 0.0f;
    //        }

    //        bool hadFlip = state.prev->players[player.index].HasFlipOrJump();
    //        bool hasFlip = player.HasFlipOrJump();

    //        // Regained flip in air
    //        if (!hadFlip && hasFlip) {
    //            return 1.0f;
    //        }

    //        return 0.0f;
    //    }
    //};

    // ============================================================================
    // BasicAerialFormReward
    // "Learner" aerial mechanic:
    //  - Player jumps (leaves ground)
    //  - Applies negative pitch (-1..-0.5) for a short time window
    //  - Then uses boost while still airborne
    // Encourages basic aerial posture before going to full fast aerials.
    // 2025-12-09 - Rev 1 (NJH)
    // 2025-12-09 - Rev 2 (NJH)
    //   - Added context scaling for being under/near an aerial-capable ball,
    //     to overlap with jump/air touch rewards
    // ============================================================================
    class BasicAerialFormReward : public Reward {
    private:
        struct BAState {
            bool wasOnGround = true;
            bool phaseActive = false;
            float timeSinceJump = 0.0f;
            float pitchHoldTime = 0.0f;
            bool hasBoostedAfter = false;
            bool pitchPhaseDone = false;
        };

        mutable std::map<uint32_t, BAState> states;

    public:
        virtual void Reset(const GameState& initialState) override {
            states.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
            auto& st = states[player.carId];

            bool wasOnGround = st.wasOnGround;
            st.wasOnGround = player.isOnGround;

            float reward = 0.0f;

            // Jump detection: transition ground -> air
            if (wasOnGround && !player.isOnGround) {
                st.phaseActive = true;
                st.timeSinceJump = 0.0f;
                st.pitchHoldTime = 0.0f;
                st.hasBoostedAfter = false;
                st.pitchPhaseDone = false;
            }

            if (!st.phaseActive || player.isOnGround)
                return 0.0f;

            st.timeSinceJump += dt;

            // Short time range after jump where we want nose-up pitch
            if (!st.pitchPhaseDone) {
                if (player.prevAction.pitch <= -0.5f) {
                    st.pitchHoldTime += dt;
                }

                // Once we've held pitch for enough time, mark done
                if (st.pitchHoldTime >= 0.06f) { // ~4 frames at 60Hz
                    st.pitchPhaseDone = true;
                }

                // If we take too long, abort
                if (st.timeSinceJump > 0.4f && !st.pitchPhaseDone) {
                    st.phaseActive = false;
                    return 0.0f;
                }
            }
            else {
                // After pitch phase, look for boost usage while airborne
                if (player.prevAction.boost > 0.5f) {
                    st.hasBoostedAfter = true;
                }

                // Evaluate when some time has passed
                if (st.timeSinceJump > 0.15f && st.hasBoostedAfter) {
                    float upVel = player.vel.z;
                    if (upVel > 300.0f) {
                        // Core "form" score based on how long we held pitch
                        float timeScore = RS_CLAMP(st.pitchHoldTime / 0.15f, 0.0f, 1.0f);
                        float base = 2.0f * timeScore;

                        // --- NEW: context factor for being in an aerial situation ---
                        Vec diff = state.ball.pos - player.pos;
                        Vec diffXY = diff;
                        diffXY.z = 0.0f;

                        float distXY = diffXY.Length();
                        float height = diff.z;

                        // Default: slightly reduced reward if we're not near a usable ball
                        float context = 0.7f;

                        // If we are roughly under / near a potentially aerial-able ball,
                        // add some extra scaling to encourage form in those situations.
                        if (distXY < 800.0f && height > 200.0f && height < 1200.0f) {
                            float proximity = 1.0f - RS_MIN(1.0f, distXY / 800.0f);
                            float heightNorm = RS_CLAMP((height - 200.0f) / 800.0f, 0.0f, 1.0f);
                            float aerialContext = 0.5f * (proximity + heightNorm); // 0..1
                            context = 0.7f + 0.3f * aerialContext;                 // [0.7,1.0]
                        }

                        reward = base * context;
                    }
                    st.phaseActive = false;
                }
            }


            return reward;
        }
    };


    // ============================================================================
    // FastAerialReward
    // Detects a proper fast aerial:
    //  - First jump, then 0.1-0.3s of negative pitch (-1..-0.5) while boosting
    //  - Then a second jump (no flip input)
    //  - Then continued nose-up control for a short window after second jump
    // Gives higher reward when boost is used in the pitch phases.
    // 2025-12-09 - Rev 4 (NJH)
    // ============================================================================
    class FastAerialReward : public Reward {
    private:
        struct FAState {
            bool prevHasJump = false;
            bool prevHasDouble = false;

            bool seqActive = false;
            float timeSinceFirstJump = 0.0f;
            float pitchUpTime = 0.0f;
            bool boostedDuringPitch = false;

            bool postPhaseActive = false;
            float postWindow = 0.0f;
            float postPitchAccum = 0.0f;
            bool boostedPost = false;
        };

        mutable std::map<uint32_t, FAState> states;

    public:
        virtual void Reset(const GameState& initialState) override {
            states.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (state.deltaTime <= 0.0f) {
                // Just guard; in practice deltaTime should be set.
            }
            float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

            auto& st = states[player.carId];

            // Reset if clearly on ground and idle
            if (player.isOnGround && !player.hasJumped && !player.hasDoubleJumped) {
                st = FAState{};
            }

            bool hasJump = player.hasJumped;
            bool hasDouble = player.hasDoubleJumped;

            bool firstJumpNow = hasJump && !st.prevHasJump;
            bool secondJumpNow = hasDouble && !st.prevHasDouble;

            st.prevHasJump = hasJump;
            st.prevHasDouble = hasDouble;

            float reward = 0.0f;

            // Start sequence on first jump, while off ground
            if (firstJumpNow && !player.isOnGround) {
                st.seqActive = true;
                st.timeSinceFirstJump = 0.0f;
                st.pitchUpTime = 0.0f;
                st.boostedDuringPitch = false;
                st.postPhaseActive = false;
                st.postPitchAccum = 0.0f;
                st.boostedPost = false;
            }

            // Phase 1: first jump → pitch up with boost
            if (st.seqActive) {
                st.timeSinceFirstJump += dt;

                // Negative pitch window (nose up)
                bool pitchUp = (player.prevAction.pitch <= -0.5f);
                if (pitchUp) {
                    st.pitchUpTime += dt;
                    if (player.prevAction.boost > 0.5f) {
                        st.boostedDuringPitch = true;
                    }
                }

                // If we took too long, abandon fast aerial
                if (st.timeSinceFirstJump > 0.5f && !hasDouble) {
                    st.seqActive = false;
                }
            }

            // Detect second jump (should be a neutral-stick jump, not a flip)
            if (st.seqActive && secondJumpNow) {
                // Require 0.1-0.3s of nose-up time before second jump
                if (st.pitchUpTime >= 0.1f && st.pitchUpTime <= 0.35f) {
                    // Check that second jump didn't have flip input at that frame
                    bool noFlipInput =
                        std::fabs(player.prevAction.pitch) < 0.3f &&
                        std::fabs(player.prevAction.yaw) < 0.3f &&
                        std::fabs(player.prevAction.roll) < 0.3f;

                    if (noFlipInput) {
                        // Enter post phase: we want to see nose-up maintained after second jump
                        st.seqActive = false;
                        st.postPhaseActive = true;
                        st.postWindow = 0.25f;   // ~0.25s window to hold nose-up
                        st.postPitchAccum = 0.0f;
                        st.boostedPost = false;
                    }
                    else {
                        // Probably a flip, abort
                        st.seqActive = false;
                    }
                }
                else {
                    st.seqActive = false;
                }
            }

            // Post phase: after second jump, hold nose-up and preferably boost
            if (st.postPhaseActive && !player.isOnGround) {
                st.postWindow -= dt;

                bool pitchUp = (player.prevAction.pitch <= -0.5f);
                if (pitchUp) {
                    st.postPitchAccum += dt;
                }
                if (player.prevAction.boost > 0.5f) {
                    st.boostedPost = true;
                }

                if (st.postWindow <= 0.0f) {
                    // Evaluate fast aerial quality
                    float minPostPitch = 0.05f; // at least some time still pitching up
                    if (st.postPitchAccum >= minPostPitch) {
                        float upVel = player.vel.z;
                        if (upVel > 600.0f) {
                            // Base reward
                            float base = 6.0f;

                            // Bonus for boosting in both phases
                            if (st.boostedDuringPitch)
                                base += 2.0f;
                            if (st.boostedPost)
                                base += 1.0f;

                            reward = RS_CLAMP(base, 0.0f, 10.0f);
                        }
                    }

                    st.postPhaseActive = false;
                }
            }

            return reward;
        }
    };

    // ============================================================================
// HalfFastAerialReward
// "Half-way" fast aerial mechanic reward:
//  - Much more forgiving than full FastAerialReward.
//  - Phases:
//      1) First jump off ground starts sequence.
//      2) Quick second jump within a time window (double jump).
//      3) Nose-up (negative pitch) and boost before/after second jump.
//  - Partial credit sources:
//      * Quick double jump, even with poor form.
//      * Nose-up time before the second jump.
//      * Nose-up time after the second jump.
//      * Boost usage during either phase.
//      * Upward velocity factor (moderate, not super strict).
//  - No hard requirement on "perfect" timing or neutral-stick second jump.
// 2025-12-09 - Rev 1 (NJH)
// ============================================================================
    class HalfFastAerialReward : public Reward {
    private:
        struct HFAState {
            bool prevHasJump = false;
            bool prevHasDouble = false;

            bool seqActive = false;   // after first jump
            float timeSinceFirst = 0.0f;

            // Pre-second-jump nose-up / boost
            float prePitchUpTime = 0.0f;
            bool boostedBeforeSecond = false;

            bool doubleJumped = false;
            float timeSinceDouble = 0.0f;

            // Post-second-jump nose-up / boost
            float postPitchUpTime = 0.0f;
            bool boostedAfterSecond = false;
        };

        mutable std::map<uint32_t, HFAState> states;

    public:
        virtual void Reset(const GameState& initialState) override {
            states.clear();
        }

        virtual float GetReward(const Player& player,
            const GameState& state,
            bool isFinal) override {
            float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

            auto& st = states[player.carId];

            // Hard reset if we're clearly idle on the ground
            if (player.isOnGround && !player.hasJumped && !player.hasDoubleJumped) {
                st = HFAState{};
            }

            bool hasJump = player.hasJumped;
            bool hasDouble = player.hasDoubleJumped;

            bool firstJumpNow = hasJump && !st.prevHasJump;
            bool secondJumpNow = hasDouble && !st.prevHasDouble;

            st.prevHasJump = hasJump;
            st.prevHasDouble = hasDouble;

            float reward = 0.0f;

            // --------------------------------------------------------------------
            // Phase 0: start sequence on first jump
            // --------------------------------------------------------------------
            if (firstJumpNow && !player.isOnGround) {
                st.seqActive = true;
                st.timeSinceFirst = 0.0f;
                st.prePitchUpTime = 0.0f;
                st.boostedBeforeSecond = false;
                st.doubleJumped = false;
                st.timeSinceDouble = 0.0f;
                st.postPitchUpTime = 0.0f;
                st.boostedAfterSecond = false;
            }

            // If we never started, nothing to do
            if (!st.seqActive && !st.doubleJumped) {
                return 0.0f;
            }

            // --------------------------------------------------------------------
            // Phase 1: between first and second jump
            // --------------------------------------------------------------------
            if (st.seqActive && !st.doubleJumped) {
                st.timeSinceFirst += dt;

                // Nose-up (negative pitch) before second jump
                bool pitchUp = (player.prevAction.pitch <= -0.4f);
                if (pitchUp) {
                    st.prePitchUpTime += dt;
                }

                // Boost usage before second jump
                if (player.prevAction.boost > 0.5f) {
                    st.boostedBeforeSecond = true;
                }

                // If we take too long without double jumping, abandon
                const float maxFirstWindow = 0.7f; // more forgiving than FastAerialReward
                if (st.timeSinceFirst > maxFirstWindow && !hasDouble) {
                    st = HFAState{};
                    return 0.0f;
                }
            }

            // --------------------------------------------------------------------
            // Phase 2: detect second jump (double jump)
            // --------------------------------------------------------------------
            if (st.seqActive && secondJumpNow) {
                st.doubleJumped = true;
                st.seqActive = false;
                st.timeSinceDouble = 0.0f;
            }

            // --------------------------------------------------------------------
            // Phase 3: after double jump, collect post nose-up / boost
            // --------------------------------------------------------------------
            if (st.doubleJumped && !player.isOnGround) {
                st.timeSinceDouble += dt;

                bool pitchUp = (player.prevAction.pitch <= -0.4f);
                if (pitchUp) {
                    st.postPitchUpTime += dt;
                }

                if (player.prevAction.boost > 0.5f) {
                    st.boostedAfterSecond = true;
                }

                // Evaluate after a small window or if we go too long
                const float evalWindow = 0.4f;
                if (st.timeSinceDouble > evalWindow) {
                    // ----------------------
                    // Partial credit pieces:
                    // ----------------------

                    float score = 0.0f;

                    // 1) Quickness of double jump (time between first and second)
                    //    - Full credit if <= 0.25s, linearly decays until 0.7s
                    float quickness = 0.0f;
                    if (st.doubleJumped) {
                        float t = st.timeSinceFirst;
                        if (t <= 0.25f) {
                            quickness = 1.0f;
                        }
                        else if (t < 0.7f) {
                            quickness = 1.0f - (t - 0.25f) / (0.7f - 0.25f);
                        }
                        quickness = RS_CLAMP(quickness, 0.0f, 1.0f);
                    }
                    score += 2.0f * quickness; // up to 2.0

                    // 2) Nose-up before second jump (helps reward half-formed attempts)
                    float prePitchNorm = RS_CLAMP(st.prePitchUpTime / 0.20f, 0.0f, 1.0f);
                    score += 1.5f * prePitchNorm; // up to 1.5

                    // 3) Nose-up after second jump (staying in aerial form)
                    float postPitchNorm = RS_CLAMP(st.postPitchUpTime / 0.20f, 0.0f, 1.0f);
                    score += 1.5f * postPitchNorm; // up to 1.5

                    // 4) Boost usage bonuses
                    if (st.boostedBeforeSecond) {
                        score += 0.5f;
                    }
                    if (st.boostedAfterSecond) {
                        score += 0.5f;
                    }

                    // 5) Upward velocity factor (soft gate; not as harsh as full fast aerial)
                    float upVel = player.vel.z;
                    float upFactor = 0.0f;
                    // No reward if barely leaving ground
                    if (upVel > 250.0f) {
                        // upFactor grows from 0 at 250 to 1 at 1100
                        upFactor = RS_CLAMP((upVel - 250.0f) / (1100.0f - 250.0f), 0.0f, 1.0f);
                    }

                    // Combine: scale total score by upward velocity factor
                    float finalScore = score * upFactor;

                    // Cap to keep it sane
                    reward = RS_CLAMP(finalScore, 0.0f, 6.0f);

                    // Reset state for next attempt
                    st = HFAState{};
                }
            }

            return reward;
        }
    };



    // ============================================================================
    // TEAM PLAY REWARDS (for future multi-agent)
    // ============================================================================

    // Reward for passing ball to teammate
    class PassReward : public Reward {
    public:
        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!player.ballTouchedStep) return 0.0f;

            // Find closest teammate
            float minTeammateDistance = FLT_MAX;
            for (auto& other : state.players) {
                if (other.carId == player.carId) continue;
                if (other.team != player.team) continue;

                float dist = (other.pos - player.pos).Length();
                minTeammateDistance = RS_MIN(minTeammateDistance, dist);
            }

            if (minTeammateDistance == FLT_MAX) return 0.0f; // No teammates

            // Reward if ball is moving toward teammate after touch
            for (auto& teammate : state.players) {
                if (teammate.carId == player.carId) continue;
                if (teammate.team != player.team) continue;

                Vec ballToTeammate = (teammate.pos - state.ball.pos).Normalized();
                float alignment = state.ball.vel.Normalized().Dot(ballToTeammate);

                if (alignment > 0.5f) {
                    // Ball is moving toward teammate
                    float distanceBonus = 1.0f / (1.0f + minTeammateDistance / 2000.0f);
                    return alignment * distanceBonus;
                }
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // TURNOVER PENALTY — Rev 2.0 (2026-02-22, NJH)
    // Penalizes touches that give the opponent a clear reachability advantage at
    // the predicted ball landing zone.
    //
    // Rev 1.0 issues fixed:
    //   - Checked current ball velocity direction instead of predicted landing zone.
    //     Missed redirected passes (wall/ceiling bounces before reaching opponent).
    //   - Measured proximity to the player, not the landing zone. An opponent
    //     camped right where the ball was predicted to land was underweighted.
    //
    // Rev 2.0 approach:
    //   - Uses BallPred::Cache to predict the ball at 0.5s, 1.0s, and 1.5s.
    //   - At each checkpoint, estimates time-to-reach for self, all teammates,
    //     and all opponents using distance / CAR_MAX_SPEED.
    //   - Fires a penalty only when the opponent's best time-to-reach is
    //     significantly shorter than the friendly team's best (ratio > threshold).
    //   - Penalty scales with how large the advantage is and how fast the ball is.
    //   - Works correctly in 1v1 (no teammates — friendlyTime = self only).
    // ============================================================================
    class PassToOpponentPenalty : public Reward {
    private:
        static constexpr float BASE_PENALTY        = -1.0f;
        static constexpr float MIN_BALL_SPEED      = 400.0f;   // UU/s — ignore soft nudges
        static constexpr float CAR_MAX_SPEED       = 2300.0f;  // UU/s — time-to-reach baseline
        // Opponent must be at least (1/0.65 ≈ 1.54x) faster than friendlies to trigger
        static constexpr float ADVANTAGE_THRESHOLD = 0.65f;
        static constexpr float CHECK_TIME_1        = 0.5f;
        static constexpr float CHECK_TIME_2        = 1.0f;
        static constexpr float CHECK_TIME_3        = 1.5f;

        BallPred::Cache _ballCache;

        // Estimated seconds for player p to reach targetPos at max car speed
        float TimeToReach(const Player& p, const Vec& targetPos) const {
            return (p.pos - targetPos).Length() / CAR_MAX_SPEED;
        }

        // Returns friendlyTime / opponentTime at a predicted ball position.
        //   > 1.0  → opponent reaches faster than any friendly (bad)
        //   <= 1.0 → friendly team wins the race (good, no penalty)
        // friendlyTime = min(self, live teammates); opponentTime = min(live opponents).
        float OpponentAdvantageAt(const Vec& ballPos,
                                  const Player& player,
                                  const GameState& state) const {
            // Friendly: self + non-demoed teammates
            float friendlyTime = TimeToReach(player, ballPos);
            for (const auto& p : state.players) {
                if (p.team != player.team || p.carId == player.carId) continue;
                if (p.isDemoed) continue;
                friendlyTime = RS_MIN(friendlyTime, TimeToReach(p, ballPos));
            }

            // Opponent: non-demoed enemies
            float opponentTime = 1e6f;
            for (const auto& p : state.players) {
                if (p.team == player.team || p.isDemoed) continue;
                opponentTime = RS_MIN(opponentTime, TimeToReach(p, ballPos));
            }

            if (opponentTime <= 0.0f) return 1e6f;
            return friendlyTime / opponentTime;  // > 1.0 → opponent is faster
        }

    public:
        virtual void PreStep(const GameState& state) override {
            _ballCache.Update(state);
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!player.ballTouchedStep) return 0.0f;

            float ballSpeed = state.ball.vel.Length();
            if (ballSpeed < MIN_BALL_SPEED) return 0.0f;

            // Find the worst-case opponent advantage across 3 prediction windows.
            // "Worst" = largest ratio (opponent is most dominant at that checkpoint).
            float worstAdvantage = 0.0f;
            const float checkTimes[3] = { CHECK_TIME_1, CHECK_TIME_2, CHECK_TIME_3 };
            for (int i = 0; i < 3; i++) {
                Vec predictedPos = _ballCache.AtTime(checkTimes[i]).pos;
                float advantage = OpponentAdvantageAt(predictedPos, player, state);
                if (advantage > worstAdvantage) worstAdvantage = advantage;
            }

            // Trigger threshold: opponent must be at least 1/ADVANTAGE_THRESHOLD faster
            const float triggerRatio = 1.0f / ADVANTAGE_THRESHOLD; // ≈ 1.538
            if (worstAdvantage < triggerRatio) return 0.0f;

            // Scale: 0.0 at threshold, 1.0 when opponent is 1.5x beyond threshold
            float penaltyScale = RS_CLAMP(
                (worstAdvantage - triggerRatio) / 1.5f,
                0.0f, 1.0f
            );
            float speedFactor = RS_CLAMP(ballSpeed / 2000.0f, 0.0f, 1.0f);

            return BASE_PENALTY * penaltyScale * speedFactor;
        }
    };

    // Reward for team spacing (not bunching up)
    class TeamSpacingReward : public Reward {
    private:
        float optimalDistance = 2500.0f;

    public:
        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float totalReward = 0.0f;
            int teammateCount = 0;

            for (auto& teammate : state.players) {
                if (teammate.carId == player.carId) continue;
                if (teammate.team != player.team) continue;

                float dist = (teammate.pos - player.pos).Length();

                // Reward for being near optimal distance
                float distanceError = abs(dist - optimalDistance);
                float spacingReward = 1.0f / (1.0f + distanceError / 1000.0f);

                totalReward += spacingReward;
                teammateCount++;
            }

            if (teammateCount == 0) return 0.0f;

            return totalReward / teammateCount;
        }
    };

    // ============================================================================
    // UpsideDownAerialReward - Rewards inverted aerial control
    // ============================================================================
    // Rewards players for performing upside-down aerials where:
    // - Car hood is pointing down (inverted orientation)
    // - Player is airborne and boosting
    // - Gaining upward velocity (+Z) while inverted
    // - Approaching the ball from below in inverted stance
    //
    // This is an advanced mechanic seen in high-level play where players
    // maintain inverted control to approach the ball from unexpected angles.
    //
    // Example: https://www.youtube.com/shorts/CU8TgiJCy_A
    //
    // Rewards are given for:
    // 1. Being inverted while airborne (+0.1 per frame)
    // 2. Boosting while inverted (+0.2 bonus)
    // 3. Gaining upward velocity while inverted (+0.3 bonus, scaled by vel)
    // 4. Approaching ball from below while inverted (+0.5 bonus, scaled by distance)
    //
    // 2025-01-09 - Rev 1.0 (NJH)
    // ============================================================================
    class UpsideDownAerialReward : public Reward {
    private:
        struct Config {
            float inverted_base_reward = 0.1f;           // Base reward for being inverted while airborne
            float boost_bonus = 0.2f;                    // Bonus for boosting while inverted
            float upward_velocity_bonus = 0.3f;          // Bonus for gaining height while inverted
            float ball_approach_bonus = 0.5f;            // Bonus for approaching ball from below while inverted

            float min_airtime = 0.3f;                    // Minimum airtime before rewarding (avoid ground flips)
            float min_height = 200.0f;                   // Minimum height above ground
            float inverted_threshold = -0.7f;            // Car up vector Z component (negative = inverted)
            float upward_vel_threshold = 100.0f;         // Minimum upward velocity to reward
            float ball_proximity_max = 2000.0f;          // Max distance to ball to get approach bonus
        };

        Config config;

    public:
        virtual void Reset(const GameState& initialState) override {}

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            // Must be airborne
            if (player.isOnGround) {
                return 0.0f;
            }

            // Must have been airborne for minimum time (avoid rewarding ground flips)
            if (player.airTime < config.min_airtime) {
                return 0.0f;
            }

            // Must be at minimum height
            if (player.pos.z < config.min_height) {
                return 0.0f;
            }

            // Check if car is inverted (hood pointing down)
            // Car up vector points upward when upright, so Z component is positive
            // When inverted, Z component is negative
            float carUpZ = player.rotMat.up.z;
            bool isInverted = carUpZ < config.inverted_threshold;

            if (!isInverted) {
                return 0.0f;
            }

            // ================================================================
            // Base reward for maintaining inverted aerial
            // ================================================================
            float reward = config.inverted_base_reward;

            // ================================================================
            // Bonus for boosting while inverted
            // ================================================================
            bool isBoosting = player.prevAction.boost > 0.5f;
            if (isBoosting && player.boost > 0.0f) {
                reward += config.boost_bonus;
            }

            // ================================================================
            // Bonus for gaining upward velocity while inverted
            // ================================================================
            float upwardVel = player.vel.z;
            if (upwardVel > config.upward_vel_threshold) {
                // Scale bonus by how fast we're going up (up to 1.0x at supersonic)
                float velRatio = RS_CLAMP(upwardVel / 2300.0f, 0.0f, 1.0f);
                reward += config.upward_velocity_bonus * velRatio;
            }

            // ================================================================
            // Bonus for approaching ball from below while inverted
            // ================================================================
            Vec toBall = state.ball.pos - player.pos;
            float distToBall = toBall.Length();

            if (distToBall > 1.0f && distToBall < config.ball_proximity_max) {
                // Check if ball is above us
                bool ballIsAbove = toBall.z > 0.0f;

                if (ballIsAbove) {
                    // Check if we're moving toward the ball
                    Vec dirToBall = toBall / distToBall;
                    float approachAlignment = player.vel.Dot(dirToBall);

                    if (approachAlignment > 0.0f) {
                        // Scale bonus by proximity (closer = better) and alignment
                        float proximityFactor = 1.0f - RS_CLAMP(distToBall / config.ball_proximity_max, 0.0f, 1.0f);
                        float alignmentFactor = RS_CLAMP(approachAlignment / 2300.0f, 0.0f, 1.0f);

                        reward += config.ball_approach_bonus * proximityFactor * alignmentFactor;
                    }
                }
            }

            // ================================================================
            // Additional bonus for hood pointing at ball (precise control)
            // ================================================================
            if (distToBall > 1.0f && distToBall < config.ball_proximity_max) {
                Vec dirToBall = toBall / distToBall;

                // Car hood is the negative up vector (since we're inverted)
                Vec hoodDir = player.rotMat.up * -1.0f;
                float hoodAlignment = hoodDir.Dot(dirToBall);

                if (hoodAlignment > 0.7f) {
                    // Bonus for pointing hood directly at ball
                    reward += 0.15f * hoodAlignment;
                }
            }

            return reward;
        }
    };

} // namespace RLGC