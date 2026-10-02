#pragma once

/// @file NullAudioEngine.hpp
/// @brief ヌルオーディオエンジン（無音実装）
/// @details ヘッドレスモードやテスト時に使用する、何も再生しないオーディオ実装。

#include <mitiru/audio/AudioEngine.hpp>

namespace mitiru::audio
{

/// @brief ヌルオーディオエンジン
/// @details すべての操作で何も処理しない IAudioEngine 実装。
///          ヘッドレスモードやユニットテストで使用する。
///
/// @code
/// mitiru::audio::NullAudioEngine audio;
/// audio.playSound("explosion"); // 何も起きない
/// @endcode
class NullAudioEngine : public IAudioEngine
{
public:
	/// @brief サウンドを再生する（何も処理しない）
	/// @param id サウンド ID（無視される）
	void playSound(std::string_view id) override
	{
		static_cast<void>(id);
	}

	/// @brief サウンドを停止する（何も処理しない）
	/// @param id サウンド ID（無視される）
	void stopSound(std::string_view id) override
	{
		static_cast<void>(id);
	}

	/// @brief BGM を再生する（何も処理しない）
	/// @param id BGM ID（無視される）
	void playMusic(std::string_view id) override
	{
		static_cast<void>(id);
	}

	/// @brief BGM を停止する（何も処理しない）
	void stopMusic() override {}

	/// @brief マスターボリュームを設定する（何も処理しない）
	/// @param volume ボリューム（無視される）
	void setVolume(float volume) override
	{
		static_cast<void>(volume);
	}

	/// @brief 常に false を返す
	/// @param id サウンド ID（無視される）
	/// @return 常に false
	[[nodiscard]] bool isPlaying(std::string_view id) const override
	{
		static_cast<void>(id);
		return false;
	}
};

} // namespace mitiru::audio
