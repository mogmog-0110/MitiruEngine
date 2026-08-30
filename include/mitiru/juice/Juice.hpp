#pragma once

/// @file Juice.hpp
/// @brief 「ジュース」軽量コンポーネント。Particles / Shake / HitStop。
/// @details いずれもゲーム側が毎フレーム update(dt) を呼び、必要なら draw / offset を取る形。
///          全部 header-only / 固定プールで alloc-free。アロケーション無しの hot path 安全。

#include <algorithm>
#include <cmath>
#include <vector>

#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/core/Screen.hpp>

namespace mitiru::juice
{

// ─────────────────────────────────────────────────────────────────────────────
// Particles: 固定プール、life で fade、square で draw。
// ─────────────────────────────────────────────────────────────────────────────

struct Particle
{
	float       x = 0.0f, y = 0.0f;
	float       vx = 0.0f, vy = 0.0f;
	float       life = 0.0f;       ///< 残量秒
	float       lifeMax = 0.0f;    ///< 初期値 (α fade 用)
	float       size = 1.0f;
	sgc::Colorf color{1.0f, 1.0f, 1.0f, 1.0f};
	bool        alive = false;
};

class Particles
{
public:
	explicit Particles(int capacity = 256) { m_pool.resize(static_cast<std::size_t>(capacity)); }

	/// @brief 単発 spawn。空きスロットが無ければ何もしない。
	void spawn(float x, float y, float vx, float vy, float life, float size,
	           const sgc::Colorf& color) noexcept
	{
		for (auto& p : m_pool)
		{
			if (!p.alive)
			{
				p.x = x; p.y = y; p.vx = vx; p.vy = vy;
				p.life = life; p.lifeMax = life;
				p.size = size; p.color = color; p.alive = true;
				return;
			}
		}
	}

	/// @brief 放射状 burst (爆発用)。count 個を等角で speed で外向きに spawn。
	void burst(float x, float y, int count, float speed, float life, float size,
	           const sgc::Colorf& color) noexcept
	{
		if (count <= 0) { return; }
		const float two_pi = 6.28318530718f;
		for (int i = 0; i < count; ++i)
		{
			const float a = (static_cast<float>(i) / static_cast<float>(count)) * two_pi;
			spawn(x, y, std::cos(a) * speed, std::sin(a) * speed, life, size, color);
		}
	}

	void update(float dt) noexcept
	{
		for (auto& p : m_pool)
		{
			if (!p.alive) { continue; }
			p.life -= dt;
			if (p.life <= 0.0f) { p.alive = false; continue; }
			p.x += p.vx * dt;
			p.y += p.vy * dt;
		}
	}

	/// @brief 生存パーティクルを camera 補正して描く (square + life fade で α 減衰)。
	void draw(Screen& screen, float camX = 0.0f, float camY = 0.0f) const
	{
		for (const auto& p : m_pool)
		{
			if (!p.alive) { continue; }
			const float a = (p.lifeMax > 0.0f) ? (p.life / p.lifeMax) : 1.0f;
			sgc::Colorf c = p.color;
			c.a *= a;
			screen.drawRect(sgc::Rectf{p.x - camX - p.size * 0.5f,
			                           p.y - camY - p.size * 0.5f,
			                           p.size, p.size}, c);
		}
	}

	[[nodiscard]] int aliveCount() const noexcept
	{
		int n = 0;
		for (const auto& p : m_pool) { if (p.alive) { ++n; } }
		return n;
	}

	[[nodiscard]] int capacity() const noexcept { return static_cast<int>(m_pool.size()); }

private:
	std::vector<Particle> m_pool;
};

// ─────────────────────────────────────────────────────────────────────────────
// Shake: trauma ベース camera shake。
// ─────────────────────────────────────────────────────────────────────────────

class Shake
{
public:
	float decayPerSec = 1.5f;  ///< trauma の減衰率 (1.0 で 1 秒間 1.0→0)
	float magnitude   = 16.0f; ///< trauma=1 時の最大オフセット (px)

	/// @brief trauma を加算 (clamp 0..1)。被弾・爆発等の game event で呼ぶ。
	void pushTrauma(float add) noexcept
	{
		m_trauma = std::clamp(m_trauma + add, 0.0f, 1.0f);
	}

	void update(float dt) noexcept
	{
		m_trauma = std::max(0.0f, m_trauma - decayPerSec * dt);
		// 擬似乱数 (deterministic LCG): replay に乗せても再現される。
		m_lcg = m_lcg * 1664525u + 1013904223u;
		const float rx = static_cast<float>((m_lcg >> 16) & 0x7FFF) / 32767.0f * 2.0f - 1.0f;
		m_lcg = m_lcg * 1664525u + 1013904223u;
		const float ry = static_cast<float>((m_lcg >> 16) & 0x7FFF) / 32767.0f * 2.0f - 1.0f;
		// trauma^2 で perceived 強度を線形に近づける。
		const float k = m_trauma * m_trauma;
		m_offset = sgc::Vec2f{rx * magnitude * k, ry * magnitude * k};
	}

	[[nodiscard]] sgc::Vec2f offset() const noexcept { return m_offset; }
	[[nodiscard]] float trauma() const noexcept       { return m_trauma; }

private:
	float          m_trauma = 0.0f;
	sgc::Vec2f     m_offset {0.0f, 0.0f};
	std::uint32_t  m_lcg = 0xCAFEBABEu;
};

// ─────────────────────────────────────────────────────────────────────────────
// HitStop: 一定秒間ゲーム時間を止める (game 側が active() を見て update を skip する)。
// ─────────────────────────────────────────────────────────────────────────────

class HitStop
{
public:
	/// @brief dur 秒の hit-stop を開始 (既存より長ければ延長、短ければ無視)。
	void trigger(float durSec) noexcept
	{
		if (durSec > m_remaining) { m_remaining = durSec; }
	}

	void update(float dt) noexcept
	{
		if (m_remaining > 0.0f) { m_remaining -= dt; }
		if (m_remaining < 0.0f) { m_remaining = 0.0f; }
	}

	[[nodiscard]] bool  active() const noexcept   { return m_remaining > 0.0f; }
	[[nodiscard]] float remaining() const noexcept { return m_remaining; }

private:
	float m_remaining = 0.0f;
};

}  // namespace mitiru::juice
