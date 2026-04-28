#pragma once
// AdvancedKickoffRewards.h
// Rev 1.0 (2025-04-24, NJH)
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
// Suggested weights (Shadow Mode 500M steps minimum — kickoff rewards can cause
// obsession quickly if weighted too high relative to goal-scoring rewards):
//   SpeedFlipKickoff       : 2.0f  (phase 3+)
//   DelayedKickoff         : 1.5f  (phase 4+ — needs baseline kickoff competence first)
//   FakeKickoff            : 2.0f  (phase 4+)
//   ReactiveKickoff        : 1.5f  (phase 4+)
//   PredictiveSecondMan    : 1.5f  (phase 4+, 2v2/3v3)
//   DiagonalBoostKickoff   : 1.5f  (phase 3+)
//   SpanishDemoKickoff     : 2.0f  (phase 5+, 2v2/3v3 only)

#include "Reward.h"
#include "../CommonValues.h"
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
    // Anti-farming:
    //   - Must actually contact ball (no reward for good speed flip alone)
    //   - Contact must occur within CONTACT_WINDOW_SECS of flip
    //   - Ball direction must be toward opponent's half (prevents rewarding bad redirects)
    //   - Only fires for kickoff taker (not second man)
    //   - Resets on kickoff end regardless of outcome
    // ============================================================================
    class SpeedFlipKickoffReward : public Reward {
    private:
        static constexpr float BOOST_COLLECT_WINDOW_SECS = 1.2f;
        static constexpr float MIN_SPEEDFLIP_SPEED       = 1600.0f; // speed at flip initiation
        static constexpr float MIN_DIAGONAL_LATERAL      = 0.25f;   // lateral/forward ratio
        static constexpr float IDEAL_FLIP_DIST_TO_BALL   = 600.0f;  // ideal distance from ball at flip
        static constexpr float FLIP_DIST_TOLERANCE       = 400.0f;  // tolerance window
        static constexpr float CONTACT_WINDOW_SECS       = 0.8f;    // flip -> contact window
        static constexpr float MIN_CONTACT_SPEED         = 1800.0f; // bot speed at contact
        static constexpr float MIN_BALL_VEL_AFTER        = 1500.0f; // ball speed after good hit
        static constexpr float MIN_CONTACT_ALIGNMENT     = 0.45f;   // ball vel toward opp goal
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
            float   flipDist      = 0.0f;   // distance to ball when flip started
            float   speedAtFlip   = 0.0f;
            bool    collectedBoost= false;
            float   prevBoost     = 0.0f;
            bool    wasFlipping   = false;
        };

        mutable std::map<uint32_t, SFState> states;

    public:
        virtual void Reset(const GameState& initialState) override { states.clear(); }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            uint32_t id = player.carId;
            float dt    = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            SFState& sf = states[id];

            bool kickoff = KickoffUtil::IsKickoff(state);

            // ---- Reset on kickoff start ----
            if (kickoff && sf.phase == SFPhase::IDLE) {
                // Only apply to kickoff taker
                if (!KickoffUtil::IsKickoffTaker(player, state)) return 0.0f;
                sf.phase         = SFPhase::BOOSTING;
                sf.kickoffTimer  = 0.0f;
                sf.collectedBoost = false;
                sf.prevBoost     = player.boost;
                sf.wasFlipping   = false;
            }

            if (!kickoff && sf.phase != SFPhase::IDLE) {
                // Kickoff ended without completion — reset
                sf.phase = SFPhase::IDLE;
            }

            if (sf.phase == SFPhase::IDLE) return 0.0f;

            sf.kickoffTimer += dt;
            sf.phaseTimer   += dt;

            // ---- PHASE: BOOSTING (approach) ----
            if (sf.phase == SFPhase::BOOSTING) {
                // Detect large boost pad collection
                float boostGain = player.boost - sf.prevBoost;
                if (boostGain > 80.0f && sf.kickoffTimer < BOOST_COLLECT_WINDOW_SECS) {
                    sf.collectedBoost = true;
                }
                sf.prevBoost = player.boost;

                // Detect flip initiation
                bool flipStarted = (!sf.wasFlipping && player.isFlipping && !player.isOnGround);
                if (flipStarted) {
                    float speed    = player.vel.Length();
                    float distBall = (player.pos - state.ball.pos).Length();

                    // Check diagonal velocity
                    Vec velN       = player.vel.Normalized();
                    float fwdComp  = fabsf(velN.Dot(player.rotMat.forward));
                    float latComp  = 1.0f - fwdComp;
                    bool diagonal  = (latComp > MIN_DIAGONAL_LATERAL);

                    // Check negative pitch (car tilted back — characteristic of speed flip)
                    float pitchDot = player.rotMat.up.z; // 1 = flat, -1 = inverted
                    bool tiltedBack = (pitchDot < 0.6f);

                    if (speed > MIN_SPEEDFLIP_SPEED && diagonal && tiltedBack) {
                        sf.speedAtFlip = speed;
                        sf.flipDist    = distBall;
                        sf.phase       = SFPhase::FLIPPING;
                        sf.phaseTimer  = 0.0f;

                        // Phase 2 reward: base + timing bonus
                        float timingError  = fabsf(distBall - IDEAL_FLIP_DIST_TO_BALL);
                        float timingQuality = RS_CLAMP(1.0f - timingError / FLIP_DIST_TOLERANCE,
                                                       0.0f, 1.0f);
                        float p2Reward = PHASE2_BASE + timingQuality * TIMING_BONUS_MAX;
                        if (sf.collectedBoost) p2Reward += BOOST_COLLECT_BONUS;

                        // Open contact window immediately
                        sf.phase = SFPhase::CONTACT_WINDOW;
                        sf.phaseTimer = 0.0f;
                        return p2Reward; // Phase 2 fires immediately on flip detection
                    }
                }
                sf.wasFlipping = player.isFlipping;
            }

            // ---- PHASE: CONTACT WINDOW ----
            if (sf.phase == SFPhase::CONTACT_WINDOW) {
                sf.phaseTimer += dt;

                if (sf.phaseTimer > CONTACT_WINDOW_SECS) {
                    sf.phase = SFPhase::IDLE;
                    return 0.0f;
                }

                if (player.ballTouchedStep) {
                    float contactSpeed = player.vel.Length();
                    float ballSpeedAfter = state.ball.vel.Length();

                    // Ball must be heading toward opponent's half
                    Vec oppGoalDir = (KickoffUtil::OppGoal(player) - state.ball.pos).Normalized();
                    float alignment = state.ball.vel.Normalized().Dot(oppGoalDir);

                    sf.phase = SFPhase::IDLE;

                    if (alignment > MIN_CONTACT_ALIGNMENT && ballSpeedAfter > MIN_BALL_VEL_AFTER) {
                        float speedBonus = RS_CLAMP((contactSpeed - MIN_CONTACT_SPEED) / 500.0f,
                                                    0.0f, 1.0f) * CONTACT_SPEED_BONUS_MAX;
                        float dirBonus   = RS_CLAMP((alignment - MIN_CONTACT_ALIGNMENT) /
                                                     (1.0f - MIN_CONTACT_ALIGNMENT), 0.0f, 1.0f);
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
    // "Delayed kickoff" is detected when:
    //   (a) Bot is the kickoff taker (closest to ball)
    //   (b) Bot does NOT approach the ball at full speed immediately
    //       — velocity toward ball < SLOW_APPROACH_THRESH for DELAY_WINDOW_SECS
    //   (c) Opponent commits first (reaches ball or starts full-speed approach)
    //   (d) Bot then commits and either:
    //       - Wins the resulting challenge (ball goes toward opp goal), OR
    //       - Times a counter-flip perfectly after opponent's first touch
    //
    // Reward structure:
    //   Base reward for delaying AND winning the resulting exchange.
    //   Bonus for how long the delay was sustained (longer patience = bigger read).
    //   Large bonus if bot counter-hits opponent's first touch (true delayed counter).
    //
    // Anti-farming:
    //   - Bot must actually have been the kicker (closest to ball at kickoff start)
    //   - Opponent must commit BEFORE bot (if bot goes first it's just a bad kickoff)
    //   - Final ball direction must favor bot's team (can't get rewarded for losing exchange)
    //   - One-shot per kickoff: no repeat firing within same kickoff episode
    // ============================================================================
    class DelayedKickoffReward : public Reward {
    private:
        static constexpr float SLOW_APPROACH_THRESH   = 600.0f;  // vel toward ball = "not committing"
        static constexpr float COMMIT_APPROACH_THRESH = 1200.0f; // vel toward ball = "committing"
        static constexpr float MIN_DELAY_SECS         = 0.4f;    // minimum hesitation
        static constexpr float MAX_DELAY_SECS         = 2.5f;    // cap
        static constexpr float OPP_COMMIT_THRESH      = 1000.0f; // opp approach speed = committed
        static constexpr float MIN_WIN_ALIGNMENT      = 0.35f;   // ball toward opp goal
        static constexpr float BASE_REWARD            = 1.5f;
        static constexpr float DELAY_BONUS_MAX        = 1.0f;    // at max delay
        static constexpr float COUNTER_HIT_BONUS      = 0.8f;    // counter-hit opp's first touch

        enum class DelayPhase { IDLE, DELAYING, COMMITTED };

        struct DelayState {
            DelayPhase phase      = DelayPhase::IDLE;
            float      delayTimer = 0.0f;
            bool       oppCommitted = false;
            bool       fired      = false;
        };

        mutable std::map<uint32_t, DelayState> states;

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
        virtual void Reset(const GameState& initialState) override { states.clear(); }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            uint32_t id = player.carId;
            float dt    = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            bool kickoff = KickoffUtil::IsKickoff(state);
            DelayState& ds = states[id];

            // ---- Reset on new kickoff ----
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
                // Check if bot is genuinely delaying (low approach speed)
                if (approach > COMMIT_APPROACH_THRESH) {
                    // Bot committed before opponent — not a delay kickoff
                    ds.phase = DelayPhase::IDLE;
                    break;
                }
                // Track opponent committing
                if (OppIsCommitting(player, state)) ds.oppCommitted = true;

                ds.delayTimer += dt;

                // After delay, if opponent committed and bot now commits too
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

                        // Counter-hit bonus: did opponent touch ball first?
                        if (ds.oppCommitted) reward += COUNTER_HIT_BONUS;

                        ds.fired = true;
                        ds.phase = DelayPhase::IDLE;
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
    // Three-phase sequence:
    //   SELL: Bot approaches ball at > SELL_APPROACH_SPEED (looks like full commit)
    //   ABORT: Bot decelerates hard (vel toward ball drops below ABORT_THRESHOLD)
    //          without touching the ball
    //   EXPLOIT: Within EXPLOIT_WINDOW_SECS:
    //     (a) Opponent had already committed (crossing past ball, over-extended), OR
    //     (b) Ball was touched by nobody, opponent wasted their approach, AND
    //     (c) Bot gets a clean touch on the now-uncontested ball toward opp goal
    //
    // Reward scales with how committed the opponent was when bot aborted.
    // Sell depth: how fast bot was going at abort = how convincing the fake was.
    //
    // Anti-farming:
    //   - Bot must have genuinely been above SELL_APPROACH_SPEED before aborting
    //   - Abort must be clean: no ball contact during the abort
    //   - After abort, bot must get a better outcome than if it had committed
    //     (ball direction better than expected from original approach angle)
    //   - Cooldown: once per kickoff
    // ============================================================================
    class FakeKickoffReward : public Reward {
    private:
        static constexpr float SELL_APPROACH_SPEED    = 1100.0f; // convincing approach
        static constexpr float ABORT_THRESHOLD        = 400.0f;  // vel toward ball post-abort
        static constexpr float OPP_COMMIT_ALIGNMENT   = 0.65f;   // opp vel toward ball
        static constexpr float EXPLOIT_WINDOW_SECS    = 2.5f;
        static constexpr float MIN_EXPLOIT_ALIGNMENT  = 0.4f;    // ball toward opp goal
        static constexpr float BASE_REWARD            = 2.0f;
        static constexpr float SELL_SPEED_BONUS_MAX   = 0.8f;    // how fast we were going at abort
        static constexpr float OPP_COMMIT_BONUS       = 0.7f;    // opponent visibly committed

        enum class FakePhase { IDLE, SELLING, ABORTED, EXPLOITING };

        struct FakeState {
            FakePhase phase         = FakePhase::IDLE;
            float     sellSpeed     = 0.0f;   // approach speed at abort
            float     oppCommitQ    = 0.0f;   // opponent commitment quality
            float     exploitTimer  = 0.0f;
            bool      fired         = false;
        };

        mutable std::map<uint32_t, FakeState> states;

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
                    // Actively selling — track opponent commitment
                    fs.sellSpeed  = RS_MAX(fs.sellSpeed, approach);
                    fs.oppCommitQ = RS_MAX(fs.oppCommitQ, OppCommitmentQuality(player, state));
                }
                // Abort: was selling, now decelerated without touching ball
                if (fs.sellSpeed > SELL_APPROACH_SPEED &&
                    approach < ABORT_THRESHOLD && !player.ballTouchedStep) {
                    fs.phase       = FakePhase::ABORTED;
                    fs.exploitTimer = 0.0f;
                }
                // If we touch the ball while selling, it's not a fake
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

                // Got a clean touch after the fake
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
    // The bot reads the opponent's approach TYPE and executes the correct counter.
    //
    // Three reactive scenarios, each rewarded independently:
    //
    // SCENARIO A — Speed flip counter:
    //   Opponent executes a speed flip kickoff (high speed, diagonal velocity).
    //   Bot detects this and positions for a counter-hit AFTER opponent's contact.
    //   Reward fires if bot gets a clean counter-hit within COUNTER_WINDOW_SECS
    //   of opponent's ball contact and ball goes toward opponent's half.
    //
    // SCENARIO B — Fake recognition:
    //   Opponent is faking (approaching then slowing before ball).
    //   Bot does NOT over-commit, instead holds position or redirects.
    //   Reward fires if bot ends up with ball possession (next touch) cleanly.
    //
    // SCENARIO C — Delay exploitation:
    //   Opponent is hanging back (slow approach speed, not committing).
    //   Bot commits hard, gets to ball first, hits it cleanly.
    //   Bonus multiplier for correctly identifying the hanging opponent
    //   (confirming bot went early because opponent gave it the opportunity).
    //
    // Anti-farming:
    //   - Each scenario fires at most once per kickoff
    //   - Scenario identification happens in real-time during kickoff
    //   - Ball contact outcome must confirm the read was correct
    // ============================================================================
    class ReactiveKickoffReward : public Reward {
    private:
        static constexpr float SPEEDFLIP_DETECT_SPEED    = 1600.0f;
        static constexpr float SPEEDFLIP_DETECT_LATERAL  = 0.25f;
        static constexpr float COUNTER_WINDOW_SECS       = 1.5f;
        static constexpr float FAKE_DETECT_ABORT_VEL     = 400.0f;  // opp slowing = fake
        static constexpr float DELAY_DETECT_APPROACH     = 500.0f;  // opp slow approach
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

        OppType IdentifyOpponent(const Player& player, const GameState& state) const {
            for (const auto& opp : state.players) {
                if (opp.team == player.team || opp.isDemoed) continue;

                Vec toBall    = (state.ball.pos - opp.pos).Normalized();
                float approach = opp.vel.Dot(toBall);
                float speed    = opp.vel.Length();

                // Speed flip indicators
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

            // Update opponent type detection continuously
            OppType current = IdentifyOpponent(player, state);
            if (current != OppType::UNKNOWN) rs.detectedType = current;

            // Detect fake: opponent was approaching fast, now slowed (without touching ball)
            for (const auto& opp : state.players) {
                if (opp.team == player.team || opp.isDemoed) continue;
                Vec toBall = (state.ball.pos - opp.pos).Normalized();
                float approach = opp.vel.Dot(toBall);
                if (rs.oppPrevApproach > 1000.0f && approach < FAKE_DETECT_ABORT_VEL) {
                    rs.detectedType = OppType::FAKING;
                }
                rs.oppPrevApproach = approach;
            }

            // Track opponent touching ball
            for (const auto& opp : state.players) {
                if (opp.team == player.team) continue;
                if (opp.ballTouchedStep) {
                    rs.oppTouchedBall = true;
                    rs.counterTimer   = 0.0f;
                }
            }
            if (rs.oppTouchedBall) rs.counterTimer += dt;

            Vec oppGoalDir = (KickoffUtil::OppGoal(player) - state.ball.pos).Normalized();

            // ---- SCENARIO A: counter-hit after opponent's speed flip ----
            if (rs.detectedType == OppType::SPEEDFLIP &&
                rs.oppTouchedBall &&
                rs.counterTimer < COUNTER_WINDOW_SECS &&
                player.ballTouchedStep) {

                float align = state.ball.vel.Normalized().Dot(oppGoalDir);
                if (align > MIN_WIN_ALIGNMENT) {
                    rs.fired = true;
                    return SCENARIO_A_REWARD;
                }
            }

            // ---- SCENARIO B: fake recognition — bot got next clean touch ----
            if (rs.detectedType == OppType::FAKING && player.ballTouchedStep) {
                float align = state.ball.vel.Normalized().Dot(oppGoalDir);
                if (align > MIN_WIN_ALIGNMENT) {
                    rs.fired = true;
                    return SCENARIO_B_REWARD;
                }
            }

            // ---- SCENARIO C: exploiting delaying opponent ----
            if (rs.detectedType == OppType::DELAYING && player.ballTouchedStep) {
                float align = state.ball.vel.Normalized().Dot(oppGoalDir);
                if (align > MIN_WIN_ALIGNMENT) {
                    rs.fired = true;
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
    // Kickoff outcomes fall into rough categories:
    //   WIN:  Ball goes toward opponent's half at speed (bot's team hits it well)
    //   DRAW: Ball goes toward side walls or stays center (50/50 outcome)
    //   LOSE: Ball goes toward bot's own half
    //
    // Second man reads which outcome is developing and positions accordingly:
    //   WIN anticipated:  Move to offensive half, prepare for follow-up shot/pass
    //   DRAW anticipated: Move to center, 50/50 ready position
    //   LOSE anticipated: Move to goal, defensive position
    //
    // Reward fires if:
    //   (a) Bot was positioned correctly for the actual outcome
    //       (within POSITION_TOLERANCE of the "correct" zone)
    //   (b) Bot's positioning happened BEFORE the kickoff outcome was determined
    //       (pre-read, not just reacting after the fact)
    //   (c) Bot's team benefits from the kickoff result (not credited for losing)
    //
    // Anti-farming:
    //   - Only fires for second man (not kicker)
    //   - Position check is done at kickoff END, verifying bot was in correct zone
    //   - Time-gating: position must have been reached before ball exits center
    //   - Once per kickoff
    // ============================================================================
    class PredictiveSecondManReward : public Reward {
    private:
        static constexpr float OFFENSIVE_Y_THRESHOLD = 1000.0f;  // in opp half
        static constexpr float DEFENSIVE_Y_THRESHOLD = -800.0f;  // in own half
        static constexpr float CENTER_RADIUS         = 2200.0f;  // near center
        static constexpr float POSITION_TOLERANCE    = 900.0f;
        static constexpr float WIN_BALL_SPEED        = 1200.0f;  // ball moving fast toward opp half
        static constexpr float LOSE_BALL_SPEED       = 1000.0f;  // ball moving toward own half
        static constexpr float BASE_REWARD           = 1.5f;
        static constexpr float PREREAD_BONUS         = 0.8f;     // if positioned before outcome clear

        enum class PredictedOutcome { UNKNOWN, WIN, DRAW, LOSE };

        struct SecondManState {
            bool              isSecondMan     = false;
            PredictedOutcome  currentPred     = PredictedOutcome::UNKNOWN;
            bool              prePositioned   = false;
            float             prePositionTimer = 0.0f;
            bool              fired           = false;
        };

        mutable std::map<uint32_t, SecondManState> states;

        PredictedOutcome PredictOutcome(const Player& player, const GameState& state) const {
            // Predict based on ball trajectory and kickoff approach speeds
            float ballY    = state.ball.pos.y * KickoffUtil::ForwardSign(player);
            float ballVelY = state.ball.vel.y * KickoffUtil::ForwardSign(player);
            float ballSpd  = state.ball.vel.Length();

            if (ballSpd > WIN_BALL_SPEED && ballVelY > 0.0f) return PredictedOutcome::WIN;
            if (ballSpd > LOSE_BALL_SPEED && ballVelY < 0.0f) return PredictedOutcome::LOSE;

            // Check kicker approach angles as leading indicator
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

        // Ideal second-man position given predicted outcome
        Vec IdealPosition(const Player& player, PredictedOutcome pred) const {
            float sign = KickoffUtil::ForwardSign(player);
            switch (pred) {
            case PredictedOutcome::WIN:
                return Vec(0.0f, sign * 2000.0f, 17.0f); // offensive half, ready to follow up
            case PredictedOutcome::LOSE:
                return Vec(0.0f, sign * -2000.0f, 17.0f); // defensive half
            case PredictedOutcome::DRAW:
            default:
                return Vec(sign * 800.0f, 0.0f, 17.0f); // center-ish, near mid boost
            }
        }

    public:
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
                // Kickoff ended — evaluate
                if (sm.isSecondMan && !sm.fired) {
                    // Determine actual outcome
                    float ballVelY = state.ball.vel.y * KickoffUtil::ForwardSign(player);
                    bool teamWon   = (ballVelY > 0.0f && state.ball.vel.Length() > 800.0f);

                    if (teamWon && sm.currentPred == PredictedOutcome::WIN) {
                        // Check if we were in the right zone
                        Vec ideal = IdealPosition(player, PredictedOutcome::WIN);
                        float dist = (player.pos - ideal).Length();
                        if (dist < POSITION_TOLERANCE) {
                            float reward = BASE_REWARD;
                            if (sm.prePositioned) reward += PREREAD_BONUS;
                            sm.fired = true;
                            sm.isSecondMan = false;
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

            // Continuously update predicted outcome
            PredictedOutcome pred = PredictOutcome(player, state);
            if (pred != PredictedOutcome::UNKNOWN) sm.currentPred = pred;

            // Check if we're already in the ideal position for current prediction
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
    // Rewards the corner boost grab kickoff route:
    //   - Bot routes diagonally through a large boost pad
    //   - Collects it cleanly (high boost gain)
    //   - Then commits to ball with high speed
    //   - Contacts ball with direction toward opponent's goal
    //
    // The route quality is evaluated holistically:
    //   SEGMENT 1: Diagonal path quality
    //     Angle of approach to boost pad should be roughly diagonal (~45°) —
    //     not driving straight to it (wastes time) or wildly off-axis.
    //   SEGMENT 2: Boost collection speed
    //     Higher speed through pad = cleaner route.
    //   SEGMENT 3: Commitment after boost
    //     Speed gain from boost and subsequent approach toward ball.
    //   SEGMENT 4: Contact quality (same as SpeedFlipKickoff contact evaluation)
    //
    // Full route reward fires only when all four segments complete.
    // Partial rewards fire at segments 1+2 to provide gradient signal.
    //
    // Anti-farming:
    //   - Bot must be kickoff taker OR a designated second-man going for boost
    //     while first man takes kickoff (detects by formation distance)
    //   - Boost must be a LARGE pad (small pads not enough to justify the detour)
    //   - Final ball contact must happen (no reward for just collecting boost)
    //   - Cooldown: once per kickoff
    // ============================================================================
    class DiagonalBoostKickoffReward : public Reward {
    private:
        static constexpr float LARGE_BOOST_GAIN         = 80.0f;   // confirms large pad
        static constexpr float MIN_COLLECTION_SPEED     = 800.0f;  // speed through pad
        static constexpr float MIN_POST_BOOST_SPEED     = 1400.0f; // must boost hard after
        static constexpr float MIN_CONTACT_ALIGNMENT    = 0.40f;
        static constexpr float DIAGONAL_ANGLE_MIN       = 0.25f;   // diagonal = both x and y movement
        static constexpr float SEGMENT12_REWARD         = 0.5f;    // partial: route + collect
        static constexpr float FULL_REWARD              = 2.0f;    // all segments complete

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

        bool IsMovingDiagonally(const Player& player) const {
            // Both x and y velocity components must be significant
            float xComp = fabsf(player.vel.x) / RS_MAX(player.vel.Length(), 1.0f);
            float yComp = fabsf(player.vel.y) / RS_MAX(player.vel.Length(), 1.0f);
            return (xComp > DIAGONAL_ANGLE_MIN && yComp > DIAGONAL_ANGLE_MIN);
        }

    public:
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
                // Detect large pad collection with diagonal approach
                if (boostGain > LARGE_BOOST_GAIN) {
                    bool diagonal = IsMovingDiagonally(player);
                    float spd     = player.vel.Length();

                    if (diagonal && spd > MIN_COLLECTION_SPEED) {
                        dg.collectedLarge  = true;
                        dg.collectionSpd   = spd;
                        dg.phase           = DiagPhase::BOOSTED;

                        if (!dg.partialFired) {
                            dg.partialFired = true;
                            return SEGMENT12_REWARD; // Partial reward for clean route
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
    // In 2v2/3v3: "Spanish kickoff" is where the second man routes AROUND the
    // ball (not going for it) and attempts to demo the opponent's kicker as they
    // crash into the ball.
    //
    // Sequence:
    //   SETUP: Bot is NOT kickoff taker (second or third man).
    //          Bot routes toward opponent kicker's position (not toward ball).
    //          Bot's velocity aligns toward opponent car, not ball.
    //   EXECUTE: Bot demos opponent kicker within DEMO_WINDOW_SECS of kickoff start.
    //   PAYOFF: Team still retains ball control after demo (own kicker gets it).
    //
    // Reward fires when:
    //   (a) Bot demoed an opponent (eventState.demo)
    //   (b) Bot was NOT routing toward ball (approach to opp > approach to ball)
    //   (c) Demo happens during kickoff phase
    //   (d) At least one teammate is closer to ball than bot was (coverage exists)
    //   (e) Ball is not immediately threatened (team has next touch) within 1.5s
    //
    // This trains coordinated kickoff strategy — one takes ball, one takes man.
    //
    // Anti-farming:
    //   - Only fires in 2v2 or 3v3 (at least 2 players per team)
    //   - Bot must not be the kicker (coverage must exist)
    //   - Demo must happen during or very shortly after kickoff phase
    //   - Once per kickoff
    // ============================================================================
    class SpanishDemoKickoffReward : public Reward {
    private:
        static constexpr float DEMO_WINDOW_SECS          = 3.0f;   // kickoff duration cap
        static constexpr float MIN_ROUTING_ALIGNMENT     = 0.55f;  // vel toward opp car
        static constexpr float COVERAGE_RADIUS           = 2000.0f; // teammate within this of ball
        static constexpr float PAYOFF_WINDOW_SECS        = 2.0f;   // check team control after
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
        virtual void Reset(const GameState& initialState) override { states.clear(); }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            uint32_t id = player.carId;
            float dt    = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            bool kickoff = KickoffUtil::IsKickoff(state);
            SpanishState& ss = states[id];

            // Count team size — must be 2v2 or 3v3
            int teamCount = 0;
            for (const auto& p : state.players)
                if (p.team == player.team) teamCount++;
            if (teamCount < 2) return 0.0f;

            if (kickoff && !ss.active) {
                // Must be second man, not kicker
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

            // Find opponent kicker (closest opp to ball)
            const Player* oppKicker = nullptr;
            float minDist = 9999.0f;
            for (const auto& opp : state.players) {
                if (opp.team == player.team || opp.isDemoed) continue;
                float d = (opp.pos - state.ball.pos).Length();
                if (d < minDist) { minDist = d; oppKicker = &opp; }
            }
            if (!oppKicker) return 0.0f;

            // Check routing: is bot heading toward opp kicker, not toward ball?
            Vec toOpp  = (oppKicker->pos - player.pos).Normalized();
            Vec toBall = (state.ball.pos  - player.pos).Normalized();
            Vec velN   = player.vel.Normalized();

            float alignToOpp  = velN.Dot(toOpp);
            float alignToBall = velN.Dot(toBall);

            if (alignToOpp > MIN_ROUTING_ALIGNMENT && alignToOpp > alignToBall + 0.15f) {
                ss.routingToOpp = true;
            }

            // Demo fires
            if (player.eventState.demo && ss.routingToOpp && TeamHasCoverage(player, state)) {
                // Open payoff window
                ss.demoFired   = true;
                ss.payoffTimer = 0.0f;
            }

            // Check payoff — team gets next touch within window
            if (ss.demoFired) {
                ss.payoffTimer += dt;

                if (TeamHasNextTouch(player, state)) {
                    ss.fired  = true;
                    ss.active = false;
                    return BASE_REWARD;
                }

                if (ss.payoffTimer > PAYOFF_WINDOW_SECS) {
                    ss.demoFired = false; // payoff window expired without team touch
                }
            }

            return 0.0f;
        }
    };

} // namespace RLGC
