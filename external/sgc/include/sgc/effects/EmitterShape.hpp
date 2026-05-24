#pragma once

/// @file EmitterShape.hpp
/// @brief エミッターシェイプ
///
/// パーティクル生成位置と方向を決定するエミッター形状。
/// 点・円・矩形・コーン型の4種類を提供する。
///
/// @code
/// sgc::CircleEmitter circle(50.0f, true);
/// auto [pos, dir] = circle.spawn(rng);
/// @endcode

#include <cmath>
#include <concepts>
#include <numbers>
#include <random>

namespace sgc
{

/// @brief エミッターのスポーン結果
struct SpawnResult
{
	float posX{0.0f};   ///< 生成位置X
	float posY{0.0f};   ///< 生成位置Y
	float dirX{0.0f};   ///< 放出方向X（正規化済み）
	float dirY{0.0f};   ///< 放出方向Y（正規化済み）
};

/// @brief エミッターシェイプのコンセプト
template <typename T>
concept EmitterShapeConcept = requires(T shape, std::mt19937& rng)
{
	{ shape.spawn(rng) } -> std::same_as<SpawnResult>;
};

/// @brief 点エミッター
///
/// 単一の点からパーティクルを全方向に放出する。
class PointEmitter
{
public:
	/// @brief 点エミッターを構築する
	/// @param x 中心X座標
	/// @param y 中心Y座標
	constexpr PointEmitter(float x = 0.0f, float y = 0.0f) noexcept
		: m_x(x), m_y(y)
	{
	}

	/// @brief パーティクルをスポーンする
	/// @param rng 乱数生成器
	/// @return スポーン結果（位置とランダム方向）
	template <typename RNG>
	SpawnResult spawn(RNG& rng) const
	{
		std::uniform_real_distribution<float> angleDist(
			0.0f, 2.0f * std::numbers::pi_v<float>);
		const float angle = angleDist(rng);
		return {m_x, m_y, std::cos(angle), std::sin(angle)};
	}

private:
	float m_x; ///< 中心X
	float m_y; ///< 中心Y
};

/// @brief 円形エミッター
///
/// 円の縁または内部からパーティクルを放出する。
class CircleEmitter
{
public:
	/// @brief 円形エミッターを構築する
	/// @param radius 半径
	/// @param filled trueなら内部も含む、falseなら縁のみ
	/// @param cx 中心X座標
	/// @param cy 中心Y座標
	constexpr CircleEmitter(float radius, bool filled = false,
		float cx = 0.0f, float cy = 0.0f) noexcept
		: m_radius(radius), m_filled(filled), m_cx(cx), m_cy(cy)
	{
	}

	/// @brief パーティクルをスポーンする
	/// @param rng 乱数生成器
	/// @return スポーン結果
	template <typename RNG>
	SpawnResult spawn(RNG& rng) const
	{
		std::uniform_real_distribution<float> angleDist(
			0.0f, 2.0f * std::numbers::pi_v<float>);
		const float angle = angleDist(rng);

		float r = m_radius;
		if (m_filled)
		{
			// 均等な面積分布のためsqrtを使う
			std::uniform_real_distribution<float> rDist(0.0f, 1.0f);
			r = m_radius * std::sqrt(rDist(rng));
		}

		const float px = m_cx + r * std::cos(angle);
		const float py = m_cy + r * std::sin(angle);

		// 方向は中心から外向き
		const float dirX = std::cos(angle);
		const float dirY = std::sin(angle);

		return {px, py, dirX, dirY};
	}

	/// @brief 半径を取得する
	/// @return 半径
	[[nodiscard]] constexpr float radius() const noexcept { return m_radius; }

	/// @brief 内部充填かどうか
	/// @return 充填フラグ
	[[nodiscard]] constexpr bool filled() const noexcept { return m_filled; }

private:
	float m_radius;  ///< 半径
	bool m_filled;   ///< 充填フラグ
	float m_cx;      ///< 中心X
	float m_cy;      ///< 中心Y
};

/// @brief 矩形エミッター
///
/// 矩形の縁または内部からパーティクルを放出する。
class RectEmitter
{
public:
	/// @brief 矩形エミッターを構築する
	/// @param width 幅
	/// @param height 高さ
	/// @param filled trueなら内部も含む
	/// @param cx 中心X座標
	/// @param cy 中心Y座標
	constexpr RectEmitter(float width, float height, bool filled = false,
		float cx = 0.0f, float cy = 0.0f) noexcept
		: m_width(width), m_height(height), m_filled(filled), m_cx(cx), m_cy(cy)
	{
	}

	/// @brief パーティクルをスポーンする
	/// @param rng 乱数生成器
	/// @return スポーン結果
	template <typename RNG>
	SpawnResult spawn(RNG& rng) const
	{
		float px, py;
		const float halfW = m_width * 0.5f;
		const float halfH = m_height * 0.5f;

		if (m_filled)
		{
			std::uniform_real_distribution<float> xDist(-halfW, halfW);
			std::uniform_real_distribution<float> yDist(-halfH, halfH);
			px = m_cx + xDist(rng);
			py = m_cy + yDist(rng);
		}
		else
		{
			// 辺の長さに比例して辺を選択
			const float perimeter = 2.0f * (m_width + m_height);
			std::uniform_real_distribution<float> perimDist(0.0f, perimeter);
			const float t = perimDist(rng);

			if (t < m_width)
			{
				// 上辺
				px = m_cx - halfW + t;
				py = m_cy - halfH;
			}
			else if (t < m_width + m_height)
			{
				// 右辺
				px = m_cx + halfW;
				py = m_cy - halfH + (t - m_width);
			}
			else if (t < 2.0f * m_width + m_height)
			{
				// 下辺
				px = m_cx + halfW - (t - m_width - m_height);
				py = m_cy + halfH;
			}
			else
			{
				// 左辺
				px = m_cx - halfW;
				py = m_cy + halfH - (t - 2.0f * m_width - m_height);
			}
		}

		// 方向は中心からの外向き（中心と同じ位置なら上向き）
		const float dx = px - m_cx;
		const float dy = py - m_cy;
		const float len = std::sqrt(dx * dx + dy * dy);

		float dirX = 0.0f;
		float dirY = -1.0f;
		if (len > 1e-6f)
		{
			dirX = dx / len;
			dirY = dy / len;
		}

		return {px, py, dirX, dirY};
	}

	/// @brief 幅を取得する
	/// @return 幅
	[[nodiscard]] constexpr float width() const noexcept { return m_width; }

	/// @brief 高さを取得する
	/// @return 高さ
	[[nodiscard]] constexpr float height() const noexcept { return m_height; }

private:
	float m_width;   ///< 幅
	float m_height;  ///< 高さ
	bool m_filled;   ///< 充填フラグ
	float m_cx;      ///< 中心X
	float m_cy;      ///< 中心Y
};

/// @brief コーン型エミッター
///
/// 指定角度の円錐範囲内にパーティクルを放出する。
class ConeEmitter
{
public:
	/// @brief コーン型エミッターを構築する
	/// @param angle コーン半角（ラジアン）
	/// @param baseAngle 基準角度（ラジアン、0=右方向）
	/// @param x 原点X座標
	/// @param y 原点Y座標
	constexpr ConeEmitter(float angle, float baseAngle = 0.0f,
		float x = 0.0f, float y = 0.0f) noexcept
		: m_halfAngle(angle), m_baseAngle(baseAngle), m_x(x), m_y(y)
	{
	}

	/// @brief パーティクルをスポーンする
	/// @param rng 乱数生成器
	/// @return スポーン結果
	template <typename RNG>
	SpawnResult spawn(RNG& rng) const
	{
		std::uniform_real_distribution<float> angleDist(
			m_baseAngle - m_halfAngle, m_baseAngle + m_halfAngle);
		const float angle = angleDist(rng);

		return {m_x, m_y, std::cos(angle), std::sin(angle)};
	}

	/// @brief コーン半角を取得する
	/// @return 半角（ラジアン）
	[[nodiscard]] constexpr float halfAngle() const noexcept { return m_halfAngle; }

	/// @brief 基準角度を取得する
	/// @return 基準角度（ラジアン）
	[[nodiscard]] constexpr float baseAngle() const noexcept { return m_baseAngle; }

private:
	float m_halfAngle;  ///< コーン半角
	float m_baseAngle;  ///< 基準角度
	float m_x;          ///< 原点X
	float m_y;          ///< 原点Y
};

} // namespace sgc
