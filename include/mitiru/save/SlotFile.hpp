#pragma once

/// @file SlotFile.hpp
/// @brief セーブスロット 1 個のファイル形式 (.mslot)。ヘッダ + メタ情報 + サムネイル PNG + 中身。
/// @details 中身 (GameMemory の .msav 形式など) は不透明な bytes として扱う。ファイル全体を XXH64 で
///          照合するので、ディスク上で 1 bit 化けても読む側が気づく。ヘッダとメタ情報は固定長の POD で、
///          リトルエンディアンの機械だけを対象にする。

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/util/Hash.hpp>

namespace mitiru::save
{

static_assert(std::endian::native == std::endian::little, ".mslot はリトルエンディアンの機械だけで読み書きする");

/// 形式の版。ヘッダかメタ情報の並びを変えたら上げる。読む側は自分より新しい版を断る。
constexpr std::uint32_t kSlotFormatVersion = 1;
constexpr char kSlotMagic[4] = {'M', 'S', 'L', 'T'};
/// checksum はこの位置より後ろ全部に掛ける (magic・版・checksum 自身を除く)。
constexpr std::size_t kSlotChecksumFrom = 16;

struct SlotFileHeader
{
	char          magic[4];
	std::uint32_t formatVersion;
	std::uint64_t checksum;      ///< XXH64 (seed 0) of bytes [kSlotChecksumFrom, end)
	std::uint32_t headerSize;
	std::uint32_t metaSize;
	std::uint32_t thumbSize;
	std::uint32_t payloadSize;
	std::uint64_t layoutHash;    ///< 中身を書いたゲームの GameMemory の形 (一覧で読まずに比べられるよう複写)
	std::uint32_t gameVersion;   ///< ゲームが決める版。エンジンは比べない
	std::uint32_t _pad;
};
static_assert(sizeof(SlotFileHeader) == 48, ".mslot のヘッダは 48 byte 固定");

struct SlotMetaPod
{
	std::int64_t  savedAtUnixSec;
	double        playtimeSec;
	std::uint16_t thumbWidth;
	std::uint16_t thumbHeight;
	std::uint32_t _pad;
	char          chapter[104];  ///< UTF-8、null 終端
};
static_assert(sizeof(SlotMetaPod) == 128, ".mslot のメタ情報は 128 byte 固定");

/// @brief 一覧とロード画面に出す情報。
struct SlotMeta
{
	std::int64_t  savedAtUnixSec = 0;
	double        playtimeSec = 0.0;
	std::string   chapter;           ///< 章の名前など (UTF-8、103 byte まで)
	std::uint16_t thumbWidth = 0;
	std::uint16_t thumbHeight = 0;
	std::uint64_t layoutHash = 0;
	std::uint32_t gameVersion = 0;
};

enum class SlotDecodeStatus : std::uint8_t
{
	Ok,
	Truncated,         ///< ヘッダか各部の長さが足りない
	BadMagic,          ///< .mslot ではない
	NewerFormat,       ///< このエンジンより新しい形式で書かれた
	ChecksumMismatch,  ///< 中身が化けている
};

struct SlotDecoded
{
	SlotDecodeStatus status = SlotDecodeStatus::Truncated;
	SlotMeta meta;
	std::vector<std::uint8_t> thumbnailPng;
	std::vector<std::uint8_t> payload;
};

/// @brief 章の名前を固定長の欄へ。入りきらない時は UTF-8 の文字の切れ目で切る。
inline void copyChapter(char (&dst)[104], std::string_view src) noexcept
{
	std::size_t n = std::min(src.size(), sizeof(dst) - 1);
	while (n > 0 && n < src.size() && (static_cast<unsigned char>(src[n]) & 0xC0u) == 0x80u) { --n; }
	std::memcpy(dst, src.data(), n);
	std::memset(dst + n, 0, sizeof(dst) - n);
}

[[nodiscard]] inline std::vector<std::uint8_t> encodeSlotFile(const SlotMeta& meta,
                                                              std::span<const std::uint8_t> thumbnailPng,
                                                              std::span<const std::uint8_t> payload)
{
	SlotFileHeader h{};
	std::memcpy(h.magic, kSlotMagic, sizeof(h.magic));
	h.formatVersion = kSlotFormatVersion;
	h.headerSize = sizeof(SlotFileHeader);
	h.metaSize = sizeof(SlotMetaPod);
	h.thumbSize = static_cast<std::uint32_t>(thumbnailPng.size());
	h.payloadSize = static_cast<std::uint32_t>(payload.size());
	h.layoutHash = meta.layoutHash;
	h.gameVersion = meta.gameVersion;

	SlotMetaPod m{};
	m.savedAtUnixSec = meta.savedAtUnixSec;
	m.playtimeSec = meta.playtimeSec;
	m.thumbWidth = meta.thumbWidth;
	m.thumbHeight = meta.thumbHeight;
	copyChapter(m.chapter, meta.chapter);

	std::vector<std::uint8_t> out(sizeof(h) + sizeof(m) + thumbnailPng.size() + payload.size());
	std::uint8_t* p = out.data() + sizeof(h);
	std::memcpy(p, &m, sizeof(m));
	p += sizeof(m);
	if (!thumbnailPng.empty()) { std::memcpy(p, thumbnailPng.data(), thumbnailPng.size()); }
	p += thumbnailPng.size();
	if (!payload.empty()) { std::memcpy(p, payload.data(), payload.size()); }

	std::memcpy(out.data(), &h, sizeof(h));
	h.checksum = util::Hash::xxhash(out.data() + kSlotChecksumFrom, out.size() - kSlotChecksumFrom);
	std::memcpy(out.data(), &h, sizeof(h));
	return out;
}

namespace detail
{

[[nodiscard]] inline SlotMeta metaFromPod(const SlotMetaPod& m, const SlotFileHeader& h)
{
	SlotMeta meta;
	meta.savedAtUnixSec = m.savedAtUnixSec;
	meta.playtimeSec = m.playtimeSec;
	meta.thumbWidth = m.thumbWidth;
	meta.thumbHeight = m.thumbHeight;
	meta.chapter.assign(m.chapter, std::find(m.chapter, m.chapter + sizeof(m.chapter), '\0'));
	meta.layoutHash = h.layoutHash;
	meta.gameVersion = h.gameVersion;
	return meta;
}

/// ヘッダを読み、各部の長さがファイルに収まるかまで確かめる。
[[nodiscard]] inline SlotDecodeStatus readSlotHeader(std::span<const std::uint8_t> bytes, SlotFileHeader& h)
{
	if (bytes.size() < sizeof(SlotFileHeader)) { return SlotDecodeStatus::Truncated; }
	std::memcpy(&h, bytes.data(), sizeof(h));
	if (std::memcmp(h.magic, kSlotMagic, sizeof(h.magic)) != 0) { return SlotDecodeStatus::BadMagic; }
	if (h.formatVersion > kSlotFormatVersion) { return SlotDecodeStatus::NewerFormat; }
	if (h.headerSize < sizeof(SlotFileHeader) || h.metaSize < sizeof(SlotMetaPod)) { return SlotDecodeStatus::Truncated; }
	const std::uint64_t need = std::uint64_t{h.headerSize} + h.metaSize + h.thumbSize + h.payloadSize;
	return need == bytes.size() ? SlotDecodeStatus::Ok : SlotDecodeStatus::Truncated;
}

}  // namespace detail

[[nodiscard]] inline SlotDecoded decodeSlotFile(std::span<const std::uint8_t> bytes)
{
	SlotDecoded out;
	SlotFileHeader h{};
	out.status = detail::readSlotHeader(bytes, h);
	if (out.status != SlotDecodeStatus::Ok) { return out; }
	if (util::Hash::xxhash(bytes.data() + kSlotChecksumFrom, bytes.size() - kSlotChecksumFrom) != h.checksum)
	{
		out.status = SlotDecodeStatus::ChecksumMismatch;
		return out;
	}
	SlotMetaPod m{};
	const std::uint8_t* p = bytes.data() + h.headerSize;
	std::memcpy(&m, p, sizeof(m));
	out.meta = detail::metaFromPod(m, h);
	p += h.metaSize;
	out.thumbnailPng.assign(p, p + h.thumbSize);
	p += h.thumbSize;
	out.payload.assign(p, p + h.payloadSize);
	return out;
}

}  // namespace mitiru::save
