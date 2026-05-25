#pragma once

/// @file SoundIntentRouter.hpp
/// @brief SoundIntent (DLL → host の intent) を IAudioEngine 操作へ写像する host 専用 glue
/// @details
/// ADR 0008: game は audio mixer を持たず SoundIntent を書くだけ。host がこの関数で
/// intent を解釈し、自分が所有する IAudioEngine を駆動する (ADR 0005 整合)。
///
/// この header は **host 側のみ** が include する。DLL は ModuleApi.hpp だけを include する
/// ので、audio 依存が DLL 側へ漏れない。
///
/// SoundIntent のフィールド解釈:
///   - category: 0=SE, 1=BGM, 2=Voice (BGM のみ playMusic 経路、他は playSound 経路)
///   - stop:     1 なら再生でなく停止 (BGM→stopMusic / SE→stopSound)
///   - loop:     BGM のループ可否
///   - volume:   0.0–1.0。0 (zero-init の既定) は「未指定 = 既定音量」とみなし 1.0 とする。
///               FrameIntents は毎フレーム zero-init されるため、音量を設定しない game が
///               無音化する footgun を防ぐ。

#include <mitiru/audio/AudioEngine.hpp>
#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::module
{

/// @brief SoundIntent 1 件を IAudioEngine 操作へ写像する
/// @param engine 駆動対象の audio engine
/// @param s 解釈する SoundIntent
inline void applySoundIntent(audio::IAudioEngine& engine, const SoundIntent& s)
{
	const bool isMusic = (s.category == 1);  // 1 = BGM

	if (s.stop != 0)
	{
		if (isMusic)             { engine.stopMusic(); }
		else if (s.id[0] != '\0'){ engine.stopSound(s.id); }
		return;
	}

	if (s.id[0] == '\0') { return; }

	const float vol = (s.volume > 0.0f) ? s.volume : 1.0f;  // 0 = 未指定 → 既定音量
	if (isMusic) { engine.playMusic(s.id, vol, s.loop != 0); }
	else         { engine.playSound(s.id, vol); }
}

}  // namespace mitiru::module
