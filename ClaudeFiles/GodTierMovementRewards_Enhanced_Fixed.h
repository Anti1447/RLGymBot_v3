// GodTierMovementRewards_Enhanced_Fixed.h
// FIXED VERSION - Enum scoping issues resolved
// Place in: GigaLearnCPP/GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/

#pragma once
#include "Reward.h"
#include "../Math.h"
#include <map>
#include <deque>
#include <vector>

namespace RLGC {

// ============================================================================
// ENHANCED MENA FLIP REWARD - FIXED
// ============================================================================

class EnhancedMenaFlipReward : public Reward {
private:
    // FIXED: Declare enum BEFORE struct that uses it
    enum class Side { NONE, LEFT, RIGHT };
    
    struct MenaData {
        Vec preFlipPosition;
        Vec preFlipVelocity;
        Vec wheelDirection;
        float preFlipSpeed;
        float startTime;
        bool wheelsAwayFromWall;
        bool speedFlipPerformed;
        bool correctAirRollUsed;
        Side wallSide;  // FIXED: Now Side is defined before this line
    };
    
    mutable std::map<uint32_t, MenaData> menaData;
    mutable std::map<uint32_t, bool> isFlipping;
    mutable float currentTime;
    
    Side DetectWallSide(const Vec& pos) const {
        float distToRightWall = abs(pos.x - CommonValues::SIDE_WALL_X);
        float distToLeftWall = abs(pos.x + CommonValues::SIDE_WALL_X);
        
        if (distToRightWall < 800.0f) return Side::RIGHT;
        if (distToLeftWall < 800.0f) return Side::LEFT;
        return Side::NONE;
    }

public:
    virtual void Reset(const GameState& initialState) override {
        menaData.clear();
        isFlipping.clear();
        currentTime = 0.0f;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        currentTime += state.deltaTime;
        float reward = 0.0f;
        
        bool wasFlipping = isFlipping[player.carId];
        isFlipping[player.carId] = player.isFlipping;
        
        Side wallSide = DetectWallSide(player.pos);
        
        // Approaching wall
        if (wallSide != Side::NONE && player.isOnGround) {
            auto& data = menaData[player.carId];
            data.preFlipPosition = player.pos;
            data.preFlipVelocity = player.vel;
            data.preFlipSpeed = player.vel.Length();
            data.wallSide = wallSide;
            
            // Check if wheels turned AWAY from wall
            float yaw = player.prevAction.yaw;
            
            if (wallSide == Side::RIGHT && yaw < -0.3f) {
                data.wheelsAwayFromWall = true;
            } else if (wallSide == Side::LEFT && yaw > 0.3f) {
                data.wheelsAwayFromWall = true;
            } else {
                data.wheelsAwayFromWall = false;
            }
        }
        
        // Flip start
        if (!wasFlipping && player.isFlipping && wallSide != Side::NONE) {
            auto& data = menaData[player.carId];
            data.startTime = currentTime;
            
            float pitch = player.prevAction.pitch;
            float yaw = player.prevAction.yaw;
            
            if (pitch > 0.5f && abs(yaw) > 0.2f) {
                data.speedFlipPerformed = true;
            }
        }
        
        // During flip
        if (player.isFlipping && wasFlipping) {
            auto& data = menaData[player.carId];
            
            float roll = player.prevAction.roll;
            
            if (data.wallSide == Side::RIGHT && roll > 0.5f) {
                data.correctAirRollUsed = true;
            } else if (data.wallSide == Side::LEFT && roll < -0.5f) {
                data.correctAirRollUsed = true;
            }
        }
        
        // After flip - on wall
        if (wasFlipping && !player.isFlipping) {
            const MenaData& data = menaData[player.carId];
            
            float distToWallX = RS_MIN(
                abs(player.pos.x - CommonValues::SIDE_WALL_X),
                abs(player.pos.x + CommonValues::SIDE_WALL_X)
            );
            bool onWall = distToWallX < 150.0f && player.pos.z > 100.0f;
            
            float endSpeed = player.vel.Length();
            float speedGain = endSpeed - data.preFlipSpeed;
            
            bool criteriaA = data.wheelsAwayFromWall;
            bool criteriaB = data.speedFlipPerformed;
            bool criteriaC = data.correctAirRollUsed;
            bool criteriaD = onWall;
            bool criteriaE = speedGain > 200.0f;
            
            int criteriaMet = 
                (criteriaA ? 1 : 0) +
                (criteriaB ? 1 : 0) +
                (criteriaC ? 1 : 0) +
                (criteriaD ? 1 : 0) +
                (criteriaE ? 1 : 0);
            
            if (criteriaMet >= 4) {
                reward += 12.0f;
                
                if (criteriaMet == 5) {
                    reward += 6.0f;
                }
                
                reward += RS_MIN((speedGain - 200.0f) / 100.0f, 5.0f);
                
                if (endSpeed > 2200.0f) {
                    reward += 4.0f;
                }
            } else if (criteriaB || criteriaC) {
                reward -= 2.0f;
            }
        }
        
        return reward;
    }
};

// ============================================================================
// ENHANCED WALL DASH REWARD - FIXED
// ============================================================================

class EnhancedWallDashReward : public Reward {
private:
    // FIXED: Declare enum BEFORE struct that uses it
    enum class Side { NONE, LEFT, RIGHT, FRONT, BACK };
    
    struct WallDashData {
        float firstJumpTime;
        float secondJumpTime;
        Vec positionAtFirstJump;
        float speedAtFirstJump;
        Side wallSide;  // FIXED: Now Side is defined before this line
        bool correctDirection;
    };
    
    mutable std::map<uint32_t, WallDashData> wallDashData;
    mutable std::map<uint32_t, int> jumpCount;
    mutable float currentTime;
    
    Side DetectWallSide(const Vec& pos) const {
        float distToRightWall = abs(pos.x - CommonValues::SIDE_WALL_X);
        float distToLeftWall = abs(pos.x + CommonValues::SIDE_WALL_X);
        float distToFrontWall = abs(pos.y - CommonValues::BACK_WALL_Y);
        float distToBackWall = abs(pos.y + CommonValues::BACK_WALL_Y);
        
        float minDist = RS_MIN(RS_MIN(distToRightWall, distToLeftWall),
                              RS_MIN(distToFrontWall, distToBackWall));
        
        if (minDist > 300.0f) return Side::NONE;
        
        if (minDist == distToRightWall) return Side::RIGHT;
        if (minDist == distToLeftWall) return Side::LEFT;
        if (minDist == distToFrontWall) return Side::FRONT;
        return Side::BACK;
    }

public:
    virtual void Reset(const GameState& initialState) override {
        wallDashData.clear();
        jumpCount.clear();
        currentTime = 0.0f;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        currentTime += state.deltaTime;
        float reward = 0.0f;
        
        Side wallSide = DetectWallSide(player.pos);
        
        if (wallSide == Side::NONE || player.pos.z < 50.0f) {
            jumpCount[player.carId] = 0;
            return 0.0f;
        }
        
        bool jumped = false;
        if (state.prev && !state.prev->players[player.index].isOnGround && player.isOnGround) {
            // Just landed - doesn't count
        } else if (state.prev) {
            float prevZVel = state.prev->players[player.index].vel.z;
            float curZVel = player.vel.z;
            
            if (curZVel > prevZVel + 150.0f) {
                jumped = true;
            }
        }
        
        if (!jumped) return 0.0f;
        
        // First jump
        if (jumpCount[player.carId] == 0) {
            WallDashData data;
            data.firstJumpTime = currentTime;
            data.positionAtFirstJump = player.pos;
            data.speedAtFirstJump = player.vel.Length();
            data.wallSide = wallSide;
            
            float yaw = player.prevAction.yaw;
            
            if ((wallSide == Side::RIGHT && yaw > 0.3f) ||
                (wallSide == Side::LEFT && yaw < -0.3f) ||
                (wallSide == Side::FRONT && player.prevAction.pitch > 0.3f) ||
                (wallSide == Side::BACK && player.prevAction.pitch < -0.3f)) {
                data.correctDirection = true;
            } else {
                data.correctDirection = false;
            }
            
            wallDashData[player.carId] = data;
            jumpCount[player.carId] = 1;
            
        // Second jump
        } else if (jumpCount[player.carId] == 1) {
            const WallDashData& data = wallDashData[player.carId];
            float timeBetweenJumps = currentTime - data.firstJumpTime;
            
            bool criteriaA = timeBetweenJumps < 0.25f;
            bool criteriaB = data.correctDirection;
            bool criteriaC = wallSide == data.wallSide;
            
            float endSpeed = player.vel.Length();
            float speedGain = endSpeed - data.speedAtFirstJump;
            bool criteriaD = speedGain > 80.0f;
            
            if (criteriaA && criteriaB && criteriaC && criteriaD) {
                reward += 8.0f;
                
                if (timeBetweenJumps < 0.15f) {
                    reward += 4.0f;
                }
                
                reward += RS_MIN(speedGain / 80.0f, 4.0f);
                
                if (state.prev) {
                    float prevBoost = state.prev->players[player.index].boost;
                    float boostUsed = prevBoost - player.boost;
                    if (boostUsed == 0.0f) {
                        reward += 3.0f;
                    }
                }
            } else if (criteriaA) {
                reward -= 1.0f;
            }
            
            jumpCount[player.carId] = 0;
        }
        
        return reward;
    }
};

} // namespace RLGC
