#pragma once

/// @file PhysicsEngine2D.hpp
/// @brief 2D物理エンジン — ブロードフェーズ、ナローフェーズ、インパルス接触解決を統合

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "sgc/math/Vec2.hpp"

namespace mitiru::physics2d
{

using BodyId = uint32_t;
inline constexpr BodyId INVALID_BODY_ID = 0;

// ── コライダー ────────────────────────────────────────────

struct CircleCollider2D
{
	sgc::Vec2f offset{};
	float radius{0.5f};
};

struct AABBCollider2D
{
	sgc::Vec2f min{-0.5f, -0.5f};
	sgc::Vec2f max{0.5f, 0.5f};
	[[nodiscard]] constexpr sgc::Vec2f center() const noexcept { return (min + max) * 0.5f; }
	[[nodiscard]] constexpr sgc::Vec2f halfExtents() const noexcept { return (max - min) * 0.5f; }
};

struct OBBCollider2D
{
	sgc::Vec2f offset{};
	sgc::Vec2f halfExtents{0.5f, 0.5f};
	float rotation{0.0f};

	[[nodiscard]] sgc::Vec2f axis(float bodyAngle, int idx) const noexcept
	{
		const float a = bodyAngle + rotation;
		const float c = std::cos(a), s = std::sin(a);
		return (idx == 0) ? sgc::Vec2f{c, s} : sgc::Vec2f{-s, c};
	}

	void worldVertices(const sgc::Vec2f& wc, float bodyAngle, sgc::Vec2f v[4]) const noexcept
	{
		const sgc::Vec2f e0 = axis(bodyAngle, 0) * halfExtents.x;
		const sgc::Vec2f e1 = axis(bodyAngle, 1) * halfExtents.y;
		v[0] = wc - e0 - e1; v[1] = wc + e0 - e1;
		v[2] = wc + e0 + e1; v[3] = wc - e0 + e1;
	}
};

using Collider2D = std::variant<CircleCollider2D, AABBCollider2D, OBBCollider2D>;

// ── 剛体・接触 ────────────────────────────────────────────

struct RigidBody2D
{
	sgc::Vec2f position{}, velocity{}, acceleration{};
	float mass{1.0f}, inverseMass{1.0f}, restitution{0.3f}, friction{0.5f};
	bool isStatic{false}, isKinematic{false};
	float angle{0.0f};
	sgc::Vec2f force{};
	Collider2D collider{CircleCollider2D{}};
};

struct Contact2D
{
	BodyId bodyA{INVALID_BODY_ID}, bodyB{INVALID_BODY_ID};
	sgc::Vec2f normal{};
	float penetration{0.0f};
	sgc::Vec2f contactPoint{};
};

struct RaycastHit2D
{
	BodyId bodyId{INVALID_BODY_ID};
	sgc::Vec2f point{}, normal{};
	float distance{0.0f};
};

struct WorldAABB2D
{
	sgc::Vec2f min{}, max{};
	[[nodiscard]] constexpr bool overlaps(const WorldAABB2D& o) const noexcept
	{
		return !(max.x < o.min.x || min.x > o.max.x || max.y < o.min.y || min.y > o.max.y);
	}
};

// ── 空間ハッシュグリッド ──────────────────────────────────

class SpatialHashGrid
{
public:
	explicit SpatialHashGrid(float cellSize = 2.0f) noexcept
		: m_cellSize(cellSize), m_invCell(cellSize > 0 ? 1.0f / cellSize : 1.0f) {}

	void setCellSize(float s) noexcept { m_cellSize = s; m_invCell = s > 0 ? 1.0f / s : 1.0f; }
	[[nodiscard]] constexpr float cellSize() const noexcept { return m_cellSize; }
	void clear() noexcept { m_cells.clear(); }

	void insert(BodyId id, const WorldAABB2D& aabb) noexcept
	{
		const int x0 = coord(aabb.min.x), y0 = coord(aabb.min.y);
		const int x1 = coord(aabb.max.x), y1 = coord(aabb.max.y);
		for (int y = y0; y <= y1; ++y)
			for (int x = x0; x <= x1; ++x)
				m_cells[key(x, y)].push_back(id);
	}

	[[nodiscard]] std::vector<std::pair<BodyId, BodyId>> queryAllPairs() const noexcept
	{
		std::unordered_set<uint64_t> seen;
		std::vector<std::pair<BodyId, BodyId>> pairs;
		for (const auto& [h, ids] : m_cells)
		{
			for (std::size_t i = 0; i < ids.size(); ++i)
				for (std::size_t j = i + 1; j < ids.size(); ++j)
				{
					const BodyId a = std::min(ids[i], ids[j]), b = std::max(ids[i], ids[j]);
					if (a != b && seen.insert((uint64_t(a) << 32) | b).second)
						pairs.emplace_back(a, b);
				}
		}
		return pairs;
	}

	[[nodiscard]] std::vector<BodyId> query(const WorldAABB2D& aabb) const noexcept
	{
		std::unordered_set<BodyId> found;
		const int x0 = coord(aabb.min.x), y0 = coord(aabb.min.y);
		const int x1 = coord(aabb.max.x), y1 = coord(aabb.max.y);
		for (int y = y0; y <= y1; ++y)
			for (int x = x0; x <= x1; ++x)
			{
				auto it = m_cells.find(key(x, y));
				if (it != m_cells.end())
					for (BodyId id : it->second) found.insert(id);
			}
		return {found.begin(), found.end()};
	}

private:
	int coord(float v) const noexcept { return int(std::floor(v * m_invCell)); }
	static uint64_t key(int x, int y) noexcept
	{
		return (uint64_t(uint32_t(x)) << 32) | uint64_t(uint32_t(y));
	}

	float m_cellSize, m_invCell;
	std::unordered_map<uint64_t, std::vector<BodyId>> m_cells;
};

// ── 衝突検出 ──────────────────────────────────────────────

namespace CollisionDetection2D
{

[[nodiscard]] inline std::optional<Contact2D> circleVsCircle(
	const sgc::Vec2f& pA, float rA, const sgc::Vec2f& pB, float rB) noexcept
{
	const sgc::Vec2f d = pB - pA;
	const float dSq = d.lengthSquared(), rSum = rA + rB;
	if (dSq >= rSum * rSum) return std::nullopt;
	const float dist = std::sqrt(dSq);
	Contact2D c;
	c.normal = (dist > 1e-6f) ? d / dist : sgc::Vec2f{0, 1};
	c.penetration = rSum - dist;
	c.contactPoint = pA + c.normal * (rA - c.penetration * 0.5f);
	return c;
}

[[nodiscard]] inline std::optional<Contact2D> aabbVsAabb(
	const sgc::Vec2f& mnA, const sgc::Vec2f& mxA,
	const sgc::Vec2f& mnB, const sgc::Vec2f& mxB) noexcept
{
	if (mxA.x < mnB.x || mnA.x > mxB.x || mxA.y < mnB.y || mnA.y > mxB.y) return std::nullopt;
	const float ox = std::min(mxA.x, mxB.x) - std::max(mnA.x, mnB.x);
	const float oy = std::min(mxA.y, mxB.y) - std::max(mnA.y, mnB.y);
	const sgc::Vec2f cA = (mnA + mxA) * 0.5f, cB = (mnB + mxB) * 0.5f;
	Contact2D c;
	if (ox <= oy)
	{
		c.penetration = ox;
		c.normal = {(cB.x > cA.x) ? 1.0f : -1.0f, 0.0f};
	}
	else
	{
		c.penetration = oy;
		c.normal = {0.0f, (cB.y > cA.y) ? 1.0f : -1.0f};
	}
	c.contactPoint = {(std::max(mnA.x, mnB.x) + std::min(mxA.x, mxB.x)) * 0.5f,
					  (std::max(mnA.y, mnB.y) + std::min(mxA.y, mxB.y)) * 0.5f};
	return c;
}

[[nodiscard]] inline std::optional<Contact2D> circleVsAabb(
	const sgc::Vec2f& cp, float r, const sgc::Vec2f& mn, const sgc::Vec2f& mx) noexcept
{
	const sgc::Vec2f closest{std::clamp(cp.x, mn.x, mx.x), std::clamp(cp.y, mn.y, mx.y)};
	const sgc::Vec2f d = cp - closest;
	const float dSq = d.lengthSquared();
	if (dSq >= r * r) return std::nullopt;
	const float dist = std::sqrt(dSq);
	Contact2D c;
	if (dist > 1e-6f)
	{
		c.normal = d / dist;
	}
	else
	{
		const sgc::Vec2f ctr = (mn + mx) * 0.5f, he = (mx - mn) * 0.5f, dl = cp - ctr;
		c.normal = (he.x - std::abs(dl.x) < he.y - std::abs(dl.y))
			? sgc::Vec2f{dl.x >= 0 ? 1.f : -1.f, 0} : sgc::Vec2f{0, dl.y >= 0 ? 1.f : -1.f};
	}
	c.penetration = r - dist;
	c.contactPoint = closest;
	return c;
}

inline void project(const sgc::Vec2f* v, int n, const sgc::Vec2f& ax, float& lo, float& hi) noexcept
{
	lo = hi = v[0].dot(ax);
	for (int i = 1; i < n; ++i)
	{
		const float p = v[i].dot(ax);
		if (p < lo) lo = p; if (p > hi) hi = p;
	}
}

[[nodiscard]] inline std::optional<Contact2D> obbVsObb(
	const sgc::Vec2f& cA, const OBBCollider2D& oA, float aA,
	const sgc::Vec2f& cB, const OBBCollider2D& oB, float aB) noexcept
{
	sgc::Vec2f vA[4], vB[4];
	oA.worldVertices(cA, aA, vA);
	oB.worldVertices(cB, aB, vB);
	const sgc::Vec2f axes[4] = {oA.axis(aA, 0), oA.axis(aA, 1), oB.axis(aB, 0), oB.axis(aB, 1)};
	float minOv = 1e30f; sgc::Vec2f best{};
	for (int i = 0; i < 4; ++i)
	{
		float lA, hA, lB, hB;
		project(vA, 4, axes[i], lA, hA);
		project(vB, 4, axes[i], lB, hB);
		const float ov = std::min(hA, hB) - std::max(lA, lB);
		if (ov <= 0) return std::nullopt;
		if (ov < minOv) { minOv = ov; best = axes[i]; }
	}
	if ((cB - cA).dot(best) < 0) best = -best;
	Contact2D c;
	c.normal = best; c.penetration = minOv; c.contactPoint = (cA + cB) * 0.5f;
	return c;
}

} // namespace CollisionDetection2D

// ── インパルスソルバー ────────────────────────────────────

namespace ImpulseSolver
{

inline void resolve(const Contact2D& ct, RigidBody2D& a, RigidBody2D& b) noexcept
{
	const float imA = a.isStatic ? 0.f : a.inverseMass;
	const float imB = b.isStatic ? 0.f : b.inverseMass;
	const float tot = imA + imB;
	if (tot < 1e-8f) return;

	const sgc::Vec2f& n = ct.normal;
	const sgc::Vec2f rv = b.velocity - a.velocity;
	const float vn = rv.dot(n);
	if (vn > 0) return;

	const float e = std::min(a.restitution, b.restitution);
	const float j = -(1.0f + e) * vn / tot;
	const sgc::Vec2f imp = n * j;

	if (!a.isStatic) a.velocity -= imp * imA;
	if (!b.isStatic) b.velocity += imp * imB;

	// 摩擦
	const sgc::Vec2f rv2 = b.velocity - a.velocity;
	const sgc::Vec2f tang = rv2 - n * rv2.dot(n);
	const float tLsq = tang.lengthSquared();
	if (tLsq > 1e-10f)
	{
		const sgc::Vec2f t = tang / std::sqrt(tLsq);
		const float jt = -rv2.dot(t) / tot;
		const float mu = std::sqrt(a.friction * b.friction);
		const sgc::Vec2f fi = (std::abs(jt) <= j * mu) ? t * jt : t * (-j * mu);
		if (!a.isStatic) a.velocity -= fi * imA;
		if (!b.isStatic) b.velocity += fi * imB;
	}

	// Baumgarte位置補正
	const float corr = std::max(ct.penetration - 0.01f, 0.0f) * 0.8f / tot;
	const sgc::Vec2f cv = n * corr;
	if (!a.isStatic) a.position -= cv * imA;
	if (!b.isStatic) b.position += cv * imB;
}

} // namespace ImpulseSolver

// ── PhysicsWorld2D ────────────────────────────────────────

using CollisionCallback2D = std::function<void(const Contact2D&)>;

class PhysicsWorld2D
{
public:
	PhysicsWorld2D() = default;

	void setGravity(const sgc::Vec2f& g) noexcept { m_gravity = g; }
	[[nodiscard]] constexpr const sgc::Vec2f& gravity() const noexcept { return m_gravity; }

	BodyId addBody(RigidBody2D body) noexcept
	{
		const BodyId id = m_nextId++;
		if (body.isStatic) body.inverseMass = 0.0f;
		else if (body.mass > 0) body.inverseMass = 1.0f / body.mass;
		m_bodies[id] = std::move(body);
		return id;
	}

	void removeBody(BodyId id) noexcept { m_bodies.erase(id); }
	[[nodiscard]] RigidBody2D* getBody(BodyId id) noexcept
	{
		auto it = m_bodies.find(id); return it != m_bodies.end() ? &it->second : nullptr;
	}
	[[nodiscard]] const RigidBody2D* getBody(BodyId id) const noexcept
	{
		auto it = m_bodies.find(id); return it != m_bodies.end() ? &it->second : nullptr;
	}

	void setCollider(BodyId id, Collider2D c) noexcept
	{
		if (auto* b = getBody(id)) b->collider = std::move(c);
	}

	[[nodiscard]] std::size_t bodyCount() const noexcept { return m_bodies.size(); }
	void setCellSize(float s) noexcept { m_grid.setCellSize(s); }
	void setSolverIterations(int n) noexcept { m_iters = n; }

	void onCollisionEnter(CollisionCallback2D cb) noexcept { m_onEnter.push_back(std::move(cb)); }
	void onCollisionExit(CollisionCallback2D cb) noexcept { m_onExit.push_back(std::move(cb)); }

	/// @brief 物理ステップ: 重力→積分→ブロードフェーズ→ナローフェーズ→接触解決→コールバック
	void update(float dt) noexcept
	{
		// 積分
		for (auto& [id, b] : m_bodies)
		{
			if (b.isStatic || b.isKinematic) continue;
			const sgc::Vec2f a = m_gravity + b.acceleration + b.force * b.inverseMass;
			b.velocity += a * dt;
			b.position += b.velocity * dt;
			b.force = {};
		}

		// ブロードフェーズ
		m_grid.clear();
		for (const auto& [id, b] : m_bodies) m_grid.insert(id, worldAABB(b));
		const auto pairs = m_grid.queryAllPairs();

		// ナローフェーズ + 解決
		std::unordered_set<uint64_t> curPairs;
		std::vector<Contact2D> contacts;

		for (const auto& [idA, idB] : pairs)
		{
			auto* bA = getBody(idA); auto* bB = getBody(idB);
			if (!bA || !bB) continue;
			auto ct = narrow(*bA, *bB);
			if (!ct) continue;
			ct->bodyA = idA; ct->bodyB = idB;
			contacts.push_back(*ct);
			for (int i = 0; i < m_iters; ++i)
			{
				auto re = narrow(*bA, *bB);
				if (!re) break;
				re->bodyA = idA; re->bodyB = idB;
				ImpulseSolver::resolve(*re, *bA, *bB);
			}
			curPairs.insert((uint64_t(idA) << 32) | idB);
		}

		// コールバック
		for (const auto& c : contacts)
		{
			const uint64_t k = (uint64_t(c.bodyA) << 32) | c.bodyB;
			if (m_prev.find(k) == m_prev.end())
				for (const auto& cb : m_onEnter) cb(c);
		}
		for (uint64_t k : m_prev)
		{
			if (curPairs.find(k) == curPairs.end())
			{
				Contact2D ex; ex.bodyA = BodyId(k >> 32); ex.bodyB = BodyId(k & 0xFFFFFFFF);
				for (const auto& cb : m_onExit) cb(ex);
			}
		}
		m_prev = std::move(curPairs);
		m_lastContacts = contacts.size();
	}

	[[nodiscard]] std::size_t lastContactCount() const noexcept { return m_lastContacts; }

	[[nodiscard]] std::optional<RaycastHit2D> raycast(
		const sgc::Vec2f& origin, const sgc::Vec2f& dir, float maxDist = 1e6f) const noexcept
	{
		const sgc::Vec2f d = dir.normalized();
		std::optional<RaycastHit2D> best;
		for (const auto& [id, b] : m_bodies)
		{
			auto h = rayBody(origin, d, maxDist, id, b);
			if (h && (!best || h->distance < best->distance)) best = h;
		}
		return best;
	}

	[[nodiscard]] std::vector<BodyId> overlapCircle(const sgc::Vec2f& c, float r) const noexcept
	{
		std::vector<BodyId> result;
		const WorldAABB2D q{{c.x - r, c.y - r}, {c.x + r, c.y + r}};
		for (const auto& [id, b] : m_bodies)
		{
			if (!q.overlaps(worldAABB(b))) continue;
			bool hit = std::visit([&](const auto& col) -> bool {
				using T = std::decay_t<decltype(col)>;
				if constexpr (std::is_same_v<T, CircleCollider2D>)
				{
					const float rs = r + col.radius;
					return (b.position + col.offset - c).lengthSquared() < rs * rs;
				}
				else if constexpr (std::is_same_v<T, AABBCollider2D>)
					return CollisionDetection2D::circleVsAabb(c, r, b.position + col.min, b.position + col.max).has_value();
				else return true; // OBB: AABBレベルの近似で既に通過済み
			}, b.collider);
			if (hit) result.push_back(id);
		}
		return result;
	}

	[[nodiscard]] auto& bodies() noexcept { return m_bodies; }
	[[nodiscard]] const auto& bodies() const noexcept { return m_bodies; }

private:
	[[nodiscard]] static WorldAABB2D worldAABB(const RigidBody2D& b) noexcept
	{
		return std::visit([&](const auto& c) -> WorldAABB2D {
			using T = std::decay_t<decltype(c)>;
			if constexpr (std::is_same_v<T, CircleCollider2D>)
			{
				const sgc::Vec2f w = b.position + c.offset;
				return {{w.x - c.radius, w.y - c.radius}, {w.x + c.radius, w.y + c.radius}};
			}
			else if constexpr (std::is_same_v<T, AABBCollider2D>)
				return {b.position + c.min, b.position + c.max};
			else
			{
				sgc::Vec2f v[4]; c.worldVertices(b.position + c.offset, b.angle, v);
				sgc::Vec2f lo = v[0], hi = v[0];
				for (int i = 1; i < 4; ++i)
				{
					if (v[i].x < lo.x) lo.x = v[i].x; if (v[i].y < lo.y) lo.y = v[i].y;
					if (v[i].x > hi.x) hi.x = v[i].x; if (v[i].y > hi.y) hi.y = v[i].y;
				}
				return {lo, hi};
			}
		}, b.collider);
	}

	[[nodiscard]] static std::optional<Contact2D> narrow(const RigidBody2D& a, const RigidBody2D& b) noexcept
	{
		return std::visit([&](const auto& cA, const auto& cB) -> std::optional<Contact2D> {
			using A = std::decay_t<decltype(cA)>; using B = std::decay_t<decltype(cB)>;
			if constexpr (std::is_same_v<A, CircleCollider2D> && std::is_same_v<B, CircleCollider2D>)
				return CollisionDetection2D::circleVsCircle(a.position + cA.offset, cA.radius, b.position + cB.offset, cB.radius);
			else if constexpr (std::is_same_v<A, AABBCollider2D> && std::is_same_v<B, AABBCollider2D>)
				return CollisionDetection2D::aabbVsAabb(a.position + cA.min, a.position + cA.max, b.position + cB.min, b.position + cB.max);
			else if constexpr (std::is_same_v<A, CircleCollider2D> && std::is_same_v<B, AABBCollider2D>)
				return CollisionDetection2D::circleVsAabb(a.position + cA.offset, cA.radius, b.position + cB.min, b.position + cB.max);
			else if constexpr (std::is_same_v<A, AABBCollider2D> && std::is_same_v<B, CircleCollider2D>)
			{
				auto r = CollisionDetection2D::circleVsAabb(b.position + cB.offset, cB.radius, a.position + cA.min, a.position + cA.max);
				if (r) r->normal = -r->normal;
				return r;
			}
			else if constexpr (std::is_same_v<A, OBBCollider2D> && std::is_same_v<B, OBBCollider2D>)
				return CollisionDetection2D::obbVsObb(a.position + cA.offset, cA, a.angle, b.position + cB.offset, cB, b.angle);
			else
			{
				auto oA = toOBB(cA); auto oB = toOBB(cB);
				return CollisionDetection2D::obbVsObb(a.position + oA.offset, oA, a.angle, b.position + oB.offset, oB, b.angle);
			}
		}, a.collider, b.collider);
	}

	static OBBCollider2D toOBB(const CircleCollider2D& c) noexcept { return {c.offset, {c.radius, c.radius}, 0}; }
	static OBBCollider2D toOBB(const AABBCollider2D& c) noexcept { return {c.center(), c.halfExtents(), 0}; }
	static OBBCollider2D toOBB(const OBBCollider2D& c) noexcept { return c; }

	[[nodiscard]] static std::optional<RaycastHit2D> rayBody(
		const sgc::Vec2f& o, const sgc::Vec2f& d, float md, BodyId id, const RigidBody2D& b) noexcept
	{
		return std::visit([&](const auto& c) -> std::optional<RaycastHit2D> {
			using T = std::decay_t<decltype(c)>;
			if constexpr (std::is_same_v<T, CircleCollider2D>)
				return rayCircle(o, d, md, id, b.position + c.offset, c.radius);
			else if constexpr (std::is_same_v<T, AABBCollider2D>)
				return rayAABB(o, d, md, id, b.position + c.min, b.position + c.max);
			else
			{
				sgc::Vec2f v[4]; c.worldVertices(b.position + c.offset, b.angle, v);
				sgc::Vec2f lo = v[0], hi = v[0];
				for (int i = 1; i < 4; ++i)
				{
					if (v[i].x < lo.x) lo.x = v[i].x; if (v[i].y < lo.y) lo.y = v[i].y;
					if (v[i].x > hi.x) hi.x = v[i].x; if (v[i].y > hi.y) hi.y = v[i].y;
				}
				return rayAABB(o, d, md, id, lo, hi);
			}
		}, b.collider);
	}

	[[nodiscard]] static std::optional<RaycastHit2D> rayCircle(
		const sgc::Vec2f& o, const sgc::Vec2f& d, float md, BodyId id,
		const sgc::Vec2f& ctr, float r) noexcept
	{
		const sgc::Vec2f oc = o - ctr;
		const float a = d.dot(d), b = 2 * oc.dot(d), c = oc.dot(oc) - r * r;
		const float disc = b * b - 4 * a * c;
		if (disc < 0) return std::nullopt;
		const float sq = std::sqrt(disc);
		float t = (-b - sq) / (2 * a);
		if (t < 0) t = (-b + sq) / (2 * a);
		if (t < 0 || t > md) return std::nullopt;
		const sgc::Vec2f hp = o + d * t;
		return RaycastHit2D{id, hp, (hp - ctr).normalized(), t};
	}

	[[nodiscard]] static std::optional<RaycastHit2D> rayAABB(
		const sgc::Vec2f& o, const sgc::Vec2f& d, float md, BodyId id,
		const sgc::Vec2f& mn, const sgc::Vec2f& mx) noexcept
	{
		float tMn = 0, tMx = md; sgc::Vec2f n{};
		auto slab = [&](float od, float dd, float lo, float hi, float nx, float ny) -> bool {
			const float inv = std::abs(dd) > 1e-8f ? 1.0f / dd : 1e8f;
			float t0 = (lo - od) * inv, t1 = (hi - od) * inv;
			sgc::Vec2f n0{nx, ny}, n1{-nx, -ny};
			if (inv < 0) { std::swap(t0, t1); std::swap(n0, n1); }
			if (t0 > tMn) { tMn = t0; n = n0; }
			if (t1 < tMx) tMx = t1;
			return tMn <= tMx;
		};
		if (!slab(o.x, d.x, mn.x, mx.x, -1, 0)) return std::nullopt;
		if (!slab(o.y, d.y, mn.y, mx.y, 0, -1)) return std::nullopt;
		if (tMn < 0) return std::nullopt;
		return RaycastHit2D{id, o + d * tMn, n, tMn};
	}

	sgc::Vec2f m_gravity{0, -9.81f};
	std::unordered_map<BodyId, RigidBody2D> m_bodies;
	SpatialHashGrid m_grid{2.0f};
	int m_iters{4};
	BodyId m_nextId{1};
	std::size_t m_lastContacts{0};
	std::vector<CollisionCallback2D> m_onEnter, m_onExit;
	std::unordered_set<uint64_t> m_prev;
};

} // namespace mitiru::physics2d
