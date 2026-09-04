#pragma once
// ============================================================================
// BackboardReboundMetricsCollector.h
//
// Standalone health metrics for BackboardReboundReward (CompleteMechanicsRewards.h),
// following the same pattern as FlipResetMetricsCollector: it independently
// re-derives the reward's trigger conditions from the raw GameState rather
// than reading the reward instance's internal state, and is stepped/reported
// from Learner.cpp so its numbers reach W&B.
//
// Tracks two separate things per Section 7 of CLAUDE.md's Reward Health vs.
// Behavior Confirmation split:
//   - Is BallPred::Cache's backboard-bounce prediction (Layer 1) actually
//     getting confirmed by a real bounce? (prediction_confirmed_rate)
//   - Is the reactive touch layer firing on real bounces, and are those
//     touches converting to goals? (qualifying_touch_count, touch_to_goal_rate)
//
// Not a substitute for RocketSimVis — this only confirms the reward is
// mechanically healthy, not that the resulting bot behavior looks right.
// ============================================================================

#include <unordered_map>
#include <cmath>
#include <nlohmann/json.hpp>
#include "../Gamestates/GameState.h"
#include "../CommonValues.h"
#include "BallPrediction.h"

namespace RLGC {

    struct BackboardReboundStats {
        int64_t predictedApproachCount = 0;  // Layer 1: new predicted-bounce sequences within horizon
        int64_t actualBounceCount      = 0;  // real backboard bounces detected (velocity-flip based)
        int64_t confirmedPredictionCount = 0; // predicted approaches later matched by a real bounce
        int64_t qualifyingTouchCount    = 0;  // reactive layer: touch that cleared all scoring gates
        int64_t touchToGoalCount        = 0;  // of those touches, how many scored on the same step

        double  bounceToTouchMean = 0.0, bounceToTouchM2 = 0.0;
        int64_t bounceToTouchCount = 0;

        void RecordBounceToTouch(float seconds) {
            bounceToTouchCount++;
            double d = (double)seconds - bounceToTouchMean;
            bounceToTouchMean += d / bounceToTouchCount;
            bounceToTouchM2 += d * ((double)seconds - bounceToTouchMean);
        }

        void Reset() {
            predictedApproachCount = 0;
            actualBounceCount = 0;
            confirmedPredictionCount = 0;
            qualifyingTouchCount = 0;
            touchToGoalCount = 0;
            bounceToTouchMean = bounceToTouchM2 = 0.0;
            bounceToTouchCount = 0;
        }

        float GetPredictionConfirmedRate() const {
            if (predictedApproachCount == 0) return 0.0f;
            return (float)confirmedPredictionCount / (float)predictedApproachCount * 100.0f;
        }

        float GetTouchToGoalRate() const {
            if (qualifyingTouchCount == 0) return 0.0f;
            return (float)touchToGoalCount / (float)qualifyingTouchCount * 100.0f;
        }

        float GetAvgBounceToTouch() const {
            return bounceToTouchCount == 0 ? 0.0f : (float)bounceToTouchMean;
        }

        nlohmann::json ToJSON() const {
            nlohmann::json j;
            j["predicted_approach_count"]   = predictedApproachCount;
            j["actual_bounce_count"]        = actualBounceCount;
            j["prediction_confirmed_rate"]  = GetPredictionConfirmedRate();
            j["qualifying_touch_count"]     = qualifyingTouchCount;
            j["touch_to_goal_rate"]         = GetTouchToGoalRate();
            j["avg_bounce_to_touch_sec"]    = GetAvgBounceToTouch();
            return j;
        }
    };

    class BackboardReboundMetricsCollector {
    private:
        // Mirrors BackboardReboundReward's own constants (CompleteMechanicsRewards.h) —
        // kept in sync by hand since this collector deliberately doesn't share
        // state with the reward instance, same as FlipResetMetricsCollector.
        static constexpr float PRED_HORIZON        = 1.5f;
        static constexpr float BOUNCE_WALL_MARGIN   = 300.0f;
        static constexpr float BOUNCE_VEL_EPS       = 50.0f;
        static constexpr float BOUNCE_TIMEOUT       = 2.5f;
        static constexpr float PREDICTION_CONFIRM_TOLERANCE = 0.4f; // seconds slack between predicted and actual bounce time

        struct ArenaState {
            BallPred::Cache cache;
            double currentTime = 0.0;

            float  prevPredBounceTime = -1.0f;      // last step's nearest predicted-bounce time
            double pendingPredictionAbsTime = -1.0; // absolute time the pending prediction expects a bounce
            bool   predictionAwaitingConfirm = false;

            double lastBounceAbsTime = -1.0;
            Team   lastBounceTeam = Team::BLUE;
        };

        std::unordered_map<const void*, ArenaState> _arenaStates;
        BackboardReboundStats _aggregated;

    public:
        void Reset() {
            _aggregated.Reset();
            // Per-arena sequence-tracking state is intentionally NOT cleared here —
            // an in-flight predicted/confirmed bounce shouldn't be dropped just
            // because a W&B report happened to land mid-sequence.
        }

        // Process one arena's game state. Call once per arena per env step.
        void Step(const GameState& state, float deltaTime) {
            float dt = (deltaTime > 0.0f) ? deltaTime : (1.0f / 120.0f);
            ArenaState& as = _arenaStates[(const void*)state.lastArena];
            as.currentTime += dt;

            as.cache.Update(state);

            // ---- Predicted-approach detection (mirrors Layer 1's gates) ----
            float curBounceTime = -1.0f;
            Vec curBouncePos;
            if (as.cache.valid) {
                for (int i = 0; i < BallPred::NUM_SLICES; i++) {
                    if (as.cache.slices[i].hitBackboard) {
                        curBounceTime = as.cache.slices[i].t;
                        curBouncePos  = as.cache.slices[i].pos;
                        break;
                    }
                }
            }
            bool newSequence = curBounceTime > as.prevPredBounceTime + 0.5f;
            as.prevPredBounceTime = curBounceTime;

            if (newSequence && curBounceTime > 0.0f && curBounceTime <= PRED_HORIZON &&
                curBouncePos.z > (CommonValues::GOAL_HEIGHT - CommonValues::BALL_RADIUS)) {
                _aggregated.predictedApproachCount++;
                as.pendingPredictionAbsTime = as.currentTime + curBounceTime;
                as.predictionAwaitingConfirm = true;
            }

            // ---- Real bounce detection (mirrors the reward's Rev 4 PreStep) ----
            if (state.prev) {
                float prevVelY = state.prev->ball.vel.y;
                float curVelY  = state.ball.vel.y;
                bool velocityReversed = (prevVelY > BOUNCE_VEL_EPS && curVelY < -BOUNCE_VEL_EPS)
                                     || (prevVelY < -BOUNCE_VEL_EPS && curVelY > BOUNCE_VEL_EPS);
                bool nearBackWall = std::fabs(std::fabs(state.ball.pos.y) - CommonValues::BACK_WALL_Y) < BOUNCE_WALL_MARGIN;
                bool aboveGoalHeight = state.ball.pos.z > (CommonValues::GOAL_HEIGHT - CommonValues::BALL_RADIUS);

                if (velocityReversed && nearBackWall && aboveGoalHeight) {
                    _aggregated.actualBounceCount++;
                    as.lastBounceAbsTime = as.currentTime;
                    as.lastBounceTeam = (state.ball.pos.y > 0.0f) ? Team::BLUE : Team::ORANGE;

                    if (as.predictionAwaitingConfirm &&
                        std::fabs(as.currentTime - as.pendingPredictionAbsTime) <= PREDICTION_CONFIRM_TOLERANCE) {
                        _aggregated.confirmedPredictionCount++;
                        as.predictionAwaitingConfirm = false;
                    }
                }
            }
            // A pending prediction that aged out without a matching bounce just
            // stops counting — it was a false call, not something to retry.
            if (as.predictionAwaitingConfirm && as.currentTime > as.pendingPredictionAbsTime + PREDICTION_CONFIRM_TOLERANCE)
                as.predictionAwaitingConfirm = false;

            // ---- Qualifying touch detection (mirrors the reactive layer's gates) ----
            if (as.lastBounceAbsTime >= 0.0 && (as.currentTime - as.lastBounceAbsTime) <= BOUNCE_TIMEOUT) {
                for (const auto& player : state.players) {
                    if (!player.ballTouchedStep || player.team != as.lastBounceTeam)
                        continue;

                    float goalY = (player.team == Team::BLUE) ? CommonValues::BACK_WALL_Y : -CommonValues::BACK_WALL_Y;
                    bool inScoringZone = std::fabs(state.ball.pos.x) < 2000.0f && std::fabs(state.ball.pos.y - goalY) < 2000.0f;
                    if (!inScoringZone)
                        continue;

                    Vec targetGoal = (player.team == Team::BLUE) ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
                    float speed = state.ball.vel.Length();
                    if (speed < 1200.0f)
                        continue;

                    Vec dir = state.ball.vel / speed;
                    float align = dir.Dot((targetGoal - state.ball.pos).Normalized());
                    if (align < 0.6f)
                        continue;

                    _aggregated.qualifyingTouchCount++;
                    _aggregated.RecordBounceToTouch((float)(as.currentTime - as.lastBounceAbsTime));
                    if (state.goalScored)
                        _aggregated.touchToGoalCount++;

                    as.lastBounceAbsTime = -1.0; // consumed, mirrors the reward's own consumption
                    break; // only one player can claim a given bounce
                }
            }
        }

        const BackboardReboundStats& GetStats() const { return _aggregated; }
        nlohmann::json GetMetricsJSON() const { return _aggregated.ToJSON(); }
    };

} // namespace RLGC
