#pragma once
// PredictiveDemoRewards.h
// Rev 1.1 (2026-04-25, NJH + Claude)
//
// Seven demo rewards built around predicting and reacting to OPPONENT MOVEMENT
// rather than just chasing them. These train the bot to read intent, exploit
// commitment, and hunt vulnerability windows — not just go supersonic into people.
//
//   1. MomentumLeadDemoReward       - "lead the target" shot: aim at future position
//   2. BallChaserAmbushReward       - intercept opponent's path TO the ball
//   3. PostTouchShadowDemoReward    - predict follow-through vector after opp touch
//   4. AerialVulnerabilityDemoReward- demo opponent during aerial commitment window
//   5. RespawnHuntReward            - route to respawn zone and demo on re-entry
//   6. FunnelExitDemoReward         - position at corner exit, not the corner
//   7. DoubleCommitDemoReward       - man-advantage creation when all opps ball-chase
//
// Suggested weights (ALL in Shadow Mode first — at least 500M steps):
//   MomentumLead           : 2.5f  (phase 5+)
//   BallChaserAmbush       : 2.0f  (phase 4+)
//   PostTouchShadow        : 2.0f  (phase 4+)
//   AerialVulnerability    : 2.5f  (phase 5+)
//   RespawnHunt            : 1.5f  (phase 5+ — monitor for spawn camp abuse)
//   FunnelExit             : 2.0f  (phase 4+)
//   DoubleCommit           : 2.0f  (phase 5+, 2v2/3v3 only)
//
// All rewards gate on demo event (player.eventState.demo) as final confirmation.
// Prediction quality determines the multiplier — the reward is for the PLAN
// matching the EXECUTION, not just for the demo itself.
//
// Rev 1.1 changes: Added Config (shadowMode), Metrics structs, inline static atomics,
// and GetAndResetMetrics() to all 7 classes. Shadow mode: metrics always accumulate,
// reward only paid to optimizer when shadowMode == false.

#include "Reward.h"
#include "../CommonValues.h"
#include <atomic>
#include <map>
#include <set>
#include <vector>
#include <cmath>
#include <algorithm>

namespace RLGC {

    // ============================================================================
    // 1. MomentumLeadDemoReward
    // ============================================================================
    // Rewards demos where the bot aimed AHEAD of the opponent's current position —
    // the classic "lead the target" technique used in ballistics and sport.
    //
    // Key W&B metrics: DemosLed (total successes), AvgLeadAlign (quality of lead),
    // AvgSpeedFactor (were targets actually fast?). If DemosLed = 0 after 50M steps,
    // lower MIN_LEAD_ALIGNMENT to 0.45.
    // ============================================================================
    class MomentumLeadDemoReward : public Reward {
    public:
        struct Config {
            bool shadowMode = false;
        };

        struct Metrics {
            int    trackingEntered  = 0;   // times tracking state entered (opponent locked)
            int    demosLed         = 0;   // successful led demos
            double avgLeadAlign     = 0.0; // mean ls.leadAlignment on success
            double avgSpeedFactor   = 0.0; // mean speedFactor on success
            double rewardSum        = 0.0;
        };

    private:
        static constexpr float PREDICT_SECS        = 1.2f;
        static constexpr float MIN_OPP_SPEED        = 600.0f;
        static constexpr float MIN_LEAD_ALIGNMENT   = 0.55f;
        static constexpr float TRACKING_RADIUS      = 2500.0f;
        static constexpr float BASE_REWARD          = 1.5f;
        static constexpr float MAX_SPEED_BONUS      = 1.5f;
        static constexpr float MAX_OPP_SPEED_SCALE  = 2200.0f;
        static constexpr float COOLDOWN_SECS        = 4.0f;

        struct LeadState {
            bool     tracking       = false;
            uint32_t targetId       = 0;
            Vec      predictedPos   = {};
            float    oppSpeed       = 0.0f;
            float    leadAlignment  = 0.0f;
            float    cooldown       = 0.0f;
        };

        mutable std::map<uint32_t, LeadState> states;
        Config config;

        // Metrics — thread-safe atomics
        inline static std::atomic<int>       s_trackingEntered { 0 };
        inline static std::atomic<int>       s_demosLed        { 0 };
        inline static std::atomic<long long> s_leadAlignSum    { 0 }; // ×1000
        inline static std::atomic<long long> s_speedFactorSum  { 0 }; // ×1000
        inline static std::atomic<long long> s_rewardMilli     { 0 };

        Vec PredictOppPos(const Player& opp, float dt) const {
            return opp.pos + opp.vel * dt;
        }

    public:
        MomentumLeadDemoReward() : config() {}
        explicit MomentumLeadDemoReward(const Config& cfg) : config(cfg) {}

        static Metrics GetAndResetMetrics() {
            Metrics m;
            m.trackingEntered = s_trackingEntered.exchange(0);
            m.demosLed        = s_demosLed.exchange(0);
            long long alignRaw = s_leadAlignSum.exchange(0);
            long long speedRaw = s_speedFactorSum.exchange(0);
            long long rewRaw   = s_rewardMilli.exchange(0);
            m.rewardSum = double(rewRaw) / 1000.0;
            if (m.demosLed > 0) {
                m.avgLeadAlign   = double(alignRaw) / (1000.0 * m.demosLed);
                m.avgSpeedFactor = double(speedRaw) / (1000.0 * m.demosLed);
            }
            return m;
        }

        virtual void Reset(const GameState& initialState) override { states.clear(); }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            uint32_t id = player.carId;
            float dt    = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            LeadState& ls = states[id];

            if (ls.cooldown > 0.0f) { ls.cooldown -= dt; return 0.0f; }

            const Player* target = nullptr;
            for (const auto& opp : state.players) {
                if (opp.team == player.team || opp.isDemoed) continue;
                float d = (opp.pos - player.pos).Length();
                if (d < TRACKING_RADIUS && opp.vel.Length() > MIN_OPP_SPEED) {
                    target = &opp;
                    break;
                }
            }

            if (!target) {
                ls.tracking = false;
                return 0.0f;
            }

            Vec predicted  = PredictOppPos(*target, PREDICT_SECS);
            Vec toPredict  = (predicted - player.pos).Normalized();
            Vec toCurrent  = (target->pos - player.pos).Normalized();
            Vec myVelN     = player.vel.Normalized();

            float alignToPredicted = myVelN.Dot(toPredict);
            float alignToCurrent   = myVelN.Dot(toCurrent);

            bool isLeading = (alignToPredicted > alignToCurrent + 0.05f) &&
                             (alignToPredicted > MIN_LEAD_ALIGNMENT);

            if (isLeading) {
                if (!ls.tracking)
                    s_trackingEntered.fetch_add(1);
                ls.tracking      = true;
                ls.targetId      = target->carId;
                ls.predictedPos  = predicted;
                ls.oppSpeed      = target->vel.Length();
                ls.leadAlignment = RS_MAX(ls.leadAlignment, alignToPredicted);
            }

            if (player.eventState.demo && ls.tracking) {
                const Player* demoed = nullptr;
                for (const auto& opp : state.players) {
                    if (opp.team == player.team) continue;
                    if (opp.isDemoed && (player.pos - opp.pos).Length() < 350.0f) {
                        demoed = &opp;
                        break;
                    }
                }

                if (demoed && demoed->carId == ls.targetId) {
                    float speedFactor = RS_CLAMP(ls.oppSpeed / MAX_OPP_SPEED_SCALE, 0.0f, 1.0f);
                    float alignBonus  = RS_CLAMP((ls.leadAlignment - MIN_LEAD_ALIGNMENT) /
                                                 (1.0f - MIN_LEAD_ALIGNMENT), 0.0f, 1.0f);
                    float reward = BASE_REWARD + speedFactor * MAX_SPEED_BONUS * alignBonus;

                    // Metrics always accumulate
                    s_demosLed.fetch_add(1);
                    s_leadAlignSum.fetch_add(static_cast<long long>(ls.leadAlignment * 1000.0f));
                    s_speedFactorSum.fetch_add(static_cast<long long>(speedFactor * 1000.0f));
                    s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));

                    ls.tracking      = false;
                    ls.leadAlignment = 0.0f;
                    ls.cooldown      = COOLDOWN_SECS;
                    return config.shadowMode ? 0.0f : reward;
                }
            }

            if (!ls.tracking || !target) ls.leadAlignment = 0.0f;

            return 0.0f;
        }
    };


    // ============================================================================
    // 2. BallChaserAmbushReward
    // ============================================================================
    // The opponent is sprinting toward the ball — ball-locked, not watching for
    // incoming cars. Reward demoing them while they're in active ball-chase mode.
    //
    // Key W&B metrics: Setups (ambush opportunities), DemosConverted (conversions),
    // AvgBonusQuality (how oblivious was the target?). Watch for high Setups but
    // near-zero DemosConverted — means bot spots the window but doesn't close.
    // ============================================================================
    class BallChaserAmbushReward : public Reward {
    public:
        struct Config {
            bool shadowMode = false;
        };

        struct Metrics {
            int    ambushSetups    = 0;   // times pendingBonus was written
            int    demosConverted  = 0;   // demos while pending was active
            double avgBonusQuality = 0.0; // mean bonus at payout (0–1)
            double rewardSum       = 0.0;
        };

    private:
        static constexpr float CHASE_RADIUS         = 2200.0f;
        static constexpr float MIN_CHASE_ALIGNMENT  = 0.72f;
        static constexpr float MAX_AWARENESS        = 0.40f;
        static constexpr float MIN_BOT_APPROACH     = 800.0f;
        static constexpr float BASE_REWARD          = 2.0f;
        static constexpr float OBLIVIOUS_BONUS      = 1.0f;
        static constexpr float COOLDOWN_SECS        = 4.0f;

        mutable std::map<uint32_t, float>    cooldowns;
        mutable std::map<uint32_t, float>    pendingBonus;
        mutable std::map<uint32_t, uint32_t> pendingTarget;
        Config config;

        // Metrics — thread-safe atomics
        inline static std::atomic<int>       s_ambushSetups   { 0 };
        inline static std::atomic<int>       s_demosConverted { 0 };
        inline static std::atomic<long long> s_bonusQualSum   { 0 }; // ×1000
        inline static std::atomic<long long> s_rewardMilli    { 0 };

    public:
        BallChaserAmbushReward() : config() {}
        explicit BallChaserAmbushReward(const Config& cfg) : config(cfg) {}

        static Metrics GetAndResetMetrics() {
            Metrics m;
            m.ambushSetups   = s_ambushSetups.exchange(0);
            m.demosConverted = s_demosConverted.exchange(0);
            long long bqRaw  = s_bonusQualSum.exchange(0);
            long long rewRaw = s_rewardMilli.exchange(0);
            m.rewardSum = double(rewRaw) / 1000.0;
            if (m.demosConverted > 0)
                m.avgBonusQuality = double(bqRaw) / (1000.0 * m.demosConverted);
            return m;
        }

        virtual void Reset(const GameState& initialState) override {
            cooldowns.clear();
            pendingBonus.clear();
            pendingTarget.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            uint32_t id = player.carId;
            float dt    = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

            if (cooldowns[id] > 0.0f) { cooldowns[id] -= dt; return 0.0f; }

            for (const auto& opp : state.players) {
                if (opp.team == player.team || opp.isDemoed) continue;

                float oppToBallDist = (opp.pos - state.ball.pos).Length();
                if (oppToBallDist > CHASE_RADIUS) continue;

                float oppSpeed = opp.vel.Length();
                if (oppSpeed < 400.0f) continue;

                Vec oppToBallDir   = (state.ball.pos - opp.pos).Normalized();
                float chaseAlign   = opp.vel.Normalized().Dot(oppToBallDir);
                if (chaseAlign < MIN_CHASE_ALIGNMENT) continue;

                Vec oppToBotDir      = (player.pos - opp.pos).Normalized();
                float awarenessAlign = opp.vel.Normalized().Dot(oppToBotDir);
                bool oblivious       = (awarenessAlign < MAX_AWARENESS);

                Vec botToOppDir   = (opp.pos - player.pos).Normalized();
                float botApproach = player.vel.Dot(botToOppDir);
                if (botApproach < MIN_BOT_APPROACH) continue;

                float chaseQ    = RS_CLAMP((chaseAlign - MIN_CHASE_ALIGNMENT) /
                                           (1.0f - MIN_CHASE_ALIGNMENT), 0.0f, 1.0f);
                float awarenessQ = oblivious ?
                    RS_CLAMP(1.0f - (awarenessAlign / MAX_AWARENESS), 0.0f, 1.0f) : 0.0f;
                float bonus = chaseQ * 0.5f + awarenessQ * 0.5f;

                bool newSetup = (pendingTarget.find(id) == pendingTarget.end() ||
                                 pendingTarget[id] != opp.carId);
                if (newSetup)
                    s_ambushSetups.fetch_add(1);

                pendingBonus[id]  = bonus;
                pendingTarget[id] = opp.carId;
            }

            if (player.eventState.demo) {
                auto it = pendingTarget.find(id);
                if (it != pendingTarget.end()) {
                    float bonus  = pendingBonus.count(id) ? pendingBonus[id] : 0.0f;
                    float reward = BASE_REWARD + bonus * OBLIVIOUS_BONUS;

                    s_demosConverted.fetch_add(1);
                    s_bonusQualSum.fetch_add(static_cast<long long>(bonus * 1000.0f));
                    s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));

                    cooldowns[id] = COOLDOWN_SECS;
                    pendingBonus.erase(id);
                    pendingTarget.erase(id);
                    return config.shadowMode ? 0.0f : reward;
                }
            }

            return 0.0f;
        }
    };


    // ============================================================================
    // 3. PostTouchShadowDemoReward
    // ============================================================================
    // Opponent just touched the ball. Their follow-through vector is predictable.
    // Bot uses that vector to intercept them for a demo.
    //
    // Key W&B metrics (per Nathan's notes):
    //   PreReadBonusFired — bot was ALREADY moving to the predicted position BEFORE
    //   the touch. This is the hardest and most interesting condition. Watch this
    //   ratio vs BaseRewardFired closely; a rising PreReadBonusFired/total ratio
    //   means the bot is developing genuine predictive read.
    // ============================================================================
    class PostTouchShadowDemoReward : public Reward {
    public:
        struct Config {
            bool shadowMode = false;
        };

        struct Metrics {
            int    touchesDetected       = 0;   // opp touches that started a shadow window
            int    preReadWindows        = 0;   // touches where preRead was already true
            int    windowsExpired        = 0;   // windows that timed out without demo
            int    baseRewardFired       = 0;   // demos WITHOUT pre-read bonus
            int    preReadBonusFired     = 0;   // demos WITH pre-read bonus  ← KEY metric
            double avgPredictionAccuracy = 0.0;
            double rewardSum             = 0.0;
        };

    private:
        static constexpr float SHADOW_PREDICT_SECS   = 1.0f;
        static constexpr float INTERCEPT_WINDOW_SECS = 2.0f;
        static constexpr float PRE_READ_ALIGNMENT    = 0.6f;
        static constexpr float BASE_REWARD           = 2.0f;
        static constexpr float PRE_READ_MULT         = 1.5f;
        static constexpr float COOLDOWN_SECS         = 4.0f;
        static constexpr float PREDICTION_TOLERANCE  = 600.0f;

        struct ShadowState {
            bool     active       = false;
            uint32_t targetId     = 0;
            Vec      predictedPos = {};
            float    windowTimer  = 0.0f;
            bool     preReadBonus = false;
            float    cooldown     = 0.0f;
        };

        mutable std::map<uint32_t, ShadowState> states;
        Config config;

        // Metrics — thread-safe atomics
        inline static std::atomic<int>       s_touchesDetected   { 0 };
        inline static std::atomic<int>       s_preReadWindows    { 0 };
        inline static std::atomic<int>       s_windowsExpired    { 0 };
        inline static std::atomic<int>       s_baseRewardFired   { 0 };
        inline static std::atomic<int>       s_preReadBonusFired { 0 };
        inline static std::atomic<long long> s_accuracySum       { 0 }; // ×1000
        inline static std::atomic<long long> s_rewardMilli       { 0 };

    public:
        PostTouchShadowDemoReward() : config() {}
        explicit PostTouchShadowDemoReward(const Config& cfg) : config(cfg) {}

        static Metrics GetAndResetMetrics() {
            Metrics m;
            m.touchesDetected   = s_touchesDetected.exchange(0);
            m.preReadWindows    = s_preReadWindows.exchange(0);
            m.windowsExpired    = s_windowsExpired.exchange(0);
            m.baseRewardFired   = s_baseRewardFired.exchange(0);
            m.preReadBonusFired = s_preReadBonusFired.exchange(0);
            long long accRaw    = s_accuracySum.exchange(0);
            long long rewRaw    = s_rewardMilli.exchange(0);
            m.rewardSum = double(rewRaw) / 1000.0;
            int totalFired = m.baseRewardFired + m.preReadBonusFired;
            if (totalFired > 0)
                m.avgPredictionAccuracy = double(accRaw) / (1000.0 * totalFired);
            return m;
        }

        virtual void Reset(const GameState& initialState) override { states.clear(); }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            uint32_t id = player.carId;
            float dt    = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            ShadowState& ss = states[id];

            if (ss.cooldown > 0.0f) { ss.cooldown -= dt; return 0.0f; }

            for (const auto& opp : state.players) {
                if (opp.team == player.team || opp.isDemoed) continue;
                if (!opp.ballTouchedStep) continue;

                Vec predictedPos   = opp.pos + opp.vel * SHADOW_PREDICT_SECS;
                Vec botToPredicted = (predictedPos - player.pos).Normalized();
                float preReadAlign = player.vel.Normalized().Dot(botToPredicted);
                bool preRead       = (preReadAlign > PRE_READ_ALIGNMENT &&
                                      player.vel.Length() > 600.0f);

                s_touchesDetected.fetch_add(1);
                if (preRead)
                    s_preReadWindows.fetch_add(1);

                ss.active       = true;
                ss.targetId     = opp.carId;
                ss.predictedPos = predictedPos;
                ss.windowTimer  = 0.0f;
                ss.preReadBonus = preRead;
            }

            if (ss.active) {
                ss.windowTimer += dt;

                if (ss.windowTimer > INTERCEPT_WINDOW_SECS) {
                    s_windowsExpired.fetch_add(1);
                    ss.active = false;
                    return 0.0f;
                }

                if (player.eventState.demo) {
                    const Player* demoed = nullptr;
                    for (const auto& opp : state.players) {
                        if (opp.team == player.team) continue;
                        if (opp.isDemoed && (player.pos - opp.pos).Length() < 400.0f) {
                            demoed = &opp;
                            break;
                        }
                    }

                    if (demoed && demoed->carId == ss.targetId) {
                        float predictionError = (player.pos - ss.predictedPos).Length();
                        float accuracy = RS_CLAMP(1.0f - predictionError / PREDICTION_TOLERANCE,
                                                  0.0f, 1.0f);
                        float reward = BASE_REWARD * accuracy;
                        if (ss.preReadBonus) reward *= PRE_READ_MULT;

                        if (ss.preReadBonus)
                            s_preReadBonusFired.fetch_add(1);
                        else
                            s_baseRewardFired.fetch_add(1);
                        s_accuracySum.fetch_add(static_cast<long long>(accuracy * 1000.0f));
                        s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));

                        ss.active   = false;
                        ss.cooldown = COOLDOWN_SECS;
                        return config.shadowMode ? 0.0f : reward;
                    }
                }
            }

            return 0.0f;
        }
    };


    // ============================================================================
    // 4. AerialVulnerabilityDemoReward
    // ============================================================================
    // Opponent commits to an aerial — airborne, boosting/flipping toward ball.
    // This is their maximum vulnerability window: predictable trajectory, can't dodge.
    //
    // Key W&B metrics: DemosInWindow (base activation), FlipBonusCount (highest tier),
    // AvgHeightBonus (are we catching genuine aerials?). If DemosInWindow = 0 after
    // 50M steps, lower COMMITTED_ALIGN to 0.45.
    // ============================================================================
    class AerialVulnerabilityDemoReward : public Reward {
    public:
        struct Config {
            bool shadowMode = false;
        };

        struct Metrics {
            int    demosInVulnWindow = 0;   // demos while opp was committed aerial
            int    flipBonusCount    = 0;   // demos where opp was flipping (highest tier)
            double avgHeightBonus    = 0.0;
            double rewardSum         = 0.0;
        };

    private:
        static constexpr float AERIAL_BALL_HEIGHT    = 400.0f;
        static constexpr float MIN_DEMO_HEIGHT       = 200.0f;
        static constexpr float COMMITTED_ALIGN       = 0.55f;
        static constexpr float BASE_REWARD           = 2.0f;
        static constexpr float FLIP_BONUS            = 1.0f;
        static constexpr float HEIGHT_SCALE          = 0.8f;
        static constexpr float MAX_HEIGHT_BONUS_Z    = 1500.0f;
        static constexpr float COOLDOWN_SECS         = 4.0f;

        struct VulnState {
            bool  oppCommitted = false;
            bool  oppFlipping  = false;
            float oppHeight    = 0.0f;
            float cooldown     = 0.0f;
        };

        mutable std::map<uint32_t, VulnState> states;
        Config config;

        // Metrics — thread-safe atomics
        inline static std::atomic<int>       s_demosInVulnWindow { 0 };
        inline static std::atomic<int>       s_flipBonusCount    { 0 };
        inline static std::atomic<long long> s_heightBonusSum    { 0 }; // ×1000
        inline static std::atomic<long long> s_rewardMilli       { 0 };

        const Player* FindCommittedAerialOpponent(const Player& player,
                                                  const GameState& state,
                                                  bool& wasFlipping,
                                                  float& height) const {
            const Player* best = nullptr;
            float bestCommit = 0.0f;

            for (const auto& opp : state.players) {
                if (opp.team == player.team || opp.isDemoed) continue;
                if (opp.isOnGround) continue;
                if (opp.pos.z < MIN_DEMO_HEIGHT) continue;
                if (state.ball.pos.z < AERIAL_BALL_HEIGHT) continue;

                Vec toBall  = (state.ball.pos - opp.pos).Normalized();
                float align = opp.vel.Normalized().Dot(toBall);
                if (align < COMMITTED_ALIGN) continue;

                float commitScore = align + (opp.isFlipping ? 0.3f : 0.0f) +
                                    RS_CLAMP(opp.pos.z / MAX_HEIGHT_BONUS_Z, 0.0f, 0.3f);

                if (commitScore > bestCommit) {
                    bestCommit  = commitScore;
                    best        = &opp;
                    wasFlipping = opp.isFlipping;
                    height      = opp.pos.z;
                }
            }
            return best;
        }

    public:
        AerialVulnerabilityDemoReward() : config() {}
        explicit AerialVulnerabilityDemoReward(const Config& cfg) : config(cfg) {}

        static Metrics GetAndResetMetrics() {
            Metrics m;
            m.demosInVulnWindow = s_demosInVulnWindow.exchange(0);
            m.flipBonusCount    = s_flipBonusCount.exchange(0);
            long long hbRaw     = s_heightBonusSum.exchange(0);
            long long rewRaw    = s_rewardMilli.exchange(0);
            m.rewardSum = double(rewRaw) / 1000.0;
            if (m.demosInVulnWindow > 0)
                m.avgHeightBonus = double(hbRaw) / (1000.0 * m.demosInVulnWindow);
            return m;
        }

        virtual void Reset(const GameState& initialState) override { states.clear(); }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            uint32_t id = player.carId;
            float dt    = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            VulnState& vs = states[id];

            if (vs.cooldown > 0.0f) { vs.cooldown -= dt; return 0.0f; }

            bool   flipping = false;
            float  height   = 0.0f;
            const Player* committed = FindCommittedAerialOpponent(player, state, flipping, height);
            vs.oppCommitted = (committed != nullptr);
            vs.oppFlipping  = flipping;
            vs.oppHeight    = height;

            if (player.eventState.demo && vs.oppCommitted) {
                bool botAirborne = (!player.isOnGround || player.pos.z > MIN_DEMO_HEIGHT);

                if (botAirborne) {
                    float heightBonus = RS_CLAMP(vs.oppHeight / MAX_HEIGHT_BONUS_Z, 0.0f, 1.0f)
                                        * HEIGHT_SCALE;
                    float flipBonus   = vs.oppFlipping ? FLIP_BONUS : 0.0f;
                    float reward      = BASE_REWARD + heightBonus + flipBonus;

                    s_demosInVulnWindow.fetch_add(1);
                    if (vs.oppFlipping) s_flipBonusCount.fetch_add(1);
                    s_heightBonusSum.fetch_add(static_cast<long long>(heightBonus * 1000.0f));
                    s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));

                    vs.cooldown = COOLDOWN_SECS;
                    return config.shadowMode ? 0.0f : reward;
                }
            }

            return 0.0f;
        }
    };


    // ============================================================================
    // 5. RespawnHuntReward
    // ============================================================================
    // After demoing an opponent, route to their respawn zone and demo them again
    // when they come back.
    //
    // Key W&B metrics:
    //   RoutedToSpawn — critical anti-farming gate. Must be > 0 for the reward to
    //   be working as intended (bot actively routed to spawn, not just wandered there).
    //   DoubleDemoCount — the real success metric; ambushPresence is partial credit.
    //   Watch for RespawnHunt/HuntsStarted being very high while DoubleDemoCount = 0
    //   which means hunts are starting but bot isn't closing them. In shadow mode,
    //   also watch for unusual spawn-camp patterns in RocketSimVis.
    // ============================================================================
    class RespawnHuntReward : public Reward {
    public:
        struct Config {
            bool shadowMode = false;
        };

        struct Metrics {
            int    huntsStarted        = 0;  // hunts initiated after first demo
            int    routedToSpawn       = 0;  // hunts where routedToSpawn became true
            int    ambushPresenceCount = 0;  // partial reward fires (0.5f)
            int    doubleDemoCount     = 0;  // full reward fires (2.5f) ← real success
            double rewardSum           = 0.0;
        };

    private:
        static constexpr float AMBUSH_RADIUS          = 1000.0f;
        static constexpr float RESPAWN_WINDOW_SECS    = 3.5f;
        static constexpr float ROUTING_SPEED          = 500.0f;
        static constexpr float AMBUSH_PRESENCE_REWARD = 0.5f;
        static constexpr float DOUBLE_DEMO_REWARD     = 2.5f;
        static constexpr float COOLDOWN_SECS          = 6.0f;

        static constexpr Vec RESPAWN_POSITIONS[8] = {
            CommonValues::BLUE_DEMOLISH_RIGHT_INSIDE,
            CommonValues::BLUE_DEMOLISH_RIGHT_OUTSIDE,
            CommonValues::BLUE_DEMOLISH_LEFT_INSIDE,
            CommonValues::BLUE_DEMOLISH_LEFT_OUTSIDE,
            CommonValues::ORANGE_DEMOLISH_RIGHT_INSIDE,
            CommonValues::ORANGE_DEMOLISH_RIGHT_OUTSIDE,
            CommonValues::ORANGE_DEMOLISH_LEFT_INSIDE,
            CommonValues::ORANGE_DEMOLISH_LEFT_OUTSIDE,
        };

        struct HuntState {
            bool     hunting         = false;
            uint32_t targetId        = 0;
            Vec      targetSpawn     = {};
            bool     routedToSpawn   = false;
            bool     targetDemoed    = false;
            float    respawnTimer    = 0.0f;
            bool     targetRespawned = false;
            bool     ambushPresent   = false;
            float    cooldown        = 0.0f;
        };

        mutable std::map<uint32_t, HuntState>   states;
        mutable std::map<uint32_t, uint32_t>    myDemoTargets;
        Config config;

        // Metrics — thread-safe atomics
        inline static std::atomic<int>       s_huntsStarted        { 0 };
        inline static std::atomic<int>       s_routedToSpawn       { 0 };
        inline static std::atomic<int>       s_ambushPresenceCount { 0 };
        inline static std::atomic<int>       s_doubleDemoCount     { 0 };
        inline static std::atomic<long long> s_rewardMilli         { 0 };

        Vec ClosestRespawnTo(const Vec& pos) const {
            float best = 9999999.0f;
            Vec bestPos = RESPAWN_POSITIONS[0];
            for (int i = 0; i < 8; i++) {
                float d = (RESPAWN_POSITIONS[i] - pos).Length();
                if (d < best) { best = d; bestPos = RESPAWN_POSITIONS[i]; }
            }
            return bestPos;
        }

    public:
        RespawnHuntReward() : config() {}
        explicit RespawnHuntReward(const Config& cfg) : config(cfg) {}

        static Metrics GetAndResetMetrics() {
            Metrics m;
            m.huntsStarted        = s_huntsStarted.exchange(0);
            m.routedToSpawn       = s_routedToSpawn.exchange(0);
            m.ambushPresenceCount = s_ambushPresenceCount.exchange(0);
            m.doubleDemoCount     = s_doubleDemoCount.exchange(0);
            long long rewRaw      = s_rewardMilli.exchange(0);
            m.rewardSum = double(rewRaw) / 1000.0;
            return m;
        }

        virtual void Reset(const GameState& initialState) override {
            states.clear();
            myDemoTargets.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            uint32_t id = player.carId;
            float dt    = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            HuntState& hs = states[id];

            if (hs.cooldown > 0.0f) { hs.cooldown -= dt; return 0.0f; }

            if (player.eventState.demo && !hs.hunting) {
                for (const auto& opp : state.players) {
                    if (opp.team == player.team) continue;
                    if (opp.isDemoed && (player.pos - opp.pos).Length() < 400.0f) {
                        hs.hunting         = true;
                        hs.targetId        = opp.carId;
                        hs.targetSpawn     = ClosestRespawnTo(opp.pos);
                        hs.routedToSpawn   = false;
                        hs.targetDemoed    = true;
                        hs.respawnTimer    = 0.0f;
                        hs.targetRespawned = false;
                        hs.ambushPresent   = false;
                        myDemoTargets[id]  = opp.carId;
                        s_huntsStarted.fetch_add(1);
                        break;
                    }
                }
            }

            if (!hs.hunting) return 0.0f;

            Vec toSpawn    = (hs.targetSpawn - player.pos).Normalized();
            float routeVel = player.vel.Dot(toSpawn);
            if (routeVel > ROUTING_SPEED && !hs.routedToSpawn) {
                hs.routedToSpawn = true;
                s_routedToSpawn.fetch_add(1);
            }

            const Player* target = nullptr;
            for (const auto& opp : state.players) {
                if (opp.carId == hs.targetId) { target = &opp; break; }
            }

            if (!target) { hs.hunting = false; return 0.0f; }

            bool currentlyDemoed = target->isDemoed;
            if (hs.targetDemoed && !currentlyDemoed) {
                hs.targetRespawned = true;
                hs.respawnTimer    = 0.0f;

                float distToSpawn = (player.pos - hs.targetSpawn).Length();
                if (distToSpawn < AMBUSH_RADIUS && hs.routedToSpawn) {
                    hs.ambushPresent = true;
                }
            }
            hs.targetDemoed = currentlyDemoed;

            if (hs.targetRespawned) {
                hs.respawnTimer += dt;

                if (hs.respawnTimer > RESPAWN_WINDOW_SECS) {
                    bool wasPresent = hs.ambushPresent && hs.routedToSpawn;
                    hs.hunting  = false;
                    hs.cooldown = COOLDOWN_SECS;

                    if (wasPresent) {
                        s_ambushPresenceCount.fetch_add(1);
                        s_rewardMilli.fetch_add(static_cast<long long>(AMBUSH_PRESENCE_REWARD * 1000.0f));
                        return config.shadowMode ? 0.0f : AMBUSH_PRESENCE_REWARD;
                    }
                    return 0.0f;
                }

                if (player.eventState.demo && hs.routedToSpawn) {
                    for (const auto& opp : state.players) {
                        if (opp.carId != hs.targetId) continue;
                        if (opp.isDemoed && (player.pos - opp.pos).Length() < 400.0f) {
                            hs.hunting  = false;
                            hs.cooldown = COOLDOWN_SECS;
                            s_doubleDemoCount.fetch_add(1);
                            s_rewardMilli.fetch_add(static_cast<long long>(DOUBLE_DEMO_REWARD * 1000.0f));
                            return config.shadowMode ? 0.0f : DOUBLE_DEMO_REWARD;
                        }
                    }
                }
            }

            return 0.0f;
        }
    };


    // ============================================================================
    // 6. FunnelExitDemoReward
    // ============================================================================
    // Ball is in a corner. Instead of contesting it, bot positions at the most
    // likely exit path and demos the opponent as they come out.
    //
    // Key W&B metrics: CornerEntries (how often the situation arises), AtExitPosition
    // (did bot read the exit correctly?), DemosInWindow (conversions), AvgTimeliness
    // (how early was the intercept?). If AtExitPosition / CornerEntries is low, bot
    // isn't learning to position — check if AMBUSH_CONE is too small.
    // ============================================================================
    class FunnelExitDemoReward : public Reward {
    public:
        struct Config {
            bool shadowMode = false;
        };

        struct Metrics {
            int    cornerEntries  = 0;   // times IDLE→WATCHING (ball entered corner)
            int    atExitPosition = 0;   // times bot was at exit pos when ball left
            int    demosInWindow  = 0;   // demos that fired in the window
            double avgTimeliness  = 0.0; // mean timeliness multiplier [0–1]
            double rewardSum      = 0.0;
        };

    private:
        static constexpr float CORNER_X_THRESHOLD = 3200.0f;
        static constexpr float CORNER_Y_THRESHOLD = 3800.0f;
        static constexpr float EXIT_WAIT_DIST     = 1400.0f;
        static constexpr float AMBUSH_CONE        = 700.0f;
        static constexpr float DEMO_WINDOW_SECS   = 2.0f;
        static constexpr float BASE_REWARD        = 2.0f;
        static constexpr float COOLDOWN_SECS      = 3.0f;

        enum class FunnelPhase { IDLE, WATCHING, DEMO_WINDOW };

        struct FunnelState {
            FunnelPhase phase     = FunnelPhase::IDLE;
            Vec         exitPos   = {};
            bool        atExitPos = false;
            float       demoTimer = 0.0f;
            float       cooldown  = 0.0f;
        };

        mutable std::map<uint32_t, FunnelState> states;
        Config config;

        // Metrics — thread-safe atomics
        inline static std::atomic<int>       s_cornerEntries  { 0 };
        inline static std::atomic<int>       s_atExitPosition { 0 };
        inline static std::atomic<int>       s_demosInWindow  { 0 };
        inline static std::atomic<long long> s_timelinessSum  { 0 }; // ×1000
        inline static std::atomic<long long> s_rewardMilli    { 0 };

        bool InCorner(const Vec& pos) const {
            return (fabsf(pos.x) > CORNER_X_THRESHOLD && fabsf(pos.y) > CORNER_Y_THRESHOLD);
        }

        Vec ComputeExitPos(const Vec& cornerPos) const {
            Vec toCenter = Vec(0.0f, 0.0f, 0.0f) - cornerPos;
            float len    = toCenter.Length();
            if (len < 1.0f) return cornerPos;
            return cornerPos + (toCenter / len) * EXIT_WAIT_DIST;
        }

    public:
        FunnelExitDemoReward() : config() {}
        explicit FunnelExitDemoReward(const Config& cfg) : config(cfg) {}

        static Metrics GetAndResetMetrics() {
            Metrics m;
            m.cornerEntries  = s_cornerEntries.exchange(0);
            m.atExitPosition = s_atExitPosition.exchange(0);
            m.demosInWindow  = s_demosInWindow.exchange(0);
            long long tlRaw  = s_timelinessSum.exchange(0);
            long long rewRaw = s_rewardMilli.exchange(0);
            m.rewardSum = double(rewRaw) / 1000.0;
            if (m.demosInWindow > 0)
                m.avgTimeliness = double(tlRaw) / (1000.0 * m.demosInWindow);
            return m;
        }

        virtual void Reset(const GameState& initialState) override { states.clear(); }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            uint32_t id = player.carId;
            float dt    = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            FunnelState& fs = states[id];

            if (fs.cooldown > 0.0f) { fs.cooldown -= dt; return 0.0f; }

            bool ballInCorner = InCorner(state.ball.pos);

            switch (fs.phase) {

            case FunnelPhase::IDLE: {
                if (ballInCorner) {
                    fs.exitPos   = ComputeExitPos(state.ball.pos);
                    fs.phase     = FunnelPhase::WATCHING;
                    fs.atExitPos = false;
                    s_cornerEntries.fetch_add(1);
                }
                break;
            }

            case FunnelPhase::WATCHING: {
                if (!ballInCorner) {
                    float distToExit = (player.pos - fs.exitPos).Length();
                    fs.atExitPos = (distToExit < AMBUSH_CONE);

                    if (fs.atExitPos) {
                        s_atExitPosition.fetch_add(1);
                        fs.phase     = FunnelPhase::DEMO_WINDOW;
                        fs.demoTimer = 0.0f;
                    } else {
                        fs.phase = FunnelPhase::IDLE;
                    }
                } else {
                    fs.exitPos = ComputeExitPos(state.ball.pos);
                }
                break;
            }

            case FunnelPhase::DEMO_WINDOW: {
                fs.demoTimer += dt;

                if (fs.demoTimer > DEMO_WINDOW_SECS) {
                    fs.phase = FunnelPhase::IDLE;
                    break;
                }

                if (player.eventState.demo) {
                    float timeliness = RS_CLAMP(1.0f - (fs.demoTimer / DEMO_WINDOW_SECS),
                                                0.0f, 1.0f);
                    float reward = BASE_REWARD * (0.7f + 0.3f * timeliness);

                    s_demosInWindow.fetch_add(1);
                    s_timelinessSum.fetch_add(static_cast<long long>(timeliness * 1000.0f));
                    s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));

                    fs.phase    = FunnelPhase::IDLE;
                    fs.cooldown = COOLDOWN_SECS;
                    return config.shadowMode ? 0.0f : reward;
                }
                break;
            }
            }

            return 0.0f;
        }
    };


    // ============================================================================
    // 7. DoubleCommitDemoReward
    // ============================================================================
    // In 2v2/3v3: ALL opponents simultaneously committed to ball. Demo the most
    // committed opponent to create a man-advantage. Bot must be the second/third man
    // (not the primary challenger) to trigger this.
    //
    // Key W&B metrics: CommittedFrames (how often the situation arises), DemosInCommit
    // (conversions), AvgCommitBonus (quality of the commitment window). Watch for
    // CommittedFrames being very high in early training — this may indicate the
    // commitment threshold is too low (both bots count as "committed" when they
    // aren't really). Start at very low weight — highest potential for weird 2v2
    // emergent behaviors.
    // ============================================================================
    class DoubleCommitDemoReward : public Reward {
    public:
        struct Config {
            bool shadowMode = false;
        };

        struct Metrics {
            int    allCommittedFrames = 0;   // frames where all-committed + bot is 2nd man
            int    demosInCommit      = 0;   // demos fired during commitment window
            double avgCommitBonus     = 0.0;
            double rewardSum          = 0.0;
        };

    private:
        static constexpr float COMMIT_RADIUS      = 2800.0f;
        static constexpr float MIN_COMMIT_ALIGN   = 0.60f;
        static constexpr float DEFENSIVE_DEPTH    = 1000.0f;
        static constexpr float BASE_REWARD        = 2.5f;
        static constexpr float FULL_COMMIT_BONUS  = 1.0f;
        static constexpr float COOLDOWN_SECS      = 5.0f;

        mutable std::map<uint32_t, float> cooldowns;
        Config config;

        // Metrics — thread-safe atomics
        inline static std::atomic<int>       s_allCommittedFrames { 0 };
        inline static std::atomic<int>       s_demosInCommit      { 0 };
        inline static std::atomic<long long> s_commitBonusSum     { 0 }; // ×1000
        inline static std::atomic<long long> s_rewardMilli        { 0 };

        struct CommitSnapshot {
            bool  allCommitted  = false;
            float avgCommitment = 0.0f;
            int   oppCount      = 0;
        };

        CommitSnapshot EvaluateOpponentCommitment(const Player& player,
                                                   const GameState& state) const {
            CommitSnapshot snap;
            snap.oppCount     = 0;
            snap.allCommitted = true;

            float commitSum = 0.0f;
            Vec ownGoal = (player.team == Team::BLUE)
                ? CommonValues::BLUE_GOAL_CENTER
                : CommonValues::ORANGE_GOAL_CENTER;

            for (const auto& opp : state.players) {
                if (opp.team == player.team || opp.isDemoed) continue;
                snap.oppCount++;

                float oppToBallDist = (opp.pos - state.ball.pos).Length();
                if (oppToBallDist > COMMIT_RADIUS) {
                    snap.allCommitted = false;
                    continue;
                }

                Vec oppToBall = (state.ball.pos - opp.pos).Normalized();
                float align   = opp.vel.Normalized().Dot(oppToBall);

                if (align < MIN_COMMIT_ALIGN)
                    snap.allCommitted = false;

                float defDepth = (opp.pos - state.ball.pos).Dot(
                    (ownGoal - state.ball.pos).Normalized());
                if (defDepth > DEFENSIVE_DEPTH)
                    snap.allCommitted = false;

                commitSum += RS_MAX(0.0f, align);
            }

            snap.avgCommitment = (snap.oppCount > 0) ? commitSum / snap.oppCount : 0.0f;
            return snap;
        }

    public:
        DoubleCommitDemoReward() : config() {}
        explicit DoubleCommitDemoReward(const Config& cfg) : config(cfg) {}

        static Metrics GetAndResetMetrics() {
            Metrics m;
            m.allCommittedFrames = s_allCommittedFrames.exchange(0);
            m.demosInCommit      = s_demosInCommit.exchange(0);
            long long cbRaw      = s_commitBonusSum.exchange(0);
            long long rewRaw     = s_rewardMilli.exchange(0);
            m.rewardSum = double(rewRaw) / 1000.0;
            if (m.demosInCommit > 0)
                m.avgCommitBonus = double(cbRaw) / (1000.0 * m.demosInCommit);
            return m;
        }

        virtual void Reset(const GameState& initialState) override { cooldowns.clear(); }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            uint32_t id = player.carId;
            float dt    = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

            if (cooldowns[id] > 0.0f) { cooldowns[id] -= dt; return 0.0f; }

            // 1v1: no "all committed" concept
            int oppCount = 0;
            for (const auto& opp : state.players)
                if (opp.team != player.team && !opp.isDemoed) oppCount++;
            if (oppCount < 2) return 0.0f;

            // Bot must NOT be closest to ball on its team
            float myDistBall = (player.pos - state.ball.pos).Length();
            for (const auto& tm : state.players) {
                if (tm.team != player.team || tm.carId == player.carId) continue;
                if ((tm.pos - state.ball.pos).Length() < myDistBall) {
                    goto can_hunt;
                }
            }
            return 0.0f;
            can_hunt:

            // Ball must not be in our own defensive half
            float ballSign = (player.team == Team::BLUE) ? state.ball.pos.y : -state.ball.pos.y;
            if (ballSign < -500.0f) return 0.0f;

            CommitSnapshot snap = EvaluateOpponentCommitment(player, state);
            if (!snap.allCommitted || snap.oppCount < 2) return 0.0f;

            // Track committed frames (even without a demo)
            s_allCommittedFrames.fetch_add(1);

            if (player.eventState.demo) {
                float commitBonus = RS_CLAMP((snap.avgCommitment - MIN_COMMIT_ALIGN) /
                                            (1.0f - MIN_COMMIT_ALIGN), 0.0f, 1.0f);
                float reward = BASE_REWARD + commitBonus * FULL_COMMIT_BONUS;

                s_demosInCommit.fetch_add(1);
                s_commitBonusSum.fetch_add(static_cast<long long>(commitBonus * 1000.0f));
                s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));

                cooldowns[id] = COOLDOWN_SECS;
                return config.shadowMode ? 0.0f : reward;
            }

            return 0.0f;
        }
    };

} // namespace RLGC
