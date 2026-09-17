#pragma once

/// @file KinematicCapsule.hpp
/// @brief PhysicsSystem3D に依存しないキャラクターコントローラ部品
///
/// 静的なAABB群に対してカプセルを collide-and-slide で動かし、接地・壁・
/// 天井を法線の傾きで分類する。階段段差は stepHeight 以下なら乗り越える。
/// 形状取得は StaticBoxWorld 契約（boxCount/box）のテンプレート引数で受け、
/// PhysicsSystem3D の overlap クエリを包んだ型に差し替えられるようにしてある。

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "sgc/math/Vec3.hpp"
#include "mitiru/debug/WarnOnce.hpp"
#include "mitiru/physics/Collider3D.hpp"
#include "mitiru/physics/CollisionDetection3D.hpp"

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

/// @brief 接触面の分類（法線とmaxSlopeDegから決まる）
enum class SurfaceType : std::uint8_t
{
	None,
	Floor,
	Wall,
	Ceiling,
};

/// @brief KinematicCapsuleの挙動パラメータ
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

/// @brief 詳細実装（Collider3D/CollisionDetection3Dの既存関数だけで組む）
namespace detail
{

/// @brief カプセルとAABBの接触情報を求める（反復で軸-AABB間の最近接点に収束させる）
[[nodiscard]] inline ContactInfo3D testCapsuleAABBLocal(
	const CapsuleCollider& capsule, const AABBCollider3D& aabb) noexcept
{
	sgc::Vec3f pointOnBox = closestPointOnAABB(capsule.pointA, aabb);
	sgc::Vec3f pointOnSegment = capsule.pointA;

	for (int iter = 0; iter < 4; ++iter)
	{
		pointOnSegment = closestPointOnSegment(pointOnBox, capsule.pointA, capsule.pointB);
		pointOnBox = closestPointOnAABB(pointOnSegment, aabb);
	}

	const SphereCollider capsuleAsSphere{pointOnSegment, capsule.radius};
	return testSphereAABB(capsuleAsSphere, aabb);
}

} // namespace detail

/// @brief 静的AABB群に対して動く、物理システムに依存しないカプセルコントローラ
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

	/// @brief 直近の接地法線を返す（isGrounded()がfalseなら未定義の値を返しうる）
	[[nodiscard]] const sgc::Vec3f& groundNormal() const noexcept { return m_groundNormal; }

	/// @brief 設定パラメータを返す
	[[nodiscard]] const KinematicCapsuleConfig& config() const noexcept { return m_config; }

	/// @brief desiredDeltaだけ動かす（collide-and-slide、最大 config.maxSlideIterations 反復）
	/// @param desiredDelta 希望移動量（1フレーム分のワールド空間ベクトル）
	/// @param world 静的形状集合（StaticBoxWorld契約を満たす型）
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
				// 段差の乗り越えは「持ち上げ→水平スイープ→降ろす」をtryStepUp内で1回だけ行う。
				// ここで水平移動を確定させたままtryStepUpに渡すと、内部でも同じ量を
				// 加算するため水平移動が二重適用される。一旦basePositionへ戻してから任せる
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
	/// @brief 現在位置のカプセル形状を作る（m_positionをカプセル中心として扱う）
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

	/// @brief 足元中心から真下へ最大maxDistまでレイキャストし、最も近いAABBを探す
	template <StaticBoxWorld World>
	[[nodiscard]] std::optional<RayHit3D> findGroundBelow(float maxDist, const World& world) const noexcept
	{
		const float halfSegment = std::max(0.0f, m_config.height * 0.5f - m_config.radius);
		const sgc::Vec3f feetCenter = m_position - m_config.up * (halfSegment + m_config.radius);
		const Ray3D downRay{feetCenter, m_config.up * -1.0f};

		std::optional<RayHit3D> closest;
		const std::size_t n = world.boxCount();
		for (std::size_t i = 0; i < n; ++i)
		{
			const auto rayHit = raycastAABB(downRay, world.box(i), maxDist);
			if (rayHit.has_value() && (!closest.has_value() || rayHit->distance < closest->distance))
			{
				closest = rayHit;
			}
		}
		return closest;
	}

	/// @brief 壁接触をstepHeight分の持ち上げ＋水平移動で回避できるか試す
	/// @param horizontalDelta この反復で適用しようとした移動量（水平成分だけ使う）
	/// @return 段差を乗り越えられたらtrue（m_positionは乗り越えた後の位置に更新済み）
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
		// 乗っているかをXZの重なりだけで判定する（カプセル半径を絡めた重なり
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
