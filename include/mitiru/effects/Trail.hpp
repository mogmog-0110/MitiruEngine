#pragma once

/// @file Trail.hpp
/// @brief トレイルエフェクト（Siv3D Trail風）
/// @details 移動するオブジェクトの軌跡を残留する尾として描画する。
///          時間経過で各ポイントがフェードアウトする。
///
/// @code
/// mitiru::effects::Trail trail;
/// trail.maxPoints = 50;
/// trail.lifetime = 0.5f;
/// // 毎フレーム:
/// trail.addPoint(playerPos);
/// trail.update(dt);
/// trail.draw(screen, {0, 1, 0.5f, 1}, 4.0f);
/// @endcode

#include <algorithm>
#include <vector>

#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>

namespace mitiru
{
class Screen;
} // namespace mitiru

namespace mitiru::effects
{

/// @brief トレイルポイント
struct TrailPoint
{
	sgc::Vec2f position; ///< 位置
	float age = 0;       ///< 経過時間（秒）
};

/// @brief トレイルエフェクト（軌跡描画）
/// @details 移動オブジェクトの位置を記録し、フェードアウトする軌跡を描画する。
class Trail
{
public:
	/// @brief 最大ポイント数
	int maxPoints = 50;

	/// @brief ポイントの生存時間（秒）
	float lifetime = 0.5f;

	/// @brief 新しいポイントを追加する
	/// @param pos 現在位置
	void addPoint(const sgc::Vec2f& pos)
	{
		m_points.push_back(TrailPoint{pos, 0});
		if (static_cast<int>(m_points.size()) > maxPoints)
		{
			m_points.erase(m_points.begin());
		}
	}

	/// @brief 更新する（経過時間を加算し、寿命切れを除去する）
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		for (auto& p : m_points)
		{
			p.age += dt;
		}
		// 寿命切れのポイントを除去する
		m_points.erase(
			std::remove_if(m_points.begin(), m_points.end(),
				[this](const TrailPoint& p) { return p.age >= lifetime; }),
			m_points.end());
	}

	/// @brief トレイルを描画する（線分の連続）
	/// @param screen 描画先サーフェス
	/// @param color ベース色（アルファはフェードアウト）
	/// @param thickness 線の太さ
	void draw(Screen& screen, const sgc::Colorf& color,
		float thickness = 2.0f) const;

	/// @brief ポイント数を取得する
	[[nodiscard]] int pointCount() const noexcept
	{
		return static_cast<int>(m_points.size());
	}

	/// @brief 全ポイントをクリアする
	void clear() { m_points.clear(); }

	/// @brief ポイント一覧を取得する
	[[nodiscard]] const std::vector<TrailPoint>& points() const noexcept
	{
		return m_points;
	}

private:
	std::vector<TrailPoint> m_points; ///< ポイントリスト
};

} // namespace mitiru::effects

// ── Screen依存の実装 ──
#include <mitiru/core/Screen.hpp>

inline void mitiru::effects::Trail::draw(
	Screen& screen, const sgc::Colorf& color, float thickness) const
{
	for (std::size_t i = 0; i + 1 < m_points.size(); ++i)
	{
		const float alpha0 = 1.0f - (m_points[i].age / lifetime);
		const float alpha1 = 1.0f - (m_points[i + 1].age / lifetime);
		const float avgAlpha = (alpha0 + alpha1) * 0.5f;
		const sgc::Colorf segColor{
			color.r, color.g, color.b,
			color.a * std::max(0.0f, avgAlpha)
		};
		// 太さもフェードする
		const float segThickness = thickness * std::max(0.0f, avgAlpha);
		screen.drawLine(
			m_points[i].position, m_points[i + 1].position,
			segColor, segThickness);
	}
}
