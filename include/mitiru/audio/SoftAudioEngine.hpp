#pragma once

/// @file SoftAudioEngine.hpp
/// @brief ソフトウェアオーディオエンジン（状態追跡付き）
/// @details 実際の音声出力はしないが、再生状態を正確に追跡する。
///          AIテストやヘッドレスモードでの動作検証に使用する。
///          将来的にminiaudioバックエンドを接続可能。

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>

#include <mitiru/audio/AudioEngine.hpp>

namespace mitiru::audio
{

/// @brief サウンドの再生状態
struct SoundState
{
	std::string id;		///< サウンドID
	float volume = 1.0f;	///< 再生ボリューム
	bool looping = false;	///< ループ再生フラグ
};

/// @brief ソフトウェアオーディオエンジン
/// @details 実際の音声出力はしないが、再生状態を正確に追跡する。
///          NullAudioEngineと異なり、playSound/stopSoundの呼び出しを
///          内部状態に反映し、isPlayingで正確に問い合わせ可能。
///
/// @code
/// mitiru::audio::SoftAudioEngine engine;
/// engine.playSound("explosion");
/// assert(engine.isPlaying("explosion")); // true
/// engine.stopSound("explosion");
/// assert(!engine.isPlaying("explosion")); // true
/// @endcode
class SoftAudioEngine : public IAudioEngine
{
public:
	/// @brief サウンドを再生する
	/// @param id サウンドID
	void playSound(std::string_view id) override
	{
		m_playingSounds[std::string(id)] = SoundState{std::string(id), m_masterVolume, false};
	}

	/// @brief サウンドを停止する
	/// @param id サウンドID
	void stopSound(std::string_view id) override
	{
		m_playingSounds.erase(std::string(id));
	}

	/// @brief BGMを再生する
	/// @param id BGM ID
	void playMusic(std::string_view id) override
	{
		m_currentMusic = std::string(id);
		m_musicPlaying = true;
	}

	/// @brief BGMを停止する
	void stopMusic() override
	{
		m_currentMusic.clear();
		m_musicPlaying = false;
	}

	/// @brief マスターボリュームを設定する
	/// @param volume ボリューム [0.0, 1.0]
	void setVolume(float volume) override
	{
		m_masterVolume = std::clamp(volume, 0.0f, 1.0f);
	}

	/// @brief 指定サウンドが再生中か判定する
	/// @param id サウンドID
	/// @return 再生中なら true
	[[nodiscard]] bool isPlaying(std::string_view id) const override
	{
		const std::string key(id);
		if (m_playingSounds.find(key) != m_playingSounds.end())
		{
			return true;
		}
		// BGMのIDもチェック
		if (m_musicPlaying && m_currentMusic == key)
		{
			return true;
		}
		return false;
	}

	// ── 追加クエリ（テスト・デバッグ用） ────────────────────

	/// @brief 再生中のサウンド数を取得する
	/// @return 再生中のサウンド数（BGMを除く）
	[[nodiscard]] size_t playingSoundCount() const noexcept
	{
		return m_playingSounds.size();
	}

	/// @brief 現在のBGM IDを取得する
	/// @return BGM ID（再生中でなければ空文字列）
	[[nodiscard]] const std::string& currentMusic() const noexcept
	{
		return m_currentMusic;
	}

	/// @brief BGMが再生中か判定する
	/// @return BGM再生中なら true
	[[nodiscard]] bool isMusicPlaying() const noexcept
	{
		return m_musicPlaying;
	}

	/// @brief マスターボリュームを取得する
	/// @return マスターボリューム [0.0, 1.0]
	[[nodiscard]] float masterVolume() const noexcept
	{
		return m_masterVolume;
	}

	/// @brief 全サウンドを停止する（BGM含む）
	void stopAll()
	{
		m_playingSounds.clear();
		m_currentMusic.clear();
		m_musicPlaying = false;
	}

private:
	std::unordered_map<std::string, SoundState> m_playingSounds;	///< 再生中のサウンド
	std::string m_currentMusic;						///< 現在のBGM ID
	bool m_musicPlaying = false;						///< BGM再生中フラグ
	float m_masterVolume = 1.0f;						///< マスターボリューム
};

} // namespace mitiru::audio
