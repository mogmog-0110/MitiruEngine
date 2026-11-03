#pragma once

/// @file JoltVehicle.hpp
/// @brief JoltGameWorld に載せる車輪つきの車両。設定 (JSON か C++ の struct) から車体と車輪を組み立て、毎 tick の運転入力を渡し、
///        車体と車輪の姿勢を drawMesh の形で返す。
/// @details 車輪の角度・サスペンション・運転入力・エンジンの回転は Jolt の保存に入り、JoltGameWorld の窓口
///          (MITIRU_SIDE_STATE) で GameMemory と一緒に巻き戻る。エンジンのトルクやギア比などの設定は保存に入らないので、
///          組み立てで決める。組み立ては world の build の中で build を呼ぶ (world を作り直すたびに呼ばれる)。
///          座標は Jolt の body の局所で、前が +z、上が +y、左が +x。

#ifdef MITIRU_HAS_JOLT

#include <cmath>
#include <string>
#include <vector>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>

#include <nlohmann/json.hpp>

#include <mitiru/physics/JoltGameWorld.hpp>

namespace mitiru::physics3d
{

struct VehicleWheelSpec
{
	float x = 0.9f;    ///< サスペンションの取り付け点 (車体の局所、m)
	float y = -0.18f;
	float z = 1.4f;
	float radius = 0.3f;
	float width = 0.1f;
	float suspensionMin = 0.3f;         ///< 一番縮んだときの長さ (m)
	float suspensionMax = 0.5f;         ///< 一番伸びたときの長さ (m)
	float suspensionFrequency = 1.5f;   ///< ばねの固有振動数 (Hz)
	float suspensionDamping = 0.5f;
	float maxSteerDeg = 0.0f;           ///< 0 なら曲がらない車輪
	float maxBrakeTorque = 1500.0f;
	float maxHandBrakeTorque = 0.0f;    ///< 0 ならハンドブレーキが効かない車輪
};

struct WheeledVehicleSpec
{
	enum class Tester { Ray, Sphere, Cylinder };

	float halfWidth = 0.9f;
	float halfHeight = 0.2f;
	float halfLength = 2.0f;
	float mass = 1500.0f;
	float centerOfMassDrop = 0.2f;      ///< 重心を箱の中心からこの分だけ下げる (m)。転びにくくする
	float maxPitchRollDeg = 180.0f;     ///< 車体の傾きの上限。180 で制限なし
	float engineMaxTorque = 500.0f;     ///< Nm
	float engineMinRpm = 1000.0f;
	float engineMaxRpm = 6000.0f;
	std::vector<float> gearRatios{2.66f, 1.78f, 1.3f, 1.0f, 0.74f};
	std::vector<float> reverseGearRatios{-2.9f};
	float clutchStrength = 10.0f;
	float antiRollBarStiffness = 1000.0f;   ///< 0 で無し
	bool allWheelDrive = false;             ///< false なら前の 1 組 (車輪 0 と 1) だけを回す
	Tester tester = Tester::Cylinder;       ///< 車輪と地面の当たりの取り方
	/// 2 つずつ組にする (0 と 1、2 と 3 ...)。組の前が左、後ろが右。差動とアンチロールバーは組ごと
	std::vector<VehicleWheelSpec> wheels;
};

/// @brief 四輪の乗用車。前の 2 輪が曲がり、後ろの 2 輪にハンドブレーキ。
[[nodiscard]] inline WheeledVehicleSpec makeDefaultCarSpec()
{
	WheeledVehicleSpec s;
	for (int i = 0; i < 4; ++i)
	{
		VehicleWheelSpec w;
		w.x = (i % 2 == 0) ? s.halfWidth : -s.halfWidth;
		w.y = -0.9f * s.halfHeight;
		w.z = (i < 2) ? s.halfLength - 2.0f * w.radius : -s.halfLength + 2.0f * w.radius;
		w.maxSteerDeg = (i < 2) ? 30.0f : 0.0f;
		w.maxHandBrakeTorque = (i < 2) ? 0.0f : 4000.0f;
		s.wheels.push_back(w);
	}
	return s;
}

/// @brief JSON (<名前>.vehicle.json) から設定を読む。鍵は全部任意で、無い鍵は既定の乗用車の値。
/// @details { "body": {"halfExtents": [w, h, l], "mass", "centerOfMassDrop", "maxPitchRollDeg"},
///            "engine": {"maxTorque", "minRpm", "maxRpm"}, "transmission": {"gearRatios": [], "reverseGearRatios": [], "clutchStrength"},
///            "drive": "front" | "all", "antiRollBarStiffness", "tester": "ray" | "sphere" | "cylinder",
///            "wheels": [{"at": [x, y, z], "radius", "width", "suspension": {"min", "max", "frequency", "damping"},
///                        "maxSteerDeg", "brakeTorque", "handBrakeTorque"}] }
/// @return 読めない値があれば false。error にその場所
[[nodiscard]] inline bool readVehicleSpec(const nlohmann::json& j, WheeledVehicleSpec& out, std::string& error)
{
	using Json = nlohmann::json;
	out = makeDefaultCarSpec();
	error.clear();
	if (!j.is_object()) { error = "vehicle: not an object"; return false; }
	const auto num = [&error](const Json& o, const char* key, float& v, const char* where) {
		if (!o.is_object() || !o.contains(key)) { return true; }
		if (!o[key].is_number()) { error = std::string(where) + "." + key + ": not a number"; return false; }
		v = o[key].get<float>();
		return true;
	};
	const auto nums = [&error](const Json& o, const char* key, std::vector<float>& v, const char* where) {
		if (!o.is_object() || !o.contains(key)) { return true; }
		if (!o[key].is_array() || o[key].empty()) { error = std::string(where) + "." + key + ": not an array of numbers"; return false; }
		std::vector<float> read;
		for (const Json& e : o[key])
		{
			if (!e.is_number()) { error = std::string(where) + "." + key + ": not an array of numbers"; return false; }
			read.push_back(e.get<float>());
		}
		v = std::move(read);
		return true;
	};
	const Json body = j.value("body", Json::object());
	if (body.contains("halfExtents"))
	{
		std::vector<float> he;
		if (!nums(body, "halfExtents", he, "body") || he.size() != 3) { error = "body.halfExtents: needs 3 numbers"; return false; }
		out.halfWidth = he[0]; out.halfHeight = he[1]; out.halfLength = he[2];
	}
	if (!num(body, "mass", out.mass, "body") || !num(body, "centerOfMassDrop", out.centerOfMassDrop, "body")
	    || !num(body, "maxPitchRollDeg", out.maxPitchRollDeg, "body"))
	{
		return false;
	}
	const Json engine = j.value("engine", Json::object());
	if (!num(engine, "maxTorque", out.engineMaxTorque, "engine") || !num(engine, "minRpm", out.engineMinRpm, "engine")
	    || !num(engine, "maxRpm", out.engineMaxRpm, "engine"))
	{
		return false;
	}
	const Json gear = j.value("transmission", Json::object());
	if (!nums(gear, "gearRatios", out.gearRatios, "transmission") || !nums(gear, "reverseGearRatios", out.reverseGearRatios, "transmission")
	    || !num(gear, "clutchStrength", out.clutchStrength, "transmission"))
	{
		return false;
	}
	if (!num(j, "antiRollBarStiffness", out.antiRollBarStiffness, "vehicle")) { return false; }
	const std::string drive = j.value("drive", std::string("front"));
	if (drive != "front" && drive != "all") { error = "drive: use \"front\" or \"all\""; return false; }
	out.allWheelDrive = drive == "all";
	const std::string tester = j.value("tester", std::string("cylinder"));
	if (tester == "ray") { out.tester = WheeledVehicleSpec::Tester::Ray; }
	else if (tester == "sphere") { out.tester = WheeledVehicleSpec::Tester::Sphere; }
	else if (tester == "cylinder") { out.tester = WheeledVehicleSpec::Tester::Cylinder; }
	else { error = "tester: use \"ray\", \"sphere\" or \"cylinder\""; return false; }
	if (!j.contains("wheels")) { return true; }
	if (!j["wheels"].is_array() || j["wheels"].size() < 2) { error = "wheels: needs an array of 2 or more wheels"; return false; }
	out.wheels.clear();
	int index = 0;
	for (const Json& wj : j["wheels"])
	{
		const std::string where = "wheels[" + std::to_string(index++) + "]";
		VehicleWheelSpec w;
		std::vector<float> at;
		if (!nums(wj, "at", at, where.c_str())) { return false; }
		if (at.size() != 3) { error = where + ".at: needs 3 numbers"; return false; }
		w.x = at[0]; w.y = at[1]; w.z = at[2];
		const Json sus = wj.is_object() ? wj.value("suspension", Json::object()) : Json::object();
		const std::string susWhere = where + ".suspension";
		if (!num(wj, "radius", w.radius, where.c_str()) || !num(wj, "width", w.width, where.c_str())
		    || !num(sus, "min", w.suspensionMin, susWhere.c_str()) || !num(sus, "max", w.suspensionMax, susWhere.c_str())
		    || !num(sus, "frequency", w.suspensionFrequency, susWhere.c_str()) || !num(sus, "damping", w.suspensionDamping, susWhere.c_str())
		    || !num(wj, "maxSteerDeg", w.maxSteerDeg, where.c_str()) || !num(wj, "brakeTorque", w.maxBrakeTorque, where.c_str())
		    || !num(wj, "handBrakeTorque", w.maxHandBrakeTorque, where.c_str()))
		{
			return false;
		}
		out.wheels.push_back(w);
	}
	return true;
}

class JoltVehicle
{
public:
	struct Pose
	{
		sgc::Vec3f pos;
		sgc::Vec3f rotDeg;
	};

	struct WheelPose
	{
		sgc::Vec3f pos;
		sgc::Vec3f rotDeg;   ///< y を車軸にした円盤を置く向き。車輪の回転と舵を含む
		float radius = 0.0f;
		float width = 0.0f;
	};

	/// @brief 車体の body を作り、車両の拘束を付ける。world の組み立ての中でだけ呼ぶ。
	/// @return 足せなければ false (JoltGameWorld::addVehicle が断った)
	bool build(JoltGameWorld& world, const WheeledVehicleSpec& spec, JPH::RVec3Arg position, JPH::QuatArg rotation = JPH::Quat::sIdentity())
	{
		constexpr float kDeg = 3.14159265358979f / 180.0f;
		m_constraint = nullptr;
		m_radius.clear();
		m_width.clear();
		JPH::RefConst<JPH::Shape> shape = JPH::OffsetCenterOfMassShapeSettings(JPH::Vec3(0.0f, -spec.centerOfMassDrop, 0.0f),
			new JPH::BoxShape(JPH::Vec3(spec.halfWidth, spec.halfHeight, spec.halfLength))).Create().Get();
		JPH::BodyCreationSettings bodySettings(shape, position, rotation, JPH::EMotionType::Dynamic, JoltGameWorld::kMovingLayer);
		bodySettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
		bodySettings.mMassPropertiesOverride.mMass = spec.mass;
		m_body = world.addBody(bodySettings);

		JPH::VehicleConstraintSettings settings;
		settings.mMaxPitchRollAngle = spec.maxPitchRollDeg * kDeg;
		for (const VehicleWheelSpec& ws : spec.wheels)
		{
			JPH::WheelSettingsWV* w = new JPH::WheelSettingsWV;
			w->mPosition = JPH::Vec3(ws.x, ws.y, ws.z);
			w->mSuspensionMinLength = ws.suspensionMin;
			w->mSuspensionMaxLength = ws.suspensionMax;
			w->mSuspensionSpring.mFrequency = ws.suspensionFrequency;
			w->mSuspensionSpring.mDamping = ws.suspensionDamping;
			w->mMaxSteerAngle = ws.maxSteerDeg * kDeg;
			w->mMaxBrakeTorque = ws.maxBrakeTorque;
			w->mMaxHandBrakeTorque = ws.maxHandBrakeTorque;
			w->mRadius = ws.radius;
			w->mWidth = ws.width;
			settings.mWheels.push_back(w);
			m_radius.push_back(ws.radius);
			m_width.push_back(ws.width);
		}
		const int pairs = static_cast<int>(spec.wheels.size() / 2);
		JPH::WheeledVehicleControllerSettings* controller = new JPH::WheeledVehicleControllerSettings;
		controller->mEngine.mMaxTorque = spec.engineMaxTorque;
		controller->mEngine.mMinRPM = spec.engineMinRpm;
		controller->mEngine.mMaxRPM = spec.engineMaxRpm;
		controller->mTransmission.mGearRatios.clear();
		for (const float g : spec.gearRatios) { controller->mTransmission.mGearRatios.push_back(g); }
		controller->mTransmission.mReverseGearRatios.clear();
		for (const float g : spec.reverseGearRatios) { controller->mTransmission.mReverseGearRatios.push_back(g); }
		controller->mTransmission.mClutchStrength = spec.clutchStrength;
		const int driven = spec.allWheelDrive ? pairs : (pairs > 0 ? 1 : 0);
		controller->mDifferentials.resize(static_cast<std::size_t>(driven));
		for (int i = 0; i < driven; ++i)
		{
			controller->mDifferentials[static_cast<std::size_t>(i)].mLeftWheel = 2 * i;
			controller->mDifferentials[static_cast<std::size_t>(i)].mRightWheel = 2 * i + 1;
			controller->mDifferentials[static_cast<std::size_t>(i)].mEngineTorqueRatio = 1.0f / static_cast<float>(driven);
		}
		settings.mController = controller;
		if (spec.antiRollBarStiffness > 0.0f)
		{
			settings.mAntiRollBars.resize(static_cast<std::size_t>(pairs));
			for (int i = 0; i < pairs; ++i)
			{
				settings.mAntiRollBars[static_cast<std::size_t>(i)].mLeftWheel = 2 * i;
				settings.mAntiRollBars[static_cast<std::size_t>(i)].mRightWheel = 2 * i + 1;
				settings.mAntiRollBars[static_cast<std::size_t>(i)].mStiffness = spec.antiRollBarStiffness;
			}
		}
		JPH::Ref<JPH::VehicleCollisionTester> tester;
		switch (spec.tester)
		{
		case WheeledVehicleSpec::Tester::Ray: tester = new JPH::VehicleCollisionTesterRay(JoltGameWorld::kMovingLayer); break;
		case WheeledVehicleSpec::Tester::Sphere:
			tester = new JPH::VehicleCollisionTesterCastSphere(JoltGameWorld::kMovingLayer, 0.5f * (spec.wheels.empty() ? 0.1f : spec.wheels.front().width));
			break;
		default: tester = new JPH::VehicleCollisionTesterCastCylinder(JoltGameWorld::kMovingLayer); break;
		}
		m_constraint = world.addVehicle(settings, m_body, tester);
		return m_constraint != nullptr;
	}

	/// @brief 運転の入力を渡す。毎 tick、step の前に GameMemory から呼ぶ。forward と right は -1〜1 (right は右が正)、
	///        brake と handBrake は 0〜1。どれかが 0 でなければ、眠っている車体を起こす。
	void drive(JoltGameWorld& world, float forward, float right, float brake, float handBrake)
	{
		if (m_constraint == nullptr) { return; }
		if (forward != 0.0f || right != 0.0f || brake != 0.0f || handBrake != 0.0f) { world.bodies().ActivateBody(m_body); }
		controller()->SetDriverInput(forward, right, brake, handBrake);
	}

	[[nodiscard]] bool built() const noexcept { return m_constraint != nullptr; }
	[[nodiscard]] JPH::BodyID body() const noexcept { return m_body; }
	[[nodiscard]] JPH::VehicleConstraint* constraint() const noexcept { return m_constraint; }
	[[nodiscard]] JPH::WheeledVehicleController* controller() const
	{
		return static_cast<JPH::WheeledVehicleController*>(m_constraint->GetController());
	}
	[[nodiscard]] int wheelCount() const noexcept { return static_cast<int>(m_radius.size()); }

	/// @brief 車体の箱の中心と向き (重心ではなく、形の原点)。
	[[nodiscard]] Pose bodyPose(JoltGameWorld& world) const
	{
		const JPH::RVec3 p = world.bodies().GetPosition(m_body);
		return {{float(p.GetX()), float(p.GetY()), float(p.GetZ())}, drawMeshRotationDeg(world.bodies().GetRotation(m_body))};
	}

	/// @brief 車輪 i を、y を車軸にした円盤として置く姿勢。drawMesh("sphere", pos, {2r, width, 2r}, rotDeg) で描ける。
	[[nodiscard]] WheelPose wheelPose(int i) const
	{
		if (m_constraint == nullptr || i < 0 || i >= wheelCount()) { return {}; }
		const JPH::RMat44 t = m_constraint->GetWheelWorldTransform(static_cast<JPH::uint>(i), JPH::Vec3::sAxisY(), JPH::Vec3::sAxisX());
		const JPH::RVec3 p = t.GetTranslation();
		return {{float(p.GetX()), float(p.GetY()), float(p.GetZ())}, drawMeshRotationDeg(t.GetRotation().GetQuaternion()),
		        m_radius[static_cast<std::size_t>(i)], m_width[static_cast<std::size_t>(i)]};
	}

	/// @brief 前向きの速さ (m/s)。後ろ向きは負
	[[nodiscard]] float forwardSpeed(JoltGameWorld& world) const
	{
		if (m_constraint == nullptr) { return 0.0f; }
		const JPH::Vec3 v = world.bodies().GetLinearVelocity(m_body);
		return v.Dot(world.bodies().GetRotation(m_body) * m_constraint->GetLocalForward());
	}

	[[nodiscard]] float wheelAngle(int i) const
	{
		return m_constraint != nullptr && i >= 0 && i < wheelCount() ? m_constraint->GetWheel(static_cast<JPH::uint>(i))->GetRotationAngle() : 0.0f;
	}

	[[nodiscard]] bool wheelOnGround(int i) const
	{
		return m_constraint != nullptr && i >= 0 && i < wheelCount() && m_constraint->GetWheel(static_cast<JPH::uint>(i))->HasContact();
	}

private:
	JPH::VehicleConstraint* m_constraint = nullptr;
	JPH::BodyID             m_body;
	std::vector<float>      m_radius;
	std::vector<float>      m_width;
};

}  // namespace mitiru::physics3d

#endif  // MITIRU_HAS_JOLT
