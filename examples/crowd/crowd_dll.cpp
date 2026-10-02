// crowd。「大勢の敵を群衆で歩かせる」章。120 体が互いに避けながら歩き、閉じた扉を回り込む
// 実行すると: 3 本の筋に分かれた 120 体が広場を回り続ける。右へは真ん中の扉を、左へは奥の脇道を通る。扉は 6 秒ごとに開け閉めし、閉まると右へ向かう流れが手前の脇道へ回る。Space で扉を手で開け閉めする (--inspect rewind で閉まる前へ戻せる)
// 関連 API: NavCrowd / DynamicNavMesh::setObstacle / NavObstacle / bakeNavCache / MITIRU_SIDE_STATE

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <mitiru.hpp>
#include <mitiru/module/ReflectEngineTypes.hpp>
#include <mitiru/module/SideState.hpp>
#include <mitiru/nav/NavCacheBake.hpp>
#include <mitiru/nav/NavCrowd.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;
using sgc::Vec3f;

struct Block { Vec3f lo, hi; };
constexpr Block kBlocks[] = {
	{{-16, -1, -10}, {16, 0, 10}},             // 床
	{{-0.5f, 0, -6.0f}, {0.5f, 2.5f, -1.5f}},  // 壁の奥側。奥の端 (z<-6) が脇道
	{{-0.5f, 0, 1.5f}, {0.5f, 2.5f, 5.0f}},    // 壁の手前側。手前の端 (z>5) が脇道
};
constexpr Block kDoor{{-0.5f, 0, -1.5f}, {0.5f, 2.5f, 1.5f}};
constexpr int kAgents = 120;
constexpr int kLanes = 3;

// レベルの三角形から、障害物で作り直せるナビメッシュの元を DLL の中で焼き、120 体を並べる
// 巻き戻しやホットリロードでも呼ばれるので、何度呼んでも同じ物を同じ順で足す
Vec3f corner(int lane, int c);
Vec3f spawn(int i, int& next);
void buildCrowd(nav::NavCrowd& crowd, const void*)
{
	std::vector<Vec3f> v;
	std::vector<std::uint32_t> idx;
	for (const Block& b : kBlocks)
	{
		const auto base = static_cast<std::uint32_t>(v.size());
		for (int c = 0; c < 8; ++c) { v.push_back({(c & 1) ? b.hi.x : b.lo.x, (c & 2) ? b.hi.y : b.lo.y, (c & 4) ? b.hi.z : b.lo.z}); }
		for (std::uint32_t f : {2, 6, 7, 2, 7, 3, 0, 1, 5, 0, 5, 4, 0, 2, 3, 0, 3, 1, 4, 5, 7, 4, 7, 6, 0, 4, 6, 0, 6, 2, 1, 3, 7, 1, 7, 5}) { idx.push_back(base + f); }
	}
	nav::NavBakeSettings settings;
	settings.tileSizeCells = 32;   // 小さいタイルほど、扉を閉めたときに作り直す範囲が狭い
	if (!crowd.load(nav::bakeNavCache(v, idx, settings).blob)) { return; }
	crowd.navMesh().setObstacle(0, nav::NavObstacle::box((kDoor.lo + kDoor.hi) * 0.5f, (kDoor.hi - kDoor.lo) * 0.5f));
	for (int i = 0; i < kAgents; ++i)
	{
		int next = 0;
		const int id = crowd.addAgent(spawn(i, next));
		crowd.setTarget(id, corner(i % kLanes, next));
	}
}

// 群衆は GameMemory の外にある。窓口として預けると、巻き戻し・セーブ・ホットリロードで GameMemory と一緒に戻る
nav::NavCrowd g_crowd(&buildCrowd);
MITIRU_SIDE_STATE("crowd", 1, g_crowd);

// 筋 lane の長方形の角。カメラは +z の側から見下ろすので、0: 左手前、1: 右手前、2: 右奥、3: 左奥。右へは扉を、左へは奥の脇道を通る
Vec3f corner(int lane, int c)
{
	const float in = 0.9f * static_cast<float>(lane), x = 12.0f - in, north = 1.0f - in, south = -8.6f + in;
	const Vec3f k[4] = {{-x, 0, north}, {x, 0, north}, {x, 0, south}, {-x, 0, south}};
	return k[c & 3];
}

// i 番目を筋の上に等間隔に置き、次に向かう角を next に返す
Vec3f spawn(int i, int& next)
{
	const int lane = i % kLanes;
	const Vec3f a = corner(lane, 0), c = corner(lane, 2);
	const float len[4] = {c.x - a.x, a.z - c.z, c.x - a.x, a.z - c.z};
	float s = 2.0f * (len[0] + len[1]) * static_cast<float>(i / kLanes) / static_cast<float>(kAgents / kLanes);
	int leg = 0;
	for (; leg < 3 && s >= len[leg]; ++leg) { s -= len[leg]; }
	next = (leg + 1) & 3;
	const Vec3f p = corner(lane, leg), q = corner(lane, leg + 1);
	return p + (q - p) * (s / len[leg]);
}

struct CrowdDoor
{
	std::uint32_t frame = 0, doorClosed = 0, manual = 0;
	std::uint32_t doorChanged = 0;             // 扉を最後に開け閉めしたフレーム
	std::int32_t  replans = 0;                 // 群衆が経路を探した agent の数の累計
	std::int32_t  doorPasses = 0, nearPasses = 0, farPasses = 0;
	std::int32_t  doorPassesWhileClosed = 0;   // 閉まって 5 フレーム後からも扉を抜けた数。0 のまま
	float         minGap = 0.0f, worstGap = 1e9f;   // 一番近い 2 体の体の隙間 (負なら重なり) と、その最小
	float         agent0x = 0.0f, agent0z = 0.0f;   // 0 番の位置 (動きの記録用)
	// 描く物は群衆から写す。エンジンの型のまま置くと、--inspect nav の窓が地図に描く
	FixedVec<nav::CrowdAgentView, kAgents> agents{};
	nav::NavObstacle door{};

	void init() { g_crowd.reset(); *this = CrowdDoor{}; }

	void update(Input in, float dt)
	{
		g_crowd.ensureBuilt(this);
		if (in.pressed(Key::Space)) { manual ^= 1u; }
		const std::uint32_t closed = ((frame / 360u) & 1u) ^ manual;   // 6 秒ごとに開け閉め
		if (closed != doorClosed) { doorChanged = frame; }
		doorClosed = closed;
		for (int i = 0; i < kAgents; ++i)   // 角の 1.5 m 手前で次の角へ向かう
		{
			const nav::CrowdAgentView v = g_crowd.view(i);
			const int lane = i % kLanes;
			for (int c = 0; c < 4; ++c)
			{
				const Vec3f k = corner(lane, c);
				if (std::hypot(k.x - v.target.x, k.z - v.target.z) < 0.5f && std::hypot(k.x - v.position.x, k.z - v.position.z) < 1.5f)
				{
					g_crowd.setTarget(i, corner(lane, c + 1));
					break;
				}
			}
		}
		g_crowd.navMesh().setObstacleEnabled(0, doorClosed != 0);
		g_crowd.update(dt);
		door = g_crowd.navMesh().obstacle(0);
		replans += g_crowd.replansLastUpdate();
		measure();
		++frame;
	}

	// 壁 (x=0) を跨いだ所で、どの通り道を使ったかを数える。重なりは全組の中心の距離から測る
	void measure()
	{
		minGap = 1e9f;
		agents.count = kAgents;
		for (int i = 0; i < kAgents; ++i)
		{
			const nav::CrowdAgentView now = g_crowd.view(i);
			const Vec3f p = now.position;
			if ((agents[i].position.x < 0.0f) != (p.x < 0.0f) && frame > 0)
			{
				const bool door = std::fabs(p.z) < 1.5f;
				doorPasses += door ? 1 : 0;
				nearPasses += p.z > 4.5f ? 1 : 0;
				farPasses += p.z < -5.5f ? 1 : 0;
				doorPassesWhileClosed += (door && doorClosed != 0 && frame > doorChanged + 5) ? 1 : 0;   // 閉まった瞬間に戸口にいた体は脇へ押し出される
			}
			agents[i] = now;
			for (int j = 0; j < i; ++j) { minGap = std::fmin(minGap, std::hypot(p.x - agents[j].position.x, p.z - agents[j].position.z) - 0.8f); }
		}
		worstGap = std::fmin(worstGap, minGap);
		agent0x = agents[0].position.x;
		agent0z = agents[0].position.z;
	}

	void draw(Screen& s) const
	{
		s.clear(hex(0xEAF1F8));
		s.camera3D({0, 27, 15}, {0, 0, 0.5f}, 50);
		s.light3D({-0.5f, -1.0f, -0.35f}, hex(0xFFFBF2));
		for (const Block& b : kBlocks) { s.drawMesh("cube", (b.lo + b.hi) * 0.5f, b.hi - b.lo, {0, 0, 0}, hex(b.lo.y < 0 ? 0xC9D1DC : 0x8C9BB0)); }
		const Vec3f doorSize = doorClosed != 0 ? kDoor.hi - kDoor.lo : Vec3f{1.0f, 0.06f, 3.0f};   // 開いた扉は床の線だけ
		s.drawMesh("cube", {0, doorSize.y * 0.5f, 0}, doorSize, {0, 0, 0}, theme::kRed);
		const Color lane[kLanes] = {theme::kBlue, theme::kGreen, theme::kOrange};
		for (int i = 0; i < kAgents; ++i) { s.drawMesh("cube", {agents[i].position.x, 0.6f, agents[i].position.z}, {0.6f, 1.2f, 0.6f}, {0, 0, 0}, lane[i % kLanes]); }
		char line[96];
		std::snprintf(line, sizeof line, "扉: %s　扉を抜けた %d　手前の脇道 %d　奥の脇道 %d", doorClosed != 0 ? "閉" : "開", doorPasses, nearPasses, farPasses);
		s.text(line, 16.0f, 64.0f, theme::kInk, 18);
		std::snprintf(line, sizeof line, "一番近い 2 体の隙間 %.2f m (最小 %.2f m)", minGap, worstGap);
		s.text(line, 16.0f, 90.0f, theme::kInk, 18);
		if (!g_crowd.error().empty()) { s.text(g_crowd.error(), 16.0f, 116.0f, theme::kRed, 18); }
		chapterTitle(s, "群衆と扉");
		chapterControls(s, "Space: 扉を開け閉め (6 秒ごとにも開け閉めする)");
	}
};

MITIRU_ASSERT_NO_PADDING(CrowdDoor);
MITIRU_REFLECT(CrowdDoor, doorClosed, minGap, worstGap, doorPasses, nearPasses, farPasses, doorPassesWhileClosed, replans,
               agent0x, agent0z, agents, door);
MITIRU_GAME(CrowdDoor);
