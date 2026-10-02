#pragma once

/// @file CollisionWorld.hpp
/// @brief 同じフレームの中で答えが返る当たり判定の問い合わせ (レイ、球とカプセルの掃引、重なり、最近点)。
/// @details CollisionWorld は 1 フレームごとに作り直す軽い窓口で、状態を持たない。
///          中身は CollisionLevel (作ったら変えない地形) と MovingBody の列 (GameMemory に置く動く足場)。
///          問い合わせはすべて const で、ヒープを使わない。
///
///          決まり:
///          - 判定は両面。raycast の法線は始点の側、掃引と重なりの法線は形の中心の側を向く。
///          - 当たりの body は地形なら kLevelBody、動く足場なら MovingBody::id。triangle は CollisionLevel の id
///            (箱の足場は 0-11)。layer は三角形の layer で、mask の (1 << layer) と照らす。
///          - 同じ距離の当たりが複数あるときは、地形、足場の並び順、三角形の id の小さい順で 1 つに決める。
///            BVH の走査順に依らないので、総当たり (bruteForce) と同じ答えになる。

#include <cstdint>
#include <span>
#include <type_traits>

#include <mitiru/action/CollisionLevel.hpp>
#include <mitiru/action/TriangleSweep.hpp>

namespace mitiru::action
{

inline constexpr std::uint32_t kLevelBody = 0;

/// @brief これ以下の開始時の重なりは「接している」とみなし、掃引を止めない (m)
inline constexpr float kContactSlop = 1e-5f;

enum class MovingShape : std::uint8_t
{
	Box,   ///< halfExtents の箱
	Mesh,  ///< CollisionLevel::movableMesh(mesh)
};

/// @brief GameMemory に置く動く足場。姿勢は game が毎 tick 書き、前の姿勢は乗っているものを運ぶのに使う
struct MovingBody
{
	Pose          pose{};
	Pose          previousPose{};
	Vec3          halfExtents{0.5f, 0.5f, 0.5f};
	std::uint32_t id    = 1;  ///< 0 は地形 (kLevelBody) と区別できないので使わない
	std::uint32_t layer = 0;  ///< Box の layer。Mesh は三角形ごとの layer を使う
	std::uint16_t mesh  = 0;
	MovingShape   shape   = MovingShape::Box;
	std::uint8_t  enabled = 1;
};

struct Capsule
{
	Vec3  a{};
	Vec3  b{};
	float radius = 0.5f;
};

struct RayHit
{
	Vec3          point{};
	Vec3          normal{};
	float         distance = 0.0f;
	std::uint32_t body     = kLevelBody;
	std::uint32_t triangle = 0;
	std::uint32_t layer    = 0;
	bool          hit      = false;
	std::uint8_t  pad[3]{};
};

struct SweepHit
{
	Vec3          point{};
	Vec3          normal{};
	Vec3          surfaceNormal{};  ///< 当たった三角形の面法線 (normal と同じ側)。坂か壁かはこちらで決める
	float         fraction    = 1.0f;  ///< delta のどこで当たったか (0..1)。外れたら 1
	float         penetration = 0.0f;  ///< startPenetrating のときの重なりの深さ
	std::uint32_t body     = kLevelBody;
	std::uint32_t triangle = 0;
	std::uint32_t layer    = 0;
	bool          hit              = false;
	bool          startPenetrating = false;
	std::uint8_t  pad[2]{};
};

struct Contact
{
	Vec3          point{};          ///< 地形の側の最近点
	Vec3          normal{};         ///< 形を押し出す向き
	Vec3          surfaceNormal{};  ///< 三角形の面法線 (normal と同じ側)。角に触れても床の三角形なら上を向く
	float         depth    = 0.0f;
	std::uint32_t body     = kLevelBody;
	std::uint32_t triangle = 0;
	std::uint32_t layer    = 0;
};

static_assert(std::is_trivially_copyable_v<MovingBody>);
static_assert(std::is_trivially_copyable_v<RayHit>);
static_assert(std::is_trivially_copyable_v<SweepHit>);
static_assert(std::is_trivially_copyable_v<Contact>);

namespace detail
{

/// @brief 問い合わせの座標系 (地形は恒等、足場はその姿勢)
struct Frame
{
	Vec3 pos{};
	Quat rot{};
	Quat inv{};
	bool identity = true;

	[[nodiscard]] Vec3 toLocal(const Vec3& p) const noexcept { return identity ? p : inv.rotate(p - pos); }
	[[nodiscard]] Vec3 toLocalDir(const Vec3& d) const noexcept { return identity ? d : inv.rotate(d); }
	[[nodiscard]] Vec3 toWorld(const Vec3& p) const noexcept { return identity ? p : pos + rot.rotate(p); }
	[[nodiscard]] Vec3 toWorldDir(const Vec3& d) const noexcept { return identity ? d : rot.rotate(d); }
};

/// @brief 三角形の出どころ 1 つ (地形、足場の BVH、足場の箱)
struct Source
{
	const TriangleBvh* bvh = nullptr;
	Vec3               half{};
	std::uint32_t      boxLayer = 0;
	std::uint32_t      body     = kLevelBody;
	std::uint32_t      order    = 0;
	Frame              frame{};
};

/// @brief 一番よい当たり。同じ値なら (出どころの順, 三角形の id) の小さい方
struct Best
{
	float         value;
	bool          found = false;
	std::uint32_t order = 0;
	std::uint32_t id    = 0;

	explicit Best(float limit) noexcept : value(limit) {}
	bool accept(float v, std::uint32_t o, std::uint32_t triId) noexcept
	{
		if (!(v <= value)) { return false; }
		if (found && v == value && (o > order || (o == order && triId >= id))) { return false; }
		value = v;
		found = true;
		order = o;
		id    = triId;
		return true;
	}
};

[[nodiscard]] inline bool layerMatch(std::uint32_t layer, std::uint32_t mask) noexcept
{
	return layer < 32u && ((mask >> layer) & 1u) != 0u;
}

/// @brief 箱の足場の 12 枚を作って f に渡す
template <class F>
void forEachBoxTriangle(const Source& s, F&& f)
{
	const Vec3 lo = -s.half;
	for (int i = 0; i < 36; i += 3)
	{
		const BvhTriangle t{boxCorner(lo, s.half, kBoxTriangleCorners[i]), boxCorner(lo, s.half, kBoxTriangleCorners[i + 1]),
		                    boxCorner(lo, s.half, kBoxTriangleCorners[i + 2]), static_cast<std::uint32_t>(i / 3),
		                    s.boxLayer};
		f(t);
	}
}

} // namespace detail

class CollisionWorld
{
public:
	CollisionWorld() = default;
	explicit CollisionWorld(const CollisionLevel* level, std::span<const MovingBody> bodies = {}) noexcept
		: m_level(level), m_bodies(bodies)
	{
	}

	/// @brief true にすると BVH を使わず全三角形を総当たりする。BVH と同じ答えになることの確かめに使う
	bool bruteForce = false;

	[[nodiscard]] const CollisionLevel* level() const noexcept { return m_level; }
	[[nodiscard]] std::span<const MovingBody> bodies() const noexcept { return m_bodies; }

	/// @brief 一番近い当たり。dir は内部で正規化する (長さ 0 なら外れ)
	/// @param cullBackface true なら表 (反時計回り) から入るレイだけ当たる (host の Jolt と同じ)
	[[nodiscard]] RayHit raycast(const Vec3& origin, const Vec3& dir, float maxDist, std::uint32_t mask = kAllLayers,
	                             bool cullBackface = false) const;

	/// @brief 球を center から center + delta へ動かしたときの最初の当たり。
	///        開始時に kContactSlop より深く重なっていれば startPenetrating (fraction 0、normal は押し出す向き)
	[[nodiscard]] SweepHit sphereCast(const Vec3& center, float radius, const Vec3& delta,
	                                  std::uint32_t mask = kAllLayers) const;
	[[nodiscard]] SweepHit capsuleCast(const Capsule& capsule, const Vec3& delta, std::uint32_t mask = kAllLayers) const;

	/// @brief 重なっている三角形を深い順に最大 capacity 件書き、書いた件数を返す。入り切らなければ深いものを残す
	int overlapSphere(const Vec3& center, float radius, Contact* out, int capacity,
	                  std::uint32_t mask = kAllLayers) const;
	int overlapCapsule(const Capsule& capsule, Contact* out, int capacity, std::uint32_t mask = kAllLayers) const;

	/// @brief maxDist 以内で一番近い地形の点。normal は地形から point へ向く
	[[nodiscard]] RayHit closestPoint(const Vec3& point, float maxDist, std::uint32_t mask = kAllLayers) const;

	/// @brief 足場 bodyId が直前の tick に point をどれだけ動かしたか。地形と知らない id は 0
	[[nodiscard]] Vec3 carryDelta(std::uint32_t bodyId, const Vec3& point) const noexcept
	{
		for (const MovingBody& b : m_bodies)
		{
			if (b.id == bodyId && b.enabled != 0 && bodyId != kLevelBody)
			{
				const Pose cur{b.pose.position, b.pose.rotation.normalized()};
				const Pose prev{b.previousPose.position, b.previousPose.rotation.normalized()};
				return cur.apply(prev.inverseApply(point)) - point;
			}
		}
		return Vec3{};
	}

private:
	template <class F>
	void forEachSource(F&& f) const;
	template <class F>
	void visitRay(const detail::Source& s, const Vec3& o, const Vec3& d, float tMax, const Vec3& inflate, F&& f) const;
	template <class F>
	void visitBox(const detail::Source& s, const Aabb& box, F&& f) const;
	template <class Sink>
	void forEachOverlap(const Vec3& p, const Vec3& q, float r, std::uint32_t mask, Sink&& sink) const;
	bool deepestOverlap(const Vec3& p, const Vec3& q, float r, std::uint32_t mask, Contact& out) const;
	[[nodiscard]] SweepHit finishSweep(const detail::Best& best, const geom::SweepContact& sc,
	                                   const detail::Source& src, const BvhTriangle& tri) const;

	const CollisionLevel*       m_level = nullptr;
	std::span<const MovingBody> m_bodies{};
};

} // namespace mitiru::action

#include <mitiru/action/detail/CollisionWorld_impl.hpp>
