#pragma once

/// @file MitiruAudioPlayer.hpp
/// @brief sgc::IAudioPlayer アダプター
/// @details mitiru::audio::IAudioEngine を sgc::IAudioPlayer インターフェースに
///          適合させるアダプタークラス。

#include <string>
#include <string_view>

#include <sgc/audio/IAudioPlayer.hpp>
#include <mitiru/audio/AudioEngine.hpp>

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
	explicit MitiruAudioPlayer(IAudioEngine* engine) noexcept
		: m_engine(engine)
	{
	}

	// ── BGM ──────────────────────────────────────────────

	/// @brief BGMを再生する
	/// @param path 音声ファイルのパス
	/// @param volume 再生ボリューム [0.0, 1.0]
	void playBgm(std::string_view path, float volume) override
	{
		if (m_engine)
		{
			m_engine->setVolume(volume);
			m_engine->playMusic(path);
			m_bgmPlaying = true;
		}
	}

	/// @brief BGMを停止する
	/// @param fadeOutSeconds フェードアウト時間（現在は無視）
	void stopBgm(float fadeOutSeconds) override
	{
		static_cast<void>(fadeOutSeconds);
		if (m_engine)
		{
			m_engine->stopMusic();
			m_bgmPlaying = false;
		}
	}

	/// @brief BGMを一時停止する
	void pauseBgm() override
	{
		if (m_engine)
		{
			m_engine->stopMusic();
			m_bgmPaused = true;
		}
	}

	/// @brief 一時停止中のBGMを再開する
	void resumeBgm() override
	{
		if (m_engine && m_bgmPaused)
		{
			m_bgmPaused = false;
			/// スタブ: 再開ロジックは将来実装
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
		static_cast<void>(volume);
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
		static_cast<void>(handle);
		/// スタブ: ハンドルベースの停止は将来実装
	}

	/// @brief 全SEを停止する
	void stopAllSe() override
	{
		/// スタブ: 将来実装
	}

	/// @brief SEのボリュームを設定する
	/// @param volume ボリューム [0.0, 1.0]
	void setSeVolume(float volume) override
	{
		static_cast<void>(volume);
		/// スタブ: SE個別ボリューム制御は将来実装
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
	bool m_bgmPlaying = false;          ///< BGM再生中フラグ
	bool m_bgmPaused = false;           ///< BGM一時停止フラグ
	int m_nextSeHandle = 0;             ///< 次のSEハンドル
};

} // namespace mitiru::audio
