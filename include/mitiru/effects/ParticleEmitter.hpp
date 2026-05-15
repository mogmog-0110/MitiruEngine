#pragma once

/// @file ParticleEmitter.hpp
/// @brief パーティクルエミッター設定
/// @details ポイント・球・コーン・ボックスの放出形状と、レートベース・バースト放出、
///          ライフタイム全体にわたるサイズ・色のパラメトリックカーブを提供する。
///
/// @code
/// mitiru::effects::ParticleEmitter emitter;
/// emitter.shape = EmissionShape::Sphere;
/// emitter.sphereRadius = 2.0f;
/// emitter.ratePerSecond = 500.0f;
/// emitter.startColor = {1, 0.5f, 0, 1};
/// emitter.endColor = {1, 0, 0, 0};
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

namespace mitiru::effects
{

/// @brief 放出形状
enum class EmissionShape : std::uint8_t
{
	Point,   ///< 単一点からの放出
	Sphere,  ///< 球面上からの放出
	Cone,    ///< 円錐内への放出
	Box,     ///< 直方体内からの放出
};

/// @brief バースト設定
/// @details 特定時刻に一度にまとまった数のパーティクルを放出する。
struct EmissionBurst
{
	float time = 0.0f;         ///< 放出時刻（秒）
	std::uint32_t count = 10;  ///< 放出数
	float repeatInterval = 0;  ///< 繰り返し間隔（0で一度きり）
};

/// @brief ライフタイムカーブ上のキーフレーム
/// @details 0.0〜1.0の正規化ライフタイムに対する値を定義する。
struct CurveKey
{
	float t = 0.0f;     ///< 正規化時刻（0.0〜1.0）
	float value = 1.0f; ///< 値
};

/// @brief パラメトリックカーブ（線形補間）
/// @details ライフタイム全体にわたる値変化を定義する。
///          キーが空の場合はデフォルト値1.0を返す。
struct ParameterCurve
{
	static constexpr std::size_t MAX_KEYS = 8; ///< 最大キー数
	CurveKey keys[MAX_KEYS] = {};              ///< キーフレーム配列
	std::uint32_t keyCount = 0;                ///< 有効なキー数

	/// @brief キーを追加する
	/// @param t 正規化時刻
	/// @param value 値
	void addKey(float t, float value) noexcept
	{
		if (keyCount < MAX_KEYS)
		{
			keys[keyCount] = CurveKey{t, value};
			++keyCount;
		}
	}

	/// @brief 正規化時刻における値を評価する
	/// @param t 正規化ライフタイム（0.0〜1.0）
	/// @return 補間された値
	[[nodiscard]] float evaluate(float t) const noexcept
	{
		if (keyCount == 0)
		{
			return 1.0f;
		}
		if (keyCount == 1)
		{
			return keys[0].value;
		}

		/// t以下の最大キーと、tより大きい最小キーを探す
		if (t <= keys[0].t)
		{
			return keys[0].value;
		}
		if (t >= keys[keyCount - 1].t)
		{
			return keys[keyCount - 1].value;
		}

		for (std::uint32_t i = 0; i + 1 < keyCount; ++i)
		{
			if (t >= keys[i].t && t <= keys[i + 1].t)
			{
				const float range = keys[i + 1].t - keys[i].t;
				if (range < 1e-6f)
				{
					return keys[i].value;
				}
				const float alpha = (t - keys[i].t) / range;
				return keys[i].value + alpha * (keys[i + 1].value - keys[i].value);
			}
		}

		return keys[keyCount - 1].value;
	}
};

/// @brief パーティクルエミッター設定
/// @details パーティクルの放出形状・レート・初期パラメータ・
///          ライフタイムカーブを統合管理する。
struct ParticleEmitter
{
	// ── 放出形状 ──────────────────────────────────
	EmissionShape shape = EmissionShape::Point;  ///< 放出形状

	/// @name Sphere設定
	/// @{
	float sphereRadius = 1.0f;  ///< 球の半径
	/// @}

	/// @name Cone設定
	/// @{
	float coneAngle = 0.523599f;  ///< コーン半角（ラジアン、デフォルト30度）
	float coneRadius = 0.0f;      ///< コーン底面半径（0でポイント放出）
	sgc::Vec3f coneDirection{0, 1, 0};  ///< コーン方向
	/// @}

	/// @name Box設定
	/// @{
	sgc::Vec3f boxHalfExtents{1, 1, 1};  ///< ボックス半径
	/// @}

	// ── 放出レート ────────────────────────────────
	float ratePerSecond = 100.0f;  ///< 毎秒放出数
	static constexpr std::size_t MAX_BURSTS = 8;       ///< 最大バースト数
	EmissionBurst bursts[MAX_BURSTS] = {};              ///< バースト設定
	std::uint32_t burstCount = 0;                       ///< 有効バースト数

	/// @brief バーストを追加する
	void addBurst(float time, std::uint32_t count, float interval = 0) noexcept
	{
		if (burstCount < MAX_BURSTS)
		{
			bursts[burstCount] = EmissionBurst{time, count, interval};
			++burstCount;
		}
	}

	// ── 初期パラメータ ──────────────────────────────
	sgc::Vec3f position{0, 0, 0};        ///< エミッター位置
	float initialSpeed = 5.0f;            ///< 初速
	float initialSpeedVariance = 1.0f;    ///< 初速のばらつき
	float lifetime = 2.0f;                ///< パーティクル寿命（秒）
	float lifetimeVariance = 0.5f;        ///< 寿命のばらつき
	float initialSize = 0.1f;             ///< 初期サイズ
	float initialSizeVariance = 0.02f;    ///< サイズのばらつき
	sgc::Colorf startColor{1, 1, 1, 1};  ///< 開始色
	sgc::Colorf endColor{1, 1, 1, 0};    ///< 終了色

	// ── 物理パラメータ ──────────────────────────────
	sgc::Vec3f gravity{0, -9.81f, 0};  ///< 重力加速度
	float drag = 0.1f;                  ///< 空気抵抗係数

	// ── ライフタイムカーブ ────────────────────────────
	ParameterCurve sizeOverLifetime;   ///< ライフタイムに対するサイズ倍率
	ParameterCurve alphaOverLifetime;  ///< ライフタイムに対するアルファ倍率

	// ── ヘルパー関数 ──────────────────────────────

	/// @brief ランダムな放出位置オフセットを計算する
	/// @param rng 乱数生成器
	/// @return 放出位置のオフセット
	template <typename Rng>
	[[nodiscard]] sgc::Vec3f samplePosition(Rng& rng) const noexcept
	{
		std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
		std::uniform_real_distribution<float> distNeg(-1.0f, 1.0f);

		switch (shape)
		{
		case EmissionShape::Point:
			return position;

		case EmissionShape::Sphere:
		{
			/// 球面上の一様分布点を生成する
			const float theta = dist01(rng) * 6.283185f;
			const float phi = std::acos(1.0f - 2.0f * dist01(rng));
			const float r = sphereRadius * std::cbrt(dist01(rng));
			return position + sgc::Vec3f{
				r * std::sin(phi) * std::cos(theta),
				r * std::sin(phi) * std::sin(theta),
				r * std::cos(phi)
			};
		}

		case EmissionShape::Cone:
		{
			/// コーン内の方向をサンプルする
			const float theta = dist01(rng) * 6.283185f;
			const float r = coneRadius * std::sqrt(dist01(rng));
			return position + sgc::Vec3f{
				r * std::cos(theta),
				0,
				r * std::sin(theta)
			};
		}

		case EmissionShape::Box:
			return position + sgc::Vec3f{
				distNeg(rng) * boxHalfExtents.x,
				distNeg(rng) * boxHalfExtents.y,
				distNeg(rng) * boxHalfExtents.z
			};
		}

		return position;
	}

	/// @brief ランダムな放出速度を計算する
	/// @param rng 乱数生成器
	/// @param emitPos 放出位置
	/// @return 初期速度ベクトル
	template <typename Rng>
	[[nodiscard]] sgc::Vec3f sampleVelocity(Rng& rng,
		const sgc::Vec3f& emitPos) const noexcept
	{
		std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
		std::uniform_real_distribution<float> distNeg(-1.0f, 1.0f);

		const float speed = std::max(0.0f,
			initialSpeed + distNeg(rng) * initialSpeedVariance);

		sgc::Vec3f direction{0, 1, 0};

		switch (shape)
		{
		case EmissionShape::Point:
		{
			/// 全方向にランダム放出
			const float theta = dist01(rng) * 6.283185f;
			const float phi = std::acos(1.0f - 2.0f * dist01(rng));
			direction = sgc::Vec3f{
				std::sin(phi) * std::cos(theta),
				std::sin(phi) * std::sin(theta),
				std::cos(phi)
			};
			break;
		}

		case EmissionShape::Sphere:
		{
			/// 中心から外向き
			const auto offset = emitPos - position;
			const float len = offset.length();
			if (len > 1e-6f)
			{
				direction = offset * (1.0f / len);
			}
			break;
		}

		case EmissionShape::Cone:
		{
			/// コーン方向にランダム角度で放出
			const float theta = dist01(rng) * 6.283185f;
			const float cosAngle = std::cos(coneAngle * dist01(rng));
			const float sinAngle = std::sqrt(1.0f - cosAngle * cosAngle);
			direction = sgc::Vec3f{
				sinAngle * std::cos(theta),
				cosAngle,
				sinAngle * std::sin(theta)
			};
			break;
		}

		case EmissionShape::Box:
		{
			/// 上方向 + ランダム散布
			direction = sgc::Vec3f{
				distNeg(rng) * 0.3f,
				1.0f,
				distNeg(rng) * 0.3f
			}.normalized();
			break;
		}
		}

		return direction * speed;
	}

	/// @brief ランダムなライフタイムをサンプルする
	/// @param rng 乱数生成器
	/// @return ライフタイム（秒）
	template <typename Rng>
	[[nodiscard]] float sampleLifetime(Rng& rng) const noexcept
	{
		std::uniform_real_distribution<float> distNeg(-1.0f, 1.0f);
		return std::max(0.1f, lifetime + distNeg(rng) * lifetimeVariance);
	}

	/// @brief ランダムな初期サイズをサンプルする
	/// @param rng 乱数生成器
	/// @return 初期サイズ
	template <typename Rng>
	[[nodiscard]] float sampleSize(Rng& rng) const noexcept
	{
		std::uniform_real_distribution<float> distNeg(-1.0f, 1.0f);
		return std::max(0.001f, initialSize + distNeg(rng) * initialSizeVariance);
	}

	/// @brief ライフタイム比率に基づく色を計算する
	/// @param normalizedAge 正規化年齢（0.0〜1.0）
	/// @return 補間された色
	[[nodiscard]] sgc::Colorf evaluateColor(float normalizedAge) const noexcept
	{
		const float alphaScale = alphaOverLifetime.evaluate(normalizedAge);
		return sgc::Colorf{
			startColor.r + (endColor.r - startColor.r) * normalizedAge,
			startColor.g + (endColor.g - startColor.g) * normalizedAge,
			startColor.b + (endColor.b - startColor.b) * normalizedAge,
			(startColor.a + (endColor.a - startColor.a) * normalizedAge) * alphaScale
		};
	}

	/// @brief ライフタイム比率に基づくサイズ倍率を計算する
	/// @param normalizedAge 正規化年齢（0.0〜1.0）
	/// @return サイズ倍率
	[[nodiscard]] float evaluateSize(float normalizedAge) const noexcept
	{
		return sizeOverLifetime.evaluate(normalizedAge);
	}
};

} // namespace mitiru::effects
