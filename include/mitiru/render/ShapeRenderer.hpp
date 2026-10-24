#pragma once

/// @file ShapeRenderer.hpp
/// @brief プリミティブシェイプレンダラー
/// @details 円・線分・三角形・ポリゴンなどの基本図形を頂点生成して描画する。

#include <cmath>
#include <cstdint>
#include <new>
#include <span>
#include <vector>

#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Vertex2D.hpp>

namespace mitiru::render
{

/// 塗りつぶし図形の頂点を、渡された配列の末尾へ足す関数群。
/// Screen は自前の ShapeRenderer を使わず、いま開いている色の run (SpriteBatch) の配列へ直接足す。
/// 図形だけ別の配列に積むと、後から描いた矩形や画像より上に出てしまい、描いた順を守れないため。
namespace shape_geom
{

inline constexpr float kPi = 3.14159265358979323846f;

/// 単位円周の点 (cos, sin)
struct UnitPoint
{
	float c;
	float s;
};

/// 楕円とリングの分割数の既定
inline constexpr int kDefaultSegments = 32;

/// 表に持つ分割数の上限。Screen::drawCircle の分割数 (24〜160) が全部収まる大きさ
inline constexpr int kMaxCachedSegments = 160;

/// 分割数 segments の円周 segments + 1 点 (始点を末尾にもう一度)。
/// 円ごとに cos と sin を 30 回ずつ呼ぶと、円 2,000 個で 1 ms を超える。分割数ごとの表を最初の呼び出しで全部作る。
/// 角度の出し方は直接計算と同じにしてあり、頂点は表を使わない場合と一致する
inline const UnitPoint* unitCircle(int segments, std::vector<UnitPoint>& scratch)
{
	static const std::vector<std::vector<UnitPoint>> tables = []
	{
		std::vector<std::vector<UnitPoint>> t(static_cast<std::size_t>(kMaxCachedSegments) + 1);
		for (int n = 3; n <= kMaxCachedSegments; ++n)
		{
			auto& row = t[static_cast<std::size_t>(n)];
			row.reserve(static_cast<std::size_t>(n) + 1);
			const float step = 2.0f * kPi / static_cast<float>(n);
			for (int i = 0; i <= n; ++i)
			{
				const float a = step * static_cast<float>(i);
				row.push_back({std::cos(a), std::sin(a)});
			}
		}
		return t;
	}();
	if (segments >= 3 && segments <= kMaxCachedSegments)
	{
		return tables[static_cast<std::size_t>(segments)].data();
	}
	// 表は 3 以上だけ持つ。呼び出し側が 3 未満を弾く前提に頼らず、ここでも 0 割りと範囲外を避ける
	if (segments < 1) { segments = 1; }
	scratch.clear();
	const float step = 2.0f * kPi / static_cast<float>(segments);
	for (int i = 0; i <= segments; ++i)
	{
		const float a = step * static_cast<float>(i);
		scratch.push_back({std::cos(a), std::sin(a)});
	}
	return scratch.data();
}

/// 中心から円周へ三角形ファンで広げる。radiusX と radiusY が違えば楕円
inline void appendFan(std::vector<Vertex2D>& vs, std::vector<std::uint32_t>& is,
                      const sgc::Vec2f& center, float radiusX, float radiusY,
                      const sgc::Colorf& color, int segments)
{
	if (segments < 3)
	{
		return;
	}
	std::vector<UnitPoint> scratch;
	const UnitPoint* ring = unitCircle(segments, scratch);

	const auto centerIdx = static_cast<std::uint32_t>(vs.size());
	if (segments <= kMaxCachedSegments)
	{
		// 円は 1 フレームに何千も来る。頂点と添字を 1 個ずつ push すると、そのたびの容量確認が積み込みの大半になるので、
		// 作業用の配列へ組んで 2 回の insert で足す。値は 1 個ずつ足す場合と同じ
		alignas(Vertex2D) unsigned char vertexStore[sizeof(Vertex2D) * (kMaxCachedSegments + 2)];   // 既定値で埋めない
		Vertex2D* verts = reinterpret_cast<Vertex2D*>(vertexStore);
		std::uint32_t idx[kMaxCachedSegments * 3];
		::new (verts) Vertex2D(center, color);
		for (int i = 0; i <= segments; ++i)
		{
			::new (verts + i + 1) Vertex2D(sgc::Vec2f{center.x + radiusX * ring[i].c, center.y + radiusY * ring[i].s}, color);
		}
		for (int i = 0; i < segments; ++i)
		{
			const auto k = static_cast<std::uint32_t>(i);
			idx[i * 3] = centerIdx;
			idx[i * 3 + 1] = centerIdx + 1 + k;
			idx[i * 3 + 2] = centerIdx + 2 + k;
		}
		vs.insert(vs.end(), verts, verts + segments + 2);
		is.insert(is.end(), idx, idx + segments * 3);
		return;
	}
	vs.emplace_back(center, color);
	for (int i = 0; i <= segments; ++i)
	{
		vs.emplace_back(sgc::Vec2f{center.x + radiusX * ring[i].c, center.y + radiusY * ring[i].s}, color);
	}
	for (int i = 0; i < segments; ++i)
	{
		const auto k = static_cast<std::uint32_t>(i);
		is.push_back(centerIdx);
		is.push_back(centerIdx + 1 + k);
		is.push_back(centerIdx + 2 + k);
	}
}

inline void appendCircle(std::vector<Vertex2D>& vs, std::vector<std::uint32_t>& is,
                         const sgc::Vec2f& center, float radius, const sgc::Colorf& color, int segments)
{
	appendFan(vs, is, center, radius, radius, color, segments);
}

inline void appendEllipse(std::vector<Vertex2D>& vs, std::vector<std::uint32_t>& is,
                          const sgc::Vec2f& center, float radiusX, float radiusY,
                          const sgc::Colorf& color, int segments)
{
	appendFan(vs, is, center, radiusX, radiusY, color, segments);
}

/// 太さを持つ線分を矩形 (4 頂点) として足す。長さ 0 の線は何も足さない
inline void appendLine(std::vector<Vertex2D>& vs, std::vector<std::uint32_t>& is,
                       const sgc::Vec2f& from, const sgc::Vec2f& to, const sgc::Colorf& color, float thickness)
{
	const sgc::Vec2f dir{to.x - from.x, to.y - from.y};
	const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
	if (len < 1e-6f)
	{
		return;
	}
	const float halfThick = thickness * 0.5f;
	const sgc::Vec2f perp{-dir.y / len * halfThick, dir.x / len * halfThick};

	const auto baseIdx = static_cast<std::uint32_t>(vs.size());
	const Vertex2D quad[4] = {
		Vertex2D(sgc::Vec2f{from.x + perp.x, from.y + perp.y}, color),
		Vertex2D(sgc::Vec2f{from.x - perp.x, from.y - perp.y}, color),
		Vertex2D(sgc::Vec2f{to.x - perp.x, to.y - perp.y}, color),
		Vertex2D(sgc::Vec2f{to.x + perp.x, to.y + perp.y}, color),
	};
	const std::uint32_t idx[6] = {baseIdx, baseIdx + 1, baseIdx + 2, baseIdx, baseIdx + 2, baseIdx + 3};
	vs.insert(vs.end(), quad, quad + 4);
	is.insert(is.end(), idx, idx + 6);
}

inline void appendTriangle(std::vector<Vertex2D>& vs, std::vector<std::uint32_t>& is,
                           const sgc::Vec2f& p0, const sgc::Vec2f& p1, const sgc::Vec2f& p2,
                           const sgc::Colorf& color)
{
	const auto baseIdx = static_cast<std::uint32_t>(vs.size());
	const Vertex2D tri[3] = {Vertex2D(p0, color), Vertex2D(p1, color), Vertex2D(p2, color)};
	const std::uint32_t idx[3] = {baseIdx, baseIdx + 1, baseIdx + 2};
	vs.insert(vs.end(), tri, tri + 3);
	is.insert(is.end(), idx, idx + 3);
}

/// 凸ポリゴンを三角形ファンで足す
inline void appendPolygon(std::vector<Vertex2D>& vs, std::vector<std::uint32_t>& is,
                          std::span<const sgc::Vec2f> points, const sgc::Colorf& color)
{
	if (points.size() < 3)
	{
		return;
	}
	const auto baseIdx = static_cast<std::uint32_t>(vs.size());
	for (const auto& pt : points)
	{
		vs.emplace_back(pt, color);
	}
	for (std::size_t i = 1; i + 1 < points.size(); ++i)
	{
		is.push_back(baseIdx);
		is.push_back(baseIdx + static_cast<std::uint32_t>(i));
		is.push_back(baseIdx + static_cast<std::uint32_t>(i + 1));
	}
}

/// 外輪と内輪の頂点を交互に並べた三角形ストリップのリング
inline void appendRing(std::vector<Vertex2D>& vs, std::vector<std::uint32_t>& is,
                       const sgc::Vec2f& center, float outerR, float innerR,
                       const sgc::Colorf& color, int segments)
{
	if (segments < 3 || outerR <= innerR)
	{
		return;
	}
	std::vector<UnitPoint> scratch;
	const UnitPoint* ring = unitCircle(segments, scratch);

	const auto baseIdx = static_cast<std::uint32_t>(vs.size());
	for (int i = 0; i <= segments; ++i)
	{
		vs.emplace_back(sgc::Vec2f{center.x + outerR * ring[i].c, center.y + outerR * ring[i].s}, color);
		vs.emplace_back(sgc::Vec2f{center.x + innerR * ring[i].c, center.y + innerR * ring[i].s}, color);
	}
	for (int i = 0; i < segments; ++i)
	{
		const auto i0 = baseIdx + static_cast<std::uint32_t>(i * 2);
		const auto i1 = i0 + 1;
		const auto i2 = i0 + 2;
		const auto i3 = i0 + 3;
		is.push_back(i0); is.push_back(i1); is.push_back(i2);
		is.push_back(i1); is.push_back(i3); is.push_back(i2);
	}
}

} // namespace shape_geom

/// @brief プリミティブシェイプレンダラー
/// @details 基本図形を頂点データとして生成する。
///          Phase 1 では GPU 送信はスタブ。
///
/// @code
/// mitiru::render::ShapeRenderer shapes;
/// shapes.drawCircle({400, 300}, 50.0f, sgc::Colorf::green(), 32);
/// shapes.drawLine({0, 0}, {100, 100}, sgc::Colorf::white(), 2.0f);
/// shapes.flush();
/// @endcode
class ShapeRenderer
{
public:
	/// @brief 円を描画する
	/// @param center 中心座標
	/// @param radius 半径
	/// @param color 描画色
	/// @param segments 分割数（多いほど滑らか）
	void drawCircle(const sgc::Vec2f& center,
	                float radius,
	                const sgc::Colorf& color,
	                int segments = 32)
	{
		if (segments < 3)
		{
			return;
		}
		shape_geom::appendCircle(m_vertices, m_indices, center, radius, color, segments);
		++m_drawCallCount;
	}

	/// @brief 線分を描画する
	/// @param from 始点
	/// @param to 終点
	/// @param color 描画色
	/// @param thickness 線の太さ
	void drawLine(const sgc::Vec2f& from,
	              const sgc::Vec2f& to,
	              const sgc::Colorf& color,
	              float thickness = 1.0f)
	{
		const auto before = m_vertices.size();
		shape_geom::appendLine(m_vertices, m_indices, from, to, color, thickness);
		if (m_vertices.size() != before)
		{
			++m_drawCallCount;
		}
	}

	/// @brief 三角形を描画する
	/// @param p0 頂点 0
	/// @param p1 頂点 1
	/// @param p2 頂点 2
	/// @param color 描画色
	void drawTriangle(const sgc::Vec2f& p0,
	                  const sgc::Vec2f& p1,
	                  const sgc::Vec2f& p2,
	                  const sgc::Colorf& color)
	{
		shape_geom::appendTriangle(m_vertices, m_indices, p0, p1, p2, color);
		++m_drawCallCount;
	}

	/// @brief ポリゴンを描画する（凸ポリゴンを三角形ファンで分割）
	/// @param points 頂点座標の配列（凸ポリゴン前提）
	/// @param color 描画色
	void drawPolygon(std::span<const sgc::Vec2f> points,
	                 const sgc::Colorf& color)
	{
		if (points.size() < 3)
		{
			return;
		}
		shape_geom::appendPolygon(m_vertices, m_indices, points, color);
		++m_drawCallCount;
	}

	/// @brief 楕円を描画する
	/// @param center 中心座標
	/// @param radiusX X 方向の半径
	/// @param radiusY Y 方向の半径
	/// @param color 描画色
	/// @param segments 分割数
	void drawEllipse(const sgc::Vec2f& center,
	                 float radiusX, float radiusY,
	                 const sgc::Colorf& color,
	                 int segments = 32)
	{
		if (segments < 3)
		{
			return;
		}
		shape_geom::appendEllipse(m_vertices, m_indices, center, radiusX, radiusY, color, segments);
		++m_drawCallCount;
	}

	/// @brief リング（ドーナツ）を描画する
	/// @param center 中心座標
	/// @param outerR 外側の半径
	/// @param innerR 内側の半径
	/// @param color 描画色
	/// @param segments 分割数
	void drawRing(const sgc::Vec2f& center,
	              float outerR, float innerR,
	              const sgc::Colorf& color,
	              int segments = 32)
	{
		if (segments < 3 || outerR <= innerR)
		{
			return;
		}
		shape_geom::appendRing(m_vertices, m_indices, center, outerR, innerR, color, segments);
		++m_drawCallCount;
	}

	/// @brief 蓄積データをフラッシュする
	/// @details GPU 描画は RenderPipeline2D が submitBatch()で行う。
	///          flush()はデータをクリアするのみ。
	void flush() noexcept
	{
		m_vertices.clear();
		m_indices.clear();
		m_drawCallCount = 0;
	}

	/// @brief 蓄積された頂点数を取得する
	[[nodiscard]] std::size_t vertexCount() const noexcept
	{
		return m_vertices.size();
	}

	/// @brief 蓄積されたインデックス数を取得する
	[[nodiscard]] std::size_t indexCount() const noexcept
	{
		return m_indices.size();
	}

	/// @brief 描画コール数を取得する
	[[nodiscard]] int drawCallCount() const noexcept
	{
		return m_drawCallCount;
	}

	/// @brief 蓄積された頂点データを取得する
	[[nodiscard]] const std::vector<Vertex2D>& vertices() const noexcept
	{
		return m_vertices;
	}

	/// @brief 蓄積されたインデックスデータを取得する
	[[nodiscard]] const std::vector<std::uint32_t>& indices() const noexcept
	{
		return m_indices;
	}

private:
	std::vector<Vertex2D> m_vertices;       ///< 蓄積された頂点データ
	std::vector<std::uint32_t> m_indices;   ///< 蓄積されたインデックスデータ
	int m_drawCallCount = 0;                ///< 描画コール数
};

} // namespace mitiru::render
