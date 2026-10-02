#pragma once

/// @file GameMemorySave.hpp
/// @brief.msav (GameMemory snapshot) の読み書き純関数
/// @details セーブ = GameMemory bytes の memcpy。形式はヘッダ
///          {magic "MSAV", formatVersion, memorySize, abiVersion, layoutHash} + bytes。
///          memorySize 不一致のロードは拒否する。GameMemory struct 変更後に
///          旧セーブが気づかないうちに化けるのを防ぐ (replay A3 と同じ思想)。v2 からは
///          MITIRU_REFLECT 由来の layoutHash も照合し、サイズ照合を素通りする
///          「同サイズの field 並べ替え / 型変更」も拒否する。
///          エンジンは .msav の bytes をセーブスロット (save/SaveSlots.hpp) の中身として包んで書く。
///          ファイルとしての .msav (スロットに包まない形) は、その名前のスロットが無い時の読み込みとテストの記録作りに使う。

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <span>
#include <string_view>
#include <vector>

#include <mitiru/module/Reflection.hpp>
#include <mitiru/save/AtomicFile.hpp>

namespace mitiru::module::save
{

/// .msav 形式バージョン (ヘッダ構造を変えたら上げる)。
/// v1 → v2: layoutHash 追加 (16 → 24 byte)。
/// v2 → v3: フィールド表を同梱 (28 byte + 表)。layout が変わっても名前で拾える
///          フィールドだけ移せる。v1/v2 ファイルは formatVersion 不一致として、落とさずに拒否する。
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

/// @brief slot 名を [a-zA-Z0-9_-] のみに削る。パス区切り等は除去し、全部消えたら "" を返す。
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

/// @brief .msav の各部の位置 (bytes を指すだけで写さない)。
struct MsavView
{
	MsavHeader                       header{};
	std::span<const FieldDescriptor> fields;
	std::span<const std::uint8_t>    memory;
	std::span<const std::uint8_t>    tail;    ///< GameMemory の後ろの bytes (GameMemory の外に持つ状態の image、ADR 0054)
};

/// @brief GameMemory bytes を .msav 形式の bytes にする。mem が無いか size 0 なら空。
/// @param layoutHash 現在の module の layout hash (ModuleReflection::identity)。0 = 照合しない。
/// @param tail GameMemory の後ろに続ける bytes (GameMemory の外に持つ状態の image、ADR 0054)。
///        ヘッダは変えないので、tail を知らない読み手は memorySize までを読んで残りを無視する。
[[nodiscard]] inline std::vector<std::uint8_t> encodeMsav(const void* mem, std::uint32_t size,
                                                          std::uint32_t abiVersion,
                                                          std::uint64_t layoutHash = 0,
                                                          const FieldDescriptor* fields = nullptr,
                                                          std::int32_t fieldCount = 0,
                                                          std::span<const std::uint8_t> tail = {})
{
	if (mem == nullptr || size == 0) { return {}; }
	if (fields == nullptr || fieldCount < 0) { fieldCount = 0; }
	MsavHeader h{};
	std::memcpy(h.magic, kMsavMagic, sizeof(h.magic));
	h.formatVersion = kMsavFormatVersion;
	h.memorySize    = size;
	h.abiVersion    = abiVersion;
	h.layoutHash    = layoutHash;
	h.fieldCount    = static_cast<std::uint32_t>(fieldCount);
	const std::size_t tableBytes = sizeof(FieldDescriptor) * static_cast<std::size_t>(fieldCount);
	std::vector<std::uint8_t> out(sizeof(h) + tableBytes + size + tail.size());
	std::memcpy(out.data(), &h, sizeof(h));
	if (tableBytes > 0) { std::memcpy(out.data() + sizeof(h), fields, tableBytes); }
	std::memcpy(out.data() + sizeof(h) + tableBytes, mem, size);
	if (!tail.empty()) { std::memcpy(out.data() + sizeof(h) + tableBytes + size, tail.data(), tail.size()); }
	return out;
}

/// @brief .msav の bytes を区切る。magic / 形式 / 長さが合わなければ nullopt。
[[nodiscard]] inline std::optional<MsavView> parseMsav(std::span<const std::uint8_t> bytes)
{
	MsavView v;
	if (bytes.size() < sizeof(MsavHeader)) { return std::nullopt; }
	std::memcpy(&v.header, bytes.data(), sizeof(MsavHeader));
	if (std::memcmp(v.header.magic, kMsavMagic, sizeof(v.header.magic)) != 0) { return std::nullopt; }
	if (v.header.formatVersion != kMsavFormatVersion) { return std::nullopt; }
	const std::uint64_t tableBytes = std::uint64_t{sizeof(FieldDescriptor)} * v.header.fieldCount;
	if (bytes.size() < sizeof(MsavHeader) + tableBytes + v.header.memorySize) { return std::nullopt; }
	const std::uint8_t* table = bytes.data() + sizeof(MsavHeader);
	v.fields = { reinterpret_cast<const FieldDescriptor*>(table), static_cast<std::size_t>(v.header.fieldCount) };
	v.memory = { table + tableBytes, static_cast<std::size_t>(v.header.memorySize) };
	const std::size_t used = sizeof(MsavHeader) + static_cast<std::size_t>(tableBytes) + v.header.memorySize;
	v.tail = { bytes.data() + used, bytes.size() - used };
	return v;
}

/// @brief .msav の bytes から GameMemory を取り出す。サイズか layout が今の module と違えば nullopt。
/// @param expectSize 現在の GameMemory サイズ。ヘッダの memorySize と不一致なら拒否。
/// @param expectLayoutHash 現在の module の layout hash。双方非 0 かつ不一致なら拒否する。
///        同サイズの field 並べ替え / 型変更を素通ししないため。どちらかが 0 ならサイズ照合だけ。
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>>
decodeMsav(std::span<const std::uint8_t> bytes, std::uint32_t expectSize, std::uint64_t expectLayoutHash = 0)
{
	if (expectSize == 0) { return std::nullopt; }
	const auto v = parseMsav(bytes);
	if (!v || v->header.memorySize != expectSize) { return std::nullopt; }
	if (v->header.layoutHash != 0 && expectLayoutHash != 0 && v->header.layoutHash != expectLayoutHash)
	{
		return std::nullopt;
	}
	return std::vector<std::uint8_t>(v->memory.begin(), v->memory.end());
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
///          形が変わったフィールド (型変更・配列の長さ変更) は移さない。化けさせない。
/// @param current 現在の GameMemory (初期化済み)。土台として複製される。
/// @return 1 つでも移せたら移行後の bytes。表が無い / 一致ゼロなら nullopt。
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>>
migrateMsav(std::span<const std::uint8_t> bytes,
            const void* current, std::uint32_t currentSize,
            const FieldDescriptor* curFields, std::int32_t curFieldCount,
            std::int32_t* movedOut = nullptr)
{
	if (movedOut != nullptr) { *movedOut = 0; }
	if (current == nullptr || currentSize == 0) { return std::nullopt; }
	if (curFields == nullptr || curFieldCount <= 0) { return std::nullopt; }
	const auto v = parseMsav(bytes);
	if (!v || v->fields.empty() || v->memory.empty()) { return std::nullopt; }

	std::vector<std::uint8_t> out(currentSize);
	std::memcpy(out.data(), current, currentSize);
	std::int32_t moved = 0;
	for (std::int32_t i = 0; i < curFieldCount; ++i)
	{
		const auto& cf = curFields[i];
		const std::uint64_t span = static_cast<std::uint64_t>(cf.elemSize) * cf.elemCount;
		if (span == 0) { continue; }
		for (const auto& of : v->fields)
		{
			if (!sameShape(cf, of)) { continue; }
			if (of.offset + span > v->memory.size() || cf.offset + span > currentSize) { break; }
			std::memcpy(out.data() + cf.offset, v->memory.data() + of.offset, static_cast<std::size_t>(span));
			++moved;
			break;
		}
	}
	if (moved == 0) { return std::nullopt; }
	if (movedOut != nullptr) { *movedOut = moved; }
	return out;
}

/// @brief GameMemory bytes を path へ .msav (スロットに包まない形) で atomic に書く。tail は encodeMsav と同じ。
/// @return 成功で true。引数不正 / 書込失敗 / rename 失敗は false (tmp は残さない)。
[[nodiscard]] inline bool saveGameMemory(const std::filesystem::path& path,
                                         const void* mem, std::uint32_t size,
                                         std::uint32_t abiVersion,
                                         std::uint64_t layoutHash = 0,
                                         const FieldDescriptor* fields = nullptr,
                                         std::int32_t fieldCount = 0,
                                         const void* tail = nullptr,
                                         std::size_t tailSize = 0)
{
	const std::span<const std::uint8_t> tailBytes =
		tail != nullptr ? std::span<const std::uint8_t>(static_cast<const std::uint8_t*>(tail), tailSize)
		                : std::span<const std::uint8_t>();
	const auto bytes = encodeMsav(mem, size, abiVersion, layoutHash, fields, fieldCount, tailBytes);
	return !bytes.empty() && mitiru::save::writeFileAtomic(path, bytes, /*keepBackup*/ false);
}

/// @brief .msav ファイルを読み、decodeMsav と同じ判定で GameMemory を返す。
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>>
loadGameMemory(const std::filesystem::path& path, std::uint32_t expectSize,
               std::uint64_t expectLayoutHash = 0)
{
	const auto bytes = mitiru::save::readWholeFile(path);
	if (!bytes) { return std::nullopt; }
	return decodeMsav(*bytes, expectSize, expectLayoutHash);
}

/// @brief .msav ファイルの GameMemory より後ろの bytes (saveGameMemory の tail) を返す。無ければ空。
///        ヘッダが読めない・GameMemory の途中で切れているファイルは nullopt。
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>> loadGameMemoryTail(const std::filesystem::path& path)
{
	const auto bytes = mitiru::save::readWholeFile(path);
	if (!bytes) { return std::nullopt; }
	const auto v = parseMsav(*bytes);
	if (!v) { return std::nullopt; }
	return std::vector<std::uint8_t>(v->tail.begin(), v->tail.end());
}

/// @brief .msav ファイルを読み、migrateMsav で今の層へ移す。
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>>
migrateGameMemory(const std::filesystem::path& path,
                  const void* current, std::uint32_t currentSize,
                  const FieldDescriptor* curFields, std::int32_t curFieldCount,
                  std::int32_t* movedOut = nullptr)
{
	if (movedOut != nullptr) { *movedOut = 0; }
	const auto bytes = mitiru::save::readWholeFile(path);
	if (!bytes) { return std::nullopt; }
	return migrateMsav(*bytes, current, currentSize, curFields, curFieldCount, movedOut);
}

/// @brief offset の byte を含む field の名前。見つからなければ nullptr。
[[nodiscard]] inline const char* fieldNameAtOffset(const FieldDescriptor* fields,
                                                    std::int32_t fieldCount,
                                                    std::uint32_t offset) noexcept
{
	if (fields == nullptr) { return nullptr; }
	for (std::int32_t i = 0; i < fieldCount; ++i)
	{
		const FieldDescriptor& f = fields[i];
		const std::uint64_t span = static_cast<std::uint64_t>(f.elemSize) * f.elemCount;
		if (span == 0) { continue; }
		if (offset >= f.offset && offset < f.offset + span) { return f.name; }
	}
	return nullptr;
}

/// @brief 2 つの bytes 列を比較し、最初に食い違った byte が属する field 名を返す (Factorio
///        の desync report と同じく「最初に食い違った箇所だけ」を報告する)。
/// @return 一致すれば nullopt。不一致なら field 名 (reflect に無ければ空文字)。
[[nodiscard]] inline std::optional<std::string> compareRoundtripBytes(
	const std::uint8_t* a, const std::uint8_t* b, std::uint32_t size,
	const FieldDescriptor* fields, std::int32_t fieldCount)
{
	for (std::uint32_t i = 0; i < size; ++i)
	{
		if (a[i] != b[i])
		{
			const char* name = fieldNameAtOffset(fields, fieldCount, i);
			return std::string(name != nullptr ? name : "");
		}
	}
	return std::nullopt;
}

/// @brief 書いた .msav の bytes を読み戻し → 再 encode → 再読み戻しして、2 回の読み戻しが bit 一致するかを
///        検査する (`--save-roundtrip-test`、Factorio FFF #158 の save-load stability と同じ考え方)。
///        累積差分ではなく元の原因だけを見るため、比較は「1 回目の読み戻し」対「2 回目の読み戻し」で行う。
/// @return 一致すれば nullopt。不一致なら食い違った field 名 (読み戻し自体の失敗時は空文字)。
[[nodiscard]] inline std::optional<std::string> checkMsavRoundtrip(
	std::span<const std::uint8_t> written, std::uint32_t memSize, std::uint32_t abiVersion,
	std::uint64_t layoutHash, const FieldDescriptor* fields, std::int32_t fieldCount)
{
	const auto bytes1 = decodeMsav(written, memSize, layoutHash);
	const auto view   = parseMsav(written);
	if (!bytes1.has_value() || !view.has_value()) { return std::string(); }
	const auto again = encodeMsav(bytes1->data(), memSize, abiVersion, layoutHash, fields, fieldCount, view->tail);
	const auto bytes2 = decodeMsav(again, memSize, layoutHash);
	if (!bytes2.has_value()) { return std::string(); }
	return compareRoundtripBytes(bytes1->data(), bytes2->data(), memSize, fields, fieldCount);
}

}  // namespace mitiru::module::save
