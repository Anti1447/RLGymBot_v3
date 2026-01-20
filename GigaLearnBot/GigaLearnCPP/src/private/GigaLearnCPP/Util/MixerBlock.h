#pragma once
#include <torch/torch.h>

namespace GGL {

    // ============================================================================
    // MLP-MIXER BLOCK - Token and Channel Mixing
    // ============================================================================
    // MLP-Mixer architecture adapted for Rocket League observations.
    //
    // Structure:
    //   Input: [batch_size, num_tokens, feature_dim]
    //   ↓
    //   LayerNorm + Token-Mixing MLP (mixes across tokens/objects)
    //   ↓
    //   LayerNorm + Channel-Mixing MLP (mixes across features)
    //   ↓
    //   Output: [batch_size, num_tokens, feature_dim]
    //
    // For Rocket League tokenization:
    //   - Ball (position, velocity, rotation): 1 token
    //   - Own car (position, velocity, rotation, controls, etc.): 1 token
    //   - Teammates: N tokens (one per teammate)
    //   - Opponents: N tokens (one per opponent)
    //   - Boost pads: M tokens (or aggregated)
    //
    // Total tokens typically 4-12 depending on game mode (1v1, 2v2, 3v3)
    //
    // Rev 1.0 (2025-01-16, NJH)

    struct MixerBlockOptions {
        MixerBlockOptions(int64_t numTokens, int64_t hiddenDim, int64_t mlpDim = 0)
            : num_tokens_(numTokens), hidden_dim_(hiddenDim), mlp_dim_(mlpDim == 0 ? hiddenDim * 4 : mlpDim) {}

        TORCH_ARG(int64_t, num_tokens);    // Number of tokens in sequence
        TORCH_ARG(int64_t, hidden_dim);    // Feature dimension per token
        TORCH_ARG(int64_t, mlp_dim);       // Hidden dimension in MLPs
    };

    class MixerBlockImpl : public torch::nn::Module {
    public:
        MixerBlockImpl(int64_t numTokens, int64_t hiddenDim, int64_t mlpDim = 0)
            : norm1(torch::nn::LayerNormOptions({hiddenDim})),
              norm2(torch::nn::LayerNormOptions({hiddenDim})),
              // Token-mixing MLP: [batch, hidden_dim, num_tokens] → [batch, hidden_dim, num_tokens]
              tokenMixing1(torch::nn::LinearOptions(numTokens, mlpDim == 0 ? numTokens * 4 : mlpDim)),
              tokenMixing2(torch::nn::LinearOptions(mlpDim == 0 ? numTokens * 4 : mlpDim, numTokens)),
              // Channel-mixing MLP: [batch, num_tokens, hidden_dim] → [batch, num_tokens, hidden_dim]
              channelMixing1(torch::nn::LinearOptions(hiddenDim, mlpDim == 0 ? hiddenDim * 4 : mlpDim)),
              channelMixing2(torch::nn::LinearOptions(mlpDim == 0 ? hiddenDim * 4 : mlpDim, hiddenDim)),
              numTokens(numTokens),
              hiddenDim(hiddenDim) {
            register_module("norm1", norm1);
            register_module("norm2", norm2);
            register_module("tokenMixing1", tokenMixing1);
            register_module("tokenMixing2", tokenMixing2);
            register_module("channelMixing1", channelMixing1);
            register_module("channelMixing2", channelMixing2);
        }

        torch::Tensor forward(torch::Tensor x) {
            // x shape: [batch_size, num_tokens, hidden_dim]

            // Token mixing with residual connection
            // Transpose: [batch, hidden_dim, num_tokens]
            auto y = torch::transpose(x, 1, 2);
            y = norm1(y.transpose(1, 2)).transpose(1, 2);  // Apply norm before mixing
            y = torch::gelu(tokenMixing1(y));
            y = tokenMixing2(y);
            // Transpose back: [batch, num_tokens, hidden_dim]
            y = torch::transpose(y, 1, 2);
            x = x + y;  // Residual

            // Channel mixing with residual connection
            // [batch, num_tokens, hidden_dim] already in correct shape
            y = norm2(x);
            y = torch::gelu(channelMixing1(y));
            y = channelMixing2(y);
            x = x + y;  // Residual

            return x;
        }

    private:
        torch::nn::LayerNorm norm1;
        torch::nn::LayerNorm norm2;
        torch::nn::Linear tokenMixing1;
        torch::nn::Linear tokenMixing2;
        torch::nn::Linear channelMixing1;
        torch::nn::Linear channelMixing2;
        int64_t numTokens;
        int64_t hiddenDim;
    };

    TORCH_MODULE(MixerBlock);


    // ============================================================================
    // MLP-MIXER NETWORK - Full stack of mixer blocks
    // ============================================================================
    // Stacks multiple MixerBlocks for deep relational learning.
    //
    // Typical configuration for Rocket League:
    //   - 2-3 mixer blocks for good balance of expressivity and speed
    //   - 128 hidden dimension (smaller than MLP for efficiency)
    //   - ~30-50% fewer parameters than equivalent 3-layer MLP
    //
    // Rev 1.0 (2025-01-16, NJH)

    class MixerNetworkImpl : public torch::nn::Module {
    public:
        MixerNetworkImpl(int64_t inputDim, int64_t numTokens, int64_t hiddenDim,
                        int64_t outputDim, int numBlocks = 2)
            : inputProjection(torch::nn::LinearOptions(inputDim, hiddenDim)),
              outputProjection(torch::nn::LinearOptions(hiddenDim, outputDim)),
              norm(torch::nn::LayerNormOptions({hiddenDim})),
              numTokens(numTokens),
              hiddenDim(hiddenDim) {

            register_module("inputProjection", inputProjection);
            register_module("norm", norm);

            // Create mixer blocks
            for (int i = 0; i < numBlocks; i++) {
                auto mixerBlock = MixerBlock(numTokens, hiddenDim);
                register_module("mixerBlock_" + std::to_string(i), mixerBlock);
                mixerBlocks.push_back(mixerBlock);
            }

            register_module("outputProjection", outputProjection);
        }

        torch::Tensor forward(torch::Tensor x, bool halfPrec = false) {
            // x shape: [batch_size, num_tokens, input_dim]
            // where input_dim = feature vector per token

            // Project input to hidden dimension
            // Keep as [batch, num_tokens, hidden_dim]
            x = inputProjection(x);

            // Apply mixer blocks
            for (auto& mixerBlock : mixerBlocks) {
                x = mixerBlock->forward(x);
            }

            // Final normalization
            x = norm(x);

            // Global average pooling across tokens to get [batch, hidden_dim]
            x = torch::mean(x, 1);

            // Project to output dimension
            x = outputProjection(x);

            return x;
        }

    private:
        torch::nn::Linear inputProjection;
        torch::nn::Linear outputProjection;
        torch::nn::LayerNorm norm;
        std::vector<MixerBlock> mixerBlocks;
        int64_t numTokens;
        int64_t hiddenDim;
    };

    TORCH_MODULE(MixerNetwork);

} // namespace GGL
