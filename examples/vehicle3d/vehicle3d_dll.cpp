// vehicle3d。「車輪のある車を走らせる」章 (Jolt の vehicle)
// 実行すると: 市松模様の広場に車が止まっている。W / S で走り、A / D で曲がり、Space でサイドブレーキ。段差を越えられる
// 関連 API: JoltVehicle / makeDefaultCarSpec / JoltGameWorld / MITIRU_SIDE_STATE (物理の世界ごと巻き戻す)

#include <cmath>
#include <cstdio>
#include <mitiru.hpp>
#include <mitiru/module/SideState.hpp>
#include <mitiru/physics/JoltVehicle.hpp>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include "../common/chapter_hud.hpp"  // 章ラベル + 操作帯 (全章共通の書式)

using namespace mitiru;
namespace phys = mitiru::physics3d;

constexpr int   kWheels = 4;
constexpr float kBumpZ[] = {40.0f, 56.0f, 72.0f};   // 段差の位置 (幅 12 m、高さ 0.2 m)

phys::JoltVehicle g_car;

void buildWorld(phys::JoltGameWorld& world, const void* /*state*/)
{
	using phys::JoltGameWorld;
	// 車輪の食いつきは相手の friction で決まる (既定の 0.2 では空転する)ので、走る面は 1 にする
	JPH::BodyCreationSettings ground(new JPH::BoxShape(JPH::Vec3(300.0f, 0.5f, 300.0f)), JPH::RVec3(0, -0.5f, 0),
		JPH::Quat::sIdentity(), JPH::EMotionType::Static, JoltGameWorld::kStaticLayer);
	ground.mFriction = 1.0f;
	world.addBody(ground, JPH::EActivation::DontActivate);
	for (const float z : kBumpZ)
	{
		JPH::BodyCreationSettings bump(new JPH::BoxShape(JPH::Vec3(6.0f, 0.1f, 0.6f)), JPH::RVec3(0, 0.1f, z),
			JPH::Quat::sIdentity(), JPH::EMotionType::Static, JoltGameWorld::kStaticLayer);
		bump.mFriction = 1.0f;
		world.addBody(bump, JPH::EActivation::DontActivate);
	}
	g_car.build(world, phys::makeDefaultCarSpec(), JPH::RVec3(0, 1.0f, 0));
}

// 物理の世界は状態 (下の struct) の外。窓口として預けると、巻き戻し・セーブのたびに状態と一緒に戻してもらえる。
phys::JoltGameWorld g_world(&buildWorld);
MITIRU_SIDE_STATE("jolt", 1, g_world);

// 描くための姿勢。update が物理の世界から写し、draw は物理の世界に触らない。
struct Pose { sgc::Vec3f pos, rotDeg; };

struct Vehicle3D
{
	Pose body{}, cabin{}, nose{};
	phys::JoltVehicle::WheelPose wheels[kWheels]{};
	sgc::Vec3f eye{0.0f, 4.0f, -9.0f}, look{0.0f, 1.0f, 3.0f};
	float headX = 0.0f, headZ = 1.0f;   // カメラが向く水平の向き。車の向きにゆっくり追いつく
	float speed = 0.0f;                 // 前向きの速さ (m/s)。後ろ向きは負
	int   gear = 0;                     // 変速機のギア (-1 が後退)

	void init() { g_world.reset(); *this = Vehicle3D{}; }

	void update(Input in, Hud hud, float dt)
	{
		g_world.ensureBuilt(this);
		if (in.pressed(Key::R)) { hud.requestRestart(); }
		const auto key = [&in](Key a, Key b) { return in.down(a) || in.down(b); };
		const Stick stick = in.pad(0).leftStick();
		float forward = (key(Key::W, Key::Up) ? 1.0f : 0.0f) - (key(Key::S, Key::Down) ? 1.0f : 0.0f);
		const float right = (key(Key::D, Key::Right) ? 1.0f : 0.0f) - (key(Key::A, Key::Left) ? 1.0f : 0.0f) + stick.x;
		float brake = 0.0f;
		if (forward * speed < -1.0f) { brake = 1.0f; forward = 0.0f; }   // 走っている向きと逆に踏んだら、後退でなくブレーキ
		g_car.drive(g_world, forward, right, brake, in.down(Key::Space) ? 1.0f : 0.0f);
		g_world.step(dt);

		const JPH::Quat q = g_world.bodies().GetRotation(g_car.body());
		const auto base = g_car.bodyPose(g_world);
		const auto at = [&](float x, float y, float z) {
			const JPH::Vec3 o = q * JPH::Vec3(x, y, z);
			return Pose{{base.pos.x + o.GetX(), base.pos.y + o.GetY(), base.pos.z + o.GetZ()}, base.rotDeg};
		};
		body = {base.pos, base.rotDeg};
		cabin = at(0.0f, 0.45f, -0.3f);
		nose = at(0.0f, 0.0f, 2.0f);
		for (int i = 0; i < kWheels; ++i) { wheels[i] = g_car.wheelPose(i); }
		speed = g_car.forwardSpeed(g_world);
		gear = g_car.controller()->GetTransmission().GetCurrentGear();

		const JPH::Vec3 fwd = q * JPH::Vec3::sAxisZ();
		const float len = std::sqrt(fwd.GetX() * fwd.GetX() + fwd.GetZ() * fwd.GetZ());
		if (len > 0.1f)   // 前が真上か真下を向いた (横転した) 間は、カメラの向きを変えない
		{
			const float k = 1.0f - std::exp(-2.5f * dt);
			const float hx = headX + (fwd.GetX() / len - headX) * k;
			const float hz = headZ + (fwd.GetZ() / len - headZ) * k;
			const float hl = std::sqrt(hx * hx + hz * hz);
			if (hl > 1e-4f) { headX = hx / hl; headZ = hz / hl; }
		}
		eye = {base.pos.x - headX * 9.0f, base.pos.y + 4.2f, base.pos.z - headZ * 9.0f};
		look = {base.pos.x + headX * 3.0f, base.pos.y + 0.8f, base.pos.z + headZ * 3.0f};
	}

	void draw(Screen& s) const
	{
		s.clear(theme::kPaper);
		s.camera3D(eye, look, Deg{55.0f});
		s.light3D({-0.5f, -0.9f, -0.4f}, hex(0xFFFBF2));
		s.skybox3D(hex(0x63A5E8), hex(0xEAF3FB));
		s.drawMesh("cube", {0.0f, -0.5f, 0.0f}, {600.0f, 1.0f, 600.0f}, {0, 0, 0}, hex(0xD5DBE5));
		// 市松模様。車の近くだけ、8 m の升を世界の座標に固定して描く (動くと流れて、速さが見える)
		const int cx = static_cast<int>(std::floor(body.pos.x / 8.0f)), cz = static_cast<int>(std::floor(body.pos.z / 8.0f));
		for (int i = -9; i <= 9; ++i)
			for (int j = -9; j <= 9; ++j)
				if (((cx + i + cz + j) & 1) == 0)
					s.drawMesh("cube", {(cx + i + 0.5f) * 8.0f, 0.01f, (cz + j + 0.5f) * 8.0f}, {8.0f, 0.02f, 8.0f}, {0, 0, 0}, hex(0xB9C3D2));
		for (const float z : kBumpZ) { s.drawMesh("cube", {0.0f, 0.1f, z}, {12.0f, 0.2f, 1.2f}, {0, 0, 0}, theme::kAmber); }

		s.drawMesh("cube", body.pos, {1.8f, 0.4f, 4.0f}, body.rotDeg, theme::kBlue);
		s.drawMesh("cube", cabin.pos, {1.4f, 0.5f, 2.0f}, cabin.rotDeg, hex(0x7FB8FF));
		s.drawMesh("cube", nose.pos, {1.6f, 0.3f, 0.1f}, nose.rotDeg, theme::kOrange);   // 前がどちらか分かる印
		for (const auto& w : wheels)
		{
			s.drawMesh("sphere", w.pos, {2.0f * w.radius, w.width, 2.0f * w.radius}, w.rotDeg, theme::kInk);
			s.drawMesh("cube", w.pos, {1.8f * w.radius, w.width * 1.5f, 0.08f}, w.rotDeg, theme::kPaper);   // 回っているのが見える印
		}

		char text[48];
		std::snprintf(text, sizeof(text), "%d km/h　ギア %d", static_cast<int>(std::fabs(speed) * 3.6f), gear);
		const Rect badge{1280.0f - 300.0f, 14.0f, 284.0f, 36.0f};
		s.drawRect(badge, rgba(226, 231, 240, 215));
		s.drawTextInRect(badge, text, theme::kInk, 18.0f, Screen::TextAlignH::Center, Screen::TextAlignV::Middle);
		chapterTitle(s, "車");
		chapterControls(s, "W S: アクセルとブレーキ　A D: ハンドル　Space: サイドブレーキ　R: さいしょから");
	}
};

MITIRU_ASSERT_NO_PADDING(Vehicle3D);
MITIRU_REFLECT(Vehicle3D, speed, gear);
MITIRU_GAME(Vehicle3D);
