#pragma once

/// @file AudioOutputFactory.hpp
/// @brief オーディオ出力バックエンドのファクトリ
/// @details プラットフォームに応じた最適なIAudioOutput実装を自動選択して生成する。
///          PulseAudioが利用可能なLinux環境ではPulseAudioOutput、
///          それ以外の環境ではNullAudioOutputにフォールバックする。

#include <memory>

#include <mitiru/audio/IAudioOutput.hpp>
#include <mitiru/audio/NullAudioOutput.hpp>

#ifdef MITIRU_PLATFORM_UNIX
#ifdef MITIRU_HAS_PULSEAUDIO
#include <mitiru/audio/PulseAudioOutput.hpp>
#endif
#endif

namespace mitiru::audio
{

/// @brief オーディオ出力バックエンド種別
enum class AudioOutputBackend
{
	Auto,        ///< 自動選択（プラットフォームに応じて最適なバックエンドを選択）
	PulseAudio,  ///< PulseAudio（Linux専用）
	Null,        ///< ヌルバックエンド（無音、テスト・ヘッドレス向け）
};

/// @brief プラットフォームに応じたオーディオ出力バックエンドを生成する
/// @param backend バックエンド種別（デフォルト: Auto）
/// @return 生成されたIAudioOutputのユニークポインタ
///
/// @details
/// - Auto: Linux+PulseAudio → PulseAudioOutput、それ以外 → NullAudioOutput
/// - PulseAudio: Linux+PulseAudioがない場合は NullAudioOutput にフォールバック
/// - Null: 常に NullAudioOutput
///
/// @code
/// auto output = mitiru::audio::createAudioOutput();
/// output->initialize(44100, 2, 4096);
/// // PulseAudioが利用可能ならリアル出力、なければ無音
/// @endcode
[[nodiscard]] inline std::unique_ptr<IAudioOutput> createAudioOutput(
	AudioOutputBackend backend = AudioOutputBackend::Auto)
{
	switch (backend)
	{
	case AudioOutputBackend::Auto:
#if defined(MITIRU_PLATFORM_UNIX) && defined(MITIRU_HAS_PULSEAUDIO)
		return std::make_unique<PulseAudioOutput>();
#else
		return std::make_unique<NullAudioOutput>();
#endif

	case AudioOutputBackend::PulseAudio:
#if defined(MITIRU_PLATFORM_UNIX) && defined(MITIRU_HAS_PULSEAUDIO)
		return std::make_unique<PulseAudioOutput>();
#else
		/// PulseAudioが利用不可 → NullAudioOutputにフォールバック
		return std::make_unique<NullAudioOutput>();
#endif

	case AudioOutputBackend::Null:
		return std::make_unique<NullAudioOutput>();
	}

	return std::make_unique<NullAudioOutput>();
}

/// @brief 現在のプラットフォームで利用可能なオーディオバックエンド名を取得する
/// @return バックエンド名の文字列
[[nodiscard]] inline const char* availableAudioBackendName() noexcept
{
#if defined(MITIRU_PLATFORM_UNIX) && defined(MITIRU_HAS_PULSEAUDIO)
	return "PulseAudio";
#else
	return "Null";
#endif
}

/// @brief PulseAudioバックエンドが利用可能か
/// @return コンパイル時にPulseAudioサポートが有効なら true
[[nodiscard]] inline constexpr bool hasPulseAudioSupport() noexcept
{
#if defined(MITIRU_PLATFORM_UNIX) && defined(MITIRU_HAS_PULSEAUDIO)
	return true;
#else
	return false;
#endif
}

} // namespace mitiru::audio
