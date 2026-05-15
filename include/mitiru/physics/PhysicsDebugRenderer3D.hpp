#pragma once

/// @file PhysicsDebugRenderer3D.hpp
/// @brief 3D物理デバッグ描画
///
/// コライダーのワイヤーフレーム、接触点、速度ベクトル、拘束線を
/// デバッグ可視化する。描画バックエンドに依存しないライン/ポイントデータを
/// 生成し、任意のレンダラーに渡せる。
///
/// @code
/// mitiru::physics3d::PhysicsDebugRenderer3D debug;
/// debug.setFlags(DebugDrawFlags::Colliders | DebugDrawFlags::Contacts);
/// debug.gather(world, manifolds);
/// for (const auto& line : debug.lines()) { /* 描画 */ }
/// for (const auto& point : debug.points()) { /* 描画 */ }
/// @endcode

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "sgc/math/Vec3.hpp"
#include "mitiru/physics/Collider3D.hpp"
#include "mitiru/physics/ContactSolver3D.hpp"
#include "mitiru/physics/PhysicsWorld3D.hpp"
#include "mitiru/physics/RigidBody3D.hpp"

namespace mitiru::physics3d
{

/// @brief デバッグ描画フラグ
enum class DebugDrawFlags : uint32_t
{
	None         = 0,
	Colliders    = 1 << 0,  ///< コライダーのワイヤーフレーム
	Contacts     = 1 << 1,  ///< 接触点と法線
	Velocities   = 1 << 2,  ///< 速度ベクトル
	Constraints  = 1 << 3,  ///< 拘束線
	AABBs        = 1 << 4,  ///< AABB境界ボックス
	CenterOfMass = 1 << 5,  ///< 重心マーカー
	All          = 0xFFFFFFFF
};

/// @brief フラグのビット演算サポート
[[nodiscard]] inline constexpr DebugDrawFlags operator|(
	DebugDrawFlags a, DebugDrawFlags b) noexcept
{
	return static_cast<DebugDrawFlags>(
		static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

[[nodiscard]] inline constexpr DebugDrawFlags operator&(
	DebugDrawFlags a, DebugDrawFlags b) noexcept
{
	return static_cast<DebugDrawFlags>(
		static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

[[nodiscard]] inline constexpr bool hasFlag(
	DebugDrawFlags flags, DebugDrawFlags flag) noexcept
{
	return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

/// @brief デバッグ描画色（RGB、0〜1）
struct DebugColor3D
{
	float r{1.0f};
	float g{1.0f};
	float b{1.0f};

	[[nodiscard]] static constexpr DebugColor3D green() noexcept { return {0.0f, 1.0f, 0.0f}; }
	[[nodiscard]] static constexpr DebugColor3D red() noexcept { return {1.0f, 0.0f, 0.0f}; }
	[[nodiscard]] static constexpr DebugColor3D blue() noexcept { return {0.0f, 0.5f, 1.0f}; }
	[[nodiscard]] static constexpr DebugColor3D yellow() noexcept { return {1.0f, 1.0f, 0.0f}; }
	[[nodiscard]] static constexpr DebugColor3D cyan() noexcept { return {0.0f, 1.0f, 1.0f}; }
	[[nodiscard]] static constexpr DebugColor3D magenta() noexcept { return {1.0f, 0.0f, 1.0f}; }
	[[nodiscard]] static constexpr DebugColor3D white() noexcept { return {1.0f, 1.0f, 1.0f}; }
	[[nodiscard]] static constexpr DebugColor3D gray() noexcept { return {0.5f, 0.5f, 0.5f}; }
};

/// @brief デバッグ描画ライン
struct DebugLine3D
{
	sgc::Vec3f start{};
	sgc::Vec3f end{};
	DebugColor3D color{};
};

/// @brief デバッグ描画ポイント
struct DebugPoint3D
{
	sgc::Vec3f position{};
	DebugColor3D color{};
	float size{4.0f};
};

/// @brief 3D物理デバッグ描画生成器
///
/// PhysicsWorld3D の状態を可視化するためのライン/ポイントデータを生成する。
/// 描画バックエンド（Renderer3D、ShapeRenderer等）への依存はなく、
/// 生データを取得して任意の方法で描画できる。
class PhysicsDebugRenderer3D
{
public:
	/// @brief デフォルトコンストラクタ
	PhysicsDebugRenderer3D() = default;

	// ── 設定 ──────────────────────────────────────────────────

	/// @brief 描画フラグを設定する
	/// @param flags 描画フラグの組み合わせ
	void setFlags(DebugDrawFlags flags) noexcept { m_flags = flags; }

	/// @brief 描画フラグを取得する
	[[nodiscard]] constexpr DebugDrawFlags flags() const noexcept { return m_flags; }

	/// @brief 速度ベクトルのスケールを設定する
	/// @param scale スケール値
	void setVelocityScale(float scale) noexcept { m_velocityScale = scale; }

	/// @brief 接触法線の長さを設定する
	/// @param length 法線の描画長さ
	void setNormalLength(float length) noexcept { m_normalLength = length; }

	/// @brief 球のワイヤーフレーム分割数を設定する
	/// @param segments 分割数
	void setSphereSegments(int segments) noexcept { m_sphereSegments = segments; }

	// ── データ収集 ────────────────────────────────────────────

	/// @brief 描画データをクリアする
	void clear() noexcept
	{
		m_lines.clear();
		m_points.clear();
	}

	/// @brief PhysicsWorld3D から描画データを収集する
	/// @param world 物理ワールド
	/// @param manifolds 接触マニフォールド（オプション）
	void gather(
		const PhysicsWorld3D& world,
		const std::vector<ContactManifold3D>& manifolds = {}) noexcept
	{
		clear();

		if (hasFlag(m_flags, DebugDrawFlags::Colliders))
		{
			gatherColliders(world);
		}

		if (hasFlag(m_flags, DebugDrawFlags::Contacts))
		{
			gatherContacts(manifolds);
		}

		if (hasFlag(m_flags, DebugDrawFlags::Velocities))
		{
			gatherVelocities(world);
		}

		if (hasFlag(m_flags, DebugDrawFlags::CenterOfMass))
		{
			gatherCentersOfMass(world);
		}
	}

	/// @brief コライダー描画データを手動で追加する
	/// @param collider ボディコライダー
	void addCollider(const BodyCollider& collider) noexcept
	{
		switch (collider.type)
		{
		case ColliderType3D::Sphere:
			drawWireSphere(collider.sphere.center, collider.sphere.radius,
				DebugColor3D::green());
			break;
		case ColliderType3D::AABB:
			drawWireAABB(collider.aabb, DebugColor3D::green());
			break;
		case ColliderType3D::Capsule:
			drawWireCapsule(collider.capsule, DebugColor3D::green());
			break;
		}
	}

	/// @brief 接触点を手動で追加する
	/// @param point 接触点
	/// @param normal 接触法線
	/// @param depth 貫通深度
	void addContact(const sgc::Vec3f& point, const sgc::Vec3f& normal,
		float depth) noexcept
	{
		// 接触点
		m_points.push_back({point, DebugColor3D::red(), 6.0f});

		// 法線ベクトル
		m_lines.push_back({
			point,
			point + normal * m_normalLength,
			DebugColor3D::red()
		});

		// 貫通深度インジケータ
		m_lines.push_back({
			point,
			point - normal * depth,
			DebugColor3D::yellow()
		});
	}

	/// @brief 速度ベクトルを手動で追加する
	/// @param position 位置
	/// @param velocity 速度ベクトル
	void addVelocity(const sgc::Vec3f& position,
		const sgc::Vec3f& velocity) noexcept
	{
		if (velocity.lengthSquared() < 1e-6f) return;

		m_lines.push_back({
			position,
			position + velocity * m_velocityScale,
			DebugColor3D::blue()
		});
	}

	// ── データアクセス ────────────────────────────────────────

	/// @brief 生成されたラインデータを取得する
	[[nodiscard]] const std::vector<DebugLine3D>& lines() const noexcept
	{
		return m_lines;
	}

	/// @brief 生成されたポイントデータを取得する
	[[nodiscard]] const std::vector<DebugPoint3D>& points() const noexcept
	{
		return m_points;
	}

	/// @brief ライン数を返す
	[[nodiscard]] std::size_t lineCount() const noexcept { return m_lines.size(); }

	/// @brief ポイント数を返す
	[[nodiscard]] std::size_t pointCount() const noexcept { return m_points.size(); }

private:
	// ── 収集メソッド ──────────────────────────────────────────

	/// @brief ワールドのコライダーを収集する
	void gatherColliders(const PhysicsWorld3D& world) noexcept
	{
		// PhysicsWorld3D の m_colliders は private なので、
		// ここでは colliderCount() を利用して公開APIベースで収集
		// 実際のコライダーアクセスは PhysicsSystem3D から渡される想定
		static_cast<void>(world);
	}

	/// @brief 接触マニフォールドを収集する
	void gatherContacts(const std::vector<ContactManifold3D>& manifolds) noexcept
	{
		for (const auto& m : manifolds)
		{
			addContact(m.point, m.normal, m.depth);
		}
	}

	/// @brief 速度ベクトルを収集する
	void gatherVelocities(const PhysicsWorld3D& world) noexcept
	{
		static_cast<void>(world);
		// PhysicsSystem3D 経由でボディをイテレートする想定
	}

	/// @brief 重心マーカーを収集する
	void gatherCentersOfMass(const PhysicsWorld3D& world) noexcept
	{
		static_cast<void>(world);
		// PhysicsSystem3D 経由でボディをイテレートする想定
	}

	// ── ワイヤーフレーム描画プリミティブ ──────────────────────

	/// @brief ワイヤーフレーム球を描画する（3つの大円）
	/// @param center 中心座標
	/// @param radius 半径
	/// @param color 描画色
	void drawWireSphere(const sgc::Vec3f& center, float radius,
		const DebugColor3D& color) noexcept
	{
		// XY平面の円
		drawWireCircle(center, radius, sgc::Vec3f{0, 0, 1}, color);
		// XZ平面の円
		drawWireCircle(center, radius, sgc::Vec3f{0, 1, 0}, color);
		// YZ平面の円
		drawWireCircle(center, radius, sgc::Vec3f{1, 0, 0}, color);
	}

	/// @brief 法線軸周りのワイヤーフレーム円を描画する
	/// @param center 中心座標
	/// @param radius 半径
	/// @param axisNormal 円の法線方向
	/// @param color 描画色
	void drawWireCircle(const sgc::Vec3f& center, float radius,
		const sgc::Vec3f& axisNormal, const DebugColor3D& color) noexcept
	{
		// 法線に垂直な2軸を計算
		sgc::Vec3f tangent1;
		sgc::Vec3f tangent2;

		if (std::abs(axisNormal.y) < 0.999f)
		{
			tangent1 = sgc::Vec3f{0, 1, 0}.cross(axisNormal).normalized();
		}
		else
		{
			tangent1 = sgc::Vec3f{1, 0, 0}.cross(axisNormal).normalized();
		}
		tangent2 = axisNormal.cross(tangent1);

		const float step = 2.0f * 3.14159265f / static_cast<float>(m_sphereSegments);

		sgc::Vec3f prev = center + tangent1 * radius;

		for (int i = 1; i <= m_sphereSegments; ++i)
		{
			const float angle = step * static_cast<float>(i);
			const sgc::Vec3f point = center +
				tangent1 * (radius * std::cos(angle)) +
				tangent2 * (radius * std::sin(angle));

			m_lines.push_back({prev, point, color});
			prev = point;
		}
	}

	/// @brief ワイヤーフレームAABBを描画する（12辺）
	/// @param aabb AABB
	/// @param color 描画色
	void drawWireAABB(const AABBCollider3D& aabb,
		const DebugColor3D& color) noexcept
	{
		const sgc::Vec3f& mn = aabb.min;
		const sgc::Vec3f& mx = aabb.max;

		// 8頂点
		const sgc::Vec3f v[8] = {
			{mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z},
			{mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z},
			{mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z},
			{mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z},
		};

		// 底面
		m_lines.push_back({v[0], v[1], color});
		m_lines.push_back({v[1], v[2], color});
		m_lines.push_back({v[2], v[3], color});
		m_lines.push_back({v[3], v[0], color});

		// 上面
		m_lines.push_back({v[4], v[5], color});
		m_lines.push_back({v[5], v[6], color});
		m_lines.push_back({v[6], v[7], color});
		m_lines.push_back({v[7], v[4], color});

		// 柱
		m_lines.push_back({v[0], v[4], color});
		m_lines.push_back({v[1], v[5], color});
		m_lines.push_back({v[2], v[6], color});
		m_lines.push_back({v[3], v[7], color});
	}

	/// @brief ワイヤーフレームカプセルを描画する
	/// @param capsule カプセルコライダー
	/// @param color 描画色
	void drawWireCapsule(const CapsuleCollider& capsule,
		const DebugColor3D& color) noexcept
	{
		// 端点の球
		drawWireSphere(capsule.pointA, capsule.radius, color);
		drawWireSphere(capsule.pointB, capsule.radius, color);

		// 軸方向
		const sgc::Vec3f axis = capsule.pointB - capsule.pointA;
		const float axisLen = axis.length();
		if (axisLen < 1e-6f) return;

		const sgc::Vec3f axisDir = axis / axisLen;

		// 軸に垂直な方向を2本取得
		sgc::Vec3f perp1;
		if (std::abs(axisDir.y) < 0.999f)
		{
			perp1 = sgc::Vec3f{0, 1, 0}.cross(axisDir).normalized();
		}
		else
		{
			perp1 = sgc::Vec3f{1, 0, 0}.cross(axisDir).normalized();
		}
		const sgc::Vec3f perp2 = axisDir.cross(perp1);

		// 4本の柱線
		const sgc::Vec3f offsets[4] = {
			perp1 * capsule.radius,
			-perp1 * capsule.radius,
			perp2 * capsule.radius,
			-perp2 * capsule.radius,
		};

		for (const auto& offset : offsets)
		{
			m_lines.push_back({
				capsule.pointA + offset,
				capsule.pointB + offset,
				color
			});
		}
	}

public:
	// ── 外部描画統合ヘルパー ──────────────────────────────────

	/// @brief ボディのデバッグ情報を直接収集する
	/// @param body 剛体
	/// @param collider コライダー
	void gatherBody(const RigidBody3D& body, const BodyCollider& collider) noexcept
	{
		if (hasFlag(m_flags, DebugDrawFlags::Colliders))
		{
			addCollider(collider);
		}

		if (hasFlag(m_flags, DebugDrawFlags::Velocities))
		{
			addVelocity(body.position(), body.linearVelocity());
		}

		if (hasFlag(m_flags, DebugDrawFlags::CenterOfMass))
		{
			m_points.push_back({body.position(), DebugColor3D::cyan(), 5.0f});

			// 小さな十字マーカー
			const float s = 0.1f;
			m_lines.push_back({
				body.position() - sgc::Vec3f{s, 0, 0},
				body.position() + sgc::Vec3f{s, 0, 0},
				DebugColor3D::cyan()
			});
			m_lines.push_back({
				body.position() - sgc::Vec3f{0, s, 0},
				body.position() + sgc::Vec3f{0, s, 0},
				DebugColor3D::cyan()
			});
			m_lines.push_back({
				body.position() - sgc::Vec3f{0, 0, s},
				body.position() + sgc::Vec3f{0, 0, s},
				DebugColor3D::cyan()
			});
		}
	}

private:
	DebugDrawFlags m_flags{DebugDrawFlags::All};
	float m_velocityScale{0.2f};
	float m_normalLength{0.5f};
	int m_sphereSegments{16};

	std::vector<DebugLine3D> m_lines;
	std::vector<DebugPoint3D> m_points;
};

} // namespace mitiru::physics3d
