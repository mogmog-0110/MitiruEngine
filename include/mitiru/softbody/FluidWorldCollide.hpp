#pragma once

/// @file FluidWorldCollide.hpp
/// @brief 流体パーティクルを CollisionWorld の地形 (箱、坂、動く箱、三角形メッシュ) の外へ押し出す FluidCollideFn。
/// @details bindWorldCollision で FluidStepInput::collide に渡す。ゲームが平面や形を足したいときは、自分の callback から
///          collideWithWorld を呼ぶ。パーティクルごとに独立に答えを決め、user を書き換えず、呼び出しの間で何も覚えない。
///          そのため FluidParallel で範囲を分けて呼んでも、同じ入力なら同じビットになる。
///
///          FluidWorldSide::Solid では、三角形の裏側を地形の中とする。CollisionWorld::closestPoint は両面の判定で、
///          箱の中の点には内向きの法線を返すので、そのまま使うと中のパーティクルが奥へ押される。ここでは最近の三角形の巻き
///          (反時計回りが表) で内外を決め、中のパーティクルも一番近い面の外へ戻す。穴の開いたメッシュや巻きのそろわない
///          メッシュには Sheet を使う (内外を見分けず、いる側へ押すだけ)。
///
///          既知の限界: 最近の三角形だけで内外を決めるので、箱どうしが重なる、または面で接する継ぎ目では、
///          埋もれた面が最近になって中のパーティクルを外と判定することがある。reach より深いパーティクルは動かさない。

#include <cmath>
#include <cstdint>

#include <mitiru/action/CollisionWorld.hpp>
#include <mitiru/softbody/FluidBody.hpp>

namespace mitiru::softbody
{

enum class FluidWorldSide : std::uint8_t
{
	Solid,  ///< 三角形の裏側は地形の中。中のパーティクルも表へ戻す
	Sheet,  ///< 両面の薄い板。パーティクルがいる側へ押すだけで、中は見分けない
};

struct FluidWorldCollider
{
	const action::CollisionWorld* world = nullptr;
	float radius = 0.04f;  ///< パーティクルの中心を面から離しておく距離 (m)。パーティクルの半径に、ゲームが見た目のために足す隙間を足した値
	float reach = 0.3f;    ///< 面の裏へこの深さまでめり込んだパーティクルを表へ戻す (m)。1 substep の最大の動きより大きくする。radius 以上に切り上げる
	std::uint32_t mask = action::kAllLayers;
	std::int32_t passes = 2;  ///< 1 つのパーティクルを押す回数の上限。凹んだ角で 2 つ目の面に残る分を取る。押さなかった時点で止める
	FluidWorldSide side = FluidWorldSide::Solid;
	std::uint8_t pad[3]{};
};

namespace detail
{

/// 浮動小数の丸めで同じ面への押し出しを繰り返さないための許容値（m）
inline constexpr float kFluidWorldSlop = 1e-6f;

struct FluidSurfaceHit
{
	Vec3 point{};      ///< 面の上の最近点
	Vec3 front{};      ///< 最近の三角形の表の単位法線 (巻きで決まる)
	Vec3 toward{};     ///< 面から問い合わせの点へ向かう単位ベクトル (距離 0 なら front)
	float distance = 0.0f;
	float planeDistance = 0.0f;  ///< 最近の三角形の平面までの符号付きの距離 (表が正)。onFace のとき押し出しに使う
	std::uint32_t body = action::kLevelBody;
	std::uint32_t triangle = 0;
	bool found = false;
	bool onFace = false;  ///< 最近の点が三角形の内側にある (縁や頂点でも、点が縁の真上なら真)
	bool behind = false;  ///< 点が最近の面の裏にいる。onFace のときだけ真にする
};

/// CollisionWorld::closestPoint と同じ順序と同点時の決め方で、最も近い三角形を選び、表裏も返す。
/// 最も近い特徴が縁や頂点の場合は、法線に平行でなければ外側とみなす。凸の立体内では最も近い特徴が必ず面の内側に
/// あるので、鋭いくさびの先の外側を、隣の面との内積で内側と誤判定しないため。
[[nodiscard]] inline FluidSurfaceHit fluidClosestSurface(const action::CollisionWorld& world, const Vec3& point,
                                                         float maxDist, std::uint32_t mask) noexcept
{
	namespace act = action;
	FluidSurfaceHit out;
	if (!(maxDist >= 0.0f)) { return out; }
	act::detail::Best best(maxDist * maxDist);
	act::geom::TriClosest bestCp;
	act::BvhTriangle bt{};
	act::detail::Source bs{};
	Vec3 bestLocal{};
	const auto visit = [&](const act::detail::Source& s) {
		const Vec3 pl = s.frame.toLocal(point);
		const auto test = [&](const act::BvhTriangle& tri, float) -> float {
			if (!act::detail::layerMatch(tri.layer, mask)) { return best.value; }
			const act::geom::TriClosest cp = act::geom::closestPointOnTriangle(pl, tri.a, tri.b, tri.c);
			if (best.accept(act::lengthSq(pl - cp.point), s.order, tri.id))
			{
				bestCp = cp;
				bt = tri;
				bs = s;
				bestLocal = pl;
			}
			return best.value;
		};
		if (s.bvh != nullptr && !world.bruteForce) { s.bvh->traverseClosest(pl, best.value, test); }
		else if (s.bvh != nullptr) { for (const act::BvhTriangle& t : s.bvh->triangles()) { test(t, 0.0f); } }
		else
		{
			// 動く箱は 12 枚の三角形を毎回調べるため、答えに影響しない場合は外接球でまとめて除外する。丸めを考慮して外接球を少し広げる。
			const float lower = act::length(pl) - act::length(s.half) * 1.0001f;
			if (lower > 0.0f && lower * lower > best.value) { return; }
			act::detail::forEachBoxTriangle(s, [&](const act::BvhTriangle& t) { test(t, 0.0f); });
		}
	};
	const act::CollisionLevel* level = world.level();
	if (level != nullptr && !level->statics().empty())
	{
		act::detail::Source s;
		s.bvh = &level->statics();
		visit(s);
	}
	const auto bodies = world.bodies();
	for (std::size_t i = 0; i < bodies.size(); ++i)
	{
		if (bodies[i].enabled == 0) { continue; }
		const act::detail::Source s = act::detail::movingSource(bodies[i], static_cast<std::uint32_t>(i + 1), level);
		if (bodies[i].shape == act::MovingShape::Mesh && (s.bvh == nullptr || s.bvh->empty())) { continue; }
		visit(s);
	}
	if (!best.found) { return out; }
	const Vec3 up{0.0f, 1.0f, 0.0f};
	const Vec3 nLocal = act::normalizeOr(act::geom::triangleNormalRaw(bt.a, bt.b, bt.c), up);
	const Vec3 dv = bestLocal - bestCp.point;
	const float dist = std::sqrt(best.value);
	const Vec3 towardLocal = dist > 1e-9f ? dv / dist : nLocal;
	out.found = true;
	out.distance = dist;
	out.point = bs.frame.toWorld(bestCp.point);
	out.front = act::normalizeOr(bs.frame.toWorldDir(nLocal), nLocal);
	out.toward = act::normalizeOr(bs.frame.toWorldDir(towardLocal), towardLocal);
	// 大きな三角形では、最近点の約 1e-6 m の誤差で dv の向きがずれるため、面上の押し出しは平面からの距離で決める。
	const float plane = act::dot(bestLocal - bt.a, nLocal);
	// 点が箱の面の対角線の真上にあると、最も近い特徴が縁になる。その場合も、dv が法線に平行なら面として扱う。
	out.onFace = bestCp.feature == act::geom::TriFeature::Face || plane * plane >= 0.999f * best.value;
	out.planeDistance = plane;
	out.behind = out.onFace && plane < 0.0f;
	out.body = bs.body;
	out.triangle = bt.id;
	return out;
}

/// 動かしたら true。false を返したパーティクルは次の pass も同じ答えになるので、呼び手は止める
[[nodiscard]] inline bool fluidPushOutOfWorld(const FluidWorldCollider& c, float search, Vec3& p) noexcept
{
	const FluidSurfaceHit hit = fluidClosestSurface(*c.world, p, search, c.mask);
	if (!hit.found) { return false; }
	if (hit.onFace)
	{
		const float depth = hit.planeDistance;
		if (depth < 0.0f && c.side == FluidWorldSide::Sheet)
		{
			if (!(-depth < c.radius - kFluidWorldSlop)) { return false; }
			p += hit.front * (-c.radius - depth);
			return true;
		}
		if (depth >= 0.0f && !(depth < c.radius - kFluidWorldSlop)) { return false; }
		p += hit.front * (c.radius - depth);
		return true;
	}
	if (!(hit.distance < c.radius - kFluidWorldSlop)) { return false; }
	p = hit.point + hit.toward * c.radius;
	return true;
}

} // namespace detail

/// @brief パーティクルの位置を CollisionWorld の地形の外へ押し出す。user は const FluidWorldCollider*。
///        NaN や無限大の位置は変更しない。
inline void collideWithWorld(void* user, Vec3* positions, int count)
{
	if (user == nullptr || positions == nullptr) { return; }
	FluidWorldCollider c = *static_cast<const FluidWorldCollider*>(user);
	if (c.world == nullptr) { return; }
	// 負の radius は面の裏側へ押してしまうので、0 (面の上に置く) に丸める。NaN も同じ
	if (!(c.radius > 0.0f)) { c.radius = 0.0f; }
	const float search = c.reach > c.radius ? c.reach : c.radius;
	for (int i = 0; i < count; ++i)
	{
		Vec3 p = positions[i];
		if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) { continue; }
		for (int pass = 0; pass < c.passes; ++pass)
		{
			if (!detail::fluidPushOutOfWorld(c, search, p)) { break; }
		}
		positions[i] = p;
	}
}

/// @brief FluidStepInput::collide に collideWithWorld を設定し、collider を渡す。
///        collider は fluidStep の呼び出し中だけ生きていればよい。
/// 既存の collide は上書きする。独自の callback と組み合わせる場合は、その callback から collideWithWorld を呼ぶ。
inline void bindWorldCollision(FluidStepInput& in, const FluidWorldCollider& collider) noexcept
{
	in.collide = &collideWithWorld;
	in.collideUser = const_cast<FluidWorldCollider*>(&collider);
}

/// 一時オブジェクトの住所を collideUser に残すと fluidStep の前に消えるので、渡せないようにする
void bindWorldCollision(FluidStepInput&, const FluidWorldCollider&&) = delete;

} // namespace mitiru::softbody
