#include "StageDoc.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <set>

namespace mitiru::tool::stageedit
{

namespace
{

using Json = nlohmann::ordered_json;
using mitiru::data::JsonText;

constexpr std::size_t kMaxUndo = 200;

bool parseNumber(const std::string& text, double& out)
{
	const char* begin = text.c_str();
	while (*begin == ' ') { ++begin; }
	char* end = nullptr;
	const double v = std::strtod(begin, &end);
	if (end == begin || !std::isfinite(v)) { return false; }
	while (*end == ' ') { ++end; }
	if (*end != '\0') { return false; }
	out = v;
	return true;
}

std::string shown(const Json& v)
{
	if (!v.is_number()) { return {}; }
	return JsonText::formatNumber(v.get<double>(), v.is_number_integer() ? "0" : "0.0");
}

Json defaultValue(const Field& f)
{
	if (!f.fallback.is_null()) { return f.fallback; }
	switch (f.type)
	{
	case FieldType::Vec3: return Json::array({ 0, 0, 0 });
	case FieldType::Vec2: return Json::array({ 0, 0 });
	case FieldType::Bool: return false;
	case FieldType::Text: return std::string();
	case FieldType::Color: return std::string("#FFFFFF");
	default: return 0;
	}
}

bool validColor(const std::string& s)
{
	if (s.size() != 7 || s[0] != '#') { return false; }
	return std::all_of(s.begin() + 1, s.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

} // namespace

bool StageDoc::load(const Schema* schema, const std::string& text, std::string& error)
{
	m_schema = schema;
	return reset(text, error);
}

bool StageDoc::reset(const std::string& text, std::string& error)
{
	auto doc = JsonText::parse(text, error);
	if (!doc) { return false; }
	m_doc = std::move(*doc);
	m_kept = text;
	m_undo.clear();
	m_redo.clear();
	m_base.reset();
	m_selected = -1;
	refresh();
	return true;
}

bool StageDoc::matches(const Kind& kind, const Json& v) const
{
	const Match& m = kind.match;
	if (m.any || (m.hasKey.empty() && m.lacksKey.empty() && !m.isArray)) { return true; }
	if (m.isArray) { return v.is_array(); }
	if (!v.is_object()) { return false; }
	if (!m.hasKey.empty() && !v.contains(m.hasKey)) { return false; }
	return m.lacksKey.empty() || !v.contains(m.lacksKey);
}

void StageDoc::refresh()
{
	const std::pair<int, int> keep = m_selected >= 0 && m_selected < static_cast<int>(m_items.size())
		? std::pair<int, int>{ m_items[static_cast<std::size_t>(m_selected)].kind, m_items[static_cast<std::size_t>(m_selected)].index }
		: std::pair<int, int>{ -1, -1 };
	m_items.clear();
	m_unmanaged = 0;
	std::set<std::string> claimed;
	std::set<std::string> arrays;
	for (std::size_t ki = 0; ki < m_schema->kinds.size(); ++ki)
	{
		const Kind& kind = m_schema->kinds[ki];
		if (kind.single)
		{
			const Json v = m_doc.value(kind.path);
			Item item{ static_cast<int>(ki), -1, kind.path, {} };
			if ((v.is_object() || v.is_array()) && readGeom(kind, v, item.geom)) { m_items.push_back(std::move(item)); }
			continue;
		}
		arrays.insert(kind.path);
		const std::size_t n = m_doc.size(kind.path);
		for (std::size_t i = 0; i < n; ++i)
		{
			const std::string ptr = kind.path + "/" + std::to_string(i);
			if (claimed.count(ptr) != 0) { continue; }
			const Json v = m_doc.value(ptr);
			if (!matches(kind, v)) { continue; }
			claimed.insert(ptr);
			Item item{ static_cast<int>(ki), static_cast<int>(i), ptr, {} };
			if (readGeom(kind, v, item.geom)) { m_items.push_back(std::move(item)); }
			else { ++m_unmanaged; }
		}
	}
	for (const std::string& path : arrays)
	{
		const std::size_t n = m_doc.size(path);
		for (std::size_t i = 0; i < n; ++i) { if (claimed.count(path + "/" + std::to_string(i)) == 0) { ++m_unmanaged; } }
	}
	m_selected = -1;
	for (std::size_t i = 0; i < m_items.size(); ++i)
	{
		if (m_items[i].kind == keep.first && m_items[i].index == keep.second) { m_selected = static_cast<int>(i); }
	}
}

void StageDoc::pushUndo(std::string before)
{
	m_undo.push_back(std::move(before));
	if (m_undo.size() > kMaxUndo) { m_undo.erase(m_undo.begin()); }
	m_redo.clear();
}

Item* StageDoc::selectedItem()
{
	return m_selected >= 0 && m_selected < static_cast<int>(m_items.size()) ? &m_items[static_cast<std::size_t>(m_selected)] : nullptr;
}

void StageDoc::beginGesture(int item)
{
	if (item < 0 || item >= static_cast<int>(m_items.size())) { return; }
	m_selected = item;
	m_base = m_doc;
	m_baseText = m_doc.render();
	m_baseItem = m_items[static_cast<std::size_t>(item)];
}

void StageDoc::gestureMove(double dx, double dy, double dz)
{
	if (!m_base) { return; }
	JsonText d = *m_base;
	writeGeom(d, m_baseItem.pointer, m_schema->kinds[static_cast<std::size_t>(m_baseItem.kind)], translated(m_baseItem.geom, dx, dy, dz));
	m_doc = std::move(d);
	refresh();
}

void StageDoc::gestureHandle(const Handle& handle, double wx, double wz)
{
	if (!m_base) { return; }
	JsonText d = *m_base;
	writeGeom(d, m_baseItem.pointer, m_schema->kinds[static_cast<std::size_t>(m_baseItem.kind)], dragged(m_baseItem.geom, handle, wx, wz, m_schema->snap));
	m_doc = std::move(d);
	refresh();
}

bool StageDoc::endGesture()
{
	if (!m_base) { return false; }
	const bool changed = m_doc.render() != m_baseText;
	if (changed) { pushUndo(m_baseText); }
	m_base.reset();
	return changed;
}

bool StageDoc::nudge(double dx, double dy, double dz)
{
	if (selectedItem() == nullptr) { return false; }
	beginGesture(m_selected);
	gestureMove(dx, dy, dz);
	return endGesture();
}

bool StageDoc::rotate(double deg)
{
	Item* item = selectedItem();
	if (item == nullptr || !item->geom.hasYaw) { return false; }
	const Kind& kind = m_schema->kinds[static_cast<std::size_t>(item->kind)];
	Geom g = item->geom;
	g.yaw = normalizedYaw(g.yaw + deg);
	const std::string before = m_doc.render();
	writeGeom(m_doc, item->pointer, kind, g);
	if (m_doc.render() == before) { return false; }
	pushUndo(before);
	refresh();
	return true;
}

bool StageDoc::duplicate()
{
	Item* item = selectedItem();
	if (item == nullptr || item->index < 0) { return false; }
	const Kind& kind = m_schema->kinds[static_cast<std::size_t>(item->kind)];
	const std::string before = m_doc.render();
	auto node = m_doc.clone(item->pointer);
	const int index = item->index + 1;
	if (!node || !m_doc.insertAt(kind.path, static_cast<std::size_t>(index), node)) { return false; }
	pushUndo(before);
	const int kindIndex = item->kind;
	refresh();
	for (std::size_t i = 0; i < m_items.size(); ++i)
	{
		if (m_items[i].kind == kindIndex && m_items[i].index == index) { m_selected = static_cast<int>(i); }
	}
	if (Item* copy = selectedItem())
	{
		const double off = m_schema->snap.move * 2.0;
		writeGeom(m_doc, copy->pointer, kind, translated(copy->geom, off, 0.0, off));
		refresh();
	}
	return true;
}

bool StageDoc::remove()
{
	Item* item = selectedItem();
	if (item == nullptr || item->index < 0) { return false; }
	const Kind& kind = m_schema->kinds[static_cast<std::size_t>(item->kind)];
	const std::string before = m_doc.render();
	if (!m_doc.erase(kind.path, static_cast<std::size_t>(item->index))) { return false; }
	pushUndo(before);
	m_selected = -1;
	refresh();
	return true;
}

bool StageDoc::add(int kindIndex, double x, double z)
{
	if (kindIndex < 0 || kindIndex >= static_cast<int>(m_schema->kinds.size())) { return false; }
	const Kind& kind = m_schema->kinds[static_cast<std::size_t>(kindIndex)];
	Geom probe;
	if (kind.fresh.is_null() || !readGeom(kind, kind.fresh, probe)) { return false; }
	const std::string before = m_doc.render();
	const JsonText::Style style = kind.inlineStyle ? JsonText::Style::Inline : JsonText::Style::Block;
	if (kind.single)
	{
		const Json existing = m_doc.value(kind.path);
		if (existing.is_object() || existing.is_array()) { return false; }
		if (!m_doc.setMember("", kind.path, m_doc.make(kind.fresh, style, ""))) { return false; }
	}
	else
	{
		if (!m_doc.has(kind.path) && !m_doc.setMember("", kind.path, m_doc.make(Json::array(), JsonText::Style::Block, ""))) { return false; }
		const auto elementLayout = m_doc.multiline(kind.path + "/0");
		const JsonText::Style fit = elementLayout ? (*elementLayout ? JsonText::Style::Block : JsonText::Style::Inline) : style;
		if (!m_doc.insertAt(kind.path, m_doc.size(kind.path), m_doc.make(kind.fresh, fit, kind.path))) { return false; }
	}
	pushUndo(before);
	refresh();
	const int want = kind.single ? -1 : static_cast<int>(m_doc.size(kind.path)) - 1;
	for (std::size_t i = 0; i < m_items.size(); ++i)
	{
		if (m_items[i].kind == kindIndex && m_items[i].index == want) { m_selected = static_cast<int>(i); }
	}
	Item* item = selectedItem();
	if (item == nullptr)
	{
		// スキーマの new が、その種類の when や gizmo に合っていない。足した分を取り消して断る
		replaceWith(before);
		m_undo.pop_back();
		return false;
	}
	const Aabb b = boundsOf(item->geom);
	const double snap = m_schema->snap.move;
	writeGeom(m_doc, item->pointer, kind, translated(item->geom, snapTo(x - (b.lo.x + b.hi.x) * 0.5, snap), 0.0, snapTo(z - (b.lo.z + b.hi.z) * 0.5, snap)));
	if (!kind.single)
	{
		for (const Item& other : m_items)
		{
			if (other.kind == kindIndex && other.index >= 0 && other.index != want) { m_doc.copyNumberStyle(item->pointer, other.pointer); break; }
		}
	}
	refresh();
	return true;
}

std::vector<FieldRow> StageDoc::rows(int itemIndex) const
{
	std::vector<FieldRow> out;
	if (itemIndex < 0 || itemIndex >= static_cast<int>(m_items.size())) { return out; }
	const Item& item = m_items[static_cast<std::size_t>(itemIndex)];
	for (const Field& f : m_schema->kinds[static_cast<std::size_t>(item.kind)].fields)
	{
		FieldRow row;
		row.key = f.key;
		row.label = f.label;
		row.type = f.type;
		row.optional = f.optional;
		const std::string ptr = pointerFor(item.pointer, f.key);
		row.present = m_doc.has(ptr);
		const Json v = row.present ? m_doc.value(ptr) : Json();
		if (f.type == FieldType::Vec3 || f.type == FieldType::Vec2)
		{
			row.compNames = f.type == FieldType::Vec3 ? std::vector<std::string>{ "x", "y", "z" } : std::vector<std::string>{ "x", "z" };
			for (std::size_t i = 0; i < row.compNames.size(); ++i) { row.comps.push_back(v.is_array() && i < v.size() ? shown(v[i]) : std::string()); }
		}
		else if (f.type == FieldType::Number || f.type == FieldType::Angle) { row.comps.push_back(shown(v)); }
		else if (f.type == FieldType::Bool) { row.flag = v.is_boolean() && v.get<bool>(); }
		else if (v.is_string()) { row.text = v.get<std::string>(); }
		out.push_back(std::move(row));
	}
	return out;
}

bool StageDoc::setField(int itemIndex, const std::string& key, int comp, const std::string& value)
{
	if (itemIndex < 0 || itemIndex >= static_cast<int>(m_items.size())) { return false; }
	const Item item = m_items[static_cast<std::size_t>(itemIndex)];
	const Kind& kind = m_schema->kinds[static_cast<std::size_t>(item.kind)];
	const auto field = std::find_if(kind.fields.begin(), kind.fields.end(), [&](const Field& f) { return f.key == key; });
	if (field == kind.fields.end()) { return false; }
	const std::string before = m_doc.render();
	const std::string ptr = pointerFor(item.pointer, key);
	bool ok = false;
	if (field->type == FieldType::Text) { ok = m_doc.setString(ptr, value); }
	else if (field->type == FieldType::Color) { ok = validColor(value) && m_doc.setString(ptr, value); }
	else
	{
		double v = 0.0;
		if (!parseNumber(value, v)) { return false; }
		v = std::clamp(v, field->minValue, field->maxValue);
		const bool vec = field->type == FieldType::Vec3 || field->type == FieldType::Vec2;
		ok = vec ? (comp >= 0 && m_doc.setNumber(ptr + "/" + std::to_string(comp), v)) : m_doc.setNumber(ptr, v);
	}
	if (!ok) { return false; }
	if (kind.gizmo.shape == Shape::Box)
	{
		Geom g;
		if (readGeom(kind, m_doc.value(item.pointer), g))
		{
			Geom fixed = g;
			fixed.a = Vec3{ std::min(g.a.x, g.b.x), std::min(g.a.y, g.b.y), std::min(g.a.z, g.b.z) };
			fixed.b = Vec3{ std::max(g.a.x, g.b.x), std::max(g.a.y, g.b.y), std::max(g.a.z, g.b.z) };
			writeGeom(m_doc, item.pointer, kind, fixed);
		}
	}
	if (m_doc.render() == before) { return false; }
	pushUndo(before);
	refresh();
	return true;
}

bool StageDoc::toggleField(int itemIndex, const std::string& key)
{
	if (itemIndex < 0 || itemIndex >= static_cast<int>(m_items.size())) { return false; }
	const std::string ptr = pointerFor(m_items[static_cast<std::size_t>(itemIndex)].pointer, key);
	const Json v = m_doc.value(ptr);
	if (!v.is_boolean()) { return false; }
	const std::string before = m_doc.render();
	if (!m_doc.setBool(ptr, !v.get<bool>())) { return false; }
	pushUndo(before);
	refresh();
	return true;
}

bool StageDoc::addField(int itemIndex, const std::string& key)
{
	if (itemIndex < 0 || itemIndex >= static_cast<int>(m_items.size())) { return false; }
	const Item& item = m_items[static_cast<std::size_t>(itemIndex)];
	const Kind& kind = m_schema->kinds[static_cast<std::size_t>(item.kind)];
	const auto field = std::find_if(kind.fields.begin(), kind.fields.end(), [&](const Field& f) { return f.key == key; });
	if (field == kind.fields.end() || m_doc.has(pointerFor(item.pointer, key))) { return false; }
	const std::string before = m_doc.render();
	if (!m_doc.setMember(item.pointer, key, m_doc.make(defaultValue(*field), JsonText::Style::Inline, item.pointer))) { return false; }
	pushUndo(before);
	refresh();
	return true;
}

bool StageDoc::removeField(int itemIndex, const std::string& key)
{
	if (itemIndex < 0 || itemIndex >= static_cast<int>(m_items.size())) { return false; }
	const Item& item = m_items[static_cast<std::size_t>(itemIndex)];
	const Kind& kind = m_schema->kinds[static_cast<std::size_t>(item.kind)];
	const auto field = std::find_if(kind.fields.begin(), kind.fields.end(), [&](const Field& f) { return f.key == key; });
	if (field == kind.fields.end() || !field->optional) { return false; }
	const std::string before = m_doc.render();
	if (!m_doc.eraseMember(item.pointer, key)) { return false; }
	pushUndo(before);
	refresh();
	return true;
}

bool StageDoc::replaceWith(const std::string& text)
{
	std::string error;
	auto doc = JsonText::parse(text, error);
	if (!doc) { return false; }
	m_doc = std::move(*doc);
	refresh();
	return true;
}

bool StageDoc::undo()
{
	if (m_undo.empty()) { return false; }
	std::string now = m_doc.render();
	const std::string target = m_undo.back();
	if (!replaceWith(target)) { return false; }
	m_undo.pop_back();
	m_redo.push_back(std::move(now));
	return true;
}

bool StageDoc::redo()
{
	if (m_redo.empty()) { return false; }
	std::string now = m_doc.render();
	const std::string target = m_redo.back();
	if (!replaceWith(target)) { return false; }
	m_redo.pop_back();
	m_undo.push_back(std::move(now));
	return true;
}

} // namespace mitiru::tool::stageedit
