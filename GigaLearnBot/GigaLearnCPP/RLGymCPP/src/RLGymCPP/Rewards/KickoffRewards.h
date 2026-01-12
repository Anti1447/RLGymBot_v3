#pragma once
#include "Reward.h"
#include "../Math.h"
#include <map>
#include <deque>
#include <vector>
#include <atomic>
#include "RewardMath.h"
#include "CompleteMechanicsRewards.h"

namespace RLGC {
    // ============================================================================
    // KickoffSpeedReward - OPTIMIZED FOR STANDARD KICKOFFS
    // Pure speed toward ball during kickoff phase
    // 2025-12-23 - Rev 3 - Optimized for aggressive standard kickoffs
    // ============================================================================
    class KickoffSpeedReward : public Reward {
    private:
        float centerRadius;
        float maxBallSpeed;
        float minSpeedThreshold;  // Must be moving fast to get reward

    public:
        explicit KickoffSpeedReward(
            float centerRadiusIn = 300.0f,
            float maxBallSpeedIn = 500.0f,
            float minSpeedThresholdIn = 1800.0f)  // Require near-supersonic speed
            : centerRadius(centerRadiusIn),
            maxBallSpeed(maxBallSpeedIn),
            minSpeedThreshold(minSpeedThresholdIn) {
        }

        virtual float GetReward(const Player& player,
            const GameState& state,
            bool isFinal) override {

            Vec ballPos = state.ball.pos;
            Vec ballVel = state.ball.vel;
            float ballDist = ballPos.Length();

            // Only during kickoff
            if (ballDist > centerRadius || ballVel.Length() > maxBallSpeed)
                return 0.0f;

            // Must be on ground for standard kickoff
            if (!player.isOnGround)
                return 0.0f;

            Vec toBall = ballPos - player.pos;
            float dist = toBall.Length();
            if (dist < 1e-3f)
                return 0.0f;

            Vec dirToBall = toBall / dist;
            float speedToBall = player.vel.Dot(dirToBall);

            // Must be moving toward ball at minimum speed
            if (speedToBall < minSpeedThreshold)
                return 0.0f;

            // Normalize by max speed
            float speedFrac = RS_CLAMP(speedToBall / CommonValues::CAR_MAX_SPEED, 0.0f, 1.0f);

            // Bonus for being close to ball (encourages final approach)
            float distFactor = 1.0f;
            if (dist < 1500.0f) {
                distFactor = 1.0f + 0.5f * (1.0f - dist / 1500.0f);  // Up to 1.5x when very close
            }

            // Bonus for facing the ball correctly
            float facingAlignment = player.rotMat.forward.Dot(dirToBall);
            float facingBonus = RS_CLAMP(facingAlignment, 0.0f, 1.0f);

            return speedFrac * distFactor * (0.7f + 0.3f * facingBonus);
        }
    };

    // ============================================================================
    // KickoffFirstTouchReward - OPTIMIZED FOR STANDARD KICKOFFS
    // Heavily rewards first touch, punishes being second or missing entirely
    // 2025-12-23 - Rev 2 - Standard kickoff focus
    // ============================================================================
    class KickoffFirstTouchReward : public Reward {
    public:
        struct KickoffFirstTouchMetrics {
            int kickoffs = 0;
            int firstWins = 0;
            int firstLoss = 0;
            int noTouch = 0;
            double rewardSum = 0.0;
        };

        inline static std::atomic<int> s_kickoffs{ 0 };
        inline static std::atomic<int> s_firstWins{ 0 };
        inline static std::atomic<int> s_firstLoss{ 0 };
        inline static std::atomic<int> s_noTouch{ 0 };
        inline static std::atomic<long long> s_rewardMilli{ 0 };

        static KickoffFirstTouchMetrics GetAndResetKickoffFirstTouchMetrics() {
            KickoffFirstTouchMetrics m;
            m.kickoffs = s_kickoffs.exchange(0);
            m.firstWins = s_firstWins.exchange(0);
            m.firstLoss = s_firstLoss.exchange(0);
            m.noTouch = s_noTouch.exchange(0);
            long long r = s_rewardMilli.exchange(0);
            m.rewardSum = double(r) / 1000.0;
            return m;
        }

    private:
        mutable bool kickoffActive = false;
        mutable float kickoffTime = 0.0f;

        mutable bool firstTouchRecorded = false;
        mutable uint32_t firstTouchCarId = 0;
        mutable float firstTouchTime = 0.0f;

        mutable std::map<uint32_t, bool> touchedDuringKickoff;
        mutable std::map<uint32_t, bool> paidThisKickoff;

        float kickoffDetectBallPos = 280.0f;  // Tighter detection
        float kickoffDetectBallVel = 200.0f;

        float firstTouchWindowSec = 1.8f;  // Tighter window - must be fast
        float kickoffMaxTrackSec = 3.5f;

    public:
        virtual void Reset(const GameState& initialState) override {
            kickoffActive = false;
            kickoffTime = 0.0f;
            firstTouchRecorded = false;
            firstTouchCarId = 0;
            firstTouchTime = 0.0f;
            touchedDuringKickoff.clear();
            paidThisKickoff.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

            bool isKickoffNow = (state.ball.pos.Length() < kickoffDetectBallPos &&
                state.ball.vel.Length() < kickoffDetectBallVel);

            // Kickoff start
            if (isKickoffNow && !kickoffActive) {
                kickoffActive = true;
                kickoffTime = 0.0f;
                firstTouchRecorded = false;
                firstTouchCarId = 0;
                firstTouchTime = 0.0f;
                touchedDuringKickoff.clear();
                paidThisKickoff.clear();
                s_kickoffs.fetch_add(1);
            }

            if (kickoffActive) {
                kickoffTime += dt;

                if (player.ballTouchedStep) {
                    touchedDuringKickoff[player.carId] = true;

                    if (!firstTouchRecorded && kickoffTime <= firstTouchWindowSec) {
                        firstTouchRecorded = true;
                        firstTouchCarId = player.carId;
                        firstTouchTime = kickoffTime;
                    }
                }

                if (!isKickoffNow || kickoffTime >= kickoffMaxTrackSec) {
                    kickoffActive = false;
                }

                return 0.0f;
            }

            if (paidThisKickoff[player.carId])
                return 0.0f;

            float reward = 0.0f;

            if (firstTouchRecorded) {
                if (player.carId == firstTouchCarId) {
                    // WINNING FIRST TOUCH - big reward, bonus for speed
                    float timeFrac = RS_CLAMP(1.0f - (firstTouchTime / firstTouchWindowSec), 0.0f, 1.0f);
                    reward = 1.5f + 1.0f * timeFrac;  // 1.5 to 2.5 reward
                    s_firstWins.fetch_add(1);
                }
                else {
                    // LOSING KICKOFF - severe penalty
                    if (touchedDuringKickoff[player.carId]) {
                        // Touched but second = bad
                        reward = -0.5f;
                        s_firstLoss.fetch_add(1);
                    }
                    else {
                        // Didn't even touch = very bad
                        reward = -1.2f;
                        s_noTouch.fetch_add(1);
                    }
                }

                s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));
            }

            paidThisKickoff[player.carId] = true;
            return reward;
        }
    };

    // ============================================================================
    // KickoffFormReward - OPTIMIZED FOR STANDARD KICKOFFS ONLY
    // Rewards: Fast boost, early flip, hitting ball center, powerful contact
    // Removes: Fake kickoffs, delayed kickoffs, retreat rewards
    // 2025-12-23 - Rev 6.0 - Pure standard kickoff optimization
    // ============================================================================
    class KickoffFormReward : public Reward {
    public:
        struct KickoffFormMetrics {
            int kickoffs = 0;
            int earlyFlips = 0;        // Changed from commits
            int centerHits = 0;        // Changed from standardTouches
            int powerfulHits = 0;      // Changed from fakeSuccess
            int followThroughs = 0;    // Changed from wavedashFollow
            double rewardSum = 0.0;
        };

        inline static std::atomic<int> s_kickoffs{ 0 };
        inline static std::atomic<int> s_earlyFlips{ 0 };
        inline static std::atomic<int> s_centerHits{ 0 };
        inline static std::atomic<int> s_powerfulHits{ 0 };
        inline static std::atomic<int> s_followThroughs{ 0 };
        inline static std::atomic<long long> s_rewardMilli{ 0 };

        static KickoffFormMetrics GetAndResetKickoffFormMetrics() {
            KickoffFormMetrics m;
            m.kickoffs = s_kickoffs.exchange(0);
            m.earlyFlips = s_earlyFlips.exchange(0);
            m.centerHits = s_centerHits.exchange(0);
            m.powerfulHits = s_powerfulHits.exchange(0);
            m.followThroughs = s_followThroughs.exchange(0);
            long long r = s_rewardMilli.exchange(0);
            m.rewardSum = double(r) / 1000.0;
            return m;
        }

    private:
        struct KOState {
            bool kickoffActive = false;
            float t = 0.0f;

            bool flipped = false;
            float flipTime = -1.0f;

            bool touchedBall = false;
            float touchTime = -1.0f;

            Vec ballPosAtTouch;
            Vec ballVelAtTouch;
            float carSpeedAtTouch = 0.0f;

            bool earlyFlipCounted = false;
            bool centerHitCounted = false;
            bool powerHitCounted = false;
            bool followThroughCounted = false;
        };

        mutable std::map<uint32_t, KOState> ks;

        float kickoffDetectBallPos = 280.0f;
        float kickoffDetectBallVel = 200.0f;

        float idealFlipTimeSec = 0.60f;      // Flip earlier for faster approach
        float flipWindowSec = 0.90f;         // Must flip before this (tighter window)
        float touchDeadlineSec = 1.7f;       // Must touch faster (was 1.9f)

        float minCarSpeedAtTouch = 2000.0f;  // Require near-supersonic speed (was 1200.0f)
        float ballCenterRadius = 100.0f;     // How close to dead center

    public:
        virtual void Reset(const GameState& initialState) override {
            ks.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
            auto& s = ks[player.carId];

            bool isKickoffNow = (state.ball.pos.Length() < kickoffDetectBallPos &&
                state.ball.vel.Length() < kickoffDetectBallVel);

            // Kickoff start
            if (isKickoffNow && !s.kickoffActive) {
                s.kickoffActive = true;
                s.t = 0.0f;
                s.flipped = false;
                s.flipTime = -1.0f;
                s.touchedBall = false;
                s.touchTime = -1.0f;
                s.earlyFlipCounted = false;
                s.centerHitCounted = false;
                s.powerHitCounted = false;
                s.followThroughCounted = false;

                s_kickoffs.fetch_add(1);
            }

            // Kickoff end
            if (!isKickoffNow && s.kickoffActive) {
                s.kickoffActive = false;
                return 0.0f;
            }

            if (!s.kickoffActive)
                return 0.0f;

            s.t += dt;
            float r = 0.0f;

            Vec dirToBall = (state.ball.pos - player.pos).Normalized();
            float faceBall = player.rotMat.forward.Dot(dirToBall);

            // ====================================================================
            // 1. EARLY FLIP REWARD - flip into the ball at the right time
            // ====================================================================
            if (!s.flipped && s.t <= flipWindowSec) {
                bool flipStartedNow = (player.prev && !player.prev->isFlipping && player.isFlipping);

                if (flipStartedNow && faceBall > 0.6f) {
                    s.flipped = true;
                    s.flipTime = s.t;

                    // Reward based on timing (closer to ideal = better)
                    float timingError = fabsf(s.t - idealFlipTimeSec);
                    float timingQuality = RS_CLAMP(1.0f - (timingError / idealFlipTimeSec), 0.0f, 1.0f);

                    float flipReward = 0.8f + 0.6f * timingQuality;  // 0.8 to 1.4
                    r += flipReward;

                    if (!s.earlyFlipCounted) {
                        s.earlyFlipCounted = true;
                        s_earlyFlips.fetch_add(1);
                        s_rewardMilli.fetch_add((long long)(flipReward * 1000.0f));
                    }
                }
            }

            // ====================================================================
            // 2. BALL CONTACT REWARDS - center hit, power, direction
            // ====================================================================
            if (!s.touchedBall && player.ballTouchedStep && s.t <= touchDeadlineSec) {
                s.touchedBall = true;
                s.touchTime = s.t;
                s.ballPosAtTouch = state.ball.pos;
                s.ballVelAtTouch = state.ball.vel;
                s.carSpeedAtTouch = player.vel.Length();

                Vec oppGoal = (player.team == Team::BLUE)
                    ? CommonValues::ORANGE_GOAL_BACK
                    : CommonValues::BLUE_GOAL_BACK;
                Vec toOppGoal = (oppGoal - state.ball.pos).Normalized();

                // A) Center hit quality - did we hit it dead center?
                float ballCenterDist = state.ball.pos.Length();
                float centerQuality = RS_CLAMP(1.0f - (ballCenterDist / ballCenterRadius), 0.0f, 1.0f);

                if (centerQuality > 0.3f) {
                    float centerReward = 1.2f * centerQuality;  // Up to 1.2
                    r += centerReward;

                    if (!s.centerHitCounted) {
                        s.centerHitCounted = true;
                        s_centerHits.fetch_add(1);
                        s_rewardMilli.fetch_add((long long)(centerReward * 1000.0f));
                    }
                }

                // B) Contact height quality - reward hitting middle-to-lower part of ball
                // Ball radius is ~93 units, so relative Z from -93 to +93
                // We want to hit at or below the center (negative relative Z is good)
                float contactHeightQuality = 1.0f;  // Default multiplier

                if (player.prev && player.prev->ballHitInfo.isValid) {
                    // Get relative contact position on ball (normalized to ball radius)
                    float relativeContactZ = player.prev->ballHitInfo.relativePosOnBall.z;

                    // Map contact height to quality multiplier:
                    // - Bottom half (z < 0): Best - multiplier 1.2-1.0
                    // - Middle (z = 0): Good - multiplier 1.0
                    // - Upper half (z > 0): Worse - multiplier 1.0-0.6
                    // - Top (z > 60): Bad - multiplier 0.6-0.3

                    if (relativeContactZ <= 0.0f) {
                        // Bottom half: linear bonus from 1.0 (center) to 1.2 (bottom)
                        contactHeightQuality = 1.0f + (-relativeContactZ / CommonValues::BALL_RADIUS) * 0.2f;
                    } else {
                        // Upper half: linear penalty from 1.0 (center) to 0.3 (top)
                        float topnessFactor = RS_CLAMP(relativeContactZ / CommonValues::BALL_RADIUS, 0.0f, 1.0f);
                        contactHeightQuality = 1.0f - (topnessFactor * 0.7f);
                    }
                }

                // C) Power and direction - fast hit toward opponent goal
                float ballSpeed = state.ball.vel.Length();
                float speedQuality = RS_CLAMP((ballSpeed - 1000.0f) / 1500.0f, 0.0f, 1.0f);

                float directionQuality = 0.0f;
                if (ballSpeed > 100.0f) {
                    directionQuality = RS_CLAMP(
                        state.ball.vel.Dot(toOppGoal) / ballSpeed,
                        0.0f, 1.0f
                    );
                }

                // Car speed requirement with supersonic bonus
                float carSpeedQuality = RS_CLAMP(
                    (s.carSpeedAtTouch - minCarSpeedAtTouch) / 800.0f,
                    0.0f, 1.0f
                );

                // Supersonic bonus multiplier (>2200 units/s)
                float supersonicBonus = 1.0f;
                if (s.carSpeedAtTouch >= 2200.0f) {
                    supersonicBonus = 2.5f;  // Huge bonus for supersonic contact
                } else if (s.carSpeedAtTouch >= 2000.0f) {
                    supersonicBonus = 1.5f;  // Partial bonus approaching supersonic
                }

                if (speedQuality > 0.1f && directionQuality > 0.3f && carSpeedQuality > 0.0f) {
                    float powerReward = 2.0f * speedQuality * directionQuality * (0.5f + 0.5f * carSpeedQuality) * supersonicBonus * contactHeightQuality;
                    r += powerReward;

                    if (!s.powerHitCounted) {
                        s.powerHitCounted = true;
                        s_powerfulHits.fetch_add(1);
                        s_rewardMilli.fetch_add((long long)(powerReward * 1000.0f));
                    }
                }
            }

            // ====================================================================
            // 3. FOLLOW-THROUGH REWARD - keep pushing after contact
            // ====================================================================
            if (s.touchedBall && !s.followThroughCounted) {
                float timeSinceTouch = s.t - s.touchTime;

                // Check for continued forward momentum in next 0.3 seconds
                if (timeSinceTouch > 0.05f && timeSinceTouch <= 0.35f) {
                    Vec dirToOppGoal = (player.team == Team::BLUE)
                        ? (CommonValues::ORANGE_GOAL_BACK - player.pos).Normalized()
                        : (CommonValues::BLUE_GOAL_BACK - player.pos).Normalized();

                    float forwardSpeed = player.vel.Dot(dirToOppGoal);

                    if (forwardSpeed > 900.0f) {
                        float followReward = 0.7f;
                        r += followReward;

                        s.followThroughCounted = true;
                        s_followThroughs.fetch_add(1);
                        s_rewardMilli.fetch_add((long long)(followReward * 1000.0f));
                    }
                }
            }

            return r;
        }
    };

    class KickoffReward : public Reward {
    public:
        float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            // Only reward during kickoff
            bool isKickoff = std::abs(state.ball.pos.x) < 50.0f &&
                std::abs(state.ball.pos.y) < 50.0f &&
                state.ball.pos.z < 100.0f;

            if (!isKickoff) {
                return 0;
            }

            // Reward velocity towards ball
            Vec toBall = state.ball.pos - player.pos;
            float distToBall = toBall.Length();

            if (distToBall < 1.0f) return 0;

            Vec toBallDir = toBall / distToBall;

            // Reward velocity towards ball
            float velTowardsBall = player.vel.Dot(toBallDir);
            float velocityReward = std::max(0.0f, velTowardsBall / 2300.0f);

            // Reward heading towards opponent goal
            Vec toOpponentGoal;
            if (player.team == Team::BLUE) {
                toOpponentGoal = Vec(0, 5120.0f, 0) - player.pos;
            }
            else {
                toOpponentGoal = Vec(0, -5120.0f, 0) - player.pos;
            }
            toOpponentGoal = toOpponentGoal.Normalized();

            float directionReward = std::max(0.0f, toBallDir.Dot(toOpponentGoal));

            // Combine rewards
            float reward = velocityReward * 0.6f + directionReward * 0.4f;

            return reward;
        }

        void Reset(const GameState& state) override {}
        void PreStep(const GameState& state) override {}
    };

    class KickoffContactPunishmentReward : public Reward {
    private:
        enum KickoffFormation {
            FORMATION_CORNER,       // Right or Left corner: ~3279 units
            FORMATION_BACK,         // Back Right or Back Left: ~3850 units
            FORMATION_FAR_BACK      // Far Back Center: 4608 units
        };

        struct KickoffTierConfig {
            // TIER 1: CORNER FORMATIONS (Right or Left)
            // Distance: ~3279 units from ball
            // Physics: Diagonal approach, medium acceleration distance
            float cornerMaxTimeWithoutContact = 2.75f;
            float cornerPunishmentPerSecond = -1.25f;

            // TIER 2: BACK FORMATIONS (Right or Left)
            // Distance: ~3850 units from ball
            // Physics: Further back, longer acceleration distance
            float backMaxTimeWithoutContact = 3.5f;
            float backPunishmentPerSecond = -1.0f;

            // TIER 3: FAR BACK CENTER
            // Distance: 4608 units from ball
            // Physics: Furthest position, maximum acceleration distance
            float farBackMaxTimeWithoutContact = 3.5f;
            float farBackPunishmentPerSecond = -1.0f;

            // Detection thresholds
            float cornerThreshold = 3300.0f;     // < this = corner formation
            float backThreshold = 3900.0f;       // < this = back formation

            // Enable/disable
            bool enablePunishment = true;

            // Tolerance for position detection (allows variance from exact positions)
            float positionTolerance = 300.0f;
        };

        mutable std::map<uint32_t, float> kickoffTimer;
        mutable std::map<uint32_t, bool> hasContactedBall;
        mutable std::map<uint32_t, bool> wasKickingOff;
        mutable std::map<uint32_t, KickoffFormation> kickoffFormation;

        KickoffTierConfig config;

        bool IsKickoffFrame(const GameState& state) const {
            // Detect kickoff: ball is near center (within 500 units)
            float ballDistFromCenter = state.ball.pos.Length();
            return ballDistFromCenter < 500.0f;
        }

        KickoffFormation DetermineKickoffFormation(const Player& player, const GameState& state) const {
            // Calculate distance from field center (0, 0)
            // Using XY plane only (ignore Z)
            float playerX = player.pos.x;
            float playerY = player.pos.y;
            float playerDistFromCenter = std::sqrt(playerX * playerX + playerY * playerY);

            // Tier 1: Corner formations
            // Expected distance: sqrt(2048² + 2560²) ≈ 3279 units
            if (playerDistFromCenter < config.cornerThreshold) {
                return FORMATION_CORNER;
            }
            // Tier 2: Back formations
            // Expected distance: sqrt(256² + 3840²) ≈ 3850 units
            else if (playerDistFromCenter < config.backThreshold) {
                return FORMATION_BACK;
            }
            // Tier 3: Far back center
            // Expected distance: 4608 units
            else {
                return FORMATION_FAR_BACK;
            }
        }

        const char* GetFormationName(KickoffFormation formation) const {
            switch (formation) {
            case FORMATION_CORNER:
                return "CORNER";
            case FORMATION_BACK:
                return "BACK";
            case FORMATION_FAR_BACK:
                return "FAR_BACK";
            default:
                return "UNKNOWN";
            }
        }

    public:
        KickoffContactPunishmentReward() : config() {}

        explicit KickoffContactPunishmentReward(const KickoffTierConfig& cfg) : config(cfg) {}

        virtual void Reset(const GameState& initialState) override {
            kickoffTimer.clear();
            hasContactedBall.clear();
            wasKickingOff.clear();
            kickoffFormation.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!config.enablePunishment) return 0.0f;

            bool currentlyKickingOff = IsKickoffFrame(state);
            bool wasKickingOffBefore = wasKickingOff.count(player.carId) && wasKickingOff[player.carId];

            // Reset tracking when kickoff ends
            if (wasKickingOffBefore && !currentlyKickingOff) {
                hasContactedBall[player.carId] = false;
                kickoffTimer[player.carId] = 0.0f;
                wasKickingOff[player.carId] = false;
                kickoffFormation.erase(player.carId);
                return 0.0f;
            }

            if (!currentlyKickingOff) {
                return 0.0f;
            }

            // Mark that we're in kickoff
            wasKickingOff[player.carId] = true;

            // Determine formation on first frame of kickoff
            if (kickoffFormation.find(player.carId) == kickoffFormation.end()) {
                kickoffFormation[player.carId] = DetermineKickoffFormation(player, state);
            }

            // Initialize timer if needed
            if (kickoffTimer.find(player.carId) == kickoffTimer.end()) {
                kickoffTimer[player.carId] = 0.0f;
                hasContactedBall[player.carId] = false;
            }

            // Check if player has contacted ball
            float carBallDist = player.pos.Dist(state.ball.pos);
            bool likelyContacted = carBallDist < 150.0f;

            if (likelyContacted) {
                hasContactedBall[player.carId] = true;
                kickoffTimer[player.carId] = 0.0f; // Reset timer on contact
                return 0.0f; // No reward/punishment when in contact
            }

            // Increment timer
            float& timer = kickoffTimer[player.carId];
            timer += RLGC::CommonValues::TICK_TIME;

            // Get appropriate thresholds based on formation
            float maxTime;
            float punishmentPerSecond;

            switch (kickoffFormation[player.carId]) {
            case FORMATION_CORNER:
                maxTime = config.cornerMaxTimeWithoutContact;
                punishmentPerSecond = config.cornerPunishmentPerSecond;
                break;
            case FORMATION_BACK:
                maxTime = config.backMaxTimeWithoutContact;
                punishmentPerSecond = config.backPunishmentPerSecond;
                break;
            case FORMATION_FAR_BACK:
                maxTime = config.farBackMaxTimeWithoutContact;
                punishmentPerSecond = config.farBackPunishmentPerSecond;
                break;
            default:
                return 0.0f;
            }

            // Apply punishment if no contact and timeout exceeded
            if (!hasContactedBall[player.carId] && timer > maxTime) {
                float timeOverLimit = timer - maxTime;
                float punishment = punishmentPerSecond * timeOverLimit;
                return punishment;
            }

            return 0.0f;
        }

        // =====================================================================
        // CONFIGURATION SETTERS - TIER 1 (CORNER FORMATIONS)
        // =====================================================================

        void SetCornerMaxTimeWithoutContact(float seconds) {
            config.cornerMaxTimeWithoutContact = seconds;
        }

        void SetCornerPunishmentPerSecond(float punishment) {
            config.cornerPunishmentPerSecond = punishment;
        }

        void SetCornerConfig(float maxTime, float punishment) {
            config.cornerMaxTimeWithoutContact = maxTime;
            config.cornerPunishmentPerSecond = punishment;
        }

        // =====================================================================
        // CONFIGURATION SETTERS - TIER 2 (BACK FORMATIONS)
        // =====================================================================

        void SetBackMaxTimeWithoutContact(float seconds) {
            config.backMaxTimeWithoutContact = seconds;
        }

        void SetBackPunishmentPerSecond(float punishment) {
            config.backPunishmentPerSecond = punishment;
        }

        void SetBackConfig(float maxTime, float punishment) {
            config.backMaxTimeWithoutContact = maxTime;
            config.backPunishmentPerSecond = punishment;
        }

        // =====================================================================
        // CONFIGURATION SETTERS - TIER 3 (FAR BACK CENTER)
        // =====================================================================

        void SetFarBackMaxTimeWithoutContact(float seconds) {
            config.farBackMaxTimeWithoutContact = seconds;
        }

        void SetFarBackPunishmentPerSecond(float punishment) {
            config.farBackPunishmentPerSecond = punishment;
        }

        void SetFarBackConfig(float maxTime, float punishment) {
            config.farBackMaxTimeWithoutContact = maxTime;
            config.farBackPunishmentPerSecond = punishment;
        }

        // =====================================================================
        // MASTER CONFIGURATION - ALL TIERS
        // =====================================================================

        void SetAllConfig(
            float cornerTime, float cornerPunish,
            float backTime, float backPunish,
            float farBackTime, float farBackPunish) {
            config.cornerMaxTimeWithoutContact = cornerTime;
            config.cornerPunishmentPerSecond = cornerPunish;
            config.backMaxTimeWithoutContact = backTime;
            config.backPunishmentPerSecond = backPunish;
            config.farBackMaxTimeWithoutContact = farBackTime;
            config.farBackPunishmentPerSecond = farBackPunish;
        }

        // =====================================================================
        // THRESHOLD CONFIGURATION
        // =====================================================================

        void SetCornerThreshold(float threshold) {
            config.cornerThreshold = threshold;
        }

        void SetBackThreshold(float threshold) {
            config.backThreshold = threshold;
        }

        void SetThresholds(float cornerThreshold, float backThreshold) {
            config.cornerThreshold = cornerThreshold;
            config.backThreshold = backThreshold;
        }

        // =====================================================================
        // UTILITY CONFIGURATION
        // =====================================================================

        void SetEnablePunishment(bool enable) {
            config.enablePunishment = enable;
        }

        void SetPositionTolerance(float tolerance) {
            config.positionTolerance = tolerance;
        }

        // =====================================================================
        // PRESET CONFIGURATIONS
        // =====================================================================

        void SetDifficultyPreset(float difficulty) {
            // difficulty: 0.0 = relaxed, 1.0 = balanced, 2.0 = professional

            if (difficulty <= 0.5f) {
                // RELAXED
                SetAllConfig(
                    1.5f, -0.8f,    // corner: lenient
                    1.8f, -0.6f,    // back: lenient
                    2.2f, -0.4f     // far back: lenient
                );
            }
            else if (difficulty <= 1.5f) {
                // BALANCED (default)
                SetAllConfig(
                    1.2f, -1.5f,    // corner: standard
                    1.5f, -1.0f,    // back: standard
                    1.9f, -0.7f     // far back: standard
                );
            }
            else {
                // PROFESSIONAL
                SetAllConfig(
                    1.0f, -2.0f,    // corner: strict
                    1.3f, -1.5f,    // back: strict
                    1.7f, -1.2f     // far back: strict
                );
            }
        }

        void SetProfessionalPreset() {
            SetDifficultyPreset(2.0f);
        }

        void SetBalancedPreset() {
            SetDifficultyPreset(1.0f);
        }

        void SetRelaxedPreset() {
            SetDifficultyPreset(0.0f);
        }

        // =====================================================================
        // LEGACY COMPATIBILITY (2-tier to 3-tier)
        // =====================================================================
        // These methods provide backwards compatibility with older 2-tier code
        // "Straight" maps to CORNER, "Diagonal" maps to BACK+FAR_BACK average

        void SetMaxTimeWithoutContact(float seconds) {
            // Sets corner only (most common formation)
            config.cornerMaxTimeWithoutContact = seconds;
        }

        void SetPunishmentPerSecond(float punishment) {
            // Sets corner only
            config.cornerPunishmentPerSecond = punishment;
        }

        void SetConfig(float maxTime, float punishment) {
            SetMaxTimeWithoutContact(maxTime);
            SetPunishmentPerSecond(punishment);
        }

        // =====================================================================
        // DETECTION VALIDATION (For debugging)
        // =====================================================================

        KickoffFormation GetPlayerFormation(const Player& player, const GameState& state) const {
            return DetermineKickoffFormation(player, state);
        }

        const char* GetPlayerFormationName(const Player& player, const GameState& state) const {
            return GetFormationName(DetermineKickoffFormation(player, state));
        }

        float GetPlayerDistanceFromCenter(const Player& player) const {
            float playerX = player.pos.x;
            float playerY = player.pos.y;
            return std::sqrt(playerX * playerX + playerY * playerY);
        }

        float GetExpectedContactTime(const Player& player, const GameState& state) const {
            switch (DetermineKickoffFormation(player, state)) {
            case FORMATION_CORNER:
                return config.cornerMaxTimeWithoutContact;
            case FORMATION_BACK:
                return config.backMaxTimeWithoutContact;
            case FORMATION_FAR_BACK:
                return config.farBackMaxTimeWithoutContact;
            default:
                return 0.0f;
            }
        }
    };


    // ============================================================================
    // KickoffBallPositionReward - BRONZE TIER
    // Rewards/punishes kickoff outcome based on ball position and velocity
    // If ball lands on opponent side -> WIN (reward)
    // If ball lands on our side -> LOSS (punishment)
    // If ball traveling away from our side -> slight reward
    // If ball traveling toward our side -> slight punishment
    // 2025-12-23 | Rev 1.0 | NJH
    // ============================================================================

    class KickoffBallPositionReward : public Reward {
    private:
        struct KickoffBallConfig {
            // Landing position rewards/punishments
            float opponentSideLandingReward = 2.0f;    // Ball landed on opponent's side
            float ownSideLandingPunishment = -2.0f;    // Ball landed on our side

            // Velocity direction rewards/punishments
            float travelingAwayReward = 0.3f;          // Ball moving away from our goal
            float travelingTowardPunishment = -0.3f;   // Ball moving toward our goal

            // Distance thresholds
            float groundContactThreshold = 150.0f;     // Ball height to consider "landed"
            float minBallVelocity = 100.0f;            // Min velocity to evaluate direction

            // Timing: apply reward after kickoff ends
            float kickoffCheckDelay = 3.0f;            // Check results after 3 seconds
            bool enablePositionReward = true;
        };

        mutable std::map<uint32_t, bool> wasKickingOff;
        mutable std::map<uint32_t, float> kickoffEndTimer;
        mutable std::map<uint32_t, bool> hasAppliedReward;

        KickoffBallConfig config;

        bool IsKickoffFrame(const GameState& state) const {
            float ballDistFromCenter = state.ball.pos.Length();
            return ballDistFromCenter < 500.0f;
        }

        bool IsBallOnTeamSide(const Player& player, const GameState& state) const {
            // Check which side of the field the ball is on
            // Y > 0 is orange side, Y < 0 is blue side
            bool ballOnOrangeSide = state.ball.pos.y > 0.0f;
            bool playerIsOrange = player.team == Team::ORANGE;

            // Ball on our side = true
            return (ballOnOrangeSide == playerIsOrange);
        }

        bool IsBallTravelingTowardOurGoal(const Player& player, const GameState& state) const {
            // Determine goal direction based on team
            // Blue (team 0): our goal is at -Y, so toward goal = negative Y
            // Orange (team 1): our goal is at +Y, so toward goal = positive Y

            Vec goalDirection = (player.team == Team::BLUE)
                ? Vec(0.0f, -1.0f, 0.0f)
                : Vec(0.0f, 1.0f, 0.0f);

            if (state.ball.vel.Length() < config.minBallVelocity) {
                return false; // Ball too slow to evaluate
            }

            float alignment = state.ball.vel.Normalized().Dot(goalDirection);
            return alignment > 0.1f; // Moving toward our goal
        }

    public:
        KickoffBallPositionReward() : config() {}

        explicit KickoffBallPositionReward(const KickoffBallConfig& cfg) : config(cfg) {}

        virtual void Reset(const GameState& initialState) override {
            wasKickingOff.clear();
            kickoffEndTimer.clear();
            hasAppliedReward.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!config.enablePositionReward) return 0.0f;

            bool currentlyKickingOff = IsKickoffFrame(state);

            // Transition: kickoff just ended
            if (wasKickingOff.count(player.carId) && wasKickingOff[player.carId] && !currentlyKickingOff) {
                kickoffEndTimer[player.carId] = 0.0f;
                hasAppliedReward[player.carId] = false;
            }

            wasKickingOff[player.carId] = currentlyKickingOff;

            // Only apply reward after kickoff has ended and some time has passed
            if (currentlyKickingOff || hasAppliedReward.count(player.carId) && hasAppliedReward[player.carId]) {
                return 0.0f;
            }

            // Increment timer post-kickoff
            if (kickoffEndTimer.find(player.carId) == kickoffEndTimer.end()) {
                kickoffEndTimer[player.carId] = 0.0f;
            }

            kickoffEndTimer[player.carId] += CommonValues::TICK_TIME;

            // Apply reward once delay has passed
            if (kickoffEndTimer[player.carId] < config.kickoffCheckDelay) {
                return 0.0f;
            }

            if (hasAppliedReward[player.carId]) {
                return 0.0f; // Already applied reward
            }

            hasAppliedReward[player.carId] = true;

            float reward = 0.0f;

            // =====================================================================
            // PRIMARY REWARD: Ball position
            // =====================================================================
            bool ballOnOurSide = IsBallOnTeamSide(player, state);

            if (ballOnOurSide) {
                // Ball ended up on our side = LOSS
                reward += config.ownSideLandingPunishment;
            }
            else {
                // Ball ended up on opponent's side = WIN
                reward += config.opponentSideLandingReward;
            }

            // =====================================================================
            // SECONDARY REWARD: Ball velocity direction
            // =====================================================================
            bool ballTravelingTowardUs = IsBallTravelingTowardOurGoal(player, state);

            if (ballTravelingTowardUs) {
                reward += config.travelingTowardPunishment;
            }
            else {
                reward += config.travelingAwayReward;
            }

            return reward;
        }

        // Configuration setters
        void SetOpponentSideLandingReward(float reward) { config.opponentSideLandingReward = reward; }
        void SetOwnSideLandingPunishment(float punishment) { config.ownSideLandingPunishment = punishment; }
        void SetTravelingAwayReward(float reward) { config.travelingAwayReward = reward; }
        void SetTravelingTowardPunishment(float punishment) { config.travelingTowardPunishment = punishment; }
        void SetKickoffCheckDelay(float seconds) { config.kickoffCheckDelay = seconds; }
        void SetEnablePositionReward(bool enable) { config.enablePositionReward = enable; }
        void SetConfig(float oppReward, float ownPunish, float awayReward, float towardPunish) {
            config.opponentSideLandingReward = oppReward;
            config.ownSideLandingPunishment = ownPunish;
            config.travelingAwayReward = awayReward;
            config.travelingTowardPunishment = towardPunish;
        }
    };


    // ============================================================================
    // KickoffCompositeReward - CONVENIENCE
    // Combines both kickoff rewards for easier use
    // Supports diagonal/straight timing differences
    // 2025-12-23 | Rev 2.0 | NJH
    // ============================================================================

    class KickoffCompositeReward : public Reward {
    private:
        KickoffContactPunishmentReward contactReward;
        KickoffBallPositionReward positionReward;

    public:
        KickoffCompositeReward()
            : contactReward(), positionReward() {
        }

        virtual void Reset(const GameState& initialState) override {
            contactReward.Reset(initialState);
            positionReward.Reset(initialState);
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float contact = contactReward.GetReward(player, state, isFinal);
            float position = positionReward.GetReward(player, state, isFinal);
            return contact + position;
        }

        // Accessors for configuration
        KickoffContactPunishmentReward* GetContactReward() { return &contactReward; }
        KickoffBallPositionReward* GetPositionReward() { return &positionReward; }
    };

    class FlipDuringKickoffReward : public Reward {
    private:
        float speed_threshold = 300.0f; // Minimum speed delta to detect a flip
        float angle_threshold = 0.3f; // Cosine threshold for "towards ball" direction (~73 degrees)
        float diagonal_threshold = 0.5f; // Cosine threshold for diagonal detection

        mutable std::map<uint32_t, Vec> prev_velocity;
        mutable std::map<uint32_t, bool> was_on_ground;
        mutable float kickoff_start_time = 0.0f;
        mutable bool kickoff_active = false;

        float kickoff_detect_ball_pos = 280.0f;
        float kickoff_detect_ball_vel = 200.0f;
        float kickoff_max_duration = 3.5f;

    public:
        virtual void Reset(const GameState& initialState) override {
            prev_velocity.clear();
            was_on_ground.clear();
            kickoff_start_time = 0.0f;
            kickoff_active = false;
        }

        virtual void PreStep(const GameState& state) override {
            float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

            bool is_kickoff_now = (state.ball.pos.Length() < kickoff_detect_ball_pos &&
                state.ball.vel.Length() < kickoff_detect_ball_vel);

            // Kickoff start
            if (is_kickoff_now && !kickoff_active) {
                kickoff_active = true;
                kickoff_start_time = 0.0f;
                prev_velocity.clear();
                was_on_ground.clear();
                for (const auto& player : state.players) {
                    was_on_ground[player.carId] = true;
                }
            }

            if (kickoff_active) {
                kickoff_start_time += dt;
                if (!is_kickoff_now || kickoff_start_time >= kickoff_max_duration) {
                    kickoff_active = false;
                }
            }
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            // Only reward during kickoff
            if (!kickoff_active) {
                return 0.0f;
            }

            // Must be in the air to perform a flip
            if (player.isOnGround) {
                was_on_ground[player.carId] = true;
                return 0.0f;
            }

            // Only detect flip in the first frame after leaving ground
            bool just_left_ground = was_on_ground[player.carId];
            was_on_ground[player.carId] = false;

            if (!just_left_ground) {
                // Already in air from previous frame, don't count as flip
                prev_velocity[player.carId] = player.vel;
                return 0.0f;
            }

            // Get previous velocity
            if (prev_velocity.find(player.carId) == prev_velocity.end()) {
                prev_velocity[player.carId] = player.vel;
                return 0.0f;
            }

            Vec prev_vel = prev_velocity[player.carId];
            float speed_delta = player.vel.Length() - prev_vel.Length();

            // Check if flip occurred (significant speed increase)
            if (speed_delta < speed_threshold) {
                prev_velocity[player.carId] = player.vel;
                return 0.0f;
            }

            // Direction from car to ball
            Vec car_to_ball = state.ball.pos - player.pos;
            float dist = car_to_ball.Length();
            if (dist < 1e-3f) {
                prev_velocity[player.carId] = player.vel;
                return 0.0f;
            }
            car_to_ball = car_to_ball / dist;

            // Get flip direction from velocity change
            Vec vel_delta = player.vel - prev_vel;
            float vel_delta_mag = vel_delta.Length();
            if (vel_delta_mag < 1e-3f) {
                prev_velocity[player.carId] = player.vel;
                return 0.0f;
            }
            Vec flip_direction = vel_delta / vel_delta_mag;

            // Dot products
            float forward_dot = flip_direction.Dot(car_to_ball);
            float right_dot = flip_direction.Dot(player.rotMat.right);

            prev_velocity[player.carId] = player.vel;

            // Check for speed flip first (highest priority)
            if (IsSpeedFlip(player, state)) {
                return 1.0f;
            }
            // Check for diagonal flip towards ball
            else if (IsDiagonalFlip(forward_dot, right_dot) && forward_dot > angle_threshold) {
                return 0.66f;
            }
            // Check for front flip towards ball
            else if (IsFrontFlip(forward_dot, right_dot) && forward_dot > angle_threshold) {
                return 0.33f;
            }

            return 0.0f;
        }

    private:
        bool IsFrontFlip(float forward_dot, float right_dot) const {
            // Front flip: high forward component, minimal right component
            return forward_dot > diagonal_threshold && std::abs(right_dot) < 0.2f;
        }

        bool IsDiagonalFlip(float forward_dot, float right_dot) const {
            // Diagonal flip: both forward and right components significant
            return forward_dot > angle_threshold && std::abs(right_dot) > 0.3f && std::abs(right_dot) < 0.9f;
        }

        bool IsSpeedFlip(const Player& player, const GameState& state) const {
            // Speed flip detection:
            // 1. Diagonal flip input (forward + right/left velocity components)
            // 2. Negative pitch applied (car tilted back)
            // 3. Boost and air roll active

            // Check if car has negative pitch (tilted back)
            Vec car_up = player.rotMat.up;
            float pitch = std::asin(-car_up.z) * 57.2958f; // Convert to degrees
            bool has_negative_pitch = pitch < -30.0f;

            // Check if boost is being used
            bool boost_active = player.boost < 99.9f;

            // Check for air roll via angular velocity around z-axis
            bool air_roll_active = std::abs(player.angVel.z) > 2.0f;

            // Check diagonal velocity components (both forward and sideways)
            float forward_vel = player.vel.Dot(player.rotMat.forward);
            float right_vel = player.vel.Dot(player.rotMat.right);
            bool is_diagonal = std::abs(forward_vel) > 200.0f && std::abs(right_vel) > 200.0f;

            return has_negative_pitch && boost_active && air_roll_active && is_diagonal;
        }
    };
} // namespace RLGymCPP