#pragma once

/// @file Siv3DAudioPlayer.hpp
/// @brief IAudioPlayer の Siv3D 実装
///
/// sgc::IAudioPlayer を Siv3D の Audio API で実装する。
/// パスをキーにしたAudioキャッシュを内部で管理する。
///
/// @note このファイルはSiv3D SDKに依存するため、CI対象外。
///
/// @code
/// sgc::siv3d::Siv3DAudioPlayer audio;
/// sgc::IAudioPlayer& a = audio;
/// a.playBgm(U8"bgm/title.mp3");
/// a.playSe(U8"se/shot.wav");
/// @endcode

#include <Siv3D.hpp>
#include <string>
#include <unordered_map>

#include "sgc/audio/IAudioPlayer.hpp"

namespace sgc::siv3d
{

/// @brief IAudioPlayer の Siv3D 実装
///
/// Siv3DのAudioクラスを使用してBGMとSEの再生を行う。
/// 同一パスのAudioはキャッシュして再利用する。
class Siv3DAudioPlayer : public IAudioPlayer
{
public:
	/// @brief BGMを再生する
	///
	/// 既にBGMが再生中の場合は停止してから新しいBGMを再生する。
	void playBgm(std::string_view path, float volume = 1.0f) override
	{
		stopBgm();
		m_bgm = getOrLoad(path);
		m_bgm.setVolume(static_cast<double>(volume * m_bgmVolume * m_masterVolume));
		m_bgm.play(s3d::MixBus0);
		m_bgmPlaying = true;
	}

	/// @brief BGMを停止する
	void stopBgm(float fadeOutSeconds = 0.0f) override
	{
		if (m_bgm)
		{
			if (fadeOutSeconds > 0.0f)
			{
				m_bgm.stop(s3d::SecondsF{static_cast<double>(fadeOutSeconds)});
			}
			else
			{
				m_bgm.stop();
			}
		}
		m_bgmPlaying = false;
	}

	/// @brief BGMを一時停止する
	void pauseBgm() override
	{
		if (m_bgm)
		{
			m_bgm.pause();
		}
		m_bgmPlaying = false;
	}

	/// @brief 一時停止中のBGMを再開する
	void resumeBgm() override
	{
		if (m_bgm)
		{
			m_bgm.play();
			m_bgmPlaying = true;
		}
	}

	/// @brief BGMのボリュームを設定する
	void setBgmVolume(float volume) override
	{
		m_bgmVolume = volume;
		if (m_bgm)
		{
			m_bgm.setVolume(static_cast<double>(m_bgmVolume * m_masterVolume));
		}
	}

	/// @brief BGMが再生中か
	[[nodiscard]] bool isBgmPlaying() const override
	{
		return m_bgmPlaying && m_bgm && m_bgm.isPlaying();
	}

	/// @brief SEを再生する
	int playSe(std::string_view path, float volume = 1.0f) override
	{
		auto audio = getOrLoad(path);
		audio.setVolume(static_cast<double>(volume * m_seVolume * m_masterVolume));
		audio.playOneShot();

		const int handle = m_nextSeHandle++;
		m_seHandles[handle] = audio;
		return handle;
	}

	/// @brief 指定ハンドルのSEを停止する
	void stopSe(int handle) override
	{
		const auto it = m_seHandles.find(handle);
		if (it != m_seHandles.end())
		{
			it->second.stop();
			m_seHandles.erase(it);
		}
	}

	/// @brief 全SEを停止する
	void stopAllSe() override
	{
		for (auto& [handle, audio] : m_seHandles)
		{
			audio.stop();
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
		if (m_bgm)
		{
			m_bgm.setVolume(static_cast<double>(m_bgmVolume * m_masterVolume));
		}
	}

private:
	std::unordered_map<std::string, s3d::Audio> m_audioCache;  ///< パス→Audioキャッシュ
	std::unordered_map<int, s3d::Audio> m_seHandles;           ///< SEハンドル→Audio
	s3d::Audio m_bgm;             ///< 現在のBGM
	float m_masterVolume = 1.0f;  ///< マスターボリューム
	float m_bgmVolume = 1.0f;     ///< BGMボリューム
	float m_seVolume = 1.0f;      ///< SEボリューム
	int m_nextSeHandle = 1;       ///< 次のSEハンドル番号
	bool m_bgmPlaying = false;    ///< BGM再生中フラグ

	/// @brief パスからAudioを取得する（未ロードならロードしてキャッシュ）
	/// @param path 音声ファイルのパス
	/// @return Audioオブジェクト
	s3d::Audio getOrLoad(std::string_view path)
	{
		const std::string key{path};
		const auto it = m_audioCache.find(key);
		if (it != m_audioCache.end())
		{
			return it->second;
		}
		s3d::Audio audio{s3d::Unicode::FromUTF8(key)};
		m_audioCache.emplace(key, audio);
		return audio;
	}
};

} // namespace sgc::siv3d
