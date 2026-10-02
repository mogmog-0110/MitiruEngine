// side_state。GameMemory の外に持つ状態の窓口 (ADR 0054) を見る (読み取り専用)。窓口ごとの形の番号・1 フレームの
// bytes・hash と、記録のリングの埋まり具合。replay の照合中 (mitiru_host --replay / --replay-test) は、窓口ごとに
// hash が記録と食い違ったフレームを帯の上の印で出す。値は snapshot の sideState 節 (observe/SideStateInspect.hpp)。

#include "Pages.hpp"

#include "../PageUtil.hpp"

#include <algorithm>
#include <map>

namespace mitiru::tool
{

namespace
{

std::string bytesText(double n)
{
	if (n < 1024.0) { return toFixed(n, 0) + " B"; }
	if (n < 1024.0 * 1024.0) { return toFixed(n / 1024.0, 1) + " KB"; }
	return toFixed(n / (1024.0 * 1024.0), 1) + " MB";
}

nlohmann::json channelRow(const Snapshot& c)
{
	const Snapshot* bytes = findAt(c, { "bytes" });
	return { { "name", stringOr(findAt(c, { "name" }), "?") },
	         { "version", "v" + jsString(c.value("version", Snapshot(0))) },
	         { "bytes", bytes != nullptr ? bytesText(numberOr(bytes, 0.0)) : std::string("\xE2\x80\x94") },
	         { "hash", stringOr(findAt(c, { "hash" }), "") },
	         { "covers", truthy(findAt(c, { "coversScene" })) } };
}

/// 窓口ごとの帯。印の位置は照合した最初のフレームから最後のフレームまでを 0..100% に置く。
nlohmann::json replayStrips(const Snapshot& replay, const std::vector<std::string>& names)
{
	const double first = numberOr(findAt(replay, { "firstFrame" }), 0.0);
	const double span = std::max(1.0, numberOr(findAt(replay, { "lastFrame" }), 0.0) - first);
	std::map<std::string, nlohmann::json> marks;
	std::map<std::string, double> firstHit;
	for (const std::string& n : names) { marks[n] = nlohmann::json::array(); }
	if (const Snapshot* list = findAt(replay, { "marks" }); list != nullptr && list->is_array())
	{
		for (const Snapshot& m : *list)
		{
			const double frame = numberOr(findAt(m, { "frame" }), first);
			const Snapshot* chs = findAt(m, { "channels" });
			if (chs == nullptr || !chs->is_array()) { continue; }
			for (const Snapshot& ch : *chs)
			{
				const std::string name = ch.is_string() ? ch.get<std::string>() : jsString(ch);
				auto& row = marks.try_emplace(name, nlohmann::json::array()).first->second;
				row.push_back({ { "left", toFixed((frame - first) / span * 100.0, 2) + "%" } });
				firstHit.try_emplace(name, frame);
			}
		}
	}
	nlohmann::json strips = nlohmann::json::array();
	for (auto& [name, row] : marks)
	{
		const auto hit = firstHit.find(name);
		strips.push_back({ { "name", name }, { "count", row.size() },
		                   { "first", hit != firstHit.end() ? "frame " + toFixed(hit->second, 0) : std::string() },
		                   { "marks", std::move(row) } });
	}
	return strips;
}

class SideStatePage final : public ToolPage
{
public:
	explicit SideStatePage(const PageContext& ctx) : m_view(ctx.view) {}

	void start() override { push(); }

	void onSnapshot(const Snapshot& snap, bool) override
	{
		m_snap = snap;
		push();
	}

private:
	void push()
	{
		const Snapshot* state = findAt(m_snap, { "sideState", "state" });
		nlohmann::json rows = nlohmann::json::array();
		std::vector<std::string> names;
		if (const Snapshot* chs = state != nullptr ? findAt(*state, { "channels" }) : nullptr; chs != nullptr && chs->is_array())
		{
			for (const Snapshot& c : *chs)
			{
				rows.push_back(channelRow(c));
				names.push_back(rows.back()["name"].get<std::string>());
			}
		}
		m_view->set("ready", state != nullptr);
		m_view->set("no_channels", state != nullptr && rows.empty());
		m_view->set("channels", std::move(rows));
		pushRing(state);
		const Snapshot* replay = state != nullptr ? findAt(*state, { "replay" }) : nullptr;
		m_view->set("has_replay", replay != nullptr);
		if (replay == nullptr) { return; }
		m_view->set("replay_compared", jsString(replay->value("compared", Snapshot(0))));
		m_view->set("replay_range", "frame " + jsString(replay->value("firstFrame", Snapshot(0))) + " .. "
		                            + jsString(replay->value("lastFrame", Snapshot(0))));
		m_view->set("replay_dropped", numberOr(findAt(*replay, { "dropped" }), 0.0) > 0.0);
		m_view->set("strips", replayStrips(*replay, names));
	}

	void pushRing(const Snapshot* state)
	{
		const Snapshot* ring = state != nullptr ? findAt(*state, { "ring" }) : nullptr;
		const auto num = [ring](const char* key) { return ring != nullptr ? numberOr(findAt(*ring, { key }), 0.0) : 0.0; };
		m_view->set("ring_frames", toFixed(num("frames"), 0) + " / " + toFixed(num("capacity"), 0));
		m_view->set("ring_used", bytesText(num("usedBytes")));
		m_view->set("ring_per_frame", bytesText(num("bytesPerFrame")));
		m_view->set("ring_alloc", bytesText(num("allocatedBytes")));
	}

	ToolView* m_view;
	Snapshot m_snap = Snapshot::object();
};

} // namespace

std::unique_ptr<ToolPage> makeSideStatePage(const PageContext& ctx)
{
	return std::make_unique<SideStatePage>(ctx);
}

} // namespace mitiru::tool
