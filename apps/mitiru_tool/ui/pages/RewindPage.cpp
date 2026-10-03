// rewind: ゲーム窓の下に付く ▶/⏸ 付きの横シークバー。再生中はつまみが最新へ追従し、バーを掴むと
// 止まってそのフレームへ戻す。戻すのはゲームではなく host の仕事で、この窓は ScrubControlChannel に
// 「何フレーム前を見せて」と書くだけ。ゲームが送ったカットシーンの区間 (hud.timelineMarker、ABI v50) は
// バーの上に名前つきの帯で出るので、帯を掴めばそのカットシーンの頭へ戻れる。
// キーは、人がバーを掴むか ⏸ を押した時だけこの窓が受け (←/→ で 1 フレーム、Home/End で端)、▶ でゲームへ返す。

#include "Pages.hpp"

#include "../PageUtil.hpp"

#include <algorithm>
#include <cmath>

namespace mitiru::tool
{

namespace
{

// フレーム数: capacity を優先。無ければ *History 配列の長さ (値の履歴を出す章との後方互換)。
int frameCount(const Snapshot& state)
{
	if (const Snapshot* cap = findAt(state, { "capacity" }); cap != nullptr && cap->is_number())
	{
		return static_cast<int>(cap->get<double>());
	}
	for (auto it = state.begin(); it != state.end(); ++it)
	{
		const std::string& k = it.key();
		if (k.size() >= 7 && k.compare(k.size() - 7, 7, "History") == 0 && it->is_array())
		{
			return static_cast<int>(it->size());
		}
	}
	return 0;
}

int intOf(const Snapshot& m, const char* key)
{
	return static_cast<int>(numberOr(findAt(m, { key }), 0.0));
}

class RewindPage final : public ToolPage
{
public:
	explicit RewindPage(const PageContext& ctx) : m_view(ctx.view), m_signals(ctx.signals), m_keyboard(ctx.keyboard) {}

	void start() override { render(); }

	void onSnapshot(const Snapshot& snap, bool) override
	{
		const Snapshot* st = findAt(snap, { "rewind", "state" });
		m_len = (st != nullptr && st->is_object()) ? frameCount(*st) : 0;
		Snapshot next = m_len > 0 ? *st : Snapshot();
		// 節目の並びが変わると添字が別の節目を指すので、重ねていた名前は消す (次の mouseover で出し直す)。
		if (findAt(next, { "markers" }) == nullptr || findAt(m_state, { "markers" }) == nullptr
		    || *findAt(next, { "markers" }) != *findAt(m_state, { "markers" }))
		{
			m_hover = -1;
		}
		m_state = std::move(next);
		if (m_len > 0 && !m_paused) { m_at = m_len - 1; }   // 再生中はライブ端へ追従、停止中は位置を保つ
		m_budget = budgetLine(snap);
		render();
	}

	void onAction(std::string_view name, const nlohmann::json& payload) override
	{
		if (name == "tt.playpause") { playPause(); }
		else if (name == "tt.hover")
		{
			m_hover = payload.contains("i") && payload["i"].is_number() ? payload["i"].get<int>() : -1;
			render();
		}
	}

	void onPointer(std::string_view, const PointerEvent& ev) override
	{
		if (m_len < 1) { return; }
		if (ev.kind == PointerEvent::Kind::Up) { m_dragging = false; return; }   // 離しても停止のまま
		if (ev.kind == PointerEvent::Kind::Move && !m_dragging) { return; }
		if (ev.kind == PointerEvent::Kind::Down)
		{
			m_dragging = true;
			m_paused = true;
			takeKeyboard();
		}
		m_at = static_cast<int>(std::lround(ev.x / std::max(ev.width, 1.0f) * static_cast<float>(m_len - 1)));
		render();
		sendScrub();
	}

	// つまみを動かすキーはバーを掴んだのと同じく止める。押したままの自動反復は 1 回ごとに 1 フレーム動く。
	void onKey(const KeyEvent& ev) override
	{
		if (m_len < 1) { return; }
		switch (ev.key)
		{
		case KeyEvent::Key::Left:  --m_at; break;
		case KeyEvent::Key::Right: ++m_at; break;
		case KeyEvent::Key::Home:  m_at = 0; break;
		case KeyEvent::Key::End:   m_at = m_len - 1; break;
		default: return;
		}
		m_paused = true;
		render();
		sendScrub();
	}

	[[nodiscard]] std::vector<std::string> pointerTargets() const override { return { "bar" }; }

private:
	void playPause()
	{
		if (m_len < 1) { return; }
		m_at = m_len - 1;
		if (m_paused)
		{
			m_paused = false;
			render();
			if (m_signals != nullptr) { m_signals->resume(); }   // ▶ そこから再生
			if (m_keyboard != nullptr) { m_keyboard->giveBack(); }
			return;
		}
		m_paused = true;   // ⏸ いまで止める
		takeKeyboard();
		render();
		sendScrub();
	}

	void takeKeyboard()
	{
		if (m_keyboard != nullptr) { m_keyboard->take(); }
	}

	// つまみの位置 (m_at、0 = 最古) を offsetFromNewest (0 = 最新) に直して頼む。
	void sendScrub()
	{
		if (m_signals != nullptr && m_len >= 1) { m_signals->scrub((m_len - 1) - m_at); }
	}

	nlohmann::json marks() const
	{
		nlohmann::json out = nlohmann::json::array();
		const Snapshot* ms = findAt(m_state, { "markers" });
		if (m_len < 2 || ms == nullptr || !ms->is_array()) { return out; }
		for (std::size_t i = 0; i < ms->size(); ++i)
		{
			const Snapshot& m = (*ms)[i];
			const double pos = static_cast<double>(m_len - 1 - intOf(m, "o")) / (m_len - 1) * 100.0;
			if (pos < 0.0 || pos > 100.0) { continue; }
			const std::string label = stringOr(findAt(m, { "label" }), "");
			out.push_back({ { "i", i }, { "left", pos }, { "k", intOf(m, "k") }, { "labeled", !label.empty() } });
		}
		return out;
	}

	// カットシーンの区間。o0 / o1 は始まりと終わりが最新から何フレーム前か (終わりがまだ先なら負)
	nlohmann::json spans() const
	{
		nlohmann::json out = nlohmann::json::array();
		const Snapshot* ss = findAt(m_state, { "spans" });
		if (m_len < 2 || ss == nullptr || !ss->is_array()) { return out; }
		const double last = static_cast<double>(m_len - 1);
		for (const Snapshot& s : *ss)
		{
			const double a = std::clamp((last - intOf(s, "o0")) / last * 100.0, 0.0, 100.0);
			const double b = std::clamp((last - intOf(s, "o1")) / last * 100.0, 0.0, 100.0);
			if (b <= a) { continue; }
			out.push_back({ { "left", a }, { "width", b - a }, { "label", stringOr(findAt(s, { "label" }), "") } });
		}
		return out;
	}

	// 節目に重ねた時だけ出す 1 行 (HTML 版の title = 「RUN 12 フレーム前 (値 3)」)。
	std::string hoverText() const
	{
		const Snapshot* ms = findAt(m_state, { "markers" });
		if (m_hover < 0 || ms == nullptr || !ms->is_array() || m_hover >= static_cast<int>(ms->size())) { return {}; }
		const Snapshot& m = (*ms)[static_cast<std::size_t>(m_hover)];
		const std::string label = stringOr(findAt(m, { "label" }), "");
		const Snapshot* v = findAt(m, { "v" });
		return (label.empty() ? "" : label + " ") + std::to_string(intOf(m, "o")) + " フレーム前 (値 "
		       + (v != nullptr ? jsString(*v) : std::string("undefined")) + ")";
	}

	// P11: 今の 1 体あたりの重さから「予算 16.6ms ならあと何体」を見積もる (perf と sceneView の組み合わせ)。
	static std::string budgetLine(const Snapshot& snap)
	{
		constexpr double kBudgetMs = 16.6;
		const Snapshot* frameMs = findAt(snap, { "perf", "state", "frameMs" });
		const Snapshot* objects = findAt(snap, { "sceneView", "state", "objects" });
		const std::size_t count = (objects != nullptr && objects->is_array()) ? objects->size() : 0;
		if (frameMs == nullptr || !frameMs->is_number() || count == 0) { return {}; }
		const double ms = frameMs->get<double>();
		const double perEntity = ms / static_cast<double>(count);
		const double extra = perEntity > 0.0 ? std::max(0.0, std::floor((kBudgetMs - ms) / perEntity)) : 0.0;
		return "今 " + std::to_string(count) + " 体で " + jsString(Snapshot(std::round(ms * 10.0) / 10.0))
		       + " ms。予算 16.6ms なら あと約 " + std::to_string(static_cast<long long>(extra)) + " 体";
	}

	void render()
	{
		if (m_len > 0) { m_at = std::clamp(m_at, 0, m_len - 1); }
		m_view->set("ok", m_len > 1);
		m_view->set("pct", m_len > 1 ? static_cast<double>(m_at) / (m_len - 1) * 100.0 : 0.0);
		m_view->set("paused", m_paused);
		m_view->set("marks", marks());
		m_view->set("spans", spans());
		m_view->set("hover", hoverText());
		m_view->set("budget", m_budget);
	}

	ToolView* m_view;
	ToolSignals* m_signals;
	ToolKeyboard* m_keyboard;
	Snapshot m_state;
	std::string m_budget;
	int m_len = 0;
	int m_at = 0;
	int m_hover = -1;
	bool m_paused = false;
	bool m_dragging = false;
};

} // namespace

std::unique_ptr<ToolPage> makeRewindPage(const PageContext& ctx)
{
	return std::make_unique<RewindPage>(ctx);
}

} // namespace mitiru::tool
