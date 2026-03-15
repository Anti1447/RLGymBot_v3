#pragma once

#include "Reward.h"
#include "../Math.h"
#include "../CommonValues.h"
#include <map>
#include <vector>

namespace RLGC {

    // ============================================================================
    // BOOST STEAL REWARD
    // Rewards picking up big boost pads on opponent's side.
    // Bonus for corner pads and desperation steals (low own boost).
    // ============================================================================
    class BoostStealReward : public Reward {
    private:
        const std::vector<Vec> bigBoostPads = {
            Vec(-3072.0f, -4096.0f, 73.0f),
            Vec(3072.0f, -4096.0f, 73.0f),
            Vec(-3584.0f, 0.0f, 73.0f),
            Vec(3584.0f, 0.0f, 73.0f),
            Vec(-3072.0f, 4096.0f, 73.0f),
            Vec(3072.0f, 4096.0f, 73.0f)
        };

        mutable std::map<uint32_t, float> lastBoostAmounts;

        // ====== SMALL BOOST PAD HELPERS ======
        // Small pads: indices 0-14 (BLUE side), 15-18 (middle), 19-33 (ORANGE side)
        // Large pads: indices {3, 4, 15, 18, 29, 30}

        static constexpr int LARGE_BOOST_INDICES[] = { 3, 4, 15, 18, 29, 30 };
        static constexpr int NUM_LARGE_BOOSTS = 6;

        bool IsSmallBoost(int boostIndex) const {
            for (int i = 0; i < NUM_LARGE_BOOSTS; i++) {
                if (LARGE_BOOST_INDICES[i] == boostIndex) {
                    return false;
                }
            }
            return true;
        }

        bool IsOpponentPad(int boostIndex, Team team) const {
            // Indices 0-14: BLUE side
            // Indices 15-18: Middle (neutral, don't reward)
            // Indices 19-33: ORANGE side
            if (team == Team::BLUE) {
                return boostIndex >= 19 && boostIndex <= 33;  // ORANGE side is opponent
            }
            else {
                return boostIndex >= 0 && boostIndex <= 14;   // BLUE side is opponent
            }
        }

        bool IsOpposingSide(const Vec& padPos, Team team) const {
            if (team == Team::BLUE) {
                return padPos.y > 1000.0f;
            }
            else {
                return padPos.y < -1000.0f;
            }
        }

    public:
        virtual void Reset(const GameState& initialState) override {
            lastBoostAmounts.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float reward = 0.0f;

            if (lastBoostAmounts.find(player.carId) == lastBoostAmounts.end()) {
                lastBoostAmounts[player.carId] = player.boost;
                return 0.0f;
            }

            float boostGained = player.boost - lastBoostAmounts[player.carId];

            // Detect big boost pad pickup (100 boost pads give >40 boost)
            if (boostGained > 40.0f) {
                for (const auto& pad : bigBoostPads) {
                    float dist = pad.Dist(player.pos);

                    if (dist < 200.0f) {
                        if (IsOpposingSide(pad, player.team)) {
                            reward += 0.1f;

                            bool cornerPad = abs(pad.y) > 3500.0f && abs(pad.x) > 2500.0f;
                            if (cornerPad) {
                                reward += 0.08f;
                            }

                            float desperation = 1.0f - (lastBoostAmounts[player.carId] / 100.0f);
                            reward += desperation * 0.05f;
                        }
                        break;
                    }
                }
            }
            // Detect small boost pad pickup (small pads give 12 boost, detect 10-20 range)
            else if (boostGained >= 10.0f && boostGained <= 20.0f) {
                // Find which small pad was picked up
                for (int i = 0; i < CommonValues::BOOST_LOCATIONS_AMOUNT; i++) {
                    if (!IsSmallBoost(i)) {
                        continue;  // Skip large pads
                    }

                    const Vec& pad = CommonValues::BOOST_LOCATIONS[i];
                    float dist = pad.Dist(player.pos);

                    if (dist < 200.0f) {  // Close enough to have picked it up
                        // Check if this pad is on opponent's side
                        if (IsOpponentPad(i, player.team)) {
                            // Reward for stealing small pad from opponent half
                            reward += 0.03f;
                        }
                        break;
                    }
                }
            }

            lastBoostAmounts[player.carId] = player.boost;
            return reward;
        }
    };

} // namespace RLGC
