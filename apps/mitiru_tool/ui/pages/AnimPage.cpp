// anim。選んだモデルの姿勢の入力 (AnimPoseParams のレイヤ)、このフレームに通ったアニメイベント、ルートモーションの
// 差分を見る (読み取り専用)。どれも gameMemory の "$type" 付きの値で、イベントと差分は姿勢と同じ持ち主の値を使う
// (例: "hero.pose" と "hero.events"、"hero.rootDelta")。クリップとイベントは番号で出す (名前は DLL が読んだ asset にある)。

#include "Pages.hpp"
#include "TypedValues.hpp"

#include "../PageUtil.hpp"

#include <algorithm>

namespace mitiru::tool
{

namespace
{

nlohmann::json layerRow(const Snapshot& l)
{
	const double weight = numberOr(findAt(l, { "weight" }), 0.0);
	const double mask = numberOr(findAt(l, { "mask" }), -1.0);
	std::string flags = truthy(findAt(l, { "loop" })) ? "loop" : "once";
	if (truthy(findAt(l, { "additive" }))) { flags += " · add"; }
	return { { "clip", jsString(l.value("clip", Snapshot(-1))) },
	         { "mask", mask < 0.0 ? std::string("全身") : "mask " + jsString(l.value("mask", Snapshot(0))) },
	         { "time", toFixed(numberOr(findAt(l, { "time" }), 0.0), 2) + " s" },
	         { "weight", toFixed(weight, 2) }, { "w", toFixed(std::clamp(weight, 0.0, 1.0) * 100.0, 1) + "%" },
	         { "flags", flags } };
}

class AnimPage final : public ToolPage
{
public:
	explicit AnimPage(const PageContext& ctx) : m_view(ctx.view) {}

	void start() override { push(); }

	void onSnapshot(const Snapshot& snap, bool) override
	{
		m_snap = snap;
		push();
	}

	void onAction(std::string_view name, const nlohmann::json& payload) override
	{
		if (name != "anim.select") { return; }
		m_selected = payloadString(payload, "label");
		push();
	}

private:
	void push()
	{
		const Snapshot* state = gameMemoryState(m_snap);
		const auto poses = state != nullptr ? findTyped(*state, "mitiru.AnimPoseParams") : std::vector<TypedValue>{};
		const TypedValue* sel = nullptr;
		for (const TypedValue& p : poses) { if (ownerLabel(p) == m_selected) { sel = &p; } }
		if (sel == nullptr && !poses.empty()) { sel = &poses.front(); }

		nlohmann::json rows = nlohmann::json::array();
		for (const TypedValue& p : poses)
		{
			const Snapshot* layers = findAt(*p.value, { "layers" });
			rows.push_back({ { "label", ownerLabel(p) }, { "layers", layers != nullptr && layers->is_array() ? layers->size() : 0 },
			                 { "selected", &p == sel } });
		}
		m_view->set("ready", state != nullptr);
		m_view->set("poses", std::move(rows));
		m_view->set("no_poses", state != nullptr && poses.empty());
		m_view->set("has_sel", sel != nullptr);
		m_view->set("sel_label", sel != nullptr ? ownerLabel(*sel) : std::string());
		pushLayers(sel);
		const TypedValue* linked = sel != nullptr && ownerIsUnique(poses, sel->owner) ? sel : nullptr;
		pushEvents(state, linked);
		pushRoot(state, linked);
	}

	void pushLayers(const TypedValue* sel)
	{
		nlohmann::json rows = nlohmann::json::array();
		const Snapshot* layers = sel != nullptr ? findAt(*sel->value, { "layers" }) : nullptr;
		if (layers != nullptr && layers->is_array()) { for (const Snapshot& l : *layers) { rows.push_back(layerRow(l)); } }
		m_view->set("in_place", sel != nullptr && truthy(findAt(*sel->value, { "inPlace" })));
		m_view->set("layers", std::move(rows));
	}

	void pushEvents(const Snapshot* state, const TypedValue* sel)
	{
		nlohmann::json rows = nlohmann::json::array();
		if (state != nullptr && sel != nullptr)
		{
			for (const TypedValue& e : findTyped(*state, "mitiru.AnimEventHit"))
			{
				if (e.owner != sel->owner) { continue; }
				rows.push_back({ { "name", jsString(e.value->value("name", Snapshot(0))) },
				                 { "clip", jsString(e.value->value("clip", Snapshot(-1))) },
				                 { "time", toFixed(numberOr(findAt(*e.value, { "time" }), 0.0), 3) + " s" } });
			}
		}
		m_view->set("no_events", rows.empty());
		m_view->set("events", std::move(rows));
	}

	void pushRoot(const Snapshot* state, const TypedValue* sel)
	{
		const auto deltas = state != nullptr ? findTyped(*state, "mitiru.YawXform") : std::vector<TypedValue>{};
		const TypedValue* d = sel != nullptr ? findOwned(deltas, sel->owner) : nullptr;
		m_view->set("has_root", d != nullptr);
		m_view->set("root_t", d != nullptr ? vec3Text(findAt(*d->value, { "t" })) + " m" : std::string());
		m_view->set("root_yaw", d != nullptr ? toFixed(numberOr(findAt(*d->value, { "yawDeg" }), 0.0), 2) + "\xC2\xB0" : std::string());
	}

	ToolView* m_view;
	Snapshot m_snap = Snapshot::object();
	std::string m_selected;
};

} // namespace

std::unique_ptr<ToolPage> makeAnimPage(const PageContext& ctx)
{
	return std::make_unique<AnimPage>(ctx);
}

} // namespace mitiru::tool
