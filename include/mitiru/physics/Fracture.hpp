#pragma once

/// @file Fracture.hpp
/// @brief 凸な立体を Voronoi の小部屋に割り、破片の描画メッシュと凸包の点を作る (読み込みの時に 1 回)。
/// @details 種の点を決まった乱数で置き、各種について「他のどの種よりその種に近い側」の半空間で元の立体を
///          切り詰める。切り口は内側の面として印を付け、色を変えて描ける。使う演算は加減乗除と sqrt と比較だけで、
///          同じ種と同じ立体からはどの CPU でも同じビットの破片ができる (破片の形は物理の形でもあるので、
///          オンラインで食い違わないようにする)。凸でない立体は割れない。考え方は docs/PHYSICS_FX.md、ADR 0069。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

#include <sgc/math/Vec2.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Vertex3D.hpp>

namespace mitiru::physics3d
{

/// @brief 凸な多面体。面は外から見て反時計回りの点の列
struct ConvexPolyhedron
{
	struct Face
	{
		std::vector<sgc::Vec3f> points;
		bool interior = false;   ///< 割って出来た切り口
	};
	std::vector<Face> faces;

	/// @brief 原点を中心にした直方体
	[[nodiscard]] static ConvexPolyhedron box(const sgc::Vec3f& half)
	{
		const auto p = [&](float x, float y, float z) { return sgc::Vec3f{x * half.x, y * half.y, z * half.z}; };
		ConvexPolyhedron b;
		b.faces = {
			{{p(1, -1, -1), p(1, 1, -1), p(1, 1, 1), p(1, -1, 1)}},
			{{p(-1, -1, 1), p(-1, 1, 1), p(-1, 1, -1), p(-1, -1, -1)}},
			{{p(-1, 1, -1), p(-1, 1, 1), p(1, 1, 1), p(1, 1, -1)}},
			{{p(-1, -1, 1), p(-1, -1, -1), p(1, -1, -1), p(1, -1, 1)}},
			{{p(-1, -1, 1), p(1, -1, 1), p(1, 1, 1), p(-1, 1, 1)}},
			{{p(1, -1, -1), p(-1, -1, -1), p(-1, 1, -1), p(1, 1, -1)}},
		};
		return b;
	}

	/// @brief 角柱。ring は XZ 面の凸多角形 ({x, z} の並び、向きはどちらでもよい)、y0 と y1 は底と上
	[[nodiscard]] static ConvexPolyhedron prism(std::span<const sgc::Vec2f> ring, float y0, float y1)
	{
		ConvexPolyhedron b;
		const std::size_t n = ring.size();
		if (n < 3) { return b; }
		// {x, z} の符号付き面積が負の並びのとき、上の面の法線が +Y を向く
		float area2 = 0.0f;
		for (std::size_t i = 0; i < n; ++i)
		{
			area2 += ring[i].x * ring[(i + 1) % n].y - ring[(i + 1) % n].x * ring[i].y;
		}
		std::vector<sgc::Vec2f> r(ring.begin(), ring.end());
		if (area2 > 0.0f) { std::reverse(r.begin(), r.end()); }
		Face top, bottom;
		for (std::size_t i = 0; i < n; ++i)
		{
			top.points.push_back({r[i].x, y1, r[i].y});
			bottom.points.push_back({r[n - 1 - i].x, y0, r[n - 1 - i].y});
			const auto& a = r[i];
			const auto& c = r[(i + 1) % n];
			b.faces.push_back({{{a.x, y0, a.y}, {c.x, y0, c.y}, {c.x, y1, c.y}, {a.x, y1, a.y}}});
		}
		b.faces.push_back(std::move(top));
		b.faces.push_back(std::move(bottom));
		return b;
	}
};

/// @brief 割り方
struct FractureDesc
{
	int pieces = 12;                 ///< 種の数 (立体の外に落ちた種は引き直す)
	std::uint64_t seed = 1;
	sgc::Colorf outer{1.0f, 1.0f, 1.0f, 1.0f};   ///< 元の面の頂点色 (描く色に掛かる)
	sgc::Colorf inner{0.55f, 0.50f, 0.45f, 1.0f}; ///< 切り口の頂点色
};

/// @brief 破片 1 つ。座標は center を原点にした局所
struct FracturePiece
{
	std::vector<render::Vertex3D> vertices;   ///< 面ごとに平らな法線 (三角形ごとに頂点を持つ)
	std::vector<std::uint32_t> indices;
	std::vector<sgc::Vec3f> hull;             ///< 凸包の点 (重複なし)
	sgc::Vec3f center{0, 0, 0};               ///< 元の立体の座標での重心
	float volume = 0.0f;
};

struct FractureResult
{
	FracturePiece whole;                ///< 割る前の立体 (割れる前の body と描画に使う)
	std::vector<FracturePiece> pieces;
};

namespace detail::fracture
{

/// @brief splitmix64。整数だけで回るので、どの CPU でも同じ列になる
[[nodiscard]] inline std::uint64_t nextRandom(std::uint64_t& s) noexcept
{
	std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
	return z ^ (z >> 31);
}

[[nodiscard]] inline float unitRandom(std::uint64_t& s) noexcept
{
	return static_cast<float>(nextRandom(s) >> 40) * (1.0f / 16777216.0f);
}

[[nodiscard]] inline sgc::Vec3f faceNormal(const std::vector<sgc::Vec3f>& pts)
{
	sgc::Vec3f n{0, 0, 0};
	for (std::size_t i = 1; i + 1 < pts.size(); ++i) { n += (pts[i] - pts[0]).cross(pts[i + 1] - pts[0]); }
	const float l = n.length();
	return l > 0.0f ? n / l : n;
}

[[nodiscard]] inline bool inside(const ConvexPolyhedron& poly, const sgc::Vec3f& p)
{
	for (const auto& f : poly.faces)
	{
		if (faceNormal(f.points).dot(p - f.points[0]) > 0.0f) { return false; }
	}
	return true;
}

/// @brief 面の点の列を半空間 n·x <= d で切る。切った点は cut に足す
[[nodiscard]] inline std::vector<sgc::Vec3f> clipFace(const std::vector<sgc::Vec3f>& pts, const sgc::Vec3f& n, float d,
                                                     std::vector<sgc::Vec3f>& cut)
{
	constexpr float kEps = 1e-6f;
	std::vector<sgc::Vec3f> out;
	for (std::size_t i = 0; i < pts.size(); ++i)
	{
		const sgc::Vec3f& a = pts[i];
		const sgc::Vec3f& b = pts[(i + 1) % pts.size()];
		const float da = n.dot(a) - d;
		const float db = n.dot(b) - d;
		if (da <= kEps) { out.push_back(a); }
		if (da > -kEps && da <= kEps) { cut.push_back(a); }
		if ((da < -kEps && db > kEps) || (da > kEps && db < -kEps))
		{
			// 辺の向きで結果が変わらないよう、端点を決まった順に並べてから交点を出す
			const bool swap = (a.x > b.x) || (a.x == b.x && (a.y > b.y || (a.y == b.y && a.z > b.z)));
			const sgc::Vec3f& p = swap ? b : a;
			const sgc::Vec3f& q = swap ? a : b;
			const float dp = swap ? db : da;
			const float dq = swap ? da : db;
			const sgc::Vec3f x = p + (q - p) * (dp / (dp - dq));
			out.push_back(x);
			cut.push_back(x);
		}
	}
	return out;
}

/// @brief 平面内の角度の代わりに使う単調な値 (0..4)。三角関数を使わずに点を並べる
[[nodiscard]] inline float pseudoAngle(float u, float v) noexcept
{
	const float s = std::abs(u) + std::abs(v);
	if (s <= 0.0f) { return 0.0f; }
	const float r = u / s;
	return v >= 0.0f ? 1.0f - r : 3.0f + r;
}

/// @brief 切り口の点を重ねを除いて n のまわりに反時計回りへ並べる
[[nodiscard]] inline std::vector<sgc::Vec3f> capPolygon(std::vector<sgc::Vec3f> cut, const sgc::Vec3f& n)
{
	std::vector<sgc::Vec3f> uniq;
	for (const auto& p : cut)
	{
		const bool dup = std::any_of(uniq.begin(), uniq.end(), [&](const sgc::Vec3f& q) { return (p - q).lengthSquared() < 1e-12f; });
		if (!dup) { uniq.push_back(p); }
	}
	if (uniq.size() < 3) { return {}; }
	sgc::Vec3f c{0, 0, 0};
	for (const auto& p : uniq) { c += p; }
	c = c / static_cast<float>(uniq.size());
	const sgc::Vec3f helper = std::abs(n.x) < 0.9f ? sgc::Vec3f{1, 0, 0} : sgc::Vec3f{0, 1, 0};
	const sgc::Vec3f u = helper.cross(n).normalized();
	const sgc::Vec3f v = n.cross(u);
	std::sort(uniq.begin(), uniq.end(), [&](const sgc::Vec3f& a, const sgc::Vec3f& b) {
		return pseudoAngle(u.dot(a - c), v.dot(a - c)) < pseudoAngle(u.dot(b - c), v.dot(b - c));
	});
	return uniq;
}

/// @brief 多面体を半空間 n·x <= d で切り詰める
[[nodiscard]] inline ConvexPolyhedron clip(const ConvexPolyhedron& poly, const sgc::Vec3f& n, float d)
{
	ConvexPolyhedron out;
	std::vector<sgc::Vec3f> cut;
	for (const auto& f : poly.faces)
	{
		auto pts = clipFace(f.points, n, d, cut);
		if (pts.size() >= 3) { out.faces.push_back({std::move(pts), f.interior}); }
	}
	auto cap = capPolygon(std::move(cut), n.normalized());
	if (cap.size() >= 3) { out.faces.push_back({std::move(cap), true}); }
	return out;
}

/// @brief 体積と重心 (四面体に分けて足す)
inline void massProperties(const ConvexPolyhedron& poly, float& volume, sgc::Vec3f& center)
{
	volume = 0.0f;
	center = {0, 0, 0};
	if (poly.faces.empty()) { return; }
	const sgc::Vec3f o = poly.faces[0].points[0];
	for (const auto& f : poly.faces)
	{
		for (std::size_t i = 1; i + 1 < f.points.size(); ++i)
		{
			const sgc::Vec3f a = f.points[0] - o, b = f.points[i] - o, c = f.points[i + 1] - o;
			const float v = a.dot(b.cross(c)) / 6.0f;
			volume += v;
			center += (a + b + c) * (v / 4.0f);
		}
	}
	center = volume > 0.0f ? o + center / volume : o;
}

[[nodiscard]] inline FracturePiece toPiece(const ConvexPolyhedron& poly, const FractureDesc& desc)
{
	FracturePiece p;
	massProperties(poly, p.volume, p.center);
	for (const auto& f : poly.faces)
	{
		const sgc::Vec3f n = faceNormal(f.points);
		const sgc::Colorf col = f.interior ? desc.inner : desc.outer;
		const auto base = static_cast<std::uint32_t>(p.vertices.size());
		for (const auto& q : f.points)
		{
			p.vertices.push_back(render::Vertex3D(q - p.center, n, {0, 0}, col));
			const bool dup = std::any_of(p.hull.begin(), p.hull.end(), [&](const sgc::Vec3f& h) { return (h - (q - p.center)).lengthSquared() < 1e-12f; });
			if (!dup) { p.hull.push_back(q - p.center); }
		}
		for (std::uint32_t i = 1; i + 1 < f.points.size(); ++i) { p.indices.insert(p.indices.end(), {base, base + i, base + i + 1}); }
	}
	return p;
}

[[nodiscard]] inline std::vector<sgc::Vec3f> scatterSeeds(const ConvexPolyhedron& poly, const FractureDesc& desc)
{
	sgc::Vec3f lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
	for (const auto& f : poly.faces)
	{
		for (const auto& p : f.points) { lo = sgc::Vec3f::min(lo, p); hi = sgc::Vec3f::max(hi, p); }
	}
	std::uint64_t s = desc.seed;
	std::vector<sgc::Vec3f> seeds;
	for (int tries = 0; static_cast<int>(seeds.size()) < desc.pieces && tries < desc.pieces * 64; ++tries)
	{
		const sgc::Vec3f u{unitRandom(s), unitRandom(s), unitRandom(s)};
		const sgc::Vec3f p = lo + (hi - lo) * u;
		if (inside(poly, p)) { seeds.push_back(p); }
	}
	return seeds;
}

} // namespace detail::fracture

/// @brief 凸な立体を desc.pieces 個の Voronoi の小部屋に割る。体積がほぼ 0 の小部屋は捨てる
[[nodiscard]] inline FractureResult fractureConvex(const ConvexPolyhedron& shape, const FractureDesc& desc = {})
{
	namespace fr = detail::fracture;
	FractureResult out;
	out.whole = fr::toPiece(shape, desc);
	const auto seeds = fr::scatterSeeds(shape, desc);
	const float minVolume = out.whole.volume * 1e-5f;
	for (std::size_t i = 0; i < seeds.size(); ++i)
	{
		ConvexPolyhedron cell = shape;
		for (std::size_t j = 0; j < seeds.size() && !cell.faces.empty(); ++j)
		{
			if (j == i || (seeds[j] - seeds[i]).lengthSquared() <= 0.0f) { continue; }
			const sgc::Vec3f n = (seeds[j] - seeds[i]).normalized();
			cell = fr::clip(cell, n, n.dot((seeds[i] + seeds[j]) * 0.5f));
		}
		if (cell.faces.size() < 4) { continue; }
		FracturePiece piece = fr::toPiece(cell, desc);
		if (piece.volume > minVolume) { out.pieces.push_back(std::move(piece)); }
	}
	return out;
}

} // namespace mitiru::physics3d
