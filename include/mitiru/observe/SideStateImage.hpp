#pragma once

/// @file SideStateImage.hpp
/// @brief GameMemory の外に持つ状態 (ADR 0054) を 1 フレーム分まとめた bytes の形と読み書き。
/// @details 窓口ごとに {名前, 形の番号, hash, bytes} を並べる。巻き戻しのリング・.msav の末尾・
/// .mtrr の state blob の末尾が同じ形を使う。bytes を省いて hash だけを持つ形もあり、replay の
/// 毎フレームの照合はそちらで足りる (ロードを代用するフレームだけ bytes ごと持つ)。
/// 形: [SideImageHeader] + 窓口の数だけ [SideImageEntry + bytes (kSideEntryHasBytes のときだけ)]。

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::observe
{

inline constexpr std::uint32_t kSideImageMagic = 0x3153534Du;  // "MSS1" (little endian)

struct SideImageHeader
{
	std::uint32_t magic;
	std::uint32_t count;
};

/// @brief bytes を持っているか (無ければ hash だけ)。
inline constexpr std::uint32_t kSideEntryHasBytes = 1u << 0;

struct SideImageEntry
{
	char          name[32];
	std::uint32_t version;
	std::uint32_t flags;
	std::uint64_t hash;
	std::uint64_t size;    ///< 元の bytes 数 (hash だけの形でも save が書いた量を持つ)
};
static_assert(sizeof(SideImageHeader) == 8, "SideImageHeader はファイル形式 (.msav / .mtrr の末尾)");
static_assert(sizeof(SideImageEntry) == 56, "SideImageEntry はファイル形式 (.msav / .mtrr の末尾)");

/// @brief FNV-1a 64bit。窓口が hash を出さないときに host が bytes から作る。
[[nodiscard]] inline std::uint64_t fnv1a64(const void* data, std::size_t n) noexcept
{
	std::uint64_t h = 0xCBF29CE484222325ull;
	const auto* p = static_cast<const std::uint8_t*>(data);
	for (std::size_t i = 0; i < n; ++i) { h = (h ^ p[i]) * 0x100000001B3ull; }
	return h;
}

/// @brief 読んだ image の 1 窓口分 (bytes は image の中を指す。無ければ nullptr)。
struct SideImageItem
{
	SideImageEntry      entry{};
	const std::uint8_t*   bytes = nullptr;
};

/// @brief image を窓口ごとに区切った読み口。parse が成功したときだけ中身が有効。
struct SideImageView
{
	SideImageItem items[module::kMaxSideStateChannels]{};
	std::uint32_t count = 0;

	[[nodiscard]] const SideImageItem* find(std::string_view name) const noexcept
	{
		for (std::uint32_t i = 0; i < count; ++i)
		{
			const char* n = items[i].entry.name;
			if (name == std::string_view(n, ::strnlen(n, sizeof(items[i].entry.name)))) { return &items[i]; }
		}
		return nullptr;
	}
};

/// @return 形が正しければ true。窓口の数が上限を超える・途中で切れている等は false (中身を信じない)。
[[nodiscard]] inline bool parseSideImage(const std::uint8_t* p, std::size_t n, SideImageView& out) noexcept
{
	out = SideImageView{};
	if (p == nullptr || n < sizeof(SideImageHeader)) { return false; }
	SideImageHeader h{};
	std::memcpy(&h, p, sizeof(h));
	if (h.magic != kSideImageMagic || h.count > static_cast<std::uint32_t>(module::kMaxSideStateChannels)) { return false; }
	std::size_t r = sizeof(h);
	for (std::uint32_t i = 0; i < h.count; ++i)
	{
		if (n - r < sizeof(SideImageEntry)) { return false; }
		SideImageEntry e{};
		std::memcpy(&e, p + r, sizeof(e));  // image は GameMemory の後ろに続くので 8 byte 境界とは限らない
		r += sizeof(SideImageEntry);
		const std::uint8_t* bytes = nullptr;
		if ((e.flags & kSideEntryHasBytes) != 0)
		{
			if (e.size > n - r) { return false; }
			bytes = p + r;
			r += static_cast<std::size_t>(e.size);
		}
		out.items[i] = SideImageItem{e, bytes};
	}
	out.count = h.count;
	return r == n;
}

/// @brief 2 つの image の hash を窓口ごとに比べ、最初に食い違った窓口の名前を返す (一致なら空)。
///        窓口の並び・形の番号が違うのも食い違いとして名前を返す。
[[nodiscard]] inline std::string_view firstDivergedSideChannel(const SideImageView& a, const SideImageView& b) noexcept
{
	if (a.count != b.count) { return "(窓口の数)"; }
	for (std::uint32_t i = 0; i < a.count; ++i)
	{
		const SideImageEntry& x = a.items[i].entry;
		const SideImageEntry& y = b.items[i].entry;
		const bool same = std::strncmp(x.name, y.name, sizeof(x.name)) == 0
		               && x.version == y.version && x.hash == y.hash;
		if (!same) { return std::string_view(x.name, ::strnlen(x.name, sizeof(x.name))); }
	}
	return {};
}

}  // namespace mitiru::observe
