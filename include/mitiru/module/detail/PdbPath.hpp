#pragma once

/// @file PdbPath.hpp
/// @brief PE イメージ (DLL) の CodeView (RSDS) 記録にある PDB のパスを読み書きする。
/// @details ホットリロードは DLL を一時名へ写して読むが、写した DLL は元の PDB を絶対パスで
/// 指したままになる。デバッガを host に付けているとデバッガがその PDB を開いたままにし、
/// 次のリンクが PDB を書けずに失敗する。PDB も一緒に写し、写した DLL の中のパスを写した PDB へ
/// 書き換えれば、デバッガとリンカが同じファイルを取り合わない。
/// 書き換えは元の文字列の領域の中で行う (新しいパスが収まらなければ書き換えない)。

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace mitiru::module::detail
{

/// @brief RSDS 記録のパス文字列の位置。offset はイメージ先頭からの byte 位置、capacity は終端 0 を含む領域の長さ。
struct PdbPathSpan
{
	std::size_t offset   = 0;
	std::size_t capacity = 0;
};

[[nodiscard]] inline std::uint32_t readU32(const std::vector<std::uint8_t>& b, std::size_t at) noexcept
{
	std::uint32_t v = 0;
	if (at + 4 <= b.size()) { std::memcpy(&v, b.data() + at, 4); }
	return v;
}

[[nodiscard]] inline std::uint16_t readU16(const std::vector<std::uint8_t>& b, std::size_t at) noexcept
{
	std::uint16_t v = 0;
	if (at + 2 <= b.size()) { std::memcpy(&v, b.data() + at, 2); }
	return v;
}

/// @brief RVA をファイル上の位置へ直す。どの section にも入らなければ nullopt。
[[nodiscard]] inline std::optional<std::size_t> rvaToFileOffset(
	const std::vector<std::uint8_t>& b, std::size_t sectionTable, std::uint16_t sectionCount, std::uint32_t rva) noexcept
{
	constexpr std::size_t kSectionHeaderSize = 40;
	for (std::uint16_t i = 0; i < sectionCount; ++i)
	{
		const std::size_t s = sectionTable + static_cast<std::size_t>(i) * kSectionHeaderSize;
		const std::uint32_t virtSize = readU32(b, s + 8);
		const std::uint32_t virtAddr = readU32(b, s + 12);
		const std::uint32_t rawSize  = readU32(b, s + 16);
		const std::uint32_t rawPtr   = readU32(b, s + 20);
		const std::uint32_t span     = (virtSize > rawSize) ? virtSize : rawSize;
		if (rva >= virtAddr && rva < virtAddr + span) { return static_cast<std::size_t>(rawPtr) + (rva - virtAddr); }
	}
	return std::nullopt;
}

/// @brief CodeView (RSDS) 記録の PDB パスの位置を探す。PE でない・記録が無いなら nullopt。
[[nodiscard]] inline std::optional<PdbPathSpan> findPdbPath(const std::vector<std::uint8_t>& b) noexcept
{
	if (b.size() < 0x40 || b[0] != 'M' || b[1] != 'Z') { return std::nullopt; }
	const std::size_t pe = readU32(b, 0x3C);
	if (pe + 24 > b.size() || readU32(b, pe) != 0x00004550u) { return std::nullopt; }  // "PE\0\0"
	const std::uint16_t sectionCount = readU16(b, pe + 6);
	const std::uint16_t optSize      = readU16(b, pe + 20);
	const std::size_t   opt          = pe + 24;
	const std::uint16_t magic        = readU16(b, opt);
	const std::size_t   dataDirs     = opt + ((magic == 0x20b) ? 112u : 96u);  // PE32+ / PE32
	constexpr std::size_t kDebugDir  = 6;
	const std::uint32_t debugRva  = readU32(b, dataDirs + kDebugDir * 8);
	const std::uint32_t debugSize = readU32(b, dataDirs + kDebugDir * 8 + 4);
	if (debugRva == 0 || debugSize == 0) { return std::nullopt; }

	const auto dirAt = rvaToFileOffset(b, opt + optSize, sectionCount, debugRva);
	if (!dirAt) { return std::nullopt; }
	constexpr std::size_t   kEntrySize    = 28;
	constexpr std::uint32_t kTypeCodeView = 2;
	for (std::size_t e = 0; e + kEntrySize <= debugSize; e += kEntrySize)
	{
		const std::size_t entry = *dirAt + e;
		if (readU32(b, entry + 12) != kTypeCodeView) { continue; }
		const std::size_t dataSize = readU32(b, entry + 16);
		const std::size_t data     = readU32(b, entry + 24);  // PointerToRawData
		if (data + dataSize > b.size() || dataSize <= 24 || std::memcmp(b.data() + data, "RSDS", 4) != 0) { continue; }
		return PdbPathSpan{ data + 24, dataSize - 24 };  // "RSDS" + GUID 16 + Age 4 の後ろ
	}
	return std::nullopt;
}

[[nodiscard]] inline std::string readPdbPath(const std::vector<std::uint8_t>& b, const PdbPathSpan& span)
{
	std::size_t n = 0;
	while (n < span.capacity && b[span.offset + n] != 0) { ++n; }
	return std::string(reinterpret_cast<const char*>(b.data() + span.offset), n);
}

/// @brief パスを書き換える。収まらなければ何もせず false。余った領域は 0 で埋める。
[[nodiscard]] inline bool writePdbPath(std::vector<std::uint8_t>& b, const PdbPathSpan& span, const std::string& path)
{
	if (path.size() + 1 > span.capacity) { return false; }
	std::memset(b.data() + span.offset, 0, span.capacity);
	std::memcpy(b.data() + span.offset, path.data(), path.size());
	return true;
}

}  // namespace mitiru::module::detail
