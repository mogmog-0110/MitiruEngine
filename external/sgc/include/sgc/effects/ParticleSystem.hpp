#pragma once

/// @file ParticleSystem.hpp
/// @brief パーティクルシステム
///
/// プリアロケーション + swap-and-pop方式のパーティクル管理。
/// エミッター設定とアフェクターによるカスタマイズ可能な粒子効果。
///
/// @code
/// sgc::EmitterConfig config;
/// config.position = {100.0f, 200.0f};
/// config.rate = 50.0f;
/// config.lifetime = 2.0f;
/// config.speed = 100.0f;
/// config.spread = 3.14f;
///
/// sgc::ParticleSystem particles(1000);
/// particles.setConfig(config);
/// particles.emit(10);
///
/// particles.update(dt);
/// auto active = particles.activeParticles();
/// @endcode

#include <cmath>
#include <cstddef>
#include <functional>
#include <numbers>
#include <span>
#include <vector>

namespace sgc
{

/// @brief パーティクル
struct Particle
{
	float x{0.0f};          ///< X座標
	float y{0.0f};          ///< Y座標
	float vx{0.0f};         ///< X速度
	float vy{0.0f};         ///< Y速度
	float lifetime{0.0f};   ///< 残り寿命（秒）
	float maxLifetime{1.0f}; ///< 最大寿命（秒）
	float size{1.0f};       ///< サイズ
	float r{1.0f};          ///< 赤
	float g{1.0f};          ///< 緑
	float b{1.0f};          ///< 青
	float a{1.0f};          ///< アルファ
};

/// @brief エミッター設定
struct EmitterConfig
{
	float positionX{0.0f};  ///< エミッター位置X
	float positionY{0.0f};  ///< エミッター位置Y
	float rate{10.0f};      ///< 毎秒の放出数
	float lifetime{1.0f};   ///< パーティクル寿命（秒）
	float speed{100.0f};    ///< 初速度
	float spread{static_cast<float>(std::numbers::pi * 2.0)};  ///< 放出角度範囲（ラジアン）
	float baseAngle{0.0f};  ///< 放出基準角度（ラジアン）
	float startSize{1.0f};  ///< 初期サイズ
	float endSize{0.0f};    ///< 終了サイズ
	float startR{1.0f};     ///< 初期色R
	float startG{1.0f};     ///< 初期色G
	float startB{1.0f};     ///< 初期色B
	float startA{1.0f};     ///< 初期アルファ
	float endA{0.0f};       ///< 終了アルファ
};

/// @brief パーティクルアフェクター型
using ParticleAffector = std::function<void(Particle&, float)>;

/// @brief パーティクルシステム
///
/// プリアロケーション方式。swap-and-popで死んだパーティクルを効率的に除去する。
class ParticleSystem
{
public:
	/// @brief パーティクルシステムを構築する
	/// @param maxParticles 最大パーティクル数
	explicit ParticleSystem(std::size_t maxParticles = 1000)
	{
		m_particles.reserve(maxParticles);
		m_maxParticles = maxParticles;
	}

	/// @brief エミッター設定を適用する
	/// @param config 設定
	void setConfig(const EmitterConfig& config)
	{
		m_config = config;
	}

	/// @brief エミッター設定を取得する
	/// @return 設定への参照
	[[nodiscard]] const EmitterConfig& config() const noexcept { return m_config; }

	/// @brief アフェクターを追加する
	/// @param affector パーティクル変更関数
	void addAffector(ParticleAffector affector)
	{
		m_affectors.push_back(std::move(affector));
	}

	/// @brief 指定数のパーティクルを即時放出する
	/// @param count 放出数
	void emit(std::size_t count)
	{
		for (std::size_t i = 0; i < count; ++i)
		{
			if (m_particles.size() >= m_maxParticles) break;

			Particle p;
			p.x = m_config.positionX;
			p.y = m_config.positionY;

			// 角度を均等分布（簡易的に連番使用）
			const float angle = m_config.baseAngle
				+ m_config.spread * (static_cast<float>(m_emitCounter) / 37.0f
					- std::floor(static_cast<float>(m_emitCounter) / 37.0f))
				- m_config.spread * 0.5f;
			++m_emitCounter;

			p.vx = std::cos(angle) * m_config.speed;
			p.vy = std::sin(angle) * m_config.speed;
			p.lifetime = m_config.lifetime;
			p.maxLifetime = m_config.lifetime;
			p.size = m_config.startSize;
			p.r = m_config.startR;
			p.g = m_config.startG;
			p.b = m_config.startB;
			p.a = m_config.startA;

			m_particles.push_back(p);
		}
	}

	/// @brief パーティクルを更新する
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		// 自動放出
		m_emitAccumulator += m_config.rate * dt;
		while (m_emitAccumulator >= 1.0f)
		{
			emit(1);
			m_emitAccumulator -= 1.0f;
		}

		// パーティクル更新
		std::size_t i = 0;
		while (i < m_particles.size())
		{
			auto& p = m_particles[i];
			p.lifetime -= dt;

			if (p.lifetime <= 0.0f)
			{
				// swap-and-pop で除去
				if (i + 1 < m_particles.size())
				{
					m_particles[i] = m_particles.back();
				}
				m_particles.pop_back();
				continue;
			}

			// 位置更新
			p.x += p.vx * dt;
			p.y += p.vy * dt;

			// ライフタイム比率による補間
			const float lifeRatio = 1.0f - (p.lifetime / p.maxLifetime);

			// サイズ補間
			p.size = m_config.startSize
				+ (m_config.endSize - m_config.startSize) * lifeRatio;

			// アルファ補間
			p.a = m_config.startA
				+ (m_config.endA - m_config.startA) * lifeRatio;

			// アフェクター適用
			for (const auto& affector : m_affectors)
			{
				affector(p, dt);
			}

			++i;
		}
	}

	/// @brief アクティブなパーティクルを取得する
	/// @return パーティクルのスパン
	[[nodiscard]] std::span<const Particle> activeParticles() const noexcept
	{
		return std::span<const Particle>(m_particles.data(), m_particles.size());
	}

	/// @brief アクティブなパーティクル数
	/// @return パーティクル数
	[[nodiscard]] std::size_t activeCount() const noexcept
	{
		return m_particles.size();
	}

	/// @brief 最大パーティクル数
	/// @return 最大数
	[[nodiscard]] std::size_t maxParticles() const noexcept
	{
		return m_maxParticles;
	}

	/// @brief 全パーティクルをクリアする
	void clear()
	{
		m_particles.clear();
		m_emitAccumulator = 0.0f;
	}

	/// @brief アフェクターをクリアする
	void clearAffectors()
	{
		m_affectors.clear();
	}

private:
	std::vector<Particle> m_particles;           ///< アクティブなパーティクル
	std::size_t m_maxParticles{1000};            ///< 最大数
	EmitterConfig m_config;                      ///< エミッター設定
	std::vector<ParticleAffector> m_affectors;   ///< アフェクター
	float m_emitAccumulator{0.0f};               ///< 放出アキュムレータ
	std::size_t m_emitCounter{0};                ///< 放出カウンタ（角度分散用）
};

} // namespace sgc
