// coop3d。2〜4 人で画面を分ける (分割画面の協力プレイ、オンラインでもそのまま遊べる)
//   1 人に 1 枚の副ビュー (view3D の slot 0..3) で、それぞれのカメラから同じ長い参道を描く。2 人なら左右、3〜4 人なら 4 分割。
//   副ビューの間に積んだ灯り・粒・影はそのビューに出るので、世界を描く関数をビューごとに 1 回ずつ呼べば全員にそろう。
//   影・TAA・遮蔽はビューごとに掛かる。2 人が 100 m 離れても、それぞれの足元に柱と自分の影が落ちる。
// あそびかた: 1P はパッド 1 か WASD、2P はパッド 2 か矢印、3P と 4P はパッド 3 と 4 で歩く。dash (Shift か RB) を押す間は速い。
//   光る玉を拾うと全員の合計に数え、玉は 4 秒で戻る。オンライン (mitiru_host --net) では人数が in.netPlayers() になり、
//   dash は人ごとの操作 (in.actionDown(人, Act::Dash)) で読む。画面の左上には自分の席と ping を出す (s.netView()、描くだけ)。
// この章で使う関数: view3D / beginView3D / compositeView3D / in.pad(n) / in.netPlayers / in.actionDown(人, 操作) / s.netView /
//   s.netModeView / MITIRU_NET_PREDICT (host 権威で自分の箱を先に歩かせる) / s.netCorrections + network::smoothPosition
//   (予測が外れて正した箱とカメラを、跳ばさずに数フレームかけて戻す。描くだけで、状態は変えない)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mitiru.hpp>
#include <mitiru/network/NetSmoothing.hpp>
#include <mitiru/render/GpuParticles.hpp>
#include <mitiru/render/SceneLook.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;

constexpr int kOrbs = 12;
constexpr int kMaxPlayers = 4;
constexpr float kRespawnSec = 4.0f;
constexpr float kHalfLength = 60.0f;   // 参道は x が -60〜60 m、z が -9〜9 m
constexpr std::uint32_t kPlayerRgb[kMaxPlayers] = {0xFF9500, 0x30C8D8, 0x8CD84A, 0xE05AB0};

enum class Act : int { Dash };

Vec3 orbPos(int k)
{
	return {-55.0f + 10.0f * static_cast<float>(k), 0.7f, (k % 2 == 0) ? -2.0f : 2.0f};
}

struct Coop3D
{
	Vec3 pos[kMaxPlayers] = {{-12.0f, 0.0f, 0.0f}, {12.0f, 0.0f, 0.0f}, {-12.0f, 0.0f, 4.0f}, {12.0f, 0.0f, 4.0f}};
	float t = 0.0f;
	float takenAt[kOrbs] = {-100, -100, -100, -100, -100, -100, -100, -100, -100, -100, -100, -100};   // 拾った時刻
	std::int32_t score = 0;
	std::int32_t players = 2;   // オフラインは 2 人。オンラインは全員の PC で同じ人数

	// 1P はパッド 1 と WASD、2P はパッド 2 と矢印、3P と 4P はパッドだけ。スティックは +y が上 (奥へ)
	Vec3 stepOf(Input in, int who) const
	{
		const Stick st = in.pad(who).leftStick();
		const bool keys = who < 2;
		const bool l = keys && in.down(who == 0 ? Key::A : Key::Left), r = keys && in.down(who == 0 ? Key::D : Key::Right);
		const bool u = keys && in.down(who == 0 ? Key::W : Key::Up), d = keys && in.down(who == 0 ? Key::S : Key::Down);
		const float x = st.x + (r ? 1.0f : 0.0f) - (l ? 1.0f : 0.0f);
		const float z = -st.y + (d ? 1.0f : 0.0f) - (u ? 1.0f : 0.0f);
		return {std::clamp(x, -1.0f, 1.0f), 0.0f, std::clamp(z, -1.0f, 1.0f)};
	}

	void walk(Input in, int who, float dt)
	{
		const Vec3 v = stepOf(in, who);
		const float speed = in.actionDown(who, Act::Dash) ? 7.0f : 4.0f;
		pos[who].x = std::clamp(pos[who].x + v.x * speed * dt, -kHalfLength + 2.0f, kHalfLength - 2.0f);
		pos[who].z = std::clamp(pos[who].z + v.z * speed * dt, -7.5f, 7.5f);
	}

	// host 権威のオンライン (--net-mode authority) で、自分の箱だけを先に歩かせる (MITIRU_NET_PREDICT)。host はこれを描くための
	// 写しで呼ぶ。玉を拾うのは host が決めるので、ここでは歩くだけ
	void predict(Input in, int who, float dt) { walk(in, who, dt); }

	void update(Input in, Hud hud, float dt)
	{
		if (in.pressed(Key::Escape)) { hud.quit(); }
		t += dt;
		players = in.netPlayers() > 0 ? in.netPlayers() : 2;
		for (int who = 0; who < players; ++who)
		{
			walk(in, who, dt);
			for (int k = 0; k < kOrbs; ++k)
			{
				const Vec3 o = orbPos(k);
				const float dx = o.x - pos[who].x, dz = o.z - pos[who].z;
				if (t - takenAt[k] >= kRespawnSec && dx * dx + dz * dz < 0.8f * 0.8f) { takenAt[k] = t; ++score; }
			}
		}
	}

	// 1 人ぶんのビューに世界を描く。灯りと粒もここで積むので、そのビューに出る
	// 描く位置。オンラインで予測が外れて正した時は、正す前の位置から数フレームかけて寄せる (オフラインでは pos のまま)
	Vec3 shownPos(const Screen& s, int who) const
	{
		return network::smoothPosition(s.netCorrections(), *this, [who](const Coop3D& g) { return g.pos[who]; });
	}

	void drawWorld(Screen& s) const
	{
		s.drawMesh("cube", {0.0f, -0.1f, 0.0f}, {2.0f * kHalfLength, 0.2f, 18.0f}, {0, 0, 0}, hex(0x6A6458));   // 石畳
		for (int i = 0; i < 20; ++i)   // 両側に 6 m おきの柱。影が参道に斜めに落ちる
		{
			const float x = -57.0f + 6.0f * static_cast<float>(i);
			for (const float z : {-6.0f, 6.0f}) { s.drawMesh("cube", {x, 1.5f, z}, {0.7f, 3.0f, 0.7f}, {0, 0, 0}, hex(0xC8B8A0)); }
		}
		render::ParticleEmitterDesc bursts[kOrbs];
		int burstCount = 0;
		for (int k = 0; k < kOrbs; ++k)
		{
			const Vec3 o = orbPos(k);
			const float since = t - takenAt[k];
			if (since >= kRespawnSec)
			{
				s.drawMesh("sphere", o, {0.35f, 0.35f, 0.35f}, {0, 0, 0}, Color{2.4f, 2.0f, 0.9f, 1.0f});   // bloom で光る
				s.pointLight3D(o + Vec3{0.0f, 0.9f, 0.0f}, 3.0f, hex(0xFFD27A), 2.0f);   // 玉の上から玉と足元を照らす
				continue;
			}
			render::ParticleEmitterDesc& b = bursts[burstCount++];   // 拾った所から火の粉が散る (経過秒で決まる)
			b.key = 100u + static_cast<std::uint32_t>(k);
			b.seed = static_cast<std::uint32_t>(k) * 7u + 1u;
			b.burst = 60;
			b.rate = 0.0f;
			b.lifeMin = 0.4f; b.lifeMax = 0.9f;
			b.speedMin = 1.5f; b.speedMax = 3.0f;
			b.spreadDeg = 180.0f;
			b.position[0] = o.x; b.position[1] = o.y; b.position[2] = o.z;
			b.size[0] = 0.12f; b.size[1] = 0.08f; b.size[2] = 0.0f;
			for (auto& c : b.color) { c[0] = 2.5f; c[1] = 1.6f; c[2] = 0.4f; c[3] = 1.0f; }
			b.age = since;
		}
		if (burstCount > 0) { s.particles3D(bursts, burstCount); }
		for (int who = 0; who < players; ++who)
		{
			const Vec3 at = shownPos(s, who);
			s.drawMesh("cube", at + Vec3{0, 0.75f, 0}, {0.6f, 1.5f, 0.6f}, {0, 0, 0}, hex(kPlayerRgb[who]));
			s.pointLight3D(at + Vec3{0, 2.0f, 0}, 4.0f, hex(kPlayerRgb[who]), 1.5f);   // 頭の上の手提げ灯
		}
	}

	// 2 人なら左右に 640 x 720、3〜4 人なら 640 x 360 の 4 分割
	Rect viewRect(int who) const
	{
		if (players <= 2) { return {640.0f * static_cast<float>(who), 0.0f, 640.0f, 720.0f}; }
		return {640.0f * static_cast<float>(who % 2), 360.0f * static_cast<float>(who / 2), 640.0f, 360.0f};
	}

	void drawView(Screen& s, int who) const
	{
		const Rect r = viewRect(who);
		render::View3DPod pod;
		pod.width = static_cast<std::int32_t>(r.size.x);
		pod.height = static_cast<std::int32_t>(r.size.y);
		pod.clear[0] = 0.10f; pod.clear[1] = 0.08f; pod.clear[2] = 0.19f;
		s.view3D(who, pod);
		const Vec3 me = shownPos(s, who);   // カメラも戻した位置から作る (箱だけ戻すと、箱を追うカメラが跳ぶ)
		const Vec3 eye = me + Vec3{0.0f, 6.0f, 7.0f};
		if (s.beginView3D(who, eye, me + Vec3{0.0f, 0.8f, 0.0f}, {0, 1, 0}, Deg{55.0f}, 0.3f, 80.0f))
		{
			drawWorld(s);
			s.endView3D();
		}
		s.compositeView3D(who, r.position.x, r.position.y, r.size.x, r.size.y);
	}

	// オンラインの様子は PC ごとに違うので、描くだけに使う (GameMemory へは書かない)
	void drawNet(Screen& s) const
	{
		const module::NetView& net = s.netView();
		if (net.state == module::kNetStateOffline) { return; }
		char line[64];
		const int other = (net.localPlayer + 1) % players;
		std::snprintf(line, sizeof(line), "%dP  ping %d ms%s", net.localPlayer + 1, static_cast<int>(net.pingMs[other]),
		              net.state == module::kNetStateDesync ? "  食い違い" : "");
		s.drawTextInRect({16.0f, 96.0f, 360.0f, 28.0f}, line, theme::kPaper, 18.0f, Screen::TextAlignH::Left,
		                 Screen::TextAlignV::Middle);
		const module::NetModeView& mode = s.netModeView();   // host 権威の参加者は、描いている状態の遅れと先に歩いた分
		if (mode.mode != module::kNetModeAuthority || net.localPlayer == 0) { return; }
		std::snprintf(line, sizeof(line), "host 権威 %d Hz  遅れ %d ms  先読み %d", mode.snapshotHz, mode.renderDelayMs,
		              mode.predictedFrames);
		s.drawTextInRect({16.0f, 124.0f, 420.0f, 28.0f}, line, theme::kPaper, 18.0f, Screen::TextAlignH::Left,
		                 Screen::TextAlignV::Middle);
	}

	void draw(Screen& s) const
	{
		s.clear(hex(0x1A1430));
		render::SceneLook look;   // 夕方。低い日の影はビューごとに 3 段のカスケードを、そのビューのカメラに合わせて焼く
		look.ambient[0] = 0.16f; look.ambient[1] = 0.14f; look.ambient[2] = 0.20f;
		look.shadow = look.shadowCascaded = look.shadowCascadeAutoFit = true;
		look.shadowCascadeCount = 3;
		look.shadowDistance = 30.0f;
		look.shadowDirection[0] = -0.5f; look.shadowDirection[1] = -0.8f; look.shadowDirection[2] = -0.35f;
		look.shadowBias = 0.1f;   // 斜めから照らす柱の側面に縞 (影の自己遮蔽) が出ないように
		look.aaMode = 3;          // TAA。履歴はビューごとに持つ
		look.ao = true;           // 柱の根元と足元の遮蔽もビューごと
		look.bloom = true;
		look.bloomThreshold = 1.0f;
		s.sceneLook3D(look);
		s.light3D({-0.5f, -0.8f, -0.35f}, hex(0xFFC890));
		for (int who = 0; who < players; ++who) { drawView(s, who); }
		for (int who = players; who < kMaxPlayers; ++who) { s.releaseView3D(who); }
		s.drawRect({638.0f, 0.0f, 4.0f, 720.0f}, theme::kInk);   // 境目
		if (players > 2) { s.drawRect({0.0f, 358.0f, 1280.0f, 4.0f}, theme::kInk); }
		char line[48];
		std::snprintf(line, sizeof(line), "あつめた玉 %d　%d 人", score, players);
		s.drawTextInRect({440.0f, 60.0f, 400.0f, 36.0f}, line, theme::kPaper, 22.0f, Screen::TextAlignH::Center,
		                 Screen::TextAlignV::Middle);
		drawNet(s);
		chapterTitle(s, "みんなで分割画面");
		chapterControls(s, "1P: パッド 1 / WASD　2P: パッド 2 / 矢印　3P 4P: パッド 3 4　dash: Shift / RB　Esc: おわる");
	}
};

// 実行:  mitiru_host.exe coop3d/coop3d.dll   (オンライン: --net か --net-host / --net-join、docs/ONLINE.md)
MITIRU_ASSERT_NO_PADDING(Coop3D);
MITIRU_ACTIONS(R"json({ "actions": [
  { "id": "dash", "label": "Dash", "slot": "key:Shift", "reads": ["key:Shift", "pad:RB"], "defaults": ["key:Shift", "pad:RB"] }
] })json");
MITIRU_GAME(Coop3D);
MITIRU_NET_PREDICT(Coop3D);
