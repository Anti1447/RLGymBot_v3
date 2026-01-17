# MLP-Mixer Architecture Diagram

## Data Flow Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                      PPO Training Loop                          │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
                    ┌──────────────────┐
                    │  Flat Obs Array  │
                    │  [batch, 30]     │
                    │  (for 2v2)       │
                    └──────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    MixerModelWrapper                            │
│                                                                 │
│  ┌────────────────────────────────────────────────────────┐   │
│  │                   RLTokenizer                          │   │
│  │                                                         │   │
│  │  Input: [batch, 30] → Output: [batch, 3, 12]          │   │
│  │                                                         │   │
│  │  Token 0: Ball        [pos(3), vel(3), pad(6)]        │   │
│  │  Token 1: Player 1    [pos(3), vel(3), quat(4), ...]  │   │
│  │  Token 2: Player 2    [pos(3), vel(3), quat(4), ...]  │   │
│  └────────────────────────────────────────────────────────┘   │
│                              │                                  │
│                              ▼                                  │
│  ┌────────────────────────────────────────────────────────┐   │
│  │                   MixerNetwork                         │   │
│  │                                                         │   │
│  │  ┌────────────────────────────────────────────────┐   │   │
│  │  │   Input Projection [12 → 256]                 │   │   │
│  │  │   Projects token features to hidden dim       │   │   │
│  │  └────────────────────────────────────────────────┘   │   │
│  │                      │                                 │   │
│  │                      ▼                                 │   │
│  │  ┌────────────────────────────────────────────────┐   │   │
│  │  │         MixerBlock #1                          │   │   │
│  │  │  ┌──────────────────────────────────────────┐ │   │   │
│  │  │  │  LayerNorm                               │ │   │   │
│  │  │  │  Token-Mixing MLP (mix across 3 tokens) │ │   │   │
│  │  │  │    - Transpose to [batch, 256, 3]       │ │   │   │
│  │  │  │    - Linear [3 → 12]                    │ │   │   │
│  │  │  │    - Swish activation                   │ │   │   │
│  │  │  │    - Linear [12 → 3]                    │ │   │   │
│  │  │  │    - Transpose back                     │ │   │   │
│  │  │  │  Residual +                             │ │   │   │
│  │  │  └──────────────────────────────────────────┘ │   │   │
│  │  │  ┌──────────────────────────────────────────┐ │   │   │
│  │  │  │  LayerNorm                               │ │   │   │
│  │  │  │  Channel-Mixing MLP (mix across 256 ch) │ │   │   │
│  │  │  │    - Linear [256 → 1024]                │ │   │   │
│  │  │  │    - Swish activation                   │ │   │   │
│  │  │  │    - Linear [1024 → 256]                │ │   │   │
│  │  │  │  Residual +                             │ │   │   │
│  │  │  └──────────────────────────────────────────┘ │   │   │
│  │  └────────────────────────────────────────────────┘   │   │
│  │                      │                                 │   │
│  │                      ▼                                 │   │
│  │  ┌────────────────────────────────────────────────┐   │   │
│  │  │         MixerBlock #2 (same structure)        │   │   │
│  │  └────────────────────────────────────────────────┘   │   │
│  │                      │                                 │   │
│  │                      ▼                                 │   │
│  │  ┌────────────────────────────────────────────────┐   │   │
│  │  │   Final LayerNorm                             │   │   │
│  │  └────────────────────────────────────────────────┘   │   │
│  │                      │                                 │   │
│  │                      ▼                                 │   │
│  │  ┌────────────────────────────────────────────────┐   │   │
│  │  │   Global Average Pooling                      │   │   │
│  │  │   [batch, 3, 256] → [batch, 256]              │   │   │
│  │  └────────────────────────────────────────────────┘   │   │
│  │                      │                                 │   │
│  │                      ▼                                 │   │
│  │  ┌────────────────────────────────────────────────┐   │   │
│  │  │   Output Projection [256 → 8]                 │   │   │
│  │  │   Maps to action space                        │   │   │
│  │  └────────────────────────────────────────────────┘   │   │
│  └────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
                    ┌──────────────────┐
                    │  Action Logits   │
                    │  [batch, 8]      │
                    └──────────────────┘
```

---

## Class Hierarchy

```
┌──────────────────────────────────────────────────────────────┐
│                         IModel (Interface)                   │
│  ┌────────────────────────────────────────────────────────┐ │
│  │  + getModelName() const → const char*                  │ │
│  │  + Forward(input, halfPrec) → Tensor                   │ │
│  │  + SetOptimLR(lr) → void                               │ │
│  │  + StepOptim() → void                                  │ │
│  │  + Save(folder, saveOptim) → void                      │ │
│  │  + Load(folder, allowNotExist, loadOptim) → void       │ │
│  │  + CopyParams() → Tensor                               │ │
│  │  + GetParamCount() → uint64_t                          │ │
│  │  + parameters() → vector<Tensor>                       │ │
│  └────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────┘
              ▲                                ▲
              │                                │
              │                                │
    ┌─────────┴─────────┐           ┌─────────┴─────────────┐
    │      Model        │           │  MixerModelWrapper    │
    │                   │           │                       │
    │ (Standard MLP)    │           │ (MLP-Mixer)           │
    │                   │           │                       │
    │ + seq: Sequential │           │ + mixerNet: Network   │
    │ + seqHalf         │           │ + mixerNetHalf        │
    │ + optim           │           │ + tokenizer           │
    │                   │           │ + optim               │
    └───────────────────┘           └───────────────────────┘
```

---

## ModelSet Integration

```
┌────────────────────────────────────────────────────────────┐
│                        ModelSet                            │
│  ┌──────────────────────────────────────────────────────┐ │
│  │  map<string, IModel*>                                │ │
│  │                                                       │ │
│  │  ["policy"]  →  MixerModelWrapper*  ──┐             │ │
│  │                                        │             │ │
│  │  ["critic"]  →  MixerModelWrapper*  ──┤             │ │
│  │                                        │             │ │
│  │  ["shared_head"]  →  nullptr          │             │ │
│  └────────────────────────────────────────┼─────────────┘ │
│                                           │               │
│                                           ▼               │
│                              ┌───────────────────────┐    │
│                              │   Virtual Dispatch    │    │
│                              │   (IModel interface)  │    │
│                              └───────────────────────┘    │
└────────────────────────────────────────────────────────────┘

Usage:
  auto logits = models["policy"]->Forward(obs, halfPrec);
                        ▲
                        │
                  Virtual call through IModel*
```

---

## Token Mixing Visualization

```
Token-Mixing Step (Mixes information ACROSS tokens)
────────────────────────────────────────────────────

Input tokens: [batch, 3 tokens, 256 features]

Ball Token:     [f1, f2, f3, ..., f256]  ─┐
Player1 Token:  [f1, f2, f3, ..., f256]  ─┤─→ Mix →  New Ball Token
Player2 Token:  [f1, f2, f3, ..., f256]  ─┘

Each new token is a weighted combination of all input tokens.
This allows the network to learn relationships between entities
(e.g., "ball is near player 1").

Channel-Mixing Step (Mixes information WITHIN each token)
──────────────────────────────────────────────────────────

For Ball Token: [f1, f2, f3, ..., f256]
                 │   │   │       │
                 └───┴───┴───────┴──→ Mix → [f1', f2', f3', ..., f256']

Each feature is a weighted combination of all features in the same token.
This allows the network to learn feature interactions
(e.g., "if velocity is high and position is near goal, important").
```

---

## Half-Precision Optimization

```
Training Mode (Full Precision)
─────────────────────────────
┌────────────────────────┐
│  mixerNet (FP32)       │
│  - Used for training   │
│  - Gradients tracked   │
│  - Updated by optimizer│
└────────────────────────┘

Inference Mode (Half Precision)
────────────────────────────────
┌────────────────────────┐      ┌────────────────────────┐
│  mixerNet (FP32)       │──────│  mixerNetHalf (FP16)   │
│  - Master weights      │ copy │  - Inference copy      │
│  - Not used in forward │ ──→  │  - 2x faster           │
│  - Updated by training │      │  - Less memory         │
└────────────────────────┘      └────────────────────────┘
                                          │
                                          ▼
                                 ┌────────────────┐
                                 │  Fast Inference│
                                 └────────────────┘

Cache invalidation: _mixerHalfOutdated = true after optimizer step
```

---

## Observation Format (2v2 Example)

```
Flat Observation Array [30 elements]
─────────────────────────────────────

Index   Content             Values
─────   ──────────────────  ─────────
0-2     Ball Position       [x, y, z]
3-5     Ball Velocity       [vx, vy, vz]
6-8     Player 1 Position   [x, y, z]
9-11    Player 1 Velocity   [vx, vy, vz]
12-15   Player 1 Rotation   [qw, qx, qy, qz]
16      Player 1 Boost      [0-100]
17      Player 1 Airtime    [0-inf]
18-20   Player 2 Position   [x, y, z]
21-23   Player 2 Velocity   [vx, vy, vz]
24-27   Player 2 Rotation   [qw, qx, qy, qz]
28      Player 2 Boost      [0-100]
29      Player 2 Airtime    [0-inf]

After Tokenization: [batch, 3 tokens, 12 features]
───────────────────────────────────────────────────

Token 0 (Ball):
  [pos_x, pos_y, pos_z, vel_x, vel_y, vel_z, 0, 0, 0, 0, 0, 0]
   └─────────────────────────────────┘ └──────────────────────┘
            6 features                      6 padding zeros

Token 1 (Player 1):
  [pos_x, pos_y, pos_z, vel_x, vel_y, vel_z, qw, qx, qy, qz, boost, airtime]
   └─────────────────────────────────────────────────────────────────────────┘
                              12 features

Token 2 (Player 2):
  [pos_x, pos_y, pos_z, vel_x, vel_y, vel_z, qw, qx, qy, qz, boost, airtime]
   └─────────────────────────────────────────────────────────────────────────┘
                              12 features
```

---

## Parameter Count Comparison

```
Standard MLP (3 layers, 256 hidden)
────────────────────────────────────
Input Layer:    30 × 256 + 256 = 7,936
Hidden Layer 1: 256 × 256 + 256 = 65,792
Hidden Layer 2: 256 × 256 + 256 = 65,792
Output Layer:   256 × 8 + 8 = 2,056
─────────────────────────────────────
TOTAL: ~142K parameters


MLP-Mixer (2 blocks, 256 hidden, 3 tokens)
───────────────────────────────────────────
Input Projection:   12 × 256 = 3,072

Block 1:
  Token-Mixing:
    Linear1: 3 × 12 + 12 = 48
    Linear2: 12 × 3 + 3 = 39
  Channel-Mixing:
    Linear1: 256 × 1024 + 1024 = 263,168
    Linear2: 1024 × 256 + 256 = 262,400
  LayerNorms: 256 × 4 = 1,024

Block 2:
  (Same as Block 1) = 526,679

Final LayerNorm: 256 × 2 = 512
Output Projection: 256 × 8 + 8 = 2,056
─────────────────────────────────────────
TOTAL: ~1.06M parameters

Note: Mixer has MORE parameters, but better representational capacity
due to relational learning across tokens.
```

---

## File Organization

```
GigaLearnBot/GigaLearnCPP/src/
│
├── public/GigaLearnCPP/
│   ├── Util/
│   │   └── ModelConfig.h         (+ SWISH enum, AdamW default)
│   └── PPO/
│       └── PPOLearnerConfig.h     (+ NetworkArch, MixerConfig)
│
└── private/GigaLearnCPP/
    ├── Util/
    │   ├── Models.h               (+ IModel interface, Swish handler)
    │   ├── MixerBlock.h           (NEW: Mixer architecture)
    │   ├── RLTokenizer.h          (NEW: Tokenization logic)
    │   ├── MixerModelWrapper.h    (NEW: Integration wrapper)
    │   └── MixerModelWrapper.cpp  (NEW: Implementation)
    │
    └── PPO/
        ├── PPOLearner.h           (+ MakeMixerModels declaration)
        └── PPOLearner.cpp         (+ MakeMixerModels implementation)
```

---

## Configuration Example

```cpp
// Enable MLP-Mixer architecture
LearnerConfig config = {};

// Set architecture type
config.ppoConfig.networkArch = NetworkArchitecture::MLP_MIXER;

// Configure mixer hyperparameters
config.ppoConfig.mixerConfig.numMixerBlocks = 2;     // Number of blocks
config.ppoConfig.mixerConfig.hiddenDim = 256;        // Feature dimension
config.ppoConfig.mixerConfig.mlpExpansionFactor = 4; // MLP expansion (4x)

// Standard PPO config
config.ppoConfig.policyLR = 3e-4f;
config.ppoConfig.criticLR = 3e-4f;
config.ppoConfig.batchSize = 100000;
config.ppoConfig.miniBatchSize = 20000;
config.ppoConfig.epochs = 2;

// Create learner
PPOLearner learner(envCreateFunc, config);
learner.Start();
```

---

*End of Architecture Diagram*
