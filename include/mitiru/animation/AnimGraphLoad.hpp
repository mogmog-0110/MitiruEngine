#pragma once

/// @file AnimGraphLoad.hpp
/// @brief `<model>.animgraph.json` を読み、クリップ・マスク・イベントの名前を AnimAsset の番号に引いた AnimGraph にする。
/// @details 名前が引けない、型が合わないなどの誤りは 1 つでもあればグラフを作らず、誤りをすべて返す
///          (半端なグラフで遊び続けない)。書式は docs/ANIM_GRAPH.md。

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <mitiru/data/JsonDiagnostics.hpp>

#include <mitiru/animation/AnimAsset.hpp>
#include <mitiru/animation/AnimBlendSpace.hpp>
#include <mitiru/animation/AnimGraph.hpp>
#include <mitiru/animation/AnimGraphCondition.hpp>
#include <mitiru/asset/AssetPack.hpp>

namespace mitiru::animation
{

struct AnimGraphLoadResult
{
	std::optional<AnimGraph> graph;
	std::vector<std::string> errors;
};

/// @brief "fox.glb" → "fox.animgraph.json" (拡張子を置き換える。拡張子が無ければ足す)。
[[nodiscard]] inline std::string animGraphPath(std::string_view modelPath)
{
	const auto slash = modelPath.find_last_of("/\\");
	const auto dot = modelPath.find_last_of('.');
	const bool hasExt = dot != std::string_view::npos && (slash == std::string_view::npos || dot > slash);
	return std::string(hasExt ? modelPath.substr(0, dot) : modelPath) + ".animgraph.json";
}

/// @brief パスのファイル名から ".animgraph.json" (無ければ最後の拡張子) を除いた名前。グラフの既定の名前
[[nodiscard]] inline std::string animGraphStem(std::string_view path)
{
	const auto slash = path.find_last_of("/\\");
	std::string name(slash == std::string_view::npos ? path : path.substr(slash + 1));
	constexpr std::string_view kExt = ".animgraph.json";
	if (name.size() > kExt.size() && name.compare(name.size() - kExt.size(), kExt.size(), kExt) == 0)
	{
		return name.substr(0, name.size() - kExt.size());
	}
	const auto dot = name.find_last_of('.');
	return dot == std::string::npos ? name : name.substr(0, dot);
}

namespace detail
{

using Json = nlohmann::json;

struct GraphBuild
{
	const AnimAsset& asset;
	AnimGraph graph;
	std::vector<std::string> errors;

	void error(std::string where, std::string what) { errors.push_back(std::move(where) + ": " + std::move(what)); }

	[[nodiscard]] int eventId(const std::string& name)
	{
		const int found = graph.findEventName(name);
		if (found >= 0) { return found; }
		graph.eventNames.push_back(name);
		return static_cast<int>(graph.eventNames.size() - 1);
	}
};

[[nodiscard]] inline std::optional<AnimParamType> paramType(std::string_view t)
{
	if (t == "float") { return AnimParamType::Float; }
	if (t == "int") { return AnimParamType::Int; }
	if (t == "bool") { return AnimParamType::Bool; }
	if (t == "trigger") { return AnimParamType::Trigger; }
	return std::nullopt;
}

inline void readParams(const Json& root, GraphBuild& b)
{
	if (!root.contains("params")) { return; }
	if (!root["params"].is_array()) { return b.error("params", "配列で書く"); }
	for (const auto& p : root["params"])
	{
		const std::string name = p.is_object() ? p.value("name", std::string()) : std::string();
		const auto type = paramType(p.is_object() ? p.value("type", std::string("float")) : std::string());
		if (name.empty() || !type) { b.error("params", "name と type (float / int / bool / trigger) が要る"); continue; }
		if (isReservedCondName(name)) { b.error("params." + name, "条件の語 (finished / time / phase) は名前に使えない"); continue; }
		if (b.graph.findParam(name) >= 0) { b.error("params." + name, "名前が重なっている"); continue; }
		if (b.graph.params.size() >= static_cast<std::size_t>(kAnimGraphMaxParams))
		{
			b.error("params." + name, "param は " + std::to_string(kAnimGraphMaxParams) + " 個まで");
			continue;
		}
		const auto& d = p.contains("default") ? p["default"] : Json();
		const float value = d.is_boolean() ? (d.get<bool>() ? 1.0f : 0.0f) : (d.is_number() ? d.get<float>() : 0.0f);
		b.graph.params.push_back({name, *type, value});
	}
}

[[nodiscard]] inline std::uint32_t paramsKey(const AnimGraph& graph)
{
	std::string all;
	for (const auto& p : graph.params) { all += p.name + ":" + std::to_string(static_cast<int>(p.type)) + ";"; }
	return animNameKey(all);
}

inline void readSyncGroups(const Json& root, GraphBuild& b)
{
	if (!root.contains("syncGroups")) { return; }
	if (!root["syncGroups"].is_object()) { return b.error("syncGroups", "{ \"組\": [\"印のイベント名\", ...] } で書く"); }
	for (const auto& [name, list] : root["syncGroups"].items())
	{
		AnimSyncGroup g{name, {}};
		if (!list.is_array()) { b.error("syncGroups." + name, "イベント名の配列で書く"); continue; }
		for (const auto& m : list)
		{
			const int id = m.is_string() ? b.graph.findEventName(m.get<std::string>()) : -1;
			if (id < 0) { b.error("syncGroups." + name, "クリップのイベントに無い印: " + (m.is_string() ? m.get<std::string>() : m.dump())); }
			else { g.markers.push_back(id); }
		}
		b.graph.syncGroups.push_back(std::move(g));
	}
}

[[nodiscard]] inline std::int16_t paramRef(const Json& j, const char* key, const std::string& where, GraphBuild& b)
{
	if (!j.contains(key)) { return -1; }
	const int p = j[key].is_string() ? b.graph.findParam(j[key].get<std::string>()) : -1;
	if (p < 0) { b.error(where + "." + key, "param が無い: " + j[key].dump()); return -1; }
	const auto t = b.graph.params[static_cast<std::size_t>(p)].type;
	if (t == AnimParamType::Trigger || t == AnimParamType::Bool) { b.error(where + "." + key, "数の param (float / int) を指す"); }
	return static_cast<std::int16_t>(p);
}

[[nodiscard]] inline int clipRef(const Json& j, const std::string& where, GraphBuild& b)
{
	const int c = j.is_string() ? b.asset.findClip(j.get<std::string>()) : -1;
	if (c < 0) { b.error(where, "クリップが無い: " + j.dump()); }
	return c;
}

inline void readBlendPoints(const Json& blend, AnimGraphStateDef& st, bool twoD, const std::string& where, GraphBuild& b)
{
	if (!blend.is_object() || !blend.contains("points") || !blend["points"].is_array() || blend["points"].empty())
	{
		return b.error(where, "points (クリップと位置の配列) が要る");
	}
	for (const auto& p : blend["points"])
	{
		const auto at = p.is_object() && p.contains("at") ? p["at"] : Json();
		const bool ok = twoD ? (at.is_array() && at.size() == 2 && at[0].is_number() && at[1].is_number()) : at.is_number();
		if (!ok) { b.error(where, twoD ? "点の at は [x, y]" : "点の at は数"); continue; }
		const int clip = clipRef(p.value("clip", Json()), where + ".clip", b);
		st.points.push_back({clip, twoD ? at[0].get<float>() : at.get<float>(), twoD ? at[1].get<float>() : 0.0f});
	}
	if (st.points.size() > static_cast<std::size_t>(kMaxBlendSamples))
	{
		b.error(where, "点は " + std::to_string(kMaxBlendSamples) + " 個まで");
	}
	const auto byX = [](const AnimBlendPoint& l, const AnimBlendPoint& r) { return l.x < r.x; };
	if (!twoD) { std::stable_sort(st.points.begin(), st.points.end(), byX); }
	st.paramX = paramRef(blend, twoD ? "x" : "param", where, b);
	if (twoD) { st.paramY = paramRef(blend, "y", where, b); }
}

inline void readMotion(const Json& s, AnimGraphStateDef& st, const std::string& where, GraphBuild& b)
{
	if (s.contains("clip"))
	{
		st.kind = AnimMotionKind::Clip;
		st.points.push_back({clipRef(s["clip"], where + ".clip", b), 0.0f, 0.0f});
	}
	else if (s.contains("blend1d"))
	{
		st.kind = AnimMotionKind::Blend1D;
		readBlendPoints(s["blend1d"], st, false, where + ".blend1d", b);
	}
	else if (s.contains("blend2d"))
	{
		st.kind = AnimMotionKind::Blend2D;
		readBlendPoints(s["blend2d"], st, true, where + ".blend2d", b);
	}
	else { b.error(where, "clip / blend1d / blend2d のどれかが要る"); }
}

inline void readState(const Json& s, AnimGraphLayerDef& layer, const std::string& where, GraphBuild& b)
{
	AnimGraphStateDef st;
	st.name = s.is_object() ? s.value("name", std::string()) : std::string();
	const std::string at = where + "." + (st.name.empty() ? std::string("?") : st.name);
	if (st.name.empty()) { return b.error(where, "状態に name が要る"); }
	for (const auto& other : layer.states)
	{
		if (other.name == st.name) { return b.error(at, "名前が重なっている"); }
	}
	st.key = animNameKey(st.name);
	readMotion(s, st, at, b);
	st.loop = s.value("loop", true);
	st.speed = s.value("speed", 1.0f);
	st.speedParam = paramRef(s, "speedParam", at, b);
	if (s.contains("sync"))
	{
		const std::string g = s["sync"].is_string() ? s["sync"].get<std::string>() : std::string();
		const auto it = std::find_if(b.graph.syncGroups.begin(), b.graph.syncGroups.end(), [&](const AnimSyncGroup& x) { return x.name == g; });
		if (it == b.graph.syncGroups.end()) { b.error(at + ".sync", "syncGroups に無い組: " + s["sync"].dump()); }
		else { st.syncGroup = static_cast<std::int16_t>(it - b.graph.syncGroups.begin()); }
	}
	if (s.contains("enter") && s["enter"].is_string()) { st.enterEvent = b.eventId(s["enter"].get<std::string>()); }
	if (s.contains("exit") && s["exit"].is_string()) { st.exitEvent = b.eventId(s["exit"].get<std::string>()); }
	layer.states.push_back(std::move(st));
}

[[nodiscard]] inline std::optional<AnimFadeCurve> fadeCurve(std::string_view c)
{
	if (c == "linear") { return AnimFadeCurve::Linear; }
	if (c == "smooth") { return AnimFadeCurve::Smooth; }
	if (c == "easeIn") { return AnimFadeCurve::EaseIn; }
	if (c == "easeOut") { return AnimFadeCurve::EaseOut; }
	return std::nullopt;
}

[[nodiscard]] inline std::int16_t stateRef(const Json& j, const AnimGraphLayerDef& layer, const std::string& where, GraphBuild& b)
{
	for (std::size_t i = 0; i < layer.states.size(); ++i)
	{
		if (j.is_string() && layer.states[i].name == j.get<std::string>()) { return static_cast<std::int16_t>(i); }
	}
	b.error(where, "状態が無い: " + j.dump());
	return 0;
}

inline void readConditions(const Json& when, AnimGraphTransitionDef& t, const std::string& where, GraphBuild& b)
{
	const Json list = when.is_string() ? Json::array({when}) : when;
	if (!list.is_array()) { return b.error(where, "条件の文字列か、その配列で書く"); }
	for (const auto& c : list)
	{
		std::string why;
		const auto cond = c.is_string() ? parseAnimCondition(c.get<std::string>(), b.graph, why) : std::nullopt;
		if (cond) { t.conditions.push_back(*cond); }
		else { b.error(where, why.empty() ? "条件は文字列: " + c.dump() : why); }
	}
}

inline void readWindow(const Json& t, AnimGraphTransitionDef& def, const std::string& where, GraphBuild& b)
{
	if (!t.contains("window")) { return; }
	const auto& w = t["window"];
	const bool shaped = w.is_array() && w.size() == 2 && w[0].is_number() && w[1].is_number();
	if (!shaped || w[0].get<float>() < 0.0f || w[1].get<float>() > 1.0f || w[0].get<float>() > w[1].get<float>())
	{
		return b.error(where + ".window", "[始まり, 終わり] (周回の中の位置 0..1、始まり <= 終わり)");
	}
	def.windowStart = w[0].get<float>();
	def.windowEnd = w[1].get<float>();
}

/// from と to の名前と、同じ組の何本目かから作る鍵。並びや priority を変えても同じ遷移を指す
[[nodiscard]] inline std::uint16_t transitionKey(const AnimGraphLayerDef& layer, const AnimGraphTransitionDef& t)
{
	const auto nameOf = [&](std::int16_t s) {
		return (s >= 0 && static_cast<std::size_t>(s) < layer.states.size()) ? layer.states[static_cast<std::size_t>(s)].name : std::string("*");
	};
	int same = 0;
	for (const auto& o : layer.transitions) { same += (o.from == t.from && o.to == t.to) ? 1 : 0; }
	const std::uint32_t h = animNameKey(nameOf(t.from) + ">" + nameOf(t.to) + "#" + std::to_string(same));
	return static_cast<std::uint16_t>((h ^ (h >> 16)) & 0xFFFFu);
}

inline void readTransition(const Json& t, AnimGraphLayerDef& layer, const std::string& where, GraphBuild& b)
{
	if (!t.is_object() || !t.contains("from") || !t.contains("to")) { return b.error(where, "from と to が要る"); }
	AnimGraphTransitionDef def;
	const std::string at = where + "[" + t["from"].dump() + " -> " + t["to"].dump() + "]";
	def.from = (t["from"] == "*") ? std::int16_t{-1} : stateRef(t["from"], layer, at + ".from", b);
	def.to = stateRef(t["to"], layer, at + ".to", b);
	if (t.contains("when")) { readConditions(t["when"], def, at + ".when", b); }
	def.duration = std::max(t.value("duration", 0.0f), 0.0f);
	const auto curve = fadeCurve(t.value("curve", std::string("linear")));
	if (!curve) { b.error(at + ".curve", "linear / smooth / easeIn / easeOut のどれか"); }
	def.curve = curve.value_or(AnimFadeCurve::Linear);
	def.priority = t.value("priority", 0);
	def.interruptible = t.value("interruptible", true);
	def.allowSelf = t.value("allowSelf", false);
	readWindow(t, def, at, b);
	def.key = transitionKey(layer, def);
	layer.transitions.push_back(std::move(def));
}

inline void readLayerHead(const Json& l, AnimGraphLayerDef& layer, const std::string& where, GraphBuild& b)
{
	if (l.contains("mask"))
	{
		layer.mask = l["mask"].is_string() ? b.asset.findMask(l["mask"].get<std::string>()) : -1;
		if (layer.mask < 0) { b.error(where + ".mask", "anim.json の masks に無い: " + l["mask"].dump()); }
	}
	const std::string blend = l.value("blend", std::string("override"));
	if (blend != "override" && blend != "additive") { b.error(where + ".blend", "override か additive"); }
	layer.additive = blend == "additive";
	layer.weight = l.value("weight", 1.0f);
	layer.weightParam = paramRef(l, "weightParam", where, b);
}

inline void readLayer(const Json& l, std::size_t index, GraphBuild& b)
{
	AnimGraphLayerDef layer;
	layer.name = l.is_object() ? l.value("name", "layer" + std::to_string(index)) : std::string();
	const std::string where = "layers." + layer.name;
	if (!l.is_object() || !l.contains("states") || !l["states"].is_array() || l["states"].empty())
	{
		return b.error(where, "states (状態の配列) が要る");
	}
	readLayerHead(l, layer, where, b);
	for (const auto& s : l["states"]) { readState(s, layer, where + ".states", b); }
	if (layer.states.empty()) { return; }
	layer.entry = l.contains("entry") ? stateRef(l["entry"], layer, where + ".entry", b) : std::int16_t{0};
	if (l.contains("transitions") && !l["transitions"].is_array()) { b.error(where + ".transitions", "配列で書く"); }
	else if (l.contains("transitions"))
	{
		for (const auto& t : l["transitions"]) { readTransition(t, layer, where + ".transitions", b); }
	}
	std::stable_sort(layer.transitions.begin(), layer.transitions.end(),
	                 [](const AnimGraphTransitionDef& x, const AnimGraphTransitionDef& y) { return x.priority > y.priority; });
	b.graph.layers.push_back(std::move(layer));
}

inline void readLayers(const Json& root, GraphBuild& b)
{
	const auto& layers = root.contains("layers") ? root["layers"] : Json();
	if (!layers.is_array() || layers.empty() || layers.size() > static_cast<std::size_t>(kAnimGraphMaxLayers))
	{
		return b.error("layers", "レイヤの配列 (1 から " + std::to_string(kAnimGraphMaxLayers) + " 枚) が要る");
	}
	for (std::size_t i = 0; i < layers.size(); ++i) { readLayer(layers[i], i, b); }
}

} // namespace detail

/// @brief グラフの JSON を asset (同じモデルの AnimAsset) の名前で引いて組む。defaultName は "name" が無いときの名前
[[nodiscard]] inline AnimGraphLoadResult parseAnimGraph(std::string_view json, const AnimAsset& asset,
                                                        std::string_view defaultName = "graph")
{
	detail::GraphBuild b{asset, {}, {}};
	std::string syntaxError;
	const auto root = data::parseJsonText(json, "animgraph", syntaxError);
	if (root.is_discarded() || !root.is_object())
	{
		return {std::nullopt, {syntaxError.empty() ? "animgraph: JSON の書き方が間違っているか、一番外側が { } のオブジェクトではありません" : syntaxError}};
	}
	// 欄の型の違い ("loop": "yes" など) は nlohmann が例外で知らせる。読み直しでゲームを落とさないよう誤りにする
	try
	{
		b.graph.name = root.value("name", std::string(defaultName));
		b.graph.key = animNameKey(b.graph.name);
		b.graph.eventNames = asset.eventNames;
		detail::readParams(root, b);
		b.graph.paramsKey = detail::paramsKey(b.graph);
		detail::readSyncGroups(root, b);
		detail::readLayers(root, b);
	}
	catch (const nlohmann::json::exception& e)
	{
		b.error("animgraph", std::string("欄の型が違う: ") + e.what());
	}
	if (!b.errors.empty()) { return {std::nullopt, std::move(b.errors)}; }
	return {std::move(b.graph), {}};
}

/// @brief グラフのファイルを読む (asset pack を mount 済みならそこから、無ければディスクから)
[[nodiscard]] inline AnimGraphLoadResult loadAnimGraphFile(std::string_view graphPath, const AnimAsset& asset)
{
	const auto bytes = vfs::readGlobal(graphPath);
	if (!bytes) { return {std::nullopt, {"animgraph: ファイルを読めない: " + std::string(graphPath)}}; }
	const std::string text(bytes->begin(), bytes->end());
	return parseAnimGraph(text, asset, animGraphStem(graphPath));
}

} // namespace mitiru::animation
