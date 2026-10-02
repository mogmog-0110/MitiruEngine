#pragma once

/// @file AnimGraph.hpp
/// @brief POD アニメーションステートマシン + ブレンドツリー基盤
/// @details `AnimationStateMachine`（IKSolver.hpp）は `std::function` 条件と
///          `std::unique_ptr<IBlendNode>` を持つため GameMemory に置けない。
///          こちらは固定長配列のみで構成した POD 版で、rewind リング / checksum に
///          そのまま含められる。骨のブレンド適用自体は行わず、`update()` の結果を
///          `AnimSample` として返すだけとし、実際の適用は `AnimationPlayer::blend()`
///          （ozz 不在環境の CPU 線形ブレンド）や将来の Ozz 実装に委ねる。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>

namespace mitiru::animation
{

/// @brief 遷移条件の種類
enum class CondKind : std::uint8_t
{
	None,            ///< 常に不成立（未使用スロット）
	BoolParamTrue,   ///< AnimParams::boolParams[paramIndex] が true
	FloatParamOver,  ///< AnimParams::floatParams[paramIndex] > threshold
	ClipFinished,    ///< 現在のステートが非ループでクリップ末尾に達した
};

/// @brief 遷移条件（関数ポインタを持たない POD）
struct Cond
{
	CondKind kind = CondKind::None;
	int paramIndex = -1;
	float threshold = 0.0f;
};

/// @brief bool/float パラメータの固定長ビュー
/// @details ゲーム側の入力・状態を AnimGraph へ渡すための POD 形式の受け渡し。
struct AnimParams
{
	static constexpr int kMaxBool = 8;
	static constexpr int kMaxFloat = 8;

	bool boolParams[kMaxBool] = {};
	float floatParams[kMaxFloat] = {};
};

/// @brief ステート定義
struct State
{
	char name[32] = {};
	int clip = -1;
	bool loop = false;
	float speed = 1.0f;
	float duration = 1.0f;  ///< クリップ長（秒）。ClipFinished 判定とループ折返しに使う
};

/// @brief 遷移定義
struct Transition
{
	int from = -1;
	int to = -1;
	Cond cond;
	float blendSec = 0.0f;
};

/// @brief レイヤーの実行時状態
struct Layer
{
	int currentState = -1;
	float time = 0.0f;
	int blendingFrom = -1;  ///< ブレンド中の遷移元ステート（ブレンドしていなければ -1）
	float blendT = 0.0f;    ///< ブレンド開始からの経過秒（0..blendSec）
};

/// @brief 1 レイヤー分の評価結果
/// @details 骨の実際のブレンドは行わず、どの 2 本のクリップをどの重みで混ぜるべきかだけを返す。
struct AnimSample
{
	int clipA = -1;    ///< ブレンド元（非ブレンド時は現在クリップと同一）
	int clipB = -1;    ///< 現在のクリップ
	float tA = 0.0f;   ///< clipA 側の再生時刻（秒）
	float tB = 0.0f;   ///< clipB 側の再生時刻（秒）
	float weight = 1.0f; ///< 0=clipA、1=clipB。ブレンド中は blendSec で 0→1
};

/// @brief POD アニメーションステートマシン + ブレンドツリー
/// @details states/transitions/layers はすべて固定長配列。GameMemory の一部として
///          値コピー・rewind・checksum の対象にできる（is_trivially_copyable 検証済み）。
template<int NStates, int NTransitions, int NLayers>
struct AnimGraph
{
	static_assert(NStates > 0, "AnimGraph には最低1ステートが必要");
	static_assert(NLayers > 0, "AnimGraph には最低1レイヤーが必要");

	State states[NStates] = {};
	Transition transitions[NTransitions] = {};
	Layer layers[NLayers] = {};

	/// @brief 全レイヤーを 1 フレーム進める（時間更新 → ブレンド進行 → 遷移判定の順）
	void update(float dt, const AnimParams& params) noexcept
	{
		for (int i = 0; i < NLayers; ++i)
		{
			updateLayer(layers[i], dt, params);
		}
	}

	/// @brief 指定レイヤーの評価結果を取得する
	[[nodiscard]] AnimSample sample(int layerIndex) const noexcept
	{
		AnimSample result;
		if (layerIndex < 0 || layerIndex >= NLayers) return result;

		const Layer& layer = layers[layerIndex];
		if (layer.currentState < 0) return result;

		const State& current = states[layer.currentState];
		result.clipB = current.clip;
		result.tB = layer.time;

		if (layer.blendingFrom >= 0)
		{
			const State& from = states[layer.blendingFrom];
			const float blendSec = findBlendSec(layer.blendingFrom, layer.currentState);
			result.clipA = from.clip;
			result.tA = layer.blendT;
			result.weight = blendSec > 0.0f
				? std::clamp(layer.blendT / blendSec, 0.0f, 1.0f)
				: 1.0f;
		}
		else
		{
			result.clipA = current.clip;
			result.tA = layer.time;
			result.weight = 1.0f;
		}
		return result;
	}

private:
	/// @brief 1 レイヤー分の時間更新・ブレンド進行・遷移判定
	void updateLayer(Layer& layer, float dt, const AnimParams& params) noexcept
	{
		if (layer.currentState < 0) layer.currentState = 0;

		const State& current = states[layer.currentState];
		layer.time += dt * current.speed;
		if (current.loop && current.duration > 0.0f)
		{
			layer.time = std::fmod(layer.time, current.duration);
		}
		else if (current.duration > 0.0f && layer.time > current.duration)
		{
			layer.time = current.duration;
		}

		if (layer.blendingFrom >= 0)
		{
			layer.blendT += dt;
			const float blendSec = findBlendSec(layer.blendingFrom, layer.currentState);
			if (blendSec <= 0.0f || layer.blendT >= blendSec)
			{
				layer.blendingFrom = -1;
				layer.blendT = 0.0f;
			}
		}

		// 最初にマッチした遷移だけを採用する（多重遷移の優先度は定義順）
		for (int t = 0; t < NTransitions; ++t)
		{
			const Transition& tr = transitions[t];
			if (tr.from != layer.currentState) continue;
			if (!evaluateCond(tr.cond, params, current, layer.time)) continue;

			layer.blendingFrom = layer.currentState;
			layer.blendT = 0.0f;
			layer.currentState = tr.to;
			layer.time = 0.0f;
			break;
		}
	}

	[[nodiscard]] bool evaluateCond(const Cond& cond, const AnimParams& params,
	                                 const State& state, float time) const noexcept
	{
		switch (cond.kind)
		{
		case CondKind::BoolParamTrue:
			return cond.paramIndex >= 0 && cond.paramIndex < AnimParams::kMaxBool
				&& params.boolParams[cond.paramIndex];
		case CondKind::FloatParamOver:
			return cond.paramIndex >= 0 && cond.paramIndex < AnimParams::kMaxFloat
				&& params.floatParams[cond.paramIndex] > cond.threshold;
		case CondKind::ClipFinished:
			return !state.loop && state.duration > 0.0f && time >= state.duration;
		case CondKind::None:
		default:
			return false;
		}
	}

	/// @brief from→to に対応する遷移の blendSec を探す（なければ 0）
	[[nodiscard]] float findBlendSec(int from, int to) const noexcept
	{
		for (int t = 0; t < NTransitions; ++t)
		{
			if (transitions[t].from == from && transitions[t].to == to)
			{
				return transitions[t].blendSec;
			}
		}
		return 0.0f;
	}
};

static_assert(std::is_trivially_copyable_v<State>);
static_assert(std::is_trivially_copyable_v<Transition>);
static_assert(std::is_trivially_copyable_v<Layer>);
static_assert(std::is_trivially_copyable_v<AnimParams>);
static_assert(std::is_trivially_copyable_v<AnimGraph<4, 4, 1>>);

} // namespace mitiru::animation
