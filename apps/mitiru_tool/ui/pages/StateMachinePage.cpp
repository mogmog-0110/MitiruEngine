// statemachine。ゲームが宣言した状態機械を図にして、今の状態、遷移の回数、直近の経過を見る (読み取り専用)。
// 何があるかは DLL が MITIRU_INSPECT_ASSETS で渡した kind "statemachine" の JSON (debug/StateMachineInspect.hpp)。
// 値と遷移の記録は host が更新のたびに取って snapshot の "statemachine" に載せる。窓は読んで描くだけで、状態は変えない。
// 状態を置く場所や遷移の条件はゲームの仕事で、ここでは宣言に無い遷移を赤で出すところまで。

#include "Pages.hpp"
#include "StateMachineGraph.hpp"
#include "TypedValues.hpp"

#include "../PageUtil.hpp"
#include "../ToolPalette.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

#include <mitiru/debug/StateMachineInspect.hpp>

namespace mitiru::tool
{

namespace
{

constexpr float kCanvasW = 440.0f;
constexpr float kBoxW = 84.0f;
constexpr float kBoxH = 22.0f;
constexpr float kImageScale = 2.0f;
constexpr std::size_t kMaxNodes = 24;
constexpr long long kRecentFrames = 30;
constexpr double kFramesPerSecond = 60.0;
constexpr int kDefaultWindow = 300;
constexpr double kMinLabelPercent = 11.0;


struct StateDef
{
	long long value = 0;
	std::string name;
};

struct EdgeDef
{
	long long from = 0;
	long long to = 0;
};

struct MachineDef
{
	std::string id;
	std::string label;
	std::string type;
	std::vector<StateDef> states;
	std::vector<EdgeDef> edges;
};

struct Node
{
	long long value = 0;
	std::string name;
	bool declared = true;
	std::uint64_t entered = 0;
};

struct EdgeRow
{
	int from = 0;
	int to = 0;
	std::uint64_t count = 0;
	std::uint64_t last = 0;
	bool declared = false;
};

struct Segment
{
	int node = 0;
	std::uint64_t start = 0;
	std::uint64_t end = 0;
};

struct Derived
{
	std::vector<Node> nodes;
	std::vector<EdgeRow> edges;
	std::vector<Segment> segments;
	int current = -1;
	std::uint64_t span = 0;
	std::size_t undeclared = 0;
};

std::string dp(float v)
{
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%.1fdp", static_cast<double>(v));
	return buf;
}

std::string percent(double v)
{
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%.2f%%", v);
	return buf;
}

std::uint32_t rawOf(const Snapshot* v)
{
	return static_cast<std::uint32_t>(numberOr(v, 0.0));
}

std::string readFile(const std::string& utf8Path)
{
	const std::u8string u(utf8Path.begin(), utf8Path.end());
	std::ifstream f(std::filesystem::path(u), std::ios::binary);
	if (!f) { return {}; }
	return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string parseDefs(const std::string& text, std::vector<MachineDef>& out)
{
	out.clear();
	const Snapshot root = Snapshot::parse(text, nullptr, false);
	const Snapshot* list = root.is_discarded() ? nullptr : findAt(root, { "machines" });
	if (list == nullptr || !list->is_array()) { return "The declaration has no machines array"; }
	for (const Snapshot& m : *list)
	{
		MachineDef def;
		def.id = stringOr(findAt(m, { "id" }), "");
		def.label = stringOr(findAt(m, { "label" }), def.id);
		def.type = stringOr(findAt(m, { "type" }), "u8");
		if (const Snapshot* states = findAt(m, { "states" }); states != nullptr && states->is_array())
		{
			for (const Snapshot& s : *states)
			{
				def.states.push_back({ static_cast<long long>(numberOr(findAt(s, { "v" }), 0.0)), stringOr(findAt(s, { "name" }), "") });
			}
		}
		if (const Snapshot* edges = findAt(m, { "edges" }); edges != nullptr && edges->is_array())
		{
			for (const Snapshot& e : *edges)
			{
				def.edges.push_back({ static_cast<long long>(numberOr(findAt(e, { "from" }), 0.0)),
				                      static_cast<long long>(numberOr(findAt(e, { "to" }), 0.0)) });
			}
		}
		out.push_back(std::move(def));
	}
	return {};
}

class Builder
{
public:
	Builder(const MachineDef& def, const Snapshot& obs, std::uint64_t frame, int window) : m_def(def), m_frame(frame)
	{
		for (const StateDef& s : def.states) { m_d.nodes.push_back({ s.value, s.name, true, 0 }); }
		const bool declaredEdges = !def.edges.empty();
		for (const EdgeDef& e : def.edges)
		{
			const int a = nodeFor(e.from), b = nodeFor(e.to);
			if (a >= 0 && b >= 0) { edgeFor(a, b).declared = true; }
		}
		const Snapshot* edges = findAt(obs, { "edges" });
		for (std::size_t i = 0; edges != nullptr && edges->is_array() && i < edges->size(); ++i)
		{
			const Snapshot& e = (*edges)[i];
			if (!e.is_array() || e.size() < 4) { continue; }
			const int a = nodeFor(decode(rawOf(&e[0]))), b = nodeFor(decode(rawOf(&e[1])));
			if (a < 0 || b < 0) { continue; }
			EdgeRow& row = edgeFor(a, b);
			row.count = static_cast<std::uint64_t>(numberOr(&e[2], 0.0));
			row.last = static_cast<std::uint64_t>(numberOr(&e[3], 0.0));
			if (!declaredEdges) { row.declared = true; }
			m_d.nodes[static_cast<std::size_t>(b)].entered += row.count;
		}
		m_d.current = nodeFor(decode(rawOf(findAt(obs, { "cur" }))));
		buildSegments(obs, window);
		for (const EdgeRow& e : m_d.edges) { if (e.count > 0 && !e.declared) { ++m_d.undeclared; } }
	}

	Derived take() { return std::move(m_d); }

private:
	long long decode(std::uint32_t raw) const { return debug::decodeStateValue(m_def.type, raw); }

	int nodeFor(long long value)
	{
		for (std::size_t i = 0; i < m_d.nodes.size(); ++i) { if (m_d.nodes[i].value == value) { return static_cast<int>(i); } }
		if (m_d.nodes.size() >= kMaxNodes) { return -1; }
		m_d.nodes.push_back({ value, "?" + std::to_string(value), false, 0 });
		return static_cast<int>(m_d.nodes.size()) - 1;
	}

	EdgeRow& edgeFor(int a, int b)
	{
		for (EdgeRow& e : m_d.edges) { if (e.from == a && e.to == b) { return e; } }
		m_d.edges.push_back({ a, b, 0, 0, false });
		return m_d.edges.back();
	}

	void buildSegments(const Snapshot& obs, int window)
	{
		const std::uint64_t t0 = m_frame > static_cast<std::uint64_t>(window) ? m_frame - static_cast<std::uint64_t>(window) : 0;
		m_d.span = m_frame - t0;
		if (m_d.span == 0 || m_d.current < 0) { return; }
		const Snapshot* events = findAt(obs, { "events" });
		std::vector<Segment> segs;
		int open = m_d.current;
		std::uint64_t openStart = t0;
		if (events != nullptr && events->is_array() && !events->empty() && (*events)[0].is_array() && (*events)[0].size() >= 3)
		{
			open = nodeFor(decode(rawOf(&(*events)[0][1])));
		}
		for (std::size_t i = 0; events != nullptr && events->is_array() && i < events->size(); ++i)
		{
			const Snapshot& e = (*events)[i];
			if (!e.is_array() || e.size() < 3) { continue; }
			const auto at = static_cast<std::uint64_t>(numberOr(&e[0], 0.0));
			const int to = nodeFor(decode(rawOf(&e[2])));
			if (at > t0 && open >= 0) { segs.push_back({ open, std::max(openStart, t0), std::min(at, m_frame) }); }
			if (at > t0) { openStart = at; }
			open = to;
		}
		if (open >= 0) { segs.push_back({ open, std::max(openStart, t0), m_frame }); }
		for (const Segment& s : segs) { if (s.end > s.start) { m_d.segments.push_back(s); } }
	}

	const MachineDef& m_def;
	std::uint64_t m_frame;
	Derived m_d;
};

EdgeLook lookOf(const EdgeRow& e, const Derived& d, std::uint64_t frame)
{
	if (e.count > 0 && !e.declared) { return EdgeLook::Undeclared; }
	if (e.from == d.current) { return EdgeLook::Outgoing; }
	if (e.count > 0 && frame >= e.last && frame - e.last <= static_cast<std::uint64_t>(kRecentFrames)) { return EdgeLook::Recent; }
	return e.count > 0 ? EdgeLook::Seen : EdgeLook::Idle;
}

nlohmann::json segmentRows(const Derived& d)
{
	nlohmann::json rows = nlohmann::json::array();
	for (std::size_t i = 0; i < d.segments.size(); ++i)
	{
		const Segment& s = d.segments[i];
		const double pct = 100.0 * static_cast<double>(s.end - s.start) / static_cast<double>(d.span);
		const Node& n = d.nodes[static_cast<std::size_t>(s.node)];
		rows.push_back({ { "w", percent(pct) }, { "label", pct >= kMinLabelPercent ? n.name : std::string() },
		                 { "bg", i + 1 == d.segments.size() ? palette_detail::hex(toolPalette().accent) : toolBandShades()[static_cast<std::size_t>(s.node) % toolBandShades().size()] },
		                 { "last", i + 1 == d.segments.size() }, { "tip", n.name + "  " + std::to_string(s.end - s.start) + " f" } });
	}
	return rows;
}

std::string spanText(std::uint64_t span)
{
	char buf[64];
	std::snprintf(buf, sizeof(buf), "Last %llu f (%.1f s)", static_cast<unsigned long long>(span), static_cast<double>(span) / kFramesPerSecond);
	return buf;
}

class StateMachinePage final : public ToolPage
{
public:
	explicit StateMachinePage(const PageContext& ctx) : m_view(ctx.view) {}

	void start() override { push(); }

	void onSnapshot(const Snapshot& snap, bool) override
	{
		const auto assets = snapshotAssets(snap, debug::kStateMachineInspectKind);
		const std::string file = assets.empty() ? std::string() : assets.front().file;
		if (file != m_file)
		{
			m_file = file;
			m_error = file.empty() ? std::string() : parseDefs(readFile(file), m_defs);
			if (file.empty()) { m_defs.clear(); }
		}
		m_snap = snap;
		push();
	}

	void onAction(std::string_view name, const nlohmann::json& payload) override
	{
		if (name == "sm.select") { m_selected = payloadString(payload, "id"); }
		else if (name == "sm.window") { m_window = std::clamp(std::atoi(payloadString(payload, "n").c_str()), 30, 3600); }
		else { return; }
		push();
	}

private:
	void push()
	{
		const Snapshot* machines = findAt(m_snap, { "statemachine", "state", "machines" });
		const auto frame = static_cast<std::uint64_t>(numberOr(findAt(m_snap, { "statemachine", "state", "frame" }), 0.0));
		const bool ready = machines != nullptr && machines->is_array() && !m_defs.empty();
		m_view->set("ready", ready);
		m_view->set("error", m_error);
		m_view->set("no_layout", m_defs.empty() && m_error.empty());
		m_view->set("win", m_window);
		if (!ready) { clear(); return; }
		const std::size_t selected = selectedIndex();
		nlohmann::json rows = nlohmann::json::array();
		for (std::size_t i = 0; i < m_defs.size() && i < machines->size(); ++i)
		{
			const Snapshot& obs = (*machines)[i];
			if (!truthy(findAt(obs, { "valid" }))) { rows.push_back(invalidRow(i, i == selected)); continue; }
			const Derived d = Builder(m_defs[i], obs, frame, m_window).take();
			rows.push_back(machineRow(i, d, obs, i == selected));
			if (i == selected) { pushSelected(m_defs[i], d, obs, frame); }
		}
		m_view->set("machines", std::move(rows));
	}

	void clear()
	{
		m_view->set("machines", nlohmann::json::array());
		m_view->set("has_sel", false);
		m_view->set("nodes", nlohmann::json::array());
		m_view->set("trans", nlohmann::json::array());
	}

	std::size_t selectedIndex() const
	{
		for (std::size_t i = 0; i < m_defs.size(); ++i) { if (m_defs[i].id == m_selected) { return i; } }
		return 0;
	}

	nlohmann::json invalidRow(std::size_t i, bool selected) const
	{
		return { { "id", m_defs[i].id }, { "label", m_defs[i].label }, { "selected", selected }, { "cur", "-" }, { "changes", "0" },
		         { "bad", 0 }, { "segs", nlohmann::json::array() }, { "note", "Outside GameMemory" } };
	}

	nlohmann::json machineRow(std::size_t i, const Derived& d, const Snapshot& obs, bool selected) const
	{
		const std::string cur = d.current >= 0 ? d.nodes[static_cast<std::size_t>(d.current)].name : std::string("?");
		return { { "id", m_defs[i].id }, { "label", m_defs[i].label }, { "selected", selected }, { "cur", cur },
		         { "changes", std::to_string(static_cast<std::uint64_t>(numberOr(findAt(obs, { "changes" }), 0.0))) },
		         { "bad", static_cast<int>(d.undeclared) }, { "segs", segmentRows(d) }, { "note", "" } };
	}

	void pushSelected(const MachineDef& def, const Derived& d, const Snapshot& obs, std::uint64_t frame)
	{
		const float height = std::clamp(120.0f + 24.0f * static_cast<float>(d.nodes.size()), 180.0f, 380.0f);
		const GraphLayout layout = layoutEllipse(d.nodes.size(), kCanvasW, height, kBoxW, kBoxH);
		std::vector<GraphEdge> edges;
		for (const EdgeRow& e : d.edges) { edges.push_back({ e.from, e.to, e.count, lookOf(e, d, frame) }); }
		GraphImage img = renderGraphEdges(layout, edges, kImageScale);
		m_view->setImage("graph", img.width, img.height, std::move(img.rgba));

		nlohmann::json nodes = nlohmann::json::array();
		for (std::size_t i = 0; i < d.nodes.size(); ++i)
		{
			const Node& n = d.nodes[i];
			nodes.push_back({ { "name", n.name }, { "left", dp(layout.boxes[i].cx - kBoxW * 0.5f) }, { "top", dp(layout.boxes[i].cy - kBoxH * 0.5f) },
			                  { "cur", static_cast<int>(i) == d.current }, { "undeclared", !n.declared },
			                  { "entered", n.entered > 0 ? std::to_string(n.entered) : std::string() } });
		}
		m_view->set("has_sel", true);
		m_view->set("sel_label", def.label);
		m_view->set("sel_changes", std::to_string(static_cast<std::uint64_t>(numberOr(findAt(obs, { "changes" }), 0.0))));
		m_view->set("sel_bad", static_cast<int>(d.undeclared));
		m_view->set("canvas_w", dp(kCanvasW));
		m_view->set("canvas_h", dp(height));
		m_view->set("nodes", std::move(nodes));
		m_view->set("trans", transitionRows(d, frame));
		m_view->set("span_text", spanText(d.span));
	}

	static nlohmann::json transitionRows(const Derived& d, std::uint64_t frame)
	{
		std::vector<const EdgeRow*> seen;
		for (const EdgeRow& e : d.edges) { if (e.count > 0) { seen.push_back(&e); } }
		std::stable_sort(seen.begin(), seen.end(), [](const EdgeRow* a, const EdgeRow* b) { return a->count > b->count; });
		nlohmann::json rows = nlohmann::json::array();
		for (std::size_t i = 0; i < seen.size() && i < 12; ++i)
		{
			const EdgeRow& e = *seen[i];
			rows.push_back({ { "path", d.nodes[static_cast<std::size_t>(e.from)].name + " \xE2\x86\x92 " + d.nodes[static_cast<std::size_t>(e.to)].name },
			                 { "count", std::to_string(e.count) }, { "ago", std::to_string(frame >= e.last ? frame - e.last : 0) + " f ago" },
			                 { "bad", !e.declared } });
		}
		return rows;
	}

	ToolView* m_view;
	std::string m_file;
	std::string m_error;
	std::vector<MachineDef> m_defs;
	Snapshot m_snap;
	std::string m_selected;
	int m_window = kDefaultWindow;
};

} // namespace

std::unique_ptr<ToolPage> makeStateMachinePage(const PageContext& ctx)
{
	return std::make_unique<StateMachinePage>(ctx);
}

} // namespace mitiru::tool
