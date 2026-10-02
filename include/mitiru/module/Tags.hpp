#pragma once

/// @file Tags.hpp
/// @brief 階層タグ (Unreal Gameplay Tags 相当) を GameMemory に置ける flat POD で表す (§3-4)。
/// @details "enemy.flying.boss" のような '.' 区切りの階層タグを、セグメントごとに 16bit id へ
/// エンコードし固定長配列 (`TagPath`) に積む。`hasTag(tags, "enemy")` は "enemy.flying.boss" の
/// ような子孫タグにも真を返す (先頭一致)。文字列そのものは GameMemory に置かない (可変長・DLL
/// 境界を跨げない) ため、id への変換は `util::Hash::fnv1a` を再利用する。id 空間は 16bit なので
/// 衝突しうるが、タグは「絞り込みの分類」用途でありゲームロジックの一意性保証には使わない前提。

#include <cstdint>
#include <cstring>

#include <mitiru/util/Hash.hpp>

namespace mitiru::module
{

inline constexpr std::size_t kTagMaxSegments = 4;  ///< "a.b.c.d" まで。超えた分は無視する
inline constexpr std::size_t kTagMaxCount    = 8;  ///< 1 個体が同時に持てるタグの数

/// @brief 1 本の階層タグの id 列。0 = 未使用セグメント (それ以降は全て未使用)。
struct TagPath
{
	std::uint16_t segments[kTagMaxSegments]{};
};

/// @brief 個体が持つタグの集合。flat POD なので GameMemory にそのまま置ける。
struct TagSet
{
	TagPath      tags[kTagMaxCount]{};
	std::uint8_t count = 0;
	std::uint8_t _pad[1] = {};   ///< 暗黙の詰め物を残さない (GameMemory はバイト単位で比べられる)
};

/// @brief 文字列セグメントを 16bit id へ変換する (fnv1a64 を xor-fold して縮める)。
[[nodiscard]] inline std::uint16_t tagSegmentId(const char* seg, std::size_t len) noexcept
{
	if (len == 0) { return 0; }
	const std::uint64_t h = util::Hash::fnv1a(seg, len);
	const std::uint32_t folded = static_cast<std::uint32_t>(h) ^ static_cast<std::uint32_t>(h >> 32);
	std::uint16_t id = static_cast<std::uint16_t>(folded ^ (folded >> 16));
	if (id == 0) { id = 1; }  // 0 は「未使用」の番兵なので実 id には使わない
	return id;
}

/// @brief "enemy.flying.boss" を '.' 区切りでセグメント id 列 (`TagPath`) に変換する。
/// @details `kTagMaxSegments` を超える階層は末尾を切り捨てる (呼び出し側の責任)。
[[nodiscard]] inline TagPath makeTagPath(const char* dotted) noexcept
{
	TagPath path{};
	if (dotted == nullptr) { return path; }

	const std::size_t len = std::strlen(dotted);
	std::size_t start = 0;
	std::size_t seg    = 0;
	for (std::size_t i = 0; i <= len && seg < kTagMaxSegments; ++i)
	{
		if (i != len && dotted[i] != '.') { continue; }
		if (i > start) { path.segments[seg++] = tagSegmentId(dotted + start, i - start); }
		start = i + 1;
	}
	return path;
}

/// @brief `path` が `prefix` を接頭辞として持つか (`prefix` の未使用セグメントで一致とみなす)。
[[nodiscard]] inline bool tagPathHasPrefix(const TagPath& path, const TagPath& prefix) noexcept
{
	for (std::size_t i = 0; i < kTagMaxSegments; ++i)
	{
		if (prefix.segments[i] == 0) { return true; }  // prefix はここで終わり = 一致
		if (path.segments[i] != prefix.segments[i]) { return false; }
	}
	return true;  // prefix が kTagMaxSegments まで全て埋まっていて、かつ最後まで一致した
}

/// @brief `tags` の中に `dotted` を接頭辞として持つタグが 1 本でもあるか。
/// @details "enemy" は "enemy.flying.boss" にも真を返す (階層タグの核心)。
[[nodiscard]] inline bool hasTag(const TagSet& tags, const char* dotted) noexcept
{
	const TagPath want = makeTagPath(dotted);
	if (want.segments[0] == 0) { return false; }  // 空/null の問い合わせは接頭辞 0 段 = 全一致になってしまう
	for (std::uint8_t i = 0; i < tags.count; ++i)
	{
		if (tagPathHasPrefix(tags.tags[i], want)) { return true; }
	}
	return false;
}

/// @brief タグを追加する。容量超過時は何もせず false を返す。
[[nodiscard]] inline bool addTag(TagSet& tags, const char* dotted) noexcept
{
	if (tags.count >= kTagMaxCount) { return false; }
	tags.tags[tags.count] = makeTagPath(dotted);
	++tags.count;
	return true;
}

}  // namespace mitiru::module
