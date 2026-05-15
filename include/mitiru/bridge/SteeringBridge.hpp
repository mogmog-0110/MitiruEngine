#pragma once

/// @file SteeringBridge.hpp
/// @brief sgc ステアリング行動統合ブリッジ
/// @details sgcのステアリング行動（seek, flee, arrive等）とフロッキングを
///          Mitiruエンジンに統合する。名前付きエージェントの管理を行う。

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include <sgc/ai/SteeringBehaviors.hpp>
#include <sgc/ai/Flocking.hpp>

namespace mitiru::bridge
{

/// @brief sgc ステアリング行動統合ブリッジ
/// @details 名前付きステアリングエージェントを管理し、各種操舵行動を適用する。
///
/// @code
/// mitiru::bridge::SteeringBridge steering;
///
/// sgc::ai::SteeringAgent<float> agent;
/// agent.position = {100.0f, 200.0f};
/// agent.maxSpeed = 100.0f;
/// agent.maxForce = 50.0f;
/// steering.addAgent("player", agent);
///
/// auto force = steering.seek("player", {500.0f, 300.0f});
/// steering.applySteering("player", force, 0.016f);
/// @endcode
class SteeringBridge
{
public:
	using Agent = sgc::ai::SteeringAgent<float>;

	// ── エージェント管理 ──────────────────────────────────────

	/// @brief エージェントを追加する
	/// @param name エージェント名
	/// @param agent エージェントの初期状態
	void addAgent(const std::string& name, Agent agent)
	{
		m_agents[name] = agent;
	}

	/// @brief エージェントを削除する
	/// @param name エージェント名
	void removeAgent(const std::string& name)
	{
		m_agents.erase(name);
	}

	// ── 操舵力適用 ────────────────────────────────────────────

	/// @brief 操舵力をエージェントに適用する
	/// @param name エージェント名
	/// @param force 適用する操舵力
	/// @param dt タイムステップ（秒）
	void applySteering(const std::string& name, const sgc::Vec2f& force, float dt)
	{
		const auto it = m_agents.find(name);
		if (it != m_agents.end())
		{
			it->second = sgc::ai::applyForce(it->second, force, dt);
		}
	}

	// ── 操舵行動クエリ ───────────────────────────────────────

	/// @brief ターゲットに向かう操舵力を計算する
	/// @param agentName エージェント名
	/// @param target 目標位置
	/// @return 操舵力ベクトル（エージェント未登録時はゼロベクトル）
	[[nodiscard]] sgc::Vec2f seek(const std::string& agentName, const sgc::Vec2f& target) const
	{
		const auto it = m_agents.find(agentName);
		if (it == m_agents.end())
		{
			return {};
		}
		return sgc::ai::seek(it->second, target);
	}

	/// @brief 脅威から逃げる操舵力を計算する
	/// @param agentName エージェント名
	/// @param threat 脅威の位置
	/// @return 操舵力ベクトル（エージェント未登録時はゼロベクトル）
	[[nodiscard]] sgc::Vec2f flee(const std::string& agentName, const sgc::Vec2f& threat) const
	{
		const auto it = m_agents.find(agentName);
		if (it == m_agents.end())
		{
			return {};
		}
		return sgc::ai::flee(it->second, threat);
	}

	/// @brief ターゲットに近づくにつれて減速する操舵力を計算する
	/// @param agentName エージェント名
	/// @param target 目標位置
	/// @param slowingRadius 減速開始距離
	/// @return 操舵力ベクトル（エージェント未登録時はゼロベクトル）
	[[nodiscard]] sgc::Vec2f arrive(const std::string& agentName, const sgc::Vec2f& target, float slowingRadius) const
	{
		const auto it = m_agents.find(agentName);
		if (it == m_agents.end())
		{
			return {};
		}
		return sgc::ai::arrive(it->second, target, slowingRadius);
	}

	// ── フロッキング ──────────────────────────────────────────

	/// @brief フロッキング力を計算する
	/// @param agentName エージェント名
	/// @param neighborRadius 近隣とみなす半径
	/// @param weights 分離・整列・結合の重み係数
	/// @return フロッキング力ベクトル（エージェント未登録時はゼロベクトル）
	[[nodiscard]] sgc::Vec2f flock(const std::string& agentName, float neighborRadius,
		const sgc::ai::FlockWeights& weights = {}) const
	{
		const auto it = m_agents.find(agentName);
		if (it == m_agents.end())
		{
			return {};
		}

		// 全エージェントをスパンとして渡す
		std::vector<Agent> allAgents;
		allAgents.reserve(m_agents.size());
		for (const auto& [name, agent] : m_agents)
		{
			allAgents.push_back(agent);
		}

		return sgc::ai::flock(it->second, std::span<const Agent>{allAgents}, neighborRadius, weights);
	}

	// ── エージェントアクセス ──────────────────────────────────

	/// @brief エージェントの状態を取得する
	/// @param name エージェント名
	/// @return エージェントへのポインタ（未登録時はnullptr）
	[[nodiscard]] const Agent* getAgent(const std::string& name) const
	{
		const auto it = m_agents.find(name);
		if (it == m_agents.end())
		{
			return nullptr;
		}
		return &it->second;
	}

	/// @brief エージェント数を取得する
	/// @return エージェント数
	[[nodiscard]] std::size_t agentCount() const noexcept
	{
		return m_agents.size();
	}

	// ── シリアライズ ──────────────────────────────────────────

	/// @brief ステアリング状態をJSON文字列として返す
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"agentCount\":" + std::to_string(m_agents.size()) + ",";
		json += "\"agents\":[";

		bool first = true;
		for (const auto& [name, agent] : m_agents)
		{
			if (!first) json += ",";
			json += "{\"name\":\"" + name + "\"";
			json += ",\"x\":" + std::to_string(agent.position.x);
			json += ",\"y\":" + std::to_string(agent.position.y);
			json += "}";
			first = false;
		}

		json += "]";
		json += "}";
		return json;
	}

private:
	std::unordered_map<std::string, Agent> m_agents;  ///< 名前 → エージェント
};

} // namespace mitiru::bridge
