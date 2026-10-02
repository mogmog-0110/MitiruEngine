#pragma once

/// @file CollisionWorld_impl.hpp
/// @brief CollisionWorld の問い合わせの中身。CollisionWorld.hpp の末尾から読む。

#include <cmath>

namespace mitiru::action
{

namespace detail
{

[[nodiscard]] inline Source movingSource(const MovingBody& b, std::uint32_t order, const CollisionLevel* level)
{
	Source s;
	s.body     = b.id;
	s.order    = order;
	const Quat q = b.pose.rotation.normalized();
	s.frame    = Frame{b.pose.position, q, q.conjugate(), false};
	if (b.shape == MovingShape::Mesh)
	{
		s.bvh = level != nullptr ? &level->movableMesh(b.mesh) : nullptr;
		return s;
	}
	s.half     = Vec3{absf(b.halfExtents.x), absf(b.halfExtents.y), absf(b.halfExtents.z)};
	s.boxLayer = b.layer;
	return s;
}

/// @brief 並び順: 深い方が先。同じ深さは body の id、三角形の id の小さい方
[[nodiscard]] inline bool contactBefore(const Contact& x, const Contact& y) noexcept
{
	if (x.depth != y.depth) { return x.depth > y.depth; }
	if (x.body != y.body) { return x.body < y.body; }
	return x.triangle < y.triangle;
}

inline int insertContact(Contact* out, int count, int capacity, const Contact& c) noexcept
{
	int pos = count;
	while (pos > 0 && contactBefore(c, out[pos - 1])) { --pos; }
	if (pos >= capacity) { return count; }
	const int last = count < capacity ? count : capacity - 1;
	for (int i = last; i > pos; --i) { out[i] = out[i - 1]; }
	out[pos] = c;
	return count < capacity ? count + 1 : count;
}

/// @brief 半径 0 の掃引はレイと同じ
[[nodiscard]] inline SweepHit rayAsSweep(const RayHit& rh, float length) noexcept
{
	SweepHit out;
	if (!rh.hit) { return out; }
	out.hit           = true;
	out.fraction      = saturate(rh.distance / length);
	out.point         = rh.point;
	out.normal        = rh.normal;
	out.surfaceNormal = rh.normal;
	out.body          = rh.body;
	out.triangle      = rh.triangle;
	out.layer         = rh.layer;
	return out;
}

/// @brief 三角形の面法線を、接触の法線と同じ側へ向けて返す (ローカル座標)
[[nodiscard]] inline Vec3 faceNormalToward(const BvhTriangle& t, const Vec3& contactNormal) noexcept
{
	const Vec3 n = normalizeOr(geom::triangleNormalRaw(t.a, t.b, t.c), contactNormal);
	return dot(n, contactNormal) < 0.0f ? -n : n;
}

[[nodiscard]] inline SweepHit penetrating(const Contact& c) noexcept
{
	SweepHit h;
	h.hit              = true;
	h.fraction         = 0.0f;
	h.point            = c.point;
	h.normal           = c.normal;
	h.surfaceNormal    = c.surfaceNormal;
	h.startPenetrating = true;
	h.penetration      = c.depth;
	h.body             = c.body;
	h.triangle         = c.triangle;
	h.layer            = c.layer;
	return h;
}

} // namespace detail

template <class F>
void CollisionWorld::forEachSource(F&& f) const
{
	if (m_level != nullptr && !m_level->statics().empty())
	{
		detail::Source s;
		s.bvh = &m_level->statics();
		f(s);
	}
	for (std::size_t i = 0; i < m_bodies.size(); ++i)
	{
		if (m_bodies[i].enabled == 0) { continue; }
		const detail::Source s = detail::movingSource(m_bodies[i], static_cast<std::uint32_t>(i + 1), m_level);
		if (m_bodies[i].shape == MovingShape::Mesh && (s.bvh == nullptr || s.bvh->empty())) { continue; }
		f(s);
	}
}

template <class F>
void CollisionWorld::visitRay(const detail::Source& s, const Vec3& o, const Vec3& d, float tMax, const Vec3& inflate,
                              F&& f) const
{
	if (s.bvh == nullptr)
	{
		detail::forEachBoxTriangle(s, [&](const BvhTriangle& t) { tMax = minf(tMax, f(t, tMax)); });
		return;
	}
	if (bruteForce)
	{
		for (const BvhTriangle& t : s.bvh->triangles()) { tMax = minf(tMax, f(t, tMax)); }
		return;
	}
	s.bvh->traverseRay(o, d, tMax, inflate, f);
}

template <class F>
void CollisionWorld::visitBox(const detail::Source& s, const Aabb& box, F&& f) const
{
	if (s.bvh == nullptr) { detail::forEachBoxTriangle(s, f); return; }
	if (bruteForce)
	{
		for (const BvhTriangle& t : s.bvh->triangles()) { f(t); }
		return;
	}
	s.bvh->traverseBox(box, f);
}

inline RayHit CollisionWorld::raycast(const Vec3& origin, const Vec3& dir, float maxDist, std::uint32_t mask,
                                      bool cullBackface) const
{
	RayHit      out;
	const float dl2 = lengthSq(dir);
	if (!(maxDist >= 0.0f) || !(dl2 > 1e-24f)) { return out; }
	const Vec3     d = dir * (1.0f / std::sqrt(dl2));
	detail::Best   best(maxDist);
	BvhTriangle    bt{};
	detail::Source bs{};
	forEachSource([&](const detail::Source& s) {
		const Vec3 ol = s.frame.toLocal(origin), dl = s.frame.toLocalDir(d);
		visitRay(s, ol, dl, best.value, Vec3{}, [&](const BvhTriangle& tri, float cur) -> float {
			float t = 0.0f;
			if (detail::layerMatch(tri.layer, mask) && geom::rayTriangle(ol, dl, tri.a, tri.b, tri.c, cur, cullBackface, t) &&
			    best.accept(t, s.order, tri.id))
			{
				bt = tri;
				bs = s;
			}
			return best.value;
		});
	});
	if (!best.found) { return out; }
	Vec3 nl = normalizeOr(geom::triangleNormalRaw(bt.a, bt.b, bt.c), Vec3{0.0f, 1.0f, 0.0f});
	if (dot(nl, bs.frame.toLocalDir(d)) > 0.0f) { nl = -nl; }
	out.hit      = true;
	out.distance = best.value;
	out.point    = origin + d * best.value;
	out.normal   = normalizeOr(bs.frame.toWorldDir(nl), nl);
	out.body     = bs.body;
	out.triangle = bt.id;
	out.layer    = bt.layer;
	return out;
}

inline SweepHit CollisionWorld::finishSweep(const detail::Best& best, const geom::SweepContact& sc,
                                            const detail::Source& src, const BvhTriangle& tri) const
{
	SweepHit out;
	if (!best.found) { return out; }
	const Vec3 face   = detail::faceNormalToward(tri, sc.normal);
	out.hit           = true;
	out.fraction      = saturate(sc.t);
	out.point         = src.frame.toWorld(sc.point);
	out.normal        = normalizeOr(src.frame.toWorldDir(sc.normal), sc.normal);
	out.surfaceNormal = normalizeOr(src.frame.toWorldDir(face), face);
	out.body          = src.body;
	out.triangle      = best.id;
	out.layer         = tri.layer;
	return out;
}

inline SweepHit CollisionWorld::sphereCast(const Vec3& center, float radius, const Vec3& delta, std::uint32_t mask) const
{
	const float r = maxf(radius, 0.0f);
	Contact     start;
	if (deepestOverlap(center, center, r, mask, start) && start.depth > kContactSlop) { return detail::penetrating(start); }
	const float dl2 = lengthSq(delta);
	if (!(dl2 > 1e-24f)) { return SweepHit{}; }
	if (!(r > 0.0f))
	{
		const float len = std::sqrt(dl2);
		return detail::rayAsSweep(raycast(center, delta, len, mask), len);
	}
	detail::Best       best(1.0f);
	geom::SweepContact bestC;
	detail::Source     bs{};
	BvhTriangle        bt{};
	forEachSource([&](const detail::Source& s) {
		const Vec3 cl = s.frame.toLocal(center), dl = s.frame.toLocalDir(delta);
		visitRay(s, cl, dl, best.value, Vec3{r, r, r}, [&](const BvhTriangle& tri, float) -> float {
			geom::SweepContact sc;
			if (detail::layerMatch(tri.layer, mask) && geom::sweepSphereTriangle(cl, r, dl, tri.a, tri.b, tri.c, sc) &&
			    best.accept(sc.t, s.order, tri.id))
			{
				bestC = sc;
				bs    = s;
				bt    = tri;
			}
			return best.value;
		});
	});
	return finishSweep(best, bestC, bs, bt);
}

inline SweepHit CollisionWorld::capsuleCast(const Capsule& capsule, const Vec3& delta, std::uint32_t mask) const
{
	const float r = maxf(capsule.radius, 0.0f);
	Contact     start;
	if (deepestOverlap(capsule.a, capsule.b, r, mask, start) && start.depth > kContactSlop)
	{
		return detail::penetrating(start);
	}
	if (!(lengthSq(delta) > 1e-24f)) { return SweepHit{}; }
	detail::Best       best(1.0f);
	geom::SweepContact bestC;
	detail::Source     bs{};
	BvhTriangle        bt{};
	forEachSource([&](const detail::Source& s) {
		const Vec3 pa = s.frame.toLocal(capsule.a), pb = s.frame.toLocal(capsule.b), dl = s.frame.toLocalDir(delta);
		const Vec3 mid = (pa + pb) * 0.5f, he = pb - mid;
		const Vec3 inflate{absf(he.x) + r, absf(he.y) + r, absf(he.z) + r};
		visitRay(s, mid, dl, best.value, inflate, [&](const BvhTriangle& tri, float) -> float {
			geom::SweepContact sc;
			if (detail::layerMatch(tri.layer, mask) &&
			    geom::sweepCapsuleTriangle(pa, pb, r, dl, tri.a, tri.b, tri.c, sc) && best.accept(sc.t, s.order, tri.id))
			{
				bestC = sc;
				bs    = s;
				bt    = tri;
			}
			return best.value;
		});
	});
	return finishSweep(best, bestC, bs, bt);
}

/// @brief カプセル p-q (p == q なら球) と重なる三角形をすべて sink(const Contact&) に渡す
template <class Sink>
void CollisionWorld::forEachOverlap(const Vec3& p, const Vec3& q, float r, std::uint32_t mask, Sink&& sink) const
{
	const bool isSphere = p == q;
	forEachSource([&](const detail::Source& s) {
		const Vec3 pl = s.frame.toLocal(p);
		const Vec3 ql = isSphere ? pl : s.frame.toLocal(q);
		Aabb       box;
		box.expand(pl);
		box.expand(ql);
		visitBox(s, box.inflated(Vec3{r, r, r}), [&](const BvhTriangle& tri) {
			geom::TriContact tc;
			if (!detail::layerMatch(tri.layer, mask)) { return; }
			const bool hit = isSphere ? geom::sphereTriangle(pl, r, tri.a, tri.b, tri.c, tc)
			                          : geom::capsuleTriangle(pl, ql, r, tri.a, tri.b, tri.c, tc);
			if (!hit) { return; }
			Contact c;
			c.point    = s.frame.toWorld(tc.point);
			c.normal   = normalizeOr(s.frame.toWorldDir(tc.normal), tc.normal);
			c.surfaceNormal = normalizeOr(s.frame.toWorldDir(detail::faceNormalToward(tri, tc.normal)), tc.normal);
			c.depth    = tc.depth;
			c.body     = s.body;
			c.triangle = tri.id;
			c.layer    = tri.layer;
			sink(c);
		});
	});
}

inline bool CollisionWorld::deepestOverlap(const Vec3& p, const Vec3& q, float r, std::uint32_t mask, Contact& out) const
{
	bool found = false;
	forEachOverlap(p, q, r, mask, [&](const Contact& c) {
		if (!found || detail::contactBefore(c, out)) { out = c; found = true; }
	});
	return found;
}

inline int CollisionWorld::overlapSphere(const Vec3& center, float radius, Contact* out, int capacity,
                                         std::uint32_t mask) const
{
	if (out == nullptr || capacity <= 0) { return 0; }
	int count = 0;
	forEachOverlap(center, center, maxf(radius, 0.0f), mask,
	               [&](const Contact& c) { count = detail::insertContact(out, count, capacity, c); });
	return count;
}

inline int CollisionWorld::overlapCapsule(const Capsule& capsule, Contact* out, int capacity, std::uint32_t mask) const
{
	if (out == nullptr || capacity <= 0) { return 0; }
	int count = 0;
	forEachOverlap(capsule.a, capsule.b, maxf(capsule.radius, 0.0f), mask,
	               [&](const Contact& c) { count = detail::insertContact(out, count, capacity, c); });
	return count;
}

inline RayHit CollisionWorld::closestPoint(const Vec3& point, float maxDist, std::uint32_t mask) const
{
	RayHit out;
	if (!(maxDist >= 0.0f)) { return out; }
	detail::Best     best(maxDist * maxDist);
	geom::TriClosest bestCp;
	BvhTriangle      bt{};
	detail::Source   bs{};
	Vec3             bestLocal{};
	forEachSource([&](const detail::Source& s) {
		const Vec3 pl   = s.frame.toLocal(point);
		const auto test = [&](const BvhTriangle& tri, float) -> float {
			if (!detail::layerMatch(tri.layer, mask)) { return best.value; }
			const geom::TriClosest cp = geom::closestPointOnTriangle(pl, tri.a, tri.b, tri.c);
			if (best.accept(lengthSq(pl - cp.point), s.order, tri.id)) { bestCp = cp; bt = tri; bs = s; bestLocal = pl; }
			return best.value;
		};
		if (s.bvh != nullptr && !bruteForce) { s.bvh->traverseClosest(pl, best.value, test); }
		else if (s.bvh != nullptr) { for (const BvhTriangle& t : s.bvh->triangles()) { test(t, 0.0f); } }
		else { detail::forEachBoxTriangle(s, [&](const BvhTriangle& t) { test(t, 0.0f); }); }
	});
	if (!best.found) { return out; }
	const Vec3 n  = normalizeOr(geom::triangleNormalRaw(bt.a, bt.b, bt.c), Vec3{0.0f, 1.0f, 0.0f});
	const Vec3 dv = bestLocal - bestCp.point;
	const Vec3 nl = bestCp.feature == geom::TriFeature::Face ? (dot(dv, n) < 0.0f ? -n : n) : normalizeOr(dv, n);
	out.hit      = true;
	out.distance = std::sqrt(best.value);
	out.point    = bs.frame.toWorld(bestCp.point);
	out.normal   = normalizeOr(bs.frame.toWorldDir(nl), nl);
	out.body     = bs.body;
	out.triangle = bt.id;
	out.layer    = bt.layer;
	return out;
}

} // namespace mitiru::action
