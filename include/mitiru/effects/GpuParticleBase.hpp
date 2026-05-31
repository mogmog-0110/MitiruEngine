#pragma once

/// @file GpuParticleBase.hpp
/// @brief GPU パーティクルシステムの共通基底クラス
/// @details DX11/DX12 実装で共有される CPU 側ロジックを集約する。
///          エミッション制御（レートベース放出・バースト）、パーティクルステージング、
///          ライフタイム管理を提供する。GPU バッファ管理はサブクラスに委譲する。

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/effects/GpuParticleSystem.hpp>

namespace mitiru::effects
{

/// @brief コンパクション実行間隔（フレーム数）
static constexpr std::uint32_t COMPACT_INTERVAL_FRAMES = 16;

/// @brief コンピュートシェーダーのスレッドグループサイズ
static constexpr std::uint32_t CS_THREAD_GROUP_SIZE = 256;

/// @brief GPU パーティクルシステム共通基底クラス
/// @details CPU 側のエミッション・ステージング・アクセサを実装し、
///          GPU バッファ操作のみをサブクラスに要求する。
class GpuParticleBase : public IGpuParticleSystem
{
public:
	/// @brief エミッター設定を適用する
	void setEmitter(const ParticleEmitter& emitter) override
	{
		m_emitter = emitter;
	}

	/// @brief エミッター設定を取得する
	[[nodiscard]] const ParticleEmitter& emitter() const noexcept override
	{
		return m_emitter;
	}

	/// @brief 個別パーティクルを放出する
	void emit(const sgc::Vec3f& position,
	          const sgc::Vec3f& velocity,
	          float lifetime,
	          const sgc::Colorf& color,
	          float size) override
	{
		if (m_activeCount >= m_maxParticles)
		{
			return;
		}

		GpuParticle p;
		p.posX = position.x;
		p.posY = position.y;
		p.posZ = position.z;
		p.velX = velocity.x;
		p.velY = velocity.y;
		p.velZ = velocity.z;
		p.accelX = 0;
		p.accelY = 0;
		p.accelZ = 0;
		p.colorR = color.r;
		p.colorG = color.g;
		p.colorB = color.b;
		p.colorA = color.a;
		p.size = size;
		p.lifetime = lifetime;
		p.age = 0;

		m_stagingParticles.push_back(p);
	}

	/// @brief エミッター設定に基づいてパーティクルを自動放出する
	void emitFromEmitter(float dt) override
	{
		m_emitAccumulator += dt;

		const float interval = 1.0f / std::max(1.0f, m_emitter.ratePerSecond);
		while (m_emitAccumulator >= interval &&
		       m_activeCount + static_cast<std::uint32_t>(m_stagingParticles.size())
		           < m_maxParticles)
		{
			m_emitAccumulator -= interval;
			emitOneFromEmitter();
		}

		/// バースト処理
		m_emitterTime += dt;
		for (std::uint32_t i = 0; i < m_emitter.burstCount; ++i)
		{
			auto& b = m_emitter.bursts[i];
			if (m_emitterTime >= b.time && m_emitterTime - dt < b.time)
			{
				burst(b.count);
			}
		}
	}

	/// @brief バースト放出を行う
	void burst(std::uint32_t count) override
	{
		for (std::uint32_t i = 0; i < count; ++i)
		{
			if (m_activeCount + static_cast<std::uint32_t>(m_stagingParticles.size())
			    >= m_maxParticles)
			{
				break;
			}
			emitOneFromEmitter();
		}
	}

	/// @brief アクティブパーティクル数を取得する
	[[nodiscard]] std::uint32_t activeCount() const noexcept override
	{
		return m_activeCount;
	}

	/// @brief 最大パーティクル数を取得する
	[[nodiscard]] std::uint32_t maxCount() const noexcept override
	{
		return m_maxParticles;
	}

	/// @brief 全パーティクルをクリアする
	void clear() override
	{
		m_activeCount = 0;
		m_stagingParticles.clear();
		m_emitAccumulator = 0;
		m_emitterTime = 0;
	}

	/// @brief システムが有効かどうかを判定する
	[[nodiscard]] bool isValid() const noexcept override
	{
		return m_valid;
	}

protected:
	/// @brief コンストラクタ
	/// @param maxParticles 最大パーティクル数
	/// @param seed 乱数 seed (既定固定 = 決定論。caller が replay seed を注入できる)
	explicit GpuParticleBase(std::uint32_t maxParticles, std::uint32_t seed = 42u)
		: m_maxParticles(maxParticles)
		, m_rng(seed)
	{
	}

	/// @brief デストラクタ
	~GpuParticleBase() override = default;

	/// @brief エミッター設定から1パーティクルを放出する
	void emitOneFromEmitter()
	{
		const auto pos = m_emitter.samplePosition(m_rng);
		const auto vel = m_emitter.sampleVelocity(m_rng, pos);
		const float lt = m_emitter.sampleLifetime(m_rng);
		const float sz = m_emitter.sampleSize(m_rng);

		emit(pos, vel, lt, m_emitter.startColor, sz);
	}

	/// @brief 生存パーティクルのみを抽出する（コンパクション共通ロジック）
	/// @param src パーティクル配列の先頭ポインタ
	/// @param count 配列内のパーティクル数
	/// @return 生存パーティクルのみを含むベクタ
	[[nodiscard]] static std::vector<GpuParticle> filterAliveParticles(
		const GpuParticle* src,
		std::uint32_t count)
	{
		std::vector<GpuParticle> alive;
		alive.reserve(count);

		for (std::uint32_t i = 0; i < count; ++i)
		{
			if (src[i].age < src[i].lifetime && src[i].size > 0)
			{
				alive.push_back(src[i]);
			}
		}

		return alive;
	}

	/// @brief コンパクションを実行すべきかカウンタで判定する
	/// @return true の場合コンパクションを実行する
	[[nodiscard]] bool shouldCompact() noexcept
	{
		++m_compactCounter;
		if (m_compactCounter < COMPACT_INTERVAL_FRAMES)
		{
			return false;
		}
		m_compactCounter = 0;
		return true;
	}

	/// @brief ディスパッチに必要なグループ数を計算する
	/// @param particleCount パーティクル数
	/// @return スレッドグループ数
	[[nodiscard]] static std::uint32_t computeDispatchGroupCount(
		std::uint32_t particleCount) noexcept
	{
		return (particleCount + CS_THREAD_GROUP_SIZE - 1) / CS_THREAD_GROUP_SIZE;
	}

	/// @brief アップロード可能なパーティクル数を計算してステージングをクリアする
	/// @return アップロードするパーティクル数（0 ならアップロード不要）
	[[nodiscard]] std::uint32_t prepareStagingUpload()
	{
		if (m_stagingParticles.empty())
		{
			return 0;
		}

		const auto uploadCount = static_cast<std::uint32_t>(
			std::min(
				m_stagingParticles.size(),
				static_cast<std::size_t>(m_maxParticles - m_activeCount)));

		if (uploadCount == 0)
		{
			m_stagingParticles.clear();
			return 0;
		}

		return uploadCount;
	}

	/// @brief ステージングアップロード完了後の後処理
	/// @param uploadCount アップロードしたパーティクル数
	void finalizeStagingUpload(std::uint32_t uploadCount)
	{
		m_activeCount += uploadCount;
		m_stagingParticles.clear();
	}

	// ── 共通メンバ変数 ────────────────────────────────
	std::uint32_t m_maxParticles = MAX_GPU_PARTICLES;  ///< 最大パーティクル数
	std::uint32_t m_activeCount = 0;                   ///< アクティブ数
	std::uint32_t m_currentBuffer = 0;                 ///< ピンポンインデックス
	std::uint32_t m_compactCounter = 0;                ///< コンパクションカウンタ
	float m_emitAccumulator = 0;                       ///< 放出アキュムレータ
	float m_emitterTime = 0;                           ///< エミッター経過時間
	bool m_valid = false;                              ///< 有効フラグ

	ParticleEmitter m_emitter;                         ///< エミッター設定
	std::mt19937 m_rng;                                ///< 乱数生成器
	std::vector<GpuParticle> m_stagingParticles;       ///< CPU側ステージング
};

} // namespace mitiru::effects
