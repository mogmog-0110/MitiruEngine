#pragma once

/// @file ScreenEffects.hpp
/// @brief VN用スクリーンエフェクトシステム
/// @details 画面全体に適用する演出エフェクト群。シェイク、フラッシュ、フェード、
///          雨・雪パーティクル、ブラー、ビネット、色フィルタ、レターボックス、ズームを
///          スタック型マネージャで同時発動・合成する。
///
/// @code
/// mitiru::vn::ScreenEffectManager effects;
/// effects.shake(0.6f, 0.4f, 30.0f);
/// effects.rain(200, 800.0f, -10.0f);
/// effects.vignette(0.7f, 0.6f);
/// // 毎フレーム:
/// effects.update(dt);
/// auto result = effects.composite(screenW, screenH);
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <sgc/types/Color.hpp>

namespace mitiru::vn
{

// ── エフェクト結果 ──────────────────────────────────────────────

/// @brief パーティクル1粒の描画データ
struct EffectParticle
{
	float x = 0.0f;          ///< X座標
	float y = 0.0f;          ///< Y座標
	float width = 1.0f;      ///< 幅
	float height = 4.0f;     ///< 高さ
	sgc::Colorf color{1.0f, 1.0f, 1.0f, 0.5f}; ///< 色
};

/// @brief カラーフィルタの種別
enum class ColorFilterType : std::uint8_t
{
	None,       ///< フィルタなし
	Sepia,      ///< セピア調
	Grayscale,  ///< グレースケール
};

/// @brief 全エフェクトの合成結果
/// @details 各フレームでレンダラに渡して描画に反映する。
struct ScreenEffectResult
{
	float offsetX = 0.0f;         ///< 画面X方向オフセット
	float offsetY = 0.0f;         ///< 画面Y方向オフセット
	sgc::Colorf overlayColor{0.0f, 0.0f, 0.0f, 0.0f}; ///< オーバーレイ色
	float overlayAlpha = 0.0f;    ///< オーバーレイ不透明度
	ColorFilterType colorFilter = ColorFilterType::None; ///< 色フィルタ
	float filterIntensity = 0.0f; ///< フィルタ強度 [0,1]
	float vignetteIntensity = 0.0f; ///< ビネット強度
	float vignetteRadius = 0.5f;    ///< ビネット半径
	float letterboxRatio = 0.0f;    ///< レターボックスバー高さ比 [0,1]
	float zoomFactor = 1.0f;        ///< ズーム倍率
	float zoomCenterX = 0.5f;       ///< ズーム中心X [0,1]
	float zoomCenterY = 0.5f;       ///< ズーム中心Y [0,1]
	float blurIntensity = 0.0f;     ///< ブラー強度 [0,1]
	std::vector<EffectParticle> particles; ///< パーティクル群
};

// ── エフェクト基底 ──────────────────────────────────────────────

/// @brief スクリーンエフェクトの基底クラス
class ScreenEffect
{
public:
	virtual ~ScreenEffect() = default;

	/// @brief エフェクトを更新する
	/// @param dt デルタタイム（秒）
	virtual void update(float dt) = 0;

	/// @brief エフェクトが完了したか
	/// @return 完了ならtrue
	[[nodiscard]] virtual bool isComplete() const noexcept = 0;

	/// @brief 結果を合成に反映する
	/// @param result 合成結果（in/out）
	/// @param screenW 画面幅
	/// @param screenH 画面高さ
	virtual void apply(ScreenEffectResult& result, float screenW, float screenH) const = 0;
};

// ── シェイクエフェクト ──────────────────────────────────────────

/// @brief シェイクの振動方向
enum class ShakeAxis : std::uint8_t
{
	Both,       ///< 水平＋垂直
	Horizontal, ///< 水平のみ
	Vertical,   ///< 垂直のみ
};

/// @brief 画面シェイクエフェクト
/// @details 設定可能な強度・持続時間・周波数を持ち、減衰と定常の2モードを提供する。
class ShakeEffect final : public ScreenEffect
{
	float m_intensity;          ///< 最大オフセット（ピクセル）
	float m_duration;           ///< 持続時間（秒）
	float m_frequency;          ///< 振動周波数（Hz）
	float m_elapsed = 0.0f;     ///< 経過時間
	bool m_decaying;            ///< 減衰モードか定常モードか
	ShakeAxis m_axis;           ///< 振動方向
	std::uint32_t m_seed = 0;   ///< ノイズシード

public:
	/// @brief シェイクを構築する
	/// @param intensity 最大ピクセルオフセット
	/// @param duration 持続時間（秒）
	/// @param frequency 振動周波数（Hz）
	/// @param decaying 減衰モード（falseで定常）
	/// @param axis 振動方向
	ShakeEffect(float intensity, float duration, float frequency = 30.0f,
	            bool decaying = true, ShakeAxis axis = ShakeAxis::Both) noexcept
		: m_intensity(intensity)
		, m_duration(std::max(0.001f, duration))
		, m_frequency(frequency)
		, m_decaying(decaying)
		, m_axis(axis)
	{
	}

	void update(float dt) override
	{
		m_elapsed += dt;
		m_seed += static_cast<std::uint32_t>(m_frequency);
	}

	[[nodiscard]] bool isComplete() const noexcept override
	{
		return m_elapsed >= m_duration;
	}

	void apply(ScreenEffectResult& result, float /*screenW*/, float /*screenH*/) const override
	{
		if (isComplete()) { return; }
		const float t = m_elapsed / m_duration;
		const float amplitude = m_decaying ? m_intensity * (1.0f - t) : m_intensity;
		if (amplitude <= 0.0f) { return; }

		const float nx = hashNoise(m_seed);
		const float ny = hashNoise(m_seed + 10000);

		if (m_axis != ShakeAxis::Vertical)
		{
			result.offsetX += amplitude * nx;
		}
		if (m_axis != ShakeAxis::Horizontal)
		{
			result.offsetY += amplitude * ny;
		}
	}

private:
	[[nodiscard]] static float hashNoise(std::uint32_t seed) noexcept
	{
		std::uint32_t h = seed;
		h ^= h >> 16;
		h *= 0x45d9f3bU;
		h ^= h >> 16;
		h *= 0x45d9f3bU;
		h ^= h >> 16;
		return static_cast<float>(h & 0xFFFF) / 32767.5f - 1.0f;
	}
};

// ── フラッシュエフェクト ────────────────────────────────────────

/// @brief フルスクリーンカラーフラッシュ
/// @details 設定色で画面全体を覆い、指定時間でフェードアウトする。
class FlashEffect final : public ScreenEffect
{
	sgc::Colorf m_color;
	float m_duration;
	float m_elapsed = 0.0f;

public:
	/// @brief フラッシュを構築する
	/// @param color フラッシュ色（デフォルト白）
	/// @param duration 持続時間（秒）
	FlashEffect(const sgc::Colorf& color = {1.0f, 1.0f, 1.0f, 1.0f},
	            float duration = 0.3f) noexcept
		: m_color(color)
		, m_duration(std::max(0.001f, duration))
	{
	}

	void update(float dt) override { m_elapsed += dt; }

	[[nodiscard]] bool isComplete() const noexcept override
	{
		return m_elapsed >= m_duration;
	}

	void apply(ScreenEffectResult& result, float /*screenW*/, float /*screenH*/) const override
	{
		if (isComplete()) { return; }
		const float t = std::clamp(m_elapsed / m_duration, 0.0f, 1.0f);
		const float alpha = m_color.a * (1.0f - t);
		if (alpha <= 0.0f) { return; }

		// オーバーレイの加算合成
		result.overlayColor = sgc::Colorf{
			std::max(result.overlayColor.r, m_color.r),
			std::max(result.overlayColor.g, m_color.g),
			std::max(result.overlayColor.b, m_color.b),
			1.0f
		};
		result.overlayAlpha = std::max(result.overlayAlpha, alpha);
	}
};

// ── フェードエフェクト ──────────────────────────────────────────

/// @brief フェード方向
enum class FadeDirection : std::uint8_t
{
	In,   ///< フェードイン（色→透明）
	Out,  ///< フェードアウト（透明→色）
};

/// @brief スクリーンフェードエフェクト
/// @details シーン遷移などで画面全体を任意色でフェードイン/アウトする。
class FadeEffect final : public ScreenEffect
{
	sgc::Colorf m_color;
	float m_duration;
	float m_elapsed = 0.0f;
	FadeDirection m_direction;

public:
	/// @brief フェードを構築する
	/// @param direction フェード方向
	/// @param color フェード色
	/// @param duration 持続時間（秒）
	FadeEffect(FadeDirection direction,
	           const sgc::Colorf& color = {0.0f, 0.0f, 0.0f, 1.0f},
	           float duration = 1.0f) noexcept
		: m_color(color)
		, m_duration(std::max(0.001f, duration))
		, m_direction(direction)
	{
	}

	void update(float dt) override { m_elapsed += dt; }

	[[nodiscard]] bool isComplete() const noexcept override
	{
		return m_elapsed >= m_duration;
	}

	void apply(ScreenEffectResult& result, float /*screenW*/, float /*screenH*/) const override
	{
		if (isComplete()) { return; }
		const float t = std::clamp(m_elapsed / m_duration, 0.0f, 1.0f);
		const float alpha = (m_direction == FadeDirection::In)
			? m_color.a * (1.0f - t)
			: m_color.a * t;
		if (alpha <= 0.0f) { return; }

		result.overlayColor = sgc::Colorf{
			std::max(result.overlayColor.r, m_color.r),
			std::max(result.overlayColor.g, m_color.g),
			std::max(result.overlayColor.b, m_color.b),
			1.0f
		};
		result.overlayAlpha = std::max(result.overlayAlpha, alpha);
	}
};

// ── 雨エフェクト ────────────────────────────────────────────────

/// @brief パーティクルベースの雨エフェクト
/// @details 画面上部からパーティクルを降らせ、画面下部でスプラッシュを生成する。
class RainEffect final : public ScreenEffect
{
	struct RainDrop
	{
		float x = 0.0f;
		float y = 0.0f;
		float speed = 0.0f;
		float length = 0.0f;
		float alpha = 0.0f;
	};

	struct Splash
	{
		float x = 0.0f;
		float y = 0.0f;
		float lifetime = 0.0f;
		float maxLifetime = 0.15f;
	};

	std::vector<RainDrop> m_drops;
	std::vector<Splash> m_splashes;
	std::mt19937 m_rng;
	std::uint32_t m_density;     ///< 同時粒子数
	float m_speed;               ///< 落下速度（px/s）
	float m_angle;               ///< 角度（度）。0=垂直、正=右傾斜
	bool m_splashEnabled;        ///< 下部スプラッシュの有無
	float m_duration;            ///< 持続時間（0で無限）
	float m_elapsed = 0.0f;

public:
	/// @brief 雨を構築する
	/// @param density 同時パーティクル数
	/// @param speed 落下速度（px/s）
	/// @param angle 傾斜角（度）
	/// @param splashOnBottom 下部スプラッシュを描くか
	/// @param duration 持続時間（0=無限）
	RainEffect(std::uint32_t density = 200, float speed = 800.0f,
	           float angle = -10.0f, bool splashOnBottom = true,
	           float duration = 0.0f) noexcept
		: m_rng(42)
		, m_density(density)
		, m_speed(speed)
		, m_angle(angle)
		, m_splashEnabled(splashOnBottom)
		, m_duration(duration)
	{
	}

	void update(float dt) override
	{
		m_elapsed += dt;

		// 不足分を補充
		std::uniform_real_distribution<float> distX(0.0f, 1.0f);
		std::uniform_real_distribution<float> distSpeed(0.8f, 1.2f);
		std::uniform_real_distribution<float> distAlpha(0.3f, 0.7f);
		std::uniform_real_distribution<float> distLen(8.0f, 20.0f);

		while (m_drops.size() < m_density)
		{
			RainDrop drop;
			drop.x = distX(m_rng);
			drop.y = -distX(m_rng) * 0.3f; // 画面上部外から
			drop.speed = m_speed * distSpeed(m_rng);
			drop.length = distLen(m_rng);
			drop.alpha = distAlpha(m_rng);
			m_drops.push_back(drop);
		}

		const float angleRad = m_angle * 3.14159265f / 180.0f;
		const float dx = std::sin(angleRad);
		const float dy = std::cos(angleRad);

		// 雨粒の更新
		for (auto it = m_drops.begin(); it != m_drops.end(); )
		{
			it->x += dx * it->speed * dt / 1920.0f;
			it->y += dy * it->speed * dt / 1080.0f;

			if (it->y > 1.0f)
			{
				if (m_splashEnabled)
				{
					Splash splash;
					splash.x = it->x;
					splash.y = 1.0f;
					splash.lifetime = 0.0f;
					m_splashes.push_back(splash);
				}
				it = m_drops.erase(it);
			}
			else
			{
				++it;
			}
		}

		// スプラッシュの更新
		for (auto it = m_splashes.begin(); it != m_splashes.end(); )
		{
			it->lifetime += dt;
			if (it->lifetime >= it->maxLifetime)
			{
				it = m_splashes.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	[[nodiscard]] bool isComplete() const noexcept override
	{
		return m_duration > 0.0f && m_elapsed >= m_duration;
	}

	void apply(ScreenEffectResult& result, float screenW, float screenH) const override
	{
		result.particles.reserve(result.particles.size() + m_drops.size() + m_splashes.size());

		for (const auto& drop : m_drops)
		{
			EffectParticle p;
			p.x = drop.x * screenW;
			p.y = drop.y * screenH;
			p.width = 1.5f;
			p.height = drop.length;
			p.color = sgc::Colorf{0.7f, 0.75f, 0.85f, drop.alpha};
			result.particles.push_back(p);
		}

		for (const auto& splash : m_splashes)
		{
			const float t = splash.lifetime / splash.maxLifetime;
			const float size = 3.0f * (1.0f - t);
			EffectParticle p;
			p.x = splash.x * screenW;
			p.y = splash.y * screenH;
			p.width = size;
			p.height = size * 0.3f;
			p.color = sgc::Colorf{0.7f, 0.75f, 0.85f, 0.4f * (1.0f - t)};
			result.particles.push_back(p);
		}
	}
};

// ── 雪エフェクト ────────────────────────────────────────────────

/// @brief パーティクルベースの雪エフェクト
/// @details ゆっくり落下し横風で揺れる雪片を描画する。
class SnowEffect final : public ScreenEffect
{
	struct Snowflake
	{
		float x = 0.0f;
		float y = 0.0f;
		float speed = 0.0f;
		float size = 0.0f;
		float alpha = 0.0f;
		float wobblePhase = 0.0f; ///< 横揺れ位相
	};

	std::vector<Snowflake> m_flakes;
	std::mt19937 m_rng;
	std::uint32_t m_density;
	float m_speed;
	float m_wind;               ///< 横風速度（px/s）
	float m_sizeVariation;      ///< サイズのばらつき
	float m_duration;
	float m_elapsed = 0.0f;

public:
	/// @brief 雪を構築する
	/// @param density 同時パーティクル数
	/// @param speed 落下速度（px/s）
	/// @param wind 横風速度（px/s、正=右方向）
	/// @param sizeVariation サイズばらつき [0,1]
	/// @param duration 持続時間（0=無限）
	SnowEffect(std::uint32_t density = 150, float speed = 60.0f,
	           float wind = 20.0f, float sizeVariation = 0.5f,
	           float duration = 0.0f) noexcept
		: m_rng(77)
		, m_density(density)
		, m_speed(speed)
		, m_wind(wind)
		, m_sizeVariation(std::clamp(sizeVariation, 0.0f, 1.0f))
		, m_duration(duration)
	{
	}

	void update(float dt) override
	{
		m_elapsed += dt;

		std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
		std::uniform_real_distribution<float> distSpeed(0.6f, 1.4f);
		std::uniform_real_distribution<float> distAlpha(0.4f, 0.9f);
		std::uniform_real_distribution<float> distPhase(0.0f, 6.283f);

		while (m_flakes.size() < m_density)
		{
			Snowflake flake;
			flake.x = dist01(m_rng);
			flake.y = -dist01(m_rng) * 0.2f;
			flake.speed = m_speed * distSpeed(m_rng);
			flake.size = 2.0f + m_sizeVariation * dist01(m_rng) * 4.0f;
			flake.alpha = distAlpha(m_rng);
			flake.wobblePhase = distPhase(m_rng);
			m_flakes.push_back(flake);
		}

		for (auto it = m_flakes.begin(); it != m_flakes.end(); )
		{
			it->y += it->speed * dt / 1080.0f;
			it->x += (m_wind + std::sin(m_elapsed * 2.0f + it->wobblePhase) * 15.0f) * dt / 1920.0f;
			it->wobblePhase += dt * 1.5f;

			if (it->y > 1.05f || it->x < -0.05f || it->x > 1.05f)
			{
				it = m_flakes.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	[[nodiscard]] bool isComplete() const noexcept override
	{
		return m_duration > 0.0f && m_elapsed >= m_duration;
	}

	void apply(ScreenEffectResult& result, float screenW, float screenH) const override
	{
		result.particles.reserve(result.particles.size() + m_flakes.size());

		for (const auto& flake : m_flakes)
		{
			EffectParticle p;
			p.x = flake.x * screenW;
			p.y = flake.y * screenH;
			p.width = flake.size;
			p.height = flake.size;
			p.color = sgc::Colorf{0.95f, 0.95f, 1.0f, flake.alpha};
			result.particles.push_back(p);
		}
	}
};

// ── ブラーエフェクト ────────────────────────────────────────────

/// @brief ピクセレーションベースの擬似ブラー
/// @details シェーダを使わずピクセル化で視覚的なぼけを表現する。
class BlurEffect final : public ScreenEffect
{
	float m_intensity;       ///< ブラー強度 [0,1]
	float m_duration;
	float m_elapsed = 0.0f;
	bool m_fadeIn;           ///< trueなら0→intensity、falseならintensity→0

public:
	/// @brief ブラーを構築する
	/// @param intensity ブラー強度 [0,1]
	/// @param duration 持続時間（秒）
	/// @param fadeIn フェードイン（trueで0から増加）
	BlurEffect(float intensity = 0.5f, float duration = 0.5f,
	           bool fadeIn = true) noexcept
		: m_intensity(std::clamp(intensity, 0.0f, 1.0f))
		, m_duration(std::max(0.001f, duration))
		, m_fadeIn(fadeIn)
	{
	}

	void update(float dt) override { m_elapsed += dt; }

	[[nodiscard]] bool isComplete() const noexcept override
	{
		return m_elapsed >= m_duration;
	}

	void apply(ScreenEffectResult& result, float /*screenW*/, float /*screenH*/) const override
	{
		if (isComplete()) { return; }
		const float t = std::clamp(m_elapsed / m_duration, 0.0f, 1.0f);
		const float currentIntensity = m_fadeIn
			? m_intensity * t
			: m_intensity * (1.0f - t);
		result.blurIntensity = std::max(result.blurIntensity, currentIntensity);
	}
};

// ── ビネットエフェクト ──────────────────────────────────────────

/// @brief 画面端を暗くするビネットエフェクト
class VignetteEffect final : public ScreenEffect
{
	float m_intensity;
	float m_radius;
	float m_duration;
	float m_elapsed = 0.0f;
	float m_fadeInTime;       ///< フェードイン時間（秒）

public:
	/// @brief ビネットを構築する
	/// @param intensity 暗さの強度 [0,1]
	/// @param radius ビネットの半径 [0,1]（1=画面全体の半分）
	/// @param duration 持続時間（0=無限）
	/// @param fadeInTime フェードインにかける秒数
	VignetteEffect(float intensity = 0.7f, float radius = 0.6f,
	               float duration = 0.0f, float fadeInTime = 0.3f) noexcept
		: m_intensity(std::clamp(intensity, 0.0f, 1.0f))
		, m_radius(std::clamp(radius, 0.0f, 1.0f))
		, m_duration(duration)
		, m_fadeInTime(std::max(0.001f, fadeInTime))
	{
	}

	void update(float dt) override { m_elapsed += dt; }

	[[nodiscard]] bool isComplete() const noexcept override
	{
		return m_duration > 0.0f && m_elapsed >= m_duration;
	}

	void apply(ScreenEffectResult& result, float /*screenW*/, float /*screenH*/) const override
	{
		if (isComplete()) { return; }
		const float fade = std::clamp(m_elapsed / m_fadeInTime, 0.0f, 1.0f);
		result.vignetteIntensity = std::max(result.vignetteIntensity, m_intensity * fade);
		result.vignetteRadius = std::min(result.vignetteRadius, m_radius);
	}
};

// ── セピア/グレースケールフィルタ ───────────────────────────────

/// @brief 色フィルタエフェクト（セピア・グレースケール）
/// @details フラッシュバックシーンなどに使用する。
class ColorFilterEffect final : public ScreenEffect
{
	ColorFilterType m_filter;
	float m_intensity;
	float m_duration;
	float m_elapsed = 0.0f;
	float m_fadeInTime;

public:
	/// @brief 色フィルタを構築する
	/// @param filter フィルタ種別
	/// @param intensity フィルタ強度 [0,1]
	/// @param duration 持続時間（0=無限）
	/// @param fadeInTime フェードイン秒数
	ColorFilterEffect(ColorFilterType filter, float intensity = 1.0f,
	                  float duration = 0.0f, float fadeInTime = 0.5f) noexcept
		: m_filter(filter)
		, m_intensity(std::clamp(intensity, 0.0f, 1.0f))
		, m_duration(duration)
		, m_fadeInTime(std::max(0.001f, fadeInTime))
	{
	}

	void update(float dt) override { m_elapsed += dt; }

	[[nodiscard]] bool isComplete() const noexcept override
	{
		return m_duration > 0.0f && m_elapsed >= m_duration;
	}

	void apply(ScreenEffectResult& result, float /*screenW*/, float /*screenH*/) const override
	{
		if (isComplete()) { return; }
		const float fade = std::clamp(m_elapsed / m_fadeInTime, 0.0f, 1.0f);
		const float currentIntensity = m_intensity * fade;
		if (currentIntensity > result.filterIntensity)
		{
			result.colorFilter = m_filter;
			result.filterIntensity = currentIntensity;
		}
	}
};

// ── レターボックスエフェクト ────────────────────────────────────

/// @brief シネマティックレターボックス（上下黒帯）
/// @details アニメーションで黒帯を表示/非表示する。
class LetterboxEffect final : public ScreenEffect
{
	float m_targetRatio;     ///< 目標バー高さ比 [0,1]（画面高さに対する片側の比率）
	float m_duration;
	float m_elapsed = 0.0f;
	bool m_opening;          ///< true=バーを広げる、false=バーを閉じる

public:
	/// @brief レターボックスを構築する
	/// @param barRatio バーの高さ比（片側、例: 0.1 = 画面高さの10%ずつ上下）
	/// @param duration アニメーション時間（秒）
	/// @param opening trueでバーを広げる
	LetterboxEffect(float barRatio = 0.1f, float duration = 0.5f,
	                bool opening = true) noexcept
		: m_targetRatio(std::clamp(barRatio, 0.0f, 0.5f))
		, m_duration(std::max(0.001f, duration))
		, m_opening(opening)
	{
	}

	void update(float dt) override { m_elapsed += dt; }

	[[nodiscard]] bool isComplete() const noexcept override
	{
		return m_elapsed >= m_duration;
	}

	void apply(ScreenEffectResult& result, float /*screenW*/, float /*screenH*/) const override
	{
		const float t = std::clamp(m_elapsed / m_duration, 0.0f, 1.0f);
		// イージング（ease-out quadratic）
		const float eased = 1.0f - (1.0f - t) * (1.0f - t);
		const float ratio = m_opening
			? m_targetRatio * eased
			: m_targetRatio * (1.0f - eased);
		result.letterboxRatio = std::max(result.letterboxRatio, ratio);
	}
};

// ── ズームエフェクト ────────────────────────────────────────────

/// @brief 画面ズームエフェクト
/// @details 指定座標に向かって画面をズームする。
class ZoomEffect final : public ScreenEffect
{
	float m_targetZoom;
	float m_centerX;         ///< ズーム中心X [0,1]
	float m_centerY;         ///< ズーム中心Y [0,1]
	float m_duration;
	float m_elapsed = 0.0f;
	float m_startZoom;

public:
	/// @brief ズームを構築する
	/// @param targetZoom 目標ズーム倍率
	/// @param centerX ズーム中心X [0,1]
	/// @param centerY ズーム中心Y [0,1]
	/// @param duration 持続時間（秒）
	/// @param startZoom 開始ズーム倍率
	ZoomEffect(float targetZoom = 1.5f, float centerX = 0.5f,
	           float centerY = 0.5f, float duration = 1.0f,
	           float startZoom = 1.0f) noexcept
		: m_targetZoom(targetZoom)
		, m_centerX(std::clamp(centerX, 0.0f, 1.0f))
		, m_centerY(std::clamp(centerY, 0.0f, 1.0f))
		, m_duration(std::max(0.001f, duration))
		, m_startZoom(startZoom)
	{
	}

	void update(float dt) override { m_elapsed += dt; }

	[[nodiscard]] bool isComplete() const noexcept override
	{
		return m_elapsed >= m_duration;
	}

	void apply(ScreenEffectResult& result, float /*screenW*/, float /*screenH*/) const override
	{
		const float t = std::clamp(m_elapsed / m_duration, 0.0f, 1.0f);
		// ease-in-out cubic
		const float eased = (t < 0.5f)
			? 4.0f * t * t * t
			: 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
		const float zoom = m_startZoom + (m_targetZoom - m_startZoom) * eased;
		result.zoomFactor = zoom;
		result.zoomCenterX = m_centerX;
		result.zoomCenterY = m_centerY;
	}
};

// ── エフェクトマネージャ ────────────────────────────────────────

/// @brief スクリーンエフェクトのスタック管理と合成
/// @details 複数のエフェクトを同時発動し、結果を1フレーム分合成する。
///          完了したエフェクトは自動的に除去される。
class ScreenEffectManager
{
	std::vector<std::unique_ptr<ScreenEffect>> m_effects;

public:
	/// @brief 全エフェクトを更新し、完了したものを除去する
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		for (auto& effect : m_effects)
		{
			effect->update(dt);
		}
		m_effects.erase(
			std::remove_if(m_effects.begin(), m_effects.end(),
				[](const std::unique_ptr<ScreenEffect>& e) { return e->isComplete(); }),
			m_effects.end());
	}

	/// @brief 全エフェクトの結果を合成して返す
	/// @param screenW 画面幅
	/// @param screenH 画面高さ
	/// @return 合成結果
	[[nodiscard]] ScreenEffectResult composite(float screenW, float screenH) const
	{
		ScreenEffectResult result;
		for (const auto& effect : m_effects)
		{
			effect->apply(result, screenW, screenH);
		}
		return result;
	}

	/// @brief アクティブなエフェクト数
	[[nodiscard]] std::size_t activeCount() const noexcept
	{
		return m_effects.size();
	}

	/// @brief 全エフェクトを即座に除去する
	void clear() noexcept
	{
		m_effects.clear();
	}

	/// @brief 任意のエフェクトを追加する
	/// @param effect エフェクトの所有権
	void addEffect(std::unique_ptr<ScreenEffect> effect)
	{
		m_effects.push_back(std::move(effect));
	}

	// ── 便利メソッド ────────────────────────────────────────

	/// @brief シェイクを開始する
	/// @param intensity 強度（ピクセル）
	/// @param duration 持続時間（秒）
	/// @param frequency 周波数（Hz）
	/// @param decaying 減衰モード
	/// @param axis 振動方向
	void shake(float intensity = 10.0f, float duration = 0.4f,
	           float frequency = 30.0f, bool decaying = true,
	           ShakeAxis axis = ShakeAxis::Both)
	{
		m_effects.push_back(
			std::make_unique<ShakeEffect>(intensity, duration, frequency, decaying, axis));
	}

	/// @brief フラッシュを開始する
	/// @param color フラッシュ色
	/// @param duration 持続時間（秒）
	void flash(const sgc::Colorf& color = {1.0f, 1.0f, 1.0f, 1.0f},
	           float duration = 0.3f)
	{
		m_effects.push_back(
			std::make_unique<FlashEffect>(color, duration));
	}

	/// @brief フェードを開始する
	/// @param direction フェード方向
	/// @param color フェード色
	/// @param duration 持続時間（秒）
	void fade(FadeDirection direction,
	          const sgc::Colorf& color = {0.0f, 0.0f, 0.0f, 1.0f},
	          float duration = 1.0f)
	{
		m_effects.push_back(
			std::make_unique<FadeEffect>(direction, color, duration));
	}

	/// @brief 雨を開始する
	/// @param density パーティクル密度
	/// @param speed 落下速度
	/// @param angle 傾斜角（度）
	/// @param splashOnBottom スプラッシュ有無
	/// @param duration 持続時間（0=無限）
	void rain(std::uint32_t density = 200, float speed = 800.0f,
	          float angle = -10.0f, bool splashOnBottom = true,
	          float duration = 0.0f)
	{
		m_effects.push_back(
			std::make_unique<RainEffect>(density, speed, angle, splashOnBottom, duration));
	}

	/// @brief 雪を開始する
	/// @param density パーティクル密度
	/// @param speed 落下速度
	/// @param wind 横風速度
	/// @param sizeVariation サイズばらつき
	/// @param duration 持続時間（0=無限）
	void snow(std::uint32_t density = 150, float speed = 60.0f,
	          float wind = 20.0f, float sizeVariation = 0.5f,
	          float duration = 0.0f)
	{
		m_effects.push_back(
			std::make_unique<SnowEffect>(density, speed, wind, sizeVariation, duration));
	}

	/// @brief ブラーを開始する
	/// @param intensity ブラー強度
	/// @param duration 持続時間
	/// @param fadeIn フェードインするか
	void blur(float intensity = 0.5f, float duration = 0.5f, bool fadeIn = true)
	{
		m_effects.push_back(
			std::make_unique<BlurEffect>(intensity, duration, fadeIn));
	}

	/// @brief ビネットを開始する
	/// @param intensity 暗さ強度
	/// @param radius ビネット半径
	/// @param duration 持続時間（0=無限）
	/// @param fadeInTime フェードイン秒数
	void vignette(float intensity = 0.7f, float radius = 0.6f,
	              float duration = 0.0f, float fadeInTime = 0.3f)
	{
		m_effects.push_back(
			std::make_unique<VignetteEffect>(intensity, radius, duration, fadeInTime));
	}

	/// @brief セピアフィルタを開始する
	/// @param intensity 強度
	/// @param duration 持続時間（0=無限）
	void sepia(float intensity = 1.0f, float duration = 0.0f)
	{
		m_effects.push_back(
			std::make_unique<ColorFilterEffect>(ColorFilterType::Sepia, intensity, duration));
	}

	/// @brief グレースケールフィルタを開始する
	/// @param intensity 強度
	/// @param duration 持続時間（0=無限）
	void grayscale(float intensity = 1.0f, float duration = 0.0f)
	{
		m_effects.push_back(
			std::make_unique<ColorFilterEffect>(ColorFilterType::Grayscale, intensity, duration));
	}

	/// @brief レターボックスを表示する
	/// @param barRatio バー高さ比
	/// @param duration アニメーション時間
	void showLetterbox(float barRatio = 0.1f, float duration = 0.5f)
	{
		m_effects.push_back(
			std::make_unique<LetterboxEffect>(barRatio, duration, true));
	}

	/// @brief レターボックスを閉じる
	/// @param barRatio 現在のバー高さ比
	/// @param duration アニメーション時間
	void hideLetterbox(float barRatio = 0.1f, float duration = 0.5f)
	{
		m_effects.push_back(
			std::make_unique<LetterboxEffect>(barRatio, duration, false));
	}

	/// @brief ズームを開始する
	/// @param targetZoom 目標倍率
	/// @param centerX ズーム中心X [0,1]
	/// @param centerY ズーム中心Y [0,1]
	/// @param duration 持続時間
	void zoom(float targetZoom = 1.5f, float centerX = 0.5f,
	          float centerY = 0.5f, float duration = 1.0f)
	{
		m_effects.push_back(
			std::make_unique<ZoomEffect>(targetZoom, centerX, centerY, duration));
	}
};

} // namespace mitiru::vn
