#pragma once

/// @file JoltPoweredRagdoll.hpp
/// @brief モデルの骨から作る ragdoll を、アニメの姿勢へ motor と運動学で追わせる (powered ragdoll)。
/// @details 部位は RagdollRig (`<名前>.ragdoll.json`) から作り、JoltGameWorld が持つ。使い方は 4 つ。
///          1. JoltGameWorld の組み立ての中で create (部位を足す)。
///          2. update で、アニメの姿勢から ragdollTargetsFromPose で部位の目標を出し、drive してから world を step。
///          3. read で部位の位置と向きを GameMemory へ写す。
///          4. draw で blendRagdollPose に写した部位と重み (ragdollWeightsFor) を渡し、drawModelNodeMatrices で描く。
///          motor の目標は Jolt の Ragdoll::DriveToPoseUsingMotors に渡す。motor の強さ・重力の掛かり方・運動学で運ぶ部位は
///          Jolt の状態 (SaveState) に入らないので、drive が毎フレーム RagdollDrive (GameMemory の状態から出す) で決め直す。

#ifdef MITIRU_HAS_JOLT

#include <algorithm>
#include <span>
#include <vector>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>
#include <Jolt/Skeleton/Skeleton.h>
#include <Jolt/Skeleton/SkeletonPose.h>

#include <mitiru/physics/JoltGameWorld.hpp>
#include <mitiru/physics/RagdollBlend.hpp>
#include <mitiru/physics/RagdollRig.hpp>

namespace mitiru::physics3d
{

namespace detail::jolt
{

[[nodiscard]] inline JPH::Quat toJoltQuat(const sgc::Vec4f& q) noexcept { return JPH::Quat(q.x, q.y, q.z, q.w).Normalized(); }

/// @brief 部位 i の関節 (SwingTwist) の設定。位置と軸はワールド
[[nodiscard]] inline JPH::Ref<JPH::SwingTwistConstraintSettings> rigJoint(const RagdollPart& p, JPH::RVec3 jointWorld,
                                                                         JPH::Vec3 axisWorld)
{
	JPH::Ref<JPH::SwingTwistConstraintSettings> j = new JPH::SwingTwistConstraintSettings;
	j->mPosition1 = j->mPosition2 = jointWorld;
	j->mTwistAxis1 = j->mTwistAxis2 = axisWorld;
	j->mPlaneAxis1 = j->mPlaneAxis2 = axisWorld.GetNormalizedPerpendicular();
	j->mNormalHalfConeAngle = JPH::DegreesToRadians(p.coneDeg);
	j->mPlaneHalfConeAngle = JPH::DegreesToRadians(p.coneDeg);
	j->mTwistMinAngle = -JPH::DegreesToRadians(p.twistDeg);
	j->mTwistMaxAngle = JPH::DegreesToRadians(p.twistDeg);
	return j;
}

/// @brief 部位の体積 (カプセル)
[[nodiscard]] inline float partVolume(const RagdollPart& p, float scale) noexcept
{
	const float r = p.radius * scale;
	const float len = (p.restTo - p.restFrom).length() * scale;
	return 3.14159265f * r * r * (len + 4.0f / 3.0f * r);
}

} // namespace detail::jolt

/// @brief 部位から Jolt の ragdoll の設定を作る。restWorld に置いたレスト姿勢で作り、totalMassKg を体積で分ける
/// @details restWorld は平行移動・回転・一様なスケールだけの行列 (スケールは部位の長さと太さに掛かる)
[[nodiscard]] inline JPH::Ref<JPH::RagdollSettings> makeRigRagdollSettings(const RagdollRig& rig, const sgc::Mat4f& restWorld,
                                                                           JPH::ObjectLayer layer, float totalMassKg = 60.0f)
{
	const RagdollPlacement place = RagdollPlacement::of(restWorld);
	JPH::Ref<JPH::Skeleton> skeleton = new JPH::Skeleton;
	float volume = 0.0f;
	for (const auto& p : rig.parts)
	{
		(void)skeleton->AddJoint(p.name, p.parent);
		volume += detail::jolt::partVolume(p, place.s);
	}
	JPH::Ref<JPH::RagdollSettings> settings = new JPH::RagdollSettings;
	settings->mSkeleton = skeleton;
	settings->mParts.resize(rig.parts.size());
	const auto toWorld = [&](const sgc::Vec3f& m) { return place.t + animation::quatRotate(place.r, m * place.s); };
	for (std::size_t i = 0; i < rig.parts.size(); ++i)
	{
		const auto& p = rig.parts[i];
		const sgc::Vec3f from = toWorld(p.restFrom), to = toWorld(p.restTo);
		const sgc::Vec3f axis = (to - from).normalized();
		JPH::RagdollSettings::Part& part = settings->mParts[i];
		part.SetShape(new JPH::CapsuleShape(0.5f * (to - from).length(), p.radius * place.s));
		part.mPosition = JPH::RVec3(detail::jolt::toJolt((from + to) * 0.5f));
		part.mRotation = JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), detail::jolt::toJolt(axis));
		part.mMotionType = JPH::EMotionType::Dynamic;
		part.mObjectLayer = layer;
		part.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
		part.mMassPropertiesOverride.mMass = totalMassKg * detail::jolt::partVolume(p, place.s) / std::max(volume, 1e-6f);
		if (p.parent >= 0)
		{
			part.mToParent = detail::jolt::rigJoint(p, JPH::RVec3(detail::jolt::toJolt(from)), detail::jolt::toJolt(axis));
		}
	}
	(void)settings->Stabilize();
	// 親子に加えて、レスト姿勢で 2 cm より近い部位どうしも当てない (太い胴と腕の付け根が重なるモデルで弾き合わない)
	std::vector<JPH::Mat44> rest(settings->mParts.size());
	for (std::size_t i = 0; i < rest.size(); ++i)
	{
		rest[i] = JPH::Mat44::sRotationTranslation(settings->mParts[i].mRotation, JPH::Vec3(settings->mParts[i].mPosition));
	}
	settings->DisableParentChildCollisions(rest.data(), 0.02f);
	settings->CalculateBodyIndexToConstraintIndex();
	settings->CalculateConstraintIndexToBodyIdxPair();
	return settings;
}

/// @brief アニメの姿勢を追う ragdoll 1 体。部位の body は JoltGameWorld が持ち、窓口で巻き戻る
class PoweredRagdoll
{
public:
	/// @brief 組み立て (JoltGameWorld の BuildFn) の中で呼ぶ。restWorld に置いたレスト姿勢で足す
	void create(JoltGameWorld& world, const RagdollRig& rig, const sgc::Mat4f& restWorld,
	            JPH::CollisionGroup::GroupID group, float totalMassKg = 60.0f)
	{
		m_ragdoll = rig.valid() ? world.addRagdoll(*makeRigRagdollSettings(rig, restWorld, JoltGameWorld::kMovingLayer,
		                                                                   totalMassKg), group)
		                        : nullptr;
		m_parts = m_ragdoll != nullptr ? rig.partCount() : 0;
		if (m_ragdoll != nullptr) { m_pose.SetSkeleton(m_ragdoll->GetRagdollSettings()->GetSkeleton()); }
	}

	[[nodiscard]] bool valid() const noexcept { return m_ragdoll != nullptr; }
	[[nodiscard]] int partCount() const noexcept { return m_parts; }
	[[nodiscard]] JPH::BodyID body(int part) const { return m_ragdoll->GetBodyID(part); }

	/// @brief 部位を目標の位置と向きへ一度に置き、速度を 0 にする (起き上がり終えた時と、組み立ての直後)
	void teleport(JoltGameWorld& world, std::span<const RagdollPartPose> targets)
	{
		if (!valid() || static_cast<int>(targets.size()) < m_parts) { return; }
		JPH::BodyInterface& bi = world.bodies();
		for (int i = 0; i < m_parts; ++i)
		{
			const auto& t = targets[static_cast<std::size_t>(i)];
			bi.SetPositionAndRotation(body(i), JPH::RVec3(detail::jolt::toJolt(t.position)), detail::jolt::toJoltQuat(t.rotation),
			                          JPH::EActivation::Activate);
			bi.SetLinearAndAngularVelocity(body(i), JPH::Vec3::sZero(), JPH::Vec3::sZero());
		}
		m_ragdoll->ResetWarmStart();
	}

	/// @brief step の前に毎フレーム呼ぶ。motor の目標をアニメの姿勢にし、部位ごとの動かし方を決める
	void drive(JoltGameWorld& world, std::span<const RagdollPartPose> targets, const RagdollDrive& d, float dt)
	{
		if (!valid() || static_cast<int>(targets.size()) < m_parts || dt <= 0.0f) { return; }
		setMotorTargets(targets);
		JPH::BodyInterface& bi = world.bodies();
		const JPH::RagdollSettings* settings = m_ragdoll->GetRagdollSettings();
		for (int i = 0; i < m_parts; ++i)
		{
			const RagdollPartDrive mode = d.part[i];
			bi.SetGravityFactor(body(i), mode == RagdollPartDrive::Follow ? 0.0f : 1.0f);
			if (mode == RagdollPartDrive::Follow)
			{
				const auto& t = targets[static_cast<std::size_t>(i)];
				bi.MoveKinematic(body(i), JPH::RVec3(detail::jolt::toJolt(t.position)), detail::jolt::toJoltQuat(t.rotation), dt);
			}
			const int c = settings->GetConstraintIndexForBodyIndex(i);
			if (c >= 0) { configureJoint(*static_cast<JPH::SwingTwistConstraint*>(m_ragdoll->GetConstraint(c)), mode, d); }
		}
	}

	/// @brief 部位のワールドの位置と向きを写す (GameMemory へ置いて描画と当たり判定に使う)
	void read(JoltGameWorld& world, std::span<RagdollPartPose> out) const
	{
		if (!valid()) { return; }
		JPH::BodyInterface& bi = world.bodies();
		for (int i = 0; i < m_parts && static_cast<std::size_t>(i) < out.size(); ++i)
		{
			const JPH::RVec3 p = bi.GetPosition(body(i));
			const JPH::Quat q = bi.GetRotation(body(i));
			out[static_cast<std::size_t>(i)] = {{static_cast<float>(p.GetX()), static_cast<float>(p.GetY()), static_cast<float>(p.GetZ())},
			                                    {q.GetX(), q.GetY(), q.GetZ(), q.GetW()}};
		}
	}

private:
	void setMotorTargets(std::span<const RagdollPartPose> targets)
	{
		const JPH::RVec3 root(detail::jolt::toJolt(targets[0].position));
		m_pose.SetRootOffset(root);
		auto& mats = m_pose.GetJointMatrices();
		for (int i = 0; i < m_parts; ++i)
		{
			const auto& t = targets[static_cast<std::size_t>(i)];
			mats[static_cast<std::size_t>(i)] =
				JPH::Mat44::sRotationTranslation(detail::jolt::toJoltQuat(t.rotation), detail::jolt::toJolt(t.position) - JPH::Vec3(root));
		}
		m_pose.CalculateJointStates();
		m_ragdoll->DriveToPoseUsingMotors(m_pose);
	}

	static void configureJoint(JPH::SwingTwistConstraint& joint, RagdollPartDrive mode, const RagdollDrive& d)
	{
		if (mode == RagdollPartDrive::Limp)
		{
			joint.SetSwingMotorState(JPH::EMotorState::Off);
			joint.SetTwistMotorState(JPH::EMotorState::Off);
			joint.SetMaxFrictionTorque(d.limpFriction);
			return;
		}
		joint.SetMaxFrictionTorque(0.0f);
		for (JPH::MotorSettings* m : {&joint.GetSwingMotorSettings(), &joint.GetTwistMotorSettings()})
		{
			m->mSpringSettings.mFrequency = d.motorFrequency;
			m->mSpringSettings.mDamping = d.motorDamping;
			m->SetTorqueLimit(d.maxTorque);
		}
	}

	JPH::Ragdoll* m_ragdoll = nullptr;   ///< world が持つ
	JPH::SkeletonPose m_pose;            ///< motor の目標 (毎フレーム上書きし、確保しない)
	int m_parts = 0;
};

} // namespace mitiru::physics3d

#endif  // MITIRU_HAS_JOLT
