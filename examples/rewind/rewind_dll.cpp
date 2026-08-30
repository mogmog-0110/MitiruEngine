// rewind。「時間を巻き戻す」を体験する章 (ブロック積み)
//   ゲームの状態をぜんぶ 1 つの構造体にまとめておくと、エンジンが毎フレームその中身を
//   自動で覚えてくれる。だから、あとから好きな過去のフレームへ丸ごと戻せる。ここでは
//   物理 (位置・速度・回転) もその構造体に入っているので、崩れたブロックも巻き戻せる。
// あそびかた: 床に置かれたブロックをマウスでつまんで積み上げる。つまんだ点がカーソルに
//             引かれ、重力でぶらんぶらん揺れる。崩れたら下の別窓のバーを左へ動かして戻す。
// 使う機能: 状態を 1 つの構造体にまとめる MITIRU_GAME / ポインタを持たない配列 FixedVec

#include <algorithm>   // std::min / std::max / std::clamp
#include <cmath>       // std::cos / std::sin / std::fabs
#include <cstdint>

#include <mitiru.hpp>
#include <mitiru/core/FixedVec.hpp>   // 長さの上限が決まった配列。ポインタを持たないので構造体ごとまるごとコピーできる
#include "../common/chapter_hud.hpp"  // 章ラベル + 操作帯 (全章共通の書式)

using namespace mitiru;

// ── プレイエリアと物理の設定 ──────────────────────────────────
constexpr float kScreenW = 1280.0f, kScreenH = 720.0f;
constexpr float kCeil = 72.0f, kFloor = 650.0f;   // ブロックが動ける範囲 (タイトルと操作帯の間)
constexpr float kWallL = 0.0f, kWallR = kScreenW;  // 左右のかべ
constexpr float kGravity = 1400.0f;                // 重力 (下向きの加速度)

constexpr int   kMaxBlocks   = 16;    // ブロックの最大数
constexpr int   kMaxContacts = 128;   // 1 フレームで扱う接触点の最大数
constexpr int   kSubsteps    = 4;     // 1 フレームを何回に分けて解くか (多いほど安定)
constexpr int   kVelIters    = 8;     // 速度をならす反復回数 (多いほど積みが安定)

constexpr float kMaxV = 1800.0f, kMaxW = 14.0f;   // 速度・角速度の上限 (爆発を防ぐ)
constexpr float kFriction = 0.5f;                 // まさつの強さ
constexpr float kSlop = 0.4f, kPosPercent = 0.4f; // めり込みの許容と押し戻す割合
constexpr float kGrabPull = 200.0f, kGrabDamp = 16.0f;   // つまみのバネと揺れの収まり

// ブロックの色は 6 色を順ぐりに使う。
constexpr Color kPalette[6] = {
	theme::kBlue, theme::kPink, theme::kGreen, theme::kOrange, theme::kAmber, theme::kRed};

// 1 つのブロック。位置・速度・回転をすべて持つ単純なデータ (ポインタなし)。
struct Block
{
	float        x = 0.0f, y = 0.0f;     // 中心の位置
	float        angle = 0.0f;           // かたむき (ラジアン)
	float        vx = 0.0f, vy = 0.0f;   // 速度
	float        w = 0.0f;               // 角速度 (回る速さ)
	float        hw = 20.0f, hh = 20.0f; // 半分の幅・高さ
	std::uint8_t color = 0;
};

// 接触点 1 つぶんの情報 (その場かぎりで使う。状態には入れない)。
// a はブロック番号、b は相手のブロック番号 (かべ・床は -1)。
struct Contact
{
	int   a = 0, b = -1;
	float px = 0.0f, py = 0.0f;   // ぶつかっている点
	float nx = 0.0f, ny = 0.0f;   // 押し戻す向き (a から相手へ)
	float depth = 0.0f;           // めり込んだ深さ
};

using Contacts = FixedVec<Contact, kMaxContacts>;

// ── ブロックの物理的な性質 (重さ・回りにくさ) ──────────────────
inline float blockMass(const Block& b) { return 4.0f * b.hw * b.hh; }        // 面積を重さとみなす
inline float blockInvMass(const Block& b) { return 1.0f / blockMass(b); }
inline float blockInvInertia(const Block& b)   // 回りにくさの逆数 (長方形の公式)
{
	const float m = blockMass(b), w = 2.0f * b.hw, h = 2.0f * b.hh;
	return 1.0f / (m * (w * w + h * h) / 12.0f);
}

// ブロックの 4 すみの世界座標を求める。
inline void blockCorners(const Block& b, float outX[4], float outY[4])
{
	const float c = std::cos(b.angle), s = std::sin(b.angle);
	const float lx[4] = {-b.hw, b.hw, b.hw, -b.hw};
	const float ly[4] = {-b.hh, -b.hh, b.hh, b.hh};
	for (int i = 0; i < 4; ++i)
	{
		outX[i] = b.x + lx[i] * c - ly[i] * s;
		outY[i] = b.y + lx[i] * s + ly[i] * c;
	}
}

// 点 (px, py) がブロックの中に入っているか。
inline bool pointInBlock(const Block& b, float px, float py)
{
	const float c = std::cos(b.angle), s = std::sin(b.angle);
	const float dx = px - b.x, dy = py - b.y;
	const float lx = dx * c + dy * s;    // ブロックのローカル座標に直す
	const float ly = -dx * s + dy * c;
	return std::fabs(lx) <= b.hw && std::fabs(ly) <= b.hh;
}

// 1 本の向きに 4 すみを射影して、いちばん手前と奥を返す。
inline void projectCorners(const float cx[4], const float cy[4], float nx, float ny,
                           float& mn, float& mx)
{
	mn = mx = cx[0] * nx + cy[0] * ny;
	for (int i = 1; i < 4; ++i)
	{
		const float d = cx[i] * nx + cy[i] * ny;
		mn = std::min(mn, d);
		mx = std::max(mx, d);
	}
}

// 2 つのブロックが重なっているか調べる (分離軸法)。
// 重なっていれば、いちばん浅い向き (押し戻す向き) とその深さを返す。
struct Sat { bool hit; float nx, ny, depth; };
inline Sat satBoxes(const Block& A, const Block& B)
{
	float ax[4], ay[4], bx[4], by[4];
	blockCorners(A, ax, ay);
	blockCorners(B, bx, by);
	const float ca = std::cos(A.angle), sa = std::sin(A.angle);
	const float cb = std::cos(B.angle), sb = std::sin(B.angle);
	const float axesX[4] = {ca, -sa, cb, -sb};   // 2 つの箱の面の向き 4 本
	const float axesY[4] = {sa, ca, sb, cb};

	float best = 1e9f, bnx = 0.0f, bny = 0.0f;
	for (int k = 0; k < 4; ++k)
	{
		float mnA, mxA, mnB, mxB;
		projectCorners(ax, ay, axesX[k], axesY[k], mnA, mxA);
		projectCorners(bx, by, axesX[k], axesY[k], mnB, mxB);
		const float overlap = std::min(mxA, mxB) - std::max(mnA, mnB);
		if (overlap <= 0.0f) { return {false, 0.0f, 0.0f, 0.0f}; }   // すき間があれば当たっていない
		if (overlap < best) { best = overlap; bnx = axesX[k]; bny = axesY[k]; }
	}
	if ((B.x - A.x) * bnx + (B.y - A.y) * bny < 0.0f) { bnx = -bnx; bny = -bny; }   // A から B へ向ける
	return {true, bnx, bny, best};
}

// つまむブロックが最初から床に置いてある状態を作る。5 個を並べ、1 個だけ上に乗せる。
inline FixedVec<Block, kMaxBlocks> makeInitialBlocks()
{
	FixedVec<Block, kMaxBlocks> v;
	struct Spec { float hw, hh; };
	const Spec floorSpecs[5] = {{46, 30}, {34, 26}, {40, 40}, {30, 44}, {44, 28}};
	float edge = 300.0f;      // 左から順に置いていく
	float baseX = 0.0f, baseTop = 0.0f;
	for (int i = 0; i < 5; ++i)
	{
		Block b;
		b.hw = floorSpecs[i].hw;
		b.hh = floorSpecs[i].hh;
		b.x = edge + b.hw;
		b.y = kFloor - b.hh;   // 床の上にのせる
		b.color = static_cast<std::uint8_t>(i);
		(void)v.push_back(b);
		if (i == 2) { baseX = b.x; baseTop = b.y - b.hh; }   // 3 個目の上に 1 個積んでおく
		edge = b.x + b.hw + 30.0f;
	}
	Block top;
	top.hw = 38.0f;
	top.hh = 34.0f;
	top.x = baseX;
	top.y = baseTop - top.hh;
	top.color = 5;
	(void)v.push_back(top);
	return v;
}

// ゲームの状態をぜんぶこの 1 つの構造体に入れる。
// エンジンはこの中身を毎フレーム自動で記録するので、あとで別窓のバーを動かすと、
// 物理の途中経過ごと過去へ戻せる (崩れる前に戻せばブロックが元どおり積み上がる)。
struct Blocks
{
	FixedVec<Block, kMaxBlocks> blocks = makeInitialBlocks();
	int   grabIndex  = -1;               // つまんでいるブロック番号 (-1 = つまんでいない)
	float grabLocalX = 0.0f, grabLocalY = 0.0f;   // つまんだ点 (ブロックのローカル座標)
	float cursorX    = 0.0f, cursorY = 0.0f;      // いまのカーソル位置
	bool  prevDown   = false;            // 前フレームでマウス左が押されていたか

	// 押した瞬間、カーソルの下にあるブロックをつまむ。上に描かれているものを優先。
	void grabAt(float mx, float my)
	{
		for (int i = static_cast<int>(blocks.size()) - 1; i >= 0; --i)
		{
			if (!pointInBlock(blocks[i], mx, my)) { continue; }
			const Block& b = blocks[i];
			const float c = std::cos(b.angle), s = std::sin(b.angle);
			const float dx = mx - b.x, dy = my - b.y;
			grabLocalX = dx * c + dy * s;    // つまんだ点をローカル座標で覚える
			grabLocalY = -dx * s + dy * c;
			grabIndex = i;
			return;
		}
	}

	// つまんだ点をカーソルへ引くバネ。重力で下がろうとするので振り子のように揺れる。
	void applyGrabSpring(float h)
	{
		Block& b = blocks[grabIndex];
		const float c = std::cos(b.angle), s = std::sin(b.angle);
		const float rx = grabLocalX * c - grabLocalY * s;   // つまんだ点の中心からのずれ
		const float ry = grabLocalX * s + grabLocalY * c;
		const float wx = b.x + rx, wy = b.y + ry;           // つまんだ点のいまの位置
		const float pvx = b.vx - b.w * ry, pvy = b.vy + b.w * rx;   // その点の速度
		const float m = blockMass(b);
		const float fx = m * (kGrabPull * (cursorX - wx) - kGrabDamp * pvx);
		const float fy = m * (kGrabPull * (cursorY - wy) - kGrabDamp * pvy);
		const float im = blockInvMass(b), iI = blockInvInertia(b);
		b.vx += im * fx * h;
		b.vy += im * fy * h;
		b.w += iI * (rx * fy - ry * fx) * h;   // 中心からずれた点を引くので回る = 振り子
	}

	// 重力とつまみのバネを速度に足す。
	void applyForces(float h)
	{
		for (Block& b : blocks) { b.vy += kGravity * h; }
		if (grabIndex >= 0) { applyGrabSpring(h); }
	}

	// 速度ぶんだけ動かし、速度が大きくなりすぎないよう上限をつける。
	void integrate(float h)
	{
		for (Block& b : blocks)
		{
			b.x += b.vx * h;
			b.y += b.vy * h;
			b.angle += b.w * h;
			b.vx = std::clamp(b.vx, -kMaxV, kMaxV);
			b.vy = std::clamp(b.vy, -kMaxV, kMaxV);
			b.w = std::clamp(b.w, -kMaxW, kMaxW);
			b.vx *= 0.999f;   // ゆっくり勢いを落として揺れを収める
			b.vy *= 0.999f;
			b.w *= 0.99f;
		}
	}

	// 接触点を 1 つ足す (相手がブロックのとき)。
	void pushPair(Contacts& out, int i, int j, float px, float py, const Sat& sat)
	{
		Contact c;
		c.a = i; c.b = j; c.px = px; c.py = py;
		c.nx = sat.nx; c.ny = sat.ny; c.depth = sat.depth;
		(void)out.push_back(c);
	}

	// 2 つのブロックの接触点を集める。相手の箱にめり込んだ角を接触点として使う。
	void addPairContacts(int i, int j, Contacts& out)
	{
		const Sat sat = satBoxes(blocks[i], blocks[j]);
		if (!sat.hit) { return; }
		const Block& A = blocks[i];
		const Block& B = blocks[j];
		float ax[4], ay[4], bx[4], by[4];
		blockCorners(A, ax, ay);
		blockCorners(B, bx, by);
		int added = 0;
		for (int k = 0; k < 4 && added < 2; ++k)
			if (pointInBlock(A, bx[k], by[k])) { pushPair(out, i, j, bx[k], by[k], sat); ++added; }
		for (int k = 0; k < 4 && added < 2; ++k)
			if (pointInBlock(B, ax[k], ay[k])) { pushPair(out, i, j, ax[k], ay[k], sat); ++added; }
		if (added == 0) { pushPair(out, i, j, (A.x + B.x) * 0.5f, (A.y + B.y) * 0.5f, sat); }
	}

	// ブロックが床・かべにめり込んでいたら接触点を足す。
	void addBoundaryContacts(int i, Contacts& out)
	{
		float cx[4], cy[4];
		blockCorners(blocks[i], cx, cy);
		for (int k = 0; k < 4; ++k)
		{
			Contact c;
			c.a = i; c.b = -1; c.px = cx[k]; c.py = cy[k];
			if (cy[k] > kFloor) { c.nx = 0.0f; c.ny = 1.0f;  c.depth = cy[k] - kFloor; (void)out.push_back(c); }
			if (cy[k] < kCeil)  { c.nx = 0.0f; c.ny = -1.0f; c.depth = kCeil - cy[k];  (void)out.push_back(c); }
			if (cx[k] < kWallL) { c.nx = -1.0f; c.ny = 0.0f; c.depth = kWallL - cx[k]; (void)out.push_back(c); }
			if (cx[k] > kWallR) { c.nx = 1.0f; c.ny = 0.0f;  c.depth = cx[k] - kWallR; (void)out.push_back(c); }
		}
	}

	// このフレームの接触点をぜんぶ集める。
	void collectContacts(Contacts& out)
	{
		const int n = static_cast<int>(blocks.size());
		for (int i = 0; i < n; ++i) { addBoundaryContacts(i, out); }
		for (int i = 0; i < n; ++i)
			for (int j = i + 1; j < n; ++j) { addPairContacts(i, j, out); }
	}

	// 接触点で速度をぶつけ合い、めり込む向きの動きを止める (＋まさつで滑りを弱める)。
	void resolveVelocity(const Contact& c)
	{
		Block& A = blocks[c.a];
		const bool st = c.b < 0;                       // 相手が床・かべなら動かない
		Block* B = st ? nullptr : &blocks[c.b];
		const float imA = blockInvMass(A), iIA = blockInvInertia(A);
		const float imB = st ? 0.0f : blockInvMass(*B);
		const float iIB = st ? 0.0f : blockInvInertia(*B);

		const float rax = c.px - A.x, ray = c.py - A.y;
		const float vax = A.vx - A.w * ray, vay = A.vy + A.w * rax;
		float rbx = 0.0f, rby = 0.0f, vbx = 0.0f, vby = 0.0f;
		if (!st) { rbx = c.px - B->x; rby = c.py - B->y; vbx = B->vx - B->w * rby; vby = B->vy + B->w * rbx; }

		const float rvx = vbx - vax, rvy = vby - vay;
		const float vn = rvx * c.nx + rvy * c.ny;
		if (vn > 0.0f) { return; }   // すでに離れていく向きなら何もしない

		const float raN = rax * c.ny - ray * c.nx;
		const float rbN = rbx * c.ny - rby * c.nx;
		const float kN = imA + imB + iIA * raN * raN + iIB * rbN * rbN;
		if (kN <= 0.0f) { return; }
		const float jn = -vn / kN;   // めり込む向きの速度を打ち消す (跳ね返りなし = 積みが落ち着く)

		const float pnx = jn * c.nx, pny = jn * c.ny;
		A.vx -= imA * pnx; A.vy -= imA * pny; A.w -= iIA * (rax * pny - ray * pnx);
		if (!st) { B->vx += imB * pnx; B->vy += imB * pny; B->w += iIB * (rbx * pny - rby * pnx); }

		const float tx = -c.ny, ty = c.nx;   // 接触面にそった向き
		const float vt = rvx * tx + rvy * ty;
		const float raT = rax * ty - ray * tx;
		const float rbT = rbx * ty - rby * tx;
		const float kT = imA + imB + iIA * raT * raT + iIB * rbT * rbT;
		if (kT <= 0.0f) { return; }
		float jt = -vt / kT;
		const float maxJ = kFriction * jn;   // まさつはぶつかりの強さまで
		jt = std::clamp(jt, -maxJ, maxJ);
		const float pfx = jt * tx, pfy = jt * ty;
		A.vx -= imA * pfx; A.vy -= imA * pfy; A.w -= iIA * (rax * pfy - ray * pfx);
		if (!st) { B->vx += imB * pfx; B->vy += imB * pfy; B->w += iIB * (rbx * pfy - rby * pfx); }
	}

	// めり込みを少しずつ押し戻す (重さに応じて分け合う)。
	void correctPosition(const Contact& c)
	{
		const float corr = std::max(c.depth - kSlop, 0.0f) * kPosPercent;
		if (corr <= 0.0f) { return; }
		Block& A = blocks[c.a];
		const bool st = c.b < 0;
		const float imA = blockInvMass(A);
		const float imB = st ? 0.0f : blockInvMass(blocks[c.b]);
		const float sum = imA + imB;
		if (sum <= 0.0f) { return; }
		A.x -= c.nx * (corr * imA / sum);
		A.y -= c.ny * (corr * imA / sum);
		if (!st)
		{
			Block& B = blocks[c.b];
			B.x += c.nx * (corr * imB / sum);
			B.y += c.ny * (corr * imB / sum);
		}
	}

	// ほとんど止まったブロックの微振動を消す (つまんでいるものは除く)。
	void settleTiny()
	{
		for (int i = 0; i < static_cast<int>(blocks.size()); ++i)
		{
			if (i == grabIndex) { continue; }
			Block& b = blocks[i];
			if (std::fabs(b.vx) < 8.0f && std::fabs(b.vy) < 8.0f && std::fabs(b.w) < 0.05f)
			{
				b.vx = 0.0f; b.vy = 0.0f; b.w = 0.0f;
			}
		}
	}

	// 1 フレームを何回かに分けて解く (細かく解くほど積みが安定する)。
	void step(float dt)
	{
		const float h = dt / kSubsteps;
		for (int sub = 0; sub < kSubsteps; ++sub)
		{
			applyForces(h);
			integrate(h);
			Contacts contacts;
			collectContacts(contacts);
			for (int it = 0; it < kVelIters; ++it)
				for (const Contact& c : contacts) { resolveVelocity(c); }
			for (const Contact& c : contacts) { correctPosition(c); }
		}
		settleTiny();
	}

	void update(Input in, float dt)
	{
		const float mx = in.mouseX(), my = in.mouseY();
		const bool  down = in.mouseDown(0);
		if (down && !prevDown) { grabAt(mx, my); }   // 押した瞬間につまむ
		if (!down) { grabIndex = -1; }               // 離したら手をはなす (そのまま落ちる)
		cursorX = mx; cursorY = my;
		prevDown = down;

		float t = dt;
		if (t > 0.033f) { t = 0.033f; }   // 処理が重い瞬間でも一気に飛ばさない
		step(t);
	}

	void draw(Screen& s) const
	{
		s.fillScreen(theme::kPaper);
		s.drawLine(Vec2{0.0f, kFloor}, Vec2{kScreenW, kFloor}, theme::kFrame, 2.0f);   // 床

		for (int i = 0; i < static_cast<int>(blocks.size()); ++i)
		{
			const Block& b = blocks[i];
			s.pushRotation(b.angle, b.x, b.y);   // ブロックの中心を軸に回して描く
			s.drawRect(b.x - b.hw, b.y - b.hh, b.hw * 2.0f, b.hh * 2.0f, kPalette[b.color % 6]);
			s.drawRectFrame(Rect{b.x - b.hw, b.y - b.hh, b.hw * 2.0f, b.hh * 2.0f}, theme::kInk, 2.0f);
			s.popTransform();
		}

		if (grabIndex >= 0)   // つまんでいる点とカーソルを線で結ぶ (分かりやすく)
		{
			const Block& b = blocks[grabIndex];
			const float c = std::cos(b.angle), sn = std::sin(b.angle);
			const float wx = b.x + (grabLocalX * c - grabLocalY * sn);
			const float wy = b.y + (grabLocalX * sn + grabLocalY * c);
			s.drawLine(Vec2{wx, wy}, Vec2{cursorX, cursorY}, theme::kSubtle, 1.5f);
			s.fillCircle(wx, wy, 4.0f, theme::kInk);
		}

		chapterTitle(s, "Rewind");
		chapterControls(s, "マウス: ブロックをつまんで積む　（崩れたら別窓のバーで巻き戻せる）");
	}
};

// DLL の入口。この構造体はポインタを持たない単純なデータなので、
// エンジンが毎フレームまるごと記録して、あとから過去へ戻せる。
// 実行:  mitiru_host.exe rewind/rewind.dll --inspect rewind
MITIRU_GAME(Blocks);
MITIRU_REWIND_BUFFER(600);   // この章は 10 秒分 (60fps) さかのぼれるようにする
