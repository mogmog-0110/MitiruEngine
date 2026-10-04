#pragma once

/// @file ActionDebugDraw.hpp
/// @brief 近接攻撃を調整するときに、向き・攻撃の円錐・ソフトロックの選んだ相手・判定の形を線で重ねる。
/// @details 線は hud.debugLine で host に頼み、host がゲームの絵の上へ投影して描く。シミュレーションの状態は
///          何も変えないので、出しても消しても巻き戻しと再生の結果は同じ。既定では出さず、Debug ビルドで
///          ゲームが選んだキー (既定 F3) を押した間だけ出す。Release ビルド (NDEBUG) では何も積まないので、
///          配布物の画面と撮った絵には出ない。線は 1 フレームに 256 本まで (ほかの debugLine と合わせて)。

#include <cmath>
#include <cstdint>

#include <mitiru/action/HitVolumes.hpp>
#include <mitiru/action/SoftLock.hpp>
#include <mitiru/module/Game.hpp>

namespace mitiru::action
{

#if defined(NDEBUG) && !defined(MITIRU_ACTION_DEBUG_DRAW)
inline constexpr bool kDebugDrawBuilt = false;
#else
inline constexpr bool kDebugDrawBuilt = true;
#endif

namespace debug_draw_detail
{
	inline constexpr int kRingSegments = 12;

	/// @brief center を中心に、直交する単位ベクトル u, v が張る面に半径 radius の円を引く
	inline void ring(Hud hud, const Vec3& center, const Vec3& u, const Vec3& v, float radius, Color c)
	{
		Vec3 prev = center + u * radius;
		for (int i = 1; i <= kRingSegments; ++i)
		{
			const float a = 2.0f * kPi * static_cast<float>(i) / kRingSegments;
			const Vec3 next = center + (u * std::cos(a) + v * std::sin(a)) * radius;
			hud.debugLine(prev, next, c);
			prev = next;
		}
	}

	/// @brief up まわりに forward を angle (ラジアン) 回した向き (どちらも up に垂直な単位ベクトル)
	[[nodiscard]] inline Vec3 turn(const Vec3& forward, const Vec3& up, float angle) noexcept
	{
		return forward * std::cos(angle) + cross(up, forward) * std::sin(angle);
	}
}  // namespace debug_draw_detail

/// @brief GameMemory に置く 4 バイトの切り替え。描く関数はどれも、出していなければ何もしない
struct DebugView
{
	std::uint8_t on = 0;
	std::uint8_t _pad[3]{};

	/// @brief key で出す・消すを切り替える。F7〜F12 は host が使うので避ける
	void toggle(Input in, Key key = Key::F3) noexcept
	{
		if (in.pressed(key)) { on = static_cast<std::uint8_t>(on ^ 1u); }
	}

	[[nodiscard]] bool shown() const noexcept { return kDebugDrawBuilt && on != 0; }

	/// @brief position から forward の向きへ長さ length の矢印 (up 成分は落とす)
	void facing(Hud hud, const Vec3& position, const Vec3& forward, float length = 1.0f,
	            Color c = hex(0x2EC4F0), const Vec3& up = {0.0f, 1.0f, 0.0f}) const
	{
		if (!shown()) { return; }
		const Vec3 f = normalizeOr(projectOnPlane(forward, up), anyPerpendicular(up));
		const Vec3 tip = position + f * length;
		const Vec3 side = cross(up, f) * (0.12f * length);
		hud.debugLine(position, tip, c);
		hud.debugLine(tip, tip - f * (0.25f * length) + side, c);
		hud.debugLine(tip, tip - f * (0.25f * length) - side, c);
	}

	/// @brief origin から forward へ、半角 halfAngleDeg・長さ range の扇を up に垂直な面に引く (攻撃の届く範囲)
	void cone(Hud hud, const Vec3& origin, const Vec3& forward, float range, float halfAngleDeg,
	          Color c = hex(0xFFC83D), const Vec3& up = {0.0f, 1.0f, 0.0f}) const
	{
		if (!shown()) { return; }
		namespace d = debug_draw_detail;
		const Vec3 n = normalizeOr(up, Vec3{0.0f, 1.0f, 0.0f});
		const Vec3 f = normalizeOr(projectOnPlane(forward, n), anyPerpendicular(n));
		const float half = halfAngleDeg * kDegToRad;
		Vec3 prev = origin + d::turn(f, n, -half) * range;
		hud.debugLine(origin, prev, c);
		for (int i = 1; i <= d::kRingSegments; ++i)
		{
			const float a = -half + 2.0f * half * static_cast<float>(i) / d::kRingSegments;
			const Vec3 next = origin + d::turn(f, n, a) * range;
			hud.debugLine(prev, next, c);
			prev = next;
		}
		hud.debugLine(origin, prev, c);
	}

	/// @brief softLock に渡した値と結果から、届く扇・選んだ相手への線と輪・向き直った後の前を引く
	void softLock(Hud hud, const Vec3& origin, const Vec3& forward, const Vec3* targets, int count,
	              const SoftLockParams& p, const SoftLockResult& r) const
	{
		if (!shown()) { return; }
		cone(hud, origin, forward, p.range, p.halfAngleDeg, hex(0xFFC83D), p.up);
		facing(hud, origin, r.forward, p.range * 0.5f, hex(0x2EC4F0), p.up);
		if (targets == nullptr || r.target < 0 || r.target >= count) { return; }
		const Vec3 n = normalizeOr(p.up, Vec3{0.0f, 1.0f, 0.0f});
		const Vec3 u = anyPerpendicular(n);
		hud.debugLine(origin, targets[r.target], hex(0xFF3B5C));
		debug_draw_detail::ring(hud, targets[r.target], u, cross(n, u), 0.5f, hex(0xFF3B5C));
	}

	/// @brief 判定の形 (球は 3 つの輪、カプセルは両端の輪と 4 本の辺、箱は 12 本の辺)
	void volume(Hud hud, const HitVolume& v, Color c = hex(0xE040FB)) const
	{
		if (!shown()) { return; }
		switch (v.kind)
		{
		case VolumeKind::Sphere: sphere(hud, v.a, v.radius, c); break;
		case VolumeKind::Capsule: capsule(hud, v, c); break;
		case VolumeKind::Box: box(hud, v, c); break;
		}
	}

private:
	static void sphere(Hud hud, const Vec3& center, float radius, Color c)
	{
		const Vec3 x{1.0f, 0.0f, 0.0f}, y{0.0f, 1.0f, 0.0f}, z{0.0f, 0.0f, 1.0f};
		debug_draw_detail::ring(hud, center, x, y, radius, c);
		debug_draw_detail::ring(hud, center, x, z, radius, c);
		debug_draw_detail::ring(hud, center, y, z, radius, c);
	}

	static void capsule(Hud hud, const HitVolume& v, Color c)
	{
		const Vec3 axis = normalizeOr(v.b - v.a, Vec3{0.0f, 1.0f, 0.0f});
		const Vec3 u = anyPerpendicular(axis);
		const Vec3 w = cross(axis, u);
		debug_draw_detail::ring(hud, v.a, u, w, v.radius, c);
		debug_draw_detail::ring(hud, v.b, u, w, v.radius, c);
		for (const Vec3& side : {u, w, u * -1.0f, w * -1.0f})
		{
			hud.debugLine(v.a + side * v.radius, v.b + side * v.radius, c);
		}
		hud.debugLine(v.a - axis * v.radius, v.b + axis * v.radius, c);   // 丸い端の先まで
	}

	static void box(Hud hud, const HitVolume& v, Color c)
	{
		Vec3 corners[8];
		for (int i = 0; i < 8; ++i) { corners[i] = v.a + v.rotation.rotate(volume_detail::corner(v.halfExtents, i)); }
		for (int e = 0; e < 24; e += 2)
		{
			hud.debugLine(corners[volume_detail::kBoxEdges[e]], corners[volume_detail::kBoxEdges[e + 1]], c);
		}
	}
};

}  // namespace mitiru::action
