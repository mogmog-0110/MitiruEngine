#include "TuneSchema.hpp"

#include <nlohmann/json.hpp>

#include <set>

namespace mitiru::tool::motiontune
{

namespace
{

using Json = nlohmann::json;

bool readField(const Json& j, Field& out, std::string& error)
{
	if (!j.is_object() || !j.contains("key") || !j["key"].is_string()) { error = "A field has no key"; return false; }
	out.key = j["key"].get<std::string>();
	out.hint = j.value("hint", std::string());
	out.kind = j.value("kind", std::string("real"));
	out.min = j.value("min", 0.0);
	out.max = j.value("max", 1.0);
	if (out.kind != "real" && out.kind != "whole" && out.kind != "flag") { error = out.key + ": kind must be real, whole or flag"; return false; }
	if (!(out.max >= out.min)) { error = out.key + ": max is below min"; return false; }
	return true;
}

bool readGroup(const Json& j, Group& out, std::string& error)
{
	if (!j.is_object() || !j.contains("fields") || !j["fields"].is_array()) { error = "A group has no fields array"; return false; }
	out.key = j.value("key", std::string());
	out.label = j.value("label", out.key);
	for (const Json& f : j["fields"])
	{
		Field field;
		if (!readField(f, field, error)) { return false; }
		out.fields.push_back(std::move(field));
	}
	return true;
}

bool readFile(const Json& j, FileDecl& out, std::string& error)
{
	if (!j.is_object() || !j.contains("path") || !j.contains("prefix") || !j.contains("groups") || !j["groups"].is_array())
	{
		error = "files[] needs path, prefix and groups";
		return false;
	}
	out.path = j["path"].get<std::string>();
	out.prefix = j["prefix"].get<std::string>();
	out.id = j.value("id", out.prefix);
	out.label = j.value("label", out.id);
	for (const Json& g : j["groups"])
	{
		Group group;
		if (!readGroup(g, group, error)) { error = out.id + ": " + error; return false; }
		out.groups.push_back(std::move(group));
	}
	return true;
}

} // namespace

namespace
{

bool parseUnchecked(const std::string& text, TuneSchema& out, std::string& error)
{
	const Json root = Json::parse(text, nullptr, false);
	if (!root.is_object()) { error = "The root must be an object"; return false; }
	out = TuneSchema{};
	if (const auto src = root.find("source"); src != root.end() && src->is_object())
	{
		out.source.logicalPrefix = src->value("logical", std::string());
		out.source.diskPrefix = src->value("disk", std::string());
	}
	if (const auto tr = root.find("trace"); tr != root.end() && tr->is_object())
	{
		out.trace.x = tr->value("x", std::string());
		out.trace.y = tr->value("y", std::string());
		out.trace.z = tr->value("z", std::string());
		out.trace.speed = tr->value("speed", std::string());
		out.trace.seconds = tr->value("seconds", 3.0);
	}
	if (out.trace.x.empty() || out.trace.y.empty() || out.trace.z.empty())
	{
		error = "trace needs the names of the x, y and z fields";
		return false;
	}
	if (const auto fav = root.find("favorites"); fav != root.end() && fav->is_array())
	{
		for (const Json& n : *fav) { if (n.is_string()) { out.favorites.push_back(n.get<std::string>()); } }
	}
	if (!root.contains("files") || !root["files"].is_array()) { error = "No files array"; return false; }
	for (const Json& f : root["files"])
	{
		FileDecl file;
		if (!readFile(f, file, error)) { return false; }
		out.files.push_back(std::move(file));
	}
	if (out.files.empty()) { error = "files is empty"; return false; }
	std::set<std::string> names;
	for (const FileDecl& file : out.files)
	{
		for (const Group& group : file.groups)
		{
			for (const Field& field : group.fields)
			{
				if (!names.insert(memoryName(file, group, field)).second) { error = "Duplicate field name: " + memoryName(file, group, field); return false; }
			}
		}
	}
	return true;
}

} // namespace

bool parseTuneSchema(const std::string& text, TuneSchema& out, std::string& error)
{
	try { return parseUnchecked(text, out, error); }
	catch (const std::exception& e)
	{
		error = std::string("Wrong value type: ") + e.what();
		return false;
	}
}

std::string memoryName(const FileDecl& file, const Group& group, const Field& field)
{
	return file.prefix + "_" + (group.key.empty() ? std::string() : group.key + "_") + field.key;
}

std::string jsonPointer(const Group& group, const Field& field)
{
	return group.key.empty() ? field.key : group.key + "/" + field.key;
}

} // namespace mitiru::tool::motiontune
