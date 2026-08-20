#pragma once

/// @file GameMemorySave.hpp
/// @brief .msav (GameMemory snapshot) の読み書き純関数 (ADR 0020)
/// @details セーブ = GameMemory bytes の memcpy。形式はヘッダ
///          {magic "MSAV", formatVersion, memorySize, abiVersion, layoutHash} + bytes。
///          memorySize 不一致のロードは拒否する — GameMemory struct 変更後の
///          旧セーブを黙って化けさせない (replay A3 と同じ思想)。v2 からは
///          MITIRU_REFLECT 由来の layoutHash も照合し、サイズ照合を素通りする
///          「同サイズの field 並べ替え / 型変更」も拒否する (ADR 0024 追記)。
///          書き込みは tmp → rename の atomic 置換で、中断しても既存 .msav を壊さない。

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/module/Reflection.hpp>

namespace mitiru::module::save
{

/// .msav 形式バージョン (ヘッダ構造を変えたら上げる)。
/// v1 → v2: layoutHash 追加 (16 → 24 byte)。
/// v2 → v3: フィールド表を同梱 (28 byte + 表)。layout が変わっても名前で拾える
///          フィールドだけ移せる。v1/v2 ファイルは formatVersion 不一致で graceful reject。
constexpr std::uint32_t kMsavFormatVersion = 3;

/// 先頭 4 byte の magic
constexpr char kMsavMagic[4] = {'M', 'S', 'A', 'V'};

/// @brief .msav の固定長ヘッダ (32 byte、native エンディアン)
struct MsavHeader
{
	char          magic[4];       ///< "MSAV"
	std::uint32_t formatVersion;  ///< kMsavFormatVersion
	std::uint32_t memorySize;     ///< 後続 bytes 数 = セーブ時の GameMemory サイズ
	std::uint32_t abiVersion;     ///< セーブ時の wire version (指紋入り、診断用、照合はしない)
	std::uint64_t layoutHash;     ///< MITIRU_REFLECT 由来の layout hash (0 = 未宣言 = 照合 skip)
	std::uint32_t fieldCount;     ///< 続くフィールド表の件数 (0 = 表なし = 移行不可)
	std::uint32_t _pad;
};
static_assert(sizeof(MsavHeader) == 32, "MsavHeader はファイル形式 — 32 byte 固定 (v3)");

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
/// @param layoutHash MITIRU_REFLECT 由来の layout hash (module::moduleLayoutHash)。
///        0 = reflection 未宣言 (ロード時の layout 照合を skip)。
/// @return 成功で true。引数不正 / 書込失敗 / rename 失敗は false (tmp は残さない)。
[[nodiscard]] inline bool saveGameMemory(const std::filesystem::path& path,
                                         const void* mem, std::uint32_t size,
                                         std::uint32_t abiVersion,
                                         std::uint64_t layoutHash = 0,
                                         const FieldDescriptor* fields = nullptr,
                                         std::int32_t fieldCount = 0)
{
	if (mem == nullptr || size == 0) { return false; }
	if (fields == nullptr || fieldCount < 0) { fieldCount = 0; }

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
		h.layoutHash    = layoutHash;
		h.fieldCount    = static_cast<std::uint32_t>(fieldCount);
		out.write(reinterpret_cast<const char*>(&h), sizeof(h));
		if (fieldCount > 0)
		{
			out.write(reinterpret_cast<const char*>(fields),
			          static_cast<std::streamsize>(sizeof(FieldDescriptor))
			              * fieldCount);
		}
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

/// @brief .msav を読み bytes を返す。magic / 形式 / サイズ / layout が合わなければ nullopt。
/// @param expectSize 現在の GameMemory サイズ。ヘッダの memorySize と不一致なら拒否。
/// @param expectLayoutHash 現在の module の layout hash (module::moduleLayoutHash)。
///        双方非 0 かつ不一致なら拒否 — 同サイズの field 並べ替え / 型変更を素通ししない。
///        どちらかが 0 (reflection 未宣言) なら従来のサイズ照合のみ (後方互換)。
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>>
loadGameMemory(const std::filesystem::path& path, std::uint32_t expectSize,
               std::uint64_t expectLayoutHash = 0)
{
	if (expectSize == 0) { return std::nullopt; }

	std::ifstream in(path, std::ios::binary);
	if (!in) { return std::nullopt; }

	MsavHeader h{};
	in.read(reinterpret_cast<char*>(&h), sizeof(h));
	if (!in || in.gcount() != static_cast<std::streamsize>(sizeof(h))) { return std::nullopt; }
	if (std::memcmp(h.magic, kMsavMagic, sizeof(h.magic)) != 0) { return std::nullopt; }
	if (h.formatVersion != kMsavFormatVersion) { return std::nullopt; }
	if (h.fieldCount > 0)
	{
		in.seekg(static_cast<std::streamoff>(sizeof(FieldDescriptor)) * h.fieldCount,
		         std::ios::cur);
		if (!in) { return std::nullopt; }
	}
	if (h.memorySize != expectSize) { return std::nullopt; }  // struct 変更後の旧セーブを拒否
	if (h.layoutHash != 0 && expectLayoutHash != 0 && h.layoutHash != expectLayoutHash)
	{
		return std::nullopt;  // 同サイズでも layout 変更 (並べ替え / 型変更) は拒否
	}

	std::vector<std::uint8_t> bytes(expectSize);
	in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(expectSize));
	if (!in || in.gcount() != static_cast<std::streamsize>(expectSize))
	{
		return std::nullopt;  // 途中切れファイル
	}
	return bytes;
}

/// @brief 2 つの記述子が「同じ中身の同じ入れ物」か。名前・型・要素の形が全て一致するときだけ真。
[[nodiscard]] inline bool sameShape(const FieldDescriptor& a,
                                    const FieldDescriptor& b) noexcept
{
	return std::strncmp(a.name, b.name, sizeof(a.name)) == 0
	    && std::strncmp(a.typeTag, b.typeTag, sizeof(a.typeTag)) == 0
	    && std::strncmp(a.elemType, b.elemType, sizeof(a.elemType)) == 0
	    && a.elemSize == b.elemSize && a.elemCount == b.elemCount
	    && a.hasCount == b.hasCount && a.countOffset == b.countOffset;
}

/// @brief 層が変わった .msav から、名前と形が一致するフィールドだけを現在の memory へ移す。
/// @details 出力は current の複製を土台にするので、記録に無い新フィールドは初期値のまま残る。
///          形が変わったフィールド (型変更・配列の長さ変更) は移さない — 化けさせない。
/// @param current 現在の GameMemory (初期化済み)。土台として複製される。
/// @return 1 つでも移せたら移行後の bytes。表が無い / 一致ゼロなら nullopt。
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>>
migrateGameMemory(const std::filesystem::path& path,
                  const void* current, std::uint32_t currentSize,
                  const FieldDescriptor* curFields, std::int32_t curFieldCount,
                  std::int32_t* movedOut = nullptr)
{
	if (movedOut != nullptr) { *movedOut = 0; }
	if (current == nullptr || currentSize == 0) { return std::nullopt; }
	if (curFields == nullptr || curFieldCount <= 0) { return std::nullopt; }

	std::ifstream in(path, std::ios::binary);
	if (!in) { return std::nullopt; }
	MsavHeader h{};
	in.read(reinterpret_cast<char*>(&h), sizeof(h));
	if (!in || in.gcount() != static_cast<std::streamsize>(sizeof(h))) { return std::nullopt; }
	if (std::memcmp(h.magic, kMsavMagic, sizeof(h.magic)) != 0) { return std::nullopt; }
	if (h.formatVersion != kMsavFormatVersion) { return std::nullopt; }
	if (h.fieldCount == 0 || h.memorySize == 0) { return std::nullopt; }

	std::vector<FieldDescriptor> old(h.fieldCount);
	in.read(reinterpret_cast<char*>(old.data()),
	        static_cast<std::streamsize>(sizeof(FieldDescriptor)) * h.fieldCount);
	if (!in) { return std::nullopt; }

	std::vector<std::uint8_t> oldMem(h.memorySize);
	in.read(reinterpret_cast<char*>(oldMem.data()),
	        static_cast<std::streamsize>(h.memorySize));
	if (!in || in.gcount() != static_cast<std::streamsize>(h.memorySize))
	{
		return std::nullopt;
	}

	std::vector<std::uint8_t> out(currentSize);
	std::memcpy(out.data(), current, currentSize);

	std::int32_t moved = 0;
	for (std::int32_t i = 0; i < curFieldCount; ++i)
	{
		const auto& cf = curFields[i];
		const std::uint64_t span =
			static_cast<std::uint64_t>(cf.elemSize) * cf.elemCount;
		if (span == 0) { continue; }
		for (const auto& of : old)
		{
			if (!sameShape(cf, of)) { continue; }
			if (of.offset + span > h.memorySize) { break; }
			if (cf.offset + span > currentSize) { break; }
			std::memcpy(out.data() + cf.offset, oldMem.data() + of.offset,
			            static_cast<std::size_t>(span));
			++moved;
			break;
		}
	}
	if (moved == 0) { return std::nullopt; }
	if (movedOut != nullptr) { *movedOut = moved; }
	return out;
}

}  // namespace mitiru::module::save
