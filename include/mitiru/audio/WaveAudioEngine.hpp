#pragma once

/// @file WaveAudioEngine.hpp
/// @brief Windows Wave APIベースのオーディオエンジン
/// @details PlaySound / waveOutSetVolume を使用した実オーディオ再生実装。
///          非Windows環境ではNullAudioEngine互換の無音動作にフォールバックする。

#include <string>
#include <string_view>
#include <unordered_map>

#include <mitiru/audio/AudioEngine.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

namespace mitiru::audio
{

/// @brief Windows Wave APIベースのオーディオエンジン
/// @details BGMはPlaySoundのSND_LOOP、SEはPlaySoundのSND_ASYNCで再生する。
///          ボリューム制御はwaveOutSetVolumeを使用する。
///          非Windows環境では全操作がノーオペレーションとなる。
///
/// @code
/// mitiru::audio::WaveAudioEngine audio;
/// audio.registerSound("bgm01", "sounds/bgm01.wav");
/// audio.registerSound("explosion", "sounds/explosion.wav");
/// audio.playMusic("bgm01");
/// audio.playSound("explosion");
/// audio.setVolume(0.5f);
/// @endcode
class WaveAudioEngine : public IAudioEngine
{
public:
	/// @brief デフォルトコンストラクタ
	WaveAudioEngine() noexcept = default;

	/// @brief デストラクタ（再生中のサウンドを停止する）
	~WaveAudioEngine() override
	{
#ifdef _WIN32
		stopMusic();
#endif
	}

	/// @brief コピー禁止
	WaveAudioEngine(const WaveAudioEngine&) = delete;
	/// @brief コピー代入禁止
	WaveAudioEngine& operator=(const WaveAudioEngine&) = delete;
	/// @brief ムーブコンストラクタ
	WaveAudioEngine(WaveAudioEngine&&) noexcept = default;
	/// @brief ムーブ代入演算子
	WaveAudioEngine& operator=(WaveAudioEngine&&) noexcept = default;

	/// @brief サウンドIDとファイルパスを登録する
	/// @param id サウンドID
	/// @param filePath WAVファイルのパス
	void registerSound(std::string_view id, std::string_view filePath)
	{
		m_soundPaths[std::string(id)] = std::string(filePath);
	}

	/// @brief サウンドを再生する（SE用）
	/// @param id サウンドID（registerSoundで事前登録が必要）
	void playSound(std::string_view id) override
	{
#ifdef _WIN32
		const auto it = m_soundPaths.find(std::string(id));
		if (it == m_soundPaths.end())
		{
			return;
		}

		/// SEはSND_ASYNCで非同期再生する
		::PlaySoundA(
			it->second.c_str(),
			NULL,
			SND_FILENAME | SND_ASYNC);

		m_currentSe = std::string(id);
#else
		static_cast<void>(id);
#endif
	}

	/// @brief サウンドを停止する
	/// @param id サウンドID
	void stopSound(std::string_view id) override
	{
#ifdef _WIN32
		if (m_currentSe == id)
		{
			/// PlaySound(NULL)で現在のサウンドを停止する
			::PlaySoundA(NULL, NULL, 0);
			m_currentSe.clear();
		}
#else
		static_cast<void>(id);
#endif
	}

	/// @brief BGMを再生する（ループ再生）
	/// @param id BGM ID（registerSoundで事前登録が必要）
	void playMusic(std::string_view id) override
	{
#ifdef _WIN32
		const auto it = m_soundPaths.find(std::string(id));
		if (it == m_soundPaths.end())
		{
			return;
		}

		/// BGMはSND_LOOPで無限ループ再生する
		::PlaySoundA(
			it->second.c_str(),
			NULL,
			SND_FILENAME | SND_ASYNC | SND_LOOP);

		m_currentMusic = std::string(id);
		m_musicPlaying = true;
#else
		static_cast<void>(id);
#endif
	}

	/// @brief BGMを停止する
	void stopMusic() override
	{
#ifdef _WIN32
		if (m_musicPlaying)
		{
			::PlaySoundA(NULL, NULL, 0);
			m_musicPlaying = false;
			m_currentMusic.clear();
		}
#endif
	}

	/// @brief マスターボリュームを設定する
	/// @param volume ボリューム [0.0, 1.0]
	void setVolume(float volume) override
	{
		m_volume = (volume < 0.0f) ? 0.0f : (volume > 1.0f) ? 1.0f : volume;

#ifdef _WIN32
		/// waveOutSetVolumeは左右チャンネルを16bitずつ指定する
		const auto level = static_cast<DWORD>(m_volume * 0xFFFF);
		const DWORD dwVolume = (level & 0xFFFF) | ((level & 0xFFFF) << 16);
		::waveOutSetVolume(NULL, dwVolume);
#endif
	}

	/// @brief 指定サウンドが再生中か判定する
	/// @param id サウンドID
	/// @return 再生中なら true
	[[nodiscard]] bool isPlaying(std::string_view id) const override
	{
		if (m_currentMusic == id && m_musicPlaying)
		{
			return true;
		}
		if (m_currentSe == id)
		{
			return true;
		}
		return false;
	}

	/// @brief 現在のボリュームを取得する
	/// @return ボリューム [0.0, 1.0]
	[[nodiscard]] float volume() const noexcept
	{
		return m_volume;
	}

	/// @brief 登録済みサウンド数を取得する
	/// @return 登録数
	[[nodiscard]] std::size_t registeredCount() const noexcept
	{
		return m_soundPaths.size();
	}

private:
	/// @brief サウンドID → ファイルパスのマップ
	std::unordered_map<std::string, std::string> m_soundPaths;

	/// @brief 現在再生中のBGM ID
	std::string m_currentMusic;

	/// @brief 現在再生中のSE ID
	std::string m_currentSe;

	/// @brief BGMが再生中か
	bool m_musicPlaying = false;

	/// @brief マスターボリューム [0.0, 1.0]
	float m_volume = 1.0f;
};

} // namespace mitiru::audio
