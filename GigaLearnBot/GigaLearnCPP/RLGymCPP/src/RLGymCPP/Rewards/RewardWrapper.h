#pragma once
#include "Reward.h"

namespace RLGC {
	// This is a base wrapper class that wraps around an existing reward
	class RewardWrapper : public Reward {
	public:

		Reward* child;

		// Assumes ownership
		RewardWrapper(Reward* child)
			: child(child) {}

		~RewardWrapper() {
			delete child;
		}

	protected:
		virtual void Reset(const GameState& initialState) override {
			child->Reset(initialState);
		}

		virtual void PreStep(const GameState& state) override {
			child->PreStep(state);
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
			return child->GetReward(player, state, isFinal);
		}

		virtual std::string GetName() {
			return child->GetName();
		}
	};
}

// 2025-12-09 | Rev 1.0 | NJH
// MathRewardWrapper
// - Combines multiple Reward children using basic math operations.
// - Supports:
//      * ADD          : r0 + r1 + ... + rN
//      * WEIGHTED_SUM : sum_i w_i * r_i
//      * MULTIPLY     : r0 * r1 * ... * rN
//      * MAX          : max_i r_i
//      * MIN          : min_i r_i
// - Assumes ownership of children and deletes them in destructor.

/*
Example usage

Combine StrongTouch with the new scalar:

using namespace RLGC;

Reward* strong  = new StrongTouchReward(15.0f, 130.0f);
Reward* scalar  = new TouchHeightWallScalarReward();

// strong * scalar (reward is powerful high, open-space aerial touches)
Reward* strongAerial =
    new MathRewardWrapper({ strong, scalar }, MathOp::MULTIPLY);


Or a weighted sum of two different rewards:

Reward* dribble = new DribbleMasteryReward();
Reward* kickoff = new KickoffSpeedReward();

Reward* combo =
    new MathRewardWrapper({ dribble, kickoff },
                          MathOp::WEIGHTED_SUM,
                          { 2.0f, 0.5f }); // 2*dribble + 0.5*kickoff


You’d then plug strongAerial or combo into your WeightedReward config like any other reward.
*/

namespace RLGC {

    enum class MathOp {
        ADD,
        WEIGHTED_SUM,
        MULTIPLY,
        MAX,
        MIN
    };

    class MathRewardWrapper : public Reward {
    private:
        std::vector<Reward*> _children;
        std::vector<float>   _weights;
        MathOp               _op;
        std::string          _nameCache;

    public:
        // children: owned by wrapper, will be deleted in destructor
        // op      : which math operation to apply
        // weights : optional; only used for WEIGHTED_SUM. If empty, all 1.0f.
        MathRewardWrapper(const std::vector<Reward*>& children,
            MathOp op,
            const std::vector<float>& weights = {})
            : _children(children),
            _weights(weights),
            _op(op) {
            if (_op == MathOp::WEIGHTED_SUM) {
                if (_weights.empty())
                    _weights.assign(_children.size(), 1.0f);
                else if (_weights.size() != _children.size())
                    throw std::runtime_error("MathRewardWrapper: weights size must match children size");
            }
        }

        virtual ~MathRewardWrapper() override {
            for (Reward* r : _children) {
                delete r;
            }
            _children.clear();
            _weights.clear();
        }

        virtual void Reset(const GameState& initialState) override {
            for (Reward* r : _children)
                r->Reset(initialState);
        }

        virtual void PreStep(const GameState& state) override {
            for (Reward* r : _children)
                r->PreStep(state);
        }

        virtual float GetReward(const Player& player,
            const GameState& state,
            bool isFinal) override {
            if (_children.empty())
                return 0.0f;

            switch (_op) {
            case MathOp::ADD:
                return Sum(player, state, isFinal);
            case MathOp::WEIGHTED_SUM:
                return WeightedSum(player, state, isFinal);
            case MathOp::MULTIPLY:
                return Multiply(player, state, isFinal);
            case MathOp::MAX:
                return Max(player, state, isFinal);
            case MathOp::MIN:
                return Min(player, state, isFinal);
            default:
                return 0.0f;
            }
        }

        virtual std::string GetName() override {
            if (!_nameCache.empty())
                return _nameCache;

            std::string opStr;
            switch (_op) {
            case MathOp::ADD:          opStr = "ADD"; break;
            case MathOp::WEIGHTED_SUM: opStr = "WEIGHTED_SUM"; break;
            case MathOp::MULTIPLY:     opStr = "MULTIPLY"; break;
            case MathOp::MAX:          opStr = "MAX"; break;
            case MathOp::MIN:          opStr = "MIN"; break;
            default:                   opStr = "UNKNOWN"; break;
            }

            std::string name = "MathRewardWrapper(" + opStr + ":[";
            for (size_t i = 0; i < _children.size(); ++i) {
                name += _children[i]->GetName();
                if (i + 1 < _children.size())
                    name += ",";
            }
            name += "])";

            _nameCache = name;
            return _nameCache;
        }

    private:
        float Sum(const Player& player,
            const GameState& state,
            bool isFinal) {
            float result = 0.0f;
            for (Reward* r : _children) {
                result += r->GetReward(player, state, isFinal);
            }
            return result;
        }

        float WeightedSum(const Player& player,
            const GameState& state,
            bool isFinal) {
            float result = 0.0f;
            for (size_t i = 0; i < _children.size(); ++i) {
                float val = _children[i]->GetReward(player, state, isFinal);
                result += _weights[i] * val;
            }
            return result;
        }

        float Multiply(const Player& player,
            const GameState& state,
            bool isFinal) {
            float result = 1.0f;
            for (Reward* r : _children) {
                result *= r->GetReward(player, state, isFinal);
            }
            return result;
        }

        float Max(const Player& player,
            const GameState& state,
            bool isFinal) {
            float result = _children[0]->GetReward(player, state, isFinal);
            for (size_t i = 1; i < _children.size(); ++i) {
                float val = _children[i]->GetReward(player, state, isFinal);
                if (val > result)
                    result = val;
            }
            return result;
        }

        float Min(const Player& player,
            const GameState& state,
            bool isFinal) {
            float result = _children[0]->GetReward(player, state, isFinal);
            for (size_t i = 1; i < _children.size(); ++i) {
                float val = _children[i]->GetReward(player, state, isFinal);
                if (val < result)
                    result = val;
            }
            return result;
        }
    };

} // namespace RLGC
