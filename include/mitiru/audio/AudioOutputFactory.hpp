#pragma once

/// @file AudioOutputFactory.hpp
/// @brief オーディオ出力バックエンドのファクトリ
/// @details 実際のデバイスは miniaudio に任せる (OS ごとの出力 API の選択も miniaudio が行う)。
///          miniaudio を持たない Emscripten では常に NullAudioOutput になる。

#include <memory>

#include <mitiru/audio/IAudioOutput.hpp>
#include <mitiru/audio/NullAudioOutput.hpp>

#ifndef __EMSCRIPTEN__
#include <mitiru/audio/MiniaudioOutput.hpp>
#endif

namespace mitiru::audio
{

enum class AudioOutputBackend
{
	Auto,  ///< OS の既定の出力デバイス (miniaudio)
	Null,  ///< 無音。テスト・ヘッドレス向け
};

/// @details デバイスを開くのは initialize() の時点で、開けなければ initialize() が false を返す。
[[nodiscard]] inline std::unique_ptr<IAudioOutput> createAudioOutput(
	AudioOutputBackend backend = AudioOutputBackend::Auto)
{
#ifndef __EMSCRIPTEN__
	if (backend == AudioOutputBackend::Auto)
	{
		return std::make_unique<MiniaudioOutput>();
	}
#else
	static_cast<void>(backend);
#endif
	return std::make_unique<NullAudioOutput>();
}

} // namespace mitiru::audio
