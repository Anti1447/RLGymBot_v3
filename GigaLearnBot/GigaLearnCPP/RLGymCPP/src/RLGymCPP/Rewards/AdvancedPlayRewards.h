#pragma once

#include "Reward.h"
#include "../CommonValues.h"
#include <map>
#include <deque>
#include <atomic>
#include <cmath>

namespace RLGC {

// ============================================================================
// ADVANCED PLAY REWARDS SYSTEM
// ============================================================================
// This file implements rewards for advanced offensive plays:
// 1. Double Taps - Backboard/wall/ceiling → goal (ENHANCED beyond existing)
// 2. Wall Passes - Wall hit → teammate/self positioning
// 3. Wall to Aerial - Wall launch → air dribble/power shot/backboard setup
//
// Existing implementations found:
// - EnhancedDoubleTapReward (CompleteMechanicsRewards.h:5040)
// - DoubleTapReward (advanced_mechanic_rewards.cpp:72)
// - BackboardReboundReward (CompleteMechanicsRewards.h:534)
// - WallTouchReward, WallHitReward, WallPopReward, WallShotReward
// - AirDribbleAccuracyReward (AirDribbleAccuracyReward.h)
//
// This system COMPLEMENTS existing rewards with:
// - More sophisticated double tap detection (setup quality, angle variety)
// - Wall pass detection (to self AND teammates)
// - Wall-to-aerial transition rewards (launch quality, aerial control)
// ============================================================================

// ============================================================================
// AdvancedDoubleTapReward
// Enhanced double tap detection focusing on QUALITY and VARIETY
// - Tracks setup quality (touch speed, angle, positioning)
// - Rewards different surfaces: backboard (best), ceiling, side walls
// - Progressive rewards: setup touch → surface contact → finish touch
// - Bonus for clean execution, speed, and goal conversion
// ============================================================================
class AdvancedDoubleTapReward : public Reward {
private:
    struct DoubleTapSequence {
        enum Phase {
            NONE,
            SETUP_TOUCH,      // First touch toward surface
            SURFACE_CONTACT,  // Ball hit surface
            FINISH_TOUCH,     // Second touch toward goal
            COMPLETED
        };

        Phase phase = NONE;
        float phaseStartTime = 0.0f;
        Vec setupTouchPos;
        Vec surfaceContactPos;
        float setupSpeed = 0.0f;
        bool setupWasAerial = false;
        bool finishWasAerial = false;

        enum SurfaceType {
            BACKBOARD,  // Most valuable
            CEILING,    // High skill
            SIDE_WALL   // Common but effective
        };
        SurfaceType surface = BACKBOARD;
    };

    mutable std::map<uint32_t, DoubleTapSequence> sequences;
    mutable float currentTime = 0.0f;

    // Timeouts
    const float SETUP_TO_SURFACE_TIMEOUT = 1.5f;
    const float SURFACE_TO_FINISH_TIMEOUT = 2.5f;
    const float TOTAL_SEQUENCE_TIMEOUT = 4.0f;

    // Surface detection thresholds
    const float BACKBOARD_DIST = 300.0f;
    const float CEILING_DIST = 250.0f;
    const float SIDE_WALL_DIST = 300.0f;

    // Metrics
    inline static std::atomic<int> s_setupCount{0};
    inline static std::atomic<int> s_finishCount{0};
    inline static std::atomic<int> s_goalCount{0};
    inline static std::atomic<long long> s_rewardMilli{0};

    bool IsNearBackboard(const Vec& pos, Team playerTeam) const {
        float targetY = (playerTeam == Team::BLUE) ?
            CommonValues::BACK_WALL_Y : -CommonValues::BACK_WALL_Y;
        return (abs(pos.y - targetY) < BACKBOARD_DIST && pos.z > 500.0f);
    }

    bool IsNearCeiling(const Vec& pos) const {
        return pos.z > (CommonValues::CEILING_Z - CEILING_DIST);
    }

    bool IsNearSideWall(const Vec& pos) const {
        return abs(pos.x) > (CommonValues::SIDE_WALL_X - SIDE_WALL_DIST);
    }

    DoubleTapSequence::SurfaceType DetectSurface(const Vec& pos, Team playerTeam) const {
        if (IsNearBackboard(pos, playerTeam)) return DoubleTapSequence::BACKBOARD;
        if (IsNearCeiling(pos)) return DoubleTapSequence::CEILING;
        if (IsNearSideWall(pos)) return DoubleTapSequence::SIDE_WALL;
        return DoubleTapSequence::BACKBOARD; // Default
    }

    bool IsGoalScored(const Vec& ballPos, Team playerTeam) const {
        float targetY = (playerTeam == Team::BLUE) ?
            CommonValues::BACK_WALL_Y : -CommonValues::BACK_WALL_Y;
        bool correctSide = (playerTeam == Team::BLUE) ?
            (ballPos.y > targetY) : (ballPos.y < -targetY);
        return correctSide &&
               abs(ballPos.x) < 893.0f &&
               ballPos.z < CommonValues::GOAL_HEIGHT;
    }

public:
    struct Metrics {
        int setupTouches = 0;
        int finishTouches = 0;
        int goals = 0;
        double totalReward = 0.0;
    };

    static Metrics GetAndResetMetrics() {
        Metrics m;
        m.setupTouches = s_setupCount.exchange(0);
        m.finishTouches = s_finishCount.exchange(0);
        m.goals = s_goalCount.exchange(0);
        long long r = s_rewardMilli.exchange(0);
        m.totalReward = double(r) / 1000.0;
        return m;
    }

    virtual void Reset(const GameState& initialState) override {
        sequences.clear();
        currentTime = 0.0f;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        currentTime += state.deltaTime;
        uint32_t id = player.carId;

        // Initialize sequence
        if (sequences.find(id) == sequences.end()) {
            sequences[id] = DoubleTapSequence();
        }

        auto& seq = sequences[id];
        float reward = 0.0f;

        // Timeout check
        if (seq.phase != DoubleTapSequence::NONE &&
            currentTime - seq.phaseStartTime > TOTAL_SEQUENCE_TIMEOUT) {
            seq.phase = DoubleTapSequence::NONE;
        }

        Vec oppGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        // PHASE 1: SETUP TOUCH
        if (player.ballTouchedStep && seq.phase == DoubleTapSequence::NONE) {
            // Check if touch is toward opponent backboard/ceiling/wall
            Vec toBall = state.ball.pos - player.pos;
            float distToGoal = (state.ball.pos - oppGoal).Length();

            // Must be in offensive half for double tap setup
            bool inOffensiveHalf = (player.team == Team::BLUE) ?
                (player.pos.y > 0.0f) : (player.pos.y < 0.0f);

            if (inOffensiveHalf && distToGoal < 4000.0f) {
                // Check ball trajectory toward surface
                Vec ballVel = state.ball.vel;
                float ballSpeed = ballVel.Length();

                if (ballSpeed > 600.0f) {
                    // Detect which surface it's heading toward
                    DoubleTapSequence::SurfaceType surface = DetectSurface(state.ball.pos, player.team);

                    // Start sequence
                    seq.phase = DoubleTapSequence::SETUP_TOUCH;
                    seq.phaseStartTime = currentTime;
                    seq.setupTouchPos = state.ball.pos;
                    seq.setupSpeed = ballSpeed;
                    seq.setupWasAerial = !player.isOnGround && player.pos.z > 250.0f;
                    seq.surface = surface;

                    // Small setup reward (encourages initiating)
                    float setupReward = 1.5f;
                    if (seq.setupWasAerial) setupReward += 1.0f; // Bonus for aerial setup

                    reward += setupReward;
                    s_setupCount.fetch_add(1);
                }
            }
        }

        // PHASE 2: SURFACE CONTACT
        if (seq.phase == DoubleTapSequence::SETUP_TOUCH) {
            // Check if ball contacted surface
            bool hitSurface = false;

            switch (seq.surface) {
                case DoubleTapSequence::BACKBOARD:
                    hitSurface = IsNearBackboard(state.ball.pos, player.team);
                    break;
                case DoubleTapSequence::CEILING:
                    hitSurface = IsNearCeiling(state.ball.pos);
                    break;
                case DoubleTapSequence::SIDE_WALL:
                    hitSurface = IsNearSideWall(state.ball.pos);
                    break;
            }

            if (hitSurface) {
                seq.phase = DoubleTapSequence::SURFACE_CONTACT;
                seq.surfaceContactPos = state.ball.pos;
                seq.phaseStartTime = currentTime;

                // Surface contact reward (varies by surface)
                float surfaceReward = 0.0f;
                switch (seq.surface) {
                    case DoubleTapSequence::BACKBOARD: surfaceReward = 2.5f; break;
                    case DoubleTapSequence::CEILING: surfaceReward = 3.0f; break;
                    case DoubleTapSequence::SIDE_WALL: surfaceReward = 2.0f; break;
                }

                reward += surfaceReward;
            }

            // Timeout check
            if (currentTime - seq.phaseStartTime > SETUP_TO_SURFACE_TIMEOUT) {
                seq.phase = DoubleTapSequence::NONE;
            }
        }

        // PHASE 3: FINISH TOUCH
        if (seq.phase == DoubleTapSequence::SURFACE_CONTACT && player.ballTouchedStep) {
            // Check if ball is heading toward goal
            Vec toGoal = (oppGoal - state.ball.pos).Normalized();
            Vec ballVelDir = state.ball.vel.Normalized();
            float alignment = ballVelDir.Dot(toGoal);

            if (alignment > 0.5f) {
                float ballSpeed = state.ball.vel.Length();
                bool isAerial = !player.isOnGround && player.pos.z > 250.0f;

                // Base finish reward
                float finishReward = 8.0f;

                // Surface multiplier
                float surfaceMult = 1.0f;
                switch (seq.surface) {
                    case DoubleTapSequence::BACKBOARD: surfaceMult = 1.5f; break;
                    case DoubleTapSequence::CEILING: surfaceMult = 1.8f; break;
                    case DoubleTapSequence::SIDE_WALL: surfaceMult = 1.3f; break;
                }

                finishReward *= surfaceMult;

                // Bonus for both touches being aerial (true double tap)
                if (isAerial && seq.setupWasAerial) {
                    finishReward += 5.0f;
                }

                // Speed bonus
                float speedBonus = RS_CLAMP(ballSpeed / 2300.0f, 0.0f, 1.0f) * 3.0f;
                finishReward += speedBonus;

                // Alignment bonus
                float alignBonus = RS_CLAMP((alignment - 0.5f) / 0.5f, 0.0f, 1.0f) * 2.0f;
                finishReward += alignBonus;

                // Goal bonus
                if (IsGoalScored(state.ball.pos, player.team)) {
                    finishReward += 15.0f * surfaceMult;
                    s_goalCount.fetch_add(1);
                }

                reward += finishReward;
                s_finishCount.fetch_add(1);

                seq.phase = DoubleTapSequence::COMPLETED;
                seq.finishWasAerial = isAerial;
            }

            // Timeout check
            if (currentTime - seq.phaseStartTime > SURFACE_TO_FINISH_TIMEOUT) {
                seq.phase = DoubleTapSequence::NONE;
            }
        }

        // Reset after completion
        if (seq.phase == DoubleTapSequence::COMPLETED) {
            seq.phase = DoubleTapSequence::NONE;
        }

        if (reward > 0.0f) {
            s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));
        }

        return reward;
    }
};

// ============================================================================
// WallPassReward
// Rewards hitting ball off wall for positioning (self-pass or teammate pass)
// - Self-pass: Wall hit → player follows → receives in better position
// - Teammate pass: Wall hit → teammate positioned to receive
// - Rewards setup quality, positioning, and successful reception
// ============================================================================
class WallPassReward : public Reward {
private:
    struct WallPassSequence {
        enum Phase {
            NONE,
            WALL_HIT,      // Hit ball off wall
            IN_FLIGHT,     // Ball traveling from wall
            RECEIVED       // Ball received by player/teammate
        };

        Phase phase = NONE;
        float phaseStartTime = 0.0f;
        uint32_t initiatorId = 0;
        Vec wallHitPos;
        Vec wallHitVel;
        bool towardGoal = false;
        bool isTeamPass = false;  // vs self-pass
    };

    mutable std::map<uint32_t, WallPassSequence> sequences;
    mutable float currentTime = 0.0f;

    const float WALL_DIST_THRESHOLD = 350.0f;
    const float PASS_TIMEOUT = 3.0f;
    const float RECEPTION_DISTANCE = 1500.0f;

    inline static std::atomic<int> s_selfPassCount{0};
    inline static std::atomic<int> s_teamPassCount{0};
    inline static std::atomic<long long> s_rewardMilli{0};

    bool IsNearWall(const Vec& pos) const {
        float distX = abs(pos.x) - CommonValues::SIDE_WALL_X;
        float distY = abs(pos.y) - CommonValues::BACK_WALL_Y;
        return (abs(distX) < WALL_DIST_THRESHOLD || abs(distY) < WALL_DIST_THRESHOLD);
    }

public:
    struct Metrics {
        int selfPasses = 0;
        int teamPasses = 0;
        double totalReward = 0.0;
    };

    static Metrics GetAndResetMetrics() {
        Metrics m;
        m.selfPasses = s_selfPassCount.exchange(0);
        m.teamPasses = s_teamPassCount.exchange(0);
        long long r = s_rewardMilli.exchange(0);
        m.totalReward = double(r) / 1000.0;
        return m;
    }

    virtual void Reset(const GameState& initialState) override {
        sequences.clear();
        currentTime = 0.0f;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        currentTime += state.deltaTime;
        uint32_t id = player.carId;

        if (sequences.find(id) == sequences.end()) {
            sequences[id] = WallPassSequence();
        }

        auto& seq = sequences[id];
        float reward = 0.0f;

        // Timeout check
        if (seq.phase != WallPassSequence::NONE &&
            currentTime - seq.phaseStartTime > PASS_TIMEOUT) {
            seq.phase = WallPassSequence::NONE;
        }

        Vec oppGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        // PHASE 1: WALL HIT
        if (player.ballTouchedStep && IsNearWall(state.ball.pos)) {
            float ballSpeed = state.ball.vel.Length();

            if (ballSpeed > 500.0f) {
                // Check trajectory
                Vec toGoal = (oppGoal - state.ball.pos).Normalized();
                float alignment = state.ball.vel.Normalized().Dot(toGoal);

                // Start sequence
                seq.phase = WallPassSequence::WALL_HIT;
                seq.phaseStartTime = currentTime;
                seq.initiatorId = player.carId;
                seq.wallHitPos = state.ball.pos;
                seq.wallHitVel = state.ball.vel;
                seq.towardGoal = (alignment > 0.3f);

                // Initial wall hit reward
                float hitReward = 1.0f;
                if (seq.towardGoal) hitReward += 1.5f;

                reward += hitReward;

                seq.phase = WallPassSequence::IN_FLIGHT;
            }
        }

        // PHASE 2: RECEPTION
        if (seq.phase == WallPassSequence::IN_FLIGHT && player.ballTouchedStep) {
            // Check if this is the initiator (self-pass) or teammate (team pass)
            bool isSelfPass = (player.carId == seq.initiatorId);

            // Must be away from wall (not just bouncing on wall)
            if (!IsNearWall(state.ball.pos)) {
                // Check positioning quality
                float distFromWallHit = (state.ball.pos - seq.wallHitPos).Length();

                if (distFromWallHit > 300.0f && distFromWallHit < RECEPTION_DISTANCE) {
                    // Calculate reception quality
                    Vec toGoal = (oppGoal - state.ball.pos).Normalized();
                    float alignment = state.ball.vel.Normalized().Dot(toGoal);

                    float receptionReward = 0.0f;

                    if (isSelfPass) {
                        // Self-pass reward
                        receptionReward = 4.0f;

                        // Bonus for maintaining goal direction
                        if (seq.towardGoal && alignment > 0.5f) {
                            receptionReward += 3.0f;
                        }

                        // Distance bonus (farther = better positioning)
                        float distBonus = RS_CLAMP(distFromWallHit / RECEPTION_DISTANCE, 0.0f, 1.0f) * 2.0f;
                        receptionReward += distBonus;

                        s_selfPassCount.fetch_add(1);
                    } else {
                        // Team pass reward (check if teammate was positioned)
                        receptionReward = 6.0f;

                        // Bonus for good positioning
                        float distToGoal = (player.pos - oppGoal).Length();
                        bool inAttackingPos = distToGoal < 3000.0f;

                        if (inAttackingPos && alignment > 0.5f) {
                            receptionReward += 5.0f;
                        }

                        s_teamPassCount.fetch_add(1);
                    }

                    reward += receptionReward;
                    seq.phase = WallPassSequence::RECEIVED;
                }
            }
        }

        // Reset after reception
        if (seq.phase == WallPassSequence::RECEIVED) {
            seq.phase = WallPassSequence::NONE;
        }

        if (reward > 0.0f) {
            s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));
        }

        return reward;
    }
};

// ============================================================================
// WallToAerialReward
// Rewards launching from wall into aerial plays
// - Detects wall contact → aerial launch
// - Tracks aerial control quality (air dribble, power shot, backboard setup)
// - Progressive rewards for launch quality, aerial control, and outcome
// ============================================================================
class WallToAerialReward : public Reward {
private:
    struct WallAerialSequence {
        enum Phase {
            NONE,
            ON_WALL,           // Player on wall
            LAUNCHED,          // Launched from wall
            AERIAL_CONTROL,    // Controlling ball in air
            COMPLETED
        };

        enum PlayType {
            AIR_DRIBBLE,      // Multiple aerial touches
            POWER_SHOT,       // Single strong aerial hit
            BACKBOARD_SETUP   // Setting up for double tap
        };

        Phase phase = NONE;
        PlayType playType = AIR_DRIBBLE;
        float phaseStartTime = 0.0f;
        Vec launchPos;
        Vec launchVel;
        int aerialTouches = 0;
        float maxHeight = 0.0f;
        bool launchedWithFlip = false;
    };

    mutable std::map<uint32_t, WallAerialSequence> sequences;
    mutable float currentTime = 0.0f;

    const float WALL_DIST_THRESHOLD = 250.0f;
    const float AERIAL_HEIGHT_MIN = 350.0f;
    const float SEQUENCE_TIMEOUT = 5.0f;

    inline static std::atomic<int> s_airDribbleCount{0};
    inline static std::atomic<int> s_powerShotCount{0};
    inline static std::atomic<int> s_backboardSetupCount{0};
    inline static std::atomic<long long> s_rewardMilli{0};

    bool IsOnWall(const Player& player) const {
        if (player.isOnGround) return false;

        float distX = abs(player.pos.x) - CommonValues::SIDE_WALL_X;
        float distY = abs(player.pos.y) - CommonValues::BACK_WALL_Y;

        // Check if touching wall (close to wall with wheels in contact)
        bool nearSideWall = abs(distX) < WALL_DIST_THRESHOLD;
        bool nearBackWall = abs(distY) < WALL_DIST_THRESHOLD;

        // Check if any wheels have contact
        bool hasWheelContact = player.wheelsWithContact[0] || player.wheelsWithContact[1] ||
                               player.wheelsWithContact[2] || player.wheelsWithContact[3];

        return (nearSideWall || nearBackWall) && hasWheelContact;
    }

    bool IsNearBackboard(const Vec& pos, Team playerTeam) const {
        float targetY = (playerTeam == Team::BLUE) ?
            CommonValues::BACK_WALL_Y : -CommonValues::BACK_WALL_Y;
        return (abs(pos.y - targetY) < 400.0f && pos.z > 500.0f);
    }

public:
    struct Metrics {
        int airDribbles = 0;
        int powerShots = 0;
        int backboardSetups = 0;
        double totalReward = 0.0;
    };

    static Metrics GetAndResetMetrics() {
        Metrics m;
        m.airDribbles = s_airDribbleCount.exchange(0);
        m.powerShots = s_powerShotCount.exchange(0);
        m.backboardSetups = s_backboardSetupCount.exchange(0);
        long long r = s_rewardMilli.exchange(0);
        m.totalReward = double(r) / 1000.0;
        return m;
    }

    virtual void Reset(const GameState& initialState) override {
        sequences.clear();
        currentTime = 0.0f;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        currentTime += state.deltaTime;
        uint32_t id = player.carId;

        if (sequences.find(id) == sequences.end()) {
            sequences[id] = WallAerialSequence();
        }

        auto& seq = sequences[id];
        float reward = 0.0f;

        // Timeout check
        if (seq.phase != WallAerialSequence::NONE &&
            currentTime - seq.phaseStartTime > SEQUENCE_TIMEOUT) {
            seq.phase = WallAerialSequence::NONE;
        }

        Vec oppGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        // PHASE 1: ON WALL
        if (IsOnWall(player) && seq.phase == WallAerialSequence::NONE) {
            seq.phase = WallAerialSequence::ON_WALL;
            seq.phaseStartTime = currentTime;
        }

        // PHASE 2: LAUNCH FROM WALL
        if (seq.phase == WallAerialSequence::ON_WALL && !IsOnWall(player)) {
            // Player left wall - check if it's an aerial launch
            if (!player.isOnGround && player.pos.z > AERIAL_HEIGHT_MIN) {
                seq.phase = WallAerialSequence::LAUNCHED;
                seq.launchPos = player.pos;
                seq.launchVel = player.vel;
                seq.phaseStartTime = currentTime;
                seq.aerialTouches = 0;
                seq.maxHeight = player.pos.z;
                seq.launchedWithFlip = player.isFlipping;

                // Launch reward
                float launchReward = 2.0f;
                if (seq.launchedWithFlip) launchReward += 1.5f;

                reward += launchReward;
            } else {
                seq.phase = WallAerialSequence::NONE;
            }
        }

        // PHASE 3: AERIAL CONTROL
        if ((seq.phase == WallAerialSequence::LAUNCHED ||
             seq.phase == WallAerialSequence::AERIAL_CONTROL) &&
            player.ballTouchedStep) {

            if (!player.isOnGround && player.pos.z > AERIAL_HEIGHT_MIN) {
                seq.phase = WallAerialSequence::AERIAL_CONTROL;
                seq.aerialTouches++;

                // Track max height
                if (state.ball.pos.z > seq.maxHeight) {
                    seq.maxHeight = state.ball.pos.z;
                }

                // Determine play type based on context
                Vec toGoal = (oppGoal - state.ball.pos).Normalized();
                float alignment = state.ball.vel.Normalized().Dot(toGoal);
                float ballSpeed = state.ball.vel.Length();
                bool nearBackboard = IsNearBackboard(state.ball.pos, player.team);

                float touchReward = 0.0f;

                // AIR DRIBBLE: Multiple touches, moderate speed
                if (seq.aerialTouches >= 2 && ballSpeed > 400.0f && ballSpeed < 1800.0f) {
                    seq.playType = WallAerialSequence::AIR_DRIBBLE;
                    touchReward = 3.0f + (seq.aerialTouches * 1.5f);

                    // Control bonus (staying close to ball)
                    float distToBall = (state.ball.pos - player.pos).Length();
                    if (distToBall < 300.0f) {
                        touchReward += 2.0f;
                    }

                    if (seq.aerialTouches == 2) {
                        s_airDribbleCount.fetch_add(1);
                    }
                }
                // POWER SHOT: Strong hit toward goal
                else if (ballSpeed > 1500.0f && alignment > 0.6f) {
                    seq.playType = WallAerialSequence::POWER_SHOT;
                    touchReward = 8.0f;

                    // Speed bonus
                    float speedBonus = RS_CLAMP((ballSpeed - 1500.0f) / 800.0f, 0.0f, 1.0f) * 4.0f;
                    touchReward += speedBonus;

                    // Alignment bonus
                    float alignBonus = RS_CLAMP((alignment - 0.6f) / 0.4f, 0.0f, 1.0f) * 3.0f;
                    touchReward += alignBonus;

                    s_powerShotCount.fetch_add(1);
                }
                // BACKBOARD SETUP: Hit toward backboard
                else if (nearBackboard && ballSpeed > 700.0f) {
                    seq.playType = WallAerialSequence::BACKBOARD_SETUP;
                    touchReward = 6.0f;

                    // Height bonus (higher backboard touches are better)
                    float heightBonus = RS_CLAMP((state.ball.pos.z - 500.0f) / 1000.0f, 0.0f, 1.0f) * 3.0f;
                    touchReward += heightBonus;

                    s_backboardSetupCount.fetch_add(1);
                }

                reward += touchReward;

                // Check for completion
                if (player.isOnGround || seq.aerialTouches >= 5) {
                    seq.phase = WallAerialSequence::COMPLETED;
                }
            } else {
                // Lost aerial control
                seq.phase = WallAerialSequence::COMPLETED;
            }
        }

        // Reset after completion
        if (seq.phase == WallAerialSequence::COMPLETED) {
            seq.phase = WallAerialSequence::NONE;
        }

        if (reward > 0.0f) {
            s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));
        }

        return reward;
    }
};

// ============================================================================
// MasterAdvancedPlayReward
// Combines all advanced play rewards with optimal weights
// Usage: new MasterAdvancedPlayReward()
// ============================================================================
class MasterAdvancedPlayReward : public Reward {
private:
    AdvancedDoubleTapReward doubleTap;
    WallPassReward wallPass;
    WallToAerialReward wallToAerial;

    // Weights (tuned for balance)
    const float DOUBLE_TAP_WEIGHT = 1.0f;
    const float WALL_PASS_WEIGHT = 0.8f;
    const float WALL_TO_AERIAL_WEIGHT = 1.2f;

public:
    virtual void Reset(const GameState& initialState) override {
        doubleTap.Reset(initialState);
        wallPass.Reset(initialState);
        wallToAerial.Reset(initialState);
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float total = 0.0f;

        total += doubleTap.GetReward(player, state, isFinal) * DOUBLE_TAP_WEIGHT;
        total += wallPass.GetReward(player, state, isFinal) * WALL_PASS_WEIGHT;
        total += wallToAerial.GetReward(player, state, isFinal) * WALL_TO_AERIAL_WEIGHT;

        return total;
    }

    // Metrics access
    static void PrintMetrics() {
        auto dt = AdvancedDoubleTapReward::GetAndResetMetrics();
        auto wp = WallPassReward::GetAndResetMetrics();
        auto wa = WallToAerialReward::GetAndResetMetrics();

        printf("=== ADVANCED PLAY METRICS ===\n");
        printf("Double Taps: %d setups, %d finishes, %d goals (%.2f reward)\n",
               dt.setupTouches, dt.finishTouches, dt.goals, dt.totalReward);
        printf("Wall Passes: %d self, %d team (%.2f reward)\n",
               wp.selfPasses, wp.teamPasses, wp.totalReward);
        printf("Wall Aerials: %d dribbles, %d shots, %d setups (%.2f reward)\n",
               wa.airDribbles, wa.powerShots, wa.backboardSetups, wa.totalReward);
    }
};

} // namespace RLGC
