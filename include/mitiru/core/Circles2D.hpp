#pragma once

/// @file Circles2D.hpp
/// @brief 箱の中に積む円の解決 (重力・壁・重なりの押し出し)。固定長・trivially copyable。
/// @details 落ちもの・玉転がしのような「円を箱に入れるだけ」の遊びを、GameMemory に
///          置ける形で書くための型。回転・任意形状・関節・連続衝突判定は持たない。
///
/// physics/PhysicsEngine2D.hpp の `PhysicsWorld2D` は `unordered_map` と `std::function` を
/// 持つため GameMemory に入らない。物理を使ったとたん、その game だけ time-travel と
/// replay-as-test (軸 2 / 軸 4) から外れる。この型はその穴を塞ぐためにある。
/// 回転や関節が要るなら `PhysicsWorld2D` を GameMemory の外に置く。
///
/// - 座標系は y 下向き正 (screen 系)。`floor` が下端、`ceiling` が上端。
/// - 反発は速さで切り替える。`restSpeed` を下回る接触は弾ませない。位置の補正と速度を
///   別々に解いているので、弾ませ続けると積んだ山がいつまでも静止しない。
/// - 押し出しの直後にも箱へ収める。1 フレームの頭だけで収めると、下の円が床の下へ
///   押し込まれ、次のフレームで床が押し返して山が上下に跳ね続ける。
/// - `resting` は床か他の円に触れた印。落下中の円と積み上がった円を区別するために使う。
///   静止しているかを速さで測ってはいけない (位置が動かなくても速度は残る)。
/// - 合体や消滅はゲーム側の関心事なので、`step` に呼び出し可能物を毎フレーム渡す形にした。
///   この型が callable を持つと trivially copyable でなくなる。
/// - 決定論: 走査順・演算順は固定。同じ入力なら結果は bit 一致 (リプレイ前提)。

#include <cmath>
#include <cstdint>
#include <type_traits>

namespace mitiru
{

/// @brief 円 1 個。`tag` の意味はゲーム側が決める (段・種類など)。
struct Circle2D
{
	float        x  = 0.0f;
	float        y  = 0.0f;
	float        vx = 0.0f;
	float        vy = 0.0f;
	float        r  = 0.0f;
	std::int32_t tag = 0;
	bool         alive   = false;
	bool         resting = false;  ///< 床か他の円に触れた。落下中と区別する
};

/// @brief 箱と解き方の設定。毎フレーム渡すので GameMemory に持たなくてよい。
struct Circles2DConfig
{
	float gravity   = 900.0f;
	float left      = 0.0f;
	float right     = 1280.0f;
	float floor     = 720.0f;
	float ceiling   = -1.0e9f;    ///< 既定では天井なし
	float restitution   = 0.2f;
	float restSpeed     = 60.0f;  ///< これを下回る接触は弾ませない
	float floorFriction = 0.92f;  ///< 床に触れた円の横速度に毎フレーム掛ける
	int   iterations    = 4;      ///< 重なりを解く回数。1 だと積み上げたときに沈む
};

/// @brief 固定長の円の集まり。`N` が枠の数。
template <int N>
struct Circles2D
{
	static_assert(N > 0, "Circles2D: 枠は 1 個以上");

	Circle2D item[N]{};
	int      count = 0;   ///< 使った枠の上限。走査を短く保つ値で、生存数ではない

	void clear() noexcept
	{
		for (Circle2D& c : item) { c = Circle2D{}; }
		count = 0;
	}

	/// @brief 空き枠に 1 個置く。枠が無ければ -1 を返す。
	int spawn(float x, float y, float r, std::int32_t tag = 0) noexcept
	{
		for (int i = 0; i < N; ++i)
		{
			if (item[i].alive) { continue; }
			item[i] = Circle2D{x, y, 0.0f, 0.0f, r, tag, true, false};
			if (i >= count) { count = i + 1; }
			return i;
		}
		return -1;
	}

	void kill(int i) noexcept
	{
		if (i >= 0 && i < N) { item[i].alive = false; }
	}

	[[nodiscard]] int live() const noexcept
	{
		int n = 0;
		for (const Circle2D& c : item) { if (c.alive) { ++n; } }
		return n;
	}

	/// @brief 1 フレーム進める。
	/// @param onOverlap `(int a, int b)` を受け bool を返す呼び出し可能物。true を返した組は
	///        押し出さない (消す・合体するなど、ゲーム側で処理した扱いになる)。
	template <typename OnOverlap>
	void step(const Circles2DConfig& cfg, float dt, OnOverlap&& onOverlap) noexcept
	{
		integrate(cfg, dt);
		for (int pass = 0; pass < cfg.iterations; ++pass)
		{
			resolve(cfg, onOverlap);
		}
	}

	/// @brief 重なりをすべて押し出すだけの `step`。
	void step(const Circles2DConfig& cfg, float dt) noexcept
	{
		step(cfg, dt, [](int, int) { return false; });
	}

private:
	/// @brief 速さが小さい接触は弾ませない。跳ね続けて静止しなくなるのを防ぐ。
	static float bounce(const Circles2DConfig& cfg, float v) noexcept
	{
		return (std::fabs(v) < cfg.restSpeed) ? 0.0f : v * cfg.restitution;
	}

	/// @brief 位置だけ箱の中へ戻す。押し出しの直後にも要る。
	static void clampToBox(const Circles2DConfig& cfg, Circle2D& c) noexcept
	{
		if (c.x - c.r < cfg.left)    { c.x = cfg.left + c.r; }
		if (c.x + c.r > cfg.right)   { c.x = cfg.right - c.r; }
		if (c.y - c.r < cfg.ceiling) { c.y = cfg.ceiling + c.r; }
		if (c.y + c.r > cfg.floor)   { c.y = cfg.floor - c.r; }
	}

	void integrate(const Circles2DConfig& cfg, float dt) noexcept
	{
		for (int i = 0; i < count; ++i)
		{
			Circle2D& c = item[i];
			if (!c.alive) { continue; }
			c.vy += cfg.gravity * dt;
			c.x += c.vx * dt;
			c.y += c.vy * dt;

			if (c.x - c.r < cfg.left)  { c.x = cfg.left + c.r;  c.vx = bounce(cfg, -c.vx); }
			if (c.x + c.r > cfg.right) { c.x = cfg.right - c.r; c.vx = bounce(cfg, -c.vx); }
			if (c.y - c.r < cfg.ceiling) { c.y = cfg.ceiling + c.r; c.vy = bounce(cfg, -c.vy); }
			if (c.y + c.r > cfg.floor)
			{
				c.y = cfg.floor - c.r;
				c.vy = bounce(cfg, -c.vy);
				c.vx *= cfg.floorFriction;
				c.resting = true;
			}
		}
	}

	/// @brief めり込んだ組を押し離す。ゲーム側が引き取った組は触らない。
	template <typename OnOverlap>
	void resolve(const Circles2DConfig& cfg, OnOverlap& onOverlap) noexcept
	{
		for (int i = 0; i < count; ++i)
		{
			Circle2D& a = item[i];
			if (!a.alive) { continue; }
			for (int j = i + 1; j < count; ++j)
			{
				Circle2D& b = item[j];
				if (!b.alive) { continue; }
				const float dx = b.x - a.x;
				const float dy = b.y - a.y;
				const float sum = a.r + b.r;
				const float d2 = dx * dx + dy * dy;
				if (d2 >= sum * sum) { continue; }
				if (onOverlap(i, j)) { continue; }
				separate(cfg, a, b, dx, dy, d2, sum);
			}
		}
	}

	/// @brief 2 つを押し離し、法線方向の速度を揃える。
	static void separate(const Circles2DConfig& cfg, Circle2D& a, Circle2D& b,
	                     float dx, float dy, float d2, float sum) noexcept
	{
		float d = std::sqrt(d2);
		if (d < 0.0001f) { d = 0.0001f; }
		const float nx = dx / d;
		const float ny = dy / d;
		const float push = (sum - d) * 0.5f;
		a.x -= nx * push; a.y -= ny * push;
		b.x += nx * push; b.y += ny * push;
		clampToBox(cfg, a);
		clampToBox(cfg, b);
		a.resting = true;
		b.resting = true;

		const float rel = (b.vx - a.vx) * nx + (b.vy - a.vy) * ny;
		if (rel > 0.0f) { return; }
		const float e = (std::fabs(rel) < cfg.restSpeed) ? 0.0f : cfg.restitution;
		const float imp = -(1.0f + e) * rel * 0.5f;
		a.vx -= nx * imp; a.vy -= ny * imp;
		b.vx += nx * imp; b.vy += ny * imp;
	}
};

static_assert(std::is_trivially_copyable_v<Circle2D>,
              "Circle2D は GameMemory に置ける必要がある");
static_assert(std::is_trivially_copyable_v<Circles2D<4>>,
              "Circles2D は GameMemory に置ける必要がある");

}  // namespace mitiru
