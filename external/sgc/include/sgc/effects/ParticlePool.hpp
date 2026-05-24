#pragma once

/// @file ParticlePool.hpp
/// @brief パーティクルシステムのオブジェクトプール
///
/// ParticleSystemインスタンスを事前確保し、動的な生成・破棄を回避する。
///
/// @code
/// sgc::ParticlePool pool(10, 500);  // 10個のシステム、各500パーティクル
///
/// auto* ps = pool.acquire();
/// if (ps)
/// {
///     ps->setConfig(config);
///     ps->emit(50);
///     // ... 使用後 ...
///     pool.release(ps);
/// }
/// @endcode

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

#include "sgc/effects/ParticleSystem.hpp"

namespace sgc
{

/// @brief ParticleSystemのオブジェクトプール
///
/// 事前に確保したParticleSystemインスタンスを貸し出し・返却する。
/// 取得時は既にリセット済みのシステムが返される。
class ParticlePool
{
public:
	/// @brief プールを構築する
	/// @param poolSize プール内のシステム数
	/// @param particlesPerSystem 各システムの最大パーティクル数
	explicit ParticlePool(std::size_t poolSize, std::size_t particlesPerSystem = 1000)
		: m_particlesPerSystem(particlesPerSystem)
	{
		m_systems.reserve(poolSize);
		m_available.reserve(poolSize);

		for (std::size_t i = 0; i < poolSize; ++i)
		{
			m_systems.push_back(
				std::make_unique<ParticleSystem>(particlesPerSystem));
			m_available.push_back(m_systems.back().get());
		}
	}

	/// @brief システムを取得する
	/// @return 利用可能なシステムのポインタ。枯渇時はnullptr
	[[nodiscard]] ParticleSystem* acquire()
	{
		if (m_available.empty())
		{
			return nullptr;
		}

		ParticleSystem* ps = m_available.back();
		m_available.pop_back();
		m_activeCount++;
		return ps;
	}

	/// @brief システムをプールに返却する
	///
	/// パーティクルとアフェクターはクリアされる。
	///
	/// @param ps 返却するシステム（このプールから取得したもの）
	void release(ParticleSystem* ps)
	{
		if (!ps) return;

		// このプールが所有するシステムか検証
		const bool owned = std::any_of(
			m_systems.begin(), m_systems.end(),
			[ps](const auto& up) { return up.get() == ps; });

		if (!owned) return;

		// 既に返却済みか検証
		const bool alreadyAvailable = std::any_of(
			m_available.begin(), m_available.end(),
			[ps](const auto* p) { return p == ps; });

		if (alreadyAvailable) return;

		ps->clear();
		ps->clearAffectors();
		ps->setConfig(EmitterConfig{});

		m_available.push_back(ps);
		m_activeCount--;
	}

	/// @brief 現在使用中のシステム数
	/// @return アクティブ数
	[[nodiscard]] std::size_t activeCount() const noexcept
	{
		return m_activeCount;
	}

	/// @brief 利用可能なシステム数
	/// @return 空きシステム数
	[[nodiscard]] std::size_t availableCount() const noexcept
	{
		return m_available.size();
	}

	/// @brief プールの総容量
	/// @return 総システム数
	[[nodiscard]] std::size_t totalCount() const noexcept
	{
		return m_systems.size();
	}

	/// @brief 各システムの最大パーティクル数
	/// @return パーティクル数
	[[nodiscard]] std::size_t particlesPerSystem() const noexcept
	{
		return m_particlesPerSystem;
	}

private:
	std::vector<std::unique_ptr<ParticleSystem>> m_systems;   ///< 全システム
	std::vector<ParticleSystem*> m_available;                  ///< 利用可能なシステム
	std::size_t m_particlesPerSystem;                          ///< 各システムの最大数
	std::size_t m_activeCount{0};                              ///< 使用中の数
};

} // namespace sgc
