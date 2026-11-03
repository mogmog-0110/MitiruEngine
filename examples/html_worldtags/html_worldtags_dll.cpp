// html_worldtags。敵の頭上に HP バーが付いてきて、当てるとダメージの数字が浮かぶ例。
//   4 体の敵 (立方体) が円を描いて歩く。Space を押すたびに 1 体ずつ順に当たって HP が減り、数字が出る。
//   バーと数字の位置は C++ が pick3d::worldToScreen で画面の座標にして UI へ送る。UI (RML) はその座標に置くだけで、
//   数字の上へ昇る動きと薄れ方も、C++ が送る経過 (0..1) から作る。
// この章で使う仕組み: ui::WorldTagFrame / ui::FloatingNumbers (mitiru/ui/WorldTags.hpp) / pick3d::worldToScreen / hud.set
#include <cmath>
#include <mitiru.hpp>
#include <mitiru/module/AutoReflect.hpp>
#include <mitiru/render/Picking3D.hpp>
#include <mitiru/ui/WorldTags.hpp>
#include "../common/chapter_hud.hpp"   // 章ラベル + 操作帯 (全章共通の書式)
using namespace mitiru;

namespace
{

constexpr int kEnemyCount = 4;
constexpr float kRadius = 3.2f;       // 敵が歩く円の半径 (m)
constexpr float kHeadHeight = 1.7f;   // 立方体の上、バーを置く高さ
const pick3d::CameraView kCamera{{0.0f, 6.0f, 9.5f}, {0.0f, 0.6f, 0.0f}, 50.0f};

sgc::Vec3f enemyPos(float angle) { return {kRadius * std::cos(angle), 0.0f, kRadius * std::sin(angle)}; }

} // namespace

struct Enemy
{
	float angle = 0.0f;
	float hp = 100.0f;
	float speed = 0.4f;
};

struct HtmlWorldTags
{
	Enemy enemies[kEnemyCount] = {};
	ui::FloatingNumbers<16> numbers;   // 浮かぶ数字。満杯なら一番古い物から入れ替わる
	int hits = 0;                      // 当てた回数 (誰に当てるかと、ダメージの大きさを決める)

	void init()
	{
		*this = HtmlWorldTags{};
		for (int i = 0; i < kEnemyCount; ++i)
		{
			enemies[i].angle = 1.5707963f * static_cast<float>(i);
			enemies[i].speed = 0.35f + 0.1f * static_cast<float>(i);
		}
	}

	void update(Input in, Hud hud, float dt)
	{
		if (in.pressed(Key::Escape)) { hud.quit(); }
		for (Enemy& e : enemies) { e.angle += e.speed * dt; }

		if (in.pressed(Key::Space) || in.padPressed(Pad::A))
		{
			Enemy& target = enemies[hits % kEnemyCount];
			const int crit = (hits % 5 == 4) ? 1 : 0;
			const int damage = (8 + (hits * 13) % 17) * (crit ? 2 : 1);
			target.hp = target.hp - static_cast<float>(damage) > 0.0f ? target.hp - static_cast<float>(damage) : 100.0f;
			const sgc::Vec3f at = enemyPos(target.angle);
			numbers.spawn({at.x, kHeadHeight, at.z}, damage, crit);
			++hits;
		}
		numbers.update(dt);

		ui::WorldTagFrame tags(kCamera);
		for (int i = 0; i < kEnemyCount; ++i)
		{
			const sgc::Vec3f at = enemyPos(enemies[i].angle);
			tags.add(i, {at.x, kHeadHeight, at.z}, enemies[i].hp / 100.0f);
		}
		tags.push(hud, "tags");
		numbers.push(hud, "damage", kCamera);

		hud.set("view.tag_count", static_cast<int>(tags.size()));
		hud.set("view.number_count", numbers.active());
		hud.set("view.hits", hits);
	}

	void draw(Screen& s) const
	{
		s.clear(hex(0xE6EEF4));   // 3D が使えない環境ではこの色のまま
		s.camera3D(kCamera.eye, kCamera.target, Deg{kCamera.fovDeg});
		s.light3D({-0.5f, -0.9f, -0.35f}, hex(0xFFF8EC));
		s.skybox3D(hex(0xA9CDEB), hex(0xF1EDE4));
		s.drawMesh("cube", {0.0f, -0.05f, 0.0f}, {12.0f, 0.1f, 12.0f}, {}, hex(0xC9D3C0));
		for (const Enemy& e : enemies)
		{
			const sgc::Vec3f at = enemyPos(e.angle);
			s.drawMesh("cube", {at.x, 0.8f, at.z}, {1.0f, 1.6f, 1.0f}, {}, hex(0xC8423A));
		}
		chapterTitle(s, "ワールド追従");
		chapterControls(s, "Space / A: 敵に当てる　頭上のバーと数字は UI (RML) が C++ の座標に置く　Esc: おわる");
	}
};

// 実行:  mitiru_host.exe html_worldtags/html_worldtags.dll
MITIRU_REFLECT_AUTO(HtmlWorldTags);

MITIRU_ASSERT_NO_PADDING(HtmlWorldTags);
MITIRU_GAME(HtmlWorldTags);
