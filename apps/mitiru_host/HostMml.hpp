#pragma once

/// @file HostMml.hpp
/// @brief ゲームが hud.mml で頼んだ MML を、host の miniaudio で鳴らす部品 (ABI v64、ADR 0164)。
/// @details 音はゲームの状態に入らない。PCM を作る処理は MmlRuntime のワーカー thread が行い、鳴らすのはゲームの thread
///          (毎フレームの頭の onFrameStart)。音を出す起動 (実機と --audio-capture) でだけ付け、--headless は付けない (頼みは捨てる)。

#include <functional>
#include <memory>
#include <vector>

#include <mitiru/audio/MmlRuntime.hpp>
#include <mitiru/core/Config.hpp>

#include "FileAudioEngine.hpp"

namespace mitiru::host
{

/// cfg に onMmlRequest と、毎フレームの poll (onFrameStart の後ろ) を足す。既にある onFrameStart はそのまま先に呼ぶ
inline void attachMml(mitiru::EngineConfig& cfg, std::shared_ptr<FileAudioEngine> audio)
{
	auto runtime = std::make_shared<mitiru::audio::MmlRuntime>();
	auto sink = std::make_shared<mitiru::audio::MmlSink>();
	sink->play = [audio](const std::vector<std::uint8_t>& wav, float volume, bool loop, std::uint64_t key)
	{
		audio->miniaudio().playFromMemoryKeyed(wav.data(), wav.size(), volume, loop, key);
	};
	sink->stop = [audio](std::uint64_t key) { audio->miniaudio().stopMemorySounds(key); };
	sink->isPlaying = [audio](std::uint64_t key) { return audio->miniaudio().isMemorySoundPlaying(key); };
	cfg.onMmlRequest = [runtime](const mitiru::module::MmlIntent& intent) { runtime->request(intent); };
	auto previous = cfg.onFrameStart;
	cfg.onFrameStart = [runtime, sink, previous](mitiru::Engine& engine)
	{
		if (previous) { previous(engine); }
		runtime->poll(*sink);
	};
}

}  // namespace mitiru::host
