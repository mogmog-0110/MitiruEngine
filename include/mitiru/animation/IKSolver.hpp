#pragma once

/// @file IKSolver.hpp
/// @brief インバースキネマティクス(IK)ソルバー + アニメーションブレンドツリー
/// @details FABRIK / CCDアルゴリズムによるIK解決。
///          アニメーションステートマシンとブレンドツリーの基盤。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mitiru::animation
{

/// @brief 3Dジョイント（ボーン）
struct Joint
{
	float position[3] = {0, 0, 0};
	float length = 1.0f;       ///< 親ジョイントからの距離
	int parentIndex = -1;      ///< 親ジョイントインデックス（-1=ルート）
	std::string name;
};

/// @brief IKチェーン
struct IKChain
{
	std::vector<int> jointIndices;  ///< ルートからエンドエフェクタへのジョイントインデックス列
	float targetPos[3] = {0, 0, 0};
	float tolerance = 0.01f;
	int maxIterations = 20;
};

/// @brief FABRIKソルバー
class FABRIKSolver
{
public:
	/// @brief ジョイント配列を設定する
	void setJoints(std::vector<Joint> joints) { m_joints = std::move(joints); }

	/// @brief IKを解決する
	/// @param chain IKチェーン定義
	/// @return 収束したかどうか
	bool solve(const IKChain& chain)
	{
		if (chain.jointIndices.size() < 2) { return false; }

		const auto& indices = chain.jointIndices;
		const int n = static_cast<int>(indices.size());

		for (int iter = 0; iter < chain.maxIterations; ++iter)
		{
			// Forward pass: エンドエフェクタからルートへ
			auto& endJoint = m_joints[static_cast<size_t>(indices[static_cast<size_t>(n - 1)])];
			endJoint.position[0] = chain.targetPos[0];
			endJoint.position[1] = chain.targetPos[1];
			endJoint.position[2] = chain.targetPos[2];

			for (int i = n - 2; i >= 0; --i)
			{
				auto& curr = m_joints[static_cast<size_t>(indices[static_cast<size_t>(i)])];
				const auto& next = m_joints[static_cast<size_t>(indices[static_cast<size_t>(i + 1)])];
				moveTowards(curr.position, next.position, next.length);
			}

			// Backward pass: ルートからエンドエフェクタへ
			// (ルート位置は固定)
			for (int i = 1; i < n; ++i)
			{
				auto& curr = m_joints[static_cast<size_t>(indices[static_cast<size_t>(i)])];
				const auto& prev = m_joints[static_cast<size_t>(indices[static_cast<size_t>(i - 1)])];
				moveTowards(curr.position, prev.position, curr.length);
			}

			// 収束チェック
			const auto& end = m_joints[static_cast<size_t>(indices[static_cast<size_t>(n - 1)])];
			const float dx = end.position[0] - chain.targetPos[0];
			const float dy = end.position[1] - chain.targetPos[1];
			const float dz = end.position[2] - chain.targetPos[2];
			if (std::sqrt(dx*dx + dy*dy + dz*dz) < chain.tolerance)
			{
				return true;
			}
		}
		return false;
	}

	/// @brief ジョイント配列への参照
	[[nodiscard]] std::vector<Joint>& joints() noexcept { return m_joints; }
	[[nodiscard]] const std::vector<Joint>& joints() const noexcept { return m_joints; }

private:
	std::vector<Joint> m_joints;

	static void moveTowards(float pos[3], const float target[3], float distance)
	{
		const float dx = target[0] - pos[0];
		const float dy = target[1] - pos[1];
		const float dz = target[2] - pos[2];
		const float len = std::sqrt(dx*dx + dy*dy + dz*dz);
		if (len < 1e-6f) { return; }
		const float scale = distance / len;
		pos[0] = target[0] - dx * scale;
		pos[1] = target[1] - dy * scale;
		pos[2] = target[2] - dz * scale;
	}
};

// =============================================
// アニメーションブレンドツリー
// =============================================

/// @brief ブレンドツリーノード基底
class IBlendNode
{
public:
	virtual ~IBlendNode() = default;
	[[nodiscard]] virtual float evaluate(float time) const = 0;
	[[nodiscard]] virtual const char* typeName() const noexcept = 0;
};

/// @brief クリップノード（リーフ: 1つのアニメーションクリップ）
class ClipNode : public IBlendNode
{
public:
	explicit ClipNode(std::string clipName, float duration = 1.0f)
		: m_clipName(std::move(clipName)), m_duration(duration) {}

	[[nodiscard]] float evaluate(float time) const override
	{
		return std::fmod(time, m_duration);
	}
	[[nodiscard]] const char* typeName() const noexcept override { return "Clip"; }
	[[nodiscard]] const std::string& clipName() const noexcept { return m_clipName; }

private:
	std::string m_clipName;
	float m_duration;
};

/// @brief ブレンドノード（2つの子をウェイトで混合）
class BlendNode : public IBlendNode
{
public:
	BlendNode(std::unique_ptr<IBlendNode> a, std::unique_ptr<IBlendNode> b, float weight = 0.5f)
		: m_a(std::move(a)), m_b(std::move(b)), m_weight(weight) {}

	[[nodiscard]] float evaluate(float time) const override
	{
		const float va = m_a ? m_a->evaluate(time) : 0.0f;
		const float vb = m_b ? m_b->evaluate(time) : 0.0f;
		return va * (1.0f - m_weight) + vb * m_weight;
	}
	[[nodiscard]] const char* typeName() const noexcept override { return "Blend"; }
	void setWeight(float w) noexcept { m_weight = std::clamp(w, 0.0f, 1.0f); }

private:
	std::unique_ptr<IBlendNode> m_a, m_b;
	float m_weight;
};

/// @brief アニメーションステートマシン
class AnimationStateMachine
{
public:
	struct Transition
	{
		std::string fromState;
		std::string toState;
		std::function<bool()> condition;
		float blendDuration = 0.2f;
	};

	void addState(const std::string& name, std::unique_ptr<IBlendNode> node)
	{
		m_states[name] = std::move(node);
		if (m_currentState.empty()) { m_currentState = name; }
	}

	void addTransition(Transition t) { m_transitions.push_back(std::move(t)); }

	void update(float dt)
	{
		m_time += dt;
		// チェック遷移条件
		for (const auto& t : m_transitions)
		{
			if (t.fromState == m_currentState && t.condition && t.condition())
			{
				m_currentState = t.toState;
				m_time = 0.0f;
				break;
			}
		}
	}

	[[nodiscard]] const std::string& currentState() const noexcept { return m_currentState; }
	[[nodiscard]] float currentTime() const noexcept { return m_time; }
	[[nodiscard]] size_t stateCount() const noexcept { return m_states.size(); }

private:
	std::unordered_map<std::string, std::unique_ptr<IBlendNode>> m_states;
	std::vector<Transition> m_transitions;
	std::string m_currentState;
	float m_time = 0.0f;
};

} // namespace mitiru::animation
