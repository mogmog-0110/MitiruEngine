#pragma once

/// @file JoltJoints.hpp
/// @brief JoltGameWorld::addJoint に渡す、ジョイント (Jolt の拘束) の設定の作り手。
/// @details 点や軸はワールド座標で渡す。作った時点の 2 つの body の相対姿勢がジョイントの原点 (ヒンジの角度 0、
///          スライダーの位置 0) になるので、body を置いた姿勢のまま渡す。返る設定の limit や motor は、足す前なら
///          書き換えてよい。ここに無い型 (SixDOF、Gear、Pulley など) は Jolt の設定をそのまま addJoint へ渡す。
///          motor の強さや limit は Jolt の保存に入らないので、組み立てで決めるか、毎フレーム GameMemory から決め直す。
///          設定は Jolt の確保で作るため、JoltGameWorld の組み立ての中で呼ぶ (world を作る前は Jolt の登録が済んでいない)。

#ifdef MITIRU_HAS_JOLT

#include <cfloat>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Constraints/ConeConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>

namespace mitiru::physics3d
{

namespace detail
{

/// @brief 長さ 0 の軸は正規化すると NaN になり、Jolt が NaN の姿勢で解き続けるので、Y 軸に置き換える。
[[nodiscard]] inline JPH::Vec3 unitAxis(JPH::Vec3Arg v) noexcept
{
	return v.LengthSq() > 1e-12f ? v.Normalized() : JPH::Vec3::sAxisY();
}

}  // namespace detail

/// @brief 2 つの body を今の相対姿勢のまま固める。
[[nodiscard]] inline JPH::Ref<JPH::FixedConstraintSettings> makeFixedJoint()
{
	JPH::Ref<JPH::FixedConstraintSettings> s = new JPH::FixedConstraintSettings;
	s->mSpace = JPH::EConstraintSpace::WorldSpace;
	s->mAutoDetectPoint = true;
	return s;
}

/// @brief anchor の 1 点で 2 つの body を留める (向きは自由)。
[[nodiscard]] inline JPH::Ref<JPH::PointConstraintSettings> makePointJoint(JPH::RVec3Arg anchor)
{
	JPH::Ref<JPH::PointConstraintSettings> s = new JPH::PointConstraintSettings;
	s->mSpace = JPH::EConstraintSpace::WorldSpace;
	s->mPoint1 = s->mPoint2 = anchor;
	return s;
}

/// @brief anchor を通る axis まわりにだけ回る。角度の範囲は [limitMin, limitMax] ラジアン (-pi〜0 と 0〜pi)。
[[nodiscard]] inline JPH::Ref<JPH::HingeConstraintSettings> makeHingeJoint(JPH::RVec3Arg anchor, JPH::Vec3Arg axis,
                                                                          float limitMin = -JPH::JPH_PI, float limitMax = JPH::JPH_PI)
{
	JPH::Ref<JPH::HingeConstraintSettings> s = new JPH::HingeConstraintSettings;
	s->mSpace = JPH::EConstraintSpace::WorldSpace;
	s->mPoint1 = s->mPoint2 = anchor;
	s->mHingeAxis1 = s->mHingeAxis2 = detail::unitAxis(axis);
	s->mNormalAxis1 = s->mNormalAxis2 = s->mHingeAxis1.GetNormalizedPerpendicular();
	s->mLimitsMin = limitMin;
	s->mLimitsMax = limitMax;
	return s;
}

/// @brief axis の方向にだけ動く。範囲は今の位置を 0 とした [limitMin, limitMax] メートル。
[[nodiscard]] inline JPH::Ref<JPH::SliderConstraintSettings> makeSliderJoint(JPH::Vec3Arg axis, float limitMin = -FLT_MAX,
                                                                            float limitMax = FLT_MAX)
{
	JPH::Ref<JPH::SliderConstraintSettings> s = new JPH::SliderConstraintSettings;
	s->mSpace = JPH::EConstraintSpace::WorldSpace;
	s->mAutoDetectPoint = true;
	s->mSliderAxis1 = s->mSliderAxis2 = detail::unitAxis(axis);
	s->mNormalAxis1 = s->mNormalAxis2 = s->mSliderAxis1.GetNormalizedPerpendicular();
	s->mLimitsMin = limitMin;
	s->mLimitsMax = limitMax;
	return s;
}

/// @brief 2 点の間の距離を [minDistance, maxDistance] に保つ (縄・棒)。負なら今の距離。
[[nodiscard]] inline JPH::Ref<JPH::DistanceConstraintSettings> makeDistanceJoint(JPH::RVec3Arg point1, JPH::RVec3Arg point2,
                                                                                float minDistance = -1.0f,
                                                                                float maxDistance = -1.0f)
{
	JPH::Ref<JPH::DistanceConstraintSettings> s = new JPH::DistanceConstraintSettings;
	s->mSpace = JPH::EConstraintSpace::WorldSpace;
	s->mPoint1 = point1;
	s->mPoint2 = point2;
	s->mMinDistance = minDistance;
	s->mMaxDistance = maxDistance;
	return s;
}

/// @brief anchor で留め、twistAxis から halfConeAngle ラジアンの円錐の中でだけ傾く (ねじりは自由)。
[[nodiscard]] inline JPH::Ref<JPH::ConeConstraintSettings> makeConeJoint(JPH::RVec3Arg anchor, JPH::Vec3Arg twistAxis,
                                                                        float halfConeAngle)
{
	JPH::Ref<JPH::ConeConstraintSettings> s = new JPH::ConeConstraintSettings;
	s->mSpace = JPH::EConstraintSpace::WorldSpace;
	s->mPoint1 = s->mPoint2 = anchor;
	s->mTwistAxis1 = s->mTwistAxis2 = detail::unitAxis(twistAxis);
	s->mHalfConeAngle = halfConeAngle;
	return s;
}

/// @brief 肩や首のような関節。twistAxis からの傾きの上限(円錐) と、軸まわりのねじりの範囲 [twistMin, twistMax] (ラジアン)。
[[nodiscard]] inline JPH::Ref<JPH::SwingTwistConstraintSettings> makeSwingTwistJoint(JPH::RVec3Arg anchor, JPH::Vec3Arg twistAxis,
                                                                                    float halfConeAngle, float twistMin,
                                                                                    float twistMax)
{
	JPH::Ref<JPH::SwingTwistConstraintSettings> s = new JPH::SwingTwistConstraintSettings;
	s->mSpace = JPH::EConstraintSpace::WorldSpace;
	s->mPosition1 = s->mPosition2 = anchor;
	s->mTwistAxis1 = s->mTwistAxis2 = detail::unitAxis(twistAxis);
	s->mPlaneAxis1 = s->mPlaneAxis2 = s->mTwistAxis1.GetNormalizedPerpendicular();
	s->mNormalHalfConeAngle = s->mPlaneHalfConeAngle = halfConeAngle;
	s->mTwistMinAngle = twistMin;
	s->mTwistMaxAngle = twistMax;
	return s;
}

}  // namespace mitiru::physics3d

#endif  // MITIRU_HAS_JOLT
