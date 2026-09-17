#pragma once

/// @file PodPool.hpp
/// @brief 世代付き弱参照 `Ref` と、それを配る固定容量プール `Pool<T,N>`。
/// module 系 (GameMemory) はインデックス直参照のため、敵を消した後に古いインデックスを
/// 使ってしまうバグを型で防げない (`hedgehog_study/mitiru_vs_he2.md` 1-5)。`Pool<T,N>::alloc()`
/// が返す `Ref` は世代を持ち、`get(Ref)` は世代が一致しない (中身が入れ替わった) 場合に
/// nullptr を返す。`FixedVec` 同様、内部はすべて固定長配列 (flat POD) で、GameMemory に
/// そのまま埋め込める。

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace mitiru
{

/// @brief `Pool<T,N>::alloc()` が返す世代付き弱参照。gen == 0 は null (未割り当て)。
struct Ref
{
	std::uint16_t index = 0;
	std::uint16_t gen   = 0;

	[[nodiscard]] constexpr bool isNull() const noexcept { return gen == 0; }
};

[[nodiscard]] constexpr bool operator==(const Ref& a, const Ref& b) noexcept
{
	return a.index == b.index && a.gen == b.gen;
}
[[nodiscard]] constexpr bool operator!=(const Ref& a, const Ref& b) noexcept { return !(a == b); }

/// @brief 固定容量 N、世代付き handle で配る flat POD プール (要素型 T は trivially_copyable、
/// N は 65535 以下 = Ref::index が uint16 に収まる範囲)。
template <typename T, std::size_t N>
struct Pool
{
	static_assert(std::is_trivially_copyable_v<T>,
	              "Pool<T,N>: 要素 T も flat POD (trivially_copyable) であること");
	static_assert(N > 0 && N <= 65535, "Pool<T,N>: N は 1..65535");

	T             slots[N]{};        ///< 要素の実体
	std::uint16_t slotGen[N]{};      ///< 0 = 未使用スロット。alloc() で 1 以上になる
	std::uint8_t  alive[N]{};        ///< 1 = 現在割り当て中 (forEachAlive / get の判定に使う)
	std::uint16_t freeStack[N]{};    ///< free() されたスロットの再利用スタック
	std::uint32_t freeStackSize = 0;
	std::uint32_t bumpNext      = 0; ///< まだ一度も使っていない先頭スロットの境界

	/// @brief 新しい要素を確保する (中身は T{} で初期化)。満杯なら null Ref を返す。
	[[nodiscard]] Ref alloc() noexcept
	{
		std::uint32_t idx;
		if (freeStackSize > 0) { idx = freeStack[--freeStackSize]; }
		else if (bumpNext < N) { idx = bumpNext++; }
		else { return Ref{}; }

		if (slotGen[idx] == 0) { slotGen[idx] = 1; }  // 0 は null 番兵なので初回だけ 1 から始める
		alive[idx] = 1;
		slots[idx] = T{};
		return Ref{ static_cast<std::uint16_t>(idx), slotGen[idx] };
	}

	/// @brief `r` を解放する。世代不一致・二重解放・範囲外は no-op。
	/// @note `gen` は uint16 (0 は null 番兵) なので、同じスロットで 65535 回
	///       alloc/free を繰り返すと世代が一巡し、古い `Ref` が偶然また有効判定される
	///       ABA が理論上ありうる。GameMemory に生ポインタでなく flat POD で埋め込む
	///       ために意図的に 16bit にしている (Ref を広げると GameMemory 全体の ABI が動く)。
	void free(Ref r) noexcept
	{
		if (!isValid(r)) { return; }
		alive[r.index] = 0;
		++slotGen[r.index];
		if (slotGen[r.index] == 0) { slotGen[r.index] = 1; }  // wrap で null 番兵の 0 を踏まない
		if (freeStackSize < N) { freeStack[freeStackSize++] = r.index; }
	}

	/// @brief `r` が指す要素へのポインタ。世代不一致・解放済み・範囲外なら nullptr。
	[[nodiscard]] T* get(Ref r) noexcept { return isValid(r) ? &slots[r.index] : nullptr; }
	[[nodiscard]] const T* get(Ref r) const noexcept { return isValid(r) ? &slots[r.index] : nullptr; }

	/// @brief 生きているスロットだけを `fn(T&, Ref)` に渡す (フリーリストの穴は自動で飛ばす)。
	template <class Fn>
	void forEachAlive(Fn&& fn)
	{
		for (std::uint32_t i = 0; i < N; ++i)
		{
			if (alive[i] != 0) { fn(slots[i], Ref{ static_cast<std::uint16_t>(i), slotGen[i] }); }
		}
	}

	/// @brief const 版 (第 1 引数は `const T&`)。
	template <class Fn>
	void forEachAlive(Fn&& fn) const
	{
		for (std::uint32_t i = 0; i < N; ++i)
		{
			if (alive[i] != 0) { fn(slots[i], Ref{ static_cast<std::uint16_t>(i), slotGen[i] }); }
		}
	}

private:
	[[nodiscard]] bool isValid(Ref r) const noexcept
	{
		return !r.isNull() && r.index < N && alive[r.index] != 0 && slotGen[r.index] == r.gen;
	}
};

}  // namespace mitiru

static_assert(std::is_trivially_copyable_v<mitiru::Ref>, "Ref は POD (DLL 境界 / GameMemory 埋め込み用)");
