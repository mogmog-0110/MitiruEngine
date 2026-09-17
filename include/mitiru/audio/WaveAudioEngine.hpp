#pragma once

/// @file WaveAudioEngine.hpp
/// @brief Windows Wave API ベースのオーディオエンジンの宣言

#include <string>
#include <string_view>
#include <unordered_map>

#include <mitiru/audio/AudioEngine.hpp>
#include <mitiru/debug/WarnOnce.hpp>

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

/// @brief PlaySound と waveOutSetVolume を使う Windows 向けオーディオエンジン
/// @details BGM は SND_LOOP、SE は SND_ASYNC で再生する。非 Windows 環境では何もしない。
class WaveAudioEngine : public IAudioEngine
{
public:
	WaveAudioEngine() noexcept = default;

	~WaveAudioEngine() override
	{
#ifdef _WIN32
		stopMusic();
#endif
	}

	WaveAudioEngine(const WaveAudioEngine&) = delete;
	WaveAudioEngine& operator=(const WaveAudioEngine&) = delete;
	WaveAudioEngine(WaveAudioEngine&&) noexcept = default;
	WaveAudioEngine& operator=(WaveAudioEngine&&) noexcept = default;

	/// @param filePath WAV ファイルのパス
	void registerSound(std::string_view id, std::string_view filePath)
	{
		m_soundPaths[std::string(id)] = std::string(filePath);
	}

	/// @brief registerSound で登録済みの SE を再生する
	void playSound(std::string_view id) override
	{
#ifdef _WIN32
		const auto it = m_soundPaths.find(std::string(id));
		if (it == m_soundPaths.end())
		{
			// 未登録 ID を黙って無視すると原因を追えないため、ID ごとに初回だけ警告する（R-01 級）
			mitiru::debug::warnOnceFix("audio.id:" + std::string(id),
				"id " + std::string(id) + " が未登録",
				"registerSound を呼ぶ前に playMusic/playSound を呼んだ",
				"先に registerSound(id, path) でパスを登録する");
			return;
		}

		::PlaySoundA(
			it->second.c_str(),
			NULL,
			SND_FILENAME | SND_ASYNC);

		m_currentSe = std::string(id);
#else
		static_cast<void>(id);
#endif
	}

	void stopSound(std::string_view id) override
	{
#ifdef _WIN32
		if (m_currentSe == id)
		{
			::PlaySoundA(NULL, NULL, 0);
			m_currentSe.clear();
		}
#else
		static_cast<void>(id);
#endif
	}

	/// @brief registerSound で登録済みの BGM をループ再生する
	void playMusic(std::string_view id) override
	{
#ifdef _WIN32
		const auto it = m_soundPaths.find(std::string(id));
		if (it == m_soundPaths.end())
		{
			// 未登録 ID を黙って無視すると原因を追えないため、ID ごとに初回だけ警告する（R-01 級）
			mitiru::debug::warnOnceFix("audio.id:" + std::string(id),
				"id " + std::string(id) + " が未登録",
				"registerSound を呼ぶ前に playMusic/playSound を呼んだ",
				"先に registerSound(id, path) でパスを登録する");
			return;
		}

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

	/// @param volume 0.0 から 1.0 のマスターボリューム
	void setVolume(float volume) override
	{
		m_volume = (volume < 0.0f) ? 0.0f : (volume > 1.0f) ? 1.0f : volume;

#ifdef _WIN32
		/// 左右チャンネルを 16 bit ずつ下位、上位の順に詰める
		const auto level = static_cast<DWORD>(m_volume * 0xFFFF);
		const DWORD dwVolume = (level & 0xFFFF) | ((level & 0xFFFF) << 16);
		::waveOutSetVolume(NULL, dwVolume);
#endif
	}

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

	/// @return 0.0 から 1.0 のマスターボリューム
	[[nodiscard]] float volume() const noexcept
	{
		return m_volume;
	}

	[[nodiscard]] std::size_t registeredCount() const noexcept
	{
		return m_soundPaths.size();
	}

private:
	std::unordered_map<std::string, std::string> m_soundPaths;

	std::string m_currentMusic;

	std::string m_currentSe;

	bool m_musicPlaying = false;

	float m_volume = 1.0f;
};

} // namespace mitiru::audio
