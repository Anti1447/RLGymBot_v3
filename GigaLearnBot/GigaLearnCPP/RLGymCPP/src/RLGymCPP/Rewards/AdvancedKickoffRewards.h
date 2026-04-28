#pragma once
// AdvancedKickoffRewards.h
// Rev 1.0 (2026-04-26, NJH + Claude)
//
// Seven kickoff rewards targeting high-skill strategic and mechanical execution:
//
//   1. SpeedFlipKickoffReward       - quality speed flip detection + contact evaluation
//   2. DelayedKickoffReward         - intentional hesitation to read opponent first
//   3. FakeKickoffReward            - sell approach, opponent commits, bot aborts + exploits
//   4. ReactiveKickoffReward        - mid-kickoff adaptation to opponent behavior
//   5. PredictiveSecondManReward    - pre-position for post-kickoff outcome before it happens
//   6. DiagonalBoostKickoffReward   - diagonal corner boost grab route with clean exit
//   7. SpanishDemoKickoffReward     - second man routes around ball to demo challenger
//
// Kickoff detection convention (shared across all):
//   isKickoff = ball.pos.Length() < 500 && ball.vel.Length() < 200
//
// Weights (PHASE_6_CHAMPION):
//   SpeedFlipKickoff       : 2.0f
//   DelayedKickoff         : 1.5f
//   FakeKickoff            : 2.0f
//   ReactiveKickoff        : 1.5f
//   PredictiveSecondMan    : 1.5f
//   DiagonalBoostKickoff   : 1.5f
//   SpanishDemoKickoff     : 2.0f
//
// Rev 1.0: Added Metrics structs, inline static atomics, GetAndResetMetrics()
// to all 7 classes. No shadow mode — active from first training step.

#include "Reward.h"
#include "../CommonValues.h"
#include <atomic>
#include <map>
#include <cmath>
#include <vector>
#include <algorithm>

namespace RLGC {

    // ============================================================================
    // Shared kickoff utilities
    // ============================================================================
    namespace KickoffUtil {
        inline bool IsKickoff(const GameState& state) {
            return (state.ball.pos.Length() < 500.0f && state.ball.vel.Length() < 200.0f);
        }

        // Returns distance this player is from the ball at their spawn
        inline float FormationDistFromCenter(const Player& player) {
            return sqrtf(player.pos.x * player.pos.x + player.pos.y * player.pos.y);
        }

        // Is this player the closest to the ball on their team? (= kickoff taker)
        inline bool IsKickoffTaker(const Player& player, const GameState& state) {
            float myDist = (player.pos - state.ball.pos).Length();
            for (const auto& p : state.players) {
                if (p.team != player.team || p.carId == player.carId) continue;
                if ((p.pos - state.ball.pos).Length() < myDist) return false;
            }
            return true;
        }

        // Team-aware goal helpers
        inline Vec OwnGoal(const Player& p) {
            return (p.team == Team::BLUE) ? CommonValues::BLUE_GOAL_CENTER
                                          : CommonValues::ORANGE_GOAL_CENTER;
        }
        inline Vec OppGoal(const Player& p) {
            return (p.team == Team::BLUE) ? CommonValues::ORANGE_GOAL_CENTER
                                          : CommonValues::BLUE_GOAL_CENTER;
        }
        // Y-axis sign toward opponent goal
        inline float ForwardSign(const Player& p) {
            return (p.team == Team::BLUE) ? 1.0f : -1.0f;
        }
    }


    // ============================================================================
    // 1. SpeedFlipKickoffReward
    // ============================================================================
    // Rewards executing a proper speed flip kickoff, evaluated across three phases:
    //
    // PHASE 1 — Boost collect (optional but rewarded):
    //   Diagonal route through corner boost pad before committing to ball.
    //   Reward if large pad collected within BOOST_COLLECT_WINDOW_SECS of kickoff start.
    //
    // PHASE 2 — Speed flip execution:
    //   Detect actual speed flip: diagonal velocity components (forward + lateral),
    //   car tilted back (negative pitch via rotMat.up.z), flip initiated while airborne,
    //   car speed > MIN_SPEEDFLIP_SPEED at moment of flip.
    //   Timing reward: bonus for flipping at the sweet spot distance from ball.
    //
    // PHASE 3 — Contact quality:
    //   Ball velocity post-contact: direction toward opponent goal AND speed above threshold.
    //   Flip-into-contact bonus: was bot still in flip rotation when ball touched?
    //   Speed bonus: faster contact = harder hit = more reward.
    //
    // Final reward = PHASE2_BASE + timing_bonus + contact_direction + contact_speed_bonus
    //
    // W&B: Kickoff/SpeedFlip/FlipDetected (phase 2 fires), Kickoff/SpeedFlip/ContactSuccess (all phases)
    // ============================================================================
    class SpeedFlipKickoffReward : public Reward {
    private:
        static constexpr float BOOST_COLLECT_WINDOW_SECS = 1.2f;
        static constexpr float MIN_SPEEDFLIP_SPEED       = 1600.0f;
        static constexpr float MIN_DIAGONAL_LATERAL      = 0.25f;
        static constexpr float IDEAL_FLIP_DIST_TO_BALL   = 600.0f;
        static constexpr float FLIP_DIST_TOLERANCE       = 400.0f;
        static constexpr float CONTACT_WINDOW_SECS       = 1.2f;   // was 0.8 — bot sometimes reaches ball after 0.8s
        static constexpr float MIN_CONTACT_SPEED         = 1800.0f;
        static constexpr float MIN_BALL_VEL_AFTER        = 1000.0f; // was 1500 — good kickoff hits land 1000-1400 UU/s
        static constexpr float MIN_CONTACT_ALIGNMENT     = 0.45f;
        static constexpr float PHASE2_BASE               = 1.0f;
        static constexpr float TIMING_BONUS_MAX          = 0.5f;
        static constexpr float CONTACT_BASE              = 1.0f;
        static constexpr float CONTACT_SPEED_BONUS_MAX   = 0.8f;
        static constexpr float BOOST_COLLECT_BONUS       = 0.4f;

        enum class SFPhase { IDLE, BOOSTING, FLIPPING, CONTACT_WINDOW };

        struct SFState {
            SFPhase phase         = SFPhase::IDLE;
            float   kickoffTimer  = 0.0f;
            float   phaseTimer    = 0.0f;
            float   flipDist      = 0.0f;
            float   speedAtFlip   = 0.0f;
            bool    collectedBoost= false;
            float   prevBoost     = 0.0f;
            bool    wasFlipping   = false;
        };

        mutable std::map<uint32_t, SFState> states;

        inline static std::atomic<int> s_flipDetected   { 0 };
        inline static std::atomic<int> s_contactSuccess { 0 };

    public:
        struct Metrics { int flipDetected = 0; int contactSuccess = 0; };
        static Metrics GetAndResetMetrics() {
            return { s_flipDetected.exchange(0), s_contactSuccess.exchange(0) };
        }

        virtual void Reset(const GameState& initialState) override { states.clear(); }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            uint32_t id = player.carId;
            float dt    = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            SFState& sf = states[id];

            bool kickoff = KickoffUtil::IsKickoff(state);

            if (kickoff && sf.phase == SFPhase::IDLE) {
                if (!KickoffUtil::IsKickoffTaker(player, state)) return 0.0f;
                sf.phase         = SFPhase::BOOSTING;
                sf.kickoffTimer  = 0.0f;
                sf.collectedBoost = false;
                sf.prevBoost     = player.boost;
                sf.wasFlipping   = false;
            }

            if (!kickoff && sf.phase != SFPhase::IDLE) {
                sf.phase = SFPhase::IDLE;
            }

            if (sf.phase == SFPhase::IDLE) return 0.0f;

            sf.kickoffTimer += dt;
            sf.phaseTimer   += dt;

            if (sf.phase == SFPhase::BOOSTING) {
                float boostGain = player.boost - sf.prevBoost;
                if (boostGain > 80.0f && sf.kickoffTimer < BOOST_COLLECT_WINDOW_SECS) {
                    sf.collectedBoost = true;
                }
                sf.prevBoost = player.boost;

                bool flipStarted = (!sf.wasFlipping && player.isFlipping && !player.isOnGround);
                if (flipStarted) {
                    float speed    = player.vel.Length();
                    float distBall = (player.pos - state.ball.pos).Length();

                    Vec velN       = player.vel.Normalized();
                    float fwdComp  = fabsf(velN.Dot(player.rotMat.forward));
                    float latComp  = 1.0f - fwdComp;
                    bool diagonal  = (latComp > MIN_DIAGONAL_LATERAL);

                    float pitchDot  = player.rotMat.up.z;
                    bool tiltedBack = (pitchDot < 0.6f);

                    if (speed > MIN_SPEEDFLIP_SPEED && diagonal && tiltedBack) {
                        sf.speedAtFlip = speed;
                        sf.flipDist    = distBall;
                        sf.phase       = SFPhase::CONTACT_WINDOW;
                        sf.phaseTimer  = 0.0f;

                        float timingError   = fabsf(distBall - IDEAL_FLIP_DIST_TO_BALL);
                        float timingQuality = RS_CLAMP(1.0f - timingError / FLIP_DIST_TOLERANCE,
                                                        0.0f, 1.0f);
                        float p2Reward = PHASE2_BASE + timingQuality * TIMING_BONUS_MAX;
                        if (sf.collectedBoost) p2Reward += BOOST_COLLECT_BONUS;

                        s_flipDetected.fetch_add(1);
                        return p2Reward;
                    }
                }
                sf.wasFlipping = player.isFlipping;
            }

            if (sf.phase == SFPhase::CONTACT_WINDOW) {
                sf.phaseTimer += dt;

                if (sf.phaseTimer > CONTACT_WINDOW_SECS) {
                    sf.phase = SFPhase::IDLE;
                    return 0.0f;
                }

                if (player.ballTouchedStep) {
                    float contactSpeed   = player.vel.Length();
                    float ballSpeedAfter = state.ball.vel.Length();

                    Vec oppGoalDir = (KickoffUtil::OppGoal(player) - state.ball.pos).Normalized();
                    float alignment = state.ball.vel.Normalized().Dot(oppGoalDir);

                    sf.phase = SFPhase::IDLE;

                    if (alignment > MIN_CONTACT_ALIGNMENT && ballSpeedAfter > MIN_BALL_VEL_AFTER) {
                        float speedBonus = RS_CLAMP((contactSpeed - MIN_CONTACT_SPEED) / 500.0f,
                                                     0.0f, 1.0f) * CONTACT_SPEED_BONUS_MAX;
                        float dirBonus   = RS_CLAMP((alignment - MIN_CONTACT_ALIGNMENT) /
                                                     (1.0f - MIN_CONTACT_ALIGNMENT), 0.0f, 1.0f);
                        s_contactSuccess.fetch_add(1);
                        return CONTACT_BASE + speedBonus + dirBonus * 0.5f;
                    }
                }
            }

            return 0.0f;
        }
    };


    // ============================================================================
    // 2. DelayedKickoffReward
    // ============================================================================
    // Rewards intentionally delaying the kickoff approach to observe the opponent
    // and then committing at an advantageous moment.
    //
    // W&B: Kickoff/Delayed/Success
    // ============================================================================
    class DelayedKickoffReward : public Reward {
    private:
        static constexpr float SLOW_APPROACH_THRESH   = 600.0f;
        static constexpr float COMMIT_APPROACH_THRESH = 1200.0f;
        static constexpr float MIN_DELAY_SECS         = 0.4f;
        static constexpr float MAX_DELAY_SECS         = 2.5f;
        static constexpr float OPP_COMMIT_THRESH      = 1000.0f;
        static constexpr float MIN_WIN_ALIGNMENT      = 0.35f;
        static constexpr float BASE_REWARD            = 1.5f;
        static constexpr float DELAY_BONUS_MAX        = 1.0f;
        static constexpr float COUNTER_HIT_BONUS      = 0.8f;

        enum class DelayPhase { IDLE, DELAYING, COMMITTED };

        struct DelayState {
            DelayPhase phase      = DelayPhase::IDLE;
            float      delayTimer = 0.0f;
            bool       oppCommitted = false;
            bool       fired      = false;
        };

        mutable std::map<uint32_t, DelayState> states;

        inline static std::atomic<int> s_delaySuccess { 0 };

        bool OppIsCommitting(const Player& player, const GameState& state) const {
            for (const auto& opp : state.players) {
                if (opp.team == player.team || opp.isDemoed) continue;
                Vec toB   = (state.ball.pos - opp.pos).Normalized();
                float app = opp.vel.Dot(toB);
                if (app > OPP_COMMIT_THRESH) return true;
            }
            return false;
        }

    public:
        struct Metrics { int delaySuccess = 0; };
        static Metrics GetAndResetMetrics() {
            return { s_delaySuccess.exchange(0) };
        }

        virtual void Reset(const GameState& initialState) override { states.clear(); }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            uint32_t id = player.carId;
            float dt    = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            bool kickoff = KickoffUtil::IsKickoff(state);
            DelayState& ds = states[id];

            if (kickoff && ds.phase == DelayPhase::IDLE && !ds.fired) {
                if (!KickoffUtil::IsKickoffTaker(player, state)) return 0.0f;
                ds.phase        = DelayPhase::DELAYING;
                ds.delayTimer   = 0.0f;
                ds.oppCommitted = false;
                ds.fired        = false;
            }

            if (!kickoff) {
                ds.phase = DelayPhase::IDLE;
                ds.fired = false;
                return 0.0f;
            }

            if (ds.phase == DelayPhase::IDLE) return 0.0f;

            Vec toBall     = (state.ball.pos - player.pos).Normalized();
            float approach = player.vel.Dot(toBall);

            switch (ds.phase) {

            case DelayPhase::DELAYING: {
                if (approach > COMMIT_APPROACH_THRESH) {
                    ds.phase = DelayPhase::IDLE;
                    break;
                }
                if (OppIsCommitting(player, state)) ds.oppCommitted = true;

                ds.delayTimer += dt;

                if (ds.oppCommitted && ds.delayTimer >= MIN_DELAY_SECS &&
                    approach > SLOW_APPROACH_THRESH) {
                    ds.phase = DelayPhase::COMMITTED;
                }
                break;
            }

            case DelayPhase::COMMITTED: {
                if (player.ballTouchedStep && !ds.fired) {
                    Vec oppGoalDir = (KickoffUtil::OppGoal(player) - state.ball.pos).Normalized();
                    float align    = state.ball.vel.Normalized().Dot(oppGoalDir);

                    if (align > MIN_WIN_ALIGNMENT) {
                        float t      = RS_CLAMP((ds.delayTimer - MIN_DELAY_SECS) /
                                                (MAX_DELAY_SECS  - MIN_DELAY_SECS), 0.0f, 1.0f);
                        float reward = BASE_REWARD + t * DELAY_BONUS_MAX;
                        if (ds.oppCommitted) reward += COUNTER_HIT_BONUS;

                        ds.fired = true;
                        ds.phase = DelayPhase::IDLE;
                        s_delaySuccess.fetch_add(1);
                        return reward;
                    }
                    ds.phase = DelayPhase::IDLE;
                }
                break;
            }

            default: break;
            }

            return 0.0f;
        }
    };


    // ============================================================================
    // 3. FakeKickoffReward
    // ============================================================================
    // Rewards selling a full-speed kickoff approach, causing the opponent to commit,
    // then aborting and exploiting their over-commitment.
    //
    // W&B: Kickoff/Fake/Success
    // ============================================================================
    class FakeKickoffReward : public Reward {
    private:
        static constexpr float SELL_APPROACH_SPEED    = 1100.0f;
        static constexpr float ABORT_THRESHOLD        = 400.0f;
        static constexpr float OPP_COMMIT_ALIGNMENT   = 0.65f;
        static constexpr float EXPLOIT_WINDOW_SECS    = 2.5f;
        static constexpr float MIN_EXPLOIT_ALIGNMENT  = 0.4f;
        static constexpr float BASE_REWARD            = 2.0f;
        static constexpr float SELL_SPEED_BONUS_MAX   = 0.8f;
        static constexpr float OPP_COMMIT_BONUS       = 0.7f;

        enum class FakePhase { IDLE, SELLING, ABORTED, EXPLOITING };

        struct FakeState {
            FakePhase phase         = FakePhase::IDLE;
            float     sellSpeed     = 0.0f;
            float     oppCommitQ    = 0.0f;
            float     exploitTimer  = 0.0f;
            bool      fired         = false;
        };

        mutable std::map<uint32_t, FakeState> states;

        inline static std::atomic<int> s_fakeSuccess { 0 };

        float OppCommitmentQuality(const Player& player, const GameState& state) const {
            float best = 0.0f;
            for (const auto& opp : state.players) {
                if (opp.team == player.team || opp.isDemoed) continue;
                Vec toBall = (state.ball.pos - opp.pos).Normalized();
                float align = opp.vel.Normalized().Dot(toBall);
                best = RS_MAX(best, align);
            }
            return best;
        }

    public:
        struct Metrics { int fakeSuccess = 0; };
        static Metrics GetAndResetMetrics() {
            return { s_fakeSuccess.exchange(0) };
        }

        virtual void Reset(const GameState& initialState) override { states.clear(); }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            uint32_t id = player.carId;
            float dt    = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            bool kickoff = KickoffUtil::IsKickoff(state);
            FakeState& fs = states[id];

            if (kickoff && fs.phase == FakePhase::IDLE && !fs.fired) {
                if (!KickoffUtil::IsKickoffTaker(player, state)) return 0.0f;
                fs.phase = FakePhase::SELLING;
                fs.fired = false;
            }

            if (!kickoff && fs.phase != FakePhase::EXPLOITING) {
                fs.phase = FakePhase::IDLE;
                fs.fired = false;
                return 0.0f;
            }

            Vec toBall     = (state.ball.pos - player.pos).Normalized();
            float approach = player.vel.Dot(toBall);

            switch (fs.phase) {

            case FakePhase::SELLING: {
                if (approach > SELL_APPROACH_SPEED) {
                    fs.sellSpeed  = RS_MAX(fs.sellSpeed, approach);
                    fs.oppCommitQ = RS_MAX(fs.oppCommitQ, OppCommitmentQuality(player, state));
                }
                if (fs.sellSpeed > SELL_APPROACH_SPEED &&
                    approach < ABORT_THRESHOLD && !player.ballTouchedStep) {
                    fs.phase       = FakePhase::ABORTED;
                    fs.exploitTimer = 0.0f;
                }
                if (player.ballTouchedStep) { fs.phase = FakePhase::IDLE; }
                break;
            }

            case FakePhase::ABORTED:
            case FakePhase::EXPLOITING: {
                fs.exploitTimer += dt;
                fs.phase = FakePhase::EXPLOITING;

                if (fs.exploitTimer > EXPLOIT_WINDOW_SECS) {
                    fs.phase = FakePhase::IDLE;
                    break;
                }

                if (player.ballTouchedStep && !fs.fired) {
                    Vec oppGoalDir = (KickoffUtil::OppGoal(player) - state.ball.pos).Normalized();
                    float align    = state.ball.vel.Normalized().Dot(oppGoalDir);

                    if (align > MIN_EXPLOIT_ALIGNMENT) {
                        float speedBonus  = RS_CLAMP((fs.sellSpeed - SELL_APPROACH_SPEED) /
                                                      1000.0f, 0.0f, 1.0f) * SELL_SPEED_BONUS_MAX;
                        float commitBonus = RS_CLAMP((fs.oppCommitQ - OPP_COMMIT_ALIGNMENT) /
                                                      (1.0f - OPP_COMMIT_ALIGNMENT), 0.0f, 1.0f)
                                           * OPP_COMMIT_BONUS;
                        fs.fired = true;
                        fs.phase = FakePhase::IDLE;
                        s_fakeSuccess.fetch_add(1);
                        return BASE_REWARD + speedBonus + commitBonus;
                    }
                    fs.phase = FakePhase::IDLE;
                }
                break;
            }

            default: break;
            }

            return 0.0f;
        }
    };


    // ============================================================================
    // 4. ReactiveKickoffReward
    // ============================================================================
    // Rewards mid-kickoff adaptation to what the opponent is actually doing.
    // Three reactive scenarios — A (counter speed flip), B (fake recognition),
    // C (delay exploitation).
    //
    // W&B: Kickoff/Reactive/ScenarioA, /ScenarioB, /ScenarioC
    // ============================================================================
    class ReactiveKickoffReward : public Reward {
    private:
        static constexpr float SPEEDFLIP_DETECT_SPEED    = 1600.0f;
        static constexpr float SPEEDFLIP_DETECT_LATERAL  = 0.25f;
        static constexpr float COUNTER_WINDOW_SECS       = 1.5f;
        static constexpr float FAKE_DETECT_ABORT_VEL     = 400.0f;
        static constexpr float DELAY_DETECT_APPROACH     = 500.0f;
        static constexpr float MIN_WIN_ALIGNMENT         = 0.35f;

        static constexpr float SCENARIO_A_REWARD = 2.0f;
        static constexpr float SCENARIO_B_REWARD = 1.5f;
        static constexpr float SCENARIO_C_REWARD = 1.5f;

        enum class OppType { UNKNOWN, SPEEDFLIP, FAKING, DELAYING };

        struct ReactState {
            OppType  detectedType   = OppType::UNKNOWN;
            bool     oppTouchedBall = false;
            float    counterTimer   = 0.0f;
            bool     inKickoff      = false;
            bool     fired          = false;
            float    oppPrevApproach = 0.0f;
        };

        mutable std::map<uint32_t, ReactState> states;

        inline static std::atomic<int> s_scenarioA { 0 };
        inline static std::atomic<int> s_scenarioB { 0 };
        inline static std::atomic<int> s_scenarioC { 0 };

        OppType IdentifyOpponent(const Player& player, const GameState& state) const {
            for (const auto& opp : state.players) {
                if (opp.team == player.team || opp.isDemoed) continue;

                Vec toBall    = (state.ball.pos - opp.pos).Normalized();
                float approach = opp.vel.Dot(toBall);
                float speed    = opp.vel.Length();

                Vec velN   = opp.vel.Normalized();
                float fwd  = fabsf(velN.Dot(opp.rotMat.forward));
                float lat  = 1.0f - fwd;
                bool diag  = (lat > SPEEDFLIP_DETECT_LATERAL);
                bool fastEnough = (speed > SPEEDFLIP_DETECT_SPEED);

                if (fastEnough && diag) return OppType::SPEEDFLIP;
                if (approach < DELAY_DETECT_APPROACH && speed > 200.0f) return OppType::DELAYING;
            }
            return OppType::UNKNOWN;
        }

    public:
        struct Metrics { int scenarioA = 0; int scenarioB = 0; int scenarioC = 0; };
        static Metrics GetAndResetMetrics() {
            return { s_scenarioA.exchange(0), s_scenarioB.exchange(0), s_scenarioC.exchange(0) };
        }

        virtual void Reset(const GameState& initialState) override { states.clear(); }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            uint32_t id = player.carId;
            float dt    = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            bool kickoff = KickoffUtil::IsKickoff(state);
            ReactState& rs = states[id];

            if (kickoff && !rs.inKickoff) {
                rs.inKickoff      = true;
                rs.detectedType   = OppType::UNKNOWN;
                rs.oppTouchedBall = false;
                rs.counterTimer   = 0.0f;
                rs.fired          = false;
                rs.oppPrevApproach = 0.0f;
            }

            if (!kickoff) {
                rs.inKickoff = false;
                rs.fired     = false;
                return 0.0f;
            }

            if (rs.fired) return 0.0f;

            OppType current = IdentifyOpponent(player, state);
            if (current != OppType::UNKNOWN) rs.detectedType = current;

            for (const auto& opp : state.players) {
                if (opp.team == player.team || opp.isDemoed) continue;
                Vec toBall = (state.ball.pos - opp.pos).Normalized();
                float approach = opp.vel.Dot(toBall);
                if (rs.oppPrevApproach > 1000.0f && approach < FAKE_DETECT_ABORT_VEL) {
                    rs.detectedType = OppType::FAKING;
                }
                rs.oppPrevApproach = approach;
            }

            for (const auto& opp : state.players) {
                if (opp.team == player.team) continue;
                if (opp.ballTouchedStep) {
                    rs.oppTouchedBall = true;
                    rs.counterTimer   = 0.0f;
                }
            }
            if (rs.oppTouchedBall) rs.counterTimer += dt;

            Vec oppGoalDir = (KickoffUtil::OppGoal(player) - state.ball.pos).Normalized();

            if (rs.detectedType == OppType::SPEEDFLIP &&
                rs.oppTouchedBall &&
                rs.counterTimer < COUNTER_WINDOW_SECS &&
                player.ballTouchedStep) {

                float align = state.ball.vel.Normalized().Dot(oppGoalDir);
                if (align > MIN_WIN_ALIGNMENT) {
                    rs.fired = true;
                    s_scenarioA.fetch_add(1);
                    return SCENARIO_A_REWARD;
                }
            }

            if (rs.detectedType == OppType::FAKING && player.ballTouchedStep) {
                float align = state.ball.vel.Normalized().Dot(oppGoalDir);
                if (align > MIN_WIN_ALIGNMENT) {
                    rs.fired = true;
                    s_scenarioB.fetch_add(1);
                    return SCENARIO_B_REWARD;
                }
            }

            if (rs.detectedType == OppType::DELAYING && player.ballTouchedStep) {
                float align = state.ball.vel.Normalized().Dot(oppGoalDir);
                if (align > MIN_WIN_ALIGNMENT) {
                    rs.fired = true;
                    s_scenarioC.fetch_add(1);
                    return SCENARIO_C_REWARD;
                }
            }

            return 0.0f;
        }
    };


    // ============================================================================
    // 5. PredictiveSecondManReward
    // ============================================================================
    // Rewards the SECOND man (not the kicker) for pre-positioning to exploit the
    // PREDICTED OUTCOME of the kickoff before it finishes.
    //
    // W&B: Kickoff/SecondMan/CorrectRead, Kickoff/SecondMan/PrereadBonus
    // ============================================================================
    class PredictiveSecondManReward : public Reward {
    private:
        static constexpr float OFFENSIVE_Y_THRESHOLD = 1000.0f;
        static constexpr float DEFENSIVE_Y_THRESHOLD = -800.0f;
        static constexpr float CENTER_RADIUS         = 2200.0f;
        static constexpr float POSITION_TOLERANCE    = 900.0f;
        static constexpr float WIN_BALL_SPEED        = 1200.0f;
        static constexpr float LOSE_BALL_SPEED       = 1000.0f;
        static constexpr float BASE_REWARD           = 1.5f;
        static constexpr float PREREAD_BONUS         = 0.8f;

        enum class PredictedOutcome { UNKNOWN, WIN, DRAW, LOSE };

        struct SecondManState {
            bool              isSecondMan     = false;
            PredictedOutcome  currentPred     = PredictedOutcome::UNKNOWN;
            bool              prePositioned   = false;
            float             prePositionTimer = 0.0f;
            bool              fired           = false;
        };

        mutable std::map<uint32_t, SecondManState> states;

        inline static std::atomic<int> s_correctRead  { 0 };
        inline static std::atomic<int> s_prereadBonus { 0 };

        PredictedOutcome PredictOutcome(const Player& player, const GameState& state) const {
            float ballY    = state.ball.pos.y * KickoffUtil::ForwardSign(player);
            float ballVelY = state.ball.vel.y * KickoffUtil::ForwardSign(player);
            float ballSpd  = state.ball.vel.Length();

            if (ballSpd > WIN_BALL_SPEED && ballVelY > 0.0f) return PredictedOutcome::WIN;
            if (ballSpd > LOSE_BALL_SPEED && ballVelY < 0.0f) return PredictedOutcome::LOSE;

            for (const auto& p : state.players) {
                if (p.team != player.team) continue;
                if (!KickoffUtil::IsKickoffTaker(p, state)) continue;
                Vec toBall = (state.ball.pos - p.pos).Normalized();
                float approach = p.vel.Dot(toBall);
                Vec velN   = p.vel.Normalized();
                Vec oppDir = (KickoffUtil::OppGoal(player) - p.pos).Normalized();
                float goalAlign = velN.Dot(oppDir);
                if (approach > 1200.0f && goalAlign > 0.5f) return PredictedOutcome::WIN;
            }

            return PredictedOutcome::DRAW;
        }

        Vec IdealPosition(const Player& player, PredictedOutcome pred) const {
            float sign = KickoffUtil::ForwardSign(player);
            switch (pred) {
            case PredictedOutcome::WIN:
                return Vec(0.0f, sign * 2000.0f, 17.0f);
            case PredictedOutcome::LOSE:
                return Vec(0.0f, sign * -2000.0f, 17.0f);
            case PredictedOutcome::DRAW:
            default:
                return Vec(sign * 800.0f, 0.0f, 17.0f);
            }
        }

    public:
        struct Metrics { int correctRead = 0; int prereadBonus = 0; };
        static Metrics GetAndResetMetrics() {
            return { s_correctRead.exchange(0), s_prereadBonus.exchange(0) };
        }

        virtual void Reset(const GameState& initialState) override { states.clear(); }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            uint32_t id = player.carId;
            float dt    = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            bool kickoff = KickoffUtil::IsKickoff(state);
            SecondManState& sm = states[id];

            if (kickoff && !sm.isSecondMan && !sm.fired) {
                sm.isSecondMan    = !KickoffUtil::IsKickoffTaker(player, state);
                sm.currentPred    = PredictedOutcome::UNKNOWN;
                sm.prePositioned  = false;
                sm.prePositionTimer = 0.0f;
                sm.fired          = false;
            }

            if (!kickoff) {
                if (sm.isSecondMan && !sm.fired) {
                    float ballVelY = state.ball.vel.y * KickoffUtil::ForwardSign(player);
                    bool teamWon   = (ballVelY > 0.0f && state.ball.vel.Length() > 800.0f);

                    if (teamWon && sm.currentPred == PredictedOutcome::WIN) {
                        Vec ideal = IdealPosition(player, PredictedOutcome::WIN);
                        float dist = (player.pos - ideal).Length();
                        if (dist < POSITION_TOLERANCE) {
                            float reward = BASE_REWARD;
                            if (sm.prePositioned) {
                                reward += PREREAD_BONUS;
                                s_prereadBonus.fetch_add(1);
                            }
                            sm.fired = true;
                            sm.isSecondMan = false;
                            s_correctRead.fetch_add(1);
                            return reward;
                        }
                    }
                }
                sm.isSecondMan = false;
                sm.fired       = false;
                return 0.0f;
            }

            if (!sm.isSecondMan || sm.fired) return 0.0f;

            sm.prePositionTimer += dt;

            PredictedOutcome pred = PredictOutcome(player, state);
            if (pred != PredictedOutcome::UNKNOWN) sm.currentPred = pred;

            if (sm.currentPred != PredictedOutcome::UNKNOWN) {
                Vec ideal = IdealPosition(player, sm.currentPred);
                float dist = (player.pos - ideal).Length();
                if (dist < POSITION_TOLERANCE && sm.prePositionTimer < 1.5f) {
                    sm.prePositioned = true;
                }
            }

            return 0.0f;
        }
    };


    // ============================================================================
    // 6. DiagonalBoostKickoffReward
    // ============================================================================
    // Rewards the corner boost grab kickoff route — diagonal approach, clean
    // collection, high-speed commit, quality contact.
    //
    // W&B: Kickoff/DiagBoost/PartialReward (route+collect), Kickoff/DiagBoost/FullReward
    // ============================================================================
    class DiagonalBoostKickoffReward : public Reward {
    private:
        static constexpr float LARGE_BOOST_GAIN         = 80.0f;
        static constexpr float MIN_COLLECTION_SPEED     = 800.0f;
        static constexpr float MIN_POST_BOOST_SPEED     = 1400.0f;
        static constexpr float MIN_CONTACT_ALIGNMENT    = 0.40f;
        static constexpr float DIAGONAL_ANGLE_MIN       = 0.25f;
        static constexpr float SEGMENT12_REWARD         = 0.5f;
        static constexpr float FULL_REWARD              = 2.0f;

        enum class DiagPhase { IDLE, ROUTING, BOOSTED, COMMITTED };

        struct DiagState {
            DiagPhase phase         = DiagPhase::IDLE;
            float     collectionSpd = 0.0f;
            float     prevBoost     = 0.0f;
            bool      collectedLarge = false;
            bool      partialFired  = false;
            bool      fired         = false;
        };

        mutable std::map<uint32_t, DiagState> states;

        inline static std::atomic<int> s_partialReward { 0 };
        inline static std::atomic<int> s_fullReward    { 0 };

        bool IsMovingDiagonally(const Player& player) const {
            float xComp = fabsf(player.vel.x) / RS_MAX(player.vel.Length(), 1.0f);
            float yComp = fabsf(player.vel.y) / RS_MAX(player.vel.Length(), 1.0f);
            return (xComp > DIAGONAL_ANGLE_MIN && yComp > DIAGONAL_ANGLE_MIN);
        }

    public:
        struct Metrics { int partialReward = 0; int fullReward = 0; };
        static Metrics GetAndResetMetrics() {
            return { s_partialReward.exchange(0), s_fullReward.exchange(0) };
        }

        virtual void Reset(const GameState& initialState) override { states.clear(); }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            uint32_t id = player.carId;
            float dt    = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            bool kickoff = KickoffUtil::IsKickoff(state);
            DiagState& dg = states[id];

            if (kickoff && dg.phase == DiagPhase::IDLE) {
                dg.phase          = DiagPhase::ROUTING;
                dg.prevBoost      = player.boost;
                dg.collectedLarge = false;
                dg.partialFired   = false;
                dg.fired          = false;
            }

            if (!kickoff && dg.phase != DiagPhase::COMMITTED) {
                dg.phase = DiagPhase::IDLE;
                dg.fired = false;
                return 0.0f;
            }

            if (dg.fired) return 0.0f;

            float boostGain = player.boost - dg.prevBoost;
            dg.prevBoost    = player.boost;

            switch (dg.phase) {

            case DiagPhase::ROUTING: {
                if (boostGain > LARGE_BOOST_GAIN) {
                    bool diagonal = IsMovingDiagonally(player);
                    float spd     = player.vel.Length();

                    if (diagonal && spd > MIN_COLLECTION_SPEED) {
                        dg.collectedLarge  = true;
                        dg.collectionSpd   = spd;
                        dg.phase           = DiagPhase::BOOSTED;

                        if (!dg.partialFired) {
                            dg.partialFired = true;
                            s_partialReward.fetch_add(1);
                            return SEGMENT12_REWARD;
                        }
                    }
                }
                break;
            }

            case DiagPhase::BOOSTED: {
                if (player.vel.Length() > MIN_POST_BOOST_SPEED) {
                    dg.phase = DiagPhase::COMMITTED;
                }
                break;
            }

            case DiagPhase::COMMITTED: {
                if (player.ballTouchedStep && !dg.fired) {
                    Vec oppGoalDir = (KickoffUtil::OppGoal(player) - state.ball.pos).Normalized();
                    float align    = state.ball.vel.Normalized().Dot(oppGoalDir);

                    if (align > MIN_CONTACT_ALIGNMENT) {
                        dg.fired = true;
                        dg.phase = DiagPhase::IDLE;
                        s_fullReward.fetch_add(1);
                        return FULL_REWARD;
                    }
                    dg.phase = DiagPhase::IDLE;
                }
                break;
            }

            default: break;
            }

            return 0.0f;
        }
    };


    // ============================================================================
    // 7. SpanishDemoKickoffReward
    // ============================================================================
    // In 2v2/3v3: second man routes AROUND the ball to demo the opponent's kicker.
    // Requires coordinated coverage — one takes ball, one takes man.
    //
    // W&B: Kickoff/SpanishDemo/DemoFired, Kickoff/SpanishDemo/PayoffSuccess
    // ============================================================================
    class SpanishDemoKickoffReward : public Reward {
    private:
        static constexpr float DEMO_WINDOW_SECS          = 3.0f;
        static constexpr float MIN_ROUTING_ALIGNMENT     = 0.55f;
        static constexpr float COVERAGE_RADIUS           = 2000.0f;
        static constexpr float PAYOFF_WINDOW_SECS        = 2.0f;
        static constexpr float BASE_REWARD               = 2.5f;

        struct SpanishState {
            bool   active         = false;
            float  kickoffTimer   = 0.0f;
            bool   routingToOpp   = false;
            bool   fired          = false;
            float  payoffTimer    = 0.0f;
            bool   demoFired      = false;
        };

        mutable std::map<uint32_t, SpanishState> states;

        inline static std::atomic<int> s_demoFired     { 0 };
        inline static std::atomic<int> s_payoffSuccess { 0 };

        bool TeamHasCoverage(const Player& player, const GameState& state) const {
            for (const auto& tm : state.players) {
                if (tm.team != player.team || tm.carId == player.carId) continue;
                if ((tm.pos - state.ball.pos).Length() < COVERAGE_RADIUS) return true;
            }
            return false;
        }

        bool TeamHasNextTouch(const Player& player, const GameState& state) const {
            for (const auto& p : state.players) {
                if (p.team == player.team && p.ballTouchedStep) return true;
            }
            return false;
        }

    public:
        struct Metrics { int demoFired = 0; int payoffSuccess = 0; };
        static Metrics GetAndResetMetrics() {
            return { s_demoFired.exchange(0), s_payoffSuccess.exchange(0) };
        }

        virtual void Reset(const GameState& initialState) override { states.clear(); }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            uint32_t id = player.carId;
            float dt    = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            bool kickoff = KickoffUtil::IsKickoff(state);
            SpanishState& ss = states[id];

            int teamCount = 0;
            for (const auto& p : state.players)
                if (p.team == player.team) teamCount++;
            if (teamCount < 2) return 0.0f;

            if (kickoff && !ss.active) {
                if (KickoffUtil::IsKickoffTaker(player, state)) return 0.0f;
                ss.active       = true;
                ss.kickoffTimer = 0.0f;
                ss.routingToOpp = false;
                ss.fired        = false;
                ss.demoFired    = false;
                ss.payoffTimer  = 0.0f;
            }

            if (!kickoff) {
                ss.active = false;
                ss.fired  = false;
                return 0.0f;
            }

            if (ss.fired) return 0.0f;

            ss.kickoffTimer += dt;
            if (ss.kickoffTimer > DEMO_WINDOW_SECS) {
                ss.active = false;
                return 0.0f;
            }

            const Player* oppKicker = nullptr;
            float minDist = 9999.0f;
            for (const auto& opp : state.players) {
                if (opp.team == player.team || opp.isDemoed) continue;
                float d = (opp.pos - state.ball.pos).Length();
                if (d < minDist) { minDist = d; oppKicker = &opp; }
            }
            if (!oppKicker) return 0.0f;

            Vec toOpp  = (oppKicker->pos - player.pos).Normalized();
            Vec toBall = (state.ball.pos  - player.pos).Normalized();
            Vec velN   = player.vel.Normalized();

            float alignToOpp  = velN.Dot(toOpp);
            float alignToBall = velN.Dot(toBall);

            if (alignToOpp > MIN_ROUTING_ALIGNMENT && alignToOpp > alignToBall + 0.15f) {
                ss.routingToOpp = true;
            }

            // Reward fires on the demo itself — routing + coverage is sufficient proof of intent.
            // PayoffSuccess tracks team follow-through as a secondary metric.
            if (player.eventState.demo && ss.routingToOpp && TeamHasCoverage(player, state)) {
                ss.demoFired   = true;
                ss.payoffTimer = 0.0f;
                ss.fired       = true;
                ss.active      = false;
                s_demoFired.fetch_add(1);
                return BASE_REWARD;
            }

            // Track payoff (team touch after demo) as informational metric only
            if (ss.demoFired) {
                ss.payoffTimer += dt;

                if (TeamHasNextTouch(player, state)) {
                    s_payoffSuccess.fetch_add(1);
                    ss.demoFired = false;
                }

                if (ss.payoffTimer > PAYOFF_WINDOW_SECS) {
                    ss.demoFired = false;
                }
            }

            return 0.0f;
        }
    };

} // namespace RLGC
