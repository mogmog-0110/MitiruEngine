#include "AnimGraphNames.hpp"

#include "../PageUtil.hpp"

#include <mitiru/animation/AnimNameKey.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace mitiru::tool
{

namespace
{

std::string nameOf(const Snapshot& v)
{
	if (v.is_string()) { return v.get<std::string>(); }
	return stringOr(findAt(v, { "name" }), "");
}

AnimGraphNames::Layer readLayer(const Snapshot& l, std::size_t index)
{
	AnimGraphNames::Layer layer;
	layer.name = stringOr(findAt(l, { "name" }), "layer" + std::to_string(index));
	if (const Snapshot* states = findAt(l, { "states" }); states != nullptr && states->is_array())
	{
		for (const Snapshot& s : *states) { layer.states.push_back(nameOf(s)); }
	}
	struct Ordered
	{
		AnimGraphNames::Transition t;
		double priority = 0.0;
	};
	std::vector<Ordered> list;
	if (const Snapshot* ts = findAt(l, { "transitions" }); ts != nullptr && ts->is_array())
	{
		for (const Snapshot& t : *ts)
		{
			list.push_back({ { nameOf(t.value("from", Snapshot())), nameOf(t.value("to", Snapshot())) },
			                 numberOr(findAt(t, { "priority" }), 0.0) });
		}
	}
	std::stable_sort(list.begin(), list.end(), [](const Ordered& a, const Ordered& b) { return a.priority > b.priority; });
	for (const Ordered& o : list) { layer.transitions.push_back(o.t); }
	return layer;
}

/// 相対パスは今の作業フォルダ (host が開いた窓は host と同じ) と MITIRU_ASSET_ROOT から探す。
std::filesystem::path resolvePath(const std::string& query)
{
	std::filesystem::path p(query);
	std::error_code ec;
	if (p.is_absolute() || std::filesystem::exists(p, ec)) { return p; }
	if (const char* root = std::getenv("MITIRU_ASSET_ROOT"); root != nullptr && *root != '\0')
	{
		if (auto q = std::filesystem::path(root) / p; std::filesystem::exists(q, ec)) { return q; }
	}
	return p;
}

std::string stemOf(const std::string& path)
{
	std::string name = std::filesystem::path(path).filename().string();
	constexpr std::string_view kExt = ".animgraph.json";
	if (name.size() > kExt.size() && name.compare(name.size() - kExt.size(), kExt.size(), kExt) == 0)
	{
		return name.substr(0, name.size() - kExt.size());
	}
	return std::filesystem::path(name).stem().string();
}

} // namespace

bool readAnimGraphNames(const Snapshot& json, const std::string& fallbackName, AnimGraphNames& out, std::string& error)
{
	const Snapshot* layers = findAt(json, { "layers" });
	if (!json.is_object() || layers == nullptr || !layers->is_array())
	{
		error = "グラフの JSON に layers が無い";
		return false;
	}
	out = AnimGraphNames{};
	out.name = stringOr(findAt(json, { "name" }), fallbackName);
	out.key = animation::animNameKey(out.name);
	if (const Snapshot* params = findAt(json, { "params" }); params != nullptr && params->is_array())
	{
		for (const Snapshot& p : *params) { out.params.push_back({ nameOf(p), stringOr(findAt(p, { "type" }), "float") }); }
	}
	for (std::size_t i = 0; i < layers->size(); ++i) { out.layers.push_back(readLayer((*layers)[i], i)); }
	if (const Snapshot* events = findAt(json, { "events" }); events != nullptr && events->is_array())
	{
		for (const Snapshot& e : *events) { out.events.push_back(nameOf(e)); }
	}
	return true;
}

std::filesystem::file_time_type animGraphNamesStamp(const std::string& path)
{
	std::error_code ec;
	const auto t = std::filesystem::last_write_time(resolvePath(path), ec);
	return ec ? std::filesystem::file_time_type{} : t;
}

std::string loadAnimGraphNames(const std::string& path, const std::string& fallbackName, AnimGraphNames& out)
{
	std::ifstream f(resolvePath(path), std::ios::binary);
	if (!f) { return "グラフの JSON を開けない: " + path; }
	const Snapshot j = Snapshot::parse(f, nullptr, false);
	if (j.is_discarded()) { return "グラフの JSON を読めない: " + path; }
	std::string error;
	return readAnimGraphNames(j, fallbackName.empty() ? stemOf(path) : fallbackName, out, error) ? std::string() : error + ": " + path;
}

} // namespace mitiru::tool
