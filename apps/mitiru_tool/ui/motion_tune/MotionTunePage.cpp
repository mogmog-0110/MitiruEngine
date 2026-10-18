// motion_tune の実装。調整値をスライダーと数値で変更し、試す / 残す / 捨てるの操作をゲームと JSON に反映する。
// 項目はゲームから渡されるスキーマ (JSON)、現在値は実行中のゲームの GameMemory (GET /api/ai/state?quiet=1) が決める。

#include "MotionTunePage.hpp"

#include "../pages/Pages.hpp"
#include "../pages/TypedValues.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace mitiru::tool
{

namespace
{

using Json = nlohmann::json;
using motiontune::Entry;

constexpr double kQuietPollSeconds = 1.0;
constexpr double kTraceDebounceSeconds = 0.5;
constexpr int kFramesPerSecond = 60;
constexpr int kMinTraceFrames = 2;

const char* const kTagQuiet = "mt.quiet";
const char* const kTagRing = "mt.ring";
const char* const kTagTry = "mt.try";
const char* const kTagKeep = "mt.keep";
const char* const kTagRevert = "mt.revert";
const char* const kTagTrace = "mt.trace";

const char* const kWaiting = "Waiting for the game";
const char* const kOffline = "Not connected";

std::string countText(std::size_t n, const std::string& noun)
{
	return std::to_string(n) + " " + noun + (n == 1 ? "" : "s");
}

} // namespace

std::unique_ptr<ToolPage> makeMotionTunePage(const PageContext& ctx)
{
	return std::make_unique<MotionTunePage>(ctx);
}

MotionTunePage::MotionTunePage(const PageContext& ctx) : m_view(ctx.view), m_http(ctx.http)
{
	m_schemaLogical = queryValue(ctx.query, "schema");
	m_rootHint = queryValue(ctx.query, "root");
	m_scriptPath = queryValue(ctx.query, "script");
	m_reportPath = queryValue(ctx.query, "report");
	m_poll.interval = kQuietPollSeconds;
}

void MotionTunePage::fail(const std::string& message)
{
	m_error = message;
	m_loaded = false;
}

void MotionTunePage::load()
{
	m_error.clear();
	m_loaded = false;
	if (m_schemaLogical.empty()) { fail("Open with --page \"motion_tune?schema=<logical path of the schema>\""); return; }
	stageedit::StageFiles schemaFile;
	std::string error;
	if (!schemaFile.open(m_schemaLogical, stageedit::SourceMap{}, m_rootHint, error)) { fail("Can't read the schema: " + error); return; }
	if (!motiontune::parseTuneSchema(schemaFile.original(), m_schema, error)) { fail("The schema is not valid: " + error); return; }
	m_model = std::make_unique<motiontune::TuneModel>(m_schema);
	for (std::size_t i = 0; i < m_schema.files.size(); ++i)
	{
		auto file = std::make_unique<stageedit::StageFiles>();
		const motiontune::FileDecl& decl = m_schema.files[i];
		if (!file->open(decl.path, m_schema.source, m_rootHint, error) || !m_model->loadFile(static_cast<int>(i), file->original(), error))
		{
			m_fileErrors.push_back(decl.id + ": " + error);
			file.reset();
		}
		m_files.push_back(std::move(file));
	}
	m_loaded = true;
	m_status = kWaiting;
}

void MotionTunePage::start()
{
	if (!m_loaded && m_error.empty()) { load(); }
	if (m_loaded && !m_scriptPath.empty() && m_script.empty()) { loadScript(m_scriptPath); }
	pushAll();
}

void MotionTunePage::pushAll()
{
	pushRows();
	pushStatus();
	pushTrace();
	writeReport();
}

void MotionTunePage::send(const char* tag, const char* method, const char* path, std::string body)
{
	if (m_http == nullptr) { return; }
	if (std::string_view(tag) != kTagQuiet) { ++m_inflight; }
	m_http->send({ tag, method, path, std::move(body) });
}

void MotionTunePage::tick(double now)
{
	m_now = now;
	scriptStep();
	if (!m_loaded || m_http == nullptr) { return; }
	if (m_poll.due(now)) { send(kTagQuiet, "GET", "/api/ai/state?quiet=1", ""); }
	if (m_traceAt >= 0.0 && now >= m_traceAt && m_connected)
	{
		m_traceAt = -1.0;
		requestTrace();
	}
}

void MotionTunePage::onAction(std::string_view name, const Json& payload)
{
	if (!m_loaded) { return; }
	if (name == "mt.set") { set(payloadString(payload, "n"), payloadString(payload, "v")); }
	else if (name == "mt.tab") { m_tab = std::atoi(payloadString(payload, "i").c_str()); }
	else if (name == "mt.filter") { m_filter = payloadString(payload, "v"); }
	else if (name == "mt.try") { tryOut(); }
	else if (name == "mt.keep") { keep(); }
	else if (name == "mt.discard") { discard(); }
	else if (name == "mt.trace") { requestTrace(); }
	else { return; }
	pushAll();
}

void MotionTunePage::set(const std::string& name, const std::string& text)
{
	char* end = nullptr;
	const double v = std::strtod(text.c_str(), &end);
	const bool converted = end != text.c_str();
	while (end != nullptr && (*end == ' ' || *end == '\t')) { ++end; }
	if (!converted || *end != '\0') { m_status = "Not a number: " + text; return; }
	if (!m_model->set(name, v)) { m_status = "Unknown field: " + name; return; }
	m_traceAt = m_now + kTraceDebounceSeconds;
	m_status = m_model->unsaved().empty() ? "Back to the saved values" : countText(m_model->unsaved().size(), "field") + " changed";
}

void MotionTunePage::commit(const char* tag, const Json& values, std::string okText)
{
	if (values.empty()) { m_status = okText; return; }
	if (!m_connected) { m_status = std::string(kOffline) + " (port " + std::to_string(m_http != nullptr ? m_http->port() : 0) + ")"; return; }
	m_status = "Sending…";
	m_sent[tag] = values;
	send(tag, "POST", "/api/ai/commit", values.dump());
}

void MotionTunePage::tryOut()
{
	commit(kTagTry, m_model->valuesOf(m_model->untried(), false), "Already in the game");
}

void MotionTunePage::discard()
{
	m_model->revertEdits();
	m_hasTuned = false;
	m_imageDirty = true;
	m_traceAt = m_now + kTraceDebounceSeconds;
	commit(kTagRevert, m_model->valuesOf(m_model->tried(), true), "Nothing to discard");
}

void MotionTunePage::keep()
{
	const std::vector<int> changed = m_model->unsaved();
	std::vector<bool> touched(m_files.size(), false);
	for (const int i : changed) { touched[static_cast<std::size_t>(m_model->entries()[static_cast<std::size_t>(i)].file)] = true; }
	std::size_t written = 0;
	for (std::size_t fi = 0; fi < m_files.size(); ++fi)
	{
		if (!touched[fi]) { continue; }
		std::string error;
		const auto text = m_files[fi] ? m_model->render(static_cast<int>(fi), m_files[fi]->original(), error) : std::nullopt;
		if (!text || !m_files[fi]->keep(*text, error))
		{
			m_status = "Couldn't keep: " + (error.empty() ? "can't open " + m_schema.files[fi].id : error);
			return;
		}
		m_model->markKept(static_cast<int>(fi));
		++written;
	}
	m_status = changed.empty() ? "Nothing to write" : "Kept " + countText(changed.size(), "field") + " in " + countText(written, "file");
	commit(kTagKeep, m_model->valuesOf(m_model->untried(), false), m_status);
}

void MotionTunePage::requestTrace()
{
	if (!m_connected) { m_traceNote = kOffline; return; }
	m_traceWanted = true;
	send(kTagRing, "GET", "/api/ai/ringsize", "");
}

void MotionTunePage::sendTraces(int ringSize)
{
	const int wanted = static_cast<int>(std::lround(m_schema.trace.seconds * kFramesPerSecond));
	const int frames = std::min(wanted, ringSize - 1);
	if (frames < kMinTraceFrames)
	{
		m_traceNote = "Not enough rewind history (" + std::to_string(ringSize) + " frames)";
		m_traceWanted = false;
		return;
	}
	Json probe = Json::array({ m_schema.trace.x, m_schema.trace.y, m_schema.trace.z });
	if (!m_schema.trace.speed.empty()) { probe.push_back(m_schema.trace.speed); }
	const std::vector<int> fields = m_model->unsaved();
	m_traceWithEdits = !fields.empty();
	m_hasBase = false;
	m_hasTuned = false;
	m_traceFailed = false;
	m_traceNote.clear();
	m_traceOutstanding = m_traceWithEdits ? 2 : 1;
	const std::string serial = std::to_string(++m_traceSerial);
	const auto body = [&](const Json& overrides) {
		return Json{ { "framesAgo", frames }, { "frames", frames }, { "overrides", overrides }, { "probe", probe } }.dump();
	};
	send((std::string(kTagTrace) + ".base." + serial).c_str(), "POST", "/api/ai/trace", body(m_model->valuesOf(fields, true)));
	if (m_traceWithEdits)
	{
		send((std::string(kTagTrace) + ".tuned." + serial).c_str(), "POST", "/api/ai/trace", body(m_model->valuesOf(fields, false)));
	}
}

void MotionTunePage::onHttp(const HttpResponse& res)
{
	const bool isQuiet = res.tag == kTagQuiet;
	if (isQuiet) { m_poll.done(); }
	else if (m_inflight > 0) { --m_inflight; }
	const Json body = parseBody(res.body);
	if (isQuiet)
	{
		if (onQuiet(res.status, body)) { pushAll(); }
		return;
	}
	if (res.tag == kTagRing) { onRing(res.status, body); }
	else if (res.tag == kTagTry || res.tag == kTagKeep || res.tag == kTagRevert) { onCommit(res, body); }
	else if (res.tag.rfind(kTagTrace, 0) == 0) { onTraceResponse(res, body); }
	else { return; }
	pushAll();
}

bool MotionTunePage::onQuiet(int status, const Json& body)
{
	m_connected = status == 200 && body.is_object();
	if (!m_connected)
	{
		const std::string was = m_status;
		m_status = std::string(kOffline) + " (port " + std::to_string(m_http->port()) + ")";
		return was != m_status;
	}
	const int n = m_model->setLive(body);
	if (m_status.rfind(kWaiting, 0) == 0 || m_status.rfind(kOffline, 0) == 0)
	{
		m_status = n == 0 ? "The game has no tuning fields" : "Read " + countText(static_cast<std::size_t>(n), "field") + " from the game";
		m_traceAt = m_now;
	}
	const bool changed = body != m_lastQuiet;
	m_lastQuiet = body;
	return changed;
}

void MotionTunePage::onRing(int status, const Json& body)
{
	if (!m_traceWanted) { return; }
	if (status != 200 || !body.is_object()) { m_traceWanted = false; m_traceNote = "Can't read the rewind length"; return; }
	sendTraces(body.value("ringSize", 0));
}

void MotionTunePage::onCommit(const HttpResponse& res, const Json& body)
{
	if (res.status != 200 || (body.is_object() && body.contains("error")))
	{
		m_status = "The game refused: " + (body.is_object() && body.contains("error") ? body["error"].dump() : "HTTP " + std::to_string(res.status));
		return;
	}
	m_model->markLive(m_sent[res.tag]);
	if (res.tag == kTagRevert) { m_status = "Discarded"; }
	else if (res.tag == kTagTry) { m_status = "Applied to the game"; }
}

void MotionTunePage::onTraceResponse(const HttpResponse& res, const Json& body)
{
	const std::string suffix = "." + std::to_string(m_traceSerial);
	if (res.tag.size() < suffix.size() || res.tag.compare(res.tag.size() - suffix.size(), suffix.size(), suffix) != 0) { return; }
	const bool base = res.tag.find(".base.") != std::string::npos;
	motiontune::Trace parsed;
	std::string error;
	const bool parsedOk = motiontune::parseTrace(body, !m_schema.trace.speed.empty(), parsed, error);
	if (res.status != 200 || !parsedOk)
	{
		m_traceNote = "Can't replay the trace: " + (error.empty() ? "HTTP " + std::to_string(res.status) : error);
		m_traceFailed = true;
	}
	else if (base) { m_base = std::move(parsed); m_hasBase = true; m_imageDirty = true; }
	else { m_tuned = std::move(parsed); m_hasTuned = true; m_imageDirty = true; }
	if (--m_traceOutstanding <= 0) { m_traceWanted = false; m_traceOutstanding = 0; }
}

} // namespace mitiru::tool
