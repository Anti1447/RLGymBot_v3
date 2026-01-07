#pragma once
// ============================================================================
// DribbleFlickMetrics.h
//
// Rev 3 - 2025-12-15 (NJH): Fix missing episode metrics.
//   - Episode rollover detection now uses Player::updateCounter reset (stateset) 
//     instead of relying on state.prev == nullptr.
//   - Adds rolling logging (gated by doExpensiveMetrics) so W&B always shows multiple metrics.
// ============================================================================

#include <cstdint>
#include <cmath>
#include <map>
#include <vector>

#include <RLGymCPP/Gamestates/GameState.h>

namespace RLGC {

    class DribbleFlickEpisodeMetrics {
    public:
        template <typename TReport>
        inline void ProcessBatch(const std::vector<GameState>& states, TReport& report, bool doExpensiveMetrics) {
            EnsureArenaSize(states.size());

            for (size_t arenaIdx = 0; arenaIdx < states.size(); ++arenaIdx) {
                const GameState& state = states[arenaIdx];
                ArenaEpisodeStats& ep = arenaStats[arenaIdx];

                const float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

                // NEW: detect episode reset via updateCounter drop (stateset)
                if (EpisodeResetDetected(ep, state)) {
                    FlushEpisode(ep, report);
                    ResetEpisode(ep);
                }

                ep.episodeTimeSec += dt;

                for (const auto& player : state.players) {
                    PlayerStats& ps = ep.perPlayer[player.carId];

                    if (player.ballTouchedStep) ps.touchCount += 1;

                    // --- Dribble tracking ---
                    const bool isDribbling = IsDribblingCarry(player, state);
                    if (isDribbling) {
                        ps.dribbleTimeSec += dt;
                        ps.curDribbleLenSec += dt;
                        if (!ps.wasDribbling) ps.dribbleCount += 1;
                        ps.wasDribbling = true;
                    }
                    else {
                        EndActiveDribble(ps);
                    }

                    // --- Flick tracking ---
                    const bool onCar = BallOnCarSimple(player, state);

                    // Keep last carried time; don't zero it until after we decide whether this touch was a flick.
                    // (Your old ordering zeroed onCarTimeSec before the flick check, making flickCount never increment.)
                    if (onCar && player.isOnGround) {
                        ps.onCarTimeSec += dt;
                    }

                    bool countedFlick = false;

                    // If we touched on the exact tick the ball is no longer "on car", treat that as a potential flick,
                    // using the carry time we accumulated *before* this tick.
                    if (player.ballTouchedStep && !onCar && ps.onCarTimeSec >= kMinOnCarTime) {
                        const float spd = state.ball.vel.Length();
                        if (spd >= kMinFlickSpeed) {
                            ps.flickCount += 1;
                            ps.flickOnGoalCount += BallIntersectsOppGoalMouth(player, state) ? 1 : 0;
                            ps.flickSpeedSum += spd;
                            ps.flickVzSum += state.ball.vel.z;
                            countedFlick = true;
                        }
                        ps.onCarTimeSec = 0.0f; // consume carry time after a touch-at-release
                    }

                    // If the ball is off-car and we did NOT just count a flick, reset carry time normally.
                    if (!onCar && !countedFlick && !player.ballTouchedStep) {
                        ps.onCarTimeSec = 0.0f;
                    }

                    if (doExpensiveMetrics) {
                        report.AddAvg("Dribble/Carry Ratio (per-step)", isDribbling);
                        report.AddAvg("Flick/OnCar Ratio (per-step)", onCar);
                    }

                }

                // NEW: rolling episode metrics so you *always* see multiple plots
                if (doExpensiveMetrics) {
                    ReportRolling(ep, report);
                }
            }
        }

    private:
        static constexpr float kMinOnCarTime = 0.25f;
        static constexpr float kMinFlickSpeed = 1500.0f;

        struct PlayerStats {
            float dribbleTimeSec = 0.0f;
            float sumDribbleLenSec = 0.0f;
            float curDribbleLenSec = 0.0f;
            float longestDribbleSec = 0.0f;
            int   dribbleCount = 0;

            float onCarTimeSec = 0.0f;
            int   flickCount = 0;
            int   flickOnGoalCount = 0;
            float flickSpeedSum = 0.0f;
            float flickVzSum = 0.0f;

            int   touchCount = 0;
            bool  wasDribbling = false;
        };

        struct ArenaEpisodeStats {
            float episodeTimeSec = 0.0f;
            std::map<uint32_t, PlayerStats> perPlayer;

            // NEW: used for reset detection
            bool hasInitCounters = false;
            uint64_t lastMinUpdateCounter = 0;
        };

        std::vector<ArenaEpisodeStats> arenaStats;

        inline void EnsureArenaSize(size_t n) {
            if (arenaStats.size() < n) arenaStats.resize(n);
        }

        static inline void ResetEpisode(ArenaEpisodeStats& ep) {
            ep.episodeTimeSec = 0.0f;
            ep.perPlayer.clear();
            ep.hasInitCounters = false;
            ep.lastMinUpdateCounter = 0;
        }

        static inline void EndActiveDribble(PlayerStats& ps) {
            if (ps.wasDribbling && ps.curDribbleLenSec > 0.0f) {
                ps.sumDribbleLenSec += ps.curDribbleLenSec;
                ps.longestDribbleSec = RS_MAX(ps.longestDribbleSec, ps.curDribbleLenSec);
                ps.curDribbleLenSec = 0.0f;
            }
            ps.wasDribbling = false;
        }

        // NEW: episode reset detection via updateCounter reset on SetState() 
        static inline bool EpisodeResetDetected(ArenaEpisodeStats& ep, const GameState& state) {
            if (state.players.empty()) return false;

            uint64_t minUC = UINT64_MAX;
            for (const auto& p : state.players) {
                minUC = RS_MIN(minUC, p.updateCounter);
            }

            if (!ep.hasInitCounters) {
                ep.hasInitCounters = true;
                ep.lastMinUpdateCounter = minUC;
                return false;
            }

            // If min updateCounter drops, we almost certainly had a stateset / new episode.
            if (minUC < ep.lastMinUpdateCounter) {
                ep.lastMinUpdateCounter = minUC;
                return true;
            }

            ep.lastMinUpdateCounter = minUC;
            return false;
        }

        template <typename TReport>
        static inline void FlushEpisode(ArenaEpisodeStats& ep, TReport& report) {
            // Optional: keep this; it will now actually fire
            // You can also omit if rolling metrics are enough.
            if (ep.episodeTimeSec <= 1e-3f || ep.perPlayer.empty()) return;
            for (auto& kv : ep.perPlayer) EndActiveDribble(kv.second);
            // One-time episode summary can be added here if you want separate “(episode end)” keys.
            (void)report;
        }

        template <typename TReport>
        static inline void ReportRolling(const ArenaEpisodeStats& ep, TReport& report) {
            if (ep.episodeTimeSec <= 1e-3f || ep.perPlayer.empty()) return;

            const int numPlayers = (int)ep.perPlayer.size();

            float totalDribbleTime = 0.0f;
            float totalDribbleLenSum = 0.0f;
            int   totalDribbleCount = 0;
            float longestDribble = 0.0f;
            int   totalTouches = 0;

            int   totalFlicks = 0;
            int   totalFlickOnGoal = 0;
            float totalFlickSpeedSum = 0.0f;
            float totalFlickVzSum = 0.0f;

            for (const auto& kv : ep.perPlayer) {
                const PlayerStats& ps = kv.second;

                totalDribbleTime += ps.dribbleTimeSec;
                totalDribbleLenSum += ps.sumDribbleLenSec;
                totalDribbleCount += ps.dribbleCount;
                longestDribble = RS_MAX(longestDribble, ps.longestDribbleSec);
                totalTouches += ps.touchCount;

                totalFlicks += ps.flickCount;
                totalFlickOnGoal += ps.flickOnGoalCount;
                totalFlickSpeedSum += ps.flickSpeedSum;
                totalFlickVzSum += ps.flickVzSum;
            }

            const float avgDribblesPerPlayer = (numPlayers > 0) ? ((float)totalDribbleCount / (float)numPlayers) : 0.0f;
            const float avgDribbleLen = (totalDribbleCount > 0) ? (totalDribbleLenSum / (float)totalDribbleCount) : 0.0f;

            const float dribbleTimeRatio =
                (numPlayers > 0) ? (totalDribbleTime / (ep.episodeTimeSec * (float)numPlayers)) : 0.0f;

            const float dribbleStartsPerMin =
                (ep.episodeTimeSec > 1e-3f) ? (avgDribblesPerPlayer / (ep.episodeTimeSec / 60.0f)) : 0.0f;

            const float dribbleStartsPerTouch =
                (totalTouches > 0) ? ((float)totalDribbleCount / (float)totalTouches) : 0.0f;

            report.AddAvg("Dribble/Episode Avg Dribble Length (s)", avgDribbleLen);
            report.AddAvg("Dribble/Episode Dribble Time Ratio", dribbleTimeRatio);
            report.AddAvg("Dribble/Episode Dribbles Per Player", avgDribblesPerPlayer);
            report.AddAvg("Dribble/Episode Longest Dribble (s)", longestDribble);
            report.AddAvg("Dribble/Episode Dribble Starts Per Min", dribbleStartsPerMin);
            report.AddAvg("Dribble/Episode DribbleStarts Per Touch", dribbleStartsPerTouch);

            const float avgFlicksPerPlayer = (numPlayers > 0) ? ((float)totalFlicks / (float)numPlayers) : 0.0f;
            const float flicksPerMin =
                (ep.episodeTimeSec > 1e-3f) ? (avgFlicksPerPlayer / (ep.episodeTimeSec / 60.0f)) : 0.0f;

            const float onGoalRate = (totalFlicks > 0) ? ((float)totalFlickOnGoal / (float)totalFlicks) : 0.0f;
            const float avgFlickSpeed = (totalFlicks > 0) ? (totalFlickSpeedSum / (float)totalFlicks) : 0.0f;
            const float avgFlickVz = (totalFlicks > 0) ? (totalFlickVzSum / (float)totalFlicks) : 0.0f;

            report.AddAvg("Flick/Episode Flicks Per Player", avgFlicksPerPlayer);
            report.AddAvg("Flick/Episode Flicks Per Min", flicksPerMin);
            report.AddAvg("Flick/Episode OnGoal Rate", onGoalRate);
            report.AddAvg("Flick/Episode Avg Speed", avgFlickSpeed);
            report.AddAvg("Flick/Episode Avg Vz", avgFlickVz);
        }

        static inline bool BallOnCarSimple(const Player& player, const GameState& state) {
            Vec rel = state.ball.pos - player.pos;
            float height = rel.z;
            float horiz = Vec(rel.x, rel.y, 0).Length();
            return (height > 40.0f && height < 150.0f && horiz < 120.0f);
        }

        static inline bool IsDribblingCarry(const Player& player, const GameState& state) {
            const float kMaxForwardOffset = 170.0f;
            const float kMinForwardOffset = -80.0f;
            const float kMaxLateralOffset = 140.0f;
            const float kMaxDistanceXY = 220.0f;

            const float kMinBallHeightAboveCar = 55.0f;
            const float kMaxBallHeightAboveCar = 220.0f;

            const float kMaxRelativeSpeed = 900.0f;
            const float kMinCarSpeed = 250.0f;

            if (!player.isOnGround) return false;

            Vec diff = state.ball.pos - player.pos;

            float dz = diff.z;
            if (dz < kMinBallHeightAboveCar || dz > kMaxBallHeightAboveCar) return false;

            Vec diffXY = diff; diffXY.z = 0.0f;
            if (diffXY.Length() > kMaxDistanceXY) return false;

            float forwardOffset = player.rotMat.forward.Dot(diff);
            float lateralOffset = player.rotMat.right.Dot(diff);

            if (forwardOffset > kMaxForwardOffset || forwardOffset < kMinForwardOffset) return false;
            if (std::fabs(lateralOffset) > kMaxLateralOffset) return false;

            if (player.vel.Length() < kMinCarSpeed) return false;
            if ((state.ball.vel - player.vel).Length() > kMaxRelativeSpeed) return false;

            return true;
        }

        static inline bool BallIntersectsOppGoalMouth(const Player& player, const GameState& state, float tMaxSec = 3.0f) {
            const float goalY = (player.team == Team::BLUE) ? CommonValues::BACK_WALL_Y : -CommonValues::BACK_WALL_Y;

            const float vy = state.ball.vel.y;
            const float dy = goalY - state.ball.pos.y;

            if (fabs(vy) < 1e-3f) return false;
            if (dy * vy <= 0.0f) return false;

            const float t = dy / vy;
            if (t <= 0.0f || t > tMaxSec) return false;

            const Vec hit = state.ball.pos + state.ball.vel * t;

            const float extraWidth = 120.0f;
            const float extraHeight = 120.0f;

            const float xLimit = CommonValues::GOAL_WIDTH_FROM_CENTER + CommonValues::BALL_RADIUS + extraWidth;
            const float zMin = 0.0f - extraHeight;
            const float zMax = CommonValues::GOAL_HEIGHT + CommonValues::BALL_RADIUS + extraHeight;

            return (fabs(hit.x) <= xLimit) && (hit.z >= zMin) && (hit.z <= zMax);
        }
    };

} // namespace RLGC
