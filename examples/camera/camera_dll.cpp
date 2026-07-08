// camera — 追従カメラ。赤べこがマウスの方へ牧場を歩き、視点が滑らかに追従してスクロールする。
// 実行すると: マウスカーソルの方へ赤べこが歩く。画面より広い牧場を、視点が deadzone + 先読みで追う。
// 関連 API: mitiru::camera::FollowCam (setTarget / update) / Screen::applyCamera / endCamera / drawSprite

#include <algorithm>   // std::clamp / std::min
#include <cmath>       // std::sqrt / std::sin / std::fabs

#include <mitiru.hpp>
#include <mitiru/camera/FollowCam.hpp>   // 追従カメラ (deadzone + 先読み + ease + world clamp)
#include <mitiru/render/Texture.hpp>     // 画像を渡して描く drawSprite のため

#include "../common/chapter_hud.hpp"     // 章ラベル + 操作帯 (全章共通の書式)

using namespace mitiru;

constexpr float kScreenW = 1280.0f, kScreenH = 720.0f;
constexpr float kWorldW  = 4000.0f, kWorldH  = 2600.0f;   // 画面よりずっと広い牧場

// 画像はすべて赤べこと同じ生成方式 (tools/ の Python で SVG 風に作った PNG)。
static const render::Texture kBody = render::Texture::fromFile(
	"camera/assets/sprites/akabeko_body.png").value_or(render::Texture{});
static const render::Texture kHead = render::Texture::fromFile(
	"camera/assets/sprites/akabeko_head_neutral.png").value_or(render::Texture{});
static const render::Texture kTree = render::Texture::fromFile(
	"camera/assets/sprites/tree.png").value_or(render::Texture{});
constexpr float kBekoScale = 0.4f;
constexpr float kPivotOffX = 32.0f, kPivotOffY = 8.0f;   // 首の支点 (observe と同じ)

// 牧場に立つ木。根元のワールド座標 (x, y) と大きさ scale。前後関係のため y の昇順で並べる。
struct Tree { float x, y, scale; };
constexpr Tree kTrees[] = {
	{ 700.0f,  260.0f, 0.60f }, { 2300.0f,  320.0f, 0.72f }, { 3400.0f,  440.0f, 0.55f },
	{1300.0f,  560.0f, 0.66f }, {  500.0f,  680.0f, 0.70f }, { 2800.0f,  740.0f, 0.58f },
	{3700.0f,  860.0f, 0.64f }, { 1800.0f,  960.0f, 0.75f }, {  980.0f, 1080.0f, 0.56f },
	{3100.0f, 1180.0f, 0.68f }, { 2200.0f, 1320.0f, 0.60f }, {  620.0f, 1440.0f, 0.72f },
	{3600.0f, 1560.0f, 0.58f }, { 1420.0f, 1680.0f, 0.66f }, { 2700.0f, 1820.0f, 0.70f },
	{3300.0f, 2020.0f, 0.62f }, {  820.0f, 2160.0f, 0.68f }, { 1980.0f, 2320.0f, 0.74f },
};

struct CameraDemo
{
	float px = kWorldW * 0.5f, py = kWorldH * 0.5f;   // 赤べこのワールド座標
	float facing = 1.0f;                              // -1 = 左、+1 = 右
	float bob = 0.0f;                                 // 歩行の位相 (揺れ / 首振り)
	bool  moving = false;
	camera::FollowCam cam;
	bool  inited = false;

	void update(Input in, Hud hud, float dt)
	{
		if (!inited)   // カメラの効き方を一度だけ設定する
		{
			cam.cfg.deadzoneHalfW = 120.0f; cam.cfg.deadzoneHalfH = 80.0f;   // この範囲は動かない
			cam.cfg.lookaheadX = 120.0f;    cam.cfg.ease = 6.0f;             // 進む向きを先読み / 追従の滑らかさ
			cam.cfg.clamp = true;                                            // 牧場の外を映さない
			cam.cfg.worldBounds = Rect{0.0f, 0.0f, kWorldW, kWorldH};
			cam.cfg.viewW = kScreenW; cam.cfg.viewH = kScreenH;
			cam.setTarget(px, py); cam.snapToTarget();
			inited = true;
		}
		if (in.pressed(Key::Escape)) { hud.quit(); }

		// マウスカーソルのワールド座標 = 画面座標 + (カメラ中心 - 画面中心)。そこへ向かって歩く。
		const float wtx = in.mouseX() + cam.pos.x - kScreenW * 0.5f;
		const float wty = in.mouseY() + cam.pos.y - kScreenH * 0.5f;
		const float dx = wtx - px, dy = wty - py;
		const float dist = std::sqrt(dx * dx + dy * dy);
		moving = false;
		if (dist > 26.0f)   // カーソルが近ければ止まる
		{
			const float sp = std::min(dist * 3.5f, 380.0f);   // 遠いほど速く (上限あり)
			px = std::clamp(px + dx / dist * sp * dt, 40.0f, kWorldW - 40.0f);
			py = std::clamp(py + dy / dist * sp * dt, 40.0f, kWorldH - 40.0f);
			if (dx > 8.0f)       { facing =  1.0f; }
			else if (dx < -8.0f) { facing = -1.0f; }
			bob += dt * 9.0f;
			moving = true;
		}

		cam.setTarget(px, py);   // 追う先を毎フレーム伝える
		cam.setFacing(facing);
		cam.update(dt);          // deadzone / 先読み / ease / clamp をまとめて適用
	}

	void draw(Screen& s) const
	{
		s.fillScreen(theme::kPaper);

		s.applyCamera(cam.pos.x, cam.pos.y);   // 以降ワールド座標で描く → カメラ中心がスクロールする
		// 木と赤べこを接地 y の順に描く (奥＝上にある方を先に)。赤べこの足元 y が
		// 手前になる木より上なら、その木より先に描く = 赤べこが木の後ろに回り込む。
		const float bekoFootY = py + kBody.height() * kBekoScale * 0.35f;
		bool bekoDrawn = false;
		for (const Tree& t : kTrees)
		{
			if (!bekoDrawn && bekoFootY < t.y) { drawBeko(s); bekoDrawn = true; }
			drawTree(s, t);
		}
		if (!bekoDrawn) { drawBeko(s); }
		s.endCamera();

		// HUD は最後に画面座標で描く = 常に最前面
		chapterTitle(s, "Follow Camera");
		chapterControls(s, "マウスの方へ赤べこが歩く　視点が追従してスクロール　ESC: おわる");
	}

	void drawTree(Screen& s, const Tree& t) const
	{
		const float w = kTree.width() * t.scale, h = kTree.height() * t.scale;
		const Rect dst{ t.x - w * 0.5f, t.y - h, w, h };   // 根元 (t.x, t.y) に画像の下端を合わせる
		s.drawSprite(kTree, dst, Rect{0.0f, 0.0f, kTree.width() * 1.0f, kTree.height() * 1.0f}, color::White, false);
	}

	void drawBeko(Screen& s) const
	{
		const float bobY = moving ? std::fabs(std::sin(bob)) * 4.0f : 0.0f;   // 歩くと上下に弾む
		const float nod  = moving ? std::sin(bob) * 3.5f : 0.0f;              // 首を軽く振る
		const bool  faceLeft = facing < 0.0f;
		const float w = kBody.width() * kBekoScale, h = kBody.height() * kBekoScale;

		const float cy = py - bobY;
		const Rect dst{ px - w * 0.5f, cy - h * 0.5f, w, h };
		s.drawSprite(kBody, dst, Rect{0.0f, 0.0f, kBody.width() * 1.0f, kBody.height() * 1.0f}, color::White, faceLeft);

		const float side = faceLeft ? -1.0f : 1.0f;
		s.pushRotation(deg(nod * side), px + side * kPivotOffX * kBekoScale, cy + kPivotOffY * kBekoScale);
		s.drawSprite(kHead, dst, Rect{0.0f, 0.0f, kHead.width() * 1.0f, kHead.height() * 1.0f}, color::White, faceLeft);
		s.popTransform();
	}
};

// 実行:  mitiru_host.exe camera/camera.dll
MITIRU_GAME(CameraDemo);
