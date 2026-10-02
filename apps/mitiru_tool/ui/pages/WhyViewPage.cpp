// why_view (ADR 0035 O6): field の名前を打ち、ゲームの /api/ai/why?field=<dotted path> を問い合わせて
// 最後に書いた phase と値の推移を見る一問一答の窓。読むだけで、ゲームの状態は変えない。
// 分岐中 (残す / 捨てるを選ぶ前) であることを scene_view と同じ帯で見せる。

#include "Pages.hpp"

#include "../PageUtil.hpp"

namespace mitiru::tool
{

namespace
{

class WhyViewPage final : public ToolPage
{
public:
	explicit WhyViewPage(const PageContext& ctx) : m_view(ctx.view), m_http(ctx.http) {}

	void start() override { publish(); }

	void onAction(std::string_view name, const nlohmann::json& payload) override
	{
		if (name == "why.field")
		{
			m_field = payloadString(payload, "v");
			const auto enter = payload.find("enter");
			if (enter != payload.end() && enter->is_boolean() && enter->get<bool>()) { query(); }
		}
		else if (name == "why.query") { query(); }
	}

	void onHttp(const HttpResponse& res) override
	{
		if (res.tag == "why.state")
		{
			m_statePoll.done();
			const Snapshot body = parseBody(res.body);
			const bool ok = res.status >= 200 && res.status < 300 && !body.is_discarded();
			m_banner = ok ? branchBannerText(findAt(body, { "branch" })) : std::string();
			publish();
			return;
		}
		if (res.tag == "why.query") { onAnswer(res); }
	}

	void tick(double now) override
	{
		if (m_http != nullptr && m_statePoll.due(now)) { m_http->send({ "why.state", "GET", "/api/ai/state", "" }); }
	}

private:
	static std::string trim(const std::string& s)
	{
		const auto b = s.find_first_not_of(" \t\r\n");
		if (b == std::string::npos) { return {}; }
		return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
	}

	void query()
	{
		const std::string name = trim(m_field);
		if (name.empty() || m_http == nullptr) { return; }
		m_error.clear();
		m_blame = "問い合わせ中…";
		publish();
		m_http->send({ "why.query", "GET", "/api/ai/why?field=" + urlEncode(name), "" });
	}

	void showFailure(std::string message)
	{
		m_error = std::move(message);
		m_blame = "-";
		m_history = nlohmann::json::array({ { { "k", "-" }, { "v", "履歴なし" }, { "newest", false }, { "muted", true } } });
		publish();
	}

	void onAnswer(const HttpResponse& res)
	{
		const Snapshot body = parseBody(res.body);
		if (res.status == 0) { m_error = connectionFailedText(m_http->port()); publish(); return; }
		if (!responseOk(res.status, body))
		{
			const std::string err = stringOr(findAt(body, { "error" }), "");
			showFailure(err.empty() ? "HTTP " + std::to_string(res.status) : err);
			return;
		}
		const std::string frame = " (frame " + jsString(body.value("frame", Snapshot())) + ")";
		m_unsupported = !truthy(findAt(body, { "blameSupported" }));
		// game が mitiru_why_blame_at を export していない時は、フレームと値の推移だけを出す。
		m_blame = m_unsupported ? "未対応 (game が mitiru_why_blame_at を export していません) " + frame
		                        : stringOr(findAt(body, { "blame" }), "") + " " + frame;
		const Snapshot* ever = findAt(body, { "everWrittenBy" });
		m_everWrote.clear();
		if (ever != nullptr && ever->is_array())
		{
			for (const Snapshot& w : *ever) { m_everWrote += (m_everWrote.empty() ? "" : ", ") + jsString(w); }
		}
		// 「一度も書かれていない」は「なぜ変わらないのか」への直接の答え。
		if (m_everWrote.empty()) { m_everWrote = "(このフレームまで一度も書かれていない)"; }
		m_history = historyRows(findAt(body, { "history" }));
		publish();
	}

	static nlohmann::json historyRows(const Snapshot* entries)
	{
		if (entries == nullptr || !entries->is_array() || entries->empty())
		{
			return nlohmann::json::array({ { { "k", "-" }, { "v", "履歴なし" }, { "newest", false }, { "muted", true } } });
		}
		nlohmann::json rows = nlohmann::json::array();
		for (const Snapshot& e : *entries)
		{
			const int ago = static_cast<int>(numberOr(findAt(e, { "framesAgo" }), 0.0));
			const Snapshot* value = findAt(e, { "value" });
			rows.push_back({ { "k", ago == 0 ? std::string("now") : std::to_string(ago) + " フレーム前" },
			                 { "v", value != nullptr ? value->dump() : std::string("undefined") },
			                 { "newest", ago == 0 }, { "muted", false } });
		}
		return rows;
	}

	void publish()
	{
		m_view->set("error", m_error);
		m_view->set("blame", m_blame);
		m_view->set("unsupported", m_unsupported);
		m_view->set("ever_wrote", m_everWrote);
		m_view->set("history", m_history);
		m_view->set("banner", m_banner);
	}

	ToolView* m_view;
	ToolHttp* m_http;
	PollTimer m_statePoll;
	std::string m_field;
	std::string m_error;
	std::string m_blame = "field を入力して問い合わせてください";
	std::string m_everWrote = "-";
	std::string m_banner;
	nlohmann::json m_history = nlohmann::json::array();
	bool m_unsupported = false;
};

} // namespace

std::unique_ptr<ToolPage> makeWhyViewPage(const PageContext& ctx)
{
	return std::make_unique<WhyViewPage>(ctx);
}

} // namespace mitiru::tool
