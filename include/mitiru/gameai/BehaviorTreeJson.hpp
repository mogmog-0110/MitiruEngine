#pragma once

/// @file BehaviorTreeJson.hpp
/// @brief JSON からビヘイビアツリーを組む。読み込み時に 1 度だけ呼び、tick では使わない。
/// @details 各ノードは {"type": 種類, ...} の object で表し、種類ごとの欄は docs/GAME_AI.md の表に従う。
///          葉の名前はゲームが渡す表で番号に直すため、表にない名前は読み込み時にエラーになる。

#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>
#include <mitiru/data/JsonDiagnostics.hpp>

#include <mitiru/gameai/BehaviorTree.hpp>

namespace mitiru::gameai
{

struct BtLeafName
{
	const char*   name = "";
	std::uint16_t id   = 0;
};

namespace bt_json_detail
{

struct KindName
{
	const char* name;
	BtKind      kind;
	const char* paramKey;  ///< param に入れる欄 (無ければ nullptr)
};

inline constexpr KindName kKinds[] = {
	{"sequence", BtKind::Sequence, nullptr},
	{"selector", BtKind::Selector, nullptr},
	{"reactiveSequence", BtKind::ReactiveSequence, nullptr},
	{"reactiveSelector", BtKind::ReactiveSelector, nullptr},
	{"parallel", BtKind::Parallel, "success"},
	{"inverter", BtKind::Inverter, nullptr},
	{"succeeder", BtKind::Succeeder, nullptr},
	{"cooldown", BtKind::Cooldown, "frames"},
	{"repeat", BtKind::Repeat, "count"},
	{"timeout", BtKind::Timeout, "frames"},
	{"wait", BtKind::Wait, "frames"},
	{"action", BtKind::Action, "param"},
	{"condition", BtKind::Condition, "param"},
};

struct Loader
{
	BtBuilder&                   b;
	std::span<const BtLeafName>  leaves;
	std::string                  error;

	bool fail(const std::string& path, const std::string& why)
	{
		if (error.empty()) { error = path + ": " + why; }
		return false;
	}

	const KindName* kindOf(const nlohmann::json& j) const
	{
		const auto it = j.find("type");
		if (it == j.end() || !it->is_string()) { return nullptr; }
		const std::string& s = it->get_ref<const std::string&>();
		for (const KindName& k : kKinds)
		{
			if (s == k.name) { return &k; }
		}
		return nullptr;
	}

	bool paramOf(const nlohmann::json& j, const KindName& k, const std::string& path, std::int32_t& out)
	{
		out = 0;
		if (k.paramKey == nullptr || !j.contains(k.paramKey)) { return true; }
		const nlohmann::json& v = j[k.paramKey];
		if (!v.is_number_integer() || v.get<long long>() < 0 || v.get<long long>() > 0x7FFFFFFF)
		{
			return fail(path, std::string("\"") + k.paramKey + "\" は 0 以上の整数");
		}
		out = static_cast<std::int32_t>(v.get<long long>());
		return true;
	}

	bool leafId(const nlohmann::json& j, const std::string& path, std::uint16_t& out)
	{
		const auto it = j.find("name");
		if (it == j.end() || !it->is_string()) { return fail(path, "葉に \"name\" が無い"); }
		const std::string& s = it->get_ref<const std::string&>();
		for (const BtLeafName& l : leaves)
		{
			if (s == l.name) { out = l.id; return true; }
		}
		return fail(path, "知らない葉の名前 \"" + s + "\"");
	}

	bool node(const nlohmann::json& j, const std::string& path)
	{
		if (!j.is_object()) { return fail(path, "ノードは object"); }
		const KindName* k = kindOf(j);
		if (k == nullptr) { return fail(path, "\"type\" が無いか、知らない種類"); }
		std::int32_t param = 0;
		if (!paramOf(j, *k, path, param)) { return false; }
		if (k->kind == BtKind::Wait) { b.wait(param); return true; }
		if (btIsLeaf(k->kind))
		{
			std::uint16_t id = 0;
			if (!leafId(j, path, id)) { return false; }
			if (k->kind == BtKind::Action) { b.action(id, param); }
			else { b.condition(id, param); }
			return true;
		}
		b.open(k->kind, param);
		const bool ok = btIsComposite(k->kind) ? children(j, path) : child(j, path);
		b.end();
		return ok;
	}

	bool children(const nlohmann::json& j, const std::string& path)
	{
		const auto it = j.find("children");
		if (it == j.end() || !it->is_array()) { return fail(path, "\"children\" (配列) が無い"); }
		for (std::size_t i = 0; i < it->size(); ++i)
		{
			if (!node((*it)[i], path + ".children[" + std::to_string(i) + "]")) { return false; }
		}
		return true;
	}

	bool child(const nlohmann::json& j, const std::string& path)
	{
		const auto it = j.find("child");
		if (it == j.end()) { return fail(path, "\"child\" が無い"); }
		return node(*it, path + ".child");
	}
};

} // namespace bt_json_detail

/// @return 失敗時は空の木 (count == 0) を返し、error に場所と理由を入れる。
[[nodiscard]] inline BtTree loadBehaviorTreeJson(std::string_view text, std::span<const BtLeafName> leaves,
                                                 std::string* error = nullptr)
{
	std::string syntaxError;
	const nlohmann::json root = data::parseJsonText(text, "ビヘイビアツリーの JSON", syntaxError);
	BtBuilder b;
	bt_json_detail::Loader loader{b, leaves, {}};
	if (root.is_discarded()) { loader.error = syntaxError; }
	else { loader.node(root, "$"); }
	const BtTree tree = b.build();
	if (loader.error.empty() && b.error != nullptr) { loader.error = b.error; }
	if (error != nullptr) { *error = loader.error; }
	return loader.error.empty() ? tree : BtTree{};
}

[[nodiscard]] inline BtTree loadBehaviorTreeJsonFile(const char* path, std::span<const BtLeafName> leaves,
                                                     std::string* error = nullptr)
{
	std::ifstream f(path, std::ios::binary);
	if (!f)
	{
		if (error != nullptr) { *error = std::string("ビヘイビアツリーを開けない: ") + path; }
		return BtTree{};
	}
	const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	return loadBehaviorTreeJson(text, leaves, error);
}

} // namespace mitiru::gameai
