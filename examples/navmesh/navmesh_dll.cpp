// navmesh。「壁を回り込んで目的地へ歩く」章 (ナビメッシュの経路探索)
//   レベル (assets/level.obj) はビルドの一工程で mitiru_navbake がナビメッシュ (level.navmesh) を生成する。
//   ゲームはそれを init で 1 度読み込み、目的地が決まるたびに経路を問い合わせる。
//   描画と生成で同じ .obj を読むため、見えている壁と避ける壁はずれない。
// 遊び方: 床をクリックするとそこへ歩く。放っておくと 4 隅を巡回する。
// この章で使う関数: nav::NavMesh (loadFile / findPath) / drawModel / pick3d::screenToRay

#include <cmath>
#include <mitiru.hpp>
#include <mitiru/module/AutoReflect.hpp>
#include <mitiru/nav/NavMesh.hpp>
#include <mitiru/render/Picking3D.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;

namespace
{

constexpr int kMaxPath = 16;
constexpr float kSpeed = 3.0f;   // m/s
constexpr float kPatrol[4][2] = {{-6.5f, -6.0f}, {6.5f, 6.5f}, {6.5f, -7.0f}, {-6.5f, 6.5f}};
const pick3d::CameraView kCamera{{0.0f, 15.0f, 10.5f}, {0.0f, 0.0f, 0.5f}, 50.0f};

// 生成したナビメッシュは読み取り専用のレベルデータで、中身はポインタを含む。GameMemory には入れず、
// DLL の static に置き、DLL が読み込まれるたびに最初の 1 回だけファイルから読み込む。
nav::NavMesh& levelNav()
{
	static nav::NavMesh nav;
	static const bool loaded = nav.loadFile("navmesh/assets/level.navmesh");
	(void)loaded;
	return nav;
}

} // namespace

// 状態は数値だけで、agent の位置と、問い合わせた経路の角からなる。巻き戻しても経路は同じ数値のまま戻る。
struct NavmeshChapter
{
	float ax = kPatrol[0][0], az = kPatrol[0][1];   // agent の位置 (床の上、y=0)
	float pathX[kMaxPath] = {}, pathZ[kMaxPath] = {};
	int pathCount = 0;   // 経路の角の数 (0 = 次の目的地を決める)
	int pathAt = 0;      // 次に向かう角
	int patrol = 0;      // 巡回の何番目へ向かっているか

	void init() { *this = NavmeshChapter{}; (void)levelNav(); }

	void walkTo(float gx, float gz)
	{
		sgc::Vec3f pts[kMaxPath];
		const auto r = levelNav().findPath({ax, 0.0f, az}, {gx, 0.0f, gz}, pts);
		pathCount = r.pointCount;
		pathAt = 1;   // 0 番は今いる所
		for (int i = 0; i < pathCount; ++i) { pathX[i] = pts[i].x; pathZ[i] = pts[i].z; }
	}

	void update(Input in, Hud hud, float dt)
	{
		if (in.pressed(Key::Escape)) { hud.quit(); }
		sgc::Vec3f hit;
		if (in.mousePressed(0) && pick3d::rayPlaneY(pick3d::screenToRay(kCamera, in.mouseX(), in.mouseY()), 0.0f, hit))
		{
			walkTo(hit.x, hit.z);
		}
		if (pathAt >= pathCount)
		{
			patrol = (patrol + 1) % 4;
			walkTo(kPatrol[patrol][0], kPatrol[patrol][1]);
			if (pathCount == 0) { return; }   // ナビメッシュが無い (焼けていない) なら立ち止まる
		}
		float step = kSpeed * dt;
		while (step > 0.0f && pathAt < pathCount)
		{
			const float dx = pathX[pathAt] - ax, dz = pathZ[pathAt] - az;
			const float d = std::sqrt(dx * dx + dz * dz);
			if (d <= step) { ax = pathX[pathAt]; az = pathZ[pathAt]; step -= d; ++pathAt; continue; }
			ax += dx / d * step; az += dz / d * step;
			step = 0.0f;
		}
	}

	void draw(Screen& s) const
	{
		s.clear(hex(0xE6EEF4));   // 3D が使えない環境ではこの色のまま
		s.camera3D(kCamera.eye, kCamera.target, kCamera.fovDeg);
		s.light3D({-0.5f, -0.9f, -0.35f}, hex(0xFFF8EC));
		s.skybox3D(hex(0xA9CDEB), hex(0xF1EDE4));
		s.drawModel("navmesh/assets/level.obj", {0.0f, 0.0f, 0.0f});

		// 残りの経路では、角に大きな玉、その間に 0.5m おきの小さな玉を置く
		sgc::Vec3f from{ax, 0.15f, az};
		for (int i = pathAt; i < pathCount; ++i)
		{
			const sgc::Vec3f to{pathX[i], 0.15f, pathZ[i]};
			const float len = std::sqrt((to.x - from.x) * (to.x - from.x) + (to.z - from.z) * (to.z - from.z));
			for (float t = 0.5f; t < len; t += 0.5f)
			{
				const float k = t / len;
				s.drawMesh("sphere", {from.x + (to.x - from.x) * k, 0.1f, from.z + (to.z - from.z) * k},
				           {0.12f, 0.12f, 0.12f}, {}, hex(0xF0B030));
			}
			s.drawMesh("sphere", to, {0.3f, 0.3f, 0.3f}, {}, hex(0xF08020));
			from = to;
		}
		s.drawMesh("cube", {ax, 0.45f, az}, {0.7f, 0.9f, 0.7f}, {}, hex(0xC8423A));

		chapterTitle(s, "Navmesh");
		chapterControls(s, "クリック: そこへ歩く　Esc: おわる");
	}
};

// 実行:  mitiru_host.exe navmesh/navmesh.dll
MITIRU_REFLECT_AUTO(NavmeshChapter);

MITIRU_ASSERT_NO_PADDING(NavmeshChapter);
MITIRU_GAME(NavmeshChapter);
