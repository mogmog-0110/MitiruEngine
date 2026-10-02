#pragma once

/// @file TrailRibbon.hpp
/// @brief 剣筋やダッシュの筋 (点列 → カメラを向いた帯) の頂点を作る
/// @details 点の位置・幅・色・年齢だけから決まる純関数なので、同じ点列からは毎回同じ帯になる。
///          レンダラはこの結果を三角形ストリップとして描く。ゲーム DLL が自分で使ってもよい。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

namespace mitiru::render
{

/// @brief 帯の 1 点。age は 0 が生まれたて、1 で寿命が尽きる
struct TrailPoint
{
	sgc::Vec3f  position{};
	float       width = 0.1f;
	sgc::Colorf color{1.0f, 1.0f, 1.0f, 1.0f};
	float       age = 0.0f;
};

enum class TrailBlend : std::uint8_t
{
	Additive,   ///< 光る筋。重なるほど明るくなり、描く順に依らない
	Alpha,      ///< 煙や影のような筋。重なりは描いた順に乗る
};

struct TrailStyle
{
	TrailBlend blend = TrailBlend::Additive;
	float fadePower = 1.0f;   ///< 不透明度に掛ける (1 - age) の指数。0 なら年齢で薄くならない
	float taper = 1.0f;       ///< 幅を寿命の終わりで (1 - taper) 倍まで細くする
	float edgeSoftness = 0.5f;///< 帯の縁をぼかす幅 (帯の半幅に対する割合)。0 で縁がくっきりする
};

/// @brief GPU へそのまま送る頂点。uv.x は先頭からの長さの割合、uv.y は帯の横 (0..1)
struct TrailVertex
{
	float position[3];
	float color[4];
	float uv[2];
};

static_assert(sizeof(TrailVertex) == 36, "TrailVertex は HLSL の入力配置と 1 対 1");

/// @brief ゲーム DLL から Screen::drawTrail へ渡す 1 点 (ABI v48)。TrailPoint と同じ中身を生の float で持つ
struct TrailPointPod
{
	float position[3];
	float width;
	float color[4];
	float age;
};

/// @brief Screen::drawTrail の見た目 (ABI v48)。blend は TrailBlend の値
struct TrailStylePod
{
	std::uint8_t blend = 0;
	std::uint8_t _pad[3] = {};
	float fadePower = 1.0f;
	float taper = 1.0f;
	float edgeSoftness = 0.5f;
};

static_assert(sizeof(TrailPointPod) == 36 && std::is_trivially_copyable_v<TrailPointPod>, "TrailPointPod wire size 固定 (v48)");
static_assert(sizeof(TrailStylePod) == 16 && std::is_trivially_copyable_v<TrailStylePod>, "TrailStylePod wire size 固定 (v48)");

[[nodiscard]] inline TrailPoint toTrailPoint(const TrailPointPod& p) noexcept
{
	return {{p.position[0], p.position[1], p.position[2]}, p.width, {p.color[0], p.color[1], p.color[2], p.color[3]}, p.age};
}

[[nodiscard]] inline TrailStyle toTrailStyle(const TrailStylePod& s) noexcept
{
	return {s.blend == 1 ? TrailBlend::Alpha : TrailBlend::Additive, s.fadePower, s.taper, s.edgeSoftness};
}

namespace detail
{

[[nodiscard]] inline sgc::Vec3f trailTangent(std::span<const TrailPoint> pts, std::size_t i) noexcept
{
	const std::size_t prev = (i > 0) ? i - 1 : i;
	const std::size_t next = (i + 1 < pts.size()) ? i + 1 : i;
	return pts[next].position - pts[prev].position;
}

/// @brief 視線と接線の両方に直交する向き。両者が平行なら前の点の向きを引き継ぐ
[[nodiscard]] inline sgc::Vec3f trailSide(const sgc::Vec3f& tangent, const sgc::Vec3f& toEye,
                                          const sgc::Vec3f& fallback) noexcept
{
	const sgc::Vec3f side = tangent.cross(toEye);
	const float len = side.length();
	if (len <= 1e-6f) { return fallback; }
	return side * (1.0f / len);
}

inline void writeTrailVertex(TrailVertex& v, const sgc::Vec3f& p, const sgc::Colorf& c, float alpha,
                             float u, float across) noexcept
{
	v.position[0] = p.x;
	v.position[1] = p.y;
	v.position[2] = p.z;
	v.color[0] = c.r;
	v.color[1] = c.g;
	v.color[2] = c.b;
	v.color[3] = alpha;
	v.uv[0] = u;
	v.uv[1] = across;
}

} // namespace detail

/// @brief 点列を帯の頂点 (点 1 つにつき左右 2 頂点の三角形ストリップ) にする
/// @return 書いた頂点数。点が 2 つ未満、または out が 2 × 点数より小さいときは 0
[[nodiscard]] inline std::size_t buildTrailRibbon(std::span<const TrailPoint> points, const sgc::Vec3f& eye,
                                                  const TrailStyle& style, std::span<TrailVertex> out) noexcept
{
	const std::size_t n = points.size();
	if (n < 2 || out.size() < n * 2) { return 0; }

	float totalLength = 0.0f;
	for (std::size_t i = 1; i < n; ++i) { totalLength += (points[i].position - points[i - 1].position).length(); }
	const float invLength = (totalLength > 1e-6f) ? 1.0f / totalLength : 0.0f;

	sgc::Vec3f side{1.0f, 0.0f, 0.0f};
	float along = 0.0f;
	for (std::size_t i = 0; i < n; ++i)
	{
		const TrailPoint& pt = points[i];
		if (i > 0) { along += (pt.position - points[i - 1].position).length(); }
		side = detail::trailSide(detail::trailTangent(points, i), eye - pt.position, side);

		const float age = std::clamp(pt.age, 0.0f, 1.0f);
		const float life = 1.0f - age;
		const float alpha = pt.color.a * ((style.fadePower > 0.0f) ? std::pow(life, style.fadePower) : 1.0f);
		const float halfWidth = 0.5f * std::max(pt.width, 0.0f) * (1.0f - std::clamp(style.taper, 0.0f, 1.0f) * age);
		const float u = (invLength > 0.0f) ? along * invLength : static_cast<float>(i) / static_cast<float>(n - 1);

		detail::writeTrailVertex(out[i * 2], pt.position + side * halfWidth, pt.color, alpha, u, 0.0f);
		detail::writeTrailVertex(out[i * 2 + 1], pt.position - side * halfWidth, pt.color, alpha, u, 1.0f);
	}
	return n * 2;
}

} // namespace mitiru::render
