#pragma once

/// @file SineSynth.hpp
/// @brief ポリフォニック正弦波シンセサイザー
/// @details CPU 上でリアルタイムに正弦波を合成するソフトウェアシンセサイザー。
///          最大 MAX_VOICES 個のボイスを同時に発音し、ADSR エンベロープで音量を制御する。
///          ホットパスではアロケーションを行わないことを保証する（ボイスは事前に確保したプールから取得する）。
///
/// @code
/// mitiru::audio::SineSynth synth;
/// synth.setMaxVoices(8);
///
/// // ノートオン（MIDI ノート番号 69 = A4 = 440 Hz, velocity 1.0）
/// auto handle = synth.noteOn(69, 1.0f);
///
/// // PCM バッファ生成（float モノラル）
/// std::vector<float> buf(512);
/// synth.render(buf.data(), 512, 44100);
///
/// // ノートオフ（リリース開始）
/// synth.noteOff(handle);
/// @endcode

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <numbers>

#include <mitiru/audio/AudioStream.hpp>

namespace mitiru::audio
{

/// @brief ADSR エンベロープパラメータ
struct AdsrParams
{
    float attackSec  = 0.01f;  ///< アタック時間（秒）
    float decaySec   = 0.1f;   ///< ディケイ時間（秒）
    float sustainLvl = 0.7f;   ///< サスティンレベル [0.0, 1.0]
    float releaseSec = 0.2f;   ///< リリース時間（秒）
};

/// @brief SineSynth が返すボイスハンドル
/// @details 0 は無効値。noteOn() の戻り値として取得し、noteOff() に渡す。
using VoiceHandle = int;

/// @brief ポリフォニック正弦波シンセサイザー
/// @details スレッドセーフ。noteOn()/noteOff() と render() は別のスレッドから呼び出せる。
///          内部ボイスプールの静的サイズは MAX_VOICES。
///          ボイスが満杯のときは、envLevel * velocity が最も小さいボイスを割り当て直す（voice stealing）。
class SineSynth : public IAudioStream
{
public:
    /// @brief 最大同時発音数
    static constexpr int MAX_VOICES = 16;

    /// @brief サンプルレート / チャンネル数を指定して構築する
    /// @details StreamingAudioEngine (IAudioStream 経由) で動作させるときに使う。
    ///          read() が内部で render(m_sampleRate) を呼び出す。
    explicit SineSynth(std::size_t voiceCount,
                       std::uint32_t sampleRate,
                       std::uint16_t channels = 1) noexcept
        : m_sampleRate(static_cast<int>(sampleRate))
        , m_channels(static_cast<int>(channels))
    {
        const int clamped = (voiceCount == 0) ? 1
            : (voiceCount > static_cast<std::size_t>(MAX_VOICES)
                ? MAX_VOICES
                : static_cast<int>(voiceCount));
        m_maxVoices = clamped;
    }

    // ─── コンストラクタ ───────────────────────────────────────────

    /// @brief デフォルトコンストラクタ
    SineSynth() noexcept = default;

    /// @brief コピー禁止
    SineSynth(const SineSynth&) = delete;
    /// @brief コピー代入禁止
    SineSynth& operator=(const SineSynth&) = delete;
    /// @brief ムーブ禁止（mutex のため）
    SineSynth(SineSynth&&) = delete;
    /// @brief ムーブ代入禁止
    SineSynth& operator=(SineSynth&&) = delete;

    // ─── パラメータ設定 ───────────────────────────────────────────

    /// @brief 最大同時発音数を設定する（MAX_VOICES 以下に収める）
    /// @param voices 希望する最大ボイス数
    void setMaxVoices(int voices) noexcept
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_maxVoices = std::clamp(voices, 1, MAX_VOICES);
    }

    /// @brief 最大同時発音数を返す
    [[nodiscard]] int maxVoices() const noexcept
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_maxVoices;
    }

    /// @brief ADSR パラメータを設定する
    /// @param params 新しい ADSR 値（次の noteOn() から有効）
    void setAdsr(const AdsrParams& params) noexcept
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_adsr = params;
    }

    /// @brief 現在の ADSR パラメータを返す
    [[nodiscard]] AdsrParams adsr() const noexcept
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_adsr;
    }

    /// @brief マスターゲインを設定する [0.0, 1.0]
    /// @param gain 新しいゲイン値
    void setGain(float gain) noexcept
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_gain = std::clamp(gain, 0.0f, 1.0f);
    }

    /// @brief マスターゲインを返す
    [[nodiscard]] float gain() const noexcept
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_gain;
    }

    // ─── 発音制御 ─────────────────────────────────────────────────

    /// @brief ノートオンを発行する
    /// @param midiNote MIDI ノート番号（0-127）。440 Hz = ノート 69
    /// @param velocity ベロシティ [0.0, 1.0]
    /// @return ボイスハンドル（0 = 失敗）
    /// @details ボイスプールが満杯の場合、envLevel * velocity が最小のボイスを割り当て直す。
    VoiceHandle noteOn(int midiNote, float velocity) noexcept
    {
        const std::lock_guard<std::mutex> lock(m_mutex);

        const float freq = midiToHz(midiNote);
        const float vel  = std::clamp(velocity, 0.0f, 1.0f);

        int slot = findFreeSlot();
        if (slot < 0)
        {
            slot = stealSlot();
        }
        if (slot < 0)
        {
            return 0;
        }

        const int handle = ++m_nextHandle;
        if (m_nextHandle <= 0)
        {
            m_nextHandle = 1; // ラップアラウンド保護
        }

        auto& v         = m_voices[static_cast<std::size_t>(slot)];
        v.handle        = handle;
        v.frequency     = freq;
        v.velocity      = vel;
        v.phase         = 0.0f;
        v.envLevel      = 0.0f;
        v.envStage      = EnvStage::Attack;
        v.active        = true;
        v.releasing     = false;

        return handle;
    }

    /// @brief ノートオフを発行する（リリースフェーズを開始する）
    /// @param handle noteOn() が返したハンドル
    /// @details ハンドルが無効の場合は何もしない。
    void noteOff(VoiceHandle handle) noexcept
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        auto* v = findVoice(handle);
        if (!v)
        {
            return;
        }
        v->releasing = true;
        if (v->envStage != EnvStage::Release)
        {
            v->envStage = EnvStage::Release;
        }
    }

    /// @brief 全ボイスを即座に停止する
    void allNotesOff() noexcept
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& v : m_voices)
        {
            v.active = false;
        }
    }

    // ─── クエリ ───────────────────────────────────────────────────

    /// @brief 現在アクティブなボイス数を返す
    [[nodiscard]] int activeVoiceCount() const noexcept
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        int count = 0;
        for (const auto& v : m_voices)
        {
            if (v.active)
            {
                ++count;
            }
        }
        return count;
    }

    /// @brief 指定したハンドルがまだ発音中かどうかを返す
    /// @param handle 確認するボイスハンドル
    [[nodiscard]] bool isActive(VoiceHandle handle) const noexcept
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        const auto* v = findVoiceConst(handle);
        return v != nullptr && v->active;
    }

    // ─── レンダリング ─────────────────────────────────────────────

    /// @brief モノラル float PCM バッファにレンダリングする
    /// @param buffer 出力バッファ（frames サイズ以上であること）
    /// @param frames 生成するフレーム数
    /// @param sampleRate サンプルレート（Hz）
    /// @details 既存のバッファに加算せず、上書きする。
    ///          アロケーションを行わないことを保証する。ホットパスでは new/malloc を呼び出さない。
    void render(float* buffer, std::size_t frames, int sampleRate) noexcept
    {
        const std::lock_guard<std::mutex> lock(m_mutex);

        for (std::size_t i = 0; i < frames; ++i)
        {
            buffer[i] = 0.0f;
        }

        if (sampleRate <= 0)
        {
            return;
        }

        const float dt      = 1.0f / static_cast<float>(sampleRate);
        const float gainVal = m_gain;

        for (auto& v : m_voices)
        {
            if (!v.active)
            {
                continue;
            }

            renderVoice(v, buffer, frames, dt, gainVal);

            if (!v.active)
            {
                // エンベロープの完了時にクリーンアップ済み
            }
        }
    }

    // ─── IAudioStream 実装 ────────────────────────────────────────
    // StreamingAudioEngine (別スレッドの pull 型 audio callback) から使う。
    // 既存の render()/noteOn()/noteOff() と共存する (どちらから呼び出しても安全)。

    /// @brief ストリームを開く
    bool open() override { m_isOpen = true; return true; }

    /// @brief ストリームを閉じる (全ボイスを停止する)
    void close() override { allNotesOff(); m_isOpen = false; }

    /// @brief IAudioStream::read。mono/stereo の float PCM を生成する
    /// @details StreamingAudioEngine の fillThreadFunc から呼び出される。
    ///          1ch の場合は render() でバッファへ直接書き込み、
    ///          2ch の場合は 1ch をレンダリングした後、左右へ複製する。
    std::size_t read(float* buffer, std::size_t frames) override
    {
        if (!buffer || frames == 0) return 0;

        if (m_channels <= 1)
        {
            render(buffer, frames, m_sampleRate);
            return frames;
        }

        // stereo: 一時モノバッファに合成し、左右のチャネルへ複製する
        // 2048 フレーム以内の小さなチャンクに分けて再帰的に処理することで、
        // ホットパスでのアロケーションを避ける。
        constexpr std::size_t kChunk = 2048;
        std::array<float, kChunk> tmp{};
        std::size_t written = 0;
        while (written < frames)
        {
            const std::size_t n = std::min(kChunk, frames - written);
            render(tmp.data(), n, m_sampleRate);
            for (std::size_t i = 0; i < n; ++i)
            {
                buffer[(written + i) * 2 + 0] = tmp[i];
                buffer[(written + i) * 2 + 1] = tmp[i];
            }
            written += n;
        }
        return written;
    }

    /// @brief シーク不可 (連続生成のストリーム)
    bool seek(std::size_t /*frameOffset*/) override { return false; }

    /// @brief フォーマット情報を返す (IAudioStream::format)
    [[nodiscard]] AudioFormat format() const noexcept override
    {
        AudioFormat f{};
        f.sampleRate    = m_sampleRate;
        f.channels      = m_channels;
        f.bitsPerSample = 32;   // float32
        return f;
    }

    /// @brief 総フレーム数。無限ストリームのため 0 を返す
    [[nodiscard]] std::size_t totalFrames() const noexcept override { return 0; }

    /// @brief EOF 判定。無限ストリームなので常に false
    [[nodiscard]] bool isEof() const noexcept override { return false; }

    /// @brief 開いているかどうか
    [[nodiscard]] bool isOpen() const noexcept override { return m_isOpen; }

    /// @brief ADSR を 4 引数で設定する (brief 互換の別名)
    void setEnvelope(float attackSec, float decaySec,
                     float sustainLevel, float releaseSec) noexcept
    {
        AdsrParams p{};
        p.attackSec  = attackSec;
        p.decaySec   = decaySec;
        p.sustainLvl = sustainLevel;
        p.releaseSec = releaseSec;
        setAdsr(p);
    }

    /// @brief マスターゲインを設定する (setGain の別名)
    void setMasterGain(float g) noexcept { setGain(g); }

private:
    // ─── 内部型 ───────────────────────────────────────────────────

    /// @brief ADSR ステージ
    enum class EnvStage : uint8_t
    {
        Attack = 0,
        Decay,
        Sustain,
        Release,
    };

    /// @brief 単一ボイスの内部状態
    struct Voice
    {
        VoiceHandle handle   = 0;
        float       frequency = 440.0f;
        float       velocity  = 1.0f;
        float       phase     = 0.0f;    ///< 現在位相 [0.0, 1.0)
        float       envLevel  = 0.0f;    ///< 現在エンベロープレベル [0.0, 1.0]
        EnvStage    envStage  = EnvStage::Attack;
        bool        active    = false;
        bool        releasing = false;
    };

    // ─── ヘルパー ─────────────────────────────────────────────────

    /// @brief MIDI ノート番号を Hz に変換する
    /// @param midiNote 0-127
    [[nodiscard]] static float midiToHz(int midiNote) noexcept
    {
        // A4 = MIDI 69 = 440 Hz
        const float semitones = static_cast<float>(midiNote - 69);
        return 440.0f * std::pow(2.0f, semitones / 12.0f);
    }

    /// @brief 空きスロットのインデックスを返す（なければ -1）
    [[nodiscard]] int findFreeSlot() const noexcept
    {
        for (int i = 0; i < m_maxVoices; ++i)
        {
            if (!m_voices[static_cast<std::size_t>(i)].active)
            {
                return i;
            }
        }
        return -1;
    }

    /// @brief 割り当て直すボイスのスロットを返す（envLevel * velocity が最小）
    [[nodiscard]] int stealSlot() const noexcept
    {
        int   best     = -1;
        float bestPrio = std::numeric_limits<float>::max();

        for (int i = 0; i < m_maxVoices; ++i)
        {
            const auto& v    = m_voices[static_cast<std::size_t>(i)];
            const float prio = v.envLevel * v.velocity;
            if (prio < bestPrio)
            {
                bestPrio = prio;
                best     = i;
            }
        }
        return best;
    }

    /// @brief ハンドルに対応するボイスを検索する
    [[nodiscard]] Voice* findVoice(VoiceHandle handle) noexcept
    {
        for (auto& v : m_voices)
        {
            if (v.active && v.handle == handle)
            {
                return &v;
            }
        }
        return nullptr;
    }

    /// @brief ハンドルに対応するボイスを検索する（const 版）
    [[nodiscard]] const Voice* findVoiceConst(VoiceHandle handle) const noexcept
    {
        for (const auto& v : m_voices)
        {
            if (v.active && v.handle == handle)
            {
                return &v;
            }
        }
        return nullptr;
    }

    /// @brief 1ボイス分を buffer に加算レンダリングする
    /// @param v      ボイス（更新される）
    /// @param buffer 出力バッファ
    /// @param frames フレーム数
    /// @param dt     1サンプルあたりの時間（秒）
    /// @param gainVal マスターゲイン
    void renderVoice(Voice& v, float* buffer, std::size_t frames,
                     float dt, float gainVal) noexcept
    {
        const float twoPi      = 2.0f * std::numbers::pi_v<float>;
        const float phaseStep  = v.frequency * dt;

        const float atkRate = (m_adsr.attackSec  > 0.0f) ? (dt / m_adsr.attackSec)  : 1.0f;
        const float decRate = (m_adsr.decaySec   > 0.0f) ? (dt / m_adsr.decaySec)   : 1.0f;
        const float relRate = (m_adsr.releaseSec > 0.0f) ? (dt / m_adsr.releaseSec) : 1.0f;

        for (std::size_t i = 0; i < frames; ++i)
        {
            // ── エンベロープ更新 ──
            switch (v.envStage)
            {
            case EnvStage::Attack:
                v.envLevel += atkRate;
                if (v.envLevel >= 1.0f)
                {
                    v.envLevel = 1.0f;
                    v.envStage = EnvStage::Decay;
                }
                break;

            case EnvStage::Decay:
                v.envLevel -= decRate * (1.0f - m_adsr.sustainLvl);
                if (v.envLevel <= m_adsr.sustainLvl)
                {
                    v.envLevel = m_adsr.sustainLvl;
                    v.envStage = EnvStage::Sustain;
                }
                break;

            case EnvStage::Sustain:
                // 値は変化しない
                break;

            case EnvStage::Release:
                v.envLevel -= relRate;
                if (v.envLevel <= 0.0f)
                {
                    v.envLevel = 0.0f;
                    v.active   = false;
                    // 残りのフレームにはゼロを加算したまま終了する
                    return;
                }
                break;
            }

            // ── 正弦波サンプル生成 ──
            const float sample = std::sin(v.phase * twoPi)
                                 * v.envLevel
                                 * v.velocity
                                 * gainVal;

            buffer[i] += sample;

            v.phase += phaseStep;
            if (v.phase >= 1.0f)
            {
                v.phase -= 1.0f;
            }
        }
    }

    // ─── メンバ変数 ───────────────────────────────────────────────

    mutable std::mutex              m_mutex;
    std::array<Voice, MAX_VOICES>   m_voices   {};
    AdsrParams                      m_adsr     {};
    float                           m_gain     = 1.0f;
    int                             m_maxVoices = MAX_VOICES;
    int                             m_nextHandle = 0;

    // ── IAudioStream 用状態 ───────────────────────────────────
    bool                            m_isOpen     = false;
    int                             m_sampleRate = 48000;
    int                             m_channels   = 1;
};

} // namespace mitiru::audio
