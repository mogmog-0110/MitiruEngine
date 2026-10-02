// frame_view (P10): 1 フレームの解剖図。ゲームの /api/frame/anatomy?frame=<何フレーム前か> を読み、
// 入力 → 書かれた field (blame) → 描画コマンド → 音を縦に並べる。最新 (0 フレーム前) を見ている間だけ
// 0.2 秒ごとに取り直すので、F7 のステップ実行と並べると 1 歩ごとに図が変わる。読むだけの窓。

#include "Pages.hpp"

#include "../PageUtil.hpp"

#include <cmath>

namespace mitiru::tool
{

namespace
{

std::string fmtValue(const Snapshot* v)
{
	if (v == nullptr || v->is_null()) { return "null"; }
	if (v->is_number())
	{
		const double d = v->get<double>();
		return std::floor(d) == d ? jsString(*v) : toFixed(d, 3);
	}
	return v->dump();
}

std::string fixed(const Snapshot& o, const char* key, int digits)
{
	return toFixed(numberOr(findAt(o, { key }), 0.0), digits);
}

nlohmann::json inputChips(const Snapshot& inp)
{
	nlohmann::json chips = nlohmann::json::array();
	const Snapshot* keys = findAt(inp, { "keysDown" });
	if (keys != nullptr && keys->is_array() && !keys->empty()) { chips.push_back("keys: " + jsString(*keys)); }
	if (truthy(findAt(inp, { "gamepadConnected" })))
	{
		std::string axes;
		const Snapshot* a = findAt(inp, { "gamepadAxes" });
		if (a != nullptr && a->is_array())
		{
			for (const Snapshot& x : *a) { axes += (axes.empty() ? "" : ",") + toFixed(numberOr(&x, 0.0), 2); }
		}
		chips.push_back("pad axes: " + axes);
	}
	if (const Snapshot* text = findAt(inp, { "textInput" }); truthy(text)) { chips.push_back("text: \"" + jsString(*text) + "\""); }
	chips.push_back("mouse: (" + fixed(inp, "mouseX", 0) + ", " + fixed(inp, "mouseY", 0) + ")");
	chips.push_back("dt: " + fixed(inp, "effectiveDt", 4) + (truthy(findAt(inp, { "paused" })) ? " (paused)" : ""));
	return chips;
}

nlohmann::json writeRows(const Snapshot* writes)
{
	nlohmann::json rows = nlohmann::json::array();
	if (writes == nullptr || !writes->is_array()) { return rows; }
	for (const Snapshot& w : *writes)
	{
		const bool supported = truthy(findAt(w, { "blameSupported" }));
		const Snapshot* blame = findAt(w, { "blame" });
		rows.push_back({ { "path", stringOr(findAt(w, { "path" }), "") },
		                 { "vals", fmtValue(findAt(w, { "from" })) + " \xE2\x86\x92 " + fmtValue(findAt(w, { "to" })) },
		                 { "blame", supported ? (blame != nullptr ? jsString(*blame) : std::string()) : std::string("未対応 (mitiru_why_blame_at 未 export)") },
		                 { "unsupported", !supported } });
	}
	return rows;
}

nlohmann::json drawRows(const Snapshot* draws)
{
	nlohmann::json rows = nlohmann::json::array();
	if (draws == nullptr || !draws->is_array()) { return rows; }
	for (const Snapshot& d : *draws)
	{
		rows.push_back({ { "call", stringOr(findAt(d, { "call" }), "") }, { "x", fixed(d, "x", 0) }, { "y", fixed(d, "y", 0) },
		                 { "w", fixed(d, "w", 0) }, { "h", fixed(d, "h", 0) }, { "text", stringOr(findAt(d, { "text" }), "") } });
	}
	return rows;
}

class FrameViewPage final : public ToolPage
{
public:
	explicit FrameViewPage(const PageContext& ctx) : m_view(ctx.view), m_http(ctx.http) { m_poll.interval = 0.2; }

	void start() override { publish(); }

	void onAction(std::string_view name, const nlohmann::json&) override
	{
		if (name == "frame.prev")
		{
			if (prevDisabled()) { return; }
			++m_offset;
			m_polling = false;
		}
		else if (name == "frame.next")
		{
			if (m_offset <= 0) { return; }
			--m_offset;
			m_polling = (m_offset == 0);
		}
		else { return; }
		m_queryNow = true;
	}

	void tick(double now) override
	{
		if (m_http == nullptr) { return; }
		if (m_queryNow && !m_poll.inFlight)
		{
			m_queryNow = false;
			m_poll.inFlight = true;
			m_poll.next = now + m_poll.interval;
			send();
			return;
		}
		if (m_polling && m_poll.due(now)) { send(); }
	}

	void onHttp(const HttpResponse& res) override
	{
		if (res.tag != "frame.anatomy") { return; }
		m_poll.done();
		// 前 / 次を押す前に送った問い合わせの応答は、今の「何フレーム前」の表示に載せない。
		if (m_sentOffset != m_offset)
		{
			m_queryNow = true;
			return;
		}
		m_error.clear();
		const Snapshot body = parseBody(res.body);
		if (res.status == 0) { m_error = connectionFailedText(m_http->port()); publish(); return; }
		if (!responseOk(res.status, body))
		{
			const std::string err = stringOr(findAt(body, { "error" }), "");
			m_error = err.empty() ? "HTTP " + std::to_string(res.status) : err;
			publish();
			return;
		}
		apply(body);
		publish();
	}

private:
	[[nodiscard]] bool prevDisabled() const noexcept { return m_ring > 0 && m_offset >= m_ring - 2; }

	void send()
	{
		m_sentOffset = m_offset;
		m_http->send({ "frame.anatomy", "GET", "/api/frame/anatomy?frame=" + std::to_string(m_offset), "" });
	}

	void apply(const Snapshot& j)
	{
		m_navReady = true;
		m_frame = j.contains("frame") ? jsString(j["frame"]) : std::string("-");
		m_ring = static_cast<int>(numberOr(findAt(j, { "ringSize" }), 0.0));
		const Snapshot* inp = findAt(j, { "input" });
		m_input = truthy(inp) ? inputChips(*inp) : nlohmann::json::array();
		m_hasInput = truthy(inp);
		m_writes = writeRows(findAt(j, { "writes" }));
		m_draws = drawRows(findAt(j, { "draw" }));
		m_sound = nlohmann::json::array();
		if (const Snapshot* events = findAt(j, { "sound", "events" }); events != nullptr && events->is_array())
		{
			for (const Snapshot& e : *events) { m_sound.push_back(e.dump()); }
		}
		const Snapshot* total = findAt(j, { "sound", "total" });
		m_soundTotal = total != nullptr ? jsString(*total) : std::string("undefined");
		m_loaded = true;
	}

	void publish()
	{
		m_view->set("frameno", m_navReady
			? "frame " + m_frame + "  (" + std::to_string(m_offset) + " フレーム前 / ring " + std::to_string(m_ring) + ")"
			: std::string("-"));
		m_view->set("prev_disabled", m_navReady && prevDisabled());
		m_view->set("next_disabled", m_navReady && m_offset <= 0);
		m_view->set("error", m_error);
		m_view->set("loaded", m_loaded);
		m_view->set("has_input", m_hasInput);
		m_view->set("input", m_input);
		m_view->set("writes", m_writes);
		m_view->set("draws", m_draws);
		m_view->set("sound", m_sound);
		m_view->set("sound_total", m_soundTotal);
	}

	ToolView* m_view;
	ToolHttp* m_http;
	PollTimer m_poll;
	int m_offset = 0;   ///< 0 = 直近のフレーム (framesAgo)
	int m_ring = 0;
	int m_sentOffset = 0;
	bool m_polling = true;   ///< 最新を見ている間だけ自動で取り直す (履歴を見ている間は動かさない)
	bool m_queryNow = false;
	bool m_navReady = false;
	bool m_loaded = false;
	bool m_hasInput = false;
	std::string m_frame = "-";
	std::string m_error;
	std::string m_soundTotal;
	nlohmann::json m_input = nlohmann::json::array();
	nlohmann::json m_writes = nlohmann::json::array();
	nlohmann::json m_draws = nlohmann::json::array();
	nlohmann::json m_sound = nlohmann::json::array();
};

} // namespace

std::unique_ptr<ToolPage> makeFrameViewPage(const PageContext& ctx)
{
	return std::make_unique<FrameViewPage>(ctx);
}

} // namespace mitiru::tool
