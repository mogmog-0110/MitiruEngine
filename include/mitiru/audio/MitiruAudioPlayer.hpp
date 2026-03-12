#pragma once

/// @file MitiruAudioPlayer.hpp
/// @brief sgc::IAudioPlayer アダプター
/// @details mitiru::audio::IAudioEngine を sgc::IAudioPlayer インターフェースに
///          適合させるアダプタークラス。

#include <string>
#include <string_view>

#include <sgc/audio/IAudioPlayer.hpp>
#include <mitiru/audio/AudioEngine.hpp>
#include <mitiru/audio/AudioMixer.hpp>

namespace mitiru::audio
{

/// @brief sgc::IAudioPlayer アダプター
/// @details MitiruのIAudioEngineをsgcのIAudioPlayerインターフェースに変換する。
///          sgcの抽象レンダリングパイプラインからオーディオを制御する際に使用する。
///
/// @code
/// auto engine = std::make_shared<NullAudioEngine>();
/// mitiru::audio::MitiruAudioPlayer player(engine.get());
/// player.playSe("click.wav"); // IAudioEngine::playSound に委譲
/// @endcode
class MitiruAudioPlayer : public sgc::IAudioPlayer
{
public:
	/// @brief コンストラクタ
	/// @param engine Mitiruオーディオエンジンへのポインタ（所有権は移転しない）
	/// @param mixer オーディオミキサーへのポインタ（nullptr可、ハンドル管理が有効になる）
	explicit MitiruAudioPlayer(IAudioEngine* engine,
		AudioMixer* mixer = nullptr) noexcept
		: m_engine(engine)
		, m_mixer(mixer)
	{
	}

	// ── BGM ──────────────────────────────────────────────

	/// @brief BGMを再生する
	/// @param path 音声ファイルのパス
	/// @param volume 再生ボリューム [0.0, 1.0]
	void playBgm(std::string_view path, float volume) override
	{
		m_currentBgmId = std::string(path);
		if (m_mixer)
		{
			m_bgmHandle = m_mixer->play(
				path, SoundCategory::Bgm, true, volume);
			m_bgmPlaying = true;
		}
		else if (m_engine)
		{
			m_engine->setVolume(volume);
			m_engine->playMusic(path);
			m_bgmPlaying = true;
		}
	}

	/// @brief BGMを停止する
	/// @param fadeOutSeconds フェードアウト時間（0でも即停止）
	void stopBgm(float fadeOutSeconds) override
	{
		if (m_mixer && m_bgmHandle >= 0)
		{
			if (fadeOutSeconds > 0.0f)
			{
				m_mixer->fadeOut(m_bgmHandle, fadeOutSeconds);
			}
			else
			{
				m_mixer->stop(m_bgmHandle);
			}
			m_bgmHandle = -1;
		}
		else if (m_engine)
		{
			m_engine->stopMusic();
		}
		m_bgmPlaying = false;
		m_bgmPaused = false;
	}

	/// @brief BGMを一時停止する
	void pauseBgm() override
	{
		if (m_mixer && m_bgmHandle >= 0)
		{
			m_mixer->pause(m_bgmHandle);
			m_bgmPaused = true;
		}
		else if (m_engine)
		{
			m_engine->stopMusic();
			m_bgmPaused = true;
		}
	}

	/// @brief 一時停止中のBGMを再開する
	void resumeBgm() override
	{
		if (m_bgmPaused)
		{
			if (m_mixer && m_bgmHandle >= 0)
			{
				m_mixer->resume(m_bgmHandle);
			}
			else if (m_engine && !m_currentBgmId.empty())
			{
				m_engine->playMusic(m_currentBgmId);
			}
			m_bgmPaused = false;
		}
	}

	/// @brief BGMのボリュームを設定する
	/// @param volume ボリューム [0.0, 1.0]
	void setBgmVolume(float volume) override
	{
		if (m_engine)
		{
			m_engine->setVolume(volume);
		}
	}

	/// @brief BGMが再生中か
	/// @return 再生中なら true
	[[nodiscard]] bool isBgmPlaying() const override
	{
		return m_bgmPlaying && !m_bgmPaused;
	}

	// ── SE ───────────────────────────────────────────────

	/// @brief SEを再生する
	/// @param path 音声ファイルのパス
	/// @param volume 再生ボリューム [0.0, 1.0]
	/// @return SE再生ハンドル
	int playSe(std::string_view path, float volume) override
	{
		if (m_mixer)
		{
			return m_mixer->play(path, SoundCategory::Se, false, volume);
		}
		if (m_engine)
		{
			m_engine->playSound(path);
		}
		return ++m_nextSeHandle;
	}

	/// @brief 指定ハンドルのSEを停止する
	/// @param handle SE再生ハンドル
	void stopSe(int handle) override
	{
		if (m_mixer)
		{
			m_mixer->stop(handle);
		}
	}

	/// @brief 全SEを停止する
	void stopAllSe() override
	{
		if (m_mixer)
		{
			m_mixer->stopByCategory(SoundCategory::Se);
		}
	}

	/// @brief SEのカテゴリボリュームを設定する
	/// @param volume ボリューム [0.0, 1.0]
	void setSeVolume(float volume) override
	{
		if (m_mixer)
		{
			m_mixer->setCategoryVolume(SoundCategory::Se, volume);
		}
	}

	// ── マスター ─────────────────────────────────────────

	/// @brief マスターボリュームを設定する
	/// @param volume マスターボリューム [0.0, 1.0]
	void setMasterVolume(float volume) override
	{
		if (m_engine)
		{
			m_engine->setVolume(volume);
		}
	}

private:
	IAudioEngine* m_engine = nullptr;   ///< オーディオエンジン（非所有）
	AudioMixer* m_mixer = nullptr;      ///< オーディオミキサー（非所有、nullptr可）
	std::string m_currentBgmId;         ///< 現在のBGM ID（再開用）
	int m_bgmHandle = -1;              ///< ミキサー上のBGMハンドル
	bool m_bgmPlaying = false;          ///< BGM再生中フラグ
	bool m_bgmPaused = false;           ///< BGM一時停止フラグ
	int m_nextSeHandle = 0;             ///< 次のSEハンドル（ミキサーなし時のフォールバック）
};

} // namespace mitiru::audio
