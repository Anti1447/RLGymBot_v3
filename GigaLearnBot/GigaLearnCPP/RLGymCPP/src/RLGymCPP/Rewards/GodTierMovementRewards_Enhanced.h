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
// ENHANCED SPEED FLIP REWARD
// "Flip forward slightly to the side, then drag stick down to bottom"
// ============================================================================

class EnhancedSpeedFlipReward : public Reward {
private:
    struct FlipData {
        Vec initialForward;
        Vec initialVelocity;
        Vec flipDirection;
        float startSpeed;
        float startTime;
        float jumpStartTime;              // Fix 6: when car left ground
        bool cancelDetected;
        bool diagonalFlipDetected;
        bool airRollUsedInCancelWindow;   // Fix 4: only early roll counts
    };

    mutable std::map<uint32_t, FlipData> flipData;
    mutable std::map<uint32_t, bool> isFlipping;
    mutable std::map<uint32_t, bool> prevOnGround;   // Fix 6
    mutable std::map<uint32_t, float> jumpStartTimes; // Fix 6
    mutable float currentTime;

public:
    virtual void Reset(const GameState& initialState) override {
        flipData.clear();
        isFlipping.clear();
        prevOnGround.clear();
        jumpStartTimes.clear();
        currentTime = 0.0f;
    }

    // Fix 2: accumulate time once per frame, not once per player
    virtual void PreStep(const GameState& state) override {
        currentTime += (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float reward = 0.0f;

        // Fix 6: detect jump (ground → air transition) to time the full sequence
        bool onGround = player.isOnGround;
        bool& wasOnGround = prevOnGround[player.carId];
        if (wasOnGround && !onGround) {
            jumpStartTimes[player.carId] = currentTime;
        }
        wasOnGround = onGround;

        bool wasFlipping = isFlipping[player.carId];
        isFlipping[player.carId] = player.isFlipping;

        // ===== FLIP START DETECTION =====
        if (!wasFlipping && player.isFlipping) {
            FlipData data;
            data.initialForward = player.rotMat.forward;
            data.initialVelocity = player.vel;
            data.startSpeed = player.vel.Length();
            data.startTime = currentTime;
            // Capture jump start time; fall back to now if car was already airborne
            data.jumpStartTime = jumpStartTimes.count(player.carId)
                                     ? jumpStartTimes[player.carId]
                                     : currentTime;
            data.cancelDetected = false;
            data.diagonalFlipDetected = false;
            data.airRollUsedInCancelWindow = false;

            float pitch = player.prevAction.pitch;
            float yaw = player.prevAction.yaw;

            // Speed flip = forward flip with slight side component
            if (pitch > 0.5f && abs(yaw) > 0.2f && abs(yaw) < 0.8f) {
                data.diagonalFlipDetected = true;
                data.flipDirection = Vec(yaw, pitch, 0).Normalized();
            }

            flipData[player.carId] = data;
            return 0.0f;
        }

        // ===== DURING FLIP - DETECT CANCEL =====
        if (player.isFlipping && wasFlipping) {
            auto& data = flipData[player.carId];

            // Fix 4: air roll only counts within the cancel window (~first 0.4s of flip)
            float timeSinceFlipStart = currentTime - data.startTime;
            if (timeSinceFlipStart < 0.4f && abs(player.prevAction.roll) > 0.3f) {
                data.airRollUsedInCancelWindow = true;
            }

            // Detect cancel: drag stick down to strongly negative pitch
            if (player.prevAction.pitch < -0.7f) {
                data.cancelDetected = true;
            }
        }

        // ===== FLIP END - EVALUATE SPEED FLIP =====
        if (wasFlipping && !player.isFlipping) {
            const FlipData& data = flipData[player.carId];

            // Fix 6: hard gate — full jump-to-flip-end sequence must be ≤ 1.5s
            float totalSequenceDuration = currentTime - data.jumpStartTime;
            if (totalSequenceDuration > 1.5f) {
                return 0.0f;
            }

            float endSpeed = player.vel.Length();
            float flipDuration = currentTime - data.startTime;

            // Fix 3: require forward-projected speed gain, not raw scalar gain
            Vec initialFwdNorm = data.initialForward.Normalized();
            float forwardSpeedBefore = data.initialVelocity.Dot(initialFwdNorm);
            float forwardSpeedAfter = player.vel.Dot(initialFwdNorm);
            float forwardSpeedGain = forwardSpeedAfter - forwardSpeedBefore;

            // === SPEED FLIP CRITERIA ===
            bool criteriaA = data.diagonalFlipDetected;
            bool criteriaB = data.cancelDetected;
            bool criteriaC = data.airRollUsedInCancelWindow;  // Fix 4
            bool criteriaD = forwardSpeedGain > 300.0f;       // Fix 3
            bool criteriaE = flipDuration < 0.6f;             // tightened from 1.0s
            bool criteriaF = endSpeed > 2000.0f;              // Fix 5: raised from 1800

            int criteriaMet =
                (criteriaA ? 1 : 0) +
                (criteriaB ? 1 : 0) +
                (criteriaC ? 1 : 0) +
                (criteriaD ? 1 : 0) +
                (criteriaE ? 1 : 0) +
                (criteriaF ? 1 : 0);

            if (criteriaMet >= 5) {
                // PERFECT SPEED FLIP
                reward += 15.0f;

                if (criteriaMet == 6) {
                    reward += 5.0f;
                }

                bool isKickoff = state.ball.pos.Length() < 200.0f &&
                                 data.initialVelocity.Length() < 1000.0f &&
                                 (player.pos - state.ball.pos).Length() > 3000.0f;
                if (isKickoff) {
                    reward += 10.0f;
                }

                if (endSpeed > 2200.0f) {
                    reward += 5.0f;
                }

            } else {
                // Fix 1: removed criteriaMet >= 3 partial tier — it was the exploit
                // Penalize poorly executed attempts to discourage the half-flip pattern
                if (data.diagonalFlipDetected || data.cancelDetected) {
                    reward -= 1.0f;
                }
            }
        }

        return reward;
    }
};

// ============================================================================
// ENHANCED ZAP DASH REWARD
// "Land front wheels, immediately jump when front wheels planted"
// ============================================================================

class EnhancedZapDashReward : public Reward {
private:
    struct ZapDashData {
        float landingTime;
        float landingSpeed;
        Vec landingVelocity;
        bool frontWheelsFirst;
        float frontWheelPitch;
    };

    mutable std::map<uint32_t, ZapDashData> zapDashData;
    mutable std::map<uint32_t, bool> wasAirborne;
    mutable float currentTime;

public:
    virtual void Reset(const GameState& initialState) override {
        zapDashData.clear();
        wasAirborne.clear();
        currentTime = 0.0f;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        currentTime += state.deltaTime;
        float reward = 0.0f;

        bool wasInAir = wasAirborne[player.carId];
        bool inAir = !player.isOnGround;
        wasAirborne[player.carId] = inAir;

        // ===== LANDING MOMENT =====
        if (wasInAir && !inAir) {
            ZapDashData data;
            data.landingTime = currentTime;
            data.landingSpeed = player.vel.Length();
            data.landingVelocity = player.vel;

            // Check if front wheels landed first
            // This is indicated by nose-down pitch
            Vec forward = player.rotMat.forward;
            data.frontWheelPitch = forward.z; // Negative = nose down

            if (data.frontWheelPitch < -0.4f) {
                data.frontWheelsFirst = true;
            }
            else {
                data.frontWheelsFirst = false;
            }

            zapDashData[player.carId] = data;
        }

        // ===== JUMP AFTER LANDING =====
        if (!wasInAir && inAir) {
            const ZapDashData& data = zapDashData[player.carId];
            float timeSinceLanding = currentTime - data.landingTime;

            // === ZAP DASH CRITERIA (from video) ===
            // 1. Land on front wheels (nose down)
            // 2. Jump IMMEDIATELY when front wheels planted (<0.15s)
            // 3. NO flip used (distinguishes from wave dash)
            // 4. Speed gained

            bool criteriaA = data.frontWheelsFirst;
            bool criteriaB = timeSinceLanding < 0.15f; // Very quick!
            bool criteriaC = !player.isFlipping;

            float currentSpeed = player.vel.Length();
            float speedGain = currentSpeed - data.landingSpeed;
            bool criteriaD = speedGain > 50.0f;

            int criteriaMet =
                (criteriaA ? 1 : 0) +
                (criteriaB ? 1 : 0) +
                (criteriaC ? 1 : 0) +
                (criteriaD ? 1 : 0);

            if (criteriaMet >= 3) {
                // SUCCESSFUL ZAP DASH
                reward += 6.0f;

                // Perfect technique bonus
                if (criteriaMet == 4) {
                    reward += 3.0f;
                }

                // Extra bonus for very nose-down landing
                if (data.frontWheelPitch < -0.6f) {
                    reward += 2.0f; // Perfect front wheel absorption
                }

                // Speed gain scaling
                reward += RS_MIN(speedGain / 50.0f, 3.0f);

                // Timing bonus
                if (timeSinceLanding < 0.08f) {
                    reward += 2.0f; // Lightning reflexes!
                }

                // Supersonic maintenance
                if (currentSpeed > 2200.0f) {
                    reward += 2.0f;
                }

            }
            else if (criteriaB && !criteriaA) {
                // Quick jump but wrong technique
                reward -= 0.5f; // Teach proper front-wheel landing
            }
        }

        return reward;
    }
};


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
