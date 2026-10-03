// perf と mixer。snapshot の perf / audio 節を、RML がそのまま出せる文字と割合に直す。perf は 3D の描画があれば
// パスごとの GPU 時間 (Dx12GpuTimer、2〜3 フレーム遅れ) も出す。
// 書式は HTML 版の data-m-format (f2 / x100 / pct01) と同じ。

#include "Pages.hpp"

#include "../PageUtil.hpp"

#include <algorithm>
#include <cmath>
#include <string>

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

std::string mib(double bytes)
{
	return toFixed(bytes / (1024.0 * 1024.0), 1);
}

/// 読み込みの節 (Engine::StreamingReport)。帯は予算に対する割合で、予算が無ければ VRAM の予算に対する割合
void pushLoads(ToolView& view, const Snapshot* loads)
{
	view.set("has_loads", loads != nullptr);
	if (loads == nullptr) { return; }
	const double resident = numberOr(findAt(*loads, { "residentBytes" }), 0.0);
	const double budget = numberOr(findAt(*loads, { "budgetBytes" }), 0.0);
	const double vram = numberOr(findAt(*loads, { "vramUsageBytes" }), 0.0);
	const double vramBudget = numberOr(findAt(*loads, { "vramBudgetBytes" }), 0.0);
	const double assetLimit = budget > 0.0 ? budget : vramBudget;
	const auto count = [&](const char* key) { return std::to_string(std::llround(numberOr(findAt(*loads, { key }), 0.0))); };
	view.set("loads_pending", count("pending"));
	view.set("loads_mb", mib(resident) + (budget > 0.0 ? " / " + mib(budget) : std::string()) + " MB");
	view.set("loads_w", toFixed(assetLimit > 0.0 ? clamp01(resident / assetLimit) * 100.0 : 0.0, 1) + "%");
	view.set("vram_mb", mib(vram) + " / " + mib(vramBudget) + " MB");
	view.set("vram_w", toFixed(vramBudget > 0.0 ? clamp01(vram / vramBudget) * 100.0 : 0.0, 1) + "%");
	view.set("loads_max_ms", f2(findAt(*loads, { "maxFinishMs" })));
	const double cells = numberOr(findAt(*loads, { "cells" }), 0.0);
	view.set("has_cells", cells > 0.0);
	view.set("cells", count("cellsLoaded") + " / " + count("cellsResident") + " / " + count("cells"));
}

/// パスごとの GPU 時間の行。帯の長さは 1 フレームの GPU 時間全体に対する割合。part は main の内訳。
nlohmann::json gpuRows(const Snapshot* gpu)
{
	nlohmann::json rows = nlohmann::json::array();
	const Snapshot* passes = gpu != nullptr ? findAt(*gpu, { "passes" }) : nullptr;
	const double total = numberOr(gpu != nullptr ? findAt(*gpu, { "totalMs" }) : nullptr, 0.0);
	if (passes == nullptr || !passes->is_array() || !(total > 0.0)) { return rows; }
	for (const Snapshot& p : *passes)
	{
		const double ms = numberOr(findAt(p, { "ms" }), 0.0);
		rows.push_back({ { "name", stringOr(findAt(p, { "name" }), "?") }, { "ms", toFixed(ms, 2) },
		                 { "w", toFixed(clamp01(ms / total) * 100.0, 1) + "%" }, { "part", truthy(findAt(p, { "part" })) } });
	}
	return rows;
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
		const Snapshot* gpu = findAt(m_snap, { "perf", "state", "gpu" });
		m_view->set("has_gpu", gpu != nullptr);
		m_view->set("gpu_total", gpu != nullptr ? f2(findAt(*gpu, { "totalMs" })) : std::string("--"));
		m_view->set("gpu_rows", gpuRows(gpu));
		pushLoads(*m_view, findAt(m_snap, { "perf", "state", "loads" }));
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
