#pragma once

/// @file ParticleBridge.hpp
/// @brief sgcパーティクル統合ブリッジ
/// @details sgcのParticleSystem、EmitterConfigをMitiruエンジンに統合する。
///          名前付きパーティクルシステムの管理と一括更新を提供。

#include <cstddef>
#include <string>
#include <unordered_map>

#include <sgc/effects/ParticleSystem.hpp>

namespace mitiru::bridge
{

/// @brief sgcパーティクル統合ブリッジ
/// @details 複数のパーティクルシステムを名前付きで管理し、一括更新する。
///
/// @code
/// mitiru::bridge::ParticleBridge particles;
///
/// sgc::EmitterConfig config;
/// config.positionX = 100.0f;
/// config.positionY = 200.0f;
/// config.rate = 50.0f;
/// config.lifetime = 2.0f;
/// particles.addSystem("explosion", config);
/// particles.emit("explosion", 50);
///
/// // 毎フレーム
/// particles.update(dt);
/// auto count = particles.activeParticleCount("explosion");
/// @endcode
class ParticleBridge
{
public:
	/// @brief パーティクルシステムを追加する
	/// @param name システム名
	/// @param config エミッター設定
	/// @param maxParticles 最大パーティクル数
	void addSystem(const std::string& name, const sgc::EmitterConfig& config,
		std::size_t maxParticles = 1000)
	{
		sgc::ParticleSystem system(maxParticles);
		system.setConfig(config);
		m_systems.emplace(name, std::move(system));
	}

	/// @brief パーティクルを放出する
	/// @param name システム名
	/// @param count 放出数
	void emit(const std::string& name, int count)
	{
		const auto it = m_systems.find(name);
		if (it == m_systems.end() || count <= 0) return;
		it->second.emit(static_cast<std::size_t>(count));
	}

	/// @brief 全システムを更新する
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		for (auto& [name, system] : m_systems)
		{
			system.update(dt);
		}
	}

	/// @brief パーティクルシステムを削除する
	/// @param name システム名
	void removeSystem(const std::string& name)
	{
		m_systems.erase(name);
	}

	/// @brief 指定システムのアクティブパーティクル数を取得する
	/// @param name システム名
	/// @return パーティクル数（未登録時は0）
	[[nodiscard]] std::size_t activeParticleCount(const std::string& name) const
	{
		const auto it = m_systems.find(name);
		if (it == m_systems.end()) return 0;
		return it->second.activeCount();
	}

	/// @brief 登録システム数を取得する
	/// @return システム数
	[[nodiscard]] std::size_t systemCount() const noexcept
	{
		return m_systems.size();
	}

	// ── シリアライズ ────────────────────────────────────────

	/// @brief パーティクル状態をJSON文字列として返す
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"systemCount\":" + std::to_string(m_systems.size()) + ",";
		json += "\"systems\":[";

		bool first = true;
		for (const auto& [name, system] : m_systems)
		{
			if (!first) json += ",";
			json += "{\"name\":\"" + name + "\"";
			json += ",\"activeCount\":" + std::to_string(system.activeCount());
			json += ",\"maxParticles\":" + std::to_string(system.maxParticles());
			json += "}";
			first = false;
		}

		json += "]";
		json += "}";
		return json;
	}

private:
	/// @brief 名前 → パーティクルシステム
	std::unordered_map<std::string, sgc::ParticleSystem> m_systems;
};

} // namespace mitiru::bridge
