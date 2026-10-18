#include "EditSchema.hpp"

#include <cstdlib>

namespace mitiru::tool::stageedit
{

namespace
{

using Json = nlohmann::ordered_json;

struct ShapeSpec
{
	const char* name;
	Shape shape;
	std::vector<const char*> required;
};

const std::vector<ShapeSpec>& shapeSpecs()
{
	static const std::vector<ShapeSpec> specs = {
		{ "box", Shape::Box, { "min", "max" } },
		{ "orientedBox", Shape::OrientedBox, { "center", "size", "yaw" } },
		{ "point", Shape::Point, { "at" } },
		{ "segment", Shape::Segment, { "from", "to" } },
		{ "polyline", Shape::Polyline, { "points" } },
		{ "wall", Shape::Wall, { "from", "to", "bottom", "top" } },
	};
	return specs;
}

bool fail(std::string& error, const std::string& where, const std::string& what)
{
	error = where + ": " + what;
	return false;
}

std::string stringOf(const Json& j, const char* key, const std::string& fallback = {})
{
	const auto it = j.find(key);
	return it != j.end() && it->is_string() ? it->get<std::string>() : fallback;
}

double numberOf(const Json& j, const char* key, double fallback)
{
	const auto it = j.find(key);
	return it != j.end() && it->is_number() ? it->get<double>() : fallback;
}

bool parseColor(const std::string& hex, Rgb& out)
{
	if (hex.size() != 7 || hex[0] != '#') { return false; }
	char* end = nullptr;
	const unsigned long v = std::strtoul(hex.c_str() + 1, &end, 16);
	if (end != hex.c_str() + 7) { return false; }
	out = Rgb{ static_cast<unsigned char>((v >> 16) & 0xFF), static_cast<unsigned char>((v >> 8) & 0xFF), static_cast<unsigned char>(v & 0xFF) };
	return true;
}

bool parseGizmo(const Json& j, const std::string& where, Gizmo& out, std::string& error)
{
	if (!j.is_object()) { return fail(error, where, "gizmo must be an object"); }
	const std::string name = stringOf(j, "shape");
	const ShapeSpec* spec = nullptr;
	for (const ShapeSpec& s : shapeSpecs()) { if (name == s.name) { spec = &s; } }
	if (spec == nullptr) { return fail(error, where, "Unknown shape \"" + name + "\" (box orientedBox point segment polyline wall)"); }
	out.shape = spec->shape;
	for (auto it = j.begin(); it != j.end(); ++it)
	{
		if (it.key() != "shape" && it.value().is_string()) { out.keys[it.key()] = it.value().get<std::string>(); }
	}
	for (const char* role : spec->required)
	{
		if (out.key(role) == nullptr) { return fail(error, where, std::string("shape ") + name + " needs \"" + role + "\""); }
	}
	return true;
}

bool parseField(const Json& j, const std::string& where, Field& out, std::string& error)
{
	if (!j.contains("key") || !j["key"].is_string()) { return fail(error, where, "Missing key (use an empty string for the element itself)"); }
	out.key = j["key"].get<std::string>();
	out.label = stringOf(j, "label", out.key);
	const std::string type = stringOf(j, "type", "number");
	static const std::vector<std::pair<const char*, FieldType>> types = {
		{ "vec3", FieldType::Vec3 }, { "vec2", FieldType::Vec2 }, { "number", FieldType::Number },
		{ "angle", FieldType::Angle }, { "bool", FieldType::Bool }, { "text", FieldType::Text }, { "color", FieldType::Color } };
	bool known = false;
	for (const auto& [n, t] : types) { if (type == n) { out.type = t; known = true; } }
	if (!known) { return fail(error, where, "Unknown type \"" + type + "\" (vec3 vec2 number angle bool text color)"); }
	out.optional = j.value("optional", false);
	out.step = numberOf(j, "step", 0.0);
	out.minValue = numberOf(j, "min", out.minValue);
	out.maxValue = numberOf(j, "max", out.maxValue);
	if (const auto it = j.find("default"); it != j.end()) { out.fallback = *it; }
	return true;
}

bool parseMatch(const Json& j, Match& out)
{
	if (!j.is_object()) { return false; }
	out.any = false;
	out.hasKey = stringOf(j, "has");
	out.lacksKey = stringOf(j, "lacks");
	out.isArray = j.value("array", false);
	return true;
}

bool parseKind(const Json& j, std::size_t index, Kind& out, std::string& error)
{
	const std::string where = "kinds[" + std::to_string(index) + "]";
	if (!j.is_object()) { return fail(error, where, "Must be an object"); }
	out.id = stringOf(j, "id");
	out.path = stringOf(j, "path");
	if (out.id.empty() || out.path.empty()) { return fail(error, where, "id and path are required"); }
	out.label = stringOf(j, "label", out.id);
	out.single = j.value("single", false);
	if (const auto it = j.find("when"); it != j.end() && !parseMatch(*it, out.match)) { return fail(error, where, "when must be an object"); }
	if (!j.contains("gizmo") || !parseGizmo(j["gizmo"], where + ".gizmo", out.gizmo, error)) { return error.empty() ? fail(error, where, "Missing gizmo") : false; }
	if (!parseColor(stringOf(j, "color", "#808080"), out.color)) { return fail(error, where, "color must be #RRGGBB"); }
	if (const auto it = j.find("fields"); it != j.end() && it->is_array())
	{
		for (std::size_t i = 0; i < it->size(); ++i)
		{
			Field f;
			if (!parseField((*it)[i], where + ".fields[" + std::to_string(i) + "]", f, error)) { return false; }
			out.fields.push_back(std::move(f));
		}
	}
	if (const auto it = j.find("new"); it != j.end()) { out.fresh = *it; }
	out.inlineStyle = stringOf(j, "style", "inline") != "block";
	return true;
}

bool parseSnap(const Json& j, Snap& out)
{
	if (!j.is_object()) { return true; }
	out.move = numberOf(j, "move", out.move);
	out.size = numberOf(j, "size", out.size);
	out.height = numberOf(j, "height", out.height);
	out.angle = numberOf(j, "angle", out.angle);
	return out.move > 0.0 && out.size > 0.0 && out.height > 0.0 && out.angle > 0.0;
}

} // namespace

bool parseSchema(const std::string& text, Schema& out, std::string& error)
{
	const Json root = Json::parse(text, nullptr, false);
	if (root.is_discarded() || !root.is_object()) { return fail(error, "Schema", "Not valid JSON"); }
	out = Schema{};
	out.title = stringOf(root, "title", "Stage");
	out.dir = stringOf(root, "dir");
	if (out.dir.empty()) { return fail(error, "Schema", "Missing dir (logical path of the stage files)"); }
	while (!out.dir.empty() && out.dir.back() == '/') { out.dir.pop_back(); }
	if (const auto it = root.find("source"); it != root.end() && it->is_object())
	{
		out.source = SourceMap{ stringOf(*it, "logical"), stringOf(*it, "disk") };
	}
	if (const auto it = root.find("snap"); it != root.end() && !parseSnap(*it, out.snap)) { return fail(error, "snap", "Steps must be greater than 0"); }
	if (const auto it = root.find("topView"); it != root.end() && it->is_object())
	{
		out.top.rightIsNegX = stringOf(*it, "right", "+x") == "-x";
		out.top.upIsNegZ = stringOf(*it, "up", "+z") == "-z";
	}
	if (const auto it = root.find("generated"); it != root.end() && it->is_object())
	{
		for (auto g = it->begin(); g != it->end(); ++g)
		{
			out.generated[g.key()] = g.value().is_string() ? g.value().get<std::string>() : std::string("a generator script");
		}
	}
	if (const auto it = root.find("exclude"); it != root.end() && it->is_array())
	{
		for (const auto& e : *it) { if (e.is_string()) { out.exclude.push_back(e.get<std::string>()); } }
	}
	const auto kinds = root.find("kinds");
	if (kinds == root.end() || !kinds->is_array() || kinds->empty()) { return fail(error, "Schema", "Missing kinds"); }
	for (std::size_t i = 0; i < kinds->size(); ++i)
	{
		Kind k;
		if (!parseKind((*kinds)[i], i, k, error)) { return false; }
		out.kinds.push_back(std::move(k));
	}
	return true;
}

} // namespace mitiru::tool::stageedit
