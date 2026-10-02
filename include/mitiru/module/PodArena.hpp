#pragma once

/// @file PodArena.hpp
/// @brief 固定サイズの POD bump アリーナ `PodArena<Bytes>` と、アリーナ先頭からの相対
/// バイトオフセットで要素を指す `Rel<T>`。GameMemory は絶対アドレスへの生ポインタを持てない
/// (`hedgehog_study/mitiru_vs_he2.md` その先)。memcpy / 巻き戻し (`GameMemoryRing`) /
/// 録画・再生でバイト列そのものを丸ごと複製・上書きすると、複製先での絶対アドレスは
/// 元の実体と別物になり、生ポインタは使えなくなる。`Rel<T>` はアリーナ先頭からの相対オフセットしか
/// 持たないため、アリーナごと (= GameMemory ごと) 複製しても `get()` は複製先の中で正しい
/// 要素を指し続ける。

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace mitiru
{

/// @brief `PodArena` 内の要素を指す相対参照。`offset < 0` は null。
template<class T>
struct Rel
{
	std::int32_t offset = -1;

	[[nodiscard]] constexpr bool isNull() const noexcept { return offset < 0; }

	/// @brief `arena.buf` 先頭からのバイトオフセットを実体アドレスへ変換する。
	template<class Arena>
	[[nodiscard]] T* get(Arena& arena) const noexcept
	{
		return isNull() ? nullptr : reinterpret_cast<T*>(arena.buf + offset);
	}

	template<class Arena>
	[[nodiscard]] const T* get(const Arena& arena) const noexcept
	{
		return isNull() ? nullptr : reinterpret_cast<const T*>(arena.buf + offset);
	}
};

template<class T>
[[nodiscard]] constexpr bool operator==(Rel<T> a, Rel<T> b) noexcept { return a.offset == b.offset; }
template<class T>
[[nodiscard]] constexpr bool operator!=(Rel<T> a, Rel<T> b) noexcept { return !(a == b); }

/// @brief 固定容量 Bytes の POD bump アリーナ。個別 free は持たない
/// (フレーム／ステージ単位で `reset()` して作り直す用途)。
template<std::size_t Bytes>
struct PodArena
{
	std::uint8_t  buf[Bytes]{};
	std::uint32_t used = 0;

	/// AutoReflect へ「中身は byte 配列として反射しない」ことを伝える印
	/// (`AutoReflect.hpp` の `isOpaqueReflectable`)。無いと `buf` が `buf.0, buf.1, ...` へ
	/// Bytes 個の FieldDescriptor に展開されてしまう。
	using MitiruOpaqueReflect = void;

	/// @brief T を count 個ぶん確保する。alignof(T) に揃えて配置し、収まらなければ null を返す。
	template<class T>
	[[nodiscard]] Rel<T> alloc(std::size_t count = 1) noexcept
	{
		static_assert(std::is_trivially_copyable_v<T>,
			"PodArena::alloc<T>: T は flat POD (trivially_copyable) であること");
		if (count == 0) { return Rel<T>{}; }

		constexpr std::size_t align = alignof(T);
		const std::size_t alignedStart = (static_cast<std::size_t>(used) + (align - 1)) & ~(align - 1);
		if (alignedStart > Bytes) { return Rel<T>{}; }

		// count が JSON/セーブデータ由来など外部入力から来ると、count * sizeof(T) が
		// size_t で桁あふれし小さい値に折り返って `bytesNeeded > Bytes` を素通りし、
		// 実際の書き込みで buf を溢れさせる恐れがある。先に割り算側で上限を見て桁あふれを避ける。
		const std::size_t remaining = Bytes - alignedStart;
		if (count > remaining / sizeof(T)) { return Rel<T>{}; }
		const std::size_t bytesNeeded = alignedStart + count * sizeof(T);

		used = static_cast<std::uint32_t>(bytesNeeded);
		return Rel<T>{ static_cast<std::int32_t>(alignedStart) };
	}

	/// @brief 全解放。個々の Rel は次の alloc で上書きされうるため、以後は破棄済みとして扱う。
	void reset() noexcept { used = 0; }
};

}  // namespace mitiru

static_assert(std::is_trivially_copyable_v<mitiru::PodArena<64>>,
	"PodArena は POD (DLL 境界 / GameMemory 埋め込み用)");
