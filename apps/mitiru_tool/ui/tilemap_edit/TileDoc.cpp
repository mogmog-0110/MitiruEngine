#include "TileDoc.hpp"

#include <mitiru/data/JsonFields.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace mitiru::tool::tilemapedit
{

namespace
{

using Json = nlohmann::ordered_json;
using mitiru::data::JsonText;

constexpr std::size_t kMaxUndo = 200;
constexpr std::size_t kNever = std::numeric_limits<std::size_t>::max();
constexpr double kMaxNumber = 1.0e9;
constexpr long long kMaxCells = 4000000;   // 巨大なレイヤーで index と cells の確保が失敗しないように

bool integral(const Json& v)
{
	return v.is_number() && std::floor(v.get<double>()) == v.get<double>() && std::fabs(v.get<double>()) <= kMaxNumber;
}

std::string layerPointer(std::size_t i) { return "layers/" + std::to_string(i); }

std::size_t cellOf(int width, int x, int y)
{
	return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
}

Json tileJson(const Change& c)
{
	const Cell& cell = *c.after;
	Json v = Json::object();
	v["tileId"] = cell.id;
	v["x"] = c.x;
	v["y"] = c.y;
	v["rotation"] = mitiru::data::jsonFloat(cell.rotation);
	v["flipX"] = cell.flipX;
	v["flipY"] = cell.flipY;
	return v;
}

} // namespace

Rect Rect::of(int ax, int ay, int bx, int by)
{
	return Rect{ std::min(ax, bx), std::min(ay, by), std::max(ax, bx), std::max(ay, by) };
}

Cell mirrorCell(Cell cell, bool horizontal)
{
	if (horizontal) { cell.flipX = !cell.flipX; } else { cell.flipY = !cell.flipY; }
	float r = std::fmod(-cell.rotation, 360.0f);
	if (r < 0.0f) { r += 360.0f; }
	cell.rotation = r == 0.0f ? 0.0f : r;
	return cell;
}

Clip mirrorClip(const Clip& clip, bool horizontal)
{
	Clip out = clip;
	for (int y = 0; y < clip.height; ++y)
	{
		for (int x = 0; x < clip.width; ++x)
		{
			const MaybeCell& src = horizontal ? clip.at(clip.width - 1 - x, y) : clip.at(x, clip.height - 1 - y);
			out.cells[cellOf(clip.width, x, y)] = src ? MaybeCell(mirrorCell(*src, horizontal)) : std::nullopt;
		}
	}
	return out;
}

bool TileDoc::readLayer(const JsonText& doc, std::size_t index, Layer& layer, std::string& error)
{
	layer.pointer = layerPointer(index);
	layer.tilesPointer = layer.pointer + "/tiles";
	const std::string where = "Layer " + std::to_string(index + 1);
	const JsonText::Node* node = doc.find(layer.pointer);
	if (node == nullptr || node->kind != JsonText::Kind::Object) { error = where + " is not an object"; return false; }
	const Json name = doc.value(layer.pointer + "/name");
	const Json w = doc.value(layer.pointer + "/width");
	const Json h = doc.value(layer.pointer + "/height");
	layer.name = name.is_string() ? name.get<std::string>() : std::string();
	if (!integral(w) || !integral(h) || w.get<int>() < 1 || h.get<int>() < 1)
	{
		error = where + " needs width and height (whole numbers, 1 or more)";
		return false;
	}
	layer.width = w.get<int>();
	layer.height = h.get<int>();
	if (static_cast<long long>(layer.width) * layer.height > kMaxCells) { error = where + " is too large (at most 4000000 cells)"; return false; }
	layer.cells.assign(cellOf(layer.width, 0, layer.height), std::nullopt);
	layer.index.assign(layer.cells.size(), -1);
	const JsonText::Node* tiles = doc.find(layer.tilesPointer);
	layer.hasTilesArray = tiles != nullptr && tiles->kind == JsonText::Kind::Array;
	for (std::size_t j = 0; layer.hasTilesArray && j < tiles->members.size(); ++j)
	{
		const Json t = doc.value(layer.tilesPointer + "/" + std::to_string(j));
		const auto tx = t.is_object() ? t.find("x") : t.end();
		const auto ty = t.is_object() ? t.find("y") : t.end();
		if (!t.is_object() || tx == t.end() || ty == t.end() || !integral(*tx) || !integral(*ty)) { ++layer.unmanaged; continue; }
		const int x = tx->get<int>();
		const int y = ty->get<int>();
		if (x < 0 || y < 0 || x >= layer.width || y >= layer.height) { ++layer.unmanaged; continue; }
		const std::size_t cell = cellOf(layer.width, x, y);
		if (layer.cells[cell])
		{
			error = where + " has two tiles at (" + std::to_string(x) + ", " + std::to_string(y) + "). Remove one in the file first";
			return false;
		}
		Cell c;
		c.id = t.contains("tileId") && integral(t["tileId"]) ? t["tileId"].get<int>() : 0;
		c.rotation = t.contains("rotation") && t["rotation"].is_number() ? t["rotation"].get<float>() : 0.0f;
		c.flipX = t.contains("flipX") && t["flipX"].is_boolean() && t["flipX"].get<bool>();
		c.flipY = t.contains("flipY") && t["flipY"].is_boolean() && t["flipY"].get<bool>();
		layer.cells[cell] = c;
		layer.index[cell] = static_cast<int>(j);
		++layer.count;
	}
	return true;
}

bool TileDoc::reset(const std::string& text, std::string& error)
{
	try { return resetChecked(text, error); }
	catch (const std::exception& e) { error = std::string("Can't read the map: ") + e.what(); return false; }
}

bool TileDoc::resetChecked(const std::string& text, std::string& error)
{
	auto doc = JsonText::parse(text, error);
	if (!doc) { return false; }
	const JsonText::Node* layers = doc->find("layers");
	if (layers == nullptr || layers->kind != JsonText::Kind::Array) { error = "Missing layers (an array of layers)"; return false; }
	if (layers->members.empty()) { error = "The map has no layers"; return false; }
	std::vector<Layer> parsed(layers->members.size());
	for (std::size_t i = 0; i < parsed.size(); ++i)
	{
		if (!readLayer(*doc, i, parsed[i], error)) { return false; }
	}
	const Json name = doc->value("name");
	const Json tw = doc->value("tileWidth");
	const Json th = doc->value("tileHeight");
	m_name = name.is_string() ? name.get<std::string>() : std::string();
	m_tileWidth = integral(tw) ? tw.get<int>() : 32;
	m_tileHeight = integral(th) ? th.get<int>() : 32;
	m_doc = std::move(*doc);
	m_layers = std::move(parsed);
	m_undo.clear();
	m_redo.clear();
	m_group.clear();
	m_grouping = false;
	m_keptDepth = 0;
	++m_revision;
	return true;
}

TileDoc::Layer* TileDoc::layerAt(int layer)
{
	return layer >= 0 && layer < static_cast<int>(m_layers.size()) ? &m_layers[static_cast<std::size_t>(layer)] : nullptr;
}

const TileDoc::Layer* TileDoc::layerAt(int layer) const
{
	return layer >= 0 && layer < static_cast<int>(m_layers.size()) ? &m_layers[static_cast<std::size_t>(layer)] : nullptr;
}

bool TileDoc::inside(int layer, int x, int y) const
{
	const Layer* l = layerAt(layer);
	return l != nullptr && x >= 0 && y >= 0 && x < l->width && y < l->height;
}

MaybeCell TileDoc::at(int layer, int x, int y) const
{
	if (!inside(layer, x, y)) { return std::nullopt; }
	const Layer& l = m_layers[static_cast<std::size_t>(layer)];
	return l.cells[cellOf(l.width, x, y)];
}

void TileDoc::insertTile(Layer& layer, Change& change)
{
	if (!layer.hasTilesArray)
	{
		m_doc.setMember(layer.pointer, "tiles", m_doc.make(Json::array(), JsonText::Style::Inline, layer.pointer));
		layer.hasTilesArray = true;
	}
	const std::size_t size = m_doc.size(layer.tilesPointer);
	const std::size_t pos = change.index >= 0 ? std::min(static_cast<std::size_t>(change.index), size) : size;
	m_doc.insertAt(layer.tilesPointer, pos, m_doc.make(tileJson(change), JsonText::Style::Inline, layer.tilesPointer));
	for (int& i : layer.index) { if (i >= static_cast<int>(pos)) { ++i; } }
	layer.index[cellOf(layer.width, change.x, change.y)] = static_cast<int>(pos);
	change.index = static_cast<int>(pos);
}

void TileDoc::eraseTile(Layer& layer, Change& change)
{
	const std::size_t cell = cellOf(layer.width, change.x, change.y);
	const int removed = layer.index[cell];
	change.index = removed;
	m_doc.erase(layer.tilesPointer, static_cast<std::size_t>(removed));
	layer.index[cell] = -1;
	for (int& i : layer.index) { if (i > removed) { --i; } }
}

void TileDoc::updateTile(Layer& layer, const Change& change)
{
	const std::string ptr = layer.tilesPointer + "/" + std::to_string(layer.index[cellOf(layer.width, change.x, change.y)]);
	// 鍵がなく既定値のままなら追加しない。鍵があっても型が違う場合は削除して書き直す
	const auto put = [&](const char* key, const Json& value, bool isDefault) {
		const std::string p = ptr + "/" + key;
		const bool present = m_doc.has(p);
		const bool done = present && (value.is_boolean() ? m_doc.setBool(p, value.get<bool>()) : m_doc.setNumber(p, value.get<double>()));
		if (done) { return; }
		if (present) { m_doc.eraseMember(ptr, key); }
		else if (isDefault) { return; }
		m_doc.setMember(ptr, key, m_doc.make(value, JsonText::Style::Inline, layer.tilesPointer));
	};
	const Cell& after = *change.after;
	const MaybeCell& before = change.before;
	if (before->id != after.id) { put("tileId", after.id, after.id == 0); }
	if (before->rotation != after.rotation) { put("rotation", mitiru::data::jsonFloat(after.rotation), after.rotation == 0.0f); }
	if (before->flipX != after.flipX) { put("flipX", after.flipX, !after.flipX); }
	if (before->flipY != after.flipY) { put("flipY", after.flipY, !after.flipY); }
}

void TileDoc::apply(Change& change)
{
	++m_revision;
	Layer& layer = m_layers[static_cast<std::size_t>(change.layer)];
	const std::size_t cell = cellOf(layer.width, change.x, change.y);
	if (!change.before && change.after) { insertTile(layer, change); ++layer.count; }
	else if (change.before && !change.after) { eraseTile(layer, change); --layer.count; }
	else if (change.before && change.after) { updateTile(layer, change); }
	layer.cells[cell] = change.after;
}

void TileDoc::commit(std::vector<Change> group)
{
	if (group.empty()) { return; }
	// 残した版が「やり直し」側にあるときは、追加するとその版へ戻れない
	if (m_keptDepth != kNever && m_keptDepth > m_undo.size()) { m_keptDepth = kNever; }
	m_redo.clear();
	m_undo.push_back(std::move(group));
	if (m_undo.size() > kMaxUndo)
	{
		m_undo.erase(m_undo.begin());
		m_keptDepth = m_keptDepth == kNever || m_keptDepth == 0 ? kNever : m_keptDepth - 1;
	}
}

bool TileDoc::set(int layer, int x, int y, const MaybeCell& cell)
{
	if (!inside(layer, x, y)) { return false; }
	const MaybeCell before = at(layer, x, y);
	if (before == cell) { return false; }
	Change change{ layer, x, y, before, cell, -1 };
	apply(change);
	if (m_grouping) { m_group.push_back(change); } else { commit({ change }); }
	return true;
}

void TileDoc::beginGroup()
{
	m_grouping = true;
	m_group.clear();
}

bool TileDoc::endGroup()
{
	m_grouping = false;
	const bool changed = !m_group.empty();
	commit(std::move(m_group));
	m_group.clear();
	return changed;
}

Clip TileDoc::copy(int layer, Rect rect) const
{
	Clip clip;
	if (layerAt(layer) == nullptr) { return clip; }
	clip.width = rect.width();
	clip.height = rect.height();
	clip.cells.reserve(cellOf(clip.width, 0, clip.height));
	for (int y = rect.y0; y <= rect.y1; ++y)
	{
		for (int x = rect.x0; x <= rect.x1; ++x) { clip.cells.push_back(at(layer, x, y)); }
	}
	return clip;
}

bool TileDoc::paste(int layer, int x, int y, const Clip& clip)
{
	beginGroup();
	for (int cy = 0; cy < clip.height; ++cy)
	{
		for (int cx = 0; cx < clip.width; ++cx) { set(layer, x + cx, y + cy, clip.at(cx, cy)); }
	}
	return endGroup();
}

bool TileDoc::clear(int layer, Rect rect)
{
	beginGroup();
	for (int y = rect.y0; y <= rect.y1; ++y)
	{
		for (int x = rect.x0; x <= rect.x1; ++x) { set(layer, x, y, std::nullopt); }
	}
	return endGroup();
}

bool TileDoc::mirror(int layer, Rect rect, bool horizontal)
{
	return paste(layer, rect.x0, rect.y0, mirrorClip(copy(layer, rect), horizontal));
}

bool TileDoc::undo()
{
	if (m_undo.empty()) { return false; }
	std::vector<Change> group = std::move(m_undo.back());
	m_undo.pop_back();
	for (auto it = group.rbegin(); it != group.rend(); ++it)
	{
		Change inverse = *it;
		std::swap(inverse.before, inverse.after);
		apply(inverse);
	}
	m_redo.push_back(std::move(group));
	return true;
}

bool TileDoc::redo()
{
	if (m_redo.empty()) { return false; }
	std::vector<Change> group = std::move(m_redo.back());
	m_redo.pop_back();
	for (Change& change : group) { apply(change); }
	m_undo.push_back(std::move(group));
	return true;
}

} // namespace mitiru::tool::tilemapedit
