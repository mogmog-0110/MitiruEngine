#pragma once

/// @file ParticleBridge.hpp
/// @brief sgcパーティクル統合ブリッジ
/// @details sgcのParticleSystem、EmitterConfigをMitiruエンジンに統合する。
///          名前付きパーティクルシステムの管理と一括更新を提供。

#include <cstddef>
#include <string>
#include <unordered_map>

#include <sgc/effects/ParticleSystem.hpp>
#include <mitiru/bridge/BridgeViewPush.hpp>

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
	// ── View Push 統合 ─────────────────────────────────────────

	/// @brief view push ハンドラを登録する（非所有 raw pointer）
	/// @details 登録後、update() の各フレームでシステムごとに
	///          `"<systemName>.activeCount"` が push される。
	///          nullptr を渡すと push を無効化する。
	///
	/// @code
	/// BridgeViewPush vp("particle", setSink, emitSink);
	/// particles.setViewPush(&vp);
	/// @endcode
	void setViewPush(BridgeViewPush* viewPush) noexcept
	{
		m_viewPush = viewPush;
	}

	/// @brief パーティクルシステムを追加する
	/// @param name システム名
	/// @param config エミッター設定
	/// @param maxParticles 最大パーティクル数
	/// @note `<name>.activeCount` の view-push key は登録時に一度だけ構築し、
	///       update() の各フレームでは std::string_view としてゼロアロケーションで再利用する。
	void addSystem(const std::string& name, const sgc::EmitterConfig& config,
		std::size_t maxParticles = 1000)
	{
		sgc::ParticleSystem system(maxParticles);
		system.setConfig(config);

		SystemEntry entry{std::move(system), {}};
		entry.activeCountKey.reserve(name.size() + 12);
		entry.activeCountKey.append(name);
		entry.activeCountKey.append(".activeCount");

		m_systems.emplace(name, std::move(entry));
	}

	/// @brief パーティクルを放出する
	/// @param name システム名
	/// @param count 放出数
	void emit(const std::string& name, int count)
	{
		const auto it = m_systems.find(name);
		if (it == m_systems.end() || count <= 0) return;
		it->second.system.emit(static_cast<std::size_t>(count));
	}

	/// @brief 全システムを更新する
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		for (auto& [name, entry] : m_systems)
		{
			entry.system.update(dt);
		}

		if (m_viewPush)
		{
			for (const auto& [name, entry] : m_systems)
			{
				m_viewPush->set(entry.activeCountKey,
					std::to_string(entry.system.activeCount()));
			}
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
		return it->second.system.activeCount();
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
		for (const auto& [name, entry] : m_systems)
		{
			if (!first) json += ",";
			json += "{\"name\":\"" + name + "\"";
			json += ",\"activeCount\":" + std::to_string(entry.system.activeCount());
			json += ",\"maxParticles\":" + std::to_string(entry.system.maxParticles());
			json += "}";
			first = false;
		}

		json += "]";
		json += "}";
		return json;
	}

private:
	/// @brief 内部エントリ（system + 事前計算済み view-push key）
	struct SystemEntry
	{
		sgc::ParticleSystem system;          ///< パーティクル本体
		std::string         activeCountKey;  ///< "<name>.activeCount" 事前計算済み
	};

	/// @brief 名前 → エントリ
	std::unordered_map<std::string, SystemEntry> m_systems;

	BridgeViewPush* m_viewPush{nullptr};  ///< 非所有。nullptr なら push 無効
};

} // namespace mitiru::bridge
