#pragma once

/// @file LocalTransform.hpp
/// @brief 親子付き 2D/3D transform の flat POD (`LocalTransform` / `LocalTransform3D`) と
/// 一括解決 `resolveWorld`。GameMemory の 1 フィールドとして持てる「親子」の書き方を提示する
/// (`docs/IMPROVEMENTS_FROM_ENGINES_2026_09_17.md` ★1-5)。通知駆動 (callback) にはしない。
/// DLL 境界に callback を増やすと ADR 0005 の enumerated set が大きくなるのと、巻き戻しが
/// memcpy 1 回で済む性質 (毎フレーム一括解決ならおかしくならない) を優先するため。

#include <cmath>
#include <cstdint>

#include <mitiru/debug/WarnOnce.hpp>

namespace mitiru
{

/// @brief `LocalTransform::inheritMask` の各ビット。OR で組み合わせる。
enum LocalTransformInherit : std::uint8_t
{
	InheritPos   = 1,
	InheritRot   = 2,
	InheritScale = 4,
};

/// @brief 2D の親子付き transform。`parent` は同じ配列内の添字、-1 (負値) は root。
struct LocalTransform
{
	float        x = 0.0f, y = 0.0f;
	float        rot = 0.0f;             ///< 度
	float        sx = 1.0f, sy = 1.0f;
	std::int16_t parent = -1;
	std::uint8_t inheritMask = InheritPos | InheritRot | InheritScale;
	std::uint8_t _pad[1] = {};   ///< 暗黙の詰め物を残さない (GameMemory はバイト単位で比べられる)
};

/// @brief 3D 版。形は `LocalTransform` と同じ (フィールドが増えるだけ)。
struct LocalTransform3D
{
	float        x = 0.0f, y = 0.0f, z = 0.0f;
	float        rotX = 0.0f, rotY = 0.0f, rotZ = 0.0f;   ///< 度
	float        sx = 1.0f, sy = 1.0f, sz = 1.0f;
	std::int16_t parent = -1;
	std::uint8_t inheritMask = InheritPos | InheritRot | InheritScale;
	std::uint8_t _pad[1] = {};   ///< 暗黙の詰め物を残さない (GameMemory はバイト単位で比べられる)
};

/// @brief `resolveWorld` が書き出す解決済み世界座標。
struct WorldTransform
{
	float x = 0.0f, y = 0.0f;
	float rot = 0.0f;
	float sx = 1.0f, sy = 1.0f;
};

struct WorldTransform3D
{
	float x = 0.0f, y = 0.0f, z = 0.0f;
	float rotX = 0.0f, rotY = 0.0f, rotZ = 0.0f;
	float sx = 1.0f, sy = 1.0f, sz = 1.0f;
};

/// @brief `resolveWorld` が扱える配列長の上限 (スタック上の作業配列サイズ)。
inline constexpr int kLocalTransformMax = 256;

namespace detail
{

[[nodiscard]] inline WorldTransform combineLocal(const WorldTransform& parent, const LocalTransform& local) noexcept
{
	const float psx = (local.inheritMask & InheritScale) ? parent.sx : 1.0f;
	const float psy = (local.inheritMask & InheritScale) ? parent.sy : 1.0f;
	const float prot = (local.inheritMask & InheritRot) ? parent.rot : 0.0f;

	WorldTransform w;
	w.sx  = psx * local.sx;
	w.sy  = psy * local.sy;
	w.rot = prot + local.rot;

	if (local.inheritMask & InheritPos)
	{
		// 親の回転・拡縮を反映してから親の位置に足す (子は親の座標系で動く)
		const float rad = prot * (3.14159265358979323846f / 180.0f);
		const float c = std::cos(rad), s = std::sin(rad);
		const float lx = local.x * psx, ly = local.y * psy;
		w.x = parent.x + (lx * c - ly * s);
		w.y = parent.y + (lx * s + ly * c);
	}
	else
	{
		w.x = local.x;
		w.y = local.y;
	}
	return w;
}

/// @brief 深さ優先で親を先に解決する。`state` は 0=未処理/1=処理中/2=完了。
/// 処理中の添字を再訪問したら循環と判定し、root 扱い (親なし) で確定させて止める。
inline void resolveOne(const LocalTransform* arr, int n, WorldTransform* out, std::uint8_t* state, int i)
{
	if (state[i] == 2) { return; }
	if (state[i] == 1)
	{
		mitiru::debug::warnOnce("transform.resolveWorld.cycle",
			"LocalTransform.parent が循環している → root 扱いにする");
		out[i]   = combineLocal(WorldTransform{}, arr[i]);
		state[i] = 2;
		return;
	}
	state[i] = 1;

	const LocalTransform& local = arr[i];
	const int p = local.parent;
	WorldTransform parentWorld{};   // 親なし = 単位変換
	if (p >= 0 && p < n && p != i)
	{
		resolveOne(arr, n, out, state, p);
		if (state[i] == 2) { return; }  // 親の解決中に循環として root 確定済み。ここで親付きに上書きしない
		parentWorld = out[p];
	}
	else if (p >= n)
	{
		mitiru::debug::warnOnce("transform.resolveWorld.range",
			"LocalTransform.parent が範囲外 → root 扱いにする");
	}

	out[i]   = combineLocal(parentWorld, local);
	state[i] = 2;
}

}  // namespace detail

/// @brief `arr[0..n)` を親→子の順で解決し `out[0..n)` へ書く。配列内の並び順は問わない
/// (子が親より先に置かれていてもよい)。同じ入力なら常に同じ結果になる (決定論)。
inline void resolveWorld(const LocalTransform* arr, int n, WorldTransform* out) noexcept
{
	if (arr == nullptr || out == nullptr || n <= 0) { return; }
	if (n > kLocalTransformMax)
	{
		mitiru::debug::warnOnceFix("transform.resolveWorld.overflow",
			"LocalTransform の要素数が上限を超えている", "kLocalTransformMax = 256",
			"配列を分割して呼ぶか kLocalTransformMax を上げる");
		n = kLocalTransformMax;
	}

	std::uint8_t state[kLocalTransformMax] = {};
	for (int i = 0; i < n; ++i) { detail::resolveOne(arr, n, out, state, i); }
}

}  // namespace mitiru
