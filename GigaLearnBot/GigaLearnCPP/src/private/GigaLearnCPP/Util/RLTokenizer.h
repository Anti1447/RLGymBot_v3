#pragma once
#include <torch/torch.h>
#include <vector>

namespace GGL {

    // ============================================================================
    // ROCKET LEAGUE TOKENIZER - Convert flat obs to tokens
    // ============================================================================
    // Converts flat observation vectors into structured tokens for MLP-Mixer.
    //
    // Tokenization Strategy:
    //   Token 0: Ball (3D position + 3D velocity) = 6 features
    //   Token 1: Own car (3D pos + 3D vel + 4D rotation quat + boost + airtime) = 12 features
    //   Tokens 2+: Each other player/teammate (same as own car) = 12 features each
    //   Token N: Boost pads (optional - can be aggregated or individual)
    //
    // Observation Indexing (typical AdvancedObs format):
    //   [0-2]:   Ball position (x, y, z)
    //   [3-5]:   Ball velocity (vx, vy, vz)
    //   [6-8]:   Own car position
    //   [9-11]:  Own car velocity
    //   [12-15]: Own car rotation (quaternion)
    //   [16]:    Own boost amount
    //   [17]:    Own airtime
    //   [18+]:   Other players (position, velocity, rotation, boost, airtime)
    //   [...]:   Boost pads (if included)
    //
    // Output:
    //   Tensor of shape [batch_size, num_tokens, features_per_token]
    //   where features_per_token is typically 6 (ball) or 12 (cars)
    //
    // Rev 1.0 (2025-01-16, NJH)

    struct TokenizerConfig {
        int obsSize = 0;              // Total observation size from obs builder
        int numPlayers = 0;           // Number of players (including self)
        bool includeBoostPads = false;    // Whether to tokenize boost pads
        int maxBoostPads = 0;         // Max boost pads to include (if enabled)
        int featuresPerToken = 12;     // Dimensionality of each token (typically 12 for cars, 6 for ball)
    };

    class RLTokenizer {
    public:
        RLTokenizer(const TokenizerConfig& cfg)
            : _config(cfg),
              ballTokenDim(6),      // ball: pos (3) + vel (3)
              carTokenDim(12),      // car: pos (3) + vel (3) + quat (4) + boost + airtime
              numTokens(1 + cfg.numPlayers) {  // 1 ball + N cars
        }

        // Convert flat observation to tokens
        // Input:  [batch_size, obsSize]
        // Output: [batch_size, num_tokens, token_dim]
        // Token 0: Ball (6 features, padded to 12)
        // Tokens 1+: Players (12 features each)
        torch::Tensor tokenize(torch::Tensor flatObs) {
            int batchSize = flatObs.size(0);
            auto device = flatObs.device();

            // Allocate token tensor [batch, num_tokens, 12]
            auto tokens = torch::zeros({batchSize, numTokens, carTokenDim},
                                      torch::TensorOptions(device).dtype(torch::kFloat32));

            // Token 0: Ball [pos(3), vel(3), pad(6)]
            // Use index with Slice for proper torch indexing
            tokens.index_put_({torch::indexing::Slice(), 0, torch::indexing::Slice(0, 3)},
                             flatObs.index({torch::indexing::Slice(), torch::indexing::Slice(0, 3)}));
            tokens.index_put_({torch::indexing::Slice(), 0, torch::indexing::Slice(3, 6)},
                             flatObs.index({torch::indexing::Slice(), torch::indexing::Slice(3, 6)}));

            // Tokens 1+: Players [pos(3), vel(3), quat(4), boost(1), airtime(1)]
            int offset = 6;  // Start after ball data
            for (int i = 0; i < _config.numPlayers && offset < _config.obsSize; i++) {
                int tokenIdx = 1 + i;

                // Position (3)
                if (offset + 3 <= _config.obsSize) {
                    tokens.index_put_({torch::indexing::Slice(), tokenIdx, torch::indexing::Slice(0, 3)},
                                     flatObs.index({torch::indexing::Slice(), torch::indexing::Slice(offset, offset + 3)}));
                    offset += 3;
                }

                // Velocity (3)
                if (offset + 3 <= _config.obsSize) {
                    tokens.index_put_({torch::indexing::Slice(), tokenIdx, torch::indexing::Slice(3, 6)},
                                     flatObs.index({torch::indexing::Slice(), torch::indexing::Slice(offset, offset + 3)}));
                    offset += 3;
                }

                // Rotation quaternion (4)
                if (offset + 4 <= _config.obsSize) {
                    tokens.index_put_({torch::indexing::Slice(), tokenIdx, torch::indexing::Slice(6, 10)},
                                     flatObs.index({torch::indexing::Slice(), torch::indexing::Slice(offset, offset + 4)}));
                    offset += 4;
                }

                // Boost amount (1)
                if (offset + 1 <= _config.obsSize) {
                    tokens.index_put_({torch::indexing::Slice(), tokenIdx, torch::indexing::Slice(10, 11)},
                                     flatObs.index({torch::indexing::Slice(), torch::indexing::Slice(offset, offset + 1)}));
                    offset += 1;
                }

                // Airtime (1)
                if (offset + 1 <= _config.obsSize) {
                    tokens.index_put_({torch::indexing::Slice(), tokenIdx, torch::indexing::Slice(11, 12)},
                                     flatObs.index({torch::indexing::Slice(), torch::indexing::Slice(offset, offset + 1)}));
                    offset += 1;
                }
            }

            return tokens;
        }

        // Reverse operation: convert tokens back to flat observation
        torch::Tensor untokenize(torch::Tensor tokens) {
            int batchSize = tokens.size(0);
            auto device = tokens.device();

            auto flatObs = torch::zeros({batchSize, _config.obsSize},
                                       torch::TensorOptions(device).dtype(torch::kFloat32));

            int offset = 0;

            // Ball token (0)
            flatObs.index_put_({torch::indexing::Slice(), torch::indexing::Slice(offset, offset + 3)},
                              tokens.index({torch::indexing::Slice(), 0, torch::indexing::Slice(0, 3)}));
            offset += 3;
            flatObs.index_put_({torch::indexing::Slice(), torch::indexing::Slice(offset, offset + 3)},
                              tokens.index({torch::indexing::Slice(), 0, torch::indexing::Slice(3, 6)}));
            offset += 3;

            // Player tokens (1+)
            for (int i = 0; i < _config.numPlayers && offset < _config.obsSize; i++) {
                int tokenIdx = 1 + i;

                if (offset + 3 <= _config.obsSize) {
                    flatObs.index_put_({torch::indexing::Slice(), torch::indexing::Slice(offset, offset + 3)},
                                      tokens.index({torch::indexing::Slice(), tokenIdx, torch::indexing::Slice(0, 3)}));
                    offset += 3;
                }
                if (offset + 3 <= _config.obsSize) {
                    flatObs.index_put_({torch::indexing::Slice(), torch::indexing::Slice(offset, offset + 3)},
                                      tokens.index({torch::indexing::Slice(), tokenIdx, torch::indexing::Slice(3, 6)}));
                    offset += 3;
                }
                if (offset + 4 <= _config.obsSize) {
                    flatObs.index_put_({torch::indexing::Slice(), torch::indexing::Slice(offset, offset + 4)},
                                      tokens.index({torch::indexing::Slice(), tokenIdx, torch::indexing::Slice(6, 10)}));
                    offset += 4;
                }
                if (offset + 1 <= _config.obsSize) {
                    flatObs.index_put_({torch::indexing::Slice(), torch::indexing::Slice(offset, offset + 1)},
                                      tokens.index({torch::indexing::Slice(), tokenIdx, torch::indexing::Slice(10, 11)}));
                    offset += 1;
                }
                if (offset + 1 <= _config.obsSize) {
                    flatObs.index_put_({torch::indexing::Slice(), torch::indexing::Slice(offset, offset + 1)},
                                      tokens.index({torch::indexing::Slice(), tokenIdx, torch::indexing::Slice(11, 12)}));
                    offset += 1;
                }
            }

            return flatObs;
        }

        int getNumTokens() const { return numTokens; }
        int getTokenDim() const { return carTokenDim; }
        int getBallTokenDim() const { return ballTokenDim; }
        const TokenizerConfig& config() const { return _config; }

    private:
        TokenizerConfig _config;
        int ballTokenDim;
        int carTokenDim;
        int numTokens;
    };

} // namespace GGL
