#pragma once

/// @file AudioEngineFactory.hpp
/// @brief オーディオエンジンのファクトリ
/// @details FMOD または miniaudio バックエンドを選択して IAudioEngine を生成する。
///          コンパイル時に MITIRU_HAS_FMOD が定義されていれば FMOD を優先し、
///          そうでなければ NullAudioEngine にフォールバックする。
///          miniaudio は MiniaudioEngine として別途利用できる (IAudioEngine 非準拠)。

#include <memory>
#include <string_view>

#include <mitiru/audio/AudioEngine.hpp>
#include <mitiru/audio/NullAudioEngine.hpp>
#include <mitiru/audio/fmod/FmodAudioEngine.hpp>

namespace mitiru::audio
{

/// @brief オーディオバックエンドの種別
enum class AudioBackend : uint8_t
{
    Auto,       ///< 自動選択（FMOD優先、なければNull）
    Fmod,       ///< FMOD Core API
    Null,       ///< ヌルバックエンド（無音）
};

/// @brief IAudioEngine を生成するファクトリ関数
/// @param backend バックエンドの種別 (デフォルト: Auto)
/// @return 生成された IAudioEngine のユニークポインタ
///
/// @details
/// - Auto: FMOD が利用可能なら FmodAudioEngine、利用できなければ NullAudioEngine
/// - Fmod: FMOD が利用できなければ NullAudioEngine にフォールバック
/// - Null: 常に NullAudioEngine
///
/// @code
/// auto engine = mitiru::audio::createAudioEngine();
/// engine->playSound("sfx/explosion.wav");
/// engine->setVolume(0.8f);
/// @endcode
[[nodiscard]] inline std::unique_ptr<IAudioEngine> createAudioEngine(
    AudioBackend backend = AudioBackend::Auto)
{
    switch (backend)
    {
    case AudioBackend::Auto:
#ifdef MITIRU_HAS_FMOD
        try
        {
            return std::make_unique<FmodAudioEngine>();
        }
        catch (const std::exception&)
        {
            return std::make_unique<NullAudioEngine>();
        }
#else
        return std::make_unique<NullAudioEngine>();
#endif

    case AudioBackend::Fmod:
#ifdef MITIRU_HAS_FMOD
        return std::make_unique<FmodAudioEngine>();
#else
        // FMOD が利用できないため、Null にフォールバックする
        return std::make_unique<NullAudioEngine>();
#endif

    case AudioBackend::Null:
        return std::make_unique<NullAudioEngine>();
    }

    return std::make_unique<NullAudioEngine>();
}

/// @brief 現在利用できるオーディオバックエンド名を取得する
/// @return バックエンド名の文字列
[[nodiscard]] inline constexpr const char* availableAudioEngineName() noexcept
{
#ifdef MITIRU_HAS_FMOD
    return "FMOD";
#else
    return "Null (miniaudio available as MiniaudioEngine)";
#endif
}

/// @brief FMOD バックエンドが利用可能か
/// @return コンパイル時に FMOD サポートが有効なら true
[[nodiscard]] inline constexpr bool hasFmodSupport() noexcept
{
#ifdef MITIRU_HAS_FMOD
    return true;
#else
    return false;
#endif
}

} // namespace mitiru::audio
