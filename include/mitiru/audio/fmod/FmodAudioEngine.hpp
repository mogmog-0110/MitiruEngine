#pragma once

/// @file FmodAudioEngine.hpp
/// @brief FMOD Core ベースのオーディオエンジン
/// @details IAudioEngine実装。FMOD Core APIをラップし、サウンド再生・
///          ボリューム制御・3Dオーディオを提供する。
///          MITIRU_HAS_FMOD が定義されていない場合はスタブとしてコンパイルされる。

#include <mitiru/audio/AudioEngine.hpp>
#include <mitiru/audio/SpatialAudio.hpp>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifdef MITIRU_HAS_FMOD
#include <fmod.h>
#include <fmod_errors.h>
#endif

namespace mitiru::audio
{

#ifdef MITIRU_HAS_FMOD

/// @brief FMOD Core APIラッパー
/// @details FMOD_System をRAII管理し、IAudioEngineインターフェースを実装する。
///          3Dオーディオ、ピッチ・パン制御をサポート。
class FmodAudioEngine : public IAudioEngine
{
public:
    /// @brief FMOD設定
    struct Config
    {
        int maxChannels = 512;             ///< 最大同時発音数
        uint32_t sampleRate = 48000;       ///< サンプルレート
        bool enable3D = true;              ///< 3Dオーディオ有効化
        float dopplerScale = 1.0f;         ///< ドップラースケール
        float distanceFactor = 1.0f;       ///< 距離ファクタ(メートル単位)
        float rolloffScale = 1.0f;         ///< ロールオフスケール
    };

    /// @brief コンストラクタ
    /// @param config FMOD設定
    /// @throws std::runtime_error FMOD初期化失敗時
    explicit FmodAudioEngine(const Config& config = {})
    {
        FMOD_RESULT result = FMOD_System_Create(&m_system, FMOD_VERSION);
        if (result != FMOD_OK)
        {
            throw std::runtime_error(
                std::string("FMOD_System_Create failed: ") + FMOD_ErrorString(result));
        }

        result = FMOD_System_Init(m_system, config.maxChannels, FMOD_INIT_NORMAL, nullptr);
        if (result != FMOD_OK)
        {
            FMOD_System_Release(m_system);
            m_system = nullptr;
            throw std::runtime_error(
                std::string("FMOD_System_Init failed: ") + FMOD_ErrorString(result));
        }

        if (config.enable3D)
        {
            FMOD_System_Set3DSettings(
                m_system, config.dopplerScale, config.distanceFactor, config.rolloffScale);
        }
    }

    /// @brief デストラクタ（全サウンド解放＋システム終了）
    ~FmodAudioEngine() override
    {
        for (auto& [key, sound] : m_sounds)
        {
            if (sound)
            {
                FMOD_Sound_Release(sound);
            }
        }
        m_sounds.clear();
        m_channels.clear();

        if (m_system)
        {
            FMOD_System_Close(m_system);
            FMOD_System_Release(m_system);
        }
    }

    // Non-copyable, non-movable (FMOD_System ownership)
    FmodAudioEngine(const FmodAudioEngine&) = delete;
    FmodAudioEngine& operator=(const FmodAudioEngine&) = delete;
    FmodAudioEngine(FmodAudioEngine&&) = delete;
    FmodAudioEngine& operator=(FmodAudioEngine&&) = delete;

    // ── IAudioEngine implementation ──

    void playSound(std::string_view id) override
    {
        auto* sound = getOrLoadSound(id);
        if (!sound) { return; }

        FMOD_CHANNEL* channel = nullptr;
        FMOD_System_PlaySound(m_system, sound, nullptr, false, &channel);
        if (channel)
        {
            FMOD_Channel_SetVolume(channel, m_masterVolume);
            m_channels[std::string(id)] = channel;
        }
    }

    void stopSound(std::string_view id) override
    {
        const auto it = m_channels.find(std::string(id));
        if (it != m_channels.end() && it->second)
        {
            FMOD_Channel_Stop(it->second);
            m_channels.erase(it);
        }
    }

    void playMusic(std::string_view id) override
    {
        stopMusic();

        auto* sound = getOrLoadSound(id, true);
        if (!sound) { return; }

        FMOD_Sound_SetMode(sound, FMOD_LOOP_NORMAL);
        FMOD_System_PlaySound(m_system, sound, nullptr, false, &m_musicChannel);
        if (m_musicChannel)
        {
            FMOD_Channel_SetVolume(m_musicChannel, m_masterVolume);
        }
    }

    void stopMusic() override
    {
        if (m_musicChannel)
        {
            FMOD_Channel_Stop(m_musicChannel);
            m_musicChannel = nullptr;
        }
    }

    void setVolume(float volume) override
    {
        m_masterVolume = std::clamp(volume, 0.0f, 1.0f);
        // Apply to active music channel
        if (m_musicChannel)
        {
            FMOD_Channel_SetVolume(m_musicChannel, m_masterVolume);
        }
    }

    [[nodiscard]] bool isPlaying(std::string_view id) const override
    {
        const auto it = m_channels.find(std::string(id));
        if (it == m_channels.end() || !it->second) { return false; }

        FMOD_BOOL playing = false;
        FMOD_Channel_IsPlaying(it->second, &playing);
        return playing != 0;
    }

    // ── Extended FMOD features ──

    /// @brief 毎フレーム更新（FMOD内部処理を進める）
    /// @details ゲームループ内で毎フレーム呼び出す必要がある。
    void update()
    {
        if (m_system)
        {
            FMOD_System_Update(m_system);
        }
    }

    /// @brief サウンドファイルを事前ロードする
    /// @param id サウンドID（ファイルパス）
    /// @param stream ストリーミングモードで読み込むか
    /// @return ロード成功なら true
    bool loadSound(std::string_view id, bool stream = false)
    {
        return getOrLoadSound(id, stream) != nullptr;
    }

    /// @brief チャンネルのピッチを設定する
    /// @param id サウンドID
    /// @param pitch ピッチ倍率（1.0 = 通常）
    void setPitch(std::string_view id, float pitch)
    {
        const auto it = m_channels.find(std::string(id));
        if (it != m_channels.end() && it->second)
        {
            FMOD_Channel_SetPitch(it->second, std::max(0.0f, pitch));
        }
    }

    /// @brief チャンネルのパンを設定する
    /// @param id サウンドID
    /// @param pan パン値 (-1.0=左, 0.0=中央, 1.0=右)
    void setPan(std::string_view id, float pan)
    {
        const auto it = m_channels.find(std::string(id));
        if (it != m_channels.end() && it->second)
        {
            FMOD_Channel_SetPan(it->second, std::clamp(pan, -1.0f, 1.0f));
        }
    }

    /// @brief 3Dリスナー位置を設定する
    /// @param position リスナー位置
    /// @param forward リスナー前方ベクトル
    /// @param up リスナー上方ベクトル
    void setListenerPosition(
        const AudioVec3& position,
        const AudioVec3& forward = {0, 0, -1},
        const AudioVec3& up = {0, 1, 0})
    {
        if (!m_system) { return; }

        FMOD_VECTOR pos = {position.x, position.y, position.z};
        FMOD_VECTOR vel = {0, 0, 0};
        FMOD_VECTOR fwd = {forward.x, forward.y, forward.z};
        FMOD_VECTOR upVec = {up.x, up.y, up.z};
        FMOD_System_Set3DListenerAttributes(m_system, 0, &pos, &vel, &fwd, &upVec);
    }

    /// @brief 3Dサウンドソース位置を設定する
    /// @param id サウンドID
    /// @param position ソース位置
    void setSoundPosition(std::string_view id, const AudioVec3& position)
    {
        const auto it = m_channels.find(std::string(id));
        if (it == m_channels.end() || !it->second) { return; }

        FMOD_VECTOR pos = {position.x, position.y, position.z};
        FMOD_VECTOR vel = {0, 0, 0};
        FMOD_Channel_Set3DAttributes(it->second, &pos, &vel);
    }

    /// @brief FMOD_Systemハンドルを取得（上級者向け）
    /// @return FMOD_Systemポインタ（所有権なし）
    [[nodiscard]] FMOD_SYSTEM* systemHandle() const noexcept { return m_system; }

private:
    /// @brief サウンドを取得またはロードする
    /// @param id サウンドID（ファイルパス）
    /// @param stream ストリーミングモードか
    /// @return FMOD_Soundポインタ（所有権はFmodAudioEngineが保持）、失敗時nullptr
    FMOD_SOUND* getOrLoadSound(std::string_view id, bool stream = false)
    {
        const std::string key(id);
        const auto it = m_sounds.find(key);
        if (it != m_sounds.end()) { return it->second; }

        FMOD_SOUND* sound = nullptr;
        FMOD_MODE mode = FMOD_DEFAULT;
        if (stream)
        {
            mode |= FMOD_CREATESTREAM;
        }

        const FMOD_RESULT result = FMOD_System_CreateSound(
            m_system, key.c_str(), mode, nullptr, &sound);
        if (result != FMOD_OK || !sound) { return nullptr; }

        m_sounds[key] = sound;
        return sound;
    }

    FMOD_SYSTEM* m_system = nullptr;
    FMOD_CHANNEL* m_musicChannel = nullptr;
    float m_masterVolume = 1.0f;
    std::unordered_map<std::string, FMOD_SOUND*> m_sounds;
    std::unordered_map<std::string, FMOD_CHANNEL*> m_channels;
};

#else // !MITIRU_HAS_FMOD

/// @brief FMODスタブ（FMOD未インストール時）
/// @details FMOD SDKが見つからない場合に使用されるスタブクラス。
///          コンストラクタで例外をスローし、FMODが必要な場面で
///          明確なエラーメッセージを提供する。
class FmodAudioEngine : public IAudioEngine
{
public:
    /// @brief コンストラクタ（常に例外をスロー）
    /// @throws std::runtime_error FMODが利用不可であることを通知
    FmodAudioEngine()
    {
        throw std::runtime_error(
            "FMOD is not available. "
            "Download FMOD Engine from https://www.fmod.com/download "
            "and extract to external/fmod/");
    }

    struct Config
    {
        int maxChannels = 512;
        uint32_t sampleRate = 48000;
        bool enable3D = true;
        float dopplerScale = 1.0f;
        float distanceFactor = 1.0f;
        float rolloffScale = 1.0f;
    };

    explicit FmodAudioEngine(const Config& /*config*/)
    {
        throw std::runtime_error(
            "FMOD is not available. "
            "Download FMOD Engine from https://www.fmod.com/download "
            "and extract to external/fmod/");
    }

    void playSound(std::string_view /*id*/) override {}
    void stopSound(std::string_view /*id*/) override {}
    void playMusic(std::string_view /*id*/) override {}
    void stopMusic() override {}
    void setVolume(float /*volume*/) override {}
    [[nodiscard]] bool isPlaying(std::string_view /*id*/) const override { return false; }
    void update() {}
};

#endif // MITIRU_HAS_FMOD

} // namespace mitiru::audio
