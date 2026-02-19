#pragma once

#include "Reward.h"
#include "../Math.h"
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

            if (boostGained > 90.0f) {
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

            lastBoostAmounts[player.carId] = player.boost;
            return reward;
        }
    };

} // namespace RLGC
