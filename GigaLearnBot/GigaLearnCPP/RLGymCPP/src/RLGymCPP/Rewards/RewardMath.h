#pragma once
// ============================================================================
// RewardMath.h
// 2025-12-18 | Rev 2.0 | NJH
// - Major revision for accurate goal detection geometry
// - Fixed goal mouth AABB to account for ball radius (center must be inside posts)
// - Corrected intersection logic to use proper goal line plane
// - Added separate "on target" vs "will score" checks
// - Improved trajectory prediction with gravity
// - All margin adjustments now properly documented
// ============================================================================
// 2025-12-18 | Rev 1.3 | NJH
// - Fix IsClosestToBall() to use Player::pos (Player has no phys member in this codebase)


#include "../CommonValues.h"

namespace RLGC {

    // ------------------------------------------------------------------------
    // Basic vector helpers
    // ------------------------------------------------------------------------
    static inline Vec SafeNorm(const Vec& v) {
        const float len = v.Length();
        if (len > 1e-6f) {
            return v * (1.0f / len);
        }
        return Vec(0.0f, 0.0f, 0.0f);
    }

    // ------------------------------------------------------------------------
    // RewardMath namespace
    // ------------------------------------------------------------------------
    namespace RewardMath {

        // ====================================================================
        // Goal Mouth Geometry
        // ====================================================================

        struct GoalMouthAABB {
            float minX, maxX;      // horizontal bounds (accounting for ball radius)
            float minZ, maxZ;      // vertical bounds (accounting for ball radius)
            float goalLineY;       // front of goal (where ball enters)
            float backNetY;        // back of net plane
            float goalSign;        // +1 for orange (positive Y), -1 for blue (negative Y)
        };

        // Configuration: How much to shrink the goal opening for "on target" checks
        // 
        // CONSERVATIVE (strict): Only count shots clearly going in
        //   - Use positive margins (shrink the target)
        //   - Example: X_MARGIN = 100, Z_MARGIN = 50
        // 
        // PERMISSIVE (loose): Count shots near the goal
        //   - Use zero or negative margins (expand the target)
        //   - Example: X_MARGIN = 0, Z_MARGIN = 0
        //
        // BALL_RADIUS is always subtracted (ball center must be inside posts)
        constexpr float GOAL_MOUTH_X_MARGIN = 0.0f;   // additional X shrink beyond ball radius
        constexpr float GOAL_MOUTH_Z_MARGIN = 0.0f;   // additional Z shrink beyond ball radius

        // Precomputed goal mouth AABBs (world coordinates)
        // These represent where the BALL CENTER must be to score
        inline const GoalMouthAABB& OrangeGoalMouthAABB() {
            static const GoalMouthAABB aabb = {
                // Ball center must be inside posts (subtract ball radius from opening)
                -(CommonValues::GOAL_WIDTH_FROM_CENTER - CommonValues::BALL_RADIUS - GOAL_MOUTH_X_MARGIN),
                 (CommonValues::GOAL_WIDTH_FROM_CENTER - CommonValues::BALL_RADIUS - GOAL_MOUTH_X_MARGIN),

                 // Ball center must be above ground and below crossbar
                 CommonValues::BALL_RADIUS,  // can't go below floor + ball radius
                 (CommonValues::GOAL_HEIGHT - CommonValues::BALL_RADIUS - GOAL_MOUTH_Z_MARGIN),

                 +CommonValues::GOAL_LINE_Y,   // front of goal (y = +5120)
                 +CommonValues::BACK_NET_Y,    // back of goal (y = +6000)
                 +1.0f                          // positive Y direction
            };
            return aabb;
        }

        inline const GoalMouthAABB& BlueGoalMouthAABB() {
            static const GoalMouthAABB aabb = {
                -(CommonValues::GOAL_WIDTH_FROM_CENTER - CommonValues::BALL_RADIUS - GOAL_MOUTH_X_MARGIN),
                 (CommonValues::GOAL_WIDTH_FROM_CENTER - CommonValues::BALL_RADIUS - GOAL_MOUTH_X_MARGIN),

                CommonValues::BALL_RADIUS,
                (CommonValues::GOAL_HEIGHT - CommonValues::BALL_RADIUS - GOAL_MOUTH_Z_MARGIN),

                -CommonValues::GOAL_LINE_Y,   // front of goal (y = -5120)
                -CommonValues::BACK_NET_Y,    // back of goal (y = -6000)
                -1.0f                          // negative Y direction
            };
            return aabb;
        }

        // Select the opponent goal mouth box for a player
        inline const GoalMouthAABB& OpponentGoalMouthAABB(const Player& player) {
            // Blue (team 0) shoots toward +Y (Orange goal)
            // Orange (team 1) shoots toward -Y (Blue goal)
            return (player.team == Team::BLUE) ? OrangeGoalMouthAABB() : BlueGoalMouthAABB();
        }

        // ====================================================================
        // Goal Intersection Detection
        // ====================================================================

        // Check if ball trajectory intersects the opponent goal mouth
        // 
        // Returns true if the ball will pass through the goal opening rectangle
        // within the specified time horizon.
        //
        // Parameters:
        //   player: The player whose opponent goal to check
        //   state: Current game state (ball position and velocity)
        //   tMaxSec: Maximum time horizon to check (seconds)
        //   requireBackNet: If true, ball must reach back net (guarantees goal)
        //                   If false, only checks goal line crossing (may hit post/bar)
        //   useGravity: If true, accounts for gravity in trajectory (more accurate)
        //               If false, assumes straight-line motion (faster but less accurate)
        //
        // Example usage:
        //   - "On target" check: requireBackNet=false, useGravity=true
        //   - "Will score" check: requireBackNet=true, useGravity=true
        //   - Fast approximation: requireBackNet=false, useGravity=false
        inline bool BallIntersectsOpponentGoalMouth(
            const Player& player,
            const GameState& state,
            float tMaxSec = 2.0f,
            bool requireBackNet = false,    // Changed default to false (more permissive)
            bool useGravity = true
        ) {
            const GoalMouthAABB& box = OpponentGoalMouthAABB(player);

            const Vec p0 = state.ball.pos;
            const Vec v0 = state.ball.vel;

            // Early exit: Ball must be moving toward opponent goal
            // Use goalSign to determine required direction (+1 = positive Y, -1 = negative Y)
            const float vyTowardGoal = box.goalSign * v0.y;
            if (vyTowardGoal <= 100.0f) return false;  // threshold to filter noise/backwards

            // Calculate time to reach goal line plane
            const float tGoalLine = (box.goalLineY - p0.y) / v0.y;

            // Validate time to goal line
            if (tGoalLine <= 0.0f || tGoalLine > tMaxSec) return false;

            // Optional: Calculate time to reach back net (for "guaranteed goal" check)
            float tBackNet = tGoalLine;
            if (requireBackNet) {
                tBackNet = (box.backNetY - p0.y) / v0.y;

                // Back net must be reached after goal line and within time horizon
                if (tBackNet <= tGoalLine || tBackNet > tMaxSec) return false;
            }

            // Setup trajectory function with optional gravity
            const float gZ = useGravity ? CommonValues::BALL_GRAVITY : 0.0f;

            auto PositionAt = [&](float t) -> Vec {
                return Vec(
                    p0.x + v0.x * t,
                    p0.y + v0.y * t,
                    p0.z + v0.z * t + 0.5f * gZ * t * t
                );
                };

            // Check if position is inside goal mouth AABB (between the posts)
            auto IsInsideGoalMouth = [&](const Vec& pos) -> bool {
                if (pos.x < box.minX || pos.x > box.maxX) return false;
                if (pos.z < box.minZ || pos.z > box.maxZ) return false;
                return true;
                };

            // Check position at goal line crossing
            const Vec posAtGoalLine = PositionAt(tGoalLine);
            if (!IsInsideGoalMouth(posAtGoalLine)) return false;

            // If requiring back net, also check position at back net crossing
            if (requireBackNet) {
                const Vec posAtBackNet = PositionAt(tBackNet);
                if (!IsInsideGoalMouth(posAtBackNet)) return false;
            }

            return true;
        }

        // ====================================================================
        // Additional Helper Functions
        // ====================================================================

        // Check if ball is currently inside a goal (already scored)
        inline bool BallIsInGoal(const GameState& state, Team goalTeam) {
            const float ballY = state.ball.pos.y;
            const float ballX = state.ball.pos.x;
            const float ballZ = state.ball.pos.z;

            if (goalTeam == Team::ORANGE) {
                // Orange goal is at positive Y
                if (ballY < CommonValues::GOAL_LINE_Y) return false;
                if (ballY > CommonValues::BACK_NET_Y) return false;
            }
            else {
                // Blue goal is at negative Y
                if (ballY > -CommonValues::GOAL_LINE_Y) return false;
                if (ballY < -CommonValues::BACK_NET_Y) return false;
            }

            // Check horizontal bounds
            if (ballX < -CommonValues::GOAL_WIDTH_FROM_CENTER) return false;
            if (ballX > CommonValues::GOAL_WIDTH_FROM_CENTER) return false;

            // Check vertical bounds
            if (ballZ < 0.0f) return false;
            if (ballZ > CommonValues::GOAL_HEIGHT) return false;

            return true;
        }

        // Get distance from ball to center of opponent goal
        inline float DistanceToOpponentGoal(const Player& player, const GameState& state) {
            const Vec goalCenter = (player.team == Team::BLUE)
                ? CommonValues::ORANGE_GOAL_CENTER
                : CommonValues::BLUE_GOAL_CENTER;

            return (state.ball.pos - goalCenter).Length();
        }

        // Get 2D angle between ball velocity and direction to opponent goal
        // Returns angle in radians (0 = directly toward goal, PI = directly away)
        inline float AngleToOpponentGoal(const Player& player, const GameState& state) {
            const Vec goalCenter = (player.team == Team::BLUE)
                ? CommonValues::ORANGE_GOAL_CENTER
                : CommonValues::BLUE_GOAL_CENTER;

            const Vec toGoal = SafeNorm(Vec(
                goalCenter.x - state.ball.pos.x,
                goalCenter.y - state.ball.pos.y,
                0.0f  // ignore Z for 2D angle
            ));

            const Vec ballDir = SafeNorm(Vec(state.ball.vel.x, state.ball.vel.y, 0.0f));

            // Dot product gives cos(angle)
            float dotProd = toGoal.x * ballDir.x + toGoal.y * ballDir.y;
            dotProd = std::max(-1.0f, std::min(1.0f, dotProd));  // clamp for acos

            return acosf(dotProd);
        }

        // Check if player is closer to ball than all opponents
        inline bool IsClosestToBall(const Player& player, const GameState& state) {
            const float myDist = (player.pos - state.ball.pos).Length();

            for (const Player& other : state.players) {
                if (other.carId == player.carId) continue;  // skip self
                if (other.team == player.team) continue;    // skip teammates

                const float otherDist = (other.pos - state.ball.pos).Length();
                if (otherDist < myDist) return false;
            }

            return true;
        }


    } // namespace RewardMath

} // namespace RLGC