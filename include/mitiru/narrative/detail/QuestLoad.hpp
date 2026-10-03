#pragma once

/// @file QuestLoad.hpp
/// @brief クエストの表の JSON を QuestTable に読む。

#include <optional>
#include <string>
#include <string_view>

namespace mitiru::narrative
{

namespace quest_detail
{

using Json = nlohmann::json;

struct Loader
{
	QuestTable&    out;
	NarrativeNames names;
	std::string    error;

	bool fail(const std::string& where, const std::string& why)
	{
		if (error.empty()) { error = where + ": " + why; }
		return false;
	}

	std::string text(const Json& j, const char* key, const std::string& where)
	{
		if (!j.contains(key)) { return {}; }
		if (!j[key].is_string()) { fail(where + "." + key, "文字列でない"); return {}; }
		return j[key].get<std::string>();
	}

	ExprRef expr(const Json& j, const char* key, const std::string& where)
	{
		std::string why;
		const ExprRef ref = out.exprs.compile(text(j, key, where), names, why);
		if (!why.empty()) { fail(where + "." + key, why); }
		return ref;
	}

	LocalText local(const Json& j, const std::string& where, const std::string& autoKey)
	{
		LocalText t;
		t.source = text(j, "text", where);
		t.text = t.source;
		t.key = j.contains("key") ? text(j, "key", where) : autoKey;
		return t;
	}

	void objectives(const Json& arr, QuestDef& q, const std::string& where)
	{
		if (!arr.is_array()) { fail(where, "配列でない"); return; }
		if (arr.size() > static_cast<std::size_t>(QuestLog::kMaxObjectives)) { fail(where, "目標は 32 個まで"); return; }
		for (std::size_t i = 0; i < arr.size() && error.empty(); ++i)
		{
			const std::string w = where + "[" + std::to_string(i) + "]";
			QuestObjective o;
			o.name = text(arr[i], "id", w);
			o.text = local(arr[i], w, "quest." + q.name + "." + o.name);
			o.done = expr(arr[i], "done", w);
			if (o.name.empty()) { fail(w, "id が無い"); }
			if (o.done.count == 0) { fail(w, "done (済んだと見なす条件) が無い"); }
			q.objectives.push_back(std::move(o));
		}
	}

	void quest(const Json& j, std::size_t i)
	{
		const std::string w = "quests[" + std::to_string(i) + "]";
		QuestDef q;
		q.name = text(j, "id", w);
		if (q.name.empty()) { fail(w, "id が無い"); return; }
		q.id = names.intern(q.name);
		if (!names.error().empty()) { fail(w, names.error()); return; }
		Json title = Json::object();
		title["text"] = j.contains("title") ? j["title"] : Json("");
		if (j.contains("titleKey")) { title["key"] = j["titleKey"]; }
		q.title = local(title, w + ".title", "quest." + q.name);
		q.start = expr(j, "start", w);
		q.complete = expr(j, "complete", w);
		q.fail = expr(j, "fail", w);
		if (error.empty() && q.start.count == 0) { fail(w, "start (始まる条件) が無い"); }
		objectives(j.contains("objectives") ? j["objectives"] : Json::array(), q, w + ".objectives");
		out.quests.push_back(std::move(q));
	}
};

}  // namespace quest_detail

[[nodiscard]] inline std::optional<QuestTable> loadQuestsFromJson(std::string_view json, std::string& error)
{
	const auto root = quest_detail::Json::parse(json.begin(), json.end(), nullptr, false);
	if (root.is_discarded() || !root.is_object() || !root.contains("quests") || !root["quests"].is_array())
	{
		error = "クエストの表は {\"quests\": [...]} の JSON にする";
		return std::nullopt;
	}
	QuestTable table;
	quest_detail::Loader loader{table, {}, {}};
	for (std::size_t i = 0; i < root["quests"].size() && loader.error.empty(); ++i) { loader.quest(root["quests"][i], i); }
	error = loader.error;
	if (!error.empty()) { return std::nullopt; }
	table.names = loader.names.sortedNames();
	return table;
}

[[nodiscard]] inline std::optional<QuestTable> loadQuestFile(std::string_view path, std::string& error)
{
	const auto bytes = vfs::readAsset(path);
	if (!bytes) { error = "クエストの表を読めない: " + std::string(path); return std::nullopt; }
	auto table = loadQuestsFromJson(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), error);
	if (!error.empty()) { error = std::string(path) + ": " + error; }
	return table;
}

}  // namespace mitiru::narrative
