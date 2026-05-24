#pragma once

/// @file Bezier.hpp
/// @brief ベジェ曲線ユーティリティ（2次・3次ベジェ、パス）
///
/// ゲームの移動経路・エフェクト曲線・UIアニメーションで使用する
/// ベジェ曲線の評価・接線計算・パスチェーン機能を提供する。
///
/// @code
/// sgc::Vec2f p0{0, 0}, p1{50, 100}, p2{100, 100}, p3{150, 0};
/// auto mid = sgc::cubicBezier(p0, p1, p2, p3, 0.5f);
/// @endcode

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace sgc
{

/// @brief 2次ベジェ曲線の点を評価する
/// @tparam Vec ベクトル型（operator+, operator* が必要）
/// @param p0 始点
/// @param p1 制御点
/// @param p2 終点
/// @param t 補間パラメータ [0, 1]
/// @return 曲線上の点
template <typename Vec>
[[nodiscard]] constexpr Vec quadraticBezier(
	const Vec& p0, const Vec& p1, const Vec& p2, float t) noexcept
{
	const float u = 1.0f - t;
	return p0 * (u * u) + p1 * (2.0f * u * t) + p2 * (t * t);
}

/// @brief 3次ベジェ曲線の点を評価する
/// @tparam Vec ベクトル型（operator+, operator* が必要）
/// @param p0 始点
/// @param p1 制御点1
/// @param p2 制御点2
/// @param p3 終点
/// @param t 補間パラメータ [0, 1]
/// @return 曲線上の点
template <typename Vec>
[[nodiscard]] constexpr Vec cubicBezier(
	const Vec& p0, const Vec& p1, const Vec& p2, const Vec& p3, float t) noexcept
{
	const float u = 1.0f - t;
	const float uu = u * u;
	const float uuu = uu * u;
	const float tt = t * t;
	const float ttt = tt * t;
	return p0 * uuu + p1 * (3.0f * uu * t) + p2 * (3.0f * u * tt) + p3 * ttt;
}

/// @brief 3次ベジェ曲線の接線ベクトルを評価する
/// @tparam Vec ベクトル型（operator+, operator-, operator* が必要）
/// @param p0 始点
/// @param p1 制御点1
/// @param p2 制御点2
/// @param p3 終点
/// @param t 補間パラメータ [0, 1]
/// @return 接線ベクトル（正規化されていない）
template <typename Vec>
[[nodiscard]] constexpr Vec cubicBezierTangent(
	const Vec& p0, const Vec& p1, const Vec& p2, const Vec& p3, float t) noexcept
{
	const float u = 1.0f - t;
	const float uu = u * u;
	const float ut = u * t;
	const float tt = t * t;
	return (p1 - p0) * (3.0f * uu) + (p2 - p1) * (6.0f * ut) + (p3 - p2) * (3.0f * tt);
}

/// @brief ベジェパス — 複数の3次ベジェセグメントをチェーンした曲線
/// @tparam Vec ベクトル型（operator+, operator-, operator* が必要）
///
/// @code
/// sgc::BezierPath<sgc::Vec2f> path;
/// path.setStart({0, 0});
/// path.addCubic({30, 60}, {70, 60}, {100, 0});
/// path.addCubic({130, -60}, {170, -60}, {200, 0});
/// auto pt = path.evaluate(0.5f);  // パスの中間点
/// @endcode
template <typename Vec>
class BezierPath
{
public:
	/// @brief 3次ベジェセグメントの制御点
	struct Segment
	{
		Vec c1;   ///< 制御点1
		Vec c2;   ///< 制御点2
		Vec end;  ///< セグメント終点
	};

	/// @brief パスの開始点を設定する
	/// @param start 開始点
	void setStart(const Vec& start)
	{
		m_start = start;
	}

	/// @brief 3次ベジェセグメントを追加する
	/// @param c1 制御点1
	/// @param c2 制御点2
	/// @param end セグメント終点
	void addCubic(const Vec& c1, const Vec& c2, const Vec& end)
	{
		m_segments.push_back({c1, c2, end});
	}

	/// @brief パス上の点を評価する
	/// @param t 補間パラメータ [0.0, 1.0]（パス全体に対する割合）
	/// @return パス上の点
	/// @throw std::runtime_error セグメントが空の場合
	[[nodiscard]] Vec evaluate(float t) const
	{
		if (m_segments.empty())
		{
			throw std::runtime_error("BezierPath: no segments");
		}

		// tをセグメントインデックスとローカルtに分割
		const float scaled = t * static_cast<float>(m_segments.size());
		const auto idx = static_cast<std::size_t>(scaled);

		// 最後のセグメントの終点を返す
		if (idx >= m_segments.size())
		{
			return m_segments.back().end;
		}

		const float localT = scaled - static_cast<float>(idx);
		const Vec& segStart = (idx == 0) ? m_start : m_segments[idx - 1].end;
		const auto& seg = m_segments[idx];

		return cubicBezier(segStart, seg.c1, seg.c2, seg.end, localT);
	}

	/// @brief パスの長さを近似計算する（線分分割による近似）
	/// @param segments 分割数（精度に影響）
	/// @return 近似長さ
	[[nodiscard]] float approximateLength(int segments = 100) const
	{
		if (m_segments.empty())
		{
			return 0.0f;
		}

		float length = 0.0f;
		Vec prev = evaluate(0.0f);

		for (int i = 1; i <= segments; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(segments);
			const Vec current = evaluate(t);
			const Vec diff = current - prev;
			length += std::sqrt(diff.x * diff.x + diff.y * diff.y);
			prev = current;
		}

		return length;
	}

	/// @brief セグメント数を返す
	/// @return セグメント数
	[[nodiscard]] std::size_t segmentCount() const noexcept
	{
		return m_segments.size();
	}

	/// @brief パスをクリアする
	void clear() noexcept
	{
		m_start = Vec{};
		m_segments.clear();
	}

private:
	Vec m_start{};                  ///< パスの開始点
	std::vector<Segment> m_segments; ///< ベジェセグメント列
};

} // namespace sgc
