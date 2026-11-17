#pragma once

/// @file FluidThreadShape.hpp
/// @brief 2 点のあいだに張る糸の形。体側が太く、途中でくびれ、先に雫を付けられる。状態は持たない。
/// @details FluidBodyState には何も足さない。呼び手が始点、終点、張り、時刻を渡し、できた形を fluidAddLimb で体の面に足す。
///          調整値の既定は、固定の定数だった頃の値に合わせてある。

#include <algorithm>
#include <cmath>

#include <mitiru/softbody/FluidShapes.hpp>

namespace mitiru::softbody
{

struct FluidThreadParams
{
	float rootRadius = 0.2f;     ///< 始点 (体側) の半径 (m)
	float footRadius = 0.13f;    ///< 終点側の濡れ跡の半径 (m)。糸の先の半径は footShare 倍
	float footShare = 0.6f;
	float neckRadius = 0.04f;    ///< 切れる直前のくびれの半径 (m)
	float neckSlack = 0.75f;     ///< 張っていないときのくびれの半径の、rootRadius に対する割合
	float thinPower = 1.4f;      ///< 張るにつれてくびれが細る速さ (大きいほど切れる間際まで太い)
	float smooth = 0.0f;         ///< 0 なら 5 分割の素直な形。0 より大きいと細かく分け、くびれを張りで終点側へ寄せ、ゆっくりうねらせる割合
	float bulb = 0.0f;           ///< smooth が効いているとき、終点の先の雫の膨らみ (終点側の半径に対する割合 × 張り)
	int segments = 0;            ///< 分割数。3 未満は smooth が 0 なら 5、それ以外は 9
	float droop = 0.25f;         ///< ゆるい糸の真ん中が下へ垂れる量 (糸の長さに対する割合)
	float droopPower = 1.0f;     ///< 垂れが張りで消える曲がり (大きいほど張る間際まで垂れが残る)
	float droopFloor = 0.0f;     ///< 張り切っても残る垂れの割合 (0 なら張ると真っすぐ)
	float wobble = 0.05f;        ///< 張った糸の横揺れの振れ幅 (糸の長さに対する割合)
	float wobbleHz = 1.3f;
	float sway = 0.06f;          ///< smooth が効いているときの、ゆるい糸の横揺れの振れ幅 (糸の長さに対する割合)
	float swayHz = 0.9f;
	float swayWaves = 0.8f;      ///< 揺れが糸に沿って進む波の数
	float rootPower = 1.6f;      ///< smooth のとき、始点からくびれへの太さの落ち方の指数 (大きいほど根元が長く太い)
	float footPower = 2.4f;      ///< くびれから終点への太さの戻り方の指数
	float neckShift = 0.18f;     ///< smooth のとき、張りでくびれが終点側へ寄る量
	float startOffset = 0.1f;    ///< 始点を終点の向きへずらす量 (m)。始点が体の中心なら、体の中から糸を出さない分
	float footPad = 0.0f;        ///< 0 より大きいと、終点に面へ貼る濡れ跡の楕円体を足す。横の半径は footRadius のこの倍
	float footPadThickness = 0.06f;
};

struct FluidThreadInput
{
	Vec3 start{};                       ///< 始点 (体側)
	Vec3 end{};                         ///< 終点 (面側)
	float tension = 0.0f;               ///< 張り (0 はゆるい、1 は張り切り。切れた糸の戻りは 1)
	float time = 0.0f;                  ///< 揺れの位相に使う時刻 (秒)
	float weight = 1.0f;                ///< 半径に掛ける 0〜1。出てくる、切れて消えるときに下げる
	Vec3 up{0.0f, 1.0f, 0.0f};          ///< 垂れる向きの逆
	Vec3 surfaceNormal{0.0f, 1.0f, 0.0f};   ///< 濡れ跡を貼る面の外向き
};

/// 1 本の糸が使う形の数の上限 (分割 + 濡れ跡)
inline constexpr int kFluidThreadMaxShapes = 33;

namespace detail
{

[[nodiscard]] inline FluidShape fluidThreadCapsule(const Vec3& a, const Vec3& b, float ra, float rb) noexcept
{
	FluidShape c;
	c.kind = FluidShape::Kind::Capsule;
	c.a = a;
	c.b = b;
	c.ra = ra;
	c.rb = rb;
	return c;
}

[[nodiscard]] inline Basis fluidThreadSurfaceBasis(const Vec3& n) noexcept
{
	const Vec3 helper = std::fabs(n.y) < 0.9f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
	const Vec3 t1 = normalizeOr(n.cross(helper));
	return {t1, n, n.cross(t1)};
}

} // namespace detail

/// @brief 糸の形を out に書き、書いた数を返す。capacity に収まる分だけ書き、濡れ跡は最後に空きがあるときだけ書く。
/// 重みが小さすぎる、始点と終点が近すぎる、capacity が 3 未満のいずれかなら、何も書かず 0 を返す。
[[nodiscard]] inline int buildFluidThread(const FluidThreadParams& k, const FluidThreadInput& in, FluidShape* out, int capacity) noexcept
{
	if (out == nullptr || capacity < 3 || in.weight < 0.02f) { return 0; }
	const Vec3 axis = in.end - in.start;
	const float len = length(axis);
	if (len < 1e-3f) { return 0; }
	const Vec3 dir = axis / len;
	const float w = in.weight;
	const float tension = clamp01(in.tension);
	const float thin = fastPow(1.0f - tension, k.thinPower);
	const float neck = (k.neckRadius + (k.neckSlack * k.rootRadius - k.neckRadius) * thin) * w;
	const float rootR = k.rootRadius * w;
	const float footR = k.footRadius * k.footShare * w;
	const float slack = 1.0f - tension;
	const float slackShare = k.droopPower == 1.0f ? slack : fastPow(slack, k.droopPower);
	const float droop = std::fmax(slackShare, k.droopFloor) * k.droop * len;
	const Vec3 start = in.start + dir * k.startOffset;
	const Vec3 side = normalizeOr(dir.cross(in.up));
	constexpr float kTwoPi = 6.2831853f;
	const Vec3 wobble = side * (k.wobble * len * tension * std::sin(kTwoPi * k.wobbleHz * in.time));
	const bool smooth = k.smooth > 0.0f;
	const int wanted = k.segments >= 3 ? k.segments : (smooth ? 9 : 5);
	const int room = capacity - (k.footPad > 0.0f ? 1 : 0);
	const int segments = std::max(3, std::min(wanted, std::min(room, kFluidThreadMaxShapes - 1)));
	const float neckAt = smooth ? std::clamp(0.5f + k.smooth * k.neckShift * tension, 0.05f, 0.95f) : 0.5f;
	const float sway = smooth ? k.smooth * k.sway * len * slack : 0.0f;
	Vec3 prev = start;
	float prevR = rootR;
	int count = 0;
	for (int n = 1; n <= segments; ++n)
	{
		const float t = static_cast<float>(n) / static_cast<float>(segments);
		const float away = smooth ? (t < neckAt ? (neckAt - t) / neckAt : (t - neckAt) / (1.0f - neckAt)) : std::fabs(2.0f * t - 1.0f);
		const float ends = rootR + (footR - rootR) * t;
		const float shaped = smooth ? fastPow(away, t < neckAt ? k.rootPower : k.footPower) : away * std::sqrt(away);
		float r = neck + (ends - neck) * shaped;
		if (smooth && n == segments) { r = footR * (1.0f + k.bulb * tension); }
		const float bow = 4.0f * t * (1.0f - t);
		const Vec3 travel = side * (sway * bow * std::sin(kTwoPi * (k.swayHz * in.time - k.swayWaves * t)));
		const Vec3 at = start + (in.end - start) * t - in.up * (droop * bow) + wobble * bow + travel;
		out[count++] = detail::fluidThreadCapsule(prev, n == segments ? in.end : at, prevR, r);
		prev = at;
		prevR = r;
	}
	if (k.footPad > 0.0f && count < capacity)
	{
		FluidShape pad;
		pad.kind = FluidShape::Kind::Ellipsoid;
		pad.axes = detail::fluidThreadSurfaceBasis(in.surfaceNormal);
		pad.half = Vec3{k.footRadius * k.footPad * w, k.footPadThickness * w, k.footRadius * k.footPad * w};
		pad.a = in.end + in.surfaceNormal * 0.01f;
		out[count++] = pad;
	}
	return count;
}

} // namespace mitiru::softbody
