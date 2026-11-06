#pragma once

/// @file RumbleEvents.hpp
/// @brief ゲームが頼んだパッドの振動を、--audio-capture の events.jsonl の行にする
/// @details 頼んだ値をそのまま書く。アクセシビリティの倍率と、重なった振動のモーターごとの強い方 (VisualIntentFx) は
///          通る前の値で、決定論の状態には触れない。音の行と分けられるよう "rumble" を持ち "id" を持たない。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::observe
{

namespace rumble_detail
{

/// @brief JSONL に 0.800000011920929 のような float の尾を出さない
[[nodiscard]] inline double rounded(float v) noexcept { return std::round(static_cast<double>(v) * 1000.0) / 1000.0; }

[[nodiscard]] inline nlohmann::json row(std::uint64_t step, float low, float high, float sec)
{
	return { { "frame", step }, { "timeSec", static_cast<double>(step) / 60.0 },
	         { "rumble", { { "low", rounded(low) }, { "high", rounded(high) }, { "sec", rounded(sec) } } } };
}

}  // namespace rumble_detail

/// @brief この update で頼まれた振動。hud.rumble (全台) は "pad" を持たず、hud.rumblePad は台の番号を "pad" に持つ
[[nodiscard]] inline std::vector<nlohmann::json> rumbleEventRows(const module::FrameIntents& intents, std::uint64_t step)
{
	std::vector<nlohmann::json> rows;
	const int visualCount = std::clamp(intents.visualIntentCount, 0, 8);
	for (int i = 0; i < visualCount; ++i)
	{
		const module::VisualIntent& v = intents.visualIntents[i];
		if (v.kind == module::kVisualIntentRumble) { rows.push_back(rumble_detail::row(step, v.r, v.g, v.durSec)); }
	}
	for (int pad = 0; pad < 4; ++pad)
	{
		const module::PadOutIntent& out = intents.padOut[pad];
		if ((out.set & module::kPadOutRumble) == 0) { continue; }
		nlohmann::json r = rumble_detail::row(step, out.rumbleLow, out.rumbleHigh, out.rumbleSec);
		r["pad"] = pad;
		rows.push_back(std::move(r));
	}
	return rows;
}

}  // namespace mitiru::observe
