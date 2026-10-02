#pragma once

/// @file JoltHumanoidRagdoll.hpp
/// @brief 人の形の ragdoll (11 部位のカプセル) の設定を作る。JoltGameWorld::addRagdoll に渡す。
/// @details 骨格は腰を根に、胸・頭・左右の上腕と前腕・左右の腿と脛。関節は swing-twist で、
/// 曲がる角度は人の関節のおおよその範囲にしてある。原点は腰の位置で、立った姿勢 (y が上) で作る。

#ifdef MITIRU_HAS_JOLT

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>
#include <Jolt/Skeleton/Skeleton.h>

#include <mitiru/physics/detail/JoltBackend.hpp>

namespace mitiru::physics3d
{

namespace detail::jolt
{

struct HumanoidPart
{
	const char* name;
	int         parent;      ///< -1 = 根
	JPH::Vec3   from;        ///< 部位の根元 (親との関節の位置、腰からの相対)
	JPH::Vec3   to;          ///< 部位の先
	float       radius;
	float       coneDeg;     ///< swing の半角
	float       twistDeg;    ///< twist の片側の角度
};

inline constexpr int kHumanoidPartCount = 11;

inline const HumanoidPart* humanoidParts() noexcept
{
	static const HumanoidPart parts[kHumanoidPartCount] = {
		{"pelvis",    -1, JPH::Vec3( 0.00f,  0.00f, 0), JPH::Vec3( 0.00f,  0.20f, 0), 0.14f,  0.0f,  0.0f},
		{"chest",      0, JPH::Vec3( 0.00f,  0.20f, 0), JPH::Vec3( 0.00f,  0.55f, 0), 0.15f, 30.0f, 20.0f},
		{"head",       1, JPH::Vec3( 0.00f,  0.62f, 0), JPH::Vec3( 0.00f,  0.82f, 0), 0.11f, 40.0f, 40.0f},
		{"upperArmL",  1, JPH::Vec3(-0.20f,  0.52f, 0), JPH::Vec3(-0.48f,  0.52f, 0), 0.05f, 80.0f, 40.0f},
		{"lowerArmL",  3, JPH::Vec3(-0.48f,  0.52f, 0), JPH::Vec3(-0.74f,  0.52f, 0), 0.045f, 70.0f, 10.0f},
		{"upperArmR",  1, JPH::Vec3( 0.20f,  0.52f, 0), JPH::Vec3( 0.48f,  0.52f, 0), 0.05f, 80.0f, 40.0f},
		{"lowerArmR",  5, JPH::Vec3( 0.48f,  0.52f, 0), JPH::Vec3( 0.74f,  0.52f, 0), 0.045f, 70.0f, 10.0f},
		{"thighL",     0, JPH::Vec3(-0.10f, -0.02f, 0), JPH::Vec3(-0.10f, -0.45f, 0), 0.07f, 50.0f, 20.0f},
		{"shinL",      7, JPH::Vec3(-0.10f, -0.45f, 0), JPH::Vec3(-0.10f, -0.88f, 0), 0.055f, 60.0f, 5.0f},
		{"thighR",     0, JPH::Vec3( 0.10f, -0.02f, 0), JPH::Vec3( 0.10f, -0.45f, 0), 0.07f, 50.0f, 20.0f},
		{"shinR",      9, JPH::Vec3( 0.10f, -0.45f, 0), JPH::Vec3( 0.10f, -0.88f, 0), 0.055f, 60.0f, 5.0f},
	};
	return parts;
}

}  // namespace detail::jolt

/// @brief 人の形の ragdoll の部位の数 (Ragdoll::GetBodyID の添字 0..10、0 が腰)。
inline constexpr int kHumanoidPartCount = detail::jolt::kHumanoidPartCount;

/// @brief 部位 part のカプセルの半径と、両端の球を含めた長さ (描くときの大きさ)。
struct HumanoidPartSize
{
	float radius;
	float length;
};

[[nodiscard]] inline HumanoidPartSize humanoidPartSize(int part) noexcept
{
	const auto& p = detail::jolt::humanoidParts()[part];
	return {p.radius, (p.to - p.from).Length() + 2.0f * p.radius};
}

/// @brief 人の形の ragdoll の設定。rootPosition は腰の world 座標。layer は動く body の層。
[[nodiscard]] inline JPH::Ref<JPH::RagdollSettings> makeHumanoidRagdollSettings(JPH::RVec3 rootPosition,
	JPH::ObjectLayer layer, float massScale = 1.0f)
{
	using detail::jolt::humanoidParts;
	JPH::Ref<JPH::Skeleton> skeleton = new JPH::Skeleton;
	for (int i = 0; i < detail::jolt::kHumanoidPartCount; ++i)
	{
		(void)skeleton->AddJoint(humanoidParts()[i].name, humanoidParts()[i].parent);
	}

	JPH::Ref<JPH::RagdollSettings> settings = new JPH::RagdollSettings;
	settings->mSkeleton = skeleton;
	settings->mParts.resize(detail::jolt::kHumanoidPartCount);
	for (int i = 0; i < detail::jolt::kHumanoidPartCount; ++i)
	{
		const auto& p = humanoidParts()[i];
		const JPH::Vec3 axis = p.to - p.from;
		const float length = axis.Length();
		const JPH::Vec3 dir = axis / length;
		JPH::RagdollSettings::Part& part = settings->mParts[static_cast<std::size_t>(i)];
		// カプセルは y 軸向きなので、y を部位の向きへ回す。
		part.SetShape(new JPH::CapsuleShape(0.5f * length, p.radius));
		part.mPosition    = rootPosition + JPH::RVec3(0.5f * (p.from + p.to));
		part.mRotation    = JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), dir);
		part.mMotionType  = JPH::EMotionType::Dynamic;
		part.mObjectLayer = layer;
		part.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
		part.mMassPropertiesOverride.mMass = massScale * 8.0f * length;
		if (p.parent < 0) { continue; }

		JPH::Ref<JPH::SwingTwistConstraintSettings> joint = new JPH::SwingTwistConstraintSettings;
		joint->mPosition1 = joint->mPosition2 = rootPosition + JPH::RVec3(p.from);
		joint->mTwistAxis1 = joint->mTwistAxis2 = dir;
		joint->mPlaneAxis1 = joint->mPlaneAxis2 = dir.GetNormalizedPerpendicular();
		joint->mNormalHalfConeAngle = JPH::DegreesToRadians(p.coneDeg);
		joint->mPlaneHalfConeAngle  = JPH::DegreesToRadians(p.coneDeg);
		joint->mTwistMinAngle = -JPH::DegreesToRadians(p.twistDeg);
		joint->mTwistMaxAngle =  JPH::DegreesToRadians(p.twistDeg);
		part.mToParent = joint;
	}
	(void)settings->Stabilize();
	settings->DisableParentChildCollisions();
	settings->CalculateBodyIndexToConstraintIndex();
	settings->CalculateConstraintIndexToBodyIdxPair();
	return settings;
}

}  // namespace mitiru::physics3d

#endif  // MITIRU_HAS_JOLT
