#pragma once

// Avoidance.hpp の中身。直接 include しない
//
// ORCA の速度を線形計画で求める。van den Berg, Guy, Lin, Manocha「Reciprocal n-Body Collision Avoidance」(2011) と、
// RVO2 Library (Apache-2.0) の linearProgram1-3 に従う。固定長の配列を使い、ヒープを使わない。

#include <cmath>

namespace mitiru::gameai::orca_detail
{

struct V2
{
	float x = 0.0f;
	float y = 0.0f;
};

[[nodiscard]] constexpr V2 operator+(V2 a, V2 b) noexcept { return {a.x + b.x, a.y + b.y}; }
[[nodiscard]] constexpr V2 operator-(V2 a, V2 b) noexcept { return {a.x - b.x, a.y - b.y}; }
[[nodiscard]] constexpr V2 operator-(V2 a) noexcept { return {-a.x, -a.y}; }
[[nodiscard]] constexpr V2 operator*(V2 a, float s) noexcept { return {a.x * s, a.y * s}; }
[[nodiscard]] constexpr V2 operator*(float s, V2 a) noexcept { return {a.x * s, a.y * s}; }
[[nodiscard]] constexpr float dot(V2 a, V2 b) noexcept { return a.x * b.x + a.y * b.y; }
[[nodiscard]] constexpr float det(V2 a, V2 b) noexcept { return a.x * b.y - a.y * b.x; }
[[nodiscard]] constexpr float absSq(V2 a) noexcept { return dot(a, a); }
[[nodiscard]] inline V2 normalize(V2 a) noexcept
{
	const float l = std::sqrt(absSq(a));
	return l > 1e-12f ? a * (1.0f / l) : V2{};
}

/// @brief 許される速度の半平面。direction の左側を許す
struct Line
{
	V2 point{};
	V2 direction{};
};

inline constexpr float kEpsilon  = 1e-5f;
inline constexpr int   kMaxLines = 16;

/// @brief lineNo 番の直線上で、前の半平面と速さの円を満たす最適な点
inline bool linearProgram1(const Line* lines, int lineNo, float radius, V2 opt, bool directionOpt, V2& result) noexcept
{
	const Line& L = lines[lineNo];
	const float dp   = dot(L.point, L.direction);
	const float disc = dp * dp + radius * radius - absSq(L.point);
	if (disc < 0.0f) { return false; }
	const float sq = std::sqrt(disc);
	float tLeft = -dp - sq, tRight = -dp + sq;
	for (int i = 0; i < lineNo; ++i)
	{
		const float denom = det(L.direction, lines[i].direction);
		const float numer = det(lines[i].direction, L.point - lines[i].point);
		if (std::fabs(denom) <= kEpsilon)
		{
			if (numer < 0.0f) { return false; }
			continue;
		}
		const float t = numer / denom;
		if (denom >= 0.0f) { tRight = tRight < t ? tRight : t; }
		else { tLeft = tLeft > t ? tLeft : t; }
		if (tLeft > tRight) { return false; }
	}
	float t = 0.0f;
	if (directionOpt) { t = dot(opt, L.direction) > 0.0f ? tRight : tLeft; }
	else
	{
		t = dot(L.direction, opt - L.point);
		t = t < tLeft ? tLeft : (t > tRight ? tRight : t);
	}
	result = L.point + t * L.direction;
	return true;
}

/// @return 解けなかった最初の直線の番号。全部満たせたら count
inline int linearProgram2(const Line* lines, int count, float radius, V2 opt, bool directionOpt, V2& result) noexcept
{
	if (directionOpt) { result = opt * radius; }
	else if (absSq(opt) > radius * radius) { result = normalize(opt) * radius; }
	else { result = opt; }
	for (int i = 0; i < count; ++i)
	{
		if (det(lines[i].direction, lines[i].point - result) > 0.0f)
		{
			const V2 keep = result;
			if (!linearProgram1(lines, i, radius, opt, directionOpt, result)) { result = keep; return i; }
		}
	}
	return count;
}

/// @brief 全部を満たせないとき、違反の最大値が最小になる速度を選ぶ
inline void linearProgram3(const Line* lines, int count, int begin, float radius, V2& result) noexcept
{
	float distance = 0.0f;
	for (int i = begin; i < count; ++i)
	{
		if (det(lines[i].direction, lines[i].point - result) <= distance) { continue; }
		Line proj[kMaxLines];
		int  n = 0;
		for (int j = 0; j < i; ++j)
		{
			const float d = det(lines[i].direction, lines[j].direction);
			Line line;
			if (std::fabs(d) <= kEpsilon)
			{
				if (dot(lines[i].direction, lines[j].direction) > 0.0f) { continue; }
				line.point = 0.5f * (lines[i].point + lines[j].point);
			}
			else
			{
				line.point = lines[i].point + (det(lines[j].direction, lines[i].point - lines[j].point) / d) * lines[i].direction;
			}
			line.direction = normalize(lines[j].direction - lines[i].direction);
			proj[n++] = line;
		}
		const V2 keep = result;
		if (linearProgram2(proj, n, radius, V2{-lines[i].direction.y, lines[i].direction.x}, true, result) < n) { result = keep; }
		distance = det(lines[i].direction, lines[i].point - result);
	}
}

/// @brief 相手 1 体から決まる半平面。share は自分が避ける割合。互いに避けるなら 0.5
inline Line orcaLine(V2 pos, V2 vel, V2 opos, V2 ovel, float combined, float share, float invHorizon, float invDt,
                     bool selfFirst) noexcept
{
	const V2    relPos = opos - pos;
	const V2    relVel = vel - ovel;
	const float distSq = absSq(relPos);
	const float rSq    = combined * combined;
	Line line;
	V2   u;
	if (distSq > rSq)
	{
		const V2    w   = relVel - invHorizon * relPos;
		const float wSq = absSq(w);
		const float d1  = dot(w, relPos);
		if (d1 < 0.0f && d1 * d1 > rSq * wSq)
		{
			const float wl = std::sqrt(wSq);
			const V2    uw = w * (1.0f / wl);
			line.direction = V2{uw.y, -uw.x};
			u = (combined * invHorizon - wl) * uw;
		}
		else
		{
			const float leg = std::sqrt(distSq - rSq);
			if (det(relPos, w) > 0.0f)
			{
				line.direction = V2{relPos.x * leg - relPos.y * combined, relPos.x * combined + relPos.y * leg} * (1.0f / distSq);
			}
			else
			{
				line.direction = -V2{relPos.x * leg + relPos.y * combined, -relPos.x * combined + relPos.y * leg} * (1.0f / distSq);
			}
			u = dot(relVel, line.direction) * line.direction - relVel;
		}
	}
	else
	{
		// すでに重なっている。次の 1 tick で離れる速度を求める。位置も同じなら番号の順で左右を決める
		V2 w = relVel - invDt * relPos;
		if (absSq(w) <= 1e-12f) { w = selfFirst ? V2{1.0f, 0.0f} : V2{-1.0f, 0.0f}; }
		const float wl = std::sqrt(absSq(w));
		const V2    uw = w * (1.0f / wl);
		line.direction = V2{uw.y, -uw.x};
		u = (combined * invDt - wl) * uw;
	}
	line.point = vel + share * u;
	return line;
}

} // namespace mitiru::gameai::orca_detail
