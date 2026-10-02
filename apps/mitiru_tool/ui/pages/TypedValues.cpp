#include "TypedValues.hpp"

#include "../PageUtil.hpp"

#include <algorithm>

namespace mitiru::tool
{

namespace
{

std::string ownerOf(const std::string& path)
{
	const auto dot = path.rfind('.');
	return dot == std::string::npos ? std::string() : path.substr(0, dot);
}

void walk(const Snapshot& node, const std::string& path, std::string_view type, std::vector<TypedValue>& out)
{
	if (node.is_object())
	{
		if (const auto it = node.find("$type"); it != node.end())
		{
			if (it->is_string() && it->get_ref<const std::string&>() == type) { out.push_back({ path, ownerOf(path), &node }); }
			return;
		}
		for (const auto& [key, child] : node.items())
		{
			walk(child, path.empty() ? key : path + "." + key, type, out);
		}
		return;
	}
	if (node.is_array())
	{
		for (std::size_t i = 0; i < node.size(); ++i)
		{
			walk(node[i], path + "[" + std::to_string(i) + "]", type, out);
		}
	}
}

} // namespace

const Snapshot* gameMemoryState(const Snapshot& snap)
{
	return findAt(snap, { "gameMemory", "state" });
}

std::vector<SnapshotAsset> snapshotAssets(const Snapshot& snap, std::string_view kind)
{
	std::vector<SnapshotAsset> out;
	const Snapshot* list = findAt(snap, { "assets" });
	if (list == nullptr || !list->is_array()) { return out; }
	for (const Snapshot& a : *list)
	{
		if (!a.is_object() || stringOr(findAt(a, { "kind" }), "") != kind) { continue; }
		out.push_back({ stringOr(findAt(a, { "name" }), ""), stringOr(findAt(a, { "file" }), "") });
	}
	return out;
}

std::vector<TypedValue> findTyped(const Snapshot& root, std::string_view type)
{
	std::vector<TypedValue> out;
	walk(root, std::string(), type, out);
	return out;
}

const TypedValue* findOwned(const std::vector<TypedValue>& values, std::string_view owner)
{
	for (const TypedValue& v : values) { if (v.owner == owner) { return &v; } }
	return nullptr;
}

bool ownerIsUnique(const std::vector<TypedValue>& list, std::string_view owner)
{
	return std::count_if(list.begin(), list.end(), [owner](const TypedValue& v) { return v.owner == owner; }) == 1;
}

std::string ownerLabel(const TypedValue& v)
{
	return v.owner.empty() ? v.path : v.owner;
}

std::string queryValue(std::string_view query, std::string_view key)
{
	std::size_t at = 0;
	while (at <= query.size())
	{
		const auto end = std::min(query.find('&', at), query.size());
		const std::string_view part = query.substr(at, end - at);
		const auto eq = part.find('=');
		if (eq != std::string_view::npos && part.substr(0, eq) == key) { return std::string(part.substr(eq + 1)); }
		at = end + 1;
	}
	return {};
}

std::string vec3Text(const Snapshot* v)
{
	if (v == nullptr || !v->is_array() || v->size() != 3) { return "\xE2\x80\x94"; }
	std::string out = "(";
	for (std::size_t i = 0; i < 3; ++i)
	{
		out += (*v)[i].is_number() ? toFixed((*v)[i].get<double>(), 2) : jsString((*v)[i]);
		out += i < 2 ? ", " : ")";
	}
	return out;
}

} // namespace mitiru::tool
