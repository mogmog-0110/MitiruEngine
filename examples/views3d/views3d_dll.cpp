// views3d。「別のカメラの絵」の見本 (副ビュー: 小さな地図と監視カメラのモニター)
//   view3D で番号 (slot) を作り、beginView3D と endView3D の間に描いた物がその slot の絵になる。主画面の絵は変わらない。
//   地図 (slot 0) は UI の assets/ui/main.rml が <img src="view3d:0"/> で右上に貼り、監視カメラ (slot 1) の絵は
//   部屋の奥のモニター (drawMeshWithView3D) に映す。同じ作りを毎フレーム渡しても作り直さない。
// あそびかた: 矢印で橙の箱が歩く。右上の地図は真上から、奥のモニターは部屋の隅のカメラからキャラを追う。
// この章で使う関数: view3D / beginView3D / endView3D / drawMeshWithView3D / RML の <img src="view3d:N"/>

#include <algorithm>
#include <cmath>
#include <mitiru.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;

struct Box { Vec3 center, size; std::uint32_t col; };
constexpr Box kRoom[] = {
	{{0, -0.1f, 0}, {14, 0.2f, 14}, 0xC9D1DC},                              // 床
	{{0, 1.5f, -7}, {14, 3, 0.2f}, 0x8C9BB0},                                // 奥の壁
	{{-3, 0.6f, -2}, {1.2f, 1.2f, 1.2f}, 0x4C73A0}, {{3.5f, 0.9f, 1.5f}, {1, 1.8f, 1}, 0x6F8F7A},
	{{-4, 0.4f, 3.5f}, {2, 0.8f, 1}, 0xB7A27F}, {{2, 0.5f, -4.5f}, {1, 1, 1}, 0xE8338A},
};
constexpr Vec3 kMonitor{0.0f, 1.8f, -6.85f};   // 奥の壁に掛けたモニター
constexpr Vec3 kCctv{6.5f, 3.0f, 6.5f};        // 監視カメラは部屋の右手前の隅

struct Views3D
{
	Vec3 hero{0.0f, 0.0f, 2.0f};
	Vec3 facing{0.0f, 0.0f, -1.0f};

	void update(Input in, float dt)
	{
		const Stick m = in.move();   // move の y は下が正 (画面の手前へ)
		const Vec3 step{m.x, 0.0f, m.y};
		if (step.x * step.x + step.z * step.z > 0.01f) { facing = step.normalized(); }
		hero.x = std::clamp(hero.x + step.x * 4.0f * dt, -6.5f, 6.5f);
		hero.z = std::clamp(hero.z + step.z * 4.0f * dt, -6.0f, 6.5f);
	}

	// 主画面と 2 つの副ビューが同じ部屋を描く
	void drawRoom(Screen& s) const
	{
		for (const Box& b : kRoom) { s.drawMesh("cube", b.center, b.size, {0, 0, 0}, hex(b.col)); }
		s.drawMesh("cube", hero + Vec3{0, 0.8f, 0}, {0.6f, 1.6f, 0.6f}, {0, 0, 0}, hex(0xFF9500));
		s.drawMesh("cube", hero + Vec3{0, 1.25f, 0} + facing * 0.32f, {0.2f, 0.2f, 0.2f}, {0, 0, 0}, hex(0x1D1D1F));
	}

	void drawViews(Screen& s) const
	{
		render::View3DPod map;   // 真上からの地図。空も影も要らない
		map.width = map.height = 256;
		map.flags = 0;
		map.clear[0] = 0.08f; map.clear[1] = 0.10f; map.clear[2] = 0.16f;
		s.view3D(0, map);
		if (s.beginView3D(0, hero + Vec3{0, 16, 0}, hero, {0, 0, -1}, Deg{45.0f}, 1.0f, 60.0f))
		{
			drawRoom(s);
			s.endView3D();
		}
		render::View3DPod cctv;   // 監視カメラ。影を落とし、モニターの縦横比 (4:3) で描く
		cctv.width = 320;
		cctv.height = 240;
		s.view3D(1, cctv);
		if (s.beginView3D(1, kCctv, hero + Vec3{0, 0.8f, 0}, {0, 1, 0}, Deg{55.0f}))
		{
			drawRoom(s);
			s.endView3D();
		}
	}

	void draw(Screen& s) const
	{
		s.clear(hex(0xEAF1F8));
		s.camera3D(hero + Vec3{0, 4.5f, 7.5f}, hero + Vec3{0, 1, 0}, Deg{55.0f});
		s.light3D({-0.5f, -1.0f, -0.35f}, hex(0xFFFBF2));
		s.skybox3D(hex(0x63A5E8), hex(0xEAF3FB));
		drawViews(s);   // 副ビューは主画面と同じフレームの中で描く
		drawRoom(s);
		s.drawMesh("cube", kMonitor, {3.4f, 2.2f, 0.12f}, {0, 0, 0}, hex(0x2A2F3A));   // 枠
		s.drawMeshWithView3D("cube", kMonitor + Vec3{0, 0, 0.04f}, {3.2f, 2.0f, 0.06f}, {0, 0, 0}, hex(0xFFFFFF), 1);
		chapterTitle(s, "別のカメラの絵");
		chapterControls(s, "矢印: あるく　右上: 地図 (UI の画像)　奥: 監視カメラ");
	}
};

// 実行:  mitiru_host.exe views3d/views3d.dll
MITIRU_ASSERT_NO_PADDING(Views3D);
MITIRU_GAME(Views3D);
