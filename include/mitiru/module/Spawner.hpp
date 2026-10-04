#pragma once

/// @file Spawner.hpp
/// @brief JSON の配置データを `MITIRU_REFLECT_AUTO` 済み struct へ流し込む汎用ローダ (§3-1)。
/// @details `MITIRU_REFLECT` / `MITIRU_REFLECT_AUTO` が作る FieldDescriptor は
/// 「フィールド名 → offset/型」の記述子で、今までは読み取り (`observe::reflectToJson`) にしか
/// 使っていなかった。同じ記述子は逆方向 (JSON → bytes) にも使えるので、AutoReflect.hpp の
/// `collectFields<T>` をそのまま再利用し、書き込み側を足すだけで済ませる。Game DLL 内で完結する
/// (Engine を呼ばない、host を経由しない)。scene.json の `objects[]` を struct へ変換したい
/// Game 側の init コードがこの header だけを include すればよい。

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/debug/WarnOnce.hpp>       // instanceOf 循環検出の通知
#include <mitiru/module/AutoReflect.hpp>  // detail::collectFields
#include <mitiru/module/Reflection.hpp>   // FieldDescriptor / ReflectSchema / reflectSchemaRegistry
#include <mitiru/module/Tags.hpp>         // TagSet (§3-4、"tags": [...] の書き込み先)
#include <mitiru/util/Hash.hpp>           // Hash::fnv1a (SpawnOrigin::objectId / sourceFile のハッシュに再利用)

namespace mitiru::module
{

/// @brief 配置データの出自 (★1-1)。`spawnFromJson` の時点で GameMemory 側の POD に残しておくと、
/// commit (ADR 0035「残す」) がどの配置ファイルの何番目に書き戻せばよいかを機械的に決められる
/// (HE2 の `ObjectData::id` / `WorldObjectStatus::objectData` 相当)。flat POD なので GameMemory
/// に直接置けて、巻き戻し・録画の対象にそのまま含まれる。
struct SpawnOrigin
{
	std::uint64_t objectId    = 0;  ///< JSON の "id" (文字列なら FNV-1a、数値ならその値)。無指定は 0
	std::uint32_t sourceFile  = 0;  ///< 配置ファイル名の FNV-1a (下位32bit)。0 = ファイル不明/コード生成
	std::uint32_t objectIndex = 0;  ///< そのファイルの objects[] での添字
	std::uint8_t  fromEditor  = 0;  ///< 1 = 配置ファイル由来、0 = コード生成 (HE2 の StatusFlags::EDITOR 相当)
	std::uint8_t  _pad[7]{};
};
static_assert(sizeof(SpawnOrigin) == 24, "SpawnOrigin: GameMemory 上のレイアウトを固定する");
static_assert(std::is_trivially_copyable_v<SpawnOrigin>, "SpawnOrigin: flat POD である必要があります");

/// @brief 配置ファイル名から `SpawnOrigin::sourceFile` を作る (呼び出し側で 1 回計算して使い回す)。
[[nodiscard]] inline std::uint32_t spawnSourceFileHash(std::string_view fileName) noexcept
{
	return static_cast<std::uint32_t>(::mitiru::util::Hash::fnv1a(fileName));
}

/// @brief `/api/ai/commit` (ADR 0035「残す」) が、書いた dotted path を含む struct に埋め込まれた
/// `SpawnOrigin` を探すための補助。`SpawnOrigin` は直ネストしたフィールドとして自動反射される
/// ("<object>.origin.objectId" のように state ツリーへ現れる) ので、host 側に専用の配線は要らない。
/// `fieldPath` の階層を外側へ辿りながら、"origin" サブツリーを持つ最初の (最も深い) ノードを返す。
/// 見つからなければ nullptr (出自不明の field、またはそもそも spawnFromJson 由来ではない field)。
[[nodiscard]] inline const nlohmann::json* findSpawnOrigin(
	const nlohmann::json& state, std::string_view fieldPath)
{
	std::vector<std::string_view> parts;
	std::size_t pos = 0;
	while (true)
	{
		const auto dot = fieldPath.find('.', pos);
		const auto end = (dot == std::string_view::npos) ? fieldPath.size() : dot;
		parts.push_back(fieldPath.substr(pos, end - pos));
		if (dot == std::string_view::npos) { break; }
		pos = dot + 1;
	}

	for (std::size_t depth = parts.size(); depth-- > 0; )
	{
		const nlohmann::json* cur = &state;
		bool ok = true;
		for (std::size_t k = 0; k < depth; ++k)
		{
			if (!cur->is_object()) { ok = false; break; }
			const auto it = cur->find(std::string(parts[k]));
			if (it == cur->end()) { ok = false; break; }
			cur = &(*it);
		}
		if (!ok || !cur->is_object()) { continue; }
		const auto oit = cur->find("origin");
		if (oit != cur->end() && oit->is_object() && oit->contains("sourceFile")) { return &(*oit); }
	}
	return nullptr;
}

}  // namespace mitiru::module

namespace mitiru::module::detail
{

/// @brief T が `TagSet tags;` という名前・型のメンバを持つか (§3-4)。`collectFields` の汎用
/// 記述子は TagSet を知らないので、"tags" だけは専用の JSON 経路で書く。
template <class T, class = void>
struct HasTagsMember : std::false_type {};
template <class T>
struct HasTagsMember<T, std::void_t<decltype(std::declval<T&>().tags)>>
	: std::bool_constant<std::is_same_v<std::decay_t<decltype(std::declval<T&>().tags)>, TagSet>> {};

/// @brief `"tags": ["enemy.flying.boss", ...]` を `TagSet` へ流し込む。配列でなければ何もしない。
inline void writeTagsFromJson(TagSet& out, const nlohmann::json& j)
{
	if (!j.is_array()) { return; }
	out = TagSet{};
	for (const auto& item : j)
	{
		if (item.is_string()) { (void)addTag(out, item.get<std::string>().c_str()); }
	}
}

/// @brief JSON ノードをドット区切り名でたどる。"pos.x" のようなネストオブジェクトと
/// "elem.0" のような配列展開の両方に対応する (`collectFields` が作る名前の形そのまま)。
/// 見つからなければ nullptr (呼び出し側は「未指定 = 変更なし」として無視する)。
inline const nlohmann::json* findByDottedName(const nlohmann::json& root, std::string_view name)
{
	const nlohmann::json* cur = &root;
	std::string_view remaining = name;
	while (true)
	{
		const auto dot  = remaining.find('.');
		const auto part = remaining.substr(0, dot);

		if (cur->is_object())
		{
			const auto it = cur->find(std::string(part));
			if (it == cur->end()) { return nullptr; }
			cur = &(*it);
		}
		else if (cur->is_array())
		{
			std::size_t idx = 0;
			for (const char c : part)
			{
				if (c < '0' || c > '9') { return nullptr; }
				idx = idx * 10 + static_cast<std::size_t>(c - '0');
			}
			if (idx >= cur->size()) { return nullptr; }
			cur = &(*cur)[idx];
		}
		else { return nullptr; }

		if (dot == std::string_view::npos) { return cur; }
		remaining = remaining.substr(dot + 1);
	}
}

/// @brief `findByDottedName` の書き込み版。無ければ object を作りながら辿り、最後のノードへの
/// 参照を返す (`writeBackFieldsToJson` の各 leaf 代入先)。
inline nlohmann::json& touchByDottedName(nlohmann::json& root, std::string_view name)
{
	nlohmann::json* cur = &root;
	std::string_view remaining = name;
	while (true)
	{
		const auto dot  = remaining.find('.');
		const auto part = remaining.substr(0, dot);
		if (!cur->is_object()) { *cur = nlohmann::json::object(); }
		cur = &(*cur)[std::string(part)];
		if (dot == std::string_view::npos) { return *cur; }
		remaining = remaining.substr(dot + 1);
	}
}

/// @brief 1 スカラーフィールドを bytes から読んで JSON 値にする (`writeSpawnerScalar` の逆方向)。
inline nlohmann::json readSpawnerScalar(const std::uint8_t* p, const char* tag)
{
	const auto eq = [&](const char* t) { return std::strcmp(tag, t) == 0; };
	if (eq("bool")) { std::uint8_t v; std::memcpy(&v, p, sizeof(v)); return v != 0; }
	if (eq("f32"))  { float v;         std::memcpy(&v, p, sizeof(v)); return v; }
	if (eq("f64"))  { double v;        std::memcpy(&v, p, sizeof(v)); return v; }
	if (eq("i8"))   { std::int8_t v;   std::memcpy(&v, p, sizeof(v)); return static_cast<int>(v); }
	if (eq("u8"))   { std::uint8_t v;  std::memcpy(&v, p, sizeof(v)); return static_cast<unsigned>(v); }
	if (eq("i16"))  { std::int16_t v;  std::memcpy(&v, p, sizeof(v)); return static_cast<int>(v); }
	if (eq("u16"))  { std::uint16_t v; std::memcpy(&v, p, sizeof(v)); return static_cast<unsigned>(v); }
	if (eq("i32"))  { std::int32_t v;  std::memcpy(&v, p, sizeof(v)); return v; }
	if (eq("u32"))  { std::uint32_t v; std::memcpy(&v, p, sizeof(v)); return v; }
	if (eq("i64"))  { std::int64_t v;  std::memcpy(&v, p, sizeof(v)); return v; }
	if (eq("u64"))  { std::uint64_t v; std::memcpy(&v, p, sizeof(v)); return v; }
	return nullptr;
}

/// @brief typeName で reflectSchemaRegistry() から要素 schema を探す (無ければ nullptr)。
inline const ReflectSchema* findSpawnerSchema(const char* typeName)
{
	if (typeName == nullptr || typeName[0] == '\0') { return nullptr; }
	for (const auto& s : reflectSchemaRegistry())
	{
		if (std::strcmp(s.typeName, typeName) == 0) { return &s; }
	}
	return nullptr;
}

/// @brief 1 スカラーフィールドへ JSON 値を書き込む。JSON 側の型が合わなければ false。
inline bool writeSpawnerScalar(
	std::uint8_t* p, const char* tag, const nlohmann::json& v, const char* fieldName)
{
	const auto fail = [&]
	{
		console::noticef("配置の JSON の field \"%s\" は型が合いません (JSON は %s、struct は %s)。値を直してください。",
			fieldName, v.type_name(), tag);
		return false;
	};
	const auto eq = [&](const char* t) { return std::strcmp(tag, t) == 0; };

	if (eq("bool"))
	{
		if (!v.is_boolean()) { return fail(); }
		const std::uint8_t b = v.get<bool>() ? 1 : 0;
		std::memcpy(p, &b, sizeof(b));
		return true;
	}
	if (!v.is_number()) { return fail(); }
	if (eq("f32")) { const float x = v.get<float>(); std::memcpy(p, &x, sizeof(x)); return true; }
	if (eq("f64")) { const double x = v.get<double>(); std::memcpy(p, &x, sizeof(x)); return true; }
	if (eq("i8"))  { const auto x = static_cast<std::int8_t>(v.get<int>());          std::memcpy(p, &x, sizeof(x)); return true; }
	if (eq("u8"))  { const auto x = static_cast<std::uint8_t>(v.get<unsigned>());    std::memcpy(p, &x, sizeof(x)); return true; }
	if (eq("i16")) { const auto x = static_cast<std::int16_t>(v.get<int>());         std::memcpy(p, &x, sizeof(x)); return true; }
	if (eq("u16")) { const auto x = static_cast<std::uint16_t>(v.get<unsigned>());   std::memcpy(p, &x, sizeof(x)); return true; }
	if (eq("i32")) { const auto x = v.get<std::int32_t>();  std::memcpy(p, &x, sizeof(x)); return true; }
	if (eq("u32")) { const auto x = v.get<std::uint32_t>(); std::memcpy(p, &x, sizeof(x)); return true; }
	if (eq("i64")) { const auto x = v.get<std::int64_t>();  std::memcpy(p, &x, sizeof(x)); return true; }
	if (eq("u64")) { const auto x = v.get<std::uint64_t>(); std::memcpy(p, &x, sizeof(x)); return true; }
	return fail();
}

/// @brief FieldDescriptor 表 (フラット化済み) に従い JSON オブジェクトを bytes へ書き込む
/// (`reflectToJson`, observe/Reflect.hpp の逆方向)。未知/欠落キーは無視、型不一致は false を
/// 積み重ねつつ他フィールドは処理を続ける (best-effort)。
inline bool writeFieldsFromJson(
	std::uint8_t* base, const FieldDescriptor* fields, std::int32_t fieldCount, const nlohmann::json& j)
{
	bool ok = true;
	for (std::int32_t i = 0; i < fieldCount; ++i)
	{
		const auto& f = fields[i];
		if (f.name[0] == '\0') { continue; }
		const nlohmann::json* v = findByDottedName(j, f.name);
		if (v == nullptr) { continue; }  // JSON 未指定 = 既定値のまま (未知キー無視と対称)
		const char* tag = f.typeTag;

		if (std::strcmp(tag, "vec") == 0)
		{
			if (!v->is_array())
			{
				console::noticef("配置の JSON の field \"%s\" が配列ではありません (%s)。配列で書いてください。",
					f.name, v->type_name());
				ok = false;
				continue;
			}
			std::uint32_t cnt = static_cast<std::uint32_t>(v->size());
			if (cnt > f.elemCount) { cnt = f.elemCount; }
			std::memcpy(base + f.offset + f.countOffset, &cnt, sizeof(cnt));
			const auto* sch = findSpawnerSchema(f.elemType);
			for (std::uint32_t k = 0; k < cnt; ++k)
			{
				std::uint8_t* ep = base + f.offset + static_cast<std::size_t>(k) * f.elemSize;
				const nlohmann::json& ev = (*v)[k];
				if (sch != nullptr) { ok = writeFieldsFromJson(ep, sch->fields, sch->fieldCount, ev) && ok; }
				else                { ok = writeSpawnerScalar(ep, f.elemType, ev, f.name) && ok; }
			}
		}
		else if (std::strcmp(tag, "str") == 0)
		{
			if (!v->is_string())
			{
				console::noticef("配置の JSON の field \"%s\" が文字列ではありません (%s)。文字列で書いてください。",
					f.name, v->type_name());
				ok = false;
				continue;
			}
			const std::string s = v->get<std::string>();
			copyTag(reinterpret_cast<char*>(base + f.offset), f.elemCount, s.c_str());
		}
		else if (std::strcmp(tag, "struct") == 0)
		{
			if (!v->is_object())
			{
				console::noticef("配置の JSON の field \"%s\" がオブジェクトではありません (%s)。{...} で書いてください。",
					f.name, v->type_name());
				ok = false;
				continue;
			}
			const auto* sch = findSpawnerSchema(f.elemType);
			if (sch != nullptr) { ok = writeFieldsFromJson(base + f.offset, sch->fields, sch->fieldCount, *v) && ok; }
		}
		else
		{
			ok = writeSpawnerScalar(base + f.offset, tag, *v, f.name) && ok;
		}
	}
	return ok;
}

/// @brief `writeFieldsFromJson` の逆方向 (ADR 0035「残す」の書き戻し)。FieldDescriptor 表に従い
/// bytes から現在値を読み、`out` の同じ dotted 名へ書く。JSON 側の既存の他キーは温存する
/// (struct に無いフィールドを消さない)。vec は現在の count 分だけ配列を作り直す。
inline void writeFieldsToJson(
	const std::uint8_t* base, const FieldDescriptor* fields, std::int32_t fieldCount, nlohmann::json& out)
{
	for (std::int32_t i = 0; i < fieldCount; ++i)
	{
		const auto& f = fields[i];
		if (f.name[0] == '\0') { continue; }
		const char* tag = f.typeTag;

		if (std::strcmp(tag, "vec") == 0)
		{
			std::uint32_t cnt = 0;
			std::memcpy(&cnt, base + f.offset + f.countOffset, sizeof(cnt));
			if (cnt > f.elemCount) { cnt = f.elemCount; }
			nlohmann::json arr = nlohmann::json::array();
			const auto* sch = findSpawnerSchema(f.elemType);
			for (std::uint32_t k = 0; k < cnt; ++k)
			{
				const std::uint8_t* ep = base + f.offset + static_cast<std::size_t>(k) * f.elemSize;
				nlohmann::json elem = nlohmann::json::object();
				if (sch != nullptr) { writeFieldsToJson(ep, sch->fields, sch->fieldCount, elem); }
				else                { elem = readSpawnerScalar(ep, f.elemType); }
				arr.push_back(std::move(elem));
			}
			touchByDottedName(out, f.name) = std::move(arr);
		}
		else if (std::strcmp(tag, "str") == 0)
		{
			const auto* p = reinterpret_cast<const char*>(base + f.offset);
			touchByDottedName(out, f.name) = std::string(p, ::strnlen(p, f.elemCount));  // 終端が無くても容量で止める
		}
		else if (std::strcmp(tag, "struct") == 0)
		{
			const auto* sch = findSpawnerSchema(f.elemType);
			if (sch != nullptr)
			{
				nlohmann::json& node = touchByDottedName(out, f.name);
				writeFieldsToJson(base + f.offset, sch->fields, sch->fieldCount, node);
			}
		}
		else
		{
			touchByDottedName(out, f.name) = readSpawnerScalar(base + f.offset, tag);
		}
	}
}

/// @brief `override` の各キーを `target` へ上書きする深いマージ (★1-10)。両方が object の
/// キーだけ再帰し、それ以外 (配列・スカラー・型不一致) は override 側の値でまるごと置き換える
/// (「差分だけ書けば残りは base のまま」を deep object のネストでも保つため)。
inline void mergeJsonInto(nlohmann::json& target, const nlohmann::json& override)
{
	if (!override.is_object()) { target = override; return; }
	if (!target.is_object()) { target = nlohmann::json::object(); }
	for (auto it = override.begin(); it != override.end(); ++it)
	{
		if (it.value().is_object() && target.contains(it.key()) && target[it.key()].is_object())
		{
			mergeJsonInto(target[it.key()], it.value());
		}
		else
		{
			target[it.key()] = it.value();
		}
	}
}

/// @brief `objects[startIndex]` から `"instanceOf": "<id>"` を最大 4 段まで辿り、根 (base) から
/// 順にマージした 1 個の JSON を返す (★1-10)。循環を検出したら `warnOnce` で 1 回だけ通知し、
/// 安全な側を選んで継承なし (`objects[startIndex]` そのまま) として扱う。参照先の id が同じ配列に無い
/// 場合もそこで継承を打ち切る (「壊れた prefab 参照でも配置全体を落とさない」の一貫)。
inline nlohmann::json resolveInstanceOf(
	const nlohmann::json& objects, const std::unordered_map<std::string, std::size_t>& idIndex,
	std::size_t startIndex)
{
	constexpr int kMaxDepth = 4;
	std::vector<std::size_t> chain{startIndex};  // [自分, 親, 祖父, ...] の順に積む

	std::size_t cur = startIndex;
	for (int depth = 0; depth < kMaxDepth; ++depth)
	{
		const auto& node = objects[cur];
		const auto it = node.is_object() ? node.find("instanceOf") : node.end();
		if (it == node.end() || !it->is_string()) { break; }
		const auto idxIt = idIndex.find(it->get<std::string>());
		if (idxIt == idIndex.end()) { break; }
		const std::size_t parent = idxIt->second;
		if (std::find(chain.begin(), chain.end(), parent) != chain.end())
		{
			mitiru::debug::warnOnce("spawner.instanceof.cycle",
				"配置の JSON の instanceOf が循環しているので、その要素は継承せずに読みます。instanceOf の指定を確かめてください。");
			return objects[startIndex];
		}
		chain.push_back(parent);
		cur = parent;
	}

	nlohmann::json merged = objects[chain.back()];
	for (auto rit = chain.rbegin() + 1; rit != chain.rend(); ++rit)
	{
		mergeJsonInto(merged, objects[*rit]);
	}
	if (!objects[startIndex].is_object() || !objects[startIndex].contains("id")) { merged.erase("id"); }
	return merged;
}

}  // namespace mitiru::module::detail

namespace mitiru::module
{

/// @brief JSON オブジェクト 1 個を `MITIRU_REFLECT_AUTO` 済み struct へ流し込む。フィールド名で
/// JSON を引き、記述子の kind (typeTag) に従って bytes を書く。JSON に無いフィールドは既定値の
/// まま、struct に無い JSON キーは無視、型が不一致なフィールドは false を返し stderr に 1 行出す
/// (他フィールドの書き込みは継続する)。T は aggregate かつ flat POD であること
/// (`MITIRU_REFLECT_AUTO` と同じ制約)。
/// origin を渡すと JSON の id から objectId を埋める (sourceFile と objectIndex は呼び出し元が
/// ファイルと配列位置を知っているので spawnAllFrom 側で埋める、ここでは触らない)。
template <class T>
[[nodiscard]] inline bool spawnFromJson(const nlohmann::json& j, T& out, SpawnOrigin* origin = nullptr)
{
	static_assert(std::is_aggregate_v<T>,
		"spawnFromJson<T>: T は aggregate 型である必要があります (MITIRU_REFLECT_AUTO と同じ制約)");
	static_assert(std::is_trivially_copyable_v<T>,
		"spawnFromJson<T>: T は flat POD (trivially_copyable) である必要があります");

	if (!j.is_object())
	{
		console::notice("spawnFromJson に渡した JSON がオブジェクトではありません。{...} の形で渡してください。");
		return false;
	}
	if (origin != nullptr)
	{
		origin->objectId    = 0;
		origin->sourceFile  = 0;
		origin->objectIndex = 0;
		origin->fromEditor  = 1;
		if (const auto it = j.find("id"); it != j.end())
		{
			if (it->is_string()) { origin->objectId = ::mitiru::util::Hash::fnv1a(it->get<std::string>()); }
			else if (it->is_number()) { origin->objectId = it->get<std::uint64_t>(); }
		}
	}
	auto fields = ::mitiru::module::detail::collectFields<T>(0, "");
	if constexpr (detail::HasTagsMember<T>::value)
	{
		// TagSet は aggregate なので collectFields が展開してしまうが、JSON 側は "tags": [...]
		// という配列 (struct 用の object 形式ではない) なので汎用書き込みからは除外し、専用経路に譲る。
		fields.erase(std::remove_if(fields.begin(), fields.end(), [](const FieldDescriptor& f) {
			const std::size_t len = std::strlen(f.name);
			if (len < 4 || std::strncmp(f.name, "tags", 4) != 0) { return false; }
			return len == 4 || f.name[4] == '.' || f.name[4] == '[';
		}), fields.end());
	}
	const bool ok = detail::writeFieldsFromJson(
		reinterpret_cast<std::uint8_t*>(&out), fields.data(),
		static_cast<std::int32_t>(fields.size()), j);

	// TagSet は collectFields の汎用記述子で表現できないため専用経路で書く (§3-4)。
	if constexpr (detail::HasTagsMember<T>::value)
	{
		if (const auto it = j.find("tags"); it != j.end()) { detail::writeTagsFromJson(out.tags, *it); }
	}
	return ok;
}

/// @brief `spawnFromJson<T>` のプレハブ継承版 (★1-10、HSON の `instanceOf` 相当)。`base` の値を
/// 既定にして `j` で上書きしてから通常の `spawnFromJson<T>` を呼ぶ。深いオブジェクトはネストごと
/// マージされるので、リング 50 個の配置でも差分のフィールドだけ書けば済む。`id` は `base` からは
/// 継承しない (`j` に無ければ未指定のまま、テンプレート自身の id が紛れ込まないようにする)。
template <class T>
[[nodiscard]] inline bool spawnFromJsonWithBase(
	const nlohmann::json& base, const nlohmann::json& j, T& out, SpawnOrigin* origin = nullptr)
{
	nlohmann::json merged = base.is_object() ? base : nlohmann::json::object();
	detail::mergeJsonInto(merged, j);
	if (!j.is_object() || !j.contains("id")) { merged.erase("id"); }
	return spawnFromJson<T>(merged, out, origin);
}

/// @brief `spawnFromJson<T>` の逆方向 (ADR 0035「残す」)。`obj` の現在値を `out` の同じ dotted
/// 名の JSON キーへ書き戻す。scene.json の `objects[]` 要素 1 個を、ゲーム内で編集した後の値で
/// 上書きしたいときに使う (呼び出しは Game 側の責務。host はどの JSON がどの struct を
/// 生んだかを知らないので、この関数を呼ぶかどうかは commit intent を受けた game 側が決める)。
template <class T>
inline void writeBackToJson(const T& obj, nlohmann::json& out)
{
	static_assert(std::is_aggregate_v<T>,
		"writeBackToJson<T>: T は aggregate 型である必要があります (MITIRU_REFLECT_AUTO と同じ制約)");
	static_assert(std::is_trivially_copyable_v<T>,
		"writeBackToJson<T>: T は flat POD (trivially_copyable) である必要があります");

	const auto fields = ::mitiru::module::detail::collectFields<T>(0, "");
	detail::writeFieldsToJson(
		reinterpret_cast<const std::uint8_t*>(&obj), fields.data(),
		static_cast<std::int32_t>(fields.size()), out);
}

/// @brief 型消去版 (host の `FieldDescriptor*` テーブル、例えば `ModuleReflection::fields`) から
/// 直接呼びたい場合の入口。`writeBackToJson<T>` と同じ処理を、既に集めた記述子配列に対して行う。
inline void writeBackFieldsToJson(
	const std::uint8_t* base, const FieldDescriptor* fields, std::int32_t fieldCount, nlohmann::json& out)
{
	detail::writeFieldsToJson(base, fields, fieldCount, out);
}

/// @brief 型の台帳に載せる配置用メタ情報 (flat POD、DLL 境界を渡る)。
/// `MITIRU_SPAWNER_INFO` で書く。無ければ caption = jsonName、placeable = 1、eternal = 0。
struct SpawnerTypeInfo
{
	char         caption[32];  ///< 人間向け表示名 (UTF-8、null 終端)
	char         group[16];    ///< "地形" / "ギミック" / "アイテム" (FieldAttr の group と同じ受け皿)
	std::uint8_t placeable;    ///< 1 = エディタ/AI が置いてよい (Player 等は 0)
	std::uint8_t eternal;      ///< 1 = 距離スポーンの対象外で常駐 (HE2 の GetStatusEternal)
	std::uint8_t _pad[6];
};
static_assert(sizeof(SpawnerTypeInfo) == 56, "SpawnerTypeInfo は DLL 境界を渡る POD");

/// @brief host が `mitiru_module_spawner_types` で受け取る 1 行 (`GET /api/ai/types` の元)。
struct SpawnerTypeEntry
{
	char            jsonName[32];
	std::uint32_t   size;        ///< sizeof(Type)
	std::uint32_t   _pad;
	SpawnerTypeInfo info;
};
static_assert(sizeof(SpawnerTypeEntry) == 96, "SpawnerTypeEntry は DLL 境界を渡る POD");

namespace detail
{

/// @brief 型消去した spawn 関数。dst (少なくとも sizeof(T) 分) へ `spawnFromJson<T>` する。
/// originOut は nullptr でもよい (`spawnAll` の出自なし経路)。
using SpawnerFn = bool (*)(const nlohmann::json&, void* dst, std::size_t dstCap, SpawnOrigin* originOut);

template <class T>
inline bool spawnErased(const nlohmann::json& j, void* dst, std::size_t dstCap, SpawnOrigin* originOut)
{
	if (dstCap < sizeof(T)) { return false; }
	T tmp{};
	const bool ok = ::mitiru::module::spawnFromJson<T>(j, tmp, originOut);
	std::memcpy(dst, &tmp, sizeof(T));
	return ok;
}

/// @brief `MITIRU_SPAWNER` が積む 1 件 (JSON の "type" 文字列 → spawn 関数)。
struct SpawnerEntry
{
	char        jsonName[32];
	SpawnerFn   spawn;
	std::size_t size;
};

/// @brief 固定長の登録テーブル (Game DLL 内 static、他 DLL とは共有しない)。
constexpr std::size_t kMaxSpawnerEntries = 64;

/// @brief `MITIRU_SPAWNER_INFO` が積む「配置に要るメタ情報」(HE2 の GameObjectClass::attributes 相当)。
struct SpawnerInfoEntry
{
	char jsonName[32];
	SpawnerTypeInfo info;
};

inline SpawnerInfoEntry* spawnerInfoEntries()
{
	static SpawnerInfoEntry table[kMaxSpawnerEntries]{};
	return table;
}

inline int& spawnerInfoEntryCount()
{
	static int n = 0;
	return n;
}

inline bool registerSpawnerInfo(const char* jsonName, const char* caption, const char* group,
                                bool placeable, bool eternal)
{
	auto& n = spawnerInfoEntryCount();
	if (n >= static_cast<int>(kMaxSpawnerEntries)) { return false; }
	auto& e = spawnerInfoEntries()[n];
	copyTag(e.jsonName, sizeof(e.jsonName), jsonName);
	copyTag(e.info.caption, sizeof(e.info.caption), caption != nullptr ? caption : jsonName);
	copyTag(e.info.group, sizeof(e.info.group), group != nullptr ? group : "");
	e.info.placeable = placeable ? 1 : 0;
	e.info.eternal   = eternal ? 1 : 0;
	++n;
	return true;
}
/// @brief `spawnAll` が呼び出し側に渡す一時バッファの上限 (spawn 対象 struct の最大サイズ)。
constexpr std::size_t kMaxSpawnObjectBytes = 4096;

inline SpawnerEntry* spawnerEntries()
{
	static SpawnerEntry table[kMaxSpawnerEntries]{};
	return table;
}

inline int& spawnerEntryCount()
{
	static int n = 0;
	return n;
}

/// @brief `MITIRU_SPAWNER(Type, "jsonName")` の実体 (static 変数初期化で 1 回だけ走る)。
inline bool registerSpawner(const char* jsonName, SpawnerFn fn, std::size_t size)
{
	auto& n = spawnerEntryCount();
	if (n >= static_cast<int>(kMaxSpawnerEntries))
	{
		console::noticef("MITIRU_SPAWNER は %zu 個までなので、\"%s\" は登録しません。", kMaxSpawnerEntries,
			jsonName != nullptr ? jsonName : "");
		return false;
	}
	auto* table = spawnerEntries();
	copyTag(table[n].jsonName, sizeof(table[n].jsonName), jsonName);
	table[n].spawn = fn;
	table[n].size  = size;
	++n;
	return true;
}

/// @brief 登録済み型を `out[0..cap)` へ並べて件数を返す (jsonName の宣言順)。`MITIRU_SPAWNER_INFO` が
/// 無い型は既定のメタ情報 (caption = jsonName、placeable = 1)。
inline int collectSpawnerTypes(SpawnerTypeEntry* out, int cap) noexcept
{
	if (out == nullptr || cap <= 0) { return 0; }
	const auto* table = spawnerEntries();
	const int   n     = spawnerEntryCount();
	int written = 0;
	for (int i = 0; i < n && written < cap; ++i)
	{
		SpawnerTypeEntry& e = out[written];
		e = SpawnerTypeEntry{};
		copyTag(e.jsonName, sizeof(e.jsonName), table[i].jsonName);
		e.size = static_cast<std::uint32_t>(table[i].size);
		copyTag(e.info.caption, sizeof(e.info.caption), table[i].jsonName);
		e.info.placeable = 1;
		const auto* infos = spawnerInfoEntries();
		for (int k = 0; k < spawnerInfoEntryCount(); ++k)
		{
			if (std::strcmp(infos[k].jsonName, table[i].jsonName) == 0) { e.info = infos[k].info; break; }
		}
		++written;
	}
	return written;
}

}  // namespace detail

namespace detail
{
/// @brief bool フィールドを読む。無い・bool でない時は fallback。
[[nodiscard]] inline bool jsonFlag(const nlohmann::json& node, const char* key, bool fallback) noexcept
{
	const auto it = node.find(key);
	return (it != node.end() && it->is_boolean()) ? it->get<bool>() : fallback;
}
}  // namespace detail

/// @brief `spawnAll` の出自つき版 (★1-1)。`sourceFile` は `spawnSourceFileHash` で配置ファイル名
/// から作ったハッシュ (呼び出し元で 1 回だけ計算して渡す。0 = ファイル不明)。`spawnFromJson` 自身は
/// 単発の JSON オブジェクトしか見えず配列内の位置もファイル名も知らないので、`objectIndex` と
/// `sourceFile` はここで補ってから `visitor(typeName, bytes, size, origin)` へ渡す。
template <class Visitor>
inline void spawnAllFrom(const nlohmann::json& objects, std::uint32_t sourceFile, Visitor&& visitor)
{
	if (!objects.is_array())
	{
		console::notice("配置の JSON が配列ではありません。一番外側を配列にしてください。");
		return;
	}
	const auto* table = detail::spawnerEntries();
	const int   n      = detail::spawnerEntryCount();

	// "instanceOf" (★1-10) が同じ配列内の "id" を指す経路。ここで先に id → 添字表を作っておく。
	std::unordered_map<std::string, std::size_t> idIndex;
	for (std::size_t idx = 0; idx < objects.size(); ++idx)
	{
		const auto& node = objects[idx];
		const auto  idIt = node.is_object() ? node.find("id") : node.end();
		if (idIt != node.end() && idIt->is_string()) { idIndex.emplace(idIt->get<std::string>(), idx); }
	}

	std::uint32_t index = 0;
	for (std::size_t rawIndex = 0; rawIndex < objects.size(); ++rawIndex)
	{
		const std::uint32_t thisIndex = index++;
		const auto& raw = objects[rawIndex];
		if (!raw.is_object())
		{
			console::noticef("配置の JSON の %zu 番目はオブジェクトではない (%s) ので、飛ばします。", rawIndex, raw.type_name());
			continue;
		}
		// HSON の isExcluded (ビルドから除外) / isEditorVisible=false (雛形。instanceOf の base に
		// するだけで世界には出さない) は spawn しない。フラグは継承させず raw 自身のものだけ見る。
		// objectIndex は配列の生添字のまま進める (SpawnOrigin が JSON 上の位置を指し続けるため)。
		if (detail::jsonFlag(raw, "isExcluded", false) || !detail::jsonFlag(raw, "isEditorVisible", true)) { continue; }
		const nlohmann::json resolved = raw.contains("instanceOf")
			? detail::resolveInstanceOf(objects, idIndex, rawIndex)
			: raw;
		const auto& j = resolved;
		const auto it = j.find("type");
		if (it == j.end() || !it->is_string())
		{
			console::noticef("配置の JSON の %zu 番目には \"type\" がないので、飛ばします。", rawIndex);
			continue;
		}
		const std::string typeName = it->get<std::string>();

		bool found = false;
		for (int i = 0; i < n; ++i)
		{
			if (typeName != table[i].jsonName) { continue; }
			found = true;
			if (table[i].size > detail::kMaxSpawnObjectBytes)
			{
				console::noticef("配置の型 \"%s\" は %zu byte より大きいので、配置から作れません。struct を小さくしてください。",
					typeName.c_str(), detail::kMaxSpawnObjectBytes);
				break;
			}
			alignas(std::max_align_t) unsigned char buf[detail::kMaxSpawnObjectBytes];
			SpawnOrigin origin{};
			const bool ok = table[i].spawn(j, buf, sizeof(buf), &origin);
			if (!ok)
			{
				console::noticef("配置の JSON の %zu 番目 (型 \"%s\") を struct に写すのに失敗しました。直前の知らせにある field を確かめてください。",
					rawIndex, typeName.c_str());
			}
			origin.sourceFile  = sourceFile;
			origin.objectIndex = thisIndex;
			visitor(typeName.c_str(), buf, table[i].size, origin);
			break;
		}
		if (!found)
		{
			console::noticef("配置の JSON の type \"%s\" は MITIRU_SPAWNER に登録されていないので、飛ばします。", typeName.c_str());
		}
	}
}

/// @brief `"type"` フィールドを持つ JSON オブジェクト配列を一括処理する。各要素の `"type"` を
/// `MITIRU_SPAWNER` 登録済みの jsonName と突き合わせ、一致した型で `spawnFromJson` した結果を
/// `visitor(typeName, bytes, size)` へ渡す。visitor 側は typeName で分岐して bytes を目的の型へ
/// memcpy する (Host-Game 境界の signal-only 流儀と同じく、この境界も raw bytes + タグで薄く
/// 保つ)。`"type"` が無い要素・未登録の type・struct サイズが `kMaxSpawnObjectBytes` を超える
/// 型は stderr に 1 行出して skip する。出自 (`SpawnOrigin`) も要るなら `spawnAllFrom` を使う。
template <class Visitor>
inline void spawnAll(const nlohmann::json& objects, Visitor&& visitor)
{
	spawnAllFrom(objects, 0, [&](const char* type, const void* bytes, std::size_t size, const SpawnOrigin&)
	{
		visitor(type, bytes, size);
	});
}

}  // namespace mitiru::module

/// @brief JSON の `"type": "jsonName"` を Type へ結び付ける (グローバル scope で書くこと)。
/// `MITIRU_REFLECT_STRUCT` と同じ「static 変数の初期化子で登録関数を呼ぶ」形。1 ファイルに
/// 複数回書いてよい (spawnAll がテーブル全体を線形探索する)。
/// 型の配置用メタ情報 (caption / group / placeable / eternal)。`MITIRU_SPAWNER` と併記する。
#define MITIRU_SPAWNER_INFO(Type, JsonName, Caption, Group, Placeable, Eternal)                 \
	namespace mitiru::module::detail                                                        \
	{                                                                                        \
		template <> inline const bool SpawnerInfoRegistered<Type> =                          \
			::mitiru::module::detail::registerSpawnerInfo(JsonName, Caption, Group, (Placeable), (Eternal)); \
	}

/// 型の台帳を host へ出す (optional)。1 つの TU (MITIRU_GAME と同じ場所) に 1 回書く。host は
/// `GET /api/ai/types` と分岐エディタの「置く」で読む。無ければ types は「未対応」と返る。
#define MITIRU_SPAWNER_TYPES_EXPORT()                                                    \
	extern "C" MITIRU_GAME_EXPORT                                                           \
	int mitiru_module_spawner_types(::mitiru::module::SpawnerTypeEntry* out, int cap)       \
	{                                                                                        \
		return ::mitiru::module::detail::collectSpawnerTypes(out, cap);                      \
	}

// 登録変数は型で特殊化した変数テンプレートにする。名前に Type を付けると `ns::Type` が識別子に
// ならず、__COUNTER__ だと TU ごとに同名の inline 変数が ODR で 1 つにまとめられて登録が抜け落ちる。
namespace mitiru::module::detail
{
template <class T> inline const bool SpawnerRegistered = false;
template <class T> inline const bool SpawnerInfoRegistered = false;
}
#define MITIRU_SPAWNER(Type, JsonName)                                                     \
	namespace mitiru::module::detail                                                        \
	{                                                                                        \
		template <> inline const bool SpawnerRegistered<Type> = ::mitiru::module::detail::registerSpawner( \
			JsonName, &::mitiru::module::detail::spawnErased<Type>, sizeof(Type));           \
	}
