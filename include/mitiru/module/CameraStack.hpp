#pragma once

/// @file CameraStack.hpp
/// @brief カメラの「積む/降ろす」スタック `CameraStack<N>` と、切替時のフレーム単位ブレンド
/// `resolveCamera`。GameMemory の 1 フィールドとして持てる形にする
/// (`docs/IMPROVEMENTS_FROM_ENGINES_2026_09_17.md` ★1-7)。カメラ状態を engine 側に持たせると
/// ADR 0017 (GameMemory が唯一の state) が成り立たなくなり巻き戻しが再現しなくなるため、flat POD のまま
/// GameMemory に置き、`FrameIntents` は増やさない (ABI 据え置き)。draw 側は `resolveCamera` の
/// 戻り値をそのまま `s.applyCamera(...)` に渡すだけでよい。

#include <cstdint>

namespace mitiru
{

/// @brief スタックに積む 1 段。`priority` は push/pop の照合キー (HE2 の viewport/owner id 相当)。
struct CameraEntry
{
	std::uint8_t kind = 0;       ///< 呼び出し側が決める種別タグ (通常/演出/デバッグ等。エンジンは見ない)
	std::uint8_t priority = 0;   ///< push した側の識別子。popByOwner や二重 push 判定に使う
	std::uint8_t _pad[2] = {};   ///< 暗黙の詰め物を残さない (GameMemory はバイト単位で比べられる)
	float        x = 0.0f, y = 0.0f;
	float        zoom = 1.0f;
	float        rot = 0.0f;
	float        blendSec = 0.0f;   ///< このカメラが top になったとき、直前の値からここへ寄せる秒数
};

namespace detail
{
[[nodiscard]] inline bool sameCameraEntry(const CameraEntry& a, const CameraEntry& b) noexcept
{
	return a.kind == b.kind && a.priority == b.priority && a.x == b.x && a.y == b.y &&
	       a.zoom == b.zoom && a.rot == b.rot && a.blendSec == b.blendSec;
}
}  // namespace detail

/// @brief 1 フレーム分の解決済みカメラ (`Camera` と同形。DrawCommands 側の型を増やさないよう独立させる)。
struct CameraFrame
{
	float x = 0.0f, y = 0.0f;
	float zoom = 1.0f;
	float rot = 0.0f;
};

/// @brief 固定容量 N のカメラスタック (flat POD。GameMemory にそのまま置ける)。
/// ブレンド用の状態 (`current` 以下) もここに同居させる。別 struct に分けると
/// 「スタックだけ巻き戻って current が古いフレームの値のまま」という不整合が起こるため。
template <int N = 8>
struct CameraStack
{
	CameraEntry  entries[N]{};
	std::uint8_t count = 0;
	std::uint8_t _pad[3] = {};

	CameraFrame  current{};        ///< 直近の resolveCamera の戻り値 (次のブレンドの起点)
	CameraFrame  blendFrom{};
	float        blendRemain = 0.0f;
	float        blendTotal  = 0.0f;
	std::uint8_t lastTopPriority = 0;
	std::uint8_t hasLastTop = 0;   ///< bool の POD 表現 (0/1)
	std::uint8_t _pad2[2] = {};

	/// @brief 末尾に積む。満杯なら何もせず false (「二重に積まない」は呼び出し側が Volume の pushed フラグ等で
	/// 保証する前提。ここでは容量超過だけを見る)。
	[[nodiscard]] bool pushCamera(const CameraEntry& e) noexcept
	{
		if (count >= static_cast<std::uint8_t>(N)) { return false; }
		entries[count] = e;
		++count;
		return true;
	}

	/// @brief 末尾を降ろす。空なら no-op。
	void popCamera() noexcept
	{
		if (count > 0) { --count; }
	}

	/// @brief `priority` が一致する entry のうち一番上を取り除き、上に載っていた分を詰める。無ければ false。
	bool removeCamera(std::uint8_t priority) noexcept
	{
		for (int i = static_cast<int>(count) - 1; i >= 0; --i)
		{
			if (entries[i].priority != priority) { continue; }
			for (int k = i; k + 1 < static_cast<int>(count); ++k) { entries[k] = entries[k + 1]; }
			--count;
			return true;
		}
		return false;
	}

	/// @brief 全フィールドが `e` と一致する entry のうち一番上を取り除く。Volume のように「自分が積んだ
	/// 分だけ降ろしたい」側が使う。同じ内容の entry が複数あれば、どれを外しても残りは同じになる。
	bool removeCamera(const CameraEntry& e) noexcept
	{
		for (int i = static_cast<int>(count) - 1; i >= 0; --i)
		{
			if (!detail::sameCameraEntry(entries[i], e)) { continue; }
			for (int k = i; k + 1 < static_cast<int>(count); ++k) { entries[k] = entries[k + 1]; }
			--count;
			return true;
		}
		return false;
	}

	[[nodiscard]] const CameraEntry* topCamera() const noexcept
	{
		return count > 0 ? &entries[count - 1] : nullptr;
	}
};

namespace detail
{
[[nodiscard]] inline CameraFrame lerpCameraFrame(const CameraFrame& a, const CameraFrame& b, float t) noexcept
{
	return CameraFrame{
		a.x + (b.x - a.x) * t,
		a.y + (b.y - a.y) * t,
		a.zoom + (b.zoom - a.zoom) * t,
		a.rot + (b.rot - a.rot) * t,
	};
}
}  // namespace detail

/// @brief top を 1 フレーム分だけ解決する。top が入れ替わった (push/pop で priority が
/// 変わった) フレームでその top の `blendSec` を使ってブレンドを開始し、以後は経過時間で
/// 補間する。同じ状態への再呼び出しは常に同じ値を返す (決定論。dt の積算だけが状態)。
template <int N>
[[nodiscard]] CameraFrame resolveCamera(CameraStack<N>& stack, float dt) noexcept
{
	const CameraEntry* top = stack.topCamera();
	const CameraFrame target = top ? CameraFrame{top->x, top->y, top->zoom, top->rot} : CameraFrame{};
	const std::uint8_t topPriority = top ? top->priority : std::uint8_t{0};
	const std::uint8_t hasTop = top ? std::uint8_t{1} : std::uint8_t{0};

	const bool switched = (stack.hasLastTop != hasTop) || (hasTop != 0 && topPriority != stack.lastTopPriority);
	if (switched)
	{
		stack.blendFrom       = stack.current;
		stack.blendTotal      = top ? top->blendSec : 0.0f;
		stack.blendRemain     = stack.blendTotal;
		stack.hasLastTop      = hasTop;
		stack.lastTopPriority = topPriority;
	}

	if (stack.blendRemain > 0.0f)
	{
		stack.blendRemain = (dt >= stack.blendRemain) ? 0.0f : (stack.blendRemain - dt);
		const float alpha = (stack.blendTotal > 0.0f) ? (1.0f - stack.blendRemain / stack.blendTotal) : 1.0f;
		stack.current = detail::lerpCameraFrame(stack.blendFrom, target, alpha);
	}
	else
	{
		stack.current = target;
	}
	return stack.current;
}

/// @brief HE2 の `ObjCameraVolume` 相当。矩形 (world 座標) に入っている間だけ `entry` を積む。
/// `pushed` は同じ構造体に同居させる (呼び出し側が GameMemory に置く)。二重 push や
/// 「push だけ記録されて実際は降りている」ような巻き戻り不整合を状態そのもので防ぐ。
struct CameraVolume
{
	float       x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
	CameraEntry entry{};
	std::uint8_t pushed = 0;
	std::uint8_t _pad[3] = {};
};

template <int N>
void updateCameraVolume(CameraStack<N>& stack, CameraVolume& volume, float playerX, float playerY) noexcept
{
	const bool inside = playerX >= volume.x && playerX <= volume.x + volume.w &&
	                     playerY >= volume.y && playerY <= volume.y + volume.h;

	if (inside && volume.pushed == 0)
	{
		if (stack.pushCamera(volume.entry)) { volume.pushed = 1; }
	}
	else if (!inside && volume.pushed != 0)
	{
		// 重なった Volume を入った順と逆でなく出ても、他の Volume のカメラを降ろさない
		(void)stack.removeCamera(volume.entry);
		volume.pushed = 0;
	}
}

}  // namespace mitiru
