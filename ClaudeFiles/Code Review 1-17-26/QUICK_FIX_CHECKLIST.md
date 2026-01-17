# Quick Fix Checklist - MLP-Mixer Integration

## ⚠️ MUST FIX BEFORE TESTING ⚠️

### 🔴 Critical: Issue #21 - Add IModel Inheritance

**File**: `MixerModelWrapper.h`

**Current (Line 25)**:
```cpp
class MixerModelWrapper {
```

**Fix to**:
```cpp
class MixerModelWrapper : public IModel {
```

**Then add `override` keywords and missing method**:

```cpp
class MixerModelWrapper : public IModel {
public:
    // Add this new method:
    const char* getModelName() const override { return modelName; }

    // Add override to existing methods:
    torch::Tensor Forward(torch::Tensor input, bool halfPrec = false) override;
    void SetOptimLR(float newLR) override;
    void StepOptim() override;
    void Save(std::filesystem::path folder, bool saveOptim = true) override;
    void Load(std::filesystem::path folder, bool allowNotExist, bool loadOptim = true) override;
    torch::Tensor CopyParams() const override;
    uint64_t GetParamCount() const override;
    std::vector<torch::Tensor> parameters() const override;

    // Rest of class unchanged...
};
```

**Why**: The `reinterpret_cast<Model*>` in `PPOLearner.cpp:192` is undefined behavior without proper inheritance.

---

### 🔴 Critical: Issue #22 - Update ModelSet to Use IModel*

**Files**: Search for `ModelSet` class definition (likely in a header file)

**Current**:
```cpp
class ModelSet {
    std::map<std::string, Model*> map;
    // ...
};
```

**Fix to**:
```cpp
class ModelSet {
    std::map<std::string, IModel*> map;  // Changed from Model* to IModel*

    void Add(IModel* model) {            // Changed from Model* to IModel*
        map[model->getModelName()] = model;
    }

    IModel* operator[](const std::string& name) {  // Changed return type
        auto it = map.find(name);
        return (it != map.end()) ? it->second : nullptr;
    }

    // Update all other methods to use IModel* instead of Model*
};
```

**Then update PPOLearner.cpp line 192**:
```cpp
// Remove the reinterpret_cast - no longer needed!
outModels.map["policy"] = policyWrapper;  // Works now that ModelSet uses IModel*
```

**Why**: Type-safe polymorphism without undefined behavior casts.

---

## 🟡 Should Fix Before Production

### Issue #20 - Export Function for RLBot

**File**: `PPOLearner.cpp` (around line 22)

**Function**: `ExportRLBOTPolicyStateDict`

**Add mixer support**:
```cpp
static void ExportRLBOTPolicyStateDict(GGL::ModelSet& models, const std::filesystem::path& folderPath) {
    auto* policy = models["policy"];

    // Check if it's a MixerModelWrapper
    if (auto* mixerPolicy = dynamic_cast<GGL::MixerModelWrapper*>(policy)) {
        // Export mixer-specific format
        RG_LOG("Exporting MLP-Mixer policy for RLBot...");
        // TODO: Implement mixer export format
        RG_ERR_CLOSE("MLP-Mixer RLBot export not yet implemented");
        return;
    }

    // Otherwise, it's a standard Model - use existing export
    auto* mlpPolicy = dynamic_cast<GGL::Model*>(policy);
    if (!mlpPolicy)
        RG_ERR_CLOSE("ExportRLBOTPolicyStateDict: policy is neither Model nor MixerModelWrapper");

    // ... existing MLP export code ...
}
```

---

## 🟢 Nice to Have

### Issue #24 - Remove Unused tokenDim Config

**File**: `PPOLearnerConfig.h:17`

**Current**:
```cpp
struct MixerConfig {
    int numMixerBlocks = 2;
    int hiddenDim = 256;
    int tokenDim = 128;        // ← This is never used
    int mlpExpansionFactor = 4;
};
```

**Option A - Remove it**:
```cpp
struct MixerConfig {
    int numMixerBlocks = 2;
    int hiddenDim = 256;
    int mlpExpansionFactor = 4;
    // tokenDim is always 12 (from RLTokenizer::carTokenDim)
};
```

**Option B - Actually use it**:
```cpp
// In MixerModelWrapper.cpp constructor:
tokenizer(TokenizerConfig{
    .obsSize = obsSize,
    .numPlayers = numPlayers,
    .includeBoostPads = false,
    .maxBoostPads = 0,
    .featuresPerToken = mixerCfg.tokenDim  // Use config instead of hardcoded 12
})
```

---

## Testing After Fixes

### 1. Compilation Test
```bash
cd GigaLearnBot/GigaLearnCPP
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

**Expected**: Clean compilation with no errors or warnings about reinterpret_cast.

---

### 2. Tokenization Round-Trip Test

Add to test suite or run manually:
```cpp
void TestTokenizerRoundTrip() {
    int obsSize = 30;  // 2v2: ball(6) + 2 players * 12
    int numPlayers = 2;

    GGL::RLTokenizer tokenizer(GGL::TokenizerConfig{
        .obsSize = obsSize,
        .numPlayers = numPlayers,
        .includeBoostPads = false,
        .maxBoostPads = 0,
        .featuresPerToken = 12
    });

    auto obs = torch::randn({16, obsSize});
    auto tokens = tokenizer.tokenize(obs);
    auto reconstructed = tokenizer.untokenize(tokens);

    bool passed = torch::allclose(obs, reconstructed, 1e-5);
    std::cout << "Tokenizer round-trip test: " << (passed ? "PASSED" : "FAILED") << std::endl;

    if (!passed) {
        std::cout << "Max diff: " << (obs - reconstructed).abs().max().item<float>() << std::endl;
    }
}
```

---

### 3. Mixer Model Creation Test

```cpp
void TestMixerModelCreation() {
    try {
        GGL::MixerModelWrapper wrapper(
            "test_policy",
            30,  // obsSize
            2,   // numPlayers
            8,   // numActions
            GGL::MixerConfig{},
            torch::kCPU,
            GGL::ModelOptimType::ADAMW
        );

        std::cout << "Mixer model creation: PASSED" << std::endl;
        std::cout << "  Parameters: " << wrapper.GetParamCount() << std::endl;

        // Test forward pass
        auto obs = torch::randn({4, 30});
        auto output = wrapper.Forward(obs, false);

        std::cout << "  Output shape: [" << output.size(0) << ", " << output.size(1) << "]" << std::endl;
        std::cout << "  Expected: [4, 8]" << std::endl;

        bool shapeCorrect = (output.size(0) == 4 && output.size(1) == 8);
        std::cout << "Forward pass test: " << (shapeCorrect ? "PASSED" : "FAILED") << std::endl;

    } catch (const std::exception& e) {
        std::cout << "Mixer model creation: FAILED - " << e.what() << std::endl;
    }
}
```

---

### 4. Integration Test with PPOLearner

```cpp
GGL::LearnerConfig config = {};
config.ppoConfig.networkArch = GGL::NetworkArchitecture::MLP_MIXER;
config.ppoConfig.mixerConfig.numMixerBlocks = 2;
config.ppoConfig.mixerConfig.hiddenDim = 256;

// Create learner (should use MixerModelWrapper)
GGL::PPOLearner learner(envCreateFunc, config);

// Verify it created mixer models
std::cout << "PPO Learner created with MLP-Mixer architecture" << std::endl;
```

---

## Summary

**Priority Order**:
1. 🔴 Fix #21 (IModel inheritance) - **REQUIRED FOR SAFETY**
2. 🔴 Fix #22 (ModelSet uses IModel*) - **REQUIRED FOR SAFETY**
3. 🟡 Fix #20 (RLBot export) - **Required for deployment**
4. 🟢 Fix #24 (tokenDim) - **Optional cleanup**

**Estimated Time**:
- Fixes #21 + #22: 15-30 minutes
- Fix #20: 30-60 minutes (need to design export format)
- Fix #24: 5 minutes

**Total**: ~1-2 hours to production-ready state

---

## Current Status

✅ **Working**:
- Tokenization (correct PyTorch syntax)
- MixerModel stores config properly
- Half-precision uses correct dimensions
- MixerModelWrapper instantiation
- Dimension validation
- Error messages

⚠️ **Unsafe but Functional**:
- reinterpret_cast workaround (works but undefined behavior)
- ModelSet type system (needs IModel* refactor)

❌ **Not Implemented**:
- RLBot export for mixer models
- tokenDim config usage

**Bottom Line**: Code will likely compile and run, but has undefined behavior. Fix #21 and #22 before serious testing.
