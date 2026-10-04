// gi3d。動く光の GI。扉を開けると差し込む朝日と、持ち歩く松明の照り返しが部屋を毎フレーム染め直す
// 実行すると: 暗い部屋に松明が 1 本。矢印で松明を持って歩くと、赤い壁や黄色い棚の照り返しが付いてくる。
// Space で東の扉が開き、日なたの砂の照り返しが部屋の奥まで届く。G で焼いた光だけの絵と比べられる。
// 関連 API: lightingBake3D + SceneLook::indirect (kIndirectGi | kIndirectDynamicGi) / pointLight3D / light3D

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <mitiru.hpp>
#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/data/JsonDiagnostics.hpp>
#include <mitiru/render/SceneLook.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;

// 部屋の形・日の向き・空・プローブの格子は room.lighting.json に 1 回だけ書く。ビルドは同じファイルから
// mitiru_lightbake で扉を閉めた時の光を焼き (それが動く GI の最初の値と、G で比べる焼いた光になる)、
// この DLL は同じ箱を読んで描く。中身はファイルだけで決まるので、ゲームの状態ではなく DLL の static に置く
constexpr const char* kRoomPath = "gi3d/assets/room.lighting.json";
constexpr const char* kBakePath = "gi3d/assets/room.lighting.bin";

struct RoomBox { Vec3 lo, hi; Color color; bool door; };
struct Room
{
	std::vector<RoomBox> boxes;
	Vec3 sunDir{-0.75f, -0.5f, 0.2f};
	Color sun = hex(0xFFFFFF), skyZenith = hex(0x6F95C4), skyHorizon = hex(0xC9D6E2), skyGround = hex(0x5A5040);
};

Color jsonColor(const nlohmann::json& j, const char* key, Color fallback)
{
	const auto it = j.find(key);
	if (it == j.end() || !it->is_string() || it->get<std::string>().size() != 7) { return fallback; }
	return hex(static_cast<std::uint32_t>(std::stoul(it->get<std::string>().substr(1), nullptr, 16)));
}

Vec3 jsonVec3(const nlohmann::json& j, const char* key, Vec3 fallback)
{
	const auto it = j.find(key);
	if (it == j.end() || !it->is_array() || it->size() != 3) { return fallback; }
	return {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()};
}

Room readRoom()
{
	Room room;
	const auto bytes = vfs::readAsset(kRoomPath);
	std::string error;
	const auto j = bytes ? data::parseJsonText(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), kRoomPath, error)
	                     : nlohmann::json();
	if (!j.is_object()) { return room; }
	for (const auto& b : j.value("boxes", nlohmann::json::array()))
	{
		room.boxes.push_back({jsonVec3(b, "min", {}), jsonVec3(b, "max", {}), jsonColor(b, "color", hex(0xCCCCCC)), b.value("name", "") == "door"});
	}
	const auto& sun = j.value("sun", nlohmann::json::object());
	const auto& sky = j.value("sky", nlohmann::json::object());
	room.sunDir = jsonVec3(sun, "direction", room.sunDir);
	room.sun = jsonColor(sun, "color", room.sun);
	room.skyZenith = jsonColor(sky, "zenith", room.skyZenith);
	room.skyHorizon = jsonColor(sky, "horizon", room.skyHorizon);
	room.skyGround = jsonColor(sky, "ground", room.skyGround);
	return room;
}

const Room g_room = readRoom();

constexpr Vec3 kEye{-2.55f, 2.35f, 2.55f}, kTarget{1.6f, 0.75f, -0.9f};
constexpr float kRoomHalf = 2.6f;      // 松明を持って歩ける範囲 (壁の内側から 0.4 m)
constexpr float kWalkSpeed = 1.8f;
constexpr float kDoorSlide = 1.75f;    // 開いた扉が +z へずれる長さ (戸口の幅 1.6 m を空ける)

// 状態は数値だけ (まるごとコピーできる形)。GI は描画だけが読むので、巻き戻しと記録／再生はそのまま効く
struct Gi3D
{
	float torch[3] = {-1.7f, 1.35f, -1.3f};
	float doorOpen = 0.0f;         // 0 = 閉じた、1 = 開いた。1.2 秒ほどで滑る
	std::uint8_t doorWanted = 0;
	std::uint8_t bakedOnly = 0;    // 1 = 動く GI を頼まず、焼いた光だけで描く (比べる用)
	std::uint8_t _pad[2] = {};

	void update(Input in, Hud hud, float dt)
	{
		if (in.pressed(Key::Escape)) { hud.quit(); }
		if (in.pressed(Key::Space)) { doorWanted = doorWanted ? 0 : 1; }
		if (in.pressed(Key::G)) { bakedOnly = bakedOnly ? 0 : 1; }
		const float target = doorWanted ? 1.0f : 0.0f;
		const float step = dt * 0.85f;
		doorOpen += std::fmax(-step, std::fmin(step, target - doorOpen));
		// カメラから見た前後左右へ歩く (move の y は下が正)
		const Stick m = in.move();
		const Vec3 fwd = Vec3{kTarget.x - kEye.x, 0.0f, kTarget.z - kEye.z}.normalized();
		const Vec3 right{-fwd.z, 0.0f, fwd.x};
		const Vec3 walk = (right * m.x - fwd * m.y) * (kWalkSpeed * dt);
		torch[0] = std::fmax(-kRoomHalf, std::fmin(kRoomHalf, torch[0] + walk.x));
		torch[2] = std::fmax(-kRoomHalf, std::fmin(kRoomHalf, torch[2] + walk.z));
	}

	render::SceneLook look() const
	{
		render::SceneLook l;
		l.shadingModel = 3;   // PBR
		l.exposure = 1.25f;
		l.shadow = true;
		l.shadowCascaded = true;
		l.shadowDirection[0] = g_room.sunDir.x; l.shadowDirection[1] = g_room.sunDir.y; l.shadowDirection[2] = g_room.sunDir.z;
		l.ambient[0] = l.ambient[1] = l.ambient[2] = 0.04f;   // 格子の外 (戸口の外) だけで使う平らな環境光
		// 何にも当たらなかったレイが拾う空。上は天頂、下は地面の色
		l.ambientSky[0] = g_room.skyZenith.r; l.ambientSky[1] = g_room.skyZenith.g; l.ambientSky[2] = g_room.skyZenith.b;
		l.ambientGround[0] = g_room.skyGround.r; l.ambientGround[1] = g_room.skyGround.g; l.ambientGround[2] = g_room.skyGround.b;
		l.bloom = true;
		l.bloomThreshold = 1.2f;
		l.indirect.flags = render::kIndirectGi | (bakedOnly ? 0 : render::kIndirectDynamicGi);
		return l;
	}

	void drawRoom(Screen& s) const
	{
		for (const RoomBox& b : g_room.boxes)
		{
			const Vec3 shift{0.0f, 0.0f, b.door ? doorOpen * kDoorSlide : 0.0f};
			s.drawMesh("cube", (b.lo + b.hi) * 0.5f + shift, b.hi - b.lo, {0, 0, 0}, b.color);
		}
	}

	// 松明: 柄と炎の玉。光は炎の少し上に置き、玉の上側が明るく光って見えるようにする
	void drawTorch(Screen& s) const
	{
		const Vec3 p{torch[0], torch[1], torch[2]};
		s.drawMesh("cube", p - Vec3{0.0f, 0.3f, 0.0f}, {0.05f, 0.55f, 0.05f}, {12.0f, 0.0f, 8.0f}, hex(0x4A3222));
		s.drawMesh("sphere", p, {0.14f, 0.18f, 0.14f}, {0, 0, 0}, hex(0xFFD27A));
		s.pointLight3D(p + Vec3{0.0f, 0.2f, 0.0f}, 5.5f, hex(0xFFB15E), 1.6f);
	}

	void draw(Screen& s) const
	{
		s.clear(hex(0x0E1014));
		s.camera3D(kEye, kTarget, Deg{70.0f});
		s.light3D(g_room.sunDir, g_room.sun);
		s.skybox3D(g_room.skyZenith, g_room.skyHorizon);
		s.sceneLook3D(look());
		s.lightingBake3D(kBakePath);   // 毎フレーム呼んでよい。読み終わるまでは平らな環境光のまま
		drawRoom(s);
		drawTorch(s);
		chapterTitle(s, bakedOnly ? "焼いた光だけ" : "動く光の GI");
		chapterControls(s, bakedOnly ? "矢印: 松明を持って歩く　Space: 扉　G: 動く GI に戻す"
		                             : "矢印: 松明を持って歩く　Space: 扉　G: 焼いた光だけにする");
	}
};

// 実行:  mitiru_host.exe gi3d/gi3d.dll   (DXR 1.1 の無い GPU では焼いた光のまま描く)
MITIRU_ASSERT_NO_PADDING(Gi3D);
MITIRU_GAME(Gi3D);
