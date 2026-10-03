#pragma once

/// @file AnimGraph.hpp
/// @brief データで書くアニメの状態機械。定義 (AnimGraph) は `<model>.animgraph.json` から組む読み取り専用の表で、
///        実行時の状態 (AnimGraphState) は GameMemory に置ける固定長の POD。
/// @details 進め方は AnimGraphStep.hpp の stepAnimGraph、姿勢の入力にするのは animGraphPose。どちらも
///          定義と状態だけで決まり、使う演算は加減乗除と floor だけなので、巻き戻しとリプレイで同じ姿勢が出る。
///          書式は docs/ANIM_GRAPH.md。

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <mitiru/animation/AnimEvents.hpp>
#include <mitiru/animation/AnimMath.hpp>
#include <mitiru/animation/AnimNameKey.hpp>

namespace mitiru::animation
{

inline constexpr int kAnimGraphMaxParams = 16;
inline constexpr int kAnimGraphMaxLayers = 4;
inline constexpr int kAnimGraphMaxEvents = 8;
/// 1 つの状態が姿勢へ出すクリップの数。ブレンドスペースは重みの大きい順にこれだけ残す
inline constexpr int kAnimGraphMaxStateSamples = 4;
/// 遷移の途中で割り込んだとき、止めて残す姿勢のクリップの数
inline constexpr int kAnimGraphMaxFrozen = 4;
/// 1 回の step で数える周回の上限 (巨大な dt で止まらないように)
inline constexpr int kAnimGraphMaxLoopsPerStep = 8;

enum class AnimParamType : std::uint8_t
{
	Float,
	Int,
	Bool,
	Trigger,   ///< 立てた step の遷移が使うと消え、使われなくても step の終わりに消える
};

struct AnimGraphParam
{
	std::string name;
	AnimParamType type = AnimParamType::Float;
	float defaultValue = 0.0f;
};

enum class AnimCondOp : std::uint8_t
{
	Greater,
	GreaterEq,
	Less,
	LessEq,
	Equal,
	NotEqual,
	IsTrue,
	IsFalse,
	Trigger,
	Finished,     ///< ループしない状態が終わりまで来た。ループする状態は 1 周した
	TimeAtLeast,  ///< 今の状態に入ってからの秒数 >= value
	PhaseAtLeast, ///< 今の周回の中の位置 (0..1) >= value
};

struct AnimCondition
{
	AnimCondOp op = AnimCondOp::IsTrue;
	std::int16_t param = -1;
	float value = 0.0f;
};

enum class AnimMotionKind : std::uint8_t
{
	Clip,
	Blend1D,
	Blend2D,
};

/// @brief ブレンドスペースの 1 点 (1D は x だけ使う)。単一のクリップの状態は 1 点だけ持つ
struct AnimBlendPoint
{
	int clip = -1;
	float x = 0.0f;
	float y = 0.0f;
};

enum class AnimFadeCurve : std::uint8_t
{
	Linear,
	Smooth,    ///< 3t^2 - 2t^3
	EaseIn,    ///< t^2
	EaseOut,   ///< 1 - (1 - t)^2
};

struct AnimGraphStateDef
{
	std::string name;
	std::uint32_t key = 0;
	AnimMotionKind kind = AnimMotionKind::Clip;
	std::vector<AnimBlendPoint> points;   ///< Blend1D は x の昇順
	std::int16_t paramX = -1;
	std::int16_t paramY = -1;
	bool loop = true;
	float speed = 1.0f;
	std::int16_t speedParam = -1;         ///< float の param を速さに掛ける
	std::int16_t syncGroup = -1;          ///< AnimGraph::syncGroups の添字
	int enterEvent = -1;                  ///< AnimGraph::eventNames の添字
	int exitEvent = -1;
};

struct AnimGraphTransitionDef
{
	std::int16_t from = -1;               ///< -1 はどの状態からでも
	std::int16_t to = 0;
	std::vector<AnimCondition> conditions;   ///< すべて成り立てば遷移する。空なら常に
	float duration = 0.0f;                ///< クロスフェードの秒数。0 なら即座に切り替える
	AnimFadeCurve curve = AnimFadeCurve::Linear;
	int priority = 0;                     ///< 大きいほど先に調べる。同じなら書いた順
	bool interruptible = true;            ///< このクロスフェードの途中で別の遷移が割り込めるか
	bool allowSelf = false;               ///< どの状態からでも出る遷移が、今の状態へ入り直すのを許す
	float windowStart = 0.0f;             ///< 今の周回の中の位置がこの範囲にあるときだけ調べる (取り消しの窓)
	float windowEnd = 1.0f;
	std::uint16_t key = 0;                ///< from と to の名前 (と同じ組の何本目か) の鍵。読み込み直しで番号を引き直す
};

struct AnimGraphLayerDef
{
	std::string name;
	int mask = -1;                        ///< AnimAsset::masks の添字。-1 は全身
	bool additive = false;
	float weight = 1.0f;
	std::int16_t weightParam = -1;        ///< float の param を weight に掛ける
	std::int16_t entry = 0;
	std::vector<AnimGraphStateDef> states;
	std::vector<AnimGraphTransitionDef> transitions;   ///< priority の大きい順 (同じなら書いた順)
};

/// @brief 足並みをそろえる組。markers はクリップのイベント名 (AnimGraph::eventNames の添字)
struct AnimSyncGroup
{
	std::string name;
	std::vector<int> markers;
};

struct AnimGraph
{
	std::string name;
	std::uint32_t key = 0;                ///< animNameKey(name)
	std::uint32_t paramsKey = 0;          ///< params の名前と型の並びの鍵。変わったら実行時の param を既定値に戻す
	std::vector<AnimGraphParam> params;
	std::vector<AnimGraphLayerDef> layers;
	std::vector<AnimSyncGroup> syncGroups;
	/// 先頭は組んだときの AnimAsset::eventNames と同じ並び。続きがグラフだけの名前 (状態の enter / exit)
	std::vector<std::string> eventNames;

	[[nodiscard]] int findParam(std::string_view n) const noexcept { return findIn(params, n); }
	[[nodiscard]] int findLayer(std::string_view n) const noexcept { return findIn(layers, n); }
	[[nodiscard]] int findState(int layer, std::string_view n) const noexcept
	{
		return (layer >= 0 && layer < static_cast<int>(layers.size())) ? findIn(layers[static_cast<std::size_t>(layer)].states, n)
		                                                                 : -1;
	}
	[[nodiscard]] int findEventName(std::string_view n) const noexcept
	{
		for (std::size_t i = 0; i < eventNames.size(); ++i)
		{
			if (eventNames[i] == n) { return static_cast<int>(i); }
		}
		return -1;
	}

private:
	template <class T>
	[[nodiscard]] static int findIn(const std::vector<T>& items, std::string_view n) noexcept
	{
		for (std::size_t i = 0; i < items.size(); ++i)
		{
			if (items[i].name == n) { return static_cast<int>(i); }
		}
		return -1;
	}
};

/// @brief 姿勢へ出すクリップ 1 本 (12 byte)
struct AnimGraphSample
{
	std::int16_t clip = -1;
	std::uint8_t loop = 1;
	std::uint8_t pad = 0;
	float time = 0.0f;
	float weight = 0.0f;
};

/// @brief 1 レイヤの実行時の状態 (88 byte)
struct AnimGraphLayerState
{
	std::uint32_t stateKey = 0;           ///< 今の状態の名前の鍵。JSON の読み込みのたびに番号をこれで引き直す
	std::uint32_t fromKey = 0;
	std::int16_t state = -1;              ///< 今の (向かう先の) 状態
	std::int16_t from = -1;               ///< クロスフェードで抜けていく状態。-1 は無しか、止めた姿勢 (frozen)
	std::int16_t transition = -1;         ///< 最後に使った遷移
	std::uint8_t fading = 0;
	std::uint8_t frozenCount = 0;
	std::uint16_t loops = 0;              ///< 今の状態で回った周回の数 (65535 で止まる)
	std::uint16_t transitionKey = 0;      ///< 最後に使った遷移の鍵 (AnimGraphTransitionDef::key)
	float stateTime = 0.0f;               ///< 今の状態に入ってからの秒数
	float phase = 0.0f;                   ///< 今の周回の中の位置 0..1
	float fromPhase = 0.0f;
	float fadeTime = 0.0f;
	float fadeDuration = 0.0f;
	AnimGraphSample frozen[kAnimGraphMaxFrozen] = {};
};

/// @brief グラフ 1 本ぶんの実行時の状態 (528 byte)。GameMemory に置き、巻き戻しとリプレイに乗る
struct AnimGraphState
{
	std::uint32_t graphKey = 0;           ///< AnimGraph::key。ツール窓がグラフの JSON と組にする
	std::uint32_t paramsKey = 0;          ///< AnimGraph::paramsKey
	std::uint32_t layerCount = 0;
	std::uint32_t triggers = 0;           ///< Trigger の param のビット
	float params[kAnimGraphMaxParams] = {};
	AnimGraphLayerState layers[kAnimGraphMaxLayers] = {};
	std::uint32_t eventCount = 0;         ///< この step に通ったイベントの数
	AnimEventHit events[kAnimGraphMaxEvents] = {};   ///< nameId は AnimGraph::eventNames の添字。状態の enter / exit は clip = -1
	YawXform rootDelta{};                 ///< この step のルートモーション (最初のレイヤ、モデル空間)
};

static_assert(sizeof(AnimGraphSample) == 12 && std::is_trivially_copyable_v<AnimGraphSample>);
static_assert(sizeof(AnimGraphLayerState) == 88 && std::is_trivially_copyable_v<AnimGraphLayerState>);
static_assert(sizeof(AnimGraphState) == 528 && std::is_trivially_copyable_v<AnimGraphState>);

/// @brief param を既定値に戻し、トリガーを下ろす
inline void resetAnimParams(const AnimGraph& graph, AnimGraphState& s) noexcept
{
	s.paramsKey = graph.paramsKey;
	s.triggers = 0;
	for (std::size_t i = 0; i < static_cast<std::size_t>(kAnimGraphMaxParams); ++i)
	{
		const bool used = i < graph.params.size() && graph.params[i].type != AnimParamType::Trigger;
		s.params[i] = used ? graph.params[i].defaultValue : 0.0f;
	}
}

/// @brief params を既定値に、各レイヤを入口の状態にする
inline void resetAnimGraph(const AnimGraph& graph, AnimGraphState& s) noexcept
{
	s = AnimGraphState{};
	s.graphKey = graph.key;
	resetAnimParams(graph, s);
	const std::size_t n = graph.layers.size() < static_cast<std::size_t>(kAnimGraphMaxLayers)
	                          ? graph.layers.size()
	                          : static_cast<std::size_t>(kAnimGraphMaxLayers);
	s.layerCount = static_cast<std::uint32_t>(n);
	for (std::size_t i = 0; i < n; ++i)
	{
		const auto& def = graph.layers[i];
		auto& l = s.layers[i];
		l.state = def.states.empty() ? std::int16_t{-1} : def.entry;
		l.stateKey = l.state >= 0 ? def.states[static_cast<std::size_t>(l.state)].key : 0u;
	}
}

[[nodiscard]] inline AnimGraphState makeAnimGraphState(const AnimGraph& graph) noexcept
{
	AnimGraphState s;
	resetAnimGraph(graph, s);
	return s;
}

/// @brief param を書く。番号は findParam の値。範囲外は何もしない。NaN は 0 にする (遷移の比較が常に偽にならないように)
inline void setAnimParam(AnimGraphState& s, int param, float value) noexcept
{
	if (param < 0 || param >= kAnimGraphMaxParams) { return; }
	s.params[param] = (value == value) ? value : 0.0f;
}
inline void setAnimParam(AnimGraphState& s, int param, int value) noexcept
{
	setAnimParam(s, param, static_cast<float>(value));
}
inline void setAnimParam(AnimGraphState& s, int param, bool value) noexcept
{
	setAnimParam(s, param, value ? 1.0f : 0.0f);
}
inline void fireAnimTrigger(AnimGraphState& s, int param) noexcept
{
	if (param >= 0 && param < kAnimGraphMaxParams) { s.triggers |= 1u << static_cast<unsigned>(param); }
}
[[nodiscard]] inline float animParam(const AnimGraphState& s, int param) noexcept
{
	return (param >= 0 && param < kAnimGraphMaxParams) ? s.params[param] : 0.0f;
}

/// @brief この step にイベント nameId が通ったか (nameId は AnimGraph::findEventName の値)
[[nodiscard]] inline bool animEventFired(const AnimGraphState& s, int nameId) noexcept
{
	for (std::uint32_t i = 0; i < s.eventCount && i < static_cast<std::uint32_t>(kAnimGraphMaxEvents); ++i)
	{
		if (static_cast<int>(s.events[i].nameId) == nameId) { return true; }
	}
	return false;
}

/// @brief レイヤ layer の今の状態の番号 (無ければ -1)
[[nodiscard]] inline int animCurrentState(const AnimGraphState& s, int layer) noexcept
{
	return (layer >= 0 && layer < static_cast<int>(s.layerCount)) ? s.layers[layer].state : -1;
}

} // namespace mitiru::animation
