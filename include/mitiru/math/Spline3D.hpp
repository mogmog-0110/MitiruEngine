#pragma once

/// @file Spline3D.hpp
/// @brief 3D Catmull-Romスプライン（等速パラメータ化・最近接点探索付き）
///
/// 制御点列を通過する滑らかな3D曲線。端点は複製して扱うため、開曲線でも
/// 制御点そのものを通過できる（ガイド点を別途要求しない）。レール移動や
/// カメラワーク、巡回パスに使う。

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

#include "sgc/math/Vec3.hpp"

namespace mitiru::math
{

/// @brief 3D Catmull-Romスプライン
class Spline3D
{
public:
	/// @brief 制御点を追加する（末尾に追加。既存の弧長テーブルは無効化される）
	void addPoint(const sgc::Vec3f& point)
	{
		m_points.push_back(point);
		m_built = false;
	}

	/// @brief 全制御点をクリアする
	void clear() noexcept
	{
		m_points.clear();
		m_arcTable.clear();
		m_built = false;
		m_length = 0.0f;
	}

	/// @brief 閉曲線（ループ）フラグを設定する
	void setLoop(bool loop) noexcept
	{
		if (m_loop != loop)
		{
			m_loop = loop;
			m_built = false;
		}
	}

	/// @brief 閉曲線かどうかを返す
	[[nodiscard]] bool isLoop() const noexcept { return m_loop; }

	/// @brief 制御点数を返す
	[[nodiscard]] std::size_t pointCount() const noexcept { return m_points.size(); }

	/// @brief セグメント数を返す（開曲線は点数-1、閉曲線は点数）
	[[nodiscard]] std::size_t segmentCount() const noexcept
	{
		if (m_points.size() < 2) return 0;
		return m_loop ? m_points.size() : m_points.size() - 1;
	}

	/// @brief 弧長テーブルを構築する（以後 position/tangent 以外は allocation なし）
	/// @param samplesPerSegment セグメントあたりのサンプル数（多いほど等速化の精度が上がる）
	void build(std::size_t samplesPerSegment = 16)
	{
		m_arcTable.clear();

		const std::size_t segs = segmentCount();
		if (segs == 0)
		{
			m_length = 0.0f;
			m_built = true;
			return;
		}

		const std::size_t totalSamples = segs * std::max<std::size_t>(samplesPerSegment, 1) + 1;
		m_arcTable.reserve(totalSamples);

		sgc::Vec3f prev = position(0.0f);
		m_arcTable.push_back({0.0f, 0.0f});
		float cumulative = 0.0f;

		for (std::size_t i = 1; i < totalSamples; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(totalSamples - 1);
			const sgc::Vec3f cur = position(t);
			cumulative += (cur - prev).length();
			m_arcTable.push_back({t, cumulative});
			prev = cur;
		}

		m_length = cumulative;
		m_built = true;
	}

	/// @brief 弧長テーブル構築済みかどうかを返す
	[[nodiscard]] bool isBuilt() const noexcept { return m_built; }

	/// @brief 曲線の全長を返す（build() 済みであること）
	[[nodiscard]] float length() const noexcept { return m_length; }

	/// @brief パス全体に対する割合 t [0, 1] で位置を評価する
	[[nodiscard]] sgc::Vec3f position(float t) const noexcept
	{
		const std::size_t segs = segmentCount();
		if (segs == 0)
		{
			return m_points.empty() ? sgc::Vec3f{} : m_points.front();
		}

		std::size_t seg{};
		float localT{};
		resolveSegment(t, segs, seg, localT);

		const auto i = static_cast<std::ptrdiff_t>(seg);
		return catmullRom(pointAt(i - 1), pointAt(i), pointAt(i + 1), pointAt(i + 2), localT);
	}

	/// @brief パス全体に対する割合 t [0, 1] で接線（正規化済み進行方向）を評価する
	[[nodiscard]] sgc::Vec3f tangent(float t) const noexcept
	{
		const std::size_t segs = segmentCount();
		if (segs == 0) return sgc::Vec3f::unitZ();

		std::size_t seg{};
		float localT{};
		resolveSegment(t, segs, seg, localT);

		const auto i = static_cast<std::ptrdiff_t>(seg);
		const sgc::Vec3f p0 = pointAt(i - 1);
		const sgc::Vec3f p1 = pointAt(i);
		const sgc::Vec3f p2 = pointAt(i + 1);
		const sgc::Vec3f p3 = pointAt(i + 2);

		// catmullRomのtに関する導関数（区間内ローカルt基準）
		const sgc::Vec3f deriv = (p2 - p0)
			+ (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * (2.0f * localT)
			+ (p1 * 3.0f - p0 - p2 * 3.0f + p3) * (3.0f * localT * localT);

		return (deriv * 0.5f).normalized();
	}

	/// @brief 弧長距離 s（[0, length()]）から等速パラメータ化した位置を返す
	/// @note build() 未実行、または点が2未満の場合は position(0) を返す
	[[nodiscard]] sgc::Vec3f positionAtDistance(float s) const noexcept
	{
		if (!m_built || m_arcTable.size() < 2)
		{
			return position(0.0f);
		}

		s = std::clamp(s, 0.0f, m_length);

		std::size_t lo = 0;
		std::size_t hi = m_arcTable.size() - 1;
		while (lo + 1 < hi)
		{
			const std::size_t mid = (lo + hi) / 2;
			if (m_arcTable[mid].distance <= s) lo = mid; else hi = mid;
		}

		const ArcEntry& a = m_arcTable[lo];
		const ArcEntry& b = m_arcTable[hi];
		const float span = b.distance - a.distance;
		const float frac = (span > 1e-8f) ? (s - a.distance) / span : 0.0f;

		return position(a.t + (b.t - a.t) * frac);
	}

	/// @brief 曲線上で点 p に最も近いパラメータ t を求める
	/// @param p 対象点
	/// @param tOut 見つかった t を書き込む（nullptrなら無視）
	/// @return p から曲線上最近接点までの距離
	/// @note 区間ごとの粗探索（build済みなら弧長テーブルのサンプルを流用）に
	///       三分探索を重ねて精密化する。距離関数は探索窓の内側で単峰と仮定する。
	float closestPoint(const sgc::Vec3f& p, float* tOut) const noexcept
	{
		if (m_points.empty())
		{
			if (tOut) *tOut = 0.0f;
			return 0.0f;
		}

		float bestT = 0.0f;
		float bestDistSq = std::numeric_limits<float>::max();
		float coarseStep = 0.05f;

		if (m_built && !m_arcTable.empty())
		{
			coarseStep = 1.0f / static_cast<float>(m_arcTable.size());
			for (const ArcEntry& entry : m_arcTable)
			{
				const float d = (position(entry.t) - p).lengthSquared();
				if (d < bestDistSq) { bestDistSq = d; bestT = entry.t; }
			}
		}
		else
		{
			const std::size_t coarseSteps = std::max<std::size_t>(segmentCount() * 8, 8);
			coarseStep = 1.0f / static_cast<float>(coarseSteps);
			for (std::size_t i = 0; i <= coarseSteps; ++i)
			{
				const float t = static_cast<float>(i) / static_cast<float>(coarseSteps);
				const float d = (position(t) - p).lengthSquared();
				if (d < bestDistSq) { bestDistSq = d; bestT = t; }
			}
		}

		float lo = std::max(0.0f, bestT - coarseStep);
		float hi = std::min(1.0f, bestT + coarseStep);
		for (int iter = 0; iter < 24; ++iter)
		{
			const float m1 = lo + (hi - lo) / 3.0f;
			const float m2 = hi - (hi - lo) / 3.0f;
			const float d1 = (position(m1) - p).lengthSquared();
			const float d2 = (position(m2) - p).lengthSquared();
			if (d1 < d2) hi = m2; else lo = m1;
		}

		bestT = (lo + hi) * 0.5f;
		if (tOut) *tOut = bestT;
		return (position(bestT) - p).length();
	}

private:
	/// @brief 弧長テーブルの1エントリ（t とそこまでの累積距離）
	struct ArcEntry
	{
		float t{0.0f};
		float distance{0.0f};
	};

	/// @brief グローバル t [0,1] をセグメント番号とローカル t [0,1] に分解する
	static void resolveSegment(float t, std::size_t segs, std::size_t& segOut, float& localTOut) noexcept
	{
		t = std::clamp(t, 0.0f, 1.0f);
		const float scaled = t * static_cast<float>(segs);
		std::size_t seg = static_cast<std::size_t>(scaled);
		if (seg >= segs) seg = segs - 1;
		segOut = seg;
		localTOut = scaled - static_cast<float>(seg);
	}

	/// @brief 制御点を符号付きインデックスで取得する（ループは巡回、開曲線は端点を複製する）
	/// @note 開曲線の範囲外は、単純な複製ではなく端点を軸とした鏡映点を返す。
	///       単純複製だと直線状の制御点でも位置が線形にならない（始点の接線がゼロになる）ため。
	[[nodiscard]] sgc::Vec3f pointAt(std::ptrdiff_t index) const noexcept
	{
		const auto n = static_cast<std::ptrdiff_t>(m_points.size());
		if (n == 0) return {};

		if (m_loop)
		{
			std::ptrdiff_t wrapped = index % n;
			if (wrapped < 0) wrapped += n;
			return m_points[static_cast<std::size_t>(wrapped)];
		}

		if (index < 0)
		{
			if (n < 2) return m_points[0];
			return m_points[0] * 2.0f - m_points[1];
		}
		if (index >= n)
		{
			if (n < 2) return m_points[static_cast<std::size_t>(n - 1)];
			return m_points[static_cast<std::size_t>(n - 1)] * 2.0f - m_points[static_cast<std::size_t>(n - 2)];
		}
		return m_points[static_cast<std::size_t>(index)];
	}

	/// @brief Catmull-Rom補間（区間内ローカル t [0,1]）
	[[nodiscard]] static sgc::Vec3f catmullRom(
		const sgc::Vec3f& p0, const sgc::Vec3f& p1, const sgc::Vec3f& p2, const sgc::Vec3f& p3, float t) noexcept
	{
		const float tt = t * t;
		const float ttt = tt * t;
		return (p1 * 2.0f
			+ (p2 - p0) * t
			+ (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * tt
			+ (p1 * 3.0f - p0 - p2 * 3.0f + p3) * ttt) * 0.5f;
	}

	std::vector<sgc::Vec3f> m_points;
	std::vector<ArcEntry> m_arcTable;
	float m_length{0.0f};
	bool m_loop{false};
	bool m_built{false};
};

} // namespace mitiru::math
