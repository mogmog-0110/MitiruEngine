// 何を置けるか (タイルの一覧、タイルセットの PNG) は、ゲームが渡す編集スキーマが決める

#include "TilemapEditPage.hpp"

#include "../PageUtil.hpp"
#include "../pages/Pages.hpp"
#include "../pages/TypedValues.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace mitiru::tool
{

namespace
{

using tilemapedit::Cell;
using tilemapedit::Rect;

constexpr int kMapWidth = 760;
constexpr int kMapHeight = 600;
using tilemapedit::kPickerWidth;

std::string pathText(const std::filesystem::path& p)
{
	const auto u8 = p.generic_u8string();
	return std::string(u8.begin(), u8.end());
}

std::string sizeText(int w, int h) { return std::to_string(w) + "x" + std::to_string(h); }

} // namespace

TilemapEditPage::TilemapEditPage(const PageContext& ctx) : m_view(ctx.view)
{
	m_schemaLogical = queryValue(ctx.query, "schema");
	m_fileName = queryValue(ctx.query, "file");
	m_rootHint = queryValue(ctx.query, "root");
	m_scriptPath = queryValue(ctx.query, "script");
}

void TilemapEditPage::fail(const std::string& message)
{
	m_error = message;
	m_loaded = false;
}

void TilemapEditPage::resetEditState()
{
	m_selection.reset();
	m_pasting = false;
	m_drag = Drag{};
}

std::optional<DecodedImage> TilemapEditPage::loadTilesetImage(std::string& problem) const
{
	if (m_schema.tileset.image.empty()) { return std::nullopt; }
	stageedit::StageFiles imageFile;
	std::string imageError;
	if (!imageFile.open(m_schema.tileset.image, stageedit::SourceMap{}, m_rootHint, imageError)) { problem = "Tileset image not found"; return std::nullopt; }
	const std::string& bytes = imageFile.original();
	auto image = decodeImageBytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
	if (!image) { problem = "Can't decode the tileset image"; }
	return image;
}

void TilemapEditPage::load()
{
	m_error.clear();
	m_notice.clear();
	m_loaded = false;
	if (m_schemaLogical.empty()) { fail("Open with --page \"tilemap_edit?schema=<logical path of the schema>&file=<map file name>\""); return; }
	std::string error;
	stageedit::StageFiles schemaFile;
	if (!schemaFile.open(m_schemaLogical, stageedit::SourceMap{}, m_rootHint, error)) { fail("Can't read the schema: " + error); return; }
	if (!tilemapedit::parseTileSchema(schemaFile.original(), m_schema, error)) { fail("The schema is not valid: " + error); return; }
	if (m_fileName.empty())
	{
		for (const std::string& name : stageedit::StageFiles::listJson(m_schema.dir))
		{
			if (std::find(m_schema.exclude.begin(), m_schema.exclude.end(), name) == m_schema.exclude.end()) { m_fileName = name; break; }
		}
		if (m_fileName.empty()) { fail("No map files in " + m_schema.dir); return; }
	}
	if (!m_files.open(m_schema.dir + "/" + m_fileName, m_schema.source, m_rootHint, error)) { fail(error); return; }
	if (!m_doc.reset(m_files.original(), error)) { fail("Can't read " + m_fileName + ": " + error); return; }

	std::string imageProblem;
	std::optional<DecodedImage> image = loadTilesetImage(imageProblem);
	m_palette = std::make_unique<tilemapedit::TilePalette>(m_schema, std::move(image));
	if (m_palette->ids().empty())
	{
		fail(imageProblem.empty() ? "The tileset image has no whole tile" : imageProblem + ", and the schema lists no tiles to show instead");
		return;
	}
	if (!imageProblem.empty()) { m_notice = imageProblem + "; showing colors"; }
	m_viewport = std::make_unique<tilemapedit::TileView>(kMapWidth, kMapHeight);
	m_layer = 0;
	m_brush = Cell{ m_palette->ids().front(), 0.0f, false, false };
	m_tool = Tool::Paint;
	m_clip = tilemapedit::Clip{};
	resetEditState();
	m_viewport->fit(m_doc.width(m_layer), m_doc.height(m_layer));
	m_autoRevision = m_doc.revision();
	m_tried = m_files.deployedDiffers();
	m_status = m_tried ? "The game has an older try. Discard to reset" : "Opened the saved version";
	m_loaded = true;
}

void TilemapEditPage::start()
{
	if (!m_loaded && m_error.empty()) { load(); }
	if (m_loaded && !m_scriptPath.empty() && m_script.empty()) { loadScript(m_scriptPath); }
	pushAll();
}

void TilemapEditPage::tick(double)
{
	scriptStep();
	if (!m_imagesDirty) { return; }
	m_imagesDirty = false;
	pushImages();
}

tilemapedit::Overlay TilemapEditPage::overlay() const
{
	tilemapedit::Overlay o;
	o.selection = m_selection;
	if (m_pasting && !m_clip.empty())
	{
		o.ghost = &m_clip;
		o.ghostX = m_ghostX;
		o.ghostY = m_ghostY;
	}
	return o;
}

void TilemapEditPage::pushImages()
{
	if (!m_loaded) { return; }
	stageedit::Canvas map = m_viewport->render(m_doc, *m_palette, m_layer, overlay());
	m_view->setImage("map", map.width(), map.height(), map.takePixels());
	stageedit::Canvas tiles = m_palette->renderPicker(kPickerWidth, m_brush);
	m_view->setImage("tiles", tiles.width(), tiles.height(), tiles.takePixels());
}

void TilemapEditPage::pushPanel()
{
	nlohmann::json files = nlohmann::json::array();
	nlohmann::json layers = nlohmann::json::array();
	if (m_loaded)
	{
		for (const std::string& name : stageedit::StageFiles::listJson(m_schema.dir))
		{
			if (std::find(m_schema.exclude.begin(), m_schema.exclude.end(), name) != m_schema.exclude.end()) { continue; }
			files.push_back({ { "name", name }, { "cur", name == m_fileName } });
		}
		for (int i = 0; i < m_doc.layerCount(); ++i)
		{
			const std::string name = m_doc.layerName(i).empty() ? "Layer " + std::to_string(i + 1) : m_doc.layerName(i);
			layers.push_back({ { "i", i }, { "name", name }, { "info", sizeText(m_doc.width(i), m_doc.height(i)) + ", " + std::to_string(m_doc.tileCount(i)) + " tiles" }, { "cur", i == m_layer } });
		}
	}
	m_view->set("files", files);
	m_view->set("layers", layers);
	if (!m_loaded) { return; }
	std::string brush = m_brush.id == 0 ? "Eraser" : "Tile " + m_palette->label(m_brush.id);
	if (m_brush.flipX) { brush += ", flip X"; }
	if (m_brush.flipY) { brush += ", flip Y"; }
	if (m_brush.rotation != 0.0f) { brush += ", " + std::to_string(static_cast<int>(m_brush.rotation)) + " deg"; }
	m_view->set("brush_text", brush);
	std::string area;
	if (m_pasting) { area = "Pasting " + sizeText(m_clip.width, m_clip.height) + ". Click or drag to place, Esc to stop"; }
	else if (m_selection) { area = "Selected " + sizeText(m_selection->width(), m_selection->height()) + " at (" + std::to_string(m_selection->x0) + ", " + std::to_string(m_selection->y0) + ")"; }
	m_view->set("area_text", area);
	const int unmanaged = m_doc.unmanaged(m_layer);
	m_view->set("unmanaged", unmanaged > 0 ? std::to_string(unmanaged) + " tiles are outside the grid and stay as they are" : std::string());
}

void TilemapEditPage::pushStatus()
{
	m_view->set("status", m_status);
	m_view->set("error", m_error);
	m_view->set("notice", m_notice);
	m_view->set("loaded", m_loaded);
	m_view->set("dirty", m_loaded && m_doc.dirty());
	m_view->set("tried", m_tried);
	m_view->set("auto_try", m_auto);
	m_view->set("can_undo", m_loaded && m_doc.canUndo());
	m_view->set("can_redo", m_loaded && m_doc.canRedo());
	m_view->set("tool", m_tool == Tool::Paint ? "paint" : "select");
	m_view->set("has_selection", m_selection.has_value());
	m_view->set("has_clip", !m_clip.empty());
	m_view->set("pasting", m_pasting);
	if (!m_loaded) { return; }
	m_view->set("file", m_fileName);
	m_view->set("zoom_text", "Cell " + std::to_string(static_cast<int>(m_viewport->cellSize())) + " px");
	m_view->set("source_text", m_files.hasSource() ? "Source: " + pathText(*m_files.sourcePath()) : "No source file; only the game's copy is written");
}

void TilemapEditPage::pushAll()
{
	pushStatus();
	pushPanel();
	m_imagesDirty = true;
	pushImages();
}

void TilemapEditPage::changed(bool gestureEnd)
{
	m_imagesDirty = true;
	if (gestureEnd) { pushPanel(); }
	pushStatus();
	if (m_auto && gestureEnd && m_doc.revision() != m_autoRevision) { tryOut(); }
}

void TilemapEditPage::tryOut()
{
	if (!m_loaded) { return; }
	std::string error;
	if (!m_files.tryOut(m_doc.text(), error)) { m_status = "Can't try: " + error; pushStatus(); return; }
	m_tried = true;
	m_autoRevision = m_doc.revision();
	m_status = "Applied to the game. Keep or discard";
	pushStatus();
}

void TilemapEditPage::keep()
{
	if (!m_loaded) { return; }
	std::string error;
	if (!m_files.keep(m_doc.text(), error)) { m_status = "Can't keep: " + error; pushStatus(); return; }
	m_doc.markKept();
	m_tried = false;
	m_status = m_files.hasSource() ? "Kept: " + pathText(*m_files.sourcePath()) + " (previous version in .bak)" : "Kept in the game's copy only (no source file)";
	pushStatus();
}

void TilemapEditPage::discard()
{
	if (!m_loaded) { return; }
	std::string error;
	if ((m_tried || m_files.deployedDiffers()) && !m_files.restore(error)) { m_status = "Can't reset: " + error; pushStatus(); return; }
	if (!m_doc.reset(m_files.original(), error)) { m_status = "Can't reset: " + error; pushStatus(); return; }
	resetEditState();
	m_layer = std::min(m_layer, m_doc.layerCount() - 1);
	m_autoRevision = m_doc.revision();
	m_tried = false;
	m_status = "Discarded";
	pushAll();
}

void TilemapEditPage::openFile(const std::string& name)
{
	if (name.empty() || name == m_fileName) { return; }
	if (m_doc.dirty() || m_tried)
	{
		m_status = "Keep or discard this map first";
		pushStatus();
		return;
	}
	const std::string previous = m_fileName;
	m_fileName = name;
	load();
	if (!m_loaded)
	{
		const std::string reason = m_error;
		m_fileName = previous;
		load();
		m_status = "Can't open: " + reason;
	}
	pushAll();
}

void TilemapEditPage::undoRedo(bool undo)
{
	if (!m_loaded) { return; }
	const bool did = undo ? m_doc.undo() : m_doc.redo();
	if (did) { m_status = undo ? "Undone" : "Redone"; changed(true); }
}

void TilemapEditPage::chooseLayer(int index)
{
	if (!m_loaded || index < 0 || index >= m_doc.layerCount() || index == m_layer) { return; }
	m_layer = index;
	resetEditState();
	m_viewport->fit(m_doc.width(m_layer), m_doc.height(m_layer));
	m_status = "Layer: " + (m_doc.layerName(index).empty() ? std::to_string(index + 1) : m_doc.layerName(index));
	pushAll();
}

void TilemapEditPage::copySelection()
{
	if (!m_selection) { m_status = "Select an area first"; changed(false); return; }
	m_clip = m_doc.copy(m_layer, *m_selection);
	m_status = "Copied " + sizeText(m_clip.width, m_clip.height) + ". Ctrl+V to paste";
	changed(true);
}

void TilemapEditPage::startPaste()
{
	if (m_clip.empty()) { m_status = "Nothing copied yet"; changed(false); return; }
	m_pasting = true;
	m_tool = Tool::Select;
	m_ghostX = m_selection ? m_selection->x0 : std::max(0, m_viewport->cellAt(0, 0).first);
	m_ghostY = m_selection ? m_selection->y0 : std::max(0, m_viewport->cellAt(0, 0).second);
	m_status = "Click or drag to place the copy. Esc to stop";
	changed(true);
}

void TilemapEditPage::cancelPaste()
{
	m_pasting = false;
	m_status = "Paste stopped";
	changed(true);
}

void TilemapEditPage::flip(bool horizontal)
{
	const char* axis = horizontal ? "horizontally" : "vertically";
	if (m_pasting) { m_clip = tilemapedit::mirrorClip(m_clip, horizontal); m_status = std::string("Flipped the copy ") + axis; }
	else if (m_selection) { m_status = m_doc.mirror(m_layer, *m_selection, horizontal) ? std::string("Flipped the selection ") + axis : "Nothing to flip"; }
	else if (m_brush.id != 0) { m_brush = tilemapedit::mirrorCell(m_brush, horizontal); m_status = std::string("Flipped the brush ") + axis; }
	else { m_status = "Pick a tile or select an area to flip"; }
	changed(true);
}

void TilemapEditPage::clearSelection()
{
	if (!m_selection) { m_status = "Select an area first"; changed(false); return; }
	m_status = m_doc.clear(m_layer, *m_selection) ? "Cleared the selection" : "The selection is already empty";
	changed(true);
}

void TilemapEditPage::onAction(std::string_view name, const nlohmann::json& payload)
{
	if (name == "te.try") { tryOut(); return; }
	if (name == "te.keep") { keep(); return; }
	if (name == "te.discard") { discard(); return; }
	if (name == "te.undo") { undoRedo(true); return; }
	if (name == "te.redo") { undoRedo(false); return; }
	if (!m_loaded) { return; }
	const auto num = [&](const char* key) { const std::string s = payloadString(payload, key); return s.empty() ? -1 : std::atoi(s.c_str()); };
	if (name == "te.open") { openFile(payloadString(payload, "name")); return; }
	if (name == "te.auto") { m_auto = !m_auto; pushStatus(); return; }
	if (name == "te.fit") { m_viewport->fit(m_doc.width(m_layer), m_doc.height(m_layer)); changed(false); return; }
	if (name == "te.layer") { chooseLayer(num("i")); return; }
	if (name == "te.tool")
	{
		m_tool = payloadString(payload, "m") == "select" ? Tool::Select : Tool::Paint;
		if (m_tool == Tool::Paint) { m_pasting = false; }
		m_status = m_tool == Tool::Paint ? "Paint: drag to paint, right click to pick a tile" : "Select: drag to select an area";
		changed(true);
		return;
	}
	if (name == "te.copy") { copySelection(); return; }
	if (name == "te.paste") { startPaste(); return; }
	if (name == "te.flipH") { flip(true); return; }
	if (name == "te.flipV") { flip(false); return; }
	if (name == "te.clear") { clearSelection(); return; }
}

void TilemapEditPage::onKey(const KeyEvent& ev)
{
	using Key = KeyEvent::Key;
	if (!m_loaded) { return; }
	if (ev.ctrl && ev.key == Key::Z) { undoRedo(!ev.shift); return; }
	if (ev.ctrl && ev.key == Key::Y) { undoRedo(false); return; }
	if (ev.ctrl && ev.key == Key::C) { copySelection(); return; }
	if (ev.ctrl && ev.key == Key::V) { startPaste(); return; }
	if (ev.ctrl) { return; }
	switch (ev.key)
	{
	case Key::H: flip(true); return;
	case Key::V: flip(false); return;
	case Key::Delete: clearSelection(); return;
	case Key::F:
	case Key::Home: onAction("te.fit", nlohmann::json::object()); return;
	case Key::Escape:
		if (m_pasting) { cancelPaste(); } else { m_selection.reset(); changed(true); }
		return;
	default: return;
	}
}

std::unique_ptr<ToolPage> makeTilemapEditPage(const PageContext& ctx)
{
	return std::make_unique<TilemapEditPage>(ctx);
}

} // namespace mitiru::tool
