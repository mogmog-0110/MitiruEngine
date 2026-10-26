// lights2d。2D ライトの章。暗い石の部屋を、法線マップつきの絵と松明の光で照らす。
// 実行すると、石畳・レンガ・樽・木箱が暗く沈み、マウスの松明が凹凸に沿った陰影を作る。壁の松明は揺れる。
// 使う機能: ambient2D / light2D (環境光と灯) / sprite(id) と、同じ名前に _n を付けた法線マップの画像

#include <algorithm>
#include <cmath>
#include <type_traits>   // Canvas と Screen で灯の渡し方が違うため、if constexpr で分ける

#include <mitiru.hpp>
#include <mitiru/module/AutoReflect.hpp>   // MITIRU_REFLECT_AUTO
#include "../common/chapter_hud.hpp"

using namespace mitiru;

// 光は、ambient2D / light2D を呼んだ後に描いた物へ当たる。だから紙の背景・暗いカード・章ラベルは先に描き、
// 部屋の絵はその後に描く。部屋の絵は 16 画素の画像を 4 倍に拡大して並べる (assets/sprites/<名前>.png と <名前>_n.png)。
constexpr float kRoomX = 64.0f, kRoomY = 94.0f, kTile = 64.0f, kScale = 4.0f;
constexpr int   kCols = 18, kRows = 9, kWallRows = 2;
struct Prop { const char* id; int col, row; };
// 同じ絵で、法線マップが「ある」(barrel / crate) と「無い」(*_plain) を並べる。光の当たり方の違いが見える
constexpr Prop kProps[] = {{"barrel", 3, 3}, {"barrel_plain", 5, 3}, {"crate", 8, 5}, {"crate_plain", 10, 5},
                           {"barrel", 13, 3}, {"barrel_plain", 15, 3}, {"crate", 6, 7}, {"crate_plain", 12, 7}};
constexpr int kWallTorchCols[] = {3, 9, 15};

template <class Surface>
void shine(Surface& s, const render::Light2DPod& light)
{
	if constexpr (std::is_same_v<Surface, Canvas>) { s.light2D(light); } else { s.light2D(&light, 1); }
}

struct Lights2d
{
	float t = 0.0f;                                 // 経過秒 (松明の揺れと、マウスを動かす前の巡回に使う)
	float torchX = 640.0f, torchY = 420.0f;          // 手に持つ松明の位置
	float lastMouseX = 0.0f, lastMouseY = 0.0f;
	bool  followMouse = false;
	std::uint8_t _pad[3] = {};   // 暗黙のパディングを残さない (状態をバイト単位で比べるため)

	void update(Input in, float dt)
	{
		t += dt;
		if (in.mouseX() != lastMouseX || in.mouseY() != lastMouseY) { followMouse = true; }   // 一度マウスを動かしたら、それを追う
		lastMouseX = in.mouseX();
		lastMouseY = in.mouseY();
		const float goalX = followMouse ? in.mouseX() : 640.0f + 380.0f * std::cos(t * 0.5f);
		const float goalY = followMouse ? in.mouseY() : 430.0f + 110.0f * std::sin(t * 0.8f);
		const float follow = std::min(1.0f, 10.0f * dt);   // 目標へ追いつく速さ
		torchX += (goalX - torchX) * follow;
		torchY += (goalY - torchY) * follow;
	}

	// 炎の揺れ。2 つの周期を重ね、phase で灯ごとにずらす
	float flicker(float phase) const { return 1.0f + 0.08f * std::sin(t * 13.0f + phase) + 0.05f * std::sin(t * 7.3f + phase * 2.1f); }

	template <class Surface>
	void drawImpl(Surface& s) const
	{
		s.fillScreen(theme::kPaper);
		s.drawRoundedRect(Rect{kRoomX - 12.0f, kRoomY - 12.0f, kCols * kTile + 24.0f, kRows * kTile + 24.0f}, theme::kCard, 14.0f);
		chapterTitle(s, "2D ライト");
		chapterControls(s, "マウス: 松明を うごかす　（左の樽と木箱は法線マップあり、右は なし）");

		s.ambient2D(Color{0.45f, 0.52f, 0.8f, 1.0f}, 0.3f);   // 灯の届かない所の明るさ (青みがかった夜)
		render::Light2DPod hand;                              // 手に持つ松明
		hand.x = torchX; hand.y = torchY; hand.height = 70.0f; hand.radius = 380.0f; hand.falloff = 1.6f;
		hand.color[0] = 1.0f; hand.color[1] = 0.72f; hand.color[2] = 0.42f; hand.intensity = 1.3f * flicker(0.0f);
		shine(s, hand);
		for (int i = 0; i < 3; ++i)                           // 壁の松明
		{
			render::Light2DPod wall;
			wall.x = kRoomX + kWallTorchCols[i] * kTile + kTile * 0.5f; wall.y = kRoomY + kTile + 16.0f;
			wall.height = 30.0f; wall.radius = 250.0f; wall.falloff = 1.8f;
			wall.color[0] = 1.0f; wall.color[1] = 0.55f; wall.color[2] = 0.25f; wall.intensity = 0.9f * flicker(2.0f * i);
			shine(s, wall);
		}

		for (int row = 0; row < kRows; ++row)
		{
			for (int col = 0; col < kCols; ++col) { s.sprite(row < kWallRows ? "wall" : "floor", kRoomX + col * kTile, kRoomY + row * kTile, kScale); }
		}
		for (int col : kWallTorchCols) { s.sprite("torch", kRoomX + col * kTile, kRoomY + kTile, kScale); }
		for (const Prop& p : kProps) { s.sprite(p.id, kRoomX + p.col * kTile, kRoomY + p.row * kTile, kScale); }
	}
	void draw(Screen& s) const { drawImpl(s); }
	void draw(Canvas& c) const { drawImpl(c); }
};

// 2D ライトの資源を、宣言したゲームだけが作る。3D は描かないので、3D の層も作らない
MITIRU_RENDER_NEEDS("light_2d,no_3d")
MITIRU_REFLECT_AUTO(Lights2d);
MITIRU_ASSERT_NO_PADDING(Lights2d);
MITIRU_GAME(Lights2d);
