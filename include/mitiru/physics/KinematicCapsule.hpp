#pragma once

/// @file KinematicCapsule.hpp
/// @brief 物理ワールドに依存しないキャラクターコントローラ部品
///
/// 静的な AABB 群に対してカプセルを collide-and-slide で動かし、接地・壁・
/// 天井を法線の傾きで分類する。階段段差は stepHeight 以下なら乗り越える。
/// 形状取得は StaticBoxWorld 契約（boxCount/box）のテンプレート引数で受ける。
///
/// Jolt の CharacterVirtual を使わずに残しているのは、状態 (位置・接地・法線) が固定長の値だけで
/// GameMemory に置けるため。game DLL の中で動かしても巻き戻しとリプレイ (軸 2 / 軸 4) から外れない。
/// host の Jolt ワールドは DLL 境界を越えて持てない。

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "sgc/math/Vec3.hpp"
#include "mitiru/debug/WarnOnce.hpp"
#include "mitiru/physics/Collider3D.hpp"

namespace mitiru::physics3d
{

/// @brief move() へ渡す静的形状集合の最小契約
template <class T>
concept StaticBoxWorld = requires(const T& world, std::size_t index)
{
	{ world.boxCount() } -> std::convertible_to<std::size_t>;
	{ world.box(index) } -> std::convertible_to<const AABBCollider3D&>;
};

/// @brief StaticBoxWorld を満たす最小実装（ポインタ+件数のビュー）
struct StaticBoxArray
{
	const AABBCollider3D* boxes{nullptr};
	std::size_t count{0};

	[[nodiscard]] std::size_t boxCount() const noexcept { return count; }
	[[nodiscard]] const AABBCollider3D& box(std::size_t index) const noexcept { return boxes[index]; }
};

/// @brief 接触面の分類（法線と maxSlopeDeg から決まる）
enum class SurfaceType : std::uint8_t
{
	None,
	Floor,
	Wall,
	Ceiling,
};

/// @brief KinematicCapsule の挙動パラメータ
struct KinematicCapsuleConfig
{
	float radius{0.5f};          ///< カプセル半径
	float height{1.8f};          ///< カプセル全長（両端の半球を含む）
	float maxSlopeDeg{45.0f};    ///< これ以下の傾きは床、超えると壁扱い
	float stepHeight{0.3f};      ///< 乗り越えられる段差の高さ
	int maxSlideIterations{4};   ///< collide-and-slideの最大反復回数
	float skinWidth{0.01f};      ///< めり込み解消時に余分に押し出す量（すり抜け防止）
	sgc::Vec3f up{0.0f, 1.0f, 0.0f}; ///< 「上」の向き (重力の逆)。床/壁の分類・段差の持ち上げ・接地レイがこれに従う。単位ベクトルでなければ構築時に正規化する
};

/// @brief move() の結果（最後に処理された接触面。接触が無ければ surface == None）
struct KinematicHit
{
	sgc::Vec3f normal{};
	float depth{0.0f};
	SurfaceType surface{SurfaceType::None};
};

/// @brief 法線と最大斜度から接触面を分類する。`up` は単位ベクトル (重力の逆) で、球状ステージや
/// 壁走りのように「上」が +Y でない場面でも床/天井/壁を同じ規則で分ける。
[[nodiscard]] inline SurfaceType classifySurface(const sgc::Vec3f& normal, float maxSlopeDeg,
                                                 const sgc::Vec3f& up) noexcept
{
	constexpr float kPi = 3.14159265358979323846f;
	const float cosSlope = std::cos(maxSlopeDeg * (kPi / 180.0f));
	const float alongUp  = normal.dot(up);

	if (alongUp >= cosSlope) return SurfaceType::Floor;
	if (alongUp <= -cosSlope) return SurfaceType::Ceiling;
	return SurfaceType::Wall;
}

/// @brief up = +Y の版。
[[nodiscard]] inline SurfaceType classifySurface(const sgc::Vec3f& normal, float maxSlopeDeg) noexcept
{
	return classifySurface(normal, maxSlopeDeg, sgc::Vec3f{0.0f, 1.0f, 0.0f});
}

namespace detail
{

[[nodiscard]] inline sgc::Vec3f closestPointOnSegment(
	const sgc::Vec3f& point, const sgc::Vec3f& a, const sgc::Vec3f& b) noexcept
{
	const sgc::Vec3f ab = b - a;
	const float lenSq = ab.lengthSquared();
	if (lenSq < 1e-10f) return a;
	const float t = std::clamp((point - a).dot(ab) / lenSq, 0.0f, 1.0f);
	return a + ab * t;
}

/// @brief 中心が箱の中に入ったときは、押し出しが最短の軸の面へ出す
[[nodiscard]] inline sgc::Vec3f insideExitNormal(const sgc::Vec3f& center, const AABBCollider3D& aabb) noexcept
{
	const sgc::Vec3f half = aabb.halfExtents();
	const sgc::Vec3f d = center - aabb.center();
	const float ox = half.x - std::abs(d.x), oy = half.y - std::abs(d.y), oz = half.z - std::abs(d.z);
	if (ox <= oy && ox <= oz) return sgc::Vec3f{d.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f};
	if (oy <= oz) return sgc::Vec3f{0.0f, d.y >= 0.0f ? 1.0f : -1.0f, 0.0f};
	return sgc::Vec3f{0.0f, 0.0f, d.z >= 0.0f ? 1.0f : -1.0f};
}

[[nodiscard]] inline ContactInfo3D sphereVsAabb(const sgc::Vec3f& center, float radius, const AABBCollider3D& aabb) noexcept
{
	const sgc::Vec3f closest = center.clamped(aabb.min, aabb.max);
	const sgc::Vec3f diff = center - closest;
	const float distSq = diff.lengthSquared();
	if (distSq >= radius * radius) return {};

	const float dist = std::sqrt(distSq);
	ContactInfo3D info;
	info.hasContact = true;
	info.normal = (dist > 1e-6f) ? diff / dist : insideExitNormal(center, aabb);
	info.depth = radius - dist;
	info.point = closest;
	return info;
}

/// @brief カプセルと AABB の接触情報を求める（反復で軸-AABB 間の最近接点に収束させる）
[[nodiscard]] inline ContactInfo3D testCapsuleAABBLocal(
	const CapsuleCollider& capsule, const AABBCollider3D& aabb) noexcept
{
	sgc::Vec3f pointOnBox = capsule.pointA.clamped(aabb.min, aabb.max);
	sgc::Vec3f pointOnSegment = capsule.pointA;

	for (int iter = 0; iter < 4; ++iter)
	{
		pointOnSegment = closestPointOnSegment(pointOnBox, capsule.pointA, capsule.pointB);
		pointOnBox = pointOnSegment.clamped(aabb.min, aabb.max);
	}
	return sphereVsAabb(pointOnSegment, capsule.radius, aabb);
}

/// @brief 足元の着地点探し用のレイと箱の交差 (slab 法)。始点が箱の中なら当たらない扱い
struct RayBoxHit
{
	float distance{0.0f};
	sgc::Vec3f normal{};
};

[[nodiscard]] inline std::optional<RayBoxHit> rayVsAabb(
	const sgc::Vec3f& origin, const sgc::Vec3f& dir, const AABBCollider3D& aabb, float maxDist) noexcept
{
	const float o[3] = {origin.x, origin.y, origin.z};
	const float d[3] = {dir.x, dir.y, dir.z};
	const float lo[3] = {aabb.min.x, aabb.min.y, aabb.min.z};
	const float hi[3] = {aabb.max.x, aabb.max.y, aabb.max.z};

	float tMin = 0.0f, tMax = maxDist;
	int hitAxis = -1;
	float hitSign = 0.0f;
	for (int axis = 0; axis < 3; ++axis)
	{
		const float inv = (std::abs(d[axis]) > 1e-8f) ? 1.0f / d[axis] : 1e8f;
		float t0 = (lo[axis] - o[axis]) * inv, t1 = (hi[axis] - o[axis]) * inv;
		float entrySign = -1.0f;
		if (inv < 0.0f) { std::swap(t0, t1); entrySign = 1.0f; }
		if (t0 >= tMin) { tMin = t0; hitAxis = axis; hitSign = entrySign; }  // 面の上に立つ始点 (t=0) も当たりにする
		tMax = std::min(tMax, t1);
		if (tMin > tMax) return std::nullopt;
	}
	if (hitAxis < 0) return std::nullopt;

	RayBoxHit hit;
	hit.distance = tMin;
	hit.normal = sgc::Vec3f{hitAxis == 0 ? hitSign : 0.0f, hitAxis == 1 ? hitSign : 0.0f, hitAxis == 2 ? hitSign : 0.0f};
	return hit;
}

} // namespace detail

/// @brief 静的 AABB 群に対して動く、物理システムに依存しないカプセルコントローラ
class KinematicCapsule
{
public:
	KinematicCapsule() = default;

	/// @param config 挙動パラメータ
	/// @param position 初期位置（カプセル中心）
	explicit KinematicCapsule(const KinematicCapsuleConfig& config, const sgc::Vec3f& position = {}) noexcept
		: m_config(config), m_position(position)
	{
		// 斜度の比較は dot(normal, up) なので up は単位長でないと閾値がずれる。0 ベクトルは +Y に戻す
		const float len = m_config.up.length();
		if (len > 1e-6f) { m_config.up = m_config.up * (1.0f / len); }
		else
		{
			mitiru::debug::warnOnce("physics.capsule.up.zero", "KinematicCapsuleConfig::up が 0 ベクトルなので +Y にする");
			m_config.up = sgc::Vec3f{0.0f, 1.0f, 0.0f};
		}
		m_groundNormal = m_config.up;
	}

	/// @brief カプセル中心位置を返す
	[[nodiscard]] const sgc::Vec3f& position() const noexcept { return m_position; }

	/// @brief カプセル中心位置を直接設定する（テレポート用。衝突解決は行わない）
	void setPosition(const sgc::Vec3f& position) noexcept { m_position = position; }

	/// @brief 直近の move() で床に接地したか
	[[nodiscard]] bool isGrounded() const noexcept { return m_grounded; }

	/// @brief 直近の接地法線を返す（isGrounded()が false なら未定義の値を返しうる）
	[[nodiscard]] const sgc::Vec3f& groundNormal() const noexcept { return m_groundNormal; }

	/// @brief 設定パラメータを返す
	[[nodiscard]] const KinematicCapsuleConfig& config() const noexcept { return m_config; }

	/// @brief desiredDelta だけ動かす（collide-and-slide、最大 config.maxSlideIterations 反復）
	/// @param desiredDelta 希望移動量（1 フレーム分のワールド空間ベクトル）
	/// @param world 静的形状集合（StaticBoxWorld 契約を満たす型）
	/// @return 最後に処理された接触面（接触が一度も無ければ surface == None）
	template <StaticBoxWorld World>
	KinematicHit move(const sgc::Vec3f& desiredDelta, const World& world) noexcept
	{
		m_grounded = false;

		sgc::Vec3f remaining = desiredDelta;
		KinematicHit hit{};

		// 1 反復目で移動量を全部適用し、以後は押し出しで新たに生じた食い込みを解くためだけに回す。
		// 接線成分を次の反復へ持ち越すと、全量適用で既に進んだ分と合わせて壁沿いに二重に進む。
		for (int iter = 0; iter < m_config.maxSlideIterations; ++iter)
		{
			const bool moving = remaining.lengthSquared() >= 1e-12f;

			const sgc::Vec3f basePosition = m_position;
			if (moving) m_position += remaining;
			const ContactInfo3D contact = deepestContact(world);
			if (!contact.hasContact) break;

			const SurfaceType surface = classifySurface(contact.normal, m_config.maxSlopeDeg, m_config.up);

			if (moving)
			{
				// 段差の乗り越えは「持ち上げ→水平スイープ→降ろす」を tryStepUp 内で 1 回だけ行う。
				// ここで水平移動を確定させたまま tryStepUp に渡すと、内部でも同じ量を
				// 加算するため水平移動が二重適用される。一旦 basePosition へ戻してから任せる
				m_position = basePosition;
				if (surface == SurfaceType::Wall && m_config.stepHeight > 0.0f && tryStepUp(remaining, world))
				{
					hit = {contact.normal, contact.depth, surface};
					break;
				}

				// 段差処理が不成立だったので、押し出し判定用に移動を戻す
				m_position += remaining;
			}
			m_position += contact.normal * (contact.depth + m_config.skinWidth);

			if (surface == SurfaceType::Floor)
			{
				m_grounded = true;
				m_groundNormal = contact.normal;
			}
			hit = {contact.normal, contact.depth, surface};

			// 面が移動方向を妨げていなければ（例: 平らな床の上を水平に移動）反復は不要
			if (moving && remaining.dot(contact.normal) >= -1e-6f) break;
			remaining = sgc::Vec3f{};
		}

		return hit;
	}

private:
	/// @brief 現在位置のカプセル形状を作る（m_position をカプセル中心として扱う）
	[[nodiscard]] CapsuleCollider makeCapsule() const noexcept
	{
		const float halfSegment = std::max(0.0f, m_config.height * 0.5f - m_config.radius);
		return CapsuleCollider{
			m_position - m_config.up * halfSegment,
			m_position + m_config.up * halfSegment,
			m_config.radius,
		};
	}

	/// @brief 現在位置での最深接触を全ボックスから探す
	template <StaticBoxWorld World>
	[[nodiscard]] ContactInfo3D deepestContact(const World& world) const noexcept
	{
		const CapsuleCollider capsule = makeCapsule();
		ContactInfo3D best{};

		const std::size_t n = world.boxCount();
		for (std::size_t i = 0; i < n; ++i)
		{
			const ContactInfo3D contact = detail::testCapsuleAABBLocal(capsule, world.box(i));
			if (contact.hasContact && contact.depth > best.depth)
			{
				best = contact;
			}
		}

		return best;
	}

	/// @brief 足元中心から真下へ最大 maxDist までレイキャストし、最も近い AABB を探す
	template <StaticBoxWorld World>
	[[nodiscard]] std::optional<detail::RayBoxHit> findGroundBelow(float maxDist, const World& world) const noexcept
	{
		const float halfSegment = std::max(0.0f, m_config.height * 0.5f - m_config.radius);
		const sgc::Vec3f feetCenter = m_position - m_config.up * (halfSegment + m_config.radius);
		const sgc::Vec3f down = m_config.up * -1.0f;

		std::optional<detail::RayBoxHit> closest;
		const std::size_t n = world.boxCount();
		for (std::size_t i = 0; i < n; ++i)
		{
			const auto rayHit = detail::rayVsAabb(feetCenter, down, world.box(i), maxDist);
			if (rayHit.has_value() && (!closest.has_value() || rayHit->distance < closest->distance))
			{
				closest = rayHit;
			}
		}
		return closest;
	}

	/// @brief 壁接触を stepHeight 分の持ち上げ＋水平移動で回避できるか試す
	/// @param horizontalDelta この反復で適用しようとした移動量（水平成分だけ使う）
	/// @return 段差を乗り越えられたら true（m_position は乗り越えた後の位置に更新済み）
	template <StaticBoxWorld World>
	bool tryStepUp(const sgc::Vec3f& horizontalDelta, const World& world) noexcept
	{
		const sgc::Vec3f savedPosition = m_position;

		m_position += m_config.up * m_config.stepHeight;
		if (deepestContact(world).hasContact)
		{
			m_position = savedPosition;
			return false;
		}

		m_position += horizontalDelta - m_config.up * horizontalDelta.dot(m_config.up);  // up に直交する成分だけ
		if (deepestContact(world).hasContact)
		{
			m_position = savedPosition;
			return false;
		}

		// 段の上に着地させる。足元中心から真下へレイキャストし、段の上面に
		// 乗っているかを XZ の重なりだけで判定する（カプセル半径を絡めた重なり
		// 判定は境界付近で壁/床の分類が数値誤差で反転しうるため使わない）
		const auto landing = findGroundBelow(m_config.stepHeight, world);
		if (!landing.has_value())
		{
			m_position = savedPosition;
			return false;
		}

		m_position += m_config.up * (m_config.skinWidth - landing->distance);
		m_grounded = true;
		m_groundNormal = landing->normal;

		return true;
	}

	KinematicCapsuleConfig m_config{};
	sgc::Vec3f m_position{};
	sgc::Vec3f m_groundNormal{0.0f, 1.0f, 0.0f};
	bool m_grounded{false};
};

} // namespace mitiru::physics3d
