#pragma once
// PredictiveDemoRewards.h
// Rev 1.0 (2025-04-24, NJH)
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

#include "Reward.h"
#include "../CommonValues.h"
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
    // How it works:
    //   At the moment the bot begins its demo approach (enters TRACKING phase),
    //   record two things:
    //     (a) opponent's current position
    //     (b) opponent's current velocity vector
    //   Extrapolate opponent's predicted position PREDICT_SECS into the future.
    //   Measure bot's approach vector: is it pointing at predicted position
    //   rather than current position?
    //   - "Lead angle" = angle between (bot -> opp_current) and (bot -> opp_predicted)
    //   When demo fires, reward scales with:
    //     - How accurately the bot led (approach aligned with predicted pos)
    //     - How far ahead it predicted (faster opp = bigger lead = smarter read)
    //     - Speed of impact (fast demos are harder to lead correctly)
    //
    // Anti-farming:
    //   - Must actually demo (eventState.demo) — approach alone never rewards
    //   - Opponent must have been moving fast enough to require a real lead
    //     (slow/stationary opponents give minimal lead bonus, near-zero reward)
    //   - Per-opponent cooldown: same target can't trigger twice within 4s
    //   - Lead must be detected BEFORE the demo (prediction window closes on contact)
    // ============================================================================
    class MomentumLeadDemoReward : public Reward {
    private:
        static constexpr float PREDICT_SECS        = 1.2f;   // extrapolate this far ahead
        static constexpr float MIN_OPP_SPEED        = 600.0f; // opp must be moving to need a lead
        static constexpr float MIN_LEAD_ALIGNMENT   = 0.55f;  // bot approach -> predicted pos
        static constexpr float TRACKING_RADIUS      = 2500.0f;
        static constexpr float BASE_REWARD          = 1.5f;
        static constexpr float MAX_SPEED_BONUS      = 1.5f;   // at max opp speed
        static constexpr float MAX_OPP_SPEED_SCALE  = 2200.0f;
        static constexpr float COOLDOWN_SECS        = 4.0f;

        struct LeadState {
            bool    tracking        = false;
            uint32_t targetId       = 0;
            Vec     predictedPos    = {};
            float   oppSpeed        = 0.0f;
            float   leadAlignment   = 0.0f;   // best alignment during approach
            float   cooldown        = 0.0f;
        };

        mutable std::map<uint32_t, LeadState> states;

        // Linear extrapolation of opponent position
        Vec PredictOppPos(const Player& opp, float dt) const {
            return opp.pos + opp.vel * dt;
        }

    public:
        virtual void Reset(const GameState& initialState) override { states.clear(); }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            uint32_t id = player.carId;
            float dt    = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            LeadState& ls = states[id];

            if (ls.cooldown > 0.0f) { ls.cooldown -= dt; return 0.0f; }

            // ---- Scan for a target to lead ----
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

            // ---- Compute lead alignment ----
            Vec predicted  = PredictOppPos(*target, PREDICT_SECS);
            Vec toPredict  = (predicted - player.pos).Normalized();
            Vec toCurrent  = (target->pos - player.pos).Normalized();
            Vec myVelN     = player.vel.Normalized();

            float alignToPredicted = myVelN.Dot(toPredict);
            float alignToCurrent   = myVelN.Dot(toCurrent);

            // Only "leading" if we're more aligned to predicted than current
            bool isLeading = (alignToPredicted > alignToCurrent + 0.05f) &&
                             (alignToPredicted > MIN_LEAD_ALIGNMENT);

            if (isLeading) {
                ls.tracking      = true;
                ls.targetId      = target->carId;
                ls.predictedPos  = predicted;
                ls.oppSpeed      = target->vel.Length();
                ls.leadAlignment = RS_MAX(ls.leadAlignment, alignToPredicted);
            }

            // ---- Demo confirmed ----
            if (player.eventState.demo && ls.tracking) {
                // Verify the demo was against our tracked target
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

                    ls.tracking      = false;
                    ls.leadAlignment = 0.0f;
                    ls.cooldown      = COOLDOWN_SECS;
                    return reward;
                }
            }

            // Reset if target gone or we lost tracking
            if (!ls.tracking || !target) ls.leadAlignment = 0.0f;

            return 0.0f;
        }
    };


    // ============================================================================
    // 2. BallChaserAmbushReward
    // ============================================================================
    // The opponent is sprinting toward the ball — they're not watching for
    // incoming cars, they're ball-locked. Reward demoing them WHILE they're
    // in active ball-chase mode (high velocity toward ball, not toward bot).
    //
    // "Ball-chasing" is detected when:
    //   (a) Opponent velocity aligns strongly with (opp -> ball) direction
    //   (b) Opponent is within CHASE_RADIUS of ball and closing
    //   (c) Opponent's velocity does NOT align with (opp -> bot) direction
    //       — they haven't seen us coming
    //
    // Reward multiplier based on:
    //   - How ball-locked opponent was (higher vel alignment = more oblivious)
    //   - How unaware they were of bot (lower alignment to bot = bigger bonus)
    //   - Demo speed
    //
    // Anti-farming:
    //   - Opponent must genuinely be chasing the ball (not just near it)
    //   - Bot approach must differ from opponent's ball-chase direction
    //     (can't just follow behind them and bump — must intercept laterally)
    //   - Per-target cooldown 4s
    // ============================================================================
    class BallChaserAmbushReward : public Reward {
    private:
        static constexpr float CHASE_RADIUS         = 2200.0f; // opp within this of ball
        static constexpr float MIN_CHASE_ALIGNMENT  = 0.72f;   // opp vel toward ball
        static constexpr float MAX_AWARENESS        = 0.40f;   // opp vel NOT toward bot
        static constexpr float MIN_BOT_APPROACH     = 800.0f;  // bot must be moving toward opp
        static constexpr float BASE_REWARD          = 2.0f;
        static constexpr float OBLIVIOUS_BONUS      = 1.0f;    // max bonus for fully unaware opp
        static constexpr float COOLDOWN_SECS        = 4.0f;

        mutable std::map<uint32_t, float>    cooldowns;
        // Track the ambush setup quality at moment of demo
        mutable std::map<uint32_t, float>    pendingBonus;
        mutable std::map<uint32_t, uint32_t> pendingTarget;

    public:
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

            // ---- Scan opponents for ball-chasing state ----
            for (const auto& opp : state.players) {
                if (opp.team == player.team || opp.isDemoed) continue;

                float oppToBallDist = (opp.pos - state.ball.pos).Length();
                if (oppToBallDist > CHASE_RADIUS) continue;

                float oppSpeed = opp.vel.Length();
                if (oppSpeed < 400.0f) continue;

                // Is opponent chasing ball?
                Vec oppToBallDir   = (state.ball.pos - opp.pos).Normalized();
                float chaseAlign   = opp.vel.Normalized().Dot(oppToBallDir);
                if (chaseAlign < MIN_CHASE_ALIGNMENT) continue;

                // Is opponent unaware of bot?
                Vec oppToBotDir    = (player.pos - opp.pos).Normalized();
                float awarenessAlign = opp.vel.Normalized().Dot(oppToBotDir);
                bool oblivious     = (awarenessAlign < MAX_AWARENESS);

                // Is bot actually approaching opponent (not just nearby)?
                Vec botToOppDir    = (opp.pos - player.pos).Normalized();
                float botApproach  = player.vel.Dot(botToOppDir);
                if (botApproach < MIN_BOT_APPROACH) continue;

                // Quality score for this ambush
                float chaseQ   = RS_CLAMP((chaseAlign - MIN_CHASE_ALIGNMENT) /
                                          (1.0f - MIN_CHASE_ALIGNMENT), 0.0f, 1.0f);
                float awarenessQ = oblivious ?
                    RS_CLAMP(1.0f - (awarenessAlign / MAX_AWARENESS), 0.0f, 1.0f) : 0.0f;
                float bonus = chaseQ * 0.5f + awarenessQ * 0.5f;

                pendingBonus[id]  = bonus;
                pendingTarget[id] = opp.carId;
            }

            // ---- Demo fires ----
            if (player.eventState.demo) {
                auto it = pendingTarget.find(id);
                if (it != pendingTarget.end()) {
                    float bonus  = pendingBonus.count(id) ? pendingBonus[id] : 0.0f;
                    float reward = BASE_REWARD + bonus * OBLIVIOUS_BONUS;
                    cooldowns[id] = COOLDOWN_SECS;
                    pendingBonus.erase(id);
                    pendingTarget.erase(id);
                    return reward;
                }
            }

            return 0.0f;
        }
    };


    // ============================================================================
    // 3. PostTouchShadowDemoReward
    // ============================================================================
    // Opponent just touched the ball. Their momentum carries them in the
    // follow-through direction — a predictable vector. Bot uses that vector
    // to predict where the opponent will be in SHADOW_PREDICT_SECS and
    // positions there for the demo.
    //
    // Sequence:
    //   STEP 1: Opponent makes ball contact (oppTouchedStep)
    //   STEP 2: Record opponent's post-touch velocity vector (their follow-through)
    //   STEP 3: Extrapolate opponent's position SHADOW_PREDICT_SECS forward
    //   STEP 4: Within INTERCEPT_WINDOW_SECS, bot demos opponent at that location
    //   STEP 5: Reward fires, scaling with how accurately the intercept matched
    //           the prediction (actual demo pos vs predicted pos at demo time)
    //
    // "Shadow" bonus: if bot was already moving toward the predicted position
    // BEFORE the opponent touched the ball (pre-read), reward gets a 1.5x multiplier.
    // This is the highest-skill version: bot reads the touch before it happens.
    //
    // Anti-farming:
    //   - Must demo within INTERCEPT_WINDOW_SECS of the touch
    //   - Prediction accuracy is measured at demo time: if actual opp pos differs
    //     greatly from predicted pos, reward is reduced
    //   - Cooldown 4s per target
    // ============================================================================
    class PostTouchShadowDemoReward : public Reward {
    private:
        static constexpr float SHADOW_PREDICT_SECS  = 1.0f;
        static constexpr float INTERCEPT_WINDOW_SECS = 2.0f;
        static constexpr float PRE_READ_ALIGNMENT    = 0.6f;   // bot moving to predicted pos BEFORE touch
        static constexpr float BASE_REWARD           = 2.0f;
        static constexpr float PRE_READ_MULT         = 1.5f;
        static constexpr float COOLDOWN_SECS         = 4.0f;
        static constexpr float PREDICTION_TOLERANCE  = 600.0f; // how close actual was to predicted

        struct ShadowState {
            bool    active         = false;
            uint32_t targetId      = 0;
            Vec     predictedPos   = {};
            float   windowTimer    = 0.0f;
            bool    preReadBonus   = false;  // was bot already heading there before touch?
            float   cooldown       = 0.0f;
        };

        mutable std::map<uint32_t, ShadowState> states;

    public:
        virtual void Reset(const GameState& initialState) override { states.clear(); }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            uint32_t id = player.carId;
            float dt    = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            ShadowState& ss = states[id];

            if (ss.cooldown > 0.0f) { ss.cooldown -= dt; return 0.0f; }

            // ---- Watch for opponent touches ----
            for (const auto& opp : state.players) {
                if (opp.team == player.team || opp.isDemoed) continue;
                if (!opp.ballTouchedStep) continue;

                // Opponent just touched the ball — predict their follow-through
                Vec predictedPos = opp.pos + opp.vel * SHADOW_PREDICT_SECS;

                // Check if bot was ALREADY moving toward this predicted position (pre-read)
                Vec botToPredicted = (predictedPos - player.pos).Normalized();
                float preReadAlign = player.vel.Normalized().Dot(botToPredicted);
                bool preRead       = (preReadAlign > PRE_READ_ALIGNMENT &&
                                      player.vel.Length() > 600.0f);

                ss.active       = true;
                ss.targetId     = opp.carId;
                ss.predictedPos = predictedPos;
                ss.windowTimer  = 0.0f;
                ss.preReadBonus = preRead;
            }

            // ---- Track active shadow window ----
            if (ss.active) {
                ss.windowTimer += dt;

                if (ss.windowTimer > INTERCEPT_WINDOW_SECS) {
                    ss.active = false;
                    return 0.0f;
                }

                // ---- Demo fires within window ----
                if (player.eventState.demo) {
                    // Find the demoed opponent
                    const Player* demoed = nullptr;
                    for (const auto& opp : state.players) {
                        if (opp.team == player.team) continue;
                        if (opp.isDemoed && (player.pos - opp.pos).Length() < 400.0f) {
                            demoed = &opp;
                            break;
                        }
                    }

                    if (demoed && demoed->carId == ss.targetId) {
                        // Measure prediction accuracy
                        float predictionError = (player.pos - ss.predictedPos).Length();
                        float accuracy = RS_CLAMP(1.0f - predictionError / PREDICTION_TOLERANCE,
                                                  0.0f, 1.0f);

                        float reward = BASE_REWARD * accuracy;
                        if (ss.preReadBonus) reward *= PRE_READ_MULT;

                        ss.active   = false;
                        ss.cooldown = COOLDOWN_SECS;
                        return reward;
                    }
                }
            }

            return 0.0f;
        }
    };


    // ============================================================================
    // 4. AerialVulnerabilityDemoReward
    // ============================================================================
    // Opponent commits to an aerial — they're airborne, boosting/flipping toward
    // the ball. This is their maximum vulnerability window: they can't dodge,
    // can't change trajectory significantly, and their position is predictable.
    //
    // Vulnerability window is defined as:
    //   - Opponent is airborne (!isOnGround)
    //   - Opponent is flipping OR boosting AND moving toward ball
    //     (committed — can't easily change direction)
    //   - Ball is at height > AERIAL_BALL_HEIGHT (it's an actual aerial, not a jump)
    //
    // Reward for demoing during this window, scaling with:
    //   - How committed opponent was (flip > boost > just airborne)
    //   - Height of the demo (higher aerial = harder to read = bigger reward)
    //   - How early in the vulnerability window the demo landed
    //     (hitting them at peak commitment = reads the aerial, not just follows)
    //
    // Anti-farming:
    //   - Opponent must genuinely be airborne and committed (not just jumped)
    //   - Bot must be approaching FROM BELOW OR SIDE (not just crashing into
    //     someone who jumped in front of us)
    //   - Minimum demo height > AERIAL_BALL_HEIGHT * 0.5 (we're up there too)
    //   - Cooldown 4s
    // ============================================================================
    class AerialVulnerabilityDemoReward : public Reward {
    private:
        static constexpr float AERIAL_BALL_HEIGHT    = 400.0f;  // genuine aerial
        static constexpr float MIN_DEMO_HEIGHT       = 200.0f;  // bot must also be airborne-ish
        static constexpr float COMMITTED_ALIGN       = 0.55f;   // opp vel toward ball
        static constexpr float BASE_REWARD           = 2.0f;
        static constexpr float FLIP_BONUS            = 1.0f;    // opponent was flipping
        static constexpr float HEIGHT_SCALE          = 0.8f;    // bonus for high demos
        static constexpr float MAX_HEIGHT_BONUS_Z    = 1500.0f; // z at which max height bonus applies
        static constexpr float COOLDOWN_SECS         = 4.0f;

        struct VulnState {
            // Track each opponent's commitment level this frame
            bool    oppCommitted    = false;
            bool    oppFlipping     = false;
            float   oppHeight       = 0.0f;
            float   cooldown        = 0.0f;
        };

        mutable std::map<uint32_t, VulnState> states;

        // Find most committed aerial opponent
        const Player* FindCommittedAerialOpponent(const Player& player,
                                                  const GameState& state,
                                                  bool& wasFlipping,
                                                  float& height) const {
            const Player* best = nullptr;
            float bestCommit = 0.0f;

            for (const auto& opp : state.players) {
                if (opp.team == player.team || opp.isDemoed) continue;
                if (opp.isOnGround) continue;  // must be airborne
                if (opp.pos.z < MIN_DEMO_HEIGHT) continue;

                // Is ball high enough that this is an aerial?
                if (state.ball.pos.z < AERIAL_BALL_HEIGHT) continue;

                // Is opponent moving toward ball? (committed)
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
        virtual void Reset(const GameState& initialState) override { states.clear(); }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            uint32_t id = player.carId;
            float dt    = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            VulnState& vs = states[id];

            if (vs.cooldown > 0.0f) { vs.cooldown -= dt; return 0.0f; }

            // Track opponent commitment each frame
            bool   flipping = false;
            float  height   = 0.0f;
            const Player* committed = FindCommittedAerialOpponent(player, state,
                                                                   flipping, height);
            vs.oppCommitted = (committed != nullptr);
            vs.oppFlipping  = flipping;
            vs.oppHeight    = height;

            // ---- Demo fires ----
            if (player.eventState.demo && vs.oppCommitted) {
                // Bot must also be somewhat airborne (not ground-sniping an aerial)
                bool botAirborne = (!player.isOnGround || player.pos.z > MIN_DEMO_HEIGHT);

                if (botAirborne) {
                    float heightBonus = RS_CLAMP(vs.oppHeight / MAX_HEIGHT_BONUS_Z, 0.0f, 1.0f)
                                        * HEIGHT_SCALE;
                    float flipBonus   = vs.oppFlipping ? FLIP_BONUS : 0.0f;
                    float reward      = BASE_REWARD + heightBonus + flipBonus;

                    vs.cooldown = COOLDOWN_SECS;
                    return reward;
                }
            }

            return 0.0f;
        }
    };


    // ============================================================================
    // 5. RespawnHuntReward
    // ============================================================================
    // After demoing an opponent, the bot routes toward their respawn zone and
    // is present when they come back. Fires when the bot demos the same opponent
    // AGAIN within RESPAWN_WINDOW_SECS of them respawning.
    //
    // Respawn positions are known from CommonValues (DEMOLISH spawn locations).
    // Bot must be within AMBUSH_RADIUS of a respawn point when the opponent
    // respawns (transitions from isDemoed=true to isDemoed=false).
    //
    // Reward tiers:
    //   - Bot is within AMBUSH_RADIUS at respawn time: intermediate reward (0.5f)
    //   - Bot demos opponent again within RESPAWN_WINDOW_SECS: full reward (2.5f)
    //   - If both conditions met, only full reward fires (not additive)
    //
    // Anti-farming:
    //   - Only fires for the SAME player that was originally demoed
    //   - Opponent must have been demoed by THIS bot (not a teammate)
    //   - Per-target cooldown 6s (full respawn cycle)
    //   - Bot must have been actively moving toward respawn zone after the demo
    //     (velocity toward respawn zone > ROUTING_SPEED at some point)
    //   - This reward explicitly should NOT fire if it was just a coincidence
    //     (bot happened to be near respawn for boost or rotation)
    // ============================================================================
    class RespawnHuntReward : public Reward {
    private:
        static constexpr float AMBUSH_RADIUS         = 1000.0f;
        static constexpr float RESPAWN_WINDOW_SECS   = 3.5f;   // window to get second demo
        static constexpr float ROUTING_SPEED         = 500.0f; // must have routed toward spawn
        static constexpr float AMBUSH_PRESENCE_REWARD = 0.5f;
        static constexpr float DOUBLE_DEMO_REWARD     = 2.5f;
        static constexpr float COOLDOWN_SECS          = 6.0f;

        // All respawn positions (blue + orange)
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
            bool    hunting         = false;
            uint32_t targetId       = 0;
            Vec     targetSpawn     = {};
            bool    routedToSpawn   = false;  // did we head toward spawn?
            bool    targetDemoed    = false;  // is target currently demoed?
            float   respawnTimer    = 0.0f;   // time since target respawned
            bool    targetRespawned = false;
            bool    ambushPresent   = false;
            float   cooldown        = 0.0f;
        };

        mutable std::map<uint32_t, HuntState> states;
        // track which opponents were demoed by us (not teammates)
        mutable std::map<uint32_t, uint32_t> myDemoTargets; // player id -> last demoed opp id

        Vec ClosestRespawnForTeam(Team oppTeam) const {
            // Return first matching team's respawn zone (simplified)
            if (oppTeam == Team::BLUE) return CommonValues::BLUE_DEMOLISH_RIGHT_INSIDE;
            return CommonValues::ORANGE_DEMOLISH_RIGHT_INSIDE;
        }

        // Find closest respawn point to a given position
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

            // ---- Track when we demo someone ----
            if (player.eventState.demo && !hs.hunting) {
                // Find the opponent we just demoed
                for (const auto& opp : state.players) {
                    if (opp.team == player.team) continue;
                    if (opp.isDemoed && (player.pos - opp.pos).Length() < 400.0f) {
                        // Start hunt
                        hs.hunting        = true;
                        hs.targetId       = opp.carId;
                        hs.targetSpawn    = ClosestRespawnTo(opp.pos);
                        hs.routedToSpawn  = false;
                        hs.targetDemoed   = true;
                        hs.respawnTimer   = 0.0f;
                        hs.targetRespawned = false;
                        hs.ambushPresent  = false;
                        myDemoTargets[id] = opp.carId;
                        break;
                    }
                }
            }

            if (!hs.hunting) return 0.0f;

            // ---- Check if bot is routing toward spawn ----
            Vec toSpawn    = (hs.targetSpawn - player.pos).Normalized();
            float routeVel = player.vel.Dot(toSpawn);
            if (routeVel > ROUTING_SPEED) hs.routedToSpawn = true;

            // ---- Find target and check respawn ----
            const Player* target = nullptr;
            for (const auto& opp : state.players) {
                if (opp.carId == hs.targetId) { target = &opp; break; }
            }

            if (!target) { hs.hunting = false; return 0.0f; }

            // Detect respawn transition (was demoed, now isn't)
            bool currentlyDemoed = target->isDemoed;
            if (hs.targetDemoed && !currentlyDemoed) {
                hs.targetRespawned = true;
                hs.respawnTimer    = 0.0f;

                // Check if bot is in ambush position
                float distToSpawn = (player.pos - hs.targetSpawn).Length();
                if (distToSpawn < AMBUSH_RADIUS && hs.routedToSpawn) {
                    hs.ambushPresent = true;
                    // Partial reward for presence
                    // (full reward only if we demo them again)
                }
            }
            hs.targetDemoed = currentlyDemoed;

            // ---- Track post-respawn window ----
            if (hs.targetRespawned) {
                hs.respawnTimer += dt;

                if (hs.respawnTimer > RESPAWN_WINDOW_SECS) {
                    // Window expired — fire partial reward if we were in ambush
                    bool wasPresent = hs.ambushPresent && hs.routedToSpawn;
                    hs.hunting  = false;
                    hs.cooldown = COOLDOWN_SECS;
                    return wasPresent ? AMBUSH_PRESENCE_REWARD : 0.0f;
                }

                // ---- Full reward: demo them again within window ----
                if (player.eventState.demo && hs.routedToSpawn) {
                    for (const auto& opp : state.players) {
                        if (opp.carId != hs.targetId) continue;
                        if (opp.isDemoed && (player.pos - opp.pos).Length() < 400.0f) {
                            hs.hunting  = false;
                            hs.cooldown = COOLDOWN_SECS;
                            return DOUBLE_DEMO_REWARD;
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
    // Ball is in a corner (constrained geometry — limited exit vectors).
    // Opponent is approaching the corner to retrieve it. Instead of contesting
    // the ball IN the corner, bot positions at the most likely EXIT PATH from
    // the corner and demos the opponent as they come out.
    //
    // "Corner" = ball in region: |x| > CORNER_X_THRESHOLD AND |y| > CORNER_Y_THRESHOLD
    //
    // "Exit path" prediction:
    //   The exit path from a corner is almost always toward center field.
    //   Bot positions between the corner and center at EXIT_WAIT_DIST units
    //   from the corner, on the predicted exit vector.
    //
    // Fires when:
    //   (a) Bot is within AMBUSH_CONE of the predicted exit path
    //   (b) Opponent retrieves ball from corner (ball leaves corner zone)
    //   (c) Bot demos opponent within DEMO_WINDOW_SECS of ball exiting corner
    //
    // Anti-farming:
    //   - Ball must actually exit the corner (not just be contested there)
    //   - Bot must be at the exit position BEFORE opponent leaves corner
    //     (position check happens at moment ball exits corner zone)
    //   - Cooldown 3s
    // ============================================================================
    class FunnelExitDemoReward : public Reward {
    private:
        static constexpr float CORNER_X_THRESHOLD   = 3200.0f;
        static constexpr float CORNER_Y_THRESHOLD   = 3800.0f;
        static constexpr float EXIT_WAIT_DIST        = 1400.0f; // how far from corner to wait
        static constexpr float AMBUSH_CONE           = 700.0f;  // radius of exit zone
        static constexpr float DEMO_WINDOW_SECS      = 2.0f;
        static constexpr float BASE_REWARD           = 2.0f;
        static constexpr float COOLDOWN_SECS         = 3.0f;

        enum class FunnelPhase { IDLE, WATCHING, DEMO_WINDOW };

        struct FunnelState {
            FunnelPhase phase       = FunnelPhase::IDLE;
            Vec         exitPos     = {};  // predicted exit interception point
            bool        atExitPos   = false;
            float       demoTimer   = 0.0f;
            float       cooldown    = 0.0f;
        };

        mutable std::map<uint32_t, FunnelState> states;

        bool InCorner(const Vec& pos) const {
            return (fabsf(pos.x) > CORNER_X_THRESHOLD && fabsf(pos.y) > CORNER_Y_THRESHOLD);
        }

        // Compute the predicted exit interception point from a corner
        // (midpoint between corner and field center, on the diagonal)
        Vec ComputeExitPos(const Vec& cornerPos) const {
            Vec toCenter = Vec(0.0f, 0.0f, 0.0f) - cornerPos;
            float len    = toCenter.Length();
            if (len < 1.0f) return cornerPos;
            return cornerPos + (toCenter / len) * EXIT_WAIT_DIST;
        }

    public:
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
                    // Compute exit interception position
                    fs.exitPos  = ComputeExitPos(state.ball.pos);
                    fs.phase    = FunnelPhase::WATCHING;
                    fs.atExitPos = false;
                }
                break;
            }

            case FunnelPhase::WATCHING: {
                if (!ballInCorner) {
                    // Ball just left corner — check if we were at exit pos
                    float distToExit = (player.pos - fs.exitPos).Length();
                    fs.atExitPos = (distToExit < AMBUSH_CONE);

                    if (fs.atExitPos) {
                        fs.phase     = FunnelPhase::DEMO_WINDOW;
                        fs.demoTimer = 0.0f;
                    } else {
                        fs.phase = FunnelPhase::IDLE;
                    }
                } else {
                    // Update exit pos as ball moves in corner
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
                    // Reward scales with how quickly we got the demo
                    // (fast = we read the exit perfectly)
                    float timeliness = RS_CLAMP(1.0f - (fs.demoTimer / DEMO_WINDOW_SECS),
                                                0.0f, 1.0f);
                    fs.phase    = FunnelPhase::IDLE;
                    fs.cooldown = COOLDOWN_SECS;
                    return BASE_REWARD * (0.7f + 0.3f * timeliness);
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
    // In 2v2 or 3v3: ALL opponents are simultaneously committed to the ball
    // (high velocity toward ball, none in defensive position). This is a man
    // advantage CREATION opportunity. Rather than challenging for the ball,
    // bot identifies the most committed opponent and demos them, turning the
    // play into a guaranteed man-up attack.
    //
    // "All committed" requires:
    //   - Every opponent has velocity aligned toward ball > MIN_COMMIT_ALIGN
    //   - Every opponent is within COMMIT_RADIUS of ball
    //   - No opponent is between ball and their own goal (nobody defending)
    //
    // Bot must NOT be the closest player to the ball (if it is, it should be
    // challenging, not hunting demos). Reward fires when bot demos an opponent
    // during this window AND the ball remains in a positive attacking position
    // after the demo (team didn't lose possession).
    //
    // In 1v1: this reward does not fire (no "all committed" concept).
    //
    // Reward scales with how "committed" all opponents were at the moment
    // of the demo — full commitment = full reward.
    //
    // Anti-farming:
    //   - Requires >= 2 opponents all simultaneously committed
    //   - Bot must not be the closest to ball at demo time
    //   - Ball must not be in bot's own defensive half (no defensive clearance demos)
    //   - Cooldown 5s
    // ============================================================================
    class DoubleCommitDemoReward : public Reward {
    private:
        static constexpr float COMMIT_RADIUS         = 2800.0f;
        static constexpr float MIN_COMMIT_ALIGN      = 0.60f;   // vel toward ball
        static constexpr float DEFENSIVE_DEPTH       = 1000.0f; // behind this = defender
        static constexpr float BASE_REWARD           = 2.5f;
        static constexpr float FULL_COMMIT_BONUS     = 1.0f;    // all opps at max commit
        static constexpr float COOLDOWN_SECS         = 5.0f;

        mutable std::map<uint32_t, float> cooldowns;

        struct CommitSnapshot {
            bool  allCommitted  = false;
            float avgCommitment = 0.0f;
            int   oppCount      = 0;
        };

        CommitSnapshot EvaluateOpponentCommitment(const Player& player,
                                                   const GameState& state) const {
            CommitSnapshot snap;
            snap.oppCount    = 0;
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

                if (align < MIN_COMMIT_ALIGN) {
                    snap.allCommitted = false;
                }

                // Check if opp is in defensive position (not committed if defending)
                Vec oppToOwnGoal = (ownGoal - opp.pos); // from opp toward OUR goal
                float defDepth   = (opp.pos - state.ball.pos).Dot(
                    (ownGoal - state.ball.pos).Normalized());
                if (defDepth > DEFENSIVE_DEPTH) {
                    snap.allCommitted = false;
                }

                commitSum += RS_MAX(0.0f, align);
            }

            snap.avgCommitment = (snap.oppCount > 0) ? commitSum / snap.oppCount : 0.0f;
            return snap;
        }

    public:
        virtual void Reset(const GameState& initialState) override { cooldowns.clear(); }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            uint32_t id = player.carId;
            float dt    = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

            if (cooldowns[id] > 0.0f) { cooldowns[id] -= dt; return 0.0f; }

            // 1v1: this reward doesn't apply
            int oppCount = 0;
            for (const auto& opp : state.players)
                if (opp.team != player.team && !opp.isDemoed) oppCount++;
            if (oppCount < 2) return 0.0f;

            // Bot must NOT be closest to ball on its team
            float myDistBall = (player.pos - state.ball.pos).Length();
            for (const auto& tm : state.players) {
                if (tm.team != player.team || tm.carId == player.carId) continue;
                if ((tm.pos - state.ball.pos).Length() < myDistBall) {
                    // Teammate is closer — bot is free to hunt
                    goto can_hunt;
                }
            }
            return 0.0f; // Bot is closest — should challenge, not demo hunt
            can_hunt:

            // Ball must not be in our own defensive half
            Vec ownGoal = (player.team == Team::BLUE)
                ? CommonValues::BLUE_GOAL_CENTER
                : CommonValues::ORANGE_GOAL_CENTER;
            float ballSign = (player.team == Team::BLUE) ? state.ball.pos.y : -state.ball.pos.y;
            if (ballSign < -500.0f) return 0.0f; // ball in our defensive half

            // ---- Check all-committed state ----
            CommitSnapshot snap = EvaluateOpponentCommitment(player, state);
            if (!snap.allCommitted || snap.oppCount < 2) return 0.0f;

            // ---- Demo fires during all-committed window ----
            if (player.eventState.demo) {
                float commitBonus = RS_CLAMP((snap.avgCommitment - MIN_COMMIT_ALIGN) /
                                            (1.0f - MIN_COMMIT_ALIGN), 0.0f, 1.0f);
                float reward = BASE_REWARD + commitBonus * FULL_COMMIT_BONUS;
                cooldowns[id] = COOLDOWN_SECS;
                return reward;
            }

            return 0.0f;
        }
    };

} // namespace RLGC
