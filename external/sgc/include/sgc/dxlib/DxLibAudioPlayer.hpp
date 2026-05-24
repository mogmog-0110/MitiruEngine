#pragma once

/// @file DxLibAudioPlayer.hpp
/// @brief IAudioPlayer の DxLib 実装
///
/// sgc::IAudioPlayer を DxLib のサウンド関数で実装する。
/// LoadSoundMem / PlaySoundMem / StopSoundMem 等を使用する。
///
/// @note このファイルはDxLib.hに依存するため、CI対象外。
///
/// @code
/// sgc::dxlib::DxLibAudioPlayer audio;
/// sgc::IAudioPlayer& a = audio;
/// a.playBgm("bgm/title.mp3");
/// a.playSe("se/shot.wav");
/// @endcode

#include <DxLib.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include "sgc/audio/IAudioPlayer.hpp"

namespace sgc::dxlib
{

/// @brief IAudioPlayer の DxLib 実装
///
/// DxLibのサウンド関数を使用してBGMとSEの再生を行う。
/// LoadSoundMemで読み込んだサウンドハンドルを内部でキャッシュする。
class DxLibAudioPlayer : public IAudioPlayer
{
public:
	/// @brief デストラクタ（全サウンドハンドルを解放する）
	~DxLibAudioPlayer() override
	{
		for (const auto& [path, handle] : m_soundCache)
		{
			DeleteSoundMem(handle);
		}
	}

	/// @brief BGMを再生する
	void playBgm(std::string_view path, float volume = 1.0f) override
	{
		stopBgm();
		m_bgmHandle = getOrLoad(path);
		const int vol = toVolume(volume * m_bgmVolume * m_masterVolume);
		ChangeVolumeSoundMem(vol, m_bgmHandle);
		PlaySoundMem(m_bgmHandle, DX_PLAYTYPE_LOOP);
		m_bgmPlaying = true;
	}

	/// @brief BGMを停止する
	///
	/// @note DxLibにはフェードアウト機能がないため、fadeOutSeconds引数は無視される。
	void stopBgm(float /*fadeOutSeconds*/ = 0.0f) override
	{
		if (m_bgmHandle >= 0)
		{
			StopSoundMem(m_bgmHandle);
		}
		m_bgmPlaying = false;
	}

	/// @brief BGMを一時停止する
	///
	/// @note DxLibにはpause機能がないため、停止として実装する。
	void pauseBgm() override
	{
		if (m_bgmHandle >= 0)
		{
			StopSoundMem(m_bgmHandle);
		}
		m_bgmPlaying = false;
	}

	/// @brief BGMを再開する
	///
	/// @note DxLibにはresume機能がないため、先頭から再生する。
	void resumeBgm() override
	{
		if (m_bgmHandle >= 0)
		{
			PlaySoundMem(m_bgmHandle, DX_PLAYTYPE_LOOP);
			m_bgmPlaying = true;
		}
	}

	/// @brief BGMのボリュームを設定する
	void setBgmVolume(float volume) override
	{
		m_bgmVolume = volume;
		if (m_bgmHandle >= 0 && m_bgmPlaying)
		{
			ChangeVolumeSoundMem(toVolume(m_bgmVolume * m_masterVolume), m_bgmHandle);
		}
	}

	/// @brief BGMが再生中か
	[[nodiscard]] bool isBgmPlaying() const override
	{
		return m_bgmPlaying && m_bgmHandle >= 0 &&
			CheckSoundMem(m_bgmHandle) == 1;
	}

	/// @brief SEを再生する
	int playSe(std::string_view path, float volume = 1.0f) override
	{
		const int soundHandle = getOrLoad(path);
		const int vol = toVolume(volume * m_seVolume * m_masterVolume);
		ChangeVolumeSoundMem(vol, soundHandle);
		PlaySoundMem(soundHandle, DX_PLAYTYPE_BACK);

		const int handle = m_nextSeHandle++;
		m_seHandles[handle] = soundHandle;
		return handle;
	}

	/// @brief 指定ハンドルのSEを停止する
	void stopSe(int handle) override
	{
		const auto it = m_seHandles.find(handle);
		if (it != m_seHandles.end())
		{
			StopSoundMem(it->second);
			m_seHandles.erase(it);
		}
	}

	/// @brief 全SEを停止する
	void stopAllSe() override
	{
		for (const auto& [handle, soundHandle] : m_seHandles)
		{
			StopSoundMem(soundHandle);
		}
		m_seHandles.clear();
	}

	/// @brief SEのボリュームを設定する
	void setSeVolume(float volume) override
	{
		m_seVolume = volume;
	}

	/// @brief マスターボリュームを設定する
	void setMasterVolume(float volume) override
	{
		m_masterVolume = volume;
		if (m_bgmHandle >= 0 && m_bgmPlaying)
		{
			ChangeVolumeSoundMem(toVolume(m_bgmVolume * m_masterVolume), m_bgmHandle);
		}
	}

private:
	std::unordered_map<std::string, int> m_soundCache;  ///< パス→サウンドハンドル
	std::unordered_map<int, int> m_seHandles;            ///< SEハンドル→サウンドハンドル
	float m_masterVolume = 1.0f;  ///< マスターボリューム
	float m_bgmVolume = 1.0f;     ///< BGMボリューム
	float m_seVolume = 1.0f;      ///< SEボリューム
	int m_bgmHandle = -1;         ///< 現在のBGMサウンドハンドル
	int m_nextSeHandle = 1;       ///< 次のSEハンドル番号
	bool m_bgmPlaying = false;    ///< BGM再生中フラグ

	/// @brief パスからサウンドハンドルを取得する（未ロードならロードしてキャッシュ）
	/// @param path 音声ファイルのパス
	/// @return DxLibサウンドハンドル
	int getOrLoad(std::string_view path)
	{
		const std::string key{path};
		const auto it = m_soundCache.find(key);
		if (it != m_soundCache.end())
		{
			return it->second;
		}
		const int handle = LoadSoundMem(key.c_str());
		m_soundCache.emplace(key, handle);
		return handle;
	}

	/// @brief 0.0〜1.0のボリュームをDxLibの0〜255に変換する
	/// @param volume ボリューム [0.0, 1.0]
	/// @return DxLibボリューム [0, 255]
	[[nodiscard]] static int toVolume(float volume) noexcept
	{
		const int v = static_cast<int>(std::round(
			std::clamp(volume, 0.0f, 1.0f) * 255.0f));
		return v;
	}
};

} // namespace sgc::dxlib
