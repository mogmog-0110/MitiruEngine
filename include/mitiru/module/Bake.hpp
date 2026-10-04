#pragma once

/// @file Bake.hpp
/// @brief 配置 JSON (Spawner が読む assets/*.json) を起動前に POD bytes へ焼く (★4-1、Unity
/// Entities の Baking 由来)。
/// @details `spawnFromJson` は毎起動 JSON をパースし `nlohmann::json` の数値変換 (`v.get<float>()`
/// 等) を経由するため、「同じ JSON から同じ bytes が出る」がプラットフォーム/ライブラリ版をまたいで
/// 厳密には保証されない。焼いた結果 (POD bytes) を一次資料にし、起動時は
/// `Reflection.hpp::layoutHash` の照合 + memcpy 1 回で済ませる。照合が合わなければ
/// `spawnFromJson`/`spawnAllFrom` へ自動フォールバックし `warnOnce` で焼き直しを促す。
/// Spawner.hpp のヘルパをそのまま再利用し (instanceOf 解決・SpawnOrigin 付与)、この header だけ
/// 追加すれば足りるようにしてある。

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/module/AutoReflect.hpp>   // detail::collectFields
#include <mitiru/module/Reflection.hpp>    // FieldDescriptor / layoutHash / reflectSchemaRegistry
#include <mitiru/module/Spawner.hpp>       // spawnFromJson / spawnAllFrom / SpawnOrigin / detail ヘルパ

namespace mitiru::module
{

// ── 単一型 T の bake (bakeSpawnJson<T> / loadBaked<T>) ──────────────────────

constexpr char          kBakeMagic[4]      = {'M', 'B', 'A', 'K'};
constexpr std::uint32_t kBakeFormatVersion = 1;

/// @brief `bakeSpawnJson<T>` の出力先頭に載る固定長ヘッダ (24 byte)。
/// @details `elemSize` (=sizeof(T)) と `schemaHash` (=T の reflect 記述子から求めた
/// `layoutHash`) の両方が一致したときだけ `loadBaked` は memcpy を許す。片方だけの照合だと
/// 「同サイズの field 並べ替え」を素通ししてしまう (`GameMemorySave.hpp` の v1→v2 と同じ理由)。
struct BakeHeader
{
	char          magic[4];        ///< "MBAK"
	std::uint32_t formatVersion;   ///< kBakeFormatVersion
	std::uint32_t elemCount;       ///< 後続要素数
	std::uint32_t elemSize;        ///< 要素 1 個の byte 数 (sizeof(T) と一致するはず)
	std::uint64_t schemaHash;      ///< T の reflect layoutHash (0 = 起こりえない、layoutHash の番兵と同じ)
};
static_assert(sizeof(BakeHeader) == 24, "BakeHeader はファイル形式 — 24 byte 固定");

/// @brief T の reflect 記述子から layoutHash を求める。`bakeSpawnJson`/`loadBaked` で共有する。
template <class T>
[[nodiscard]] inline std::uint64_t bakeSchemaHash()
{
	const auto fields = ::mitiru::module::detail::collectFields<T>(0, "");
	return layoutHash(fields.data(), static_cast<std::int32_t>(fields.size()),
		reflectSchemaRegistry().data(), static_cast<std::int32_t>(reflectSchemaRegistry().size()));
}

namespace detail
{

/// @brief T が直メンバ `SpawnOrigin origin;` を持つ場合、その先頭 (= objectId) の byte offset を
/// 返す。`collectFields` は入れ子 aggregate をフラット化するため "origin.objectId" という名前で
/// 現れる。SpawnOrigin::objectId は同型の先頭メンバなので、この offset がそのまま
/// `SpawnOrigin` 24 byte 全体の書き込み先になる。無ければ -1 (このゲームは origin を
/// 埋め込まない設計、bake は T の JSON フィールドだけを焼く)。
template <class T>
[[nodiscard]] inline int findOriginBaseOffset(const std::vector<FieldDescriptor>& fields)
{
	for (const auto& f : fields)
	{
		if (std::strcmp(f.name, "origin.objectId") == 0 && std::strcmp(f.typeTag, "u64") == 0)
		{
			return static_cast<int>(f.offset);
		}
	}
	return -1;
}

}  // namespace detail

/// @brief 配置 JSON (Spawner の `objects[]` と同じ形) を、起動時に memcpy 1 回で読める POD bytes
/// へ焼く。`spawnFromJson`/`spawnAllFrom` と同じ規則 (instanceOf 継承の解決・SpawnOrigin の付与)
/// で埋めるので、実行時に同じ JSON を読んだ場合と bit 一致する (10 進文字列→float 変換はここでしか
/// 走らない)。T が直メンバ `SpawnOrigin origin;` を持つ場合はそこへ解決済みの origin を書き込む
/// (spawnAllFrom の visitor が普段やる代入と同じことを焼く時点で済ませる)。
/// T は `spawnFromJson<T>` と同じ制約 (aggregate かつ flat POD)。
template <class T>
[[nodiscard]] inline std::vector<std::uint8_t> bakeSpawnJson(
	const nlohmann::json& objects, std::uint32_t sourceFile = 0)
{
	static_assert(std::is_aggregate_v<T>,
		"bakeSpawnJson<T>: T は aggregate 型である必要があります (spawnFromJson と同じ制約)");
	static_assert(std::is_trivially_copyable_v<T>,
		"bakeSpawnJson<T>: T は flat POD (trivially_copyable) である必要があります");

	std::vector<std::uint8_t> out;
	if (!objects.is_array())
	{
		console::notice("bakeSpawnJson に渡した配置の JSON が配列ではありません。一番外側を配列にしてください。");
		return out;
	}

	// instanceOf (★1-10) の id → 添字表。spawnAllFrom と同じ組み立て方。
	std::unordered_map<std::string, std::size_t> idIndex;
	for (std::size_t idx = 0; idx < objects.size(); ++idx)
	{
		const auto& node = objects[idx];
		const auto  idIt = node.is_object() ? node.find("id") : node.end();
		if (idIt != node.end() && idIt->is_string()) { idIndex.emplace(idIt->get<std::string>(), idx); }
	}

	const auto fields       = ::mitiru::module::detail::collectFields<T>(0, "");
	const int  originOffset = detail::findOriginBaseOffset<T>(fields);

	std::vector<T> items;
	items.reserve(objects.size());
	std::uint32_t index = 0;
	for (std::size_t rawIndex = 0; rawIndex < objects.size(); ++rawIndex)
	{
		const auto& raw = objects[rawIndex];
		if (!raw.is_object())
		{
			console::noticef("配置の JSON の %zu 番目はオブジェクトではない (%s) ので、飛ばします。", rawIndex, raw.type_name());
			continue;
		}
		const nlohmann::json resolved = raw.contains("instanceOf")
			? ::mitiru::module::detail::resolveInstanceOf(objects, idIndex, rawIndex)
			: raw;

		T           tmp{};
		SpawnOrigin origin{};
		const bool  ok = spawnFromJson<T>(resolved, tmp, &origin);
		if (!ok)
		{
			console::noticef("配置の JSON の %zu 番目を struct に写すのに失敗しました。直前の知らせにある field を確かめてください。",
				rawIndex);
		}
		origin.sourceFile  = sourceFile;
		origin.objectIndex = index++;
		if (originOffset >= 0)
		{
			std::memcpy(reinterpret_cast<std::uint8_t*>(&tmp) + originOffset, &origin, sizeof(SpawnOrigin));
		}
		items.push_back(tmp);
	}

	BakeHeader header{};
	std::memcpy(header.magic, kBakeMagic, sizeof(header.magic));
	header.formatVersion = kBakeFormatVersion;
	header.elemCount      = static_cast<std::uint32_t>(items.size());
	header.elemSize        = static_cast<std::uint32_t>(sizeof(T));
	header.schemaHash      = layoutHash(fields.data(), static_cast<std::int32_t>(fields.size()),
		reflectSchemaRegistry().data(), static_cast<std::int32_t>(reflectSchemaRegistry().size()));

	out.resize(sizeof(BakeHeader) + items.size() * sizeof(T));
	std::memcpy(out.data(), &header, sizeof(BakeHeader));
	if (!items.empty())
	{
		std::memcpy(out.data() + sizeof(BakeHeader), items.data(), items.size() * sizeof(T));
	}
	return out;
}

/// @brief `bakeSpawnJson<T>` の逆方向。ヘッダ照合 (magic / formatVersion / elemSize / schemaHash)
/// が全て通れば memcpy のみ、破損や型不一致なら 0 を返す (呼び出し側は `spawnAllFrom`/
/// `spawnFromJson` の JSON 経路へフォールバックすること)。`capacity` を超える分は切り捨てて
/// `warnOnce` を出す (受け皿 `WorldObjects<T,N>` 等の N が足りないケース)。
template <class T>
[[nodiscard]] inline std::uint32_t loadBaked(
	const std::vector<std::uint8_t>& bytes, T* out, std::uint32_t capacity)
{
	static_assert(std::is_trivially_copyable_v<T>, "loadBaked<T>: T は flat POD である必要があります");
	if (out == nullptr || capacity == 0) { return 0; }
	if (bytes.size() < sizeof(BakeHeader)) { return 0; }  // 破損 (ヘッダより短い)

	BakeHeader header{};
	std::memcpy(&header, bytes.data(), sizeof(header));
	if (std::memcmp(header.magic, kBakeMagic, sizeof(header.magic)) != 0) { return 0; }
	if (header.formatVersion != kBakeFormatVersion) { return 0; }
	if (header.elemSize != static_cast<std::uint32_t>(sizeof(T)))
	{
		mitiru::debug::warnOnce("bake.schema.size-mismatch",
			"焼いた配置ファイル (.baked) の要素の大きさが今の struct と合わないので、JSON から読みます。"
			"mitiru bake で焼き直してください。");
		return 0;
	}
	if (header.schemaHash != bakeSchemaHash<T>())
	{
		mitiru::debug::warnOnce("bake.schema.hash-mismatch",
			"焼いた配置ファイル (.baked) の field の並びか型が今の struct と合わないので、JSON から読みます。"
			"mitiru bake で焼き直してください。");
		return 0;
	}

	std::uint32_t   n    = header.elemCount;
	const std::size_t need = sizeof(BakeHeader) + static_cast<std::size_t>(n) * sizeof(T);
	if (bytes.size() < need) { return 0; }  // 破損 (途中で切れている)

	if (n > capacity)
	{
		mitiru::debug::warnOnce("bake.capacity.exceeded",
			"焼いた配置ファイル (.baked) の要素が受け皿 (WorldObjects<T,N> の N) より多いので、入りきらない分を捨てました。"
			"N を増やすか、配置を減らしてください。");
		n = capacity;
	}
	if (n > 0) { std::memcpy(out, bytes.data() + sizeof(BakeHeader), static_cast<std::size_t>(n) * sizeof(T)); }
	return n;
}

// ── 複数型混在ファイルの bake (spawnAllFrom と対になる版) ────────────────────

constexpr char          kBakeAllMagic[4]      = {'M', 'B', 'A', 'L'};
constexpr std::uint32_t kBakeAllFormatVersion = 1;

/// @brief `bakeAllFrom` の出力先頭に載るヘッダ。`tableHash` は登録済み `MITIRU_SPAWNER` 表
/// (jsonName + サイズの並び) から作る粗いハッシュで、型の追加/削除/サイズ変更を検出する。
/// 単一型版の `schemaHash` ほど厳密に field 単位までは見ない (型ごとの FieldDescriptor は
/// Spawner のテーブルに載っていないため)。粗くても素通りしないことを優先する。
struct BakeAllHeader
{
	char          magic[4];       ///< "MBAL"
	std::uint32_t formatVersion;  ///< kBakeAllFormatVersion
	std::uint32_t elemCount;      ///< 後続エントリ数
	std::uint64_t tableHash;      ///< 登録済み spawner 表のハッシュ
};
static_assert(sizeof(BakeAllHeader) == 24, "BakeAllHeader はファイル形式 — 24 byte 固定");

/// @brief 1 エントリぶんのヘッダ (直後に `size` byte の struct 本体が続く、可変長レコード)。
struct BakeAllEntryHeader
{
	char          jsonName[32];  ///< spawnAllFrom の visitor が受け取る typeName と同じ
	std::uint32_t size;          ///< 続く本体の byte 数 (= 登録時の sizeof(Type))
	SpawnOrigin   origin;
};

/// @brief 登録済み `MITIRU_SPAWNER` 表 (jsonName + size) から粗いハッシュを作る。
/// 呼び出しは DLL 内で完結する (`detail::spawnerEntries()` は Game DLL local static)。
[[nodiscard]] inline std::uint64_t spawnerTableHash()
{
	const auto* table = ::mitiru::module::detail::spawnerEntries();
	const int   n      = ::mitiru::module::detail::spawnerEntryCount();
	std::uint64_t h = 14695981039346656037ull;
	const auto fold = [&h](const void* p, std::size_t bytes) noexcept
	{
		const auto* b = static_cast<const unsigned char*>(p);
		for (std::size_t i = 0; i < bytes; ++i) { h ^= b[i]; h *= 1099511628211ull; }
	};
	for (int i = 0; i < n; ++i)
	{
		fold(table[i].jsonName, sizeof(table[i].jsonName));
		fold(&table[i].size, sizeof(table[i].size));
	}
	return (h != 0) ? h : 1;
}

/// @brief `spawnAllFrom` と同じ規則 (instanceOf 解決込み) で JSON 配列全体を焼く。
/// 型ごとの本体サイズが違う (異種混在) ため、可変長レコード (`BakeAllEntryHeader` + 本体) を
/// 連ねる形式にしてある。
[[nodiscard]] inline std::vector<std::uint8_t> bakeAllFrom(
	const nlohmann::json& objects, std::uint32_t sourceFile = 0)
{
	std::vector<std::uint8_t> body;
	std::uint32_t             count = 0;
	spawnAllFrom(objects, sourceFile,
		[&](const char* typeName, const void* bytes, std::size_t size, const SpawnOrigin& origin)
		{
			BakeAllEntryHeader eh{};
			::mitiru::module::detail::copyTag(eh.jsonName, sizeof(eh.jsonName), typeName);
			eh.size   = static_cast<std::uint32_t>(size);
			eh.origin = origin;
			const std::size_t at = body.size();
			body.resize(at + sizeof(BakeAllEntryHeader) + size);
			std::memcpy(body.data() + at, &eh, sizeof(BakeAllEntryHeader));
			if (size > 0) { std::memcpy(body.data() + at + sizeof(BakeAllEntryHeader), bytes, size); }
			++count;
		});

	BakeAllHeader header{};
	std::memcpy(header.magic, kBakeAllMagic, sizeof(header.magic));
	header.formatVersion = kBakeAllFormatVersion;
	header.elemCount      = count;
	header.tableHash      = spawnerTableHash();

	std::vector<std::uint8_t> out(sizeof(BakeAllHeader) + body.size());
	std::memcpy(out.data(), &header, sizeof(header));
	if (!body.empty()) { std::memcpy(out.data() + sizeof(BakeAllHeader), body.data(), body.size()); }
	return out;
}

/// @brief `bakeAllFrom` の逆方向。妥当なら各エントリを `visitor(typeName, bytes, size, origin)`
/// (spawnAllFrom と同じ形) へ渡し true。ヘッダ照合失敗・破損なら false (JSON へフォールバックする
/// のは呼び出し側の責務)。
template <class Visitor>
[[nodiscard]] inline bool loadBakedAllFrom(const std::vector<std::uint8_t>& bytes, Visitor&& visitor)
{
	if (bytes.size() < sizeof(BakeAllHeader)) { return false; }
	BakeAllHeader header{};
	std::memcpy(&header, bytes.data(), sizeof(header));
	if (std::memcmp(header.magic, kBakeAllMagic, sizeof(header.magic)) != 0) { return false; }
	if (header.formatVersion != kBakeAllFormatVersion) { return false; }
	if (header.tableHash != spawnerTableHash())
	{
		mitiru::debug::warnOnce("bake.table.hash-mismatch",
			"焼いた配置ファイル (.baked) の MITIRU_SPAWNER の型が今の登録と合わないので、JSON から読みます。"
			"mitiru bake で焼き直してください。");
		return false;
	}

	// 境界の検証と配送を分ける。途中切れを見つけた時点で一部を visitor に渡していると、呼び出し側が
	// JSON へフォールバックした分と二重に生まれる。
	std::size_t pos = sizeof(BakeAllHeader);
	for (std::uint32_t i = 0; i < header.elemCount; ++i)
	{
		if (pos + sizeof(BakeAllEntryHeader) > bytes.size()) { return false; }  // 途中切れ
		BakeAllEntryHeader eh{};
		std::memcpy(&eh, bytes.data() + pos, sizeof(BakeAllEntryHeader));
		pos += sizeof(BakeAllEntryHeader);
		if (pos + eh.size > bytes.size()) { return false; }  // 途中切れ
		pos += eh.size;
	}
	pos = sizeof(BakeAllHeader);
	for (std::uint32_t i = 0; i < header.elemCount; ++i)
	{
		BakeAllEntryHeader eh{};
		std::memcpy(&eh, bytes.data() + pos, sizeof(BakeAllEntryHeader));
		pos += sizeof(BakeAllEntryHeader);
		visitor(eh.jsonName, bytes.data() + pos, static_cast<std::size_t>(eh.size), eh.origin);
		pos += eh.size;
	}
	return true;
}

/// @brief `MITIRU_NO_BAKE=1` が立っていれば true (焼き済みを無視して JSON 経路を強制する)。
[[nodiscard]] inline bool bakeDisabledByEnv() noexcept
{
	const char* v = std::getenv("MITIRU_NO_BAKE");
	return v != nullptr && std::strcmp(v, "1") == 0;
}

/// @brief 「同名の .baked があれば優先する」の本体。`bakedBytes` が有効な baked データなら
/// それを読んで visitor へ渡し true を返す。そうでなければ (env 無効化・未生成・スキーマ不一致)
/// `objects` (元 JSON) を `spawnAllFrom` で処理して false を返す。戻り値は「baked を使ったか」
/// であって成否ではない (JSON 経路の要素単位の失敗は spawnAllFrom 自身が stderr に出して続行する)。
template <class Visitor>
inline bool spawnAllFromOrBaked(
	const nlohmann::json& objects, std::uint32_t sourceFile,
	const std::vector<std::uint8_t>& bakedBytes, Visitor&& visitor)
{
	if (!bakeDisabledByEnv() && !bakedBytes.empty() && loadBakedAllFrom(bakedBytes, visitor))
	{
		return true;
	}
	spawnAllFrom(objects, sourceFile, std::forward<Visitor>(visitor));
	return false;
}

// ── ファイル I/O (mitiru_host --bake / mitiru bake の下請け) ─────────────────

/// @brief bytes を path へ atomic に書く (tmp 書き→rename、`GameMemorySave.hpp` と同じ流儀)。
[[nodiscard]] inline bool writeBakedFile(
	const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
	std::error_code ec;
	if (path.has_parent_path()) { std::filesystem::create_directories(path.parent_path(), ec); }

	std::filesystem::path tmp = path;
	tmp += ".tmp";
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		if (!out) { return false; }
		if (!bytes.empty())
		{
			out.write(reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
		}
		out.flush();
		if (!out) { out.close(); std::filesystem::remove(tmp, ec); return false; }
	}
	std::filesystem::rename(tmp, path, ec);
	if (ec) { std::filesystem::remove(tmp, ec); return false; }
	return true;
}

/// @brief path から bytes を読む。存在しない/開けない場合は空を返す (呼び出し側は
/// `bytes.empty()` を「baked 無し」として扱う)。
[[nodiscard]] inline std::vector<std::uint8_t> readBakedFile(const std::filesystem::path& path)
{
	std::vector<std::uint8_t> bytes;
	std::ifstream in(path, std::ios::binary);
	if (!in) { return bytes; }
	in.seekg(0, std::ios::end);
	const auto size = in.tellg();
	if (size <= 0) { return bytes; }
	bytes.resize(static_cast<std::size_t>(size));
	in.seekg(0, std::ios::beg);
	in.read(reinterpret_cast<char*>(bytes.data()), size);
	if (!in) { bytes.clear(); }
	return bytes;
}

/// @brief JSON ファイル (root が配列、または `{"objects":[...]}` 形) を読み `bakeAllFrom` で
/// 焼いて outPath へ書く。`MITIRU_BAKE_ASSETS()` (Game DLL の export) と `mitiru bake` (CLI、
/// 同じ処理をプロジェクト内の全 JSON へ回す) の共通下請け。成功で true。
[[nodiscard]] inline bool bakeAssetsFileGeneric(
	const std::filesystem::path& jsonPath, const std::filesystem::path& outPath)
{
	std::ifstream in(jsonPath);
	if (!in)
	{
		console::noticef("配置の JSON %s を開けません。", jsonPath.string().c_str());
		return false;
	}
	nlohmann::json root;
	try { in >> root; }
	catch (const nlohmann::json::parse_error& e)
	{
		console::noticef("配置の JSON %s を読めません (%s)。書き間違いを直してください。", jsonPath.string().c_str(), e.what());
		return false;
	}

	const nlohmann::json* objects = nullptr;
	if (root.is_array()) { objects = &root; }
	else if (root.is_object())
	{
		if (const auto it = root.find("objects"); it != root.end() && it->is_array()) { objects = &(*it); }
	}
	if (objects == nullptr)
	{
		console::noticef("配置の JSON %s は配列でも {\"objects\":[...]} でもありません。一番外側をどちらかの形にしてください。",
			jsonPath.string().c_str());
		return false;
	}

	const std::uint32_t sourceFile = spawnSourceFileHash(jsonPath.filename().string());
	const auto           baked      = bakeAllFrom(*objects, sourceFile);
	if (!writeBakedFile(outPath, baked))
	{
		console::noticef("焼いたファイル %s の書き込みに失敗しました。書き込める場所か確かめてください。", outPath.string().c_str());
		return false;
	}
	return true;
}

}  // namespace mitiru::module

/// @brief Game DLL に「配置 JSON を焼く」export を追加する (optional、host `--bake` が
/// `mitiru_module_bake_assets` を GetProcAddress で解決して呼ぶ)。`MITIRU_GAME` と併記する。
/// 中身は `bakeAssetsFileGeneric` の薄い extern "C" ラッパーで、DLL 固有の分岐は要らない
/// (`MITIRU_SPAWNER` で登録済みの型を使うだけなので Game 側コードの変更も不要)。
#define MITIRU_BAKE_ASSETS()                                                             \
	extern "C" MITIRU_GAME_EXPORT                                                        \
	bool mitiru_module_bake_assets(const char* jsonPath, const char* outPath)            \
	{                                                                                     \
		return ::mitiru::module::bakeAssetsFileGeneric(jsonPath, outPath);               \
	}
