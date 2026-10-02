#include "SnapshotFormat.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>

namespace mitiru::tool
{

namespace
{

using Json = nlohmann::json;

bool contains(const std::vector<std::string>& list, std::string_view key)
{
	return std::find(list.begin(), list.end(), key) != list.end();
}

// UTF-8 の 1 文字の先頭バイトか (続きのバイトは 10xxxxxx)。
bool isLeadByte(char c) noexcept
{
	return (static_cast<unsigned char>(c) & 0xC0u) != 0x80u;
}

std::size_t codepointCount(const std::string& s)
{
	return static_cast<std::size_t>(std::count_if(s.begin(), s.end(), isLeadByte));
}

std::string firstCodepoints(const std::string& s, std::size_t n)
{
	std::size_t seen = 0;
	for (std::size_t i = 0; i < s.size(); ++i)
	{
		if (isLeadByte(s[i]) && seen++ == n) { return s.substr(0, i); }
	}
	return s;
}

bool isIntegral(double d) noexcept
{
	return std::isfinite(d) && std::floor(d) == d && std::fabs(d) < 1e21;
}

std::string numberString(const Snapshot& v)
{
	if (v.is_number_integer() || v.is_number_unsigned()) { return v.dump(); }
	const double d = v.get<double>();
	if (isIntegral(d))
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%.0f", d);
		return buf;
	}
	return v.dump();
}

bool isNumberIntegral(const Snapshot& v)
{
	return v.is_number_integer() || v.is_number_unsigned() || isIntegral(v.get<double>());
}

std::string summarizeObject(const Snapshot& v)
{
	std::string s;
	for (auto it = v.begin(); it != v.end(); ++it)
	{
		if (!s.empty()) { s += "  \xC2\xB7  "; }
		const Snapshot& x = it.value();
		s += it.key() + ' ' + ((x.is_object() || x.is_array()) ? std::string("\xE2\x80\xA6") : jsString(x));
	}
	if (codepointCount(s) > 64) { s = firstCodepoints(s, 62) + "\xE2\x80\xA6"; }
	return s;
}

std::string summarizeArray(const Snapshot& v)
{
	const bool allPrimitive = std::all_of(v.begin(), v.end(),
		[](const Snapshot& x) { return !x.is_object() && !x.is_array(); });
	if (!allPrimitive) { return std::to_string(v.size()) + " items"; }
	std::string s = "[ ";
	for (std::size_t i = 0; i < v.size(); ++i)
	{
		if (i > 0) { s += ", "; }
		// JS の Array.join は null を空文字にする
		if (!v[i].is_null()) { s += jsString(v[i]); }
	}
	return s + " ]";
}

std::vector<std::string> splitOn(std::string_view s, char sep)
{
	std::vector<std::string> out;
	std::size_t start = 0;
	while (true)
	{
		const std::size_t at = s.find(sep, start);
		out.emplace_back(s.substr(start, at == std::string_view::npos ? std::string_view::npos : at - start));
		if (at == std::string_view::npos) { return out; }
		start = at + 1;
	}
}

double toNumber(const std::string& s)
{
	return std::strtod(s.c_str(), nullptr);
}

Json plainRow(const std::optional<std::string>& key, const std::string& text)
{
	return Json{ { "group", false }, { "k", key.value_or("") }, { "has_k", key.has_value() },
	             { "v", text }, { "w", "text" }, { "min", 0 }, { "max", 0 }, { "val", 0 } };
}

// 値を disabled の部品の形にする (mitiru_reflect_decorate.js の decorateRow)。
Json decoratedRow(const std::string& key, const Snapshot& value, const ReflectMeta& meta)
{
	Json row = plainRow(key, kvValue(value));
	if (meta.widget == ReflectMeta::Widget::Enum)
	{
		const int index = value.is_number() ? static_cast<int>(value.get<double>()) : 0;
		const bool inRange = index >= 0 && index < static_cast<int>(meta.names.size());
		row["w"] = "enum";
		row["v"] = inRange ? meta.names[static_cast<std::size_t>(index)] : std::string();
	}
	else if (meta.widget == ReflectMeta::Widget::Range && value.is_number())
	{
		const double d = value.get<double>();
		row["w"] = "range";
		row["min"] = meta.min;
		row["max"] = meta.max;
		row["val"] = d;
		row["v"] = isNumberIntegral(value) ? numberString(value) : toFixed(d, 2);
	}
	return row;
}

ReflectMeta metaFor(const Snapshot& metaTable, const std::string& key)
{
	if (!metaTable.is_object()) { return {}; }
	const auto it = metaTable.find(key);
	if (it == metaTable.end() || !it->is_object()) { return {}; }
	const auto type = it->find("elemType");
	if (type == it->end() || !type->is_string()) { return {}; }
	return parseReflectMeta(type->get<std::string>());
}

// 組の付いた行を、最初に出てきた位置へ 1 つの組としてまとめる (foldGroups と同じ並び)。
Json foldGroups(std::vector<std::pair<std::string, Json>>&& rows,
                const std::set<std::string, std::less<>>& openGroups)
{
	Json items = Json::array();
	std::map<std::string, std::size_t, std::less<>> groupAt;
	for (auto& [group, row] : rows)
	{
		if (group.empty()) { items.push_back(std::move(row)); continue; }
		const auto found = groupAt.find(group);
		if (found != groupAt.end()) { items[found->second]["rows"].push_back(std::move(row)); continue; }
		groupAt.emplace(group, items.size());
		items.push_back(Json{ { "group", true }, { "name", group }, { "open", openGroups.contains(group) },
		                      { "rows", Json::array({ std::move(row) }) } });
	}
	return items;
}

Json objectRows(const Snapshot& entry, const Snapshot& state, bool decorate,
                const std::set<std::string, std::less<>>& openGroups)
{
	std::vector<std::string> keys;
	if (entry.is_object() && entry.contains("order") && entry["order"].is_array())
	{
		for (const auto& k : entry["order"]) { if (k.is_string()) { keys.push_back(k.get<std::string>()); } }
	}
	else
	{
		for (auto it = state.begin(); it != state.end(); ++it) { keys.push_back(it.key()); }
	}
	const Snapshot& metaTable = (decorate && entry.contains("meta")) ? entry["meta"] : Snapshot();
	std::vector<std::pair<std::string, Json>> rows;
	for (const std::string& k : keys)
	{
		const auto it = state.find(k);
		if (it == state.end()) { continue; }
		const ReflectMeta meta = metaFor(metaTable, k);
		rows.emplace_back(meta.group, meta.any() ? decoratedRow(k, *it, meta) : plainRow(k, kvValue(*it)));
	}
	return foldGroups(std::move(rows), openGroups);
}

Json sectionFor(const std::string& key, const Snapshot& entry, const std::set<std::string, std::less<>>& openGroups)
{
	const bool hasTitle = entry.is_object() && entry.contains("title") && entry["title"].is_string()
	                      && !entry["title"].get<std::string>().empty();
	Json section{ { "title", hasTitle ? entry["title"].get<std::string>() : key } };
	const bool wrapped = entry.is_object() && entry.contains("state")
	                     && (entry["state"].is_object() || entry["state"].is_array() || entry["state"].is_null());
	const Snapshot& state = wrapped ? entry["state"] : entry;
	if (state.is_object())
	{
		section["items"] = objectRows(entry, state, key == "gameMemory", openGroups);
	}
	else if (state.is_array())
	{
		Json items = Json::array();
		for (std::size_t i = 0; i < state.size(); ++i)
		{
			items.push_back(plainRow("[" + std::to_string(i) + "]", kvValue(state[i])));
		}
		section["items"] = std::move(items);
	}
	else
	{
		section["items"] = Json::array({ plainRow(std::nullopt, kvValue(state)) });
	}
	return section;
}

std::vector<std::string> sectionKeys(const Snapshot& snap, const KvFilter& filter)
{
	std::vector<std::string> keys;
	if (!snap.is_object()) { return keys; }
	for (auto it = snap.begin(); it != snap.end(); ++it)
	{
		if (!filter.only.empty() && !contains(filter.only, it.key())) { continue; }
		if (contains(filter.skip, it.key())) { continue; }
		keys.push_back(it.key());
	}
	return keys;
}

void appendTreeRows(Json& out, const std::string& key, const Snapshot& node, int depth, const std::string& path,
                    const std::set<std::string, std::less<>>& collapsed)
{
	Json row{ { "indent", Json(Json::array_t(static_cast<std::size_t>(depth), 0)) }, { "key", key }, { "path", path },
	          { "leaf", true }, { "open", false }, { "val", "" }, { "hint", "" } };
	if (!node.is_object() && !node.is_array())
	{
		row["val"] = jsString(node);
		out.push_back(std::move(row));
		return;
	}
	const bool open = !collapsed.contains(path);
	const std::size_t n = node.size();
	row["leaf"] = false;
	row["open"] = open;
	if (n > 0) { row["hint"] = std::to_string(n) + (node.is_array() ? " items" : " keys"); }
	out.push_back(std::move(row));
	if (!open) { return; }
	if (node.is_array())
	{
		for (std::size_t i = 0; i < n; ++i)
		{
			const std::string childKey = "[" + std::to_string(i) + "]";
			appendTreeRows(out, childKey, node[i], depth + 1, path + "/" + childKey, collapsed);
		}
		return;
	}
	for (auto it = node.begin(); it != node.end(); ++it)
	{
		appendTreeRows(out, it.key(), it.value(), depth + 1, path + "/" + it.key(), collapsed);
	}
}

} // namespace

std::string toFixed(double v, int digits)
{
	char buf[64];
	std::snprintf(buf, sizeof(buf), "%.*f", digits, v);
	return buf;
}

std::string jsString(const Snapshot& v)
{
	if (v.is_null()) { return "null"; }
	if (v.is_string()) { return v.get<std::string>(); }
	if (v.is_boolean()) { return v.get<bool>() ? "true" : "false"; }
	if (v.is_number()) { return numberString(v); }
	if (v.is_array())
	{
		std::string s;
		for (std::size_t i = 0; i < v.size(); ++i) { s += (i > 0 ? "," : "") + (v[i].is_null() ? std::string() : jsString(v[i])); }
		return s;
	}
	return "[object Object]";
}

std::string kvValue(const Snapshot& v)
{
	if (v.is_number()) { return isNumberIntegral(v) ? numberString(v) : toFixed(v.get<double>(), 2); }
	if (v.is_array()) { return summarizeArray(v); }
	if (v.is_object()) { return summarizeObject(v); }
	return jsString(v);
}

ReflectMeta parseReflectMeta(std::string_view elemType)
{
	ReflectMeta out;
	std::string rest(elemType);
	if (const auto gi = rest.find(";group:"); gi != std::string::npos)
	{
		out.group = rest.substr(gi + 7);
		rest.resize(gi);
	}
	else if (rest.rfind("group:", 0) == 0)
	{
		out.group = rest.substr(6);
		rest.clear();
	}
	if (rest.rfind("enum:", 0) == 0)
	{
		out.widget = ReflectMeta::Widget::Enum;
		out.names = splitOn(std::string_view(rest).substr(5), ',');
		return out;
	}
	std::vector<std::string> slider;
	if (rest.rfind("range:", 0) == 0)
	{
		const auto segs = splitOn(rest, ';');
		slider = splitOn(std::string_view(segs[0]).substr(6), ':');
		for (const auto& s : segs) { if (s.rfind("ui:", 0) == 0) { slider = splitOn(std::string_view(s).substr(3), ':'); } }
	}
	else if (rest.rfind("ui:", 0) == 0)
	{
		slider = splitOn(std::string_view(rest).substr(3), ':');
	}
	if (slider.size() >= 2)
	{
		out.widget = ReflectMeta::Widget::Range;
		out.min = toNumber(slider[0]);
		out.max = toNumber(slider[1]);
	}
	return out;
}

std::size_t kvSectionCount(const Snapshot& snap, const KvFilter& filter)
{
	return sectionKeys(snap, filter).size();
}

nlohmann::json kvSections(const Snapshot& snap, const KvFilter& filter,
                          const std::set<std::string, std::less<>>& openGroups)
{
	Json out = Json::array();
	for (const std::string& key : sectionKeys(snap, filter)) { out.push_back(sectionFor(key, snap[key], openGroups)); }
	return out;
}

nlohmann::json treeRows(const Snapshot& root, const std::string& rootLabel,
                        const std::set<std::string, std::less<>>& collapsed)
{
	Json out = Json::array();
	appendTreeRows(out, rootLabel, root, 0, rootLabel, collapsed);
	return out;
}

} // namespace mitiru::tool
