// perf と mixer。snapshot の perf / audio 節を、RML がそのまま出せる文字と割合に直す。
// 書式は HTML 版の data-m-format (f2 / x100 / pct01) と同じ。

#include "Pages.hpp"

#include "../PageUtil.hpp"

#include <algorithm>
#include <cmath>

namespace mitiru::tool
{

namespace
{

double clamp01(double v)
{
	return std::clamp(v, 0.0, 1.0);
}

std::string x100(const Snapshot* v)
{
	return std::to_string(static_cast<long long>(std::lround(clamp01(numberOr(v, 0.0)) * 100.0)));
}

std::string pct01(const Snapshot* v)
{
	const double rounded = std::round(clamp01(numberOr(v, 0.0)) * 10000.0) / 100.0;
	return jsString(Snapshot(rounded)) + "%";
}

std::string f2(const Snapshot* v)
{
	return toFixed(numberOr(v, 0.0), 2);
}

class PerfPage final : public ToolPage
{
public:
	explicit PerfPage(const PageContext& ctx) : m_view(ctx.view) {}

	void start() override { push(); }

	void onSnapshot(const Snapshot& snap, bool) override
	{
		m_snap = snap;
		// 折れ線は snapshot を受けるたびに 1 点足す (fps が同じ値のままでも時間は進む)。
		if (const Snapshot* fps = findAt(m_snap, { "perf", "state", "fps" }); fps != nullptr && fps->is_number()) { ++m_sample; }
		push();
	}

private:
	void push()
	{
		const Snapshot* state = findAt(m_snap, { "perf", "state" });
		const Snapshot* fps = findAt(m_snap, { "perf", "state", "fps" });
		const Snapshot* ms = findAt(m_snap, { "perf", "state", "frameMs" });
		const Snapshot* dropped = findAt(m_snap, { "perf", "state", "droppedSteps" });
		m_view->set("ready", truthy(state));
		m_view->set("fps", fps != nullptr ? jsString(*fps) : std::string("--"));
		m_view->set("fps_value", numberOr(fps, 0.0));
		m_view->set("sample", m_sample);
		m_view->set("frame_ms", ms != nullptr ? f2(ms) : std::string("--"));
		m_view->set("slow", truthy(findAt(m_snap, { "perf", "state", "slowMotion" })));
		m_view->set("dropped", dropped != nullptr ? jsString(*dropped) : std::string("0"));
	}

	ToolView* m_view;
	Snapshot m_snap = Snapshot::object();
	long long m_sample = 0;
};

nlohmann::json channelRow(const Snapshot& ch)
{
	const std::string kind = stringOr(findAt(ch, { "kind" }), "");
	const char* tag = kind == "music" ? "M" : (kind == "voice" ? "V" : "S");
	return { { "h", pct01(findAt(ch, { "level" })) }, { "music", kind == "music" }, { "tag", tag } };
}

nlohmann::json voiceRow(const Snapshot& ch)
{
	const Snapshot* id = findAt(ch, { "id" });
	const Snapshot* asset = findAt(ch, { "asset" });
	const Snapshot* left = findAt(ch, { "remainingSec" });
	return {
		{ "id", id != nullptr ? jsString(*id) : std::string() }, { "has_id", truthy(id) },
		{ "asset", asset != nullptr ? jsString(*asset) : std::string() }, { "has_asset", truthy(asset) },
		{ "gain", x100(findAt(ch, { "level" })) + "%" },
		{ "pan", "pan " + f2(findAt(ch, { "pan" })) },
		{ "left", f2(left) + "s left" }, { "has_left", left != nullptr && left->is_number() && left->get<double>() >= 0.0 },
	};
}

class MixerPage final : public ToolPage
{
public:
	explicit MixerPage(const PageContext& ctx) : m_view(ctx.view) {}

	void start() override { push(); }

	void onSnapshot(const Snapshot& snap, bool) override
	{
		m_snap = snap;
		push();
	}

private:
	void push()
	{
		const Snapshot* state = findAt(m_snap, { "audio", "state" });
		const Snapshot* channels = findAt(m_snap, { "audio", "state", "channels" });
		const Snapshot* engineName = findAt(m_snap, { "audio", "state", "engine" });
		nlohmann::json chs = nlohmann::json::array();
		nlohmann::json voices = nlohmann::json::array();
		if (channels != nullptr && channels->is_array())
		{
			for (const Snapshot& ch : *channels)
			{
				chs.push_back(channelRow(ch));
				if (stringOr(findAt(ch, { "kind" }), "") == "voice") { voices.push_back(voiceRow(ch)); }
			}
		}
		const bool ready = truthy(state);
		m_view->set("ready", ready);
		m_view->set("master_h", pct01(findAt(m_snap, { "audio", "state", "masterVolume" })));
		const Snapshot* master = findAt(m_snap, { "audio", "state", "masterVolume" });
		m_view->set("master_pct", master != nullptr ? x100(master) : std::string("--"));
		m_view->set("engine", truthy(engineName) ? jsString(*engineName) : std::string());
		m_view->set("silent", ready && chs.empty());
		m_view->set("no_voice", ready && !truthy(findAt(m_snap, { "audio", "state", "voiceCount" })));
		m_view->set("channels", std::move(chs));
		m_view->set("voices", std::move(voices));
	}

	ToolView* m_view;
	Snapshot m_snap = Snapshot::object();
};

} // namespace

std::unique_ptr<ToolPage> makePerfPage(const PageContext& ctx)
{
	return std::make_unique<PerfPage>(ctx);
}

std::unique_ptr<ToolPage> makeMixerPage(const PageContext& ctx)
{
	return std::make_unique<MixerPage>(ctx);
}

} // namespace mitiru::tool
