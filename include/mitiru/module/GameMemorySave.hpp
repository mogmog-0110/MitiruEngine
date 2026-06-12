#pragma once

/// @file GameMemorySave.hpp
/// @brief .msav (GameMemory snapshot) の読み書き純関数 (ADR 0020)
/// @details セーブ = GameMemory bytes の memcpy。形式はヘッダ
///          {magic "MSAV", formatVersion, memorySize, abiVersion} + bytes。
///          memorySize 不一致のロードは拒否する — GameMemory struct 変更後の
///          旧セーブを黙って化けさせない (replay A3 と同じ思想)。
///          書き込みは tmp → rename の atomic 置換で、中断しても既存 .msav を壊さない。

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru::module::save
{

/// .msav 形式バージョン (ヘッダ構造を変えたら上げる)
constexpr std::uint32_t kMsavFormatVersion = 1;

/// 先頭 4 byte の magic
constexpr char kMsavMagic[4] = {'M', 'S', 'A', 'V'};

/// @brief .msav の固定長ヘッダ (16 byte、native エンディアン)
struct MsavHeader
{
	char          magic[4];       ///< "MSAV"
	std::uint32_t formatVersion;  ///< kMsavFormatVersion
	std::uint32_t memorySize;     ///< 後続 bytes 数 = セーブ時の GameMemory サイズ
	std::uint32_t abiVersion;     ///< セーブ時の ModuleApi version (診断用、照合はしない)
};
static_assert(sizeof(MsavHeader) == 16, "MsavHeader はファイル形式 — 16 byte 固定");

/// @brief slot 名を [a-zA-Z0-9_-] のみに削る。パス区切り等は除去、全滅なら "" を返す。
[[nodiscard]] inline std::string sanitizeSlot(std::string_view name)
{
	std::string out;
	out.reserve(name.size());
	for (const char c : name)
	{
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
		                || (c >= '0' && c <= '9') || c == '_' || c == '-';
		if (ok) { out.push_back(c); }
	}
	return out;
}

/// @brief GameMemory bytes を path へ atomic に書く (tmp 書き → rename)。
/// @return 成功で true。引数不正 / 書込失敗 / rename 失敗は false (tmp は残さない)。
[[nodiscard]] inline bool saveGameMemory(const std::filesystem::path& path,
                                         const void* mem, std::uint32_t size,
                                         std::uint32_t abiVersion)
{
	if (mem == nullptr || size == 0) { return false; }

	std::error_code ec;
	if (path.has_parent_path())
	{
		std::filesystem::create_directories(path.parent_path(), ec);  // 既存なら no-op
	}

	// tmp に全量書いてから rename — 書込中クラッシュで半端な .msav を残さない。
	std::filesystem::path tmp = path;
	tmp += ".tmp";
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		if (!out) { return false; }

		MsavHeader h{};
		std::memcpy(h.magic, kMsavMagic, sizeof(h.magic));
		h.formatVersion = kMsavFormatVersion;
		h.memorySize    = size;
		h.abiVersion    = abiVersion;
		out.write(reinterpret_cast<const char*>(&h), sizeof(h));
		out.write(static_cast<const char*>(mem), size);
		out.flush();
		if (!out)
		{
			out.close();
			std::filesystem::remove(tmp, ec);
			return false;
		}
	}

	std::filesystem::rename(tmp, path, ec);  // 既存 .msav は atomic に置換される
	if (ec)
	{
		std::filesystem::remove(tmp, ec);
		return false;
	}
	return true;
}

/// @brief .msav を読み bytes を返す。magic / 形式 / サイズが合わなければ nullopt。
/// @param expectSize 現在の GameMemory サイズ。ヘッダの memorySize と不一致なら拒否。
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>>
loadGameMemory(const std::filesystem::path& path, std::uint32_t expectSize)
{
	if (expectSize == 0) { return std::nullopt; }

	std::ifstream in(path, std::ios::binary);
	if (!in) { return std::nullopt; }

	MsavHeader h{};
	in.read(reinterpret_cast<char*>(&h), sizeof(h));
	if (!in || in.gcount() != static_cast<std::streamsize>(sizeof(h))) { return std::nullopt; }
	if (std::memcmp(h.magic, kMsavMagic, sizeof(h.magic)) != 0) { return std::nullopt; }
	if (h.formatVersion != kMsavFormatVersion) { return std::nullopt; }
	if (h.memorySize != expectSize) { return std::nullopt; }  // struct 変更後の旧セーブを拒否

	std::vector<std::uint8_t> bytes(expectSize);
	in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(expectSize));
	if (!in || in.gcount() != static_cast<std::streamsize>(expectSize))
	{
		return std::nullopt;  // 途中切れファイル
	}
	return bytes;
}

}  // namespace mitiru::module::save
