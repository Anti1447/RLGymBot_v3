#pragma once
// ============================================================================
// BallPredictionValidator.h
//
// Validates BallPred::Cache (BallPrediction.h) against what actually happens.
//
// BallPred::Cache re-simulates the ball's next 4 seconds from scratch every
// step. This validator remembers what a past step's cache predicted for a
// handful of fixed horizons (0.5s / 1.0s / 2.0s) and, once that much real
// time has elapsed, compares the prediction to the ball's actual position
// and velocity. BallPrediction.h itself documents that it does not model
// angular velocity / Magnus effect, so predictions are expected to degrade
// after high-spin contacts — this is how that degradation gets measured
// instead of assumed.
//
// Not tied to any single reward: 15+ reward classes read BallPred::Cache
// (AerialOutplayReward, BackboardReboundReward, PreemptiveSaveRewards,
// WallCeilingRedirectReward, MultiModalRewards, etc.), so this is wired up
// independently in Learner.h/.cpp, the same way FlipResetMetricsCollector is
// — not owned by any one reward.
//
// Usage: one BallPredictionValidator total. Step() is called once per
// arena's GameState per env step (see Learner.cpp) and keys its per-arena
// prediction queues internally via GameState::lastArena.
// ============================================================================

#include <deque>
#include <unordered_map>
#include <cmath>
#include <nlohmann/json.hpp>
#include "../Gamestates/GameState.h"
#include "BallPrediction.h"

namespace RLGC {

    struct BallPredValidationStats {
        // Welford mean/variance of position error (UU) and velocity error
        // (UU/s) between a past prediction and the ball's actual state once
        // that prediction's horizon has elapsed.
        struct HorizonStats {
            double  posErrMean = 0.0, posErrM2 = 0.0;
            double  velErrMean = 0.0, velErrM2 = 0.0;
            float   posErrMax  = 0.0f;
            int64_t count      = 0;

            void Record(float posErr, float velErr) {
                count++;
                double d1 = (double)posErr - posErrMean;
                posErrMean += d1 / count;
                posErrM2 += d1 * ((double)posErr - posErrMean);

                double d2 = (double)velErr - velErrMean;
                velErrMean += d2 / count;
                velErrM2 += d2 * ((double)velErr - velErrMean);

                posErrMax = std::max(posErrMax, posErr);
            }

            float GetPosErrMean() const { return (float)posErrMean; }
            float GetPosErrStd() const { return count < 2 ? 0.0f : (float)std::sqrt(posErrM2 / (count - 1)); }
            float GetVelErrMean() const { return (float)velErrMean; }

            void Reset() {
                posErrMean = posErrM2 = velErrMean = velErrM2 = 0.0;
                posErrMax = 0.0f;
                count = 0;
            }
        };

        HorizonStats at0_5s, at1_0s, at2_0s;

        void Reset() { at0_5s.Reset(); at1_0s.Reset(); at2_0s.Reset(); }

        nlohmann::json ToJSON() const {
            nlohmann::json j;
            j["pos_err_mean_0.5s"] = at0_5s.GetPosErrMean();
            j["pos_err_std_0.5s"]  = at0_5s.GetPosErrStd();
            j["pos_err_max_0.5s"]  = at0_5s.posErrMax;
            j["pos_err_mean_1.0s"] = at1_0s.GetPosErrMean();
            j["pos_err_std_1.0s"]  = at1_0s.GetPosErrStd();
            j["pos_err_max_1.0s"]  = at1_0s.posErrMax;
            j["pos_err_mean_2.0s"] = at2_0s.GetPosErrMean();
            j["pos_err_std_2.0s"]  = at2_0s.GetPosErrStd();
            j["pos_err_max_2.0s"]  = at2_0s.posErrMax;
            j["vel_err_mean_1.0s"] = at1_0s.GetVelErrMean();
            j["sample_count"]      = at1_0s.count;
            return j;
        }
    };

    class BallPredictionValidator {
    private:
        static constexpr float HORIZON_SHORT = 0.5f;
        static constexpr float HORIZON_MID   = 1.0f;
        static constexpr float HORIZON_LONG  = 2.0f;

        // A ball position jump bigger than this in one step can't happen from
        // normal flight (max ball speed ~6000uu/s * a ~0.067s step is ~400uu) —
        // it means a goal/kickoff/state-setter reset just teleported the ball,
        // so in-flight predictions spanning that instant are meaningless and
        // must be discarded rather than scored as prediction error.
        static constexpr float RESET_JUMP_THRESHOLD = 3000.0f;

        struct PendingSample {
            float dueIn; // seconds remaining until this prediction should be checked
            Vec   pos;
            Vec   vel;
        };

        struct ArenaState {
            BallPred::Cache cache;
            std::deque<PendingSample> pendingShort, pendingMid, pendingLong;
        };

        std::unordered_map<const void*, ArenaState> _arenaStates;
        BallPredValidationStats _aggregated;

        static void StepQueue(std::deque<PendingSample>& q, float dt, const Vec& actualPos,
                               const Vec& actualVel, BallPredValidationStats::HorizonStats& stats) {
            for (auto& s : q)
                s.dueIn -= dt;
            while (!q.empty() && q.front().dueIn <= dt * 0.5f) {
                float posErr = (q.front().pos - actualPos).Length();
                float velErr = (q.front().vel - actualVel).Length();
                stats.Record(posErr, velErr);
                q.pop_front();
            }
        }

    public:
        void Reset() {
            _aggregated.Reset();
        }

        // Process one arena's game state. Call once per arena per env step.
        void Step(const GameState& state, float deltaTime) {
            float dt = (deltaTime > 0.0f) ? deltaTime : (1.0f / 120.0f);

            ArenaState& as = _arenaStates[(const void*)state.lastArena];

            bool resetDetected = false;
            if (state.prev)
                resetDetected = (state.ball.pos - state.prev->ball.pos).Length() > RESET_JUMP_THRESHOLD;

            if (resetDetected) {
                as.pendingShort.clear();
                as.pendingMid.clear();
                as.pendingLong.clear();
            } else {
                StepQueue(as.pendingShort, dt, state.ball.pos, state.ball.vel, _aggregated.at0_5s);
                StepQueue(as.pendingMid,   dt, state.ball.pos, state.ball.vel, _aggregated.at1_0s);
                StepQueue(as.pendingLong,  dt, state.ball.pos, state.ball.vel, _aggregated.at2_0s);
            }

            as.cache.Update(state);
            if (as.cache.valid) {
                as.pendingShort.push_back({ HORIZON_SHORT, as.cache.AtTime(HORIZON_SHORT).pos, as.cache.AtTime(HORIZON_SHORT).vel });
                as.pendingMid.push_back(  { HORIZON_MID,   as.cache.AtTime(HORIZON_MID).pos,   as.cache.AtTime(HORIZON_MID).vel });
                as.pendingLong.push_back( { HORIZON_LONG,  as.cache.AtTime(HORIZON_LONG).pos,  as.cache.AtTime(HORIZON_LONG).vel });
            }
        }

        const BallPredValidationStats& GetStats() const { return _aggregated; }
        nlohmann::json GetMetricsJSON() const { return _aggregated.ToJSON(); }
    };

} // namespace RLGC
