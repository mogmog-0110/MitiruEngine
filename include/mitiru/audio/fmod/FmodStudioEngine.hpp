#pragma once

/// @file FmodStudioEngine.hpp
/// @brief FMOD Studio イベントベースオーディオエンジン
/// @details FMOD Studioのバンク・イベントシステムをラップし、
///          アダプティブミュージックやインタラクティブオーディオを実現する。

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifdef MITIRU_HAS_FMOD
#include <fmod.h>
#include <fmod_studio.h>
#include <fmod_errors.h>
#endif

namespace mitiru::audio
{

#ifdef MITIRU_HAS_FMOD

/// @brief FMOD Studio APIラッパー
/// @details FMOD Studioシステムを管理し、バンクのロード/アンロード、
///          イベントの再生/停止、パラメータ制御を提供する。
///          FMOD Studioはイベントベースのオーディオデザインを可能にし、
///          サウンドデザイナーがFMOD Studioツールで作成したアセットを
///          そのまま再生できる。
class FmodStudioEngine
{
public:
    /// @brief コンストラクタ
    /// @param maxChannels 最大同時発音数
    /// @throws std::runtime_error FMOD Studio初期化失敗時
    explicit FmodStudioEngine(int maxChannels = 512)
    {
        FMOD_RESULT result = FMOD_Studio_System_Create(&m_studioSystem, FMOD_VERSION);
        if (result != FMOD_OK)
        {
            throw std::runtime_error(
                std::string("FMOD_Studio_System_Create failed: ") + FMOD_ErrorString(result));
        }

        result = FMOD_Studio_System_Initialize(
            m_studioSystem, maxChannels, FMOD_STUDIO_INIT_NORMAL, FMOD_INIT_NORMAL, nullptr);
        if (result != FMOD_OK)
        {
            FMOD_Studio_System_Release(m_studioSystem);
            m_studioSystem = nullptr;
            throw std::runtime_error(
                std::string("FMOD_Studio_System_Initialize failed: ") + FMOD_ErrorString(result));
        }
    }

    /// @brief デストラクタ（全バンクアンロード＋システム終了）
    ~FmodStudioEngine()
    {
        for (auto& [key, instance] : m_eventInstances)
        {
            if (instance)
            {
                FMOD_Studio_EventInstance_Stop(instance, FMOD_STUDIO_STOP_IMMEDIATE);
                FMOD_Studio_EventInstance_Release(instance);
            }
        }
        m_eventInstances.clear();

        for (auto& [key, bank] : m_banks)
        {
            if (bank)
            {
                FMOD_Studio_Bank_Unload(bank);
            }
        }
        m_banks.clear();

        if (m_studioSystem)
        {
            FMOD_Studio_System_Release(m_studioSystem);
        }
    }

    // コピー禁止・ムーブ禁止
    FmodStudioEngine(const FmodStudioEngine&) = delete;
    FmodStudioEngine& operator=(const FmodStudioEngine&) = delete;
    FmodStudioEngine(FmodStudioEngine&&) = delete;
    FmodStudioEngine& operator=(FmodStudioEngine&&) = delete;

    /// @brief 毎フレーム更新
    void update()
    {
        if (m_studioSystem)
        {
            FMOD_Studio_System_Update(m_studioSystem);
        }
    }

    // ── バンク管理 ──

    /// @brief バンクファイルをロードする
    /// @param path バンクファイルパス (.bank)
    /// @return ロード成功なら true
    bool loadBank(std::string_view path)
    {
        const std::string key(path);
        if (m_banks.count(key) > 0) { return true; }

        FMOD_STUDIO_BANK* bank = nullptr;
        const FMOD_RESULT result = FMOD_Studio_System_LoadBankFile(
            m_studioSystem, key.c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &bank);
        if (result != FMOD_OK || !bank) { return false; }

        m_banks[key] = bank;
        return true;
    }

    /// @brief バンクをアンロードする
    /// @param path バンクファイルパス
    void unloadBank(std::string_view path)
    {
        const std::string key(path);
        const auto it = m_banks.find(key);
        if (it != m_banks.end())
        {
            if (it->second)
            {
                FMOD_Studio_Bank_Unload(it->second);
            }
            m_banks.erase(it);
        }
    }

    // ── イベント再生 ──

    /// @brief イベントを再生する
    /// @param eventPath FMOD Studioイベントパス (例: "event:/Music/BGM_01")
    /// @return 再生成功なら true
    bool playEvent(std::string_view eventPath)
    {
        const std::string key(eventPath);

        // 既存インスタンスがあれば停止して再利用
        stopEvent(eventPath);

        FMOD_STUDIO_EVENTDESCRIPTION* desc = nullptr;
        FMOD_RESULT result = FMOD_Studio_System_GetEvent(
            m_studioSystem, key.c_str(), &desc);
        if (result != FMOD_OK || !desc) { return false; }

        FMOD_STUDIO_EVENTINSTANCE* instance = nullptr;
        result = FMOD_Studio_EventDescription_CreateInstance(desc, &instance);
        if (result != FMOD_OK || !instance) { return false; }

        result = FMOD_Studio_EventInstance_Start(instance);
        if (result != FMOD_OK)
        {
            FMOD_Studio_EventInstance_Release(instance);
            return false;
        }

        m_eventInstances[key] = instance;
        return true;
    }

    /// @brief イベントを停止する
    /// @param eventPath FMOD Studioイベントパス
    /// @param allowFadeout フェードアウトを許可するか
    void stopEvent(std::string_view eventPath, bool allowFadeout = true)
    {
        const std::string key(eventPath);
        const auto it = m_eventInstances.find(key);
        if (it != m_eventInstances.end() && it->second)
        {
            const FMOD_STUDIO_STOP_MODE mode = allowFadeout
                ? FMOD_STUDIO_STOP_ALLOWFADEOUT
                : FMOD_STUDIO_STOP_IMMEDIATE;
            FMOD_Studio_EventInstance_Stop(it->second, mode);
            FMOD_Studio_EventInstance_Release(it->second);
            m_eventInstances.erase(it);
        }
    }

    /// @brief イベントパラメータを設定する
    /// @param eventPath FMOD Studioイベントパス
    /// @param paramName パラメータ名
    /// @param value パラメータ値
    /// @return 設定成功なら true
    bool setParameter(std::string_view eventPath, std::string_view paramName, float value)
    {
        const auto it = m_eventInstances.find(std::string(eventPath));
        if (it == m_eventInstances.end() || !it->second) { return false; }

        const std::string name(paramName);
        const FMOD_RESULT result = FMOD_Studio_EventInstance_SetParameterByName(
            it->second, name.c_str(), value, false);
        return result == FMOD_OK;
    }

    /// @brief グローバルパラメータを設定する
    /// @param paramName パラメータ名
    /// @param value パラメータ値
    /// @return 設定成功なら true
    bool setGlobalParameter(std::string_view paramName, float value)
    {
        const std::string name(paramName);
        const FMOD_RESULT result = FMOD_Studio_System_SetParameterByName(
            m_studioSystem, name.c_str(), value, false);
        return result == FMOD_OK;
    }

    /// @brief イベントインスタンスの生ポインタを取得する（上級者向け）
    /// @param eventPath FMOD Studioイベントパス
    /// @return FMOD_STUDIO_EVENTINSTANCEポインタ、見つからなければnullptr
    [[nodiscard]] FMOD_STUDIO_EVENTINSTANCE* getEventInstance(std::string_view eventPath) const
    {
        const auto it = m_eventInstances.find(std::string(eventPath));
        if (it != m_eventInstances.end()) { return it->second; }
        return nullptr;
    }

    /// @brief FMOD_STUDIO_SYSTEMハンドルを取得（上級者向け）
    [[nodiscard]] FMOD_STUDIO_SYSTEM* studioSystemHandle() const noexcept
    {
        return m_studioSystem;
    }

private:
    FMOD_STUDIO_SYSTEM* m_studioSystem = nullptr;
    std::unordered_map<std::string, FMOD_STUDIO_BANK*> m_banks;
    std::unordered_map<std::string, FMOD_STUDIO_EVENTINSTANCE*> m_eventInstances;
};

#else // !MITIRU_HAS_FMOD

/// @brief FMOD Studioスタブ（FMOD未インストール時）
class FmodStudioEngine
{
public:
    explicit FmodStudioEngine(int /*maxChannels*/ = 512)
    {
        throw std::runtime_error(
            "FMOD Studio is not available. "
            "Download FMOD Engine from https://www.fmod.com/download "
            "and extract to external/fmod/");
    }

    void update() {}
    bool loadBank(std::string_view /*path*/) { return false; }
    void unloadBank(std::string_view /*path*/) {}
    bool playEvent(std::string_view /*eventPath*/) { return false; }
    void stopEvent(std::string_view /*eventPath*/, bool /*allowFadeout*/ = true) {}
    bool setParameter(std::string_view /*eventPath*/, std::string_view /*paramName*/, float /*value*/) { return false; }
    bool setGlobalParameter(std::string_view /*paramName*/, float /*value*/) { return false; }
};

#endif // MITIRU_HAS_FMOD

} // namespace mitiru::audio
