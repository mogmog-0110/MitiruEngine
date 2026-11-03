#pragma once

/// @file TimeFlow.hpp
/// @brief スローとヒットストップに合わせてバスの高さと low-pass を変える仕組み（mix.json の timeFlow、ADR 0113）。
/// @details host から見える時間の変化だけを使う。hud.hitStop の間と、EngineConfig の timeScale または layerTimeScale[0] が 1 未満の間が対象。
/// ゲームが独自の dt で作るスロー（OOZE DRIVE の集中など）は host から見えないため、従来どおり hud.busPitch と hud.busLowPass で指定する。
/// 「時間の流れ」を表す flow（1 = 等速、0 = 停止）から ease = 1 - flow を作り、高さは直線、low-pass は対数で停止時の値へ近づける。
/// 音は GameMemory の外にあるため、この値は host が持ち、巻き戻しや replay の結果に影響しない（docs/DETERMINISM.md）。

#include <algorithm>
#include <cmath>

#include <mitiru/audio/mix/BusDucker.hpp>

namespace mitiru::audio::mix
{

struct TimeFlowSettings
{
	MixBus bus = MixBus::Sfx;
	float  pitchAtStop = 0.5f;        ///< 止まったときの高さの倍率 (0.125〜1)。music には掛からない (曲の高さを変える APIが無い)
	float  lowPassHzAtStop = 0.0f;    ///< 止まったときの low-pass (0 で掛けない)
	float  rampSec = 0.05f;           ///< 値が動く速さ (止まりから等速まで、または逆に、この秒数で動く)
	bool   followHitStop = true;      ///< hud.hitStop の間を flow 0 とみなす
	bool   followTimeScale = true;    ///< EngineConfig の timeScale と layerTimeScale[0] (1 未満) を flow とみなす
};

/// host が各ステップで渡す時間変化の入力
struct TimeFlowInput
{
	bool  hitStop = false;
	float timeScale = 1.0f;        ///< EngineConfig::timeScale
	float layerScale = 1.0f;       ///< EngineConfig::layerTimeScale[0]
};

inline constexpr float kTimeFlowOpenHz = 20000.0f;

[[nodiscard]] inline const char* validateTimeFlow(const TimeFlowSettings& t) noexcept
{
	if (t.bus == MixBus::Master || t.bus == MixBus::Voice) { return "bus は music / sfx / ui / ambient / user6 / user7 (台詞には掛けない)"; }
	if (!(t.pitchAtStop >= 0.125f && t.pitchAtStop <= 1.0f)) { return "pitchAtStop は 0.125 以上、1 以下"; }
	if (t.bus == MixBus::Music && t.pitchAtStop != 1.0f) { return "music の高さは変えられない。bus が music のときは pitchAtStop を 1 にして lowPassHzAtStop だけ書く"; }
	if (!(t.lowPassHzAtStop >= 0.0f && t.lowPassHzAtStop <= kTimeFlowOpenHz)) { return "lowPassHzAtStop は 0 以上、20000 以下"; }
	if (!(t.rampSec >= 0.0f && t.rampSec <= 5.0f)) { return "rampSec は 0 以上、5 以下"; }
	return "";
}

/// @brief 時間の流れ。1 = 等速、0 = 止まっている
[[nodiscard]] inline float timeFlowOf(const TimeFlowSettings& t, const TimeFlowInput& in) noexcept
{
	float flow = 1.0f;
	if (t.followHitStop && in.hitStop) { flow = 0.0f; }
	if (t.followTimeScale)
	{
		const float scale = in.timeScale * in.layerScale;
		flow = std::min(flow, std::clamp(std::isfinite(scale) ? scale : 1.0f, 0.0f, 1.0f));
	}
	return flow;
}

/// flow（0..1）に対応する高さの倍率
[[nodiscard]] inline float timeFlowPitch(const TimeFlowSettings& t, float flow) noexcept
{
	const float ease = 1.0f - std::clamp(flow, 0.0f, 1.0f);
	return 1.0f + (t.pitchAtStop - 1.0f) * ease;
}

/// flow に対応する low-pass（Hz）。0 は掛けない。耳には周波数の比で聞こえるため、対数で近づける。
[[nodiscard]] inline float timeFlowLowPassHz(const TimeFlowSettings& t, float flow) noexcept
{
	const float ease = 1.0f - std::clamp(flow, 0.0f, 1.0f);
	if (t.lowPassHzAtStop <= 0.0f || ease <= 0.0f) { return 0.0f; }
	return kTimeFlowOpenHz * std::pow(t.lowPassHzAtStop / kTimeFlowOpenHz, ease);
}

}  // namespace mitiru::audio::mix
