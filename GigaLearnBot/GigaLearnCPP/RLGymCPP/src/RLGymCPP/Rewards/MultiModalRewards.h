#pragma once
#include "../Framework.h"
#include "../CommonValues.h"
#include "BallPrediction.h"
#include <atomic>
#include <cmath>

namespace RLGC {

    // ============================================================================
    // DOUBLE COMMIT PENALTY - Penalize second/third man ball-chasing
    // ============================================================================
    // In multi-modal training (2v2, 3v3), penalize any player who commits to the
    // ball when a closer teammate is already first man. Returns 0 in 1v1 (no
    // teammates to double-commit with).
    //
    // Rev 2.0 (2026-04-26, NJH + Claude) — fixes from Rev 1.0:
    //   - BUG FIX: commitCount > 1 → >= 1 (was dead in 2v2 — only 1 teammate exists)
    //   - First-man exemption: closest player to ball on team is immune
    //   - Emergency defensive gate: no penalty when ball is near own goal
    //   - Tighter commit detection: cos 0.3→0.5 (72°→60°), speed 300→500 UU/s
    //   - Penalty formula: (commitCount-1) → commitCount (counts all extra bots)
    //   - Added Config struct, Metrics, W&B atomics
    // ============================================================================
    class DoubleCommitPenalty : public Reward {
    public:
        struct Config {
            float commitDistance       = 1500.0f; // radius: teammate within this of ball counts
            float penaltyPerBot        = 0.1f;    // penalty per committing teammate
            float minDistBeforePenalty = 300.0f;  // no penalty if player is basically on ball
            float commitThreshold      = 0.5f;    // seconds of overlap before penalty fires
            float minCommitSpeed       = 500.0f;  // min speed to count as committing (was 300)
            float commitAlignCos       = 0.5f;    // cos(angle) threshold — 60° cone (was 0.3 = 72°)
            float emergencyGoalDist    = 1000.0f; // ball within this of own goal → no penalty
            bool  shadowMode           = false;
        };

        struct Metrics {
            int    penaltyFrames  = 0;   // frames where penalty was applied to this player
            int    commitCountSum = 0;   // sum of commitCounts at penalty frames (÷penaltyFrames = avg)
            double penaltySum     = 0.0; // total penalty magnitude issued
        };

    private:
        mutable std::map<uint32_t, float> commitStartTime;
        Config config;

        // Metrics — thread-safe atomics (shared across all instances / all parallel envs)
        inline static std::atomic<int>       s_penaltyFrames  { 0 };
        inline static std::atomic<int>       s_commitCountSum { 0 };
        inline static std::atomic<long long> s_penaltyMilli   { 0 };

    public:
        DoubleCommitPenalty() : config() {}
        explicit DoubleCommitPenalty(const Config& cfg) : config(cfg) {}

        // Legacy positional constructor — preserves all existing RewardConfig.cpp call sites
        DoubleCommitPenalty(float distance, float penalty, float minDist, float threshold)
            : config() {
            config.commitDistance       = distance;
            config.penaltyPerBot        = penalty;
            config.minDistBeforePenalty = minDist;
            config.commitThreshold      = threshold;
        }

        static Metrics GetAndResetMetrics() {
            Metrics m;
            m.penaltyFrames  = s_penaltyFrames.exchange(0);
            m.commitCountSum = s_commitCountSum.exchange(0);
            long long pm     = s_penaltyMilli.exchange(0);
            m.penaltySum     = double(pm) / 1000.0;
            return m;
        }

        virtual void Reset(const GameState& initialState) override {
            commitStartTime.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            // 1v1: no teammates, penalty never applies
            // (commitCount will be 0 naturally, but early exit is cleaner)
            bool hasTeammate = false;
            for (const auto& p : state.players) {
                if (p.team == player.team && p.carId != player.carId) { hasTeammate = true; break; }
            }
            if (!hasTeammate) return 0.0f;

            float distToBall = (state.ball.pos - player.pos).Length();
            if (distToBall < config.minDistBeforePenalty) return 0.0f;

            // Emergency defensive gate: everyone committing near own goal is correct
            Vec ownGoal = (player.team == Team::BLUE)
                ? CommonValues::BLUE_GOAL_CENTER
                : CommonValues::ORANGE_GOAL_CENTER;
            if ((state.ball.pos - ownGoal).Length() < config.emergencyGoalDist &&
                state.ball.pos.z < 400.0f)
                return 0.0f;

            // First-man exemption: if this player is the closest to ball on their team,
            // their commit is correct — penalize them would be wrong
            float myDistToBall = distToBall;
            for (const auto& tm : state.players) {
                if (tm.team != player.team || tm.carId == player.carId || tm.isDemoed) continue;
                if ((tm.pos - state.ball.pos).Length() < myDistToBall)
                    goto has_closer_teammate;
            }
            // No teammate is closer — this player IS the first man, commitment is valid
            { commitStartTime.erase(player.carId); return 0.0f; }
            has_closer_teammate:

            // Commit detection: tighter cone (0.5 = 60°) and higher speed floor (500 UU/s)
            auto IsActivelyCommitting = [&](const Player& p) -> bool {
                if (p.isDemoed) return false;
                Vec toBallTm = (state.ball.pos - p.pos);
                if (toBallTm.Length() < 1e-3f) return true;
                float speed = p.vel.Length();
                if (speed < config.minCommitSpeed) return false;
                return p.vel.Normalized().Dot(toBallTm.Normalized()) > config.commitAlignCos;
            };

            // Count teammates also committing (these are additional first-men, also wrong)
            int commitCount = 0;
            for (const auto& tm : state.players) {
                if (tm.team != player.team || tm.carId == player.carId || tm.isDemoed) continue;
                float tmDist = (state.ball.pos - tm.pos).Length();
                if (tmDist < config.commitDistance && IsActivelyCommitting(tm))
                    commitCount++;
            }

            float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
            uint32_t playerId = player.carId;

            if (commitCount >= 1) {  // was > 1 — this was the 2v2 dead-code bug
                commitStartTime[playerId] += dt;

                if (commitStartTime[playerId] > config.commitThreshold) {
                    float penalty = config.penaltyPerBot * commitCount;  // was (commitCount-1)

                    s_penaltyFrames.fetch_add(1);
                    s_commitCountSum.fetch_add(commitCount);
                    s_penaltyMilli.fetch_add(static_cast<long long>(penalty * 1000.0f));

                    return config.shadowMode ? 0.0f : -penalty;
                }
            } else {
                commitStartTime.erase(playerId);
            }

            return 0.0f;
        }
    };

    // NOTE: FieldSpacingReward is already defined in CompleteMechanicsRewards.h
    // Use that implementation for multi-modal training


    // ============================================================================
    // MIDFIELD ROTATION REWARD - Return to midfield after offensive play
    // ============================================================================
    // In multi-team scenarios, bots should rotate back to midfield after
    // committing offensively, especially when:
    // - Low on boost (can't chase efficiently)
    // - Far from ball (not helping offense)
    // - Ball is in own half (defensive situation)
    //
    // Immediate reward: +0.15 per second moving toward midfield when criteria met
    // Bonus: +0.5 event reward when successfully reaching midfield
    //
    // Avoids vanishing gradients by:
    // 1. Continuous reward (0.15/s) for every frame moving in right direction
    // 2. Clear immediate feedback on heading toward goal
    // 3. Event bonus (0.5) when reaching midfield zone
    //
    // Rev 1.0 (2025-01-15, NJH)
    class MiddleRotationReward : public Reward {
    public:
        float boostThreshold;      // Below this boost level, should rotate back
        float ballDistThreshold;   // If farther than this from ball, consider rotating
        float midlineY;            // Y-coordinate of midline (use 0.0 for center)
        float rotationRewardRate;  // Reward per frame when rotating appropriately
        float midlineReachBonus;   // One-time bonus for reaching midfield

        mutable std::unordered_map<uint32_t, bool> wasInOffensiveZone;
        mutable std::unordered_map<uint32_t, float> lastReachReward;

        MiddleRotationReward(
            float boost = 30.0f,
            float ballDist = 3000.0f,
            float midY = 0.0f,
            float rotRate = 0.15f,
            float reachBonus = 0.5f
        ) : boostThreshold(boost),
            ballDistThreshold(ballDist),
            midlineY(midY),
            rotationRewardRate(rotRate),
            midlineReachBonus(reachBonus) {
        }

        bool IsInOffensiveZone(const Player& player, const GameState& state) const {
            Vec ownGoal = (player.team == Team::BLUE)
                ? CommonValues::BLUE_GOAL_CENTER
                : CommonValues::ORANGE_GOAL_CENTER;

            float distToOwnGoal = (player.pos - ownGoal).Length();

            // Offensive zone: far from own goal, pushing forward
            return distToOwnGoal > 3000.0f;
        }

        bool ShouldRotate(const Player& player, const GameState& state) const {
            // Low on boost OR far from ball = should rotate back
            float distToBall = (player.pos - state.ball.pos).Length();

            return (player.boost < boostThreshold) || (distToBall > ballDistThreshold);
        }

        bool IsInMidfield(const Player& player, const GameState& state) const {
            Vec ownGoal = (player.team == Team::BLUE)
                ? CommonValues::BLUE_GOAL_CENTER
                : CommonValues::ORANGE_GOAL_CENTER;

            float distToOwnGoal = (player.pos - ownGoal).Length();

            // Midfield: roughly 2000-4000 units from own goal
            return distToOwnGoal >= 1500.0f && distToOwnGoal <= 4000.0f;
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            bool currentlyOffensive = IsInOffensiveZone(player, state);
            bool shouldRotateNow = ShouldRotate(player, state);
            bool inMidfield = IsInMidfield(player, state);

            // Track state transitions
            if (currentlyOffensive) {
                wasInOffensiveZone[player.carId] = true;
            }

            float reward = 0.0f;

            // IMMEDIATE CONTINUOUS REWARD: If we were offensive and should rotate, reward rotation
            if (wasInOffensiveZone[player.carId] && shouldRotateNow) {
                // Reward based on movement toward own goal
                Vec ownGoal = (player.team == Team::BLUE)
                    ? CommonValues::BLUE_GOAL_CENTER
                    : CommonValues::ORANGE_GOAL_CENTER;

                Vec toGoal = (ownGoal - player.pos).Normalized();
                float goalwheelVelocity = player.vel.Dot(toGoal);

                // Positive velocity toward goal = reward
                if (goalwheelVelocity > 500.0f) {
                    reward = rotationRewardRate;  // Continuous reward every frame
                }
            }

            // EVENT BONUS: Reaching midfield from offensive zone
            if (wasInOffensiveZone[player.carId] && inMidfield) {
                // One-time bonus per rotation cycle
                if (lastReachReward[player.carId] < state.deltaTime - 5.0f) {
                    reward += midlineReachBonus;
                    lastReachReward[player.carId] = state.deltaTime;
                    wasInOffensiveZone[player.carId] = false;  // Reset cycle
                }
            }

            return reward;
        }

        virtual void Reset(const GameState& initialState) override {
            wasInOffensiveZone.clear();
            lastReachReward.clear();
        }
    };


    // ============================================================================
    // GOOD RECOVERY REWARD - Landing on wheels
    // ============================================================================
    // Rewards landing smoothly on 2+ wheels after airtime, with a slight bonus
    // for wavedashing. Focus is on clean recoveries without flip-spamming.
    //
    // Immediate reward structure:
    // - +0.3 for landing on 2+ wheels (main reward)
    // - +0.05 small bonus for wavedashing during recovery
    // - -0.1 if landing on <2 wheels (unstable landing)
    //
    // Avoids vanishing gradients by:
    // 1. Immediate reward on landing (same frame as contact)
    // 2. No flip incentive - prevents flip-spamming behavior
    // 3. Slight wavedash bonus - encourages skill without forcing it
    //
    // Rev 2.0 (2025-01-15, NJH) - Simplified to prevent flip spam
    class GoodRecoveryReward : public Reward {
    public:
        mutable std::unordered_map<uint32_t, bool> wasInAir;

        GoodRecoveryReward() {
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            float reward = 0.0f;

            if (player.isOnGround) {
                // Check if we just landed from air
                if (wasInAir[player.carId]) {
                    // We were in air, now on ground - landing event!
                    wasInAir[player.carId] = false;

                    // Count wheels in contact
                    int wheelsInContact = 0;
                    if (player.wheelsWithContact[0]) wheelsInContact++;
                    if (player.wheelsWithContact[1]) wheelsInContact++;
                    if (player.wheelsWithContact[2]) wheelsInContact++;
                    if (player.wheelsWithContact[3]) wheelsInContact++;

                    // MAIN REWARD: 2+ wheels = good landing, <2 = poor landing
                    if (wheelsInContact >= 2) {
                        reward += 0.3f;  // Smooth stable landing on multiple wheels
                    } else {
                        reward -= 0.1f;  // Unstable landing on 0-1 wheels
                    }

                    // SLIGHT BONUS: Wavedashing (but not flip spam)
                    // Wavedashing is detected by handbrake + jump state
                    // hasFlipped here means we DID flip, so we skip this bonus
                    if (!player.hasFlipped && player.prevAction.handbrake > 0.5f) {
                        // Small bonus for wavedashing (not flipping)
                        if (wheelsInContact >= 2) {
                            reward += 0.05f;
                        }
                    }
                }
            } else {
                // In air
                wasInAir[player.carId] = true;
            }

            return reward;
        }

        virtual void Reset(const GameState& initialState) override {
            wasInAir.clear();
        }
    };


    // ============================================================================
    // TEAMMATE BUMP PENALTY - Penalize bumping teammates
    // ============================================================================
    // In multi-modal training (2v2, 3v3), teammates are allies and bumping them
    // disrupts team coordination and wastes momentum. This penalty teaches respect
    // for teammate positioning and avoids chaotic collisions.
    //
    // Immediate punishment: -0.5 whenever you bump a teammate
    // Continuous effect: Applied every frame a bump happens
    //
    // Rev 1.0 (2025-01-15, NJH)
    class TeammteBumpPenalty : public Reward {
    public:
        float penaltyAmount;

        TeammteBumpPenalty(float penalty = 0.5f) : penaltyAmount(penalty) {
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            // Check if we bumped a teammate
            // The bump event is recorded in player.eventState.bump
            // But we need to verify it was against a teammate
            if (!player.eventState.bump) return 0.0f;

            // Find which player we bumped
            // We look for a teammate who was bumped in this frame
            for (const auto& other : state.players) {
                if (other.team != player.team) continue;  // Must be a teammate
                if (other.carId == player.carId) continue;  // Not ourselves
                if (!other.eventState.bumped) continue;  // They must have been bumped

                // Check if we're close enough that we could have bumped them
                float distToOther = (player.pos - other.pos).Length();
                if (distToOther < 300.0f) {  // Collision distance threshold
                    // We bumped a teammate - apply penalty
                    return -penaltyAmount;
                }
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // PASSING SETUP REWARD - Reward setting up a pass trajectory
    // ============================================================================
    // This reward encourages:
    // 1. Ball velocity moving toward a teammate (not opponent)
    // 2. Teammate closer to ball destination than opponents
    // 3. Clean, predictable ball trajectories (not crazy bounces)
    //
    // Strategy:
    // - Compute ball velocity direction
    // - Check if velocity points toward teammate space (not opponent space)
    // - Trace ball trajectory 1-2 bounces ahead
    // - Reward if teammates are in better position than opponents along trajectory
    // - Light reward for just creating good ball velocity toward team half
    //
    // Reward structure:
    // - +2.0: Ball moving toward teammate, teammate closer than closest opponent
    // - +0.8: Ball moving toward team half even if no clear teammate position yet
    // - +1.5: Bonus when pass trajectory avoids heavy wall/ceiling bounces
    // - -0.2: Penalty if ball moving toward opponent space
    //
    // Rev 1.0 (2025-01-21, NJH)
    // Rev 2.0 (2026-02-18, NJH) - Migrated to BallPred::Cache
    class PassingSetupReward : public Reward {
    private:
        BallPred::Cache _ballCache;

        bool IsTeammatePosition(const Vec& pos, const Player& player, const GameState& state, float proximityRadius = 1500.0f) const {
            // Check if any teammate is close to this position
            for (const auto& teammate : state.players) {
                if (teammate.team != player.team) continue;
                if (teammate.carId == player.carId) continue;
                if (teammate.isDemoed) continue;

                float distToPos = (teammate.pos - pos).Length();
                if (distToPos < proximityRadius) {
                    return true;
                }
            }
            return false;
        }

        float GetTeammateDistToPos(const Vec& pos, const Player& player, const GameState& state) const {
            float minDist = 1e6f;
            for (const auto& teammate : state.players) {
                if (teammate.team != player.team) continue;
                if (teammate.carId == player.carId) continue;
                if (teammate.isDemoed) continue;

                float dist = (teammate.pos - pos).Length();
                minDist = RS_MIN(minDist, dist);
            }
            return minDist;
        }

        float GetOpponentDistToPos(const Vec& pos, const Player& player, const GameState& state) const {
            float minDist = 1e6f;
            for (const auto& opponent : state.players) {
                if (opponent.team == player.team) continue;
                if (opponent.isDemoed) continue;

                float dist = (opponent.pos - pos).Length();
                minDist = RS_MIN(minDist, dist);
            }
            return minDist;
        }

        // NEW: Calculate optimal pass zone between teammate and goal
        Vec GetOptimalPassZone(const Vec& teammatePpos, const Vec& goalPos, const Vec& teamVel, float boost) const {
            // Direction from teammate toward goal
            Vec towardGoal = (goalPos - teammatePpos).Normalized();

            // Teammate velocity direction (where they're moving)
            Vec movementDir = teamVel.Length() > 100.0f ? teamVel.Normalized() : towardGoal;

            // Blend: If teammate moving toward goal, pass can be further ahead
            // If teammate moving sideways/backward, pass should be closer
            float alignmentToGoal = RS_MAX(0.0f, movementDir.Dot(towardGoal));  // 0-1

            // Boost factor: High boost (100) = pass can be further (zone closer to goal)
            // Low boost (0) = pass should be closer to teammate (easier reception)
            float boostFactor = boost / 100.0f;  // 0-1

            // Optimal distance factor: blend movement alignment and boost
            // High alignment + high boost = pass closer to goal (0.7-1.0)
            // Low alignment + low boost = pass closer to teammate (0.3-0.5)
            float optimalDistanceFactor = 0.4f + (alignmentToGoal * 0.3f) + (boostFactor * 0.3f);

            // Return position in the zone
            float distToGoal = (goalPos - teammatePpos).Length();
            return teammatePpos + towardGoal * (distToGoal * optimalDistanceFactor);
        }

        // NEW: Evaluate how well pass zone matches actual pass
        float EvaluatePassZoneAccuracy(const Vec& predictedBallPos, const Vec& optimalZone,
                                       const Vec& teamPos, const Vec& goalPos) const {
            // How close is predicted ball to optimal zone?
            float distToOptimal = (predictedBallPos - optimalZone).Length();

            // Radius of acceptable zone: 400 units (larger zone = more forgiving)
            float zoneRadius = 400.0f;

            if (distToOptimal <= zoneRadius) {
                // Perfect zone hit: scale from 1.0 (at center) to 0.3 (at edge)
                return 1.0f - (distToOptimal / zoneRadius) * 0.7f;
            } else {
                // Outside zone: still small bonus if not too far
                if (distToOptimal <= zoneRadius * 2.0f) {
                    return 0.3f * (1.0f - ((distToOptimal - zoneRadius) / zoneRadius) * 0.5f);
                }
                return 0.0f;
            }
        }

    public:
        PassingSetupReward() {}

        virtual void PreStep(const GameState& state) override {
            _ballCache.Update(state);
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            // Only reward if WE just touched the ball (setting up the pass)
            if (!player.ballTouchedStep) return 0.0f;

            // Check ball velocity direction
            Vec ballVel = state.ball.vel;
            if (ballVel.Length() < 400.0f) return 0.0f;  // Ball too slow to be a pass (was 100)

            Vec ballVelNorm = ballVel.Normalized();

            // Get our goal (which direction is "forward" for us)
            Vec ourGoal = (player.team == Team::BLUE)
                ? CommonValues::BLUE_GOAL_CENTER
                : CommonValues::ORANGE_GOAL_CENTER;

            Vec theirGoal = (player.team == Team::BLUE)
                ? CommonValues::ORANGE_GOAL_CENTER
                : CommonValues::BLUE_GOAL_CENTER;

            // Compute goal direction from ball position, not player position.
            // Ball velocity is relative to ball, so the reference point should be ball too.
            Vec towardTheirGoal = (theirGoal - state.ball.pos).Normalized();

            // Use the pre-computed ball prediction cache (populated in PreStep).
            // Check for surface redirects across the full prediction window.
            bool isSurfacePass = _ballCache.HitAnySurface();

            // Check if ball is moving generally toward their goal (not back to our goal).
            // For surface-bounce passes the ball may initially travel upward or toward our
            // back wall, so only penalize if there is no surface redirect predicted.
            float goalwardComponent = ballVelNorm.Dot(towardTheirGoal);
            if (goalwardComponent < 0.2f && !isSurfacePass) {
                // Ball moving backward and no surface redirect - not a good pass setup
                return -0.2f;
            }

            // Use the slice at ~1.5 seconds as the "landing zone" for pass evaluation.
            // This is close enough to where a typical pass will reach a teammate.
            Vec predictedBallPos = _ballCache.AtTime(1.5f).pos;

            // Check if teammates are better positioned than opponents at ball destination
            float tmDist = GetTeammateDistToPos(predictedBallPos, player, state);
            float oppDist = GetOpponentDistToPos(predictedBallPos, player, state);

            float reward = 0.0f;

            // PRIMARY REWARD: Ball trajectory favors teammates
            if (tmDist < 1e6f && oppDist < 1e6f) {
                if (tmDist < oppDist - 500.0f) {
                    // Teammate is significantly closer - good pass setup!
                    reward += 2.0f;
                }
                else if (tmDist < oppDist) {
                    // Teammate slightly closer - decent pass
                    reward += 1.0f;
                }
            }

            // SECONDARY REWARD: Ball moving toward team half (even if no clear receiver)
            Vec teamHalfDir = (theirGoal - state.ball.pos).Normalized();
            if (ballVelNorm.Dot(teamHalfDir) > 0.5f) {
                reward += 0.8f;
            }

            // TRAJECTORY QUALITY BONUS: reward clean passes and intentional surface redirects.
            if (isSurfacePass) {
                // Intentional wall/ceiling/backboard redirect ending near a teammate
                if (tmDist < 2000.0f) {
                    reward += 1.0f;  // Surface-redirect pass with receiver in range
                }
            } else {
                // Direct / lob pass quality: count ground bounces within the first 2s only.
                // The full _ballCache.groundBounces spans 4s — bounces after reception are irrelevant.
                int earlyGroundBounces = 0;
                for (int i = 0; i < BallPred::NUM_SLICES; i++) {
                    if (_ballCache.slices[i].t > 2.0f) break;
                    if (_ballCache.slices[i].hitGround &&
                        (i == 0 || !_ballCache.slices[i - 1].hitGround)) {
                        earlyGroundBounces++;
                    }
                }
                if (earlyGroundBounces == 0) {
                    reward += 1.5f;  // Clean lob with no bounces - excellent!
                } else if (earlyGroundBounces == 1) {
                    reward += 0.8f;  // One ground bounce - acceptable
                }
                // earlyGroundBounces >= 2: chaotic, no bonus
            }

            // Evaluate pass zone accuracy
            float zoneAccuracyBonus = 0.0f;
            for (const auto& teammate : state.players) {
                if (teammate.team != player.team || teammate.carId == player.carId) continue;
                if (teammate.isDemoed) continue;

                Vec opponentGoal = theirGoal;

                Vec optimalZone = GetOptimalPassZone(teammate.pos, opponentGoal,
                                                     teammate.vel, teammate.boost);

                float zoneScore = EvaluatePassZoneAccuracy(predictedBallPos, optimalZone,
                                                           teammate.pos, opponentGoal);

                float bonus = 0.5f * zoneScore;

                // Weight by teammate quality (favor passes to teammates with more boost)
                float boostWeight = 1.0f + (teammate.boost / 100.0f) * 0.2f;  // 1.0-1.2x
                bonus *= boostWeight;

                // Low-boost teamplay multiplier: encourage passing when low on boost
                float lowBoostTeamplayMultiplier = 1.5f - (player.boost / 100.0f) * 0.5f;  // 1.0-1.5x
                bonus *= lowBoostTeamplayMultiplier;

                zoneAccuracyBonus = RS_MAX(zoneAccuracyBonus, bonus);
            }

            if (zoneAccuracyBonus > 0.0f) {
                reward += zoneAccuracyBonus;
            }

            return reward;
        }
    };


    // ============================================================================
    // RECEIVING PASS REWARD - Reward receiving a good pass from teammate
    // ============================================================================
    // This reward encourages:
    // 1. Positioning to receive passes (being in teammate passing lanes)
    // 2. Touching the ball when it's already moving toward you (receiving)
    // 3. Maintaining good position relative to ball trajectory
    //
    // Strategy:
    // - Track ball velocity and which team member moved it last
    // - If teammate moved ball toward us, reward our positioning/touch
    // - Reward based on how cleanly we receive (ball speed alignment)
    // - Bonus for converting receive into a shot or dribble setup
    //
    // Reward structure:
    // - +1.5: Touching ball that teammate passed toward you
    // - +0.5: Good positioning relative to incoming pass trajectory
    // - +2.0: Successfully converting receive into immediate shot/play
    // - -0.3: Missing a pass opportunity (ball flies past without touch)
    //
    // Rev 1.0 (2025-01-21, NJH)
    class ReceivingPassReward : public Reward {
    private:
        mutable std::unordered_map<uint32_t, uint32_t> lastBallTouchBy;
        mutable std::unordered_map<uint32_t, float> passReceiveTime;
        mutable std::unordered_map<uint32_t, bool> recentPassToMe;
        mutable std::unordered_map<uint32_t, bool> surfacePassActive;  // Pass via wall/ceiling/backboard
        mutable std::unordered_map<uint32_t, float> elapsedTime;

        // Proximity thresholds for surface detection (matching AdvancedPlayRewards.h patterns)
        static constexpr float WALL_NEAR_THRESH = 300.0f;
        static constexpr float CEIL_NEAR_THRESH = 200.0f;
        static constexpr float BACK_NEAR_THRESH = 400.0f;
        static constexpr float BACK_MIN_Z       = 300.0f;

        bool IsBallNearSurface(const Vec& ballPos) const {
            bool nearSideWall   = std::abs(ballPos.x) > (CommonValues::SIDE_WALL_X - WALL_NEAR_THRESH);
            bool nearCeiling    = ballPos.z > (CommonValues::CEILING_Z - CEIL_NEAR_THRESH);
            bool nearBackboard  = std::abs(ballPos.y) > (CommonValues::BACK_WALL_Y - BACK_NEAR_THRESH)
                                  && ballPos.z > BACK_MIN_Z;
            return nearSideWall || nearCeiling || nearBackboard;
        }

    public:
        ReceivingPassReward() {}

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            uint32_t id = player.carId;
            elapsedTime[id] += state.deltaTime;

            float reward = 0.0f;

            // TRACK WHO LAST TOUCHED THE BALL
            for (const auto& p : state.players) {
                if (p.ballTouchedStep && p.team == player.team && p.carId != player.carId) {
                    lastBallTouchBy[0] = p.carId;  // Teammate touched ball
                }
            }

            // Check if this is a pass intended for us
            if (lastBallTouchBy[0] != 0 && lastBallTouchBy[0] != player.carId) {
                Vec ballVel = state.ball.vel;
                if (ballVel.Length() > 200.0f) {
                    Vec toUs = (player.pos - state.ball.pos).Normalized();
                    float towardComponent = ballVel.Normalized().Dot(toUs);

                    // For surface-bounce passes the ball may be heading toward a wall/ceiling,
                    // not directly at us yet.  Relax the threshold when ball is near a surface.
                    bool nearSurface = IsBallNearSurface(state.ball.pos);
                    float dirThreshold = nearSurface ? 0.1f : 0.3f;

                    if (towardComponent > dirThreshold) {
                        recentPassToMe[id] = true;
                        surfacePassActive[id] = nearSurface;
                        passReceiveTime[id] = elapsedTime[id];
                    }
                }
            }

            // REWARD 1: Positioning relative to incoming pass
            if (recentPassToMe[id]) {
                Vec ballVel = state.ball.vel;
                if (ballVel.Length() > 200.0f) {
                    Vec theirGoal = (player.team == Team::BLUE)
                        ? CommonValues::ORANGE_GOAL_CENTER
                        : CommonValues::BLUE_GOAL_CENTER;
                    Vec towardGoal = (theirGoal - player.pos).Normalized();

                    Vec ballVelNorm = ballVel.Normalized();
                    float goalwardComponent = ballVelNorm.Dot(towardGoal);

                    // Reward receiver who is moving TOWARD the ball (approach alignment),
                    // not just receivers moving in the same direction as the ball (old behavior
                    // penalized players correctly sprinting INTO an incoming pass).
                    Vec toBall = (state.ball.pos - player.pos).Normalized();
                    float approachAlignment = player.vel.Length() > 100.0f
                        ? player.vel.Normalized().Dot(toBall)
                        : 1.0f;  // stationary receiver = fine (they'll flip/dodge to it)

                    if (goalwardComponent > 0.3f && approachAlignment > 0.1f) {
                        reward += 0.5f;

                        float boostBonus = (player.boost / 100.0f) * 0.3f;
                        reward += boostBonus;
                    }
                }
            }

            // REWARD 2: Receiving the pass
            if (player.ballTouchedStep && recentPassToMe[id]) {
                float timeSincePass = elapsedTime[id] - passReceiveTime[id];

                // Surface-bounce passes take longer to arrive; allow a wider window
                float receiveWindow = surfacePassActive[id] ? 2.5f : 1.0f;

                if (timeSincePass < receiveWindow) {
                    reward += 1.5f;  // Clean reception

                    // Extra bonus for successfully receiving a surface-bounce pass
                    if (surfacePassActive[id]) {
                        reward += 0.5f;
                    }

                    // BONUS: Converting receive into offense (threshold lowered 2000→1400 UU/s;
                    // most good first-touch shots are 1300-1800 UU/s, 2000 was rarely reached)
                    Vec ballVel = state.ball.vel;
                    if (ballVel.Length() > 1400.0f) {
                        float boostBonus = (player.boost / 100.0f) * 0.5f;
                        reward += 2.0f + boostBonus;
                    }

                    recentPassToMe[id] = false;
                    surfacePassActive[id] = false;
                }
            }

            // PENALTY: Missing a pass opportunity
            if (recentPassToMe[id]) {
                float timeSincePass = elapsedTime[id] - passReceiveTime[id];

                // Surface passes need more time before we declare a miss.
                // Direct pass window raised 0.5→1.0s: a ball at 1000 UU/s takes ~1.5s to
                // travel 1500 UU — 0.5s was declaring misses before the ball even arrived.
                float missWindow = surfacePassActive[id] ? 2.0f : 1.0f;

                if (timeSincePass > missWindow) {
                    float distToBall = (player.pos - state.ball.pos).Length();

                    if (distToBall > 2000.0f) {
                        reward -= 0.3f;
                        recentPassToMe[id] = false;
                        surfacePassActive[id] = false;
                    }
                }
            }

            return reward;
        }

        virtual void Reset(const GameState& initialState) override {
            lastBallTouchBy.clear();
            passReceiveTime.clear();
            recentPassToMe.clear();
            surfacePassActive.clear();
            elapsedTime.clear();
        }
    };


    // ============================================================================
    // PASSING ACCURACY REWARD - Reward passes that reach intended receiver
    // ============================================================================
    // This reward is designed to be used alongside PassingSetupReward.
    // While PassingSetupReward rewards the INTENTION (setting up a good trajectory),
    // this reward tracks the EXECUTION (did the pass actually get to a teammate?).
    //
    // Strategy:
    // - After you touch ball, track if a teammate touches it within 2 seconds
    // - Reward based on how efficiently the pass sequence happened
    // - Bonus if pass leads to a shot
    //
    // Reward structure:
    // - +3.0: Teammate receives pass (ball goes player->teammate)
    // - +5.0: Pass leads directly to a shot (teammate scores/shoots within 0.5s)
    // - +1.0: Partial credit if teammate gets 2nd touch (possession chain)
    //
    // Rev 1.0 (2025-01-21, NJH)
    class PassingAccuracyReward : public Reward {
    private:
        mutable std::unordered_map<uint32_t, float> lastPassTime;
        mutable std::unordered_map<uint32_t, uint32_t> passedFromPlayer;  // Who initiated pass
        mutable std::unordered_map<uint32_t, float> elapsedTime;  // Track time manually

    public:
        PassingAccuracyReward() {}

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            // Update elapsed time
            uint32_t id = player.carId;
            elapsedTime[id] += state.deltaTime;

            float reward = 0.0f;

            // TRACK: Did we just touch the ball?
            if (player.ballTouchedStep) {
                lastPassTime[player.carId] = elapsedTime[id];
                passedFromPlayer[player.carId] = player.carId;
            }

            // CHECK: Did a teammate receive our pass?
            if (player.ballTouchedStep && passedFromPlayer[0] != player.carId) {
                // We're receiving a pass from a teammate
                uint32_t passerCarId = passedFromPlayer[0];
                float timeSincePass = elapsedTime[id] - lastPassTime[passerCarId];

                if (timeSincePass < 2.0f && timeSincePass > 0.1f) {
                    // Within ~2 second window - this is a successful pass!
                    reward += 3.0f;

                    // Check if this is a setup for a shot
                    // (Is the ball now high speed and toward goal?)
                    Vec theirGoal = (player.team == Team::BLUE)
                        ? CommonValues::ORANGE_GOAL_CENTER
                        : CommonValues::BLUE_GOAL_CENTER;

                    Vec toGoal = (theirGoal - player.pos).Normalized();
                    float goalwardSpeed = state.ball.vel.Dot(toGoal);

                    if (goalwardSpeed > 1500.0f) {
                        // Pass led to immediate offensive play!
                        reward += 1.0f;
                    }
                }
            }

            return reward;
        }

        virtual void Reset(const GameState& initialState) override {
            lastPassTime.clear();
            passedFromPlayer.clear();
            elapsedTime.clear();
        }
    };

} // namespace RLGC
