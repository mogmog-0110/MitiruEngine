// motion_tune 画面の実装。欄の行、タブ、状態の文、軌跡の図と数値を扱う。

#include "MotionTunePage.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace mitiru::tool
{

namespace
{

using Json = nlohmann::json;
using motiontune::Entry;

constexpr std::size_t kMaxRows = 220;
constexpr int kTraceWidth = 780;
constexpr int kTraceHeight = 250;
constexpr double kSliderSteps = 200.0;
const char* const kFavoriteLabel = "Favorites";

std::string valueText(const Entry& e, double v)
{
	if (e.kind == "flag") { return v >= 0.5 ? "true" : "false"; }
	char buf[48];
	std::snprintf(buf, sizeof(buf), e.kind == "whole" ? "%.0f" : "%.6g", v);
	return buf;
}

double sliderStep(const Entry& e)
{
	if (e.kind != "real") { return 1.0; }
	const double raw = (e.max - e.min) / kSliderSteps;
	const double decade = std::pow(10.0, std::floor(std::log10(std::max(raw, 1e-6))));
	return decade;
}

std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

bool matches(const Entry& e, const std::string& needle)
{
	if (needle.empty()) { return true; }
	return lower(e.key).find(needle) != std::string::npos || e.hint.find(needle) != std::string::npos
	    || lower(e.name).find(needle) != std::string::npos;
}

std::string signedMeters(double v)
{
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%+.2f m", v);
	return buf;
}

std::string plainMeters(double v)
{
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%.2f m", v);
	return buf;
}

Json statRow(const char* label, const std::string& base, const std::string& tuned, const std::string& delta)
{
	return { { "label", label }, { "base", base }, { "tuned", tuned }, { "delta", delta } };
}

} // namespace

std::vector<int> MotionTunePage::visibleRows() const
{
	std::vector<int> out;
	if (!m_model) { return out; }
	const std::string needle = lower(m_filter);
	const auto& entries = m_model->entries();
	const std::vector<std::string>& favorites = m_schema.favorites;
	const int tab = std::clamp(m_tab, 0, static_cast<int>(m_schema.files.size()));
	if (tab == 0)
	{
		for (const std::string& name : favorites)
		{
			for (std::size_t i = 0; i < entries.size(); ++i)
			{
				if (entries[i].name == name && matches(entries[i], needle)) { out.push_back(static_cast<int>(i)); }
			}
		}
		return out;
	}
	for (std::size_t i = 0; i < entries.size() && out.size() < kMaxRows; ++i)
	{
		if (entries[i].file == tab - 1 && matches(entries[i], needle)) { out.push_back(static_cast<int>(i)); }
	}
	return out;
}

void MotionTunePage::pushRows()
{
	Json tabs = Json::array();
	Json rows = Json::array();
	if (m_model)
	{
		const std::vector<int> unsaved = m_model->unsaved();
		const auto changedIn = [&](int file) {
			return static_cast<int>(std::count_if(unsaved.begin(), unsaved.end(), [&](int i) { return m_model->entries()[static_cast<std::size_t>(i)].file == file; }));
		};
		tabs.push_back({ { "i", 0 }, { "label", kFavoriteLabel }, { "cur", m_tab == 0 }, { "n", static_cast<int>(m_schema.favorites.size()) }, { "chg", 0 } });
		for (std::size_t f = 0; f < m_schema.files.size(); ++f)
		{
			tabs.push_back({ { "i", static_cast<int>(f) + 1 }, { "label", m_schema.files[f].label }, { "cur", m_tab == static_cast<int>(f) + 1 }, { "n", 0 }, { "chg", changedIn(static_cast<int>(f)) } });
		}
		int lastGroup = -1;
		int lastFile = -1;
		for (const int i : visibleRows())
		{
			const Entry& e = m_model->entries()[static_cast<std::size_t>(i)];
			const bool head = (e.file != lastFile || e.group != lastGroup) && !e.groupLabel.empty();
			lastFile = e.file;
			lastGroup = e.group;
			rows.push_back({ { "n", e.name }, { "key", e.key }, { "hint", e.hint }, { "head", head }, { "glabel", e.groupLabel },
			                 { "flag", e.kind == "flag" }, { "toggle", e.editValue >= 0.5 ? "0" : "1" }, { "min", e.min }, { "max", e.max }, { "step", sliderStep(e) },
			                 { "v", valueText(e, e.editValue) }, { "vnum", e.editValue }, { "file", valueText(e, e.fileValue) },
			                 { "chg", e.hasFile && !motiontune::sameValue(e.editValue, e.fileValue) },
			                 { "ingame", e.hasLive && e.hasFile && !motiontune::sameValue(e.liveValue, e.fileValue) },
			                 { "nolive", !e.hasLive }, { "nofile", !e.hasFile } });
		}
	}
	m_view->set("tabs", tabs);
	m_view->set("rows", rows);
	m_view->set("filter", m_filter);
}

void MotionTunePage::pushStatus()
{
	std::string files;
	for (const std::string& e : m_fileErrors) { files += (files.empty() ? "" : " / ") + e; }
	m_view->set("error", m_error);
	m_view->set("file_errors", files);
	m_view->set("loaded", m_loaded);
	m_view->set("connected", m_connected);
	m_view->set("status", m_status);
	m_view->set("trace_note", m_traceNote);
	m_view->set("unsaved", m_model ? static_cast<int>(m_model->unsaved().size()) : 0);
	m_view->set("untried", m_model ? static_cast<int>(m_model->untried().size()) : 0);
	m_view->set("ingame", m_model ? static_cast<int>(m_model->tried().size()) : 0);
	m_view->set("seconds", m_schema.trace.seconds);
	m_view->set("port", m_http != nullptr ? m_http->port() : 0);
}

void MotionTunePage::pushTrace()
{
	Json stats = Json::array();
	const motiontune::Trace* base = baseTrace();
	const motiontune::Trace* tuned = tunedTrace();
	if (base != nullptr)
	{
		const motiontune::TraceStats b = motiontune::statsOf(*base);
		const motiontune::TraceStats t = tuned != nullptr ? motiontune::statsOf(*tuned) : b;
		const auto cell = [&](double v) { return plainMeters(v); };
		const std::string none = "—";
		stats.push_back(statRow("Peak height", cell(b.apexRise), tuned ? cell(t.apexRise) : none, tuned ? signedMeters(t.apexRise - b.apexRise) : none));
		stats.push_back(statRow("Distance", cell(b.travel), tuned ? cell(t.travel) : none, tuned ? signedMeters(t.travel - b.travel) : none));
		stats.push_back(statRow("End height", signedMeters(b.endRise), tuned ? signedMeters(t.endRise) : none, tuned ? signedMeters(t.endRise - b.endRise) : none));
		if (!m_schema.trace.speed.empty())
		{
			char bs[32], ts[32], ds[32];
			std::snprintf(bs, sizeof(bs), "%.1f m/s", b.peakSpeed);
			std::snprintf(ts, sizeof(ts), "%.1f m/s", t.peakSpeed);
			std::snprintf(ds, sizeof(ds), "%+.1f m/s", t.peakSpeed - b.peakSpeed);
			stats.push_back(statRow("Top speed", bs, tuned ? ts : none, tuned ? ds : none));
		}
		if (m_imageDirty)
		{
			m_imageDirty = false;
			stageedit::Canvas image = motiontune::renderTrace(base, tuned, kTraceWidth, kTraceHeight);
			m_view->setImage("trace", image.width(), image.height(), image.takePixels());
		}
	}
	m_view->set("stats", stats);
	m_view->set("has_trace", base != nullptr);
	m_view->set("has_tuned", tuned != nullptr);
}

void MotionTunePage::writeReport() const
{
	if (m_reportPath.empty() || !m_model) { return; }
	const auto statsJson = [](const motiontune::Trace& t) {
		const motiontune::TraceStats s = motiontune::statsOf(t);
		return Json{ { "apexRise", s.apexRise }, { "travel", s.travel }, { "endRise", s.endRise }, { "peakSpeed", s.peakSpeed }, { "frames", t.points.size() } };
	};
	Json edits = Json::object();
	Json live = Json::object();
	for (const Entry& e : m_model->entries())
	{
		if (e.hasFile && !motiontune::sameValue(e.editValue, e.fileValue)) { edits[e.name] = e.editValue; }
		if (e.hasLive && !motiontune::sameValue(e.liveValue, e.fileValue)) { live[e.name] = e.liveValue; }
	}
	Json report{ { "connected", m_connected }, { "status", m_status }, { "traceNote", m_traceNote }, { "unsaved", m_model->unsaved().size() },
	             { "untried", m_model->untried().size() }, { "edits", edits }, { "liveDiffersFromFile", live }, { "idle", idle() } };
	report["base"] = m_hasBase ? statsJson(m_base) : Json(nullptr);
	report["tuned"] = m_hasTuned ? statsJson(m_tuned) : Json(nullptr);
	std::ofstream out(m_reportPath, std::ios::binary | std::ios::trunc);
	out << report.dump(1);
}

} // namespace mitiru::tool
