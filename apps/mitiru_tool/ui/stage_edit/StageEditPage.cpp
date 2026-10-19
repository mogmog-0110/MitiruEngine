// stage_edit。ステージの JSON を図の上で書き換え、試す / 残す / 捨てるでゲームとファイルへ渡す。
// 何が置けて、どんな欄を持つかは、ゲームが渡した編集スキーマ (JSON) が決める。

#include "StageEditPage.hpp"

#include "../PageUtil.hpp"
#include "../pages/Pages.hpp"
#include "../pages/TypedValues.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace mitiru::tool
{

namespace
{

using Json = nlohmann::json;
using mitiru::data::JsonText;
using stageedit::Item;

constexpr int kTopWidth = 760;
constexpr int kTopHeight = 400;
constexpr int kOrbitWidth = 760;
constexpr int kOrbitHeight = 230;
constexpr std::size_t kMaxListRows = 200;
const char* const kNotAllowed = "That value isn't allowed";

std::string number(double v)
{
	return JsonText::formatNumber(v, "0.0");
}

std::string colorHex(stageedit::Rgb c)
{
	char buf[16];
	std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", c.r, c.g, c.b);
	return buf;
}

std::string pathText(const std::filesystem::path& p)
{
	const auto u8 = p.generic_u8string();
	return std::string(u8.begin(), u8.end());
}

} // namespace

StageEditPage::StageEditPage(const PageContext& ctx) : m_view(ctx.view), m_query(ctx.query)
{
	m_schemaLogical = queryValue(ctx.query, "schema");
	m_fileName = queryValue(ctx.query, "file");
	m_rootHint = queryValue(ctx.query, "root");
	m_scriptPath = queryValue(ctx.query, "script");
}

void StageEditPage::fail(const std::string& message)
{
	m_error = message;
	m_loaded = false;
}

bool StageEditPage::loadSchema(const std::string& logical)
{
	stageedit::StageFiles schemaFile;
	std::string error;
	if (!schemaFile.open(logical, stageedit::SourceMap{}, m_rootHint, error)) { fail("Can't read the schema: " + error); return false; }
	if (!stageedit::parseSchema(schemaFile.original(), m_schema, error)) { fail("The schema is not valid: " + error); return false; }
	return true;
}

void StageEditPage::load()
{
	m_error.clear();
	m_loaded = false;
	if (m_schemaLogical.empty()) { fail("Open with --page \"stage_edit?schema=<logical path of the schema>&file=<stage file name>\""); return; }
	if (!loadSchema(m_schemaLogical)) { return; }
	if (m_fileName.empty()) { fail("Pass a stage file name with file= (for example file=r1_run.json)"); return; }
	std::string error;
	if (!m_files.open(m_schema.dir + "/" + m_fileName, m_schema.source, m_rootHint, error)) { fail(error); return; }
	if (!m_doc.load(&m_schema, m_files.original(), error)) { fail("Can't read " + m_fileName + ": " + error); return; }
	m_top = std::make_unique<stageedit::TopView>(m_schema, kTopWidth, kTopHeight);
	m_orbit = std::make_unique<stageedit::OrbitView>(m_schema, kOrbitWidth, kOrbitHeight);
	m_top->fit(m_doc);
	m_orbit->frame(m_doc);
	const auto gen = m_schema.generated.find(m_fileName);
	m_generatedBy = gen == m_schema.generated.end() ? std::string() : gen->second;
	m_tried = m_files.deployedDiffers();
	m_status = m_tried ? "The game has an older try. Discard to reset" : "Opened the saved version";
	m_loaded = true;
}

void StageEditPage::start()
{
	if (!m_loaded && m_error.empty()) { load(); }
	if (m_loaded && !m_scriptPath.empty() && m_script.empty()) { loadScript(m_scriptPath); }
	pushAll();
}

void StageEditPage::tick(double)
{
	scriptStep();
	if (!m_imagesDirty) { return; }
	m_imagesDirty = false;
	pushImages();
}

std::string StageEditPage::labelOf(const Item& item) const
{
	const stageedit::Kind& kind = m_schema.kinds[static_cast<std::size_t>(item.kind)];
	return item.index < 0 ? kind.label : kind.label + " " + std::to_string(item.index + 1);
}

std::string StageEditPage::summaryOf(const Item& item) const
{
	const stageedit::Aabb b = stageedit::boundsOf(item.geom);
	const stageedit::Geom& g = item.geom;
	if (g.shape == stageedit::Shape::Point)
	{
		return "(" + number(g.a.x) + ", " + number(g.a.y) + ", " + number(g.a.z) + ")";
	}
	return "x " + number(b.lo.x) + ".." + number(b.hi.x) + "  z " + number(b.lo.z) + ".." + number(b.hi.z);
}

void StageEditPage::pushImages()
{
	if (!m_loaded) { return; }
	stageedit::Canvas top = m_top->render(m_doc);
	m_view->setImage("top", top.width(), top.height(), top.takePixels());
	stageedit::Canvas orbit = m_orbit->render(m_doc);
	m_view->setImage("orbit", orbit.width(), orbit.height(), orbit.takePixels());
}

void StageEditPage::pushPanel()
{
	Json kinds = Json::array();
	Json rows = Json::array();
	Json list = Json::array();
	if (!m_loaded)
	{
		m_view->set("kinds", kinds);
		m_view->set("rows", rows);
		m_view->set("items", list);
		m_view->set("has_sel", false);
		return;
	}
	std::vector<int> counts(m_schema.kinds.size(), 0);
	for (const Item& item : m_doc.items()) { ++counts[static_cast<std::size_t>(item.kind)]; }
	for (std::size_t i = 0; i < m_schema.kinds.size(); ++i)
	{
		kinds.push_back({ { "i", static_cast<int>(i) }, { "label", m_schema.kinds[i].label }, { "color", colorHex(m_schema.kinds[i].color) }, { "count", counts[i] } });
	}
	const int sel = m_doc.selected();
	for (const stageedit::FieldRow& r : m_doc.rows(sel))
	{
		Json comps = Json::array();
		for (std::size_t i = 0; i < r.comps.size(); ++i)
		{
			comps.push_back({ { "i", static_cast<int>(i) }, { "n", i < r.compNames.size() ? r.compNames[i] : std::string() }, { "v", r.comps[i] } });
		}
		const bool vec = r.type == stageedit::FieldType::Vec3 || r.type == stageedit::FieldType::Vec2;
		const bool num = r.type == stageedit::FieldType::Number || r.type == stageedit::FieldType::Angle;
		rows.push_back({ { "key", r.key }, { "label", r.label }, { "present", r.present }, { "optional", r.optional },
		                 { "is_num", vec || num }, { "is_bool", r.type == stageedit::FieldType::Bool },
		                 { "is_text", r.type == stageedit::FieldType::Text || r.type == stageedit::FieldType::Color },
		                 { "comps", comps }, { "flag_text", r.flag ? "On" : "Off" }, { "text", r.text } });
	}
	const int listKind = sel >= 0 ? m_doc.items()[static_cast<std::size_t>(sel)].kind : 0;
	for (std::size_t i = 0; i < m_doc.items().size() && list.size() < kMaxListRows; ++i)
	{
		const Item& item = m_doc.items()[i];
		if (item.kind != listKind) { continue; }
		list.push_back({ { "i", static_cast<int>(i) }, { "label", labelOf(item) }, { "summary", summaryOf(item) }, { "sel", static_cast<int>(i) == sel } });
	}
	Json files = Json::array();
	for (const std::string& name : stageedit::StageFiles::listJson(m_schema.dir))
	{
		if (std::find(m_schema.exclude.begin(), m_schema.exclude.end(), name) != m_schema.exclude.end()) { continue; }
		files.push_back({ { "name", name }, { "cur", name == m_fileName }, { "gen", m_schema.generated.count(name) != 0 } });
	}
	m_view->set("files", files);
	m_view->set("kinds", kinds);
	m_view->set("rows", rows);
	m_view->set("items", list);
	m_view->set("list_title", m_schema.kinds[static_cast<std::size_t>(listKind)].label + " list");
	m_view->set("has_sel", sel >= 0);
	m_view->set("sel_label", sel >= 0 ? labelOf(m_doc.items()[static_cast<std::size_t>(sel)]) : std::string());
	m_view->set("sel_summary", sel >= 0 ? summaryOf(m_doc.items()[static_cast<std::size_t>(sel)]) : std::string());
	m_view->set("unmanaged", m_doc.unmanagedCount() > 0 ? std::to_string(m_doc.unmanagedCount()) + " items can't be edited and stay as they are" : std::string());
}

void StageEditPage::pushStatus()
{
	m_view->set("status", m_status);
	m_view->set("error", m_error);
	m_view->set("loaded", m_loaded);
	m_view->set("dirty", m_loaded && m_doc.dirty());
	m_view->set("tried", m_tried);
	m_view->set("auto_try", m_auto);
	m_view->set("can_undo", m_loaded && m_doc.canUndo());
	m_view->set("can_redo", m_loaded && m_doc.canRedo());
	m_view->set("mode", m_mode);
	m_view->set("gen_by", m_generatedBy);
	if (!m_loaded) { return; }
	m_view->set("file", m_fileName);
	const auto& top = m_schema.top;
	m_view->set("orient", std::string("Top view, up = ") + (top.upIsNegZ ? "-z" : "+z") + ", right = " + (top.rightIsNegX ? "-x" : "+x"));
	m_view->set("grid", number(m_top->gridStep()) + " m");
	m_view->set("snap_text", "Snap: move " + number(m_schema.snap.move) + " m, size " + number(m_schema.snap.size) + " m, height " + number(m_schema.snap.height) + " m, angle " + number(m_schema.snap.angle) + " deg");
	m_view->set("source_text", m_files.hasSource() ? "Source: " + pathText(*m_files.sourcePath()) : "No source file; only the game's copy is written");
}

void StageEditPage::pushAll()
{
	pushStatus();
	pushPanel();
	m_imagesDirty = true;
	pushImages();
}

void StageEditPage::changed(bool gestureEnd)
{
	m_imagesDirty = true;
	if (gestureEnd) { pushPanel(); }
	pushStatus();
	if (m_auto && gestureEnd) { tryOut(); }
}

void StageEditPage::tryOut()
{
	if (!m_loaded) { return; }
	const std::string text = m_doc.text();
	std::string error;
	if (!JsonText::parse(text, error)) { m_status = "Can't try: not valid JSON (" + error + ")"; pushStatus(); return; }
	if (!m_files.tryOut(text, error)) { m_status = "Can't try: " + error; pushStatus(); return; }
	m_tried = true;
	m_status = "Applied to the game. Keep or discard";
	pushStatus();
}

void StageEditPage::keep()
{
	if (!m_loaded) { return; }
	const std::string text = m_doc.text();
	std::string error;
	if (!m_files.keep(text, error)) { m_status = "Can't keep: " + error; pushStatus(); return; }
	m_doc.markKept();
	m_tried = false;
	m_status = m_files.hasSource() ? "Kept: " + pathText(*m_files.sourcePath()) + " (previous version in .bak)" : "Kept in the game's copy only (no source file)";
	pushStatus();
}

void StageEditPage::discard()
{
	if (!m_loaded) { return; }
	std::string error;
	if ((m_tried || m_files.deployedDiffers()) && !m_files.restore(error)) { m_status = "Can't reset: " + error; pushStatus(); return; }
	if (!m_doc.reset(m_files.original(), error)) { m_status = "Can't reset: " + error; pushStatus(); return; }
	m_tried = false;
	m_status = "Discarded";
	pushAll();
}

void StageEditPage::adopt()
{
	if (!m_loaded || m_generatedBy.empty()) { return; }
	stageedit::StageFiles schemaFile;
	std::string error;
	if (!schemaFile.open(m_schemaLogical, m_schema.source, m_rootHint, error)) { m_status = "Can't make manual: " + error; pushStatus(); return; }
	auto doc = JsonText::parse(schemaFile.original(), error);
	if (!doc || !doc->eraseMember("generated", m_fileName) || !schemaFile.keep(doc->render(), error))
	{
		m_status = "Can't make manual: " + (error.empty() ? std::string("not listed in the schema's generated") : error);
		pushStatus();
		return;
	}
	const std::string script = m_generatedBy;
	m_schema.generated.erase(m_fileName);
	m_generatedBy.clear();
	m_status = "Now manual (no longer owned by " + script + ")";
	pushStatus();
}

void StageEditPage::openFile(const std::string& name)
{
	if (name.empty() || name == m_fileName) { return; }
	if (m_doc.dirty() || m_tried)
	{
		m_status = "Keep or discard this stage first";
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

void StageEditPage::undoRedo(bool undo)
{
	if (!m_loaded) { return; }
	const bool did = undo ? m_doc.undo() : m_doc.redo();
	if (did) { m_status = undo ? "Undone" : "Redone"; changed(true); }
}

void StageEditPage::onAction(std::string_view name, const nlohmann::json& payload)
{
	if (name == "se.try") { tryOut(); return; }
	if (name == "se.keep") { keep(); return; }
	if (name == "se.discard") { discard(); return; }
	if (name == "se.adopt") { adopt(); return; }
	if (name == "se.undo") { undoRedo(true); return; }
	if (name == "se.redo") { undoRedo(false); return; }
	if (!m_loaded) { return; }
	const auto num = [&](const char* key) { const std::string s = payloadString(payload, key); return s.empty() ? -1 : std::atoi(s.c_str()); };
	if (name == "se.open") { openFile(payloadString(payload, "name")); return; }
	if (name == "se.auto") { m_auto = !m_auto; pushStatus(); return; }
	if (name == "se.view") { m_mode = payloadString(payload, "m"); pushStatus(); m_imagesDirty = true; return; }
	if (name == "se.fit") { m_top->fit(m_doc); m_orbit->frame(m_doc); changed(false); return; }
	if (name == "se.select") { m_doc.select(num("i")); changed(true); return; }
	if (name == "se.add")
	{
		const bool ok = m_doc.add(num("k"), m_top->centerX(), m_top->centerZ());
		m_status = ok ? "Added " + labelOf(m_doc.items()[static_cast<std::size_t>(m_doc.selected())]) : "Can't add (it exists, or the schema has no new)";
		changed(true);
		return;
	}
	if (name == "se.dup") { m_status = m_doc.duplicate() ? "Duplicated" : "Can't duplicate"; changed(true); return; }
	if (name == "se.del") { m_status = m_doc.remove() ? "Deleted" : "Can't delete this"; changed(true); return; }
	if (name == "se.field")
	{
		const auto edit = m_doc.editField(m_doc.selected(), payloadString(payload, "key"), num("i"), payloadString(payload, "v"));
		if (edit == stageedit::StageDoc::FieldEdit::Rejected) { m_status = kNotAllowed; }
		else if (m_status == kNotAllowed) { m_status.clear(); }   // 打ち直して通った後まで、途中の誤りを残さない
		changed(true);
		return;
	}
	if (name == "se.toggle") { m_doc.toggleField(m_doc.selected(), payloadString(payload, "key")); changed(true); return; }
	if (name == "se.addfield") { m_doc.addField(m_doc.selected(), payloadString(payload, "key")); changed(true); return; }
	if (name == "se.rmfield") { m_doc.removeField(m_doc.selected(), payloadString(payload, "key")); changed(true); return; }
}

std::unique_ptr<ToolPage> makeStageEditPage(const PageContext& ctx)
{
	return std::make_unique<StageEditPage>(ctx);
}

} // namespace mitiru::tool
