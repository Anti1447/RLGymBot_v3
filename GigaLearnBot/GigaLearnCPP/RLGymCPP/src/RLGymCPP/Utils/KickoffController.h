#pragma once

#include <vector>
#include <array>
#include <cmath>
#include "../Framework.h"

namespace RLGC {

    enum class KickoffMode {
        HARDCODED,
        STOCHASTIC
    };

    class KickoffController {
    private:
        std::vector<std::array<float, 8>> kickoff_sequence;
        KickoffMode mode;
        // throttle, steer, pitch, yaw, roll, jump, boost, handbrake
        void build_kickoff_sequence() {
            // 11 * 4 frames of throttle + boost
            for (int i = 0; i < 5 * 1; i++) {
                kickoff_sequence.push_back({ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f });
            }
            // 4 * 4 frames of throttle + boost + steer/yaw left
            for (int i = 0; i < 2 * 1; i++) {
                kickoff_sequence.push_back({ 1.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f });
            }
            // 2 * 4 frames of throttle + jump + boost
            for (int i = 0; i < 2 * 1; i++) {
                kickoff_sequence.push_back({ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f });
            }
            // 1 * 4 frames of throttle + boost
            for (int i = 0; i < 1 * 1; i++) {
                kickoff_sequence.push_back({ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f });
            }
            // 1 * 4 frames of complex rotation
            for (int i = 0; i < 1 * 1; i++) {
                kickoff_sequence.push_back({ 1.0f, 0.8f, -0.7f, 0.8f, 0.0f, 1.0f, 1.0f, 0.0f });
            }
            // 13 * 4 frames of throttle + pitch + boost
            for (int i = 0; i < 6 * 1; i++) {
                kickoff_sequence.push_back({ 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f });
            }
            // 10 * 4 frames of throttle + roll + pitch
            for (int i = 0; i < 2 * 1; i++) {
                kickoff_sequence.push_back({ 1.0f, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f });
            }
        }

    public:
        KickoffController(KickoffMode mode = KickoffMode::HARDCODED) : mode(mode) {
            build_kickoff_sequence();
            //std::cout << "[KICKOFF] Mode: " << (mode == KickoffMode::HARDCODED ? "HARDCODED" : "STOCHASTIC") << std::endl;
            //std::cout << "[KICKOFF] Sequence length: " << kickoff_sequence.size() << std::endl;
        }

        KickoffMode GetMode() const {
            return mode;
        }

        bool should_begin_kickoff(const GameState& gs) {
            bool result = std::abs(gs.ball.pos.x) < 50.0f &&
                std::abs(gs.ball.pos.y) < 50.0f &&
                gs.ball.pos.z < 100.0f;

            /*if (result) {
                std::cout << "[KICKOFF] Ball pos: (" << gs.ball.pos.x << ", "
                    << gs.ball.pos.y << ", " << gs.ball.pos.z << ")" << std::endl;
            }*/

            return result;
        }

        bool is_player_kickoff_taker(const Player& player, const GameState& gs) {
            float min_dist = 1e9f;
            int closest_idx = -1;

            // Find closest player to ball
            for (int i = 0; i < gs.players.size(); i++) {
                float dx = gs.players[i].pos.x - gs.ball.pos.x;  // Changed from .car.pos
                float dy = gs.players[i].pos.y - gs.ball.pos.y;  // Changed from .car.pos
                float dist = std::sqrt(dx * dx + dy * dy);

                if (dist < min_dist) {
                    min_dist = dist;
                    closest_idx = i;
                }
            }

            // Check if this player is the closest one
            if (closest_idx >= 0 && closest_idx == player.index) {
                return true;
            }

            return false;
        }

        std::array<float, 8> get_raw_action_array(int index) {
            if (index < 0 || index >= static_cast<int>(kickoff_sequence.size())) {
                return {};
            }
            return kickoff_sequence[index];
        }

        Action get_kickoff_action(int index, Team team) {
            if (index < 0 || index >= static_cast<int>(kickoff_sequence.size())) {
                return Action();
            }

            auto action_arr = kickoff_sequence[index];

            // Mirror for team 1 (Orange)
            if (team == Team::ORANGE) {
                action_arr[1] = -action_arr[1];  // steer
                action_arr[3] = -action_arr[3];  // yaw
                action_arr[4] = -action_arr[4];  // roll
            }

            Action action;
            action.throttle = action_arr[0];
            action.steer = action_arr[1];
            action.pitch = action_arr[2];
            action.yaw = action_arr[3];
            action.roll = action_arr[4];
            action.jump = action_arr[5] > 0.5f;
            action.boost = action_arr[6] > 0.5f;
            action.handbrake = action_arr[7] > 0.5f;

            return action;
        }

        int get_sequence_length() const {
            return static_cast<int>(kickoff_sequence.size());
        }
    };

} // namespace RLGC