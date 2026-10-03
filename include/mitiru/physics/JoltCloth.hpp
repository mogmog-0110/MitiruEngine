#pragma once

/// @file JoltCloth.hpp
/// @brief マントや旗の布 (Jolt の soft body) と、布が当たるキャラの体のカプセル (ADR 0069、docs/PHYSICS_FX.md)。
/// @details 布は JoltGameWorld の組み立てで作り、留める頂点 (上の行か左の列) は 1 本の関節に付けて、毎 tick
///          follow でその関節のワールド行列へ運ぶ (Jolt の skinned constraint)。頂点の位置・速度・留め具の状態は
///          Jolt の保存に入り、JoltGameWorld の窓口 (ADR 0054) で GameMemory と一緒に戻る。1 スレッドで解くので
///          同じ入力から同じビットになる。風は頂点の速度へ足すだけで、状態を持たない。

#ifdef MITIRU_HAS_JOLT

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <span>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/SoftBody/SoftBodyCreationSettings.h>
#include <Jolt/Physics/SoftBody/SoftBodyMotionProperties.h>
#include <Jolt/Physics/SoftBody/SoftBodySharedSettings.h>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>

#include <mitiru/animation/AnimMath.hpp>
#include <mitiru/physics/JoltGameWorld.hpp>

namespace mitiru::physics3d
{

enum class ClothPin : std::uint8_t
{
	TopRow = 0,      ///< 上の行を留める (マント)
	LeftColumn = 1,  ///< 左の列を留める (旗)
};

/// @brief 格子の布。布の局所で x が右、row が下 (-y)、上の辺の中央が原点
struct ClothSheetDesc
{
	int columns = 10;
	int rows = 14;
	float width = 0.6f;
	float height = 0.9f;
	ClothPin pin = ClothPin::TopRow;
	float compliance = 1e-5f;        ///< 伸びの柔らかさ (0 で伸びない)
	float shearCompliance = 1e-5f;
	float bendCompliance = 1e-2f;    ///< 曲げの柔らかさ (大きいほど柔らかい布)
	float vertexRadius = 0.02f;      ///< 頂点の太さ (体の表面から少し浮かせる)
	std::uint32_t iterations = 8;
	float linearDamping = 0.3f;
	float friction = 0.3f;
	float gravityFactor = 1.0f;
};

namespace detail::cloth
{

[[nodiscard]] inline JPH::Mat44 toJolt(const sgc::Mat4f& m) noexcept
{
	return JPH::Mat44(JPH::Vec4(m.m[0][0], m.m[1][0], m.m[2][0], m.m[3][0]),
	                  JPH::Vec4(m.m[0][1], m.m[1][1], m.m[2][1], m.m[3][1]),
	                  JPH::Vec4(m.m[0][2], m.m[1][2], m.m[2][2], m.m[3][2]),
	                  JPH::Vec4(m.m[0][3], m.m[1][3], m.m[2][3], m.m[3][3]));
}

[[nodiscard]] inline bool pinned(const ClothSheetDesc& d, int x, int y) noexcept
{
	return d.pin == ClothPin::TopRow ? y == 0 : x == 0;
}

} // namespace detail::cloth

class JoltCloth
{
public:
	/// @brief 組み立ての中で作る。sheetToJoint は布の局所 → 関節の局所、jointWorld は今の関節のワールド行列
	void create(JoltGameWorld& world, const ClothSheetDesc& desc, const sgc::Mat4f& sheetToJoint, const sgc::Mat4f& jointWorld)
	{
		m_desc = desc;
		const sgc::Mat4f sheetWorld = jointWorld * sheetToJoint;
		const sgc::Vec3f origin = sheetWorld.transformPoint({0, 0, 0});
		const JPH::Ref<JPH::SoftBodySharedSettings> s = sharedSettings(desc, sheetWorld, origin);
		// 関節のバインド姿勢を body の局所 (原点 = 布の上の辺の中央) で持つ
		const JPH::Mat44 bind = JPH::Mat44::sTranslation(JPH::Vec3(-origin.x, -origin.y, -origin.z)) * detail::cloth::toJolt(jointWorld);
		s->mInvBindMatrices.push_back(JPH::SoftBodySharedSettings::InvBind(0, bind.Inversed()));
		addPins(*s);
		s->CalculateSkinnedConstraintNormals();
		s->Optimize();
		JPH::SoftBodyCreationSettings cs(s, JPH::RVec3(origin.x, origin.y, origin.z), JPH::Quat::sIdentity(), JoltGameWorld::kMovingLayer);
		cs.mNumIterations = desc.iterations;
		cs.mLinearDamping = desc.linearDamping;
		cs.mFriction = desc.friction;
		cs.mGravityFactor = desc.gravityFactor;
		cs.mVertexRadius = desc.vertexRadius;
		cs.mAllowSleeping = false;
		m_body = world.addSoftBody(cs);
		skin(world, jointWorld, true);
	}

	/// @brief 留めた頂点を関節の今のワールド行列へ運ぶ。step の前に毎 tick 呼ぶ
	void follow(JoltGameWorld& world, const sgc::Mat4f& jointWorld) { skin(world, jointWorld, false); }

	/// @brief 風。頂点の速度の、面に垂直な成分を風の速さへ drag の割合で寄せる (dt 秒ぶん)
	void wind(JoltGameWorld& world, const sgc::Vec3f& velocity, float drag, float dt)
	{
		const JPH::BodyLockWrite lock(world.system().GetBodyLockInterfaceNoLock(), m_body);
		if (!lock.Succeeded()) { return; }
		auto* mp = static_cast<JPH::SoftBodyMotionProperties*>(lock.GetBody().GetMotionProperties());
		auto& verts = mp->GetVertices();
		const JPH::Vec3 w(velocity.x, velocity.y, velocity.z);
		const float k = drag * dt;
		for (int y = 0; y < m_desc.rows; ++y)
		{
			for (int x = 0; x < m_desc.columns; ++x)
			{
				auto& v = verts[static_cast<std::size_t>(y * m_desc.columns + x)];
				if (v.mInvMass <= 0.0f) { continue; }
				const JPH::Vec3 n = normalAt(verts, x, y);
				v.mVelocity += n * ((w - v.mVelocity).Dot(n) * k);
			}
		}
	}

	/// @brief 頂点のワールド座標を out へ書く (columns × rows 個、row 0 が上)
	void readPositions(JoltGameWorld& world, std::span<sgc::Vec3f> out) const
	{
		const JPH::BodyLockRead lock(world.system().GetBodyLockInterfaceNoLock(), m_body);
		if (!lock.Succeeded()) { return; }
		const JPH::RMat44 com = lock.GetBody().GetCenterOfMassTransform();
		const auto* mp = static_cast<const JPH::SoftBodyMotionProperties*>(lock.GetBody().GetMotionProperties());
		const auto& verts = mp->GetVertices();
		const std::size_t n = std::min(out.size(), verts.size());
		for (std::size_t i = 0; i < n; ++i)
		{
			const JPH::RVec3 p = com * verts[i].mPosition;
			out[i] = {static_cast<float>(p.GetX()), static_cast<float>(p.GetY()), static_cast<float>(p.GetZ())};
		}
	}

	[[nodiscard]] int columns() const noexcept { return m_desc.columns; }
	[[nodiscard]] int rows() const noexcept { return m_desc.rows; }
	[[nodiscard]] int vertexCount() const noexcept { return m_desc.columns * m_desc.rows; }
	[[nodiscard]] JPH::BodyID body() const noexcept { return m_body; }

private:
	[[nodiscard]] static JPH::Ref<JPH::SoftBodySharedSettings> sharedSettings(const ClothSheetDesc& d, const sgc::Mat4f& sheetWorld,
	                                                                          const sgc::Vec3f& origin)
	{
		JPH::Ref<JPH::SoftBodySharedSettings> s = new JPH::SoftBodySharedSettings;
		const float dx = d.width / static_cast<float>(d.columns - 1);
		const float dy = d.height / static_cast<float>(d.rows - 1);
		for (int y = 0; y < d.rows; ++y)
		{
			for (int x = 0; x < d.columns; ++x)
			{
				const sgc::Vec3f p = sheetWorld.transformPoint({-0.5f * d.width + dx * static_cast<float>(x), -dy * static_cast<float>(y), 0.0f}) - origin;
				JPH::SoftBodySharedSettings::Vertex v;
				v.mPosition = JPH::Float3(p.x, p.y, p.z);
				v.mInvMass = detail::cloth::pinned(d, x, y) ? 0.0f : 1.0f;
				s->mVertices.push_back(v);
			}
		}
		for (int y = 0; y + 1 < d.rows; ++y)
		{
			for (int x = 0; x + 1 < d.columns; ++x)
			{
				const auto i00 = static_cast<JPH::uint32>(y * d.columns + x);
				const auto i01 = i00 + static_cast<JPH::uint32>(d.columns);
				s->AddFace(JPH::SoftBodySharedSettings::Face(i00, i01, i00 + 1));
				s->AddFace(JPH::SoftBodySharedSettings::Face(i00 + 1, i01, i01 + 1));
			}
		}
		const JPH::SoftBodySharedSettings::VertexAttributes attr(d.compliance, d.shearCompliance, d.bendCompliance);
		s->CreateConstraints(&attr, 1, JPH::SoftBodySharedSettings::EBendType::Distance);
		return s;
	}

	/// @brief 留める頂点を関節 0 に重み 1 で固く付ける
	void addPins(JPH::SoftBodySharedSettings& s) const
	{
		for (int y = 0; y < m_desc.rows; ++y)
		{
			for (int x = 0; x < m_desc.columns; ++x)
			{
				if (!detail::cloth::pinned(m_desc, x, y)) { continue; }
				JPH::SoftBodySharedSettings::Skinned sk(static_cast<JPH::uint32>(y * m_desc.columns + x), 0.0f, FLT_MAX, 0.0f);
				sk.mWeights[0] = JPH::SoftBodySharedSettings::SkinWeight(0, 1.0f);
				s.mSkinnedConstraints.push_back(sk);
			}
		}
	}

	void skin(JoltGameWorld& world, const sgc::Mat4f& jointWorld, bool hard)
	{
		const JPH::BodyLockWrite lock(world.system().GetBodyLockInterfaceNoLock(), m_body);
		if (!lock.Succeeded()) { return; }
		JPH::Body& b = lock.GetBody();
		const JPH::RMat44 com = b.GetCenterOfMassTransform();
		const JPH::Mat44 joint = com.InversedRotationTranslation().ToMat44() * detail::cloth::toJolt(jointWorld);
		auto* mp = static_cast<JPH::SoftBodyMotionProperties*>(b.GetMotionProperties());
		mp->SkinVertices(com, &joint, 1, hard, world.tempAllocator());
	}

	[[nodiscard]] JPH::Vec3 normalAt(const JPH::Array<JPH::SoftBodyMotionProperties::Vertex>& v, int x, int y) const
	{
		const auto at = [&](int cx, int cy) { return v[static_cast<std::size_t>(cy * m_desc.columns + cx)].mPosition; };
		const JPH::Vec3 dx = at(x + 1 < m_desc.columns ? x + 1 : x, y) - at(x > 0 ? x - 1 : x, y);
		const JPH::Vec3 dy = at(x, y + 1 < m_desc.rows ? y + 1 : y) - at(x, y > 0 ? y - 1 : y);
		return dy.Cross(dx).NormalizedOr(JPH::Vec3::sZero());
	}

	ClothSheetDesc m_desc{};
	JPH::BodyID m_body;
};

/// @brief 2 点の間に置くカプセルの kinematic body。布をキャラの体に当てるために、骨の位置へ毎 tick 運ぶ
class KinematicCapsule
{
public:
	/// @brief 組み立ての中で作る。a と b はカプセルの両端の芯 (長さは作った時のまま)
	void create(JoltGameWorld& world, const sgc::Vec3f& a, const sgc::Vec3f& b, float radius)
	{
		const float halfHeight = 0.5f * (b - a).length();
		JPH::BodyCreationSettings s(new JPH::CapsuleShape(halfHeight > 1e-4f ? halfHeight : 1e-4f, radius), center(a, b), rotation(a, b),
			JPH::EMotionType::Kinematic, JoltGameWorld::kMovingLayer);
		m_body = world.addBody(s, JPH::EActivation::Activate);
	}

	/// @brief 両端を a と b へ dt 秒で運ぶ (step の前に呼ぶ)
	void follow(JoltGameWorld& world, const sgc::Vec3f& a, const sgc::Vec3f& b, float dt)
	{
		world.bodies().MoveKinematic(m_body, center(a, b), rotation(a, b), dt);
	}

	[[nodiscard]] JPH::BodyID body() const noexcept { return m_body; }

private:
	[[nodiscard]] static JPH::RVec3 center(const sgc::Vec3f& a, const sgc::Vec3f& b) noexcept
	{
		const sgc::Vec3f c = (a + b) * 0.5f;
		return {c.x, c.y, c.z};
	}

	[[nodiscard]] static JPH::Quat rotation(const sgc::Vec3f& a, const sgc::Vec3f& b)
	{
		const sgc::Vec3f d = b - a;
		const float len = d.length();
		const sgc::Vec4f q = len > 1e-6f ? animation::quatFromTo({0, 1, 0}, d / len) : animation::kQuatIdentity;
		return JPH::Quat(q.x, q.y, q.z, q.w).Normalized();
	}

	JPH::BodyID m_body;
};

} // namespace mitiru::physics3d

#endif // MITIRU_HAS_JOLT
