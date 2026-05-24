#pragma once

/// @file Spline.hpp
/// @brief Catmull-Romスプライン補間
///
/// 複数の通過点を滑らかに結ぶスプライン曲線。
/// キャラクターの移動経路やカメラワークに使用する。
///
/// @code
/// sgc::SplinePath<sgc::Vec2f> path;
/// path.addPoint({0, 0});
/// path.addPoint({100, 50});
/// path.addPoint({200, 0});
/// path.addPoint({300, 50});
/// auto pt = path.evaluate(0.5f);
/// @endcode

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace sgc
{

/// @brief Catmull-Romスプライン補間
/// @tparam Vec ベクトル型（operator+, operator-, operator* が必要）
/// @param p0 前の制御点
/// @param p1 補間区間の始点
/// @param p2 補間区間の終点
/// @param p3 次の制御点
/// @param t 補間パラメータ [0, 1]
/// @return スプライン上の点
///
/// @code
/// auto pt = sgc::catmullRom(prev, start, end, next, 0.5f);
/// @endcode
template <typename Vec>
[[nodiscard]] constexpr Vec catmullRom(
	const Vec& p0, const Vec& p1, const Vec& p2, const Vec& p3, float t) noexcept
{
	const float tt = t * t;
	const float ttt = tt * t;

	// Catmull-Rom行列係数
	// q(t) = 0.5 * ((2*P1) + (-P0+P2)*t + (2*P0-5*P1+4*P2-P3)*t^2 + (-P0+3*P1-3*P2+P3)*t^3)
	return (p1 * 2.0f
		+ (p2 - p0) * t
		+ (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * tt
		+ (p1 * 3.0f - p0 - p2 * 3.0f + p3) * ttt) * 0.5f;
}

/// @brief Catmull-Romスプラインパス — 通過点を滑らかに結ぶ曲線
/// @tparam Vec ベクトル型（operator+, operator-, operator* が必要）
///
/// 最低4点必要。最初と最後の点はガイド点として使用され、
/// 実際のパスは2番目の点から最後から2番目の点までを通る。
///
/// @code
/// sgc::SplinePath<sgc::Vec2f> path;
/// path.addPoint({0, 0});
/// path.addPoint({100, 50});
/// path.addPoint({200, 0});
/// path.addPoint({300, 50});
/// auto pt = path.evaluate(0.33f);
/// @endcode
template <typename Vec>
class SplinePath
{
public:
	/// @brief 通過点を追加する
	/// @param point 追加する点
	void addPoint(const Vec& point)
	{
		m_points.push_back(point);
	}

	/// @brief スプラインパス上の点を評価する
	/// @param t 補間パラメータ [0.0, 1.0]（パス全体に対する割合）
	/// @return パス上の点
	/// @throw std::runtime_error 点が4未満の場合
	///
	/// @note 最初の点と最後の点はガイド点。パスはm_points[1]からm_points[n-2]を通る。
	[[nodiscard]] Vec evaluate(float t) const
	{
		if (m_points.size() < 4)
		{
			throw std::runtime_error("SplinePath: need at least 4 points");
		}

		// 有効セグメント数 = n - 3（ガイド点を除く）
		const auto numSegments = m_points.size() - 3;
		const float scaled = t * static_cast<float>(numSegments);
		auto idx = static_cast<std::size_t>(scaled);

		if (idx >= numSegments)
		{
			idx = numSegments - 1;
		}

		const float localT = scaled - static_cast<float>(idx);
		return catmullRom(
			m_points[idx],
			m_points[idx + 1],
			m_points[idx + 2],
			m_points[idx + 3],
			localT);
	}

	/// @brief 登録されている点の数を返す
	/// @return 点の数
	[[nodiscard]] std::size_t pointCount() const noexcept
	{
		return m_points.size();
	}

	/// @brief パスをクリアする
	void clear() noexcept
	{
		m_points.clear();
	}

	/// @brief パスの近似長さを計算する
	/// @param segments 分割数
	/// @return 近似長さ
	[[nodiscard]] float approximateLength(int segments = 100) const
	{
		if (m_points.size() < 4)
		{
			return 0.0f;
		}

		float length = 0.0f;
		Vec prev = evaluate(0.0f);

		for (int i = 1; i <= segments; ++i)
		{
			const float localT = static_cast<float>(i) / static_cast<float>(segments);
			const Vec current = evaluate(localT);
			const Vec diff = current - prev;
			length += std::sqrt(diff.x * diff.x + diff.y * diff.y);
			prev = current;
		}

		return length;
	}

private:
	std::vector<Vec> m_points;  ///< 通過点列
};

} // namespace sgc
