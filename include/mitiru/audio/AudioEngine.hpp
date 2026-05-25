#pragma once

/// @file AudioEngine.hpp
/// @brief オーディオエンジンインターフェース
/// @details ゲームオーディオの再生・停止・ボリューム制御を抽象化する。

#include <string>
#include <string_view>

namespace mitiru::audio
{

/// @brief オーディオエンジン抽象インターフェース
/// @details サウンドエフェクト（SE）とBGMの再生を統一的に制御する。
///          具体的な実装はプラットフォーム固有のサブクラスで提供する。
class IAudioEngine
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IAudioEngine() = default;

	/// @brief サウンドを再生する
	/// @param id サウンドID
	virtual void playSound(std::string_view id) = 0;

	/// @brief 音量指定でサウンドを再生する
	/// @details 既定は音量を無視して playSound(id) に委譲する。per-sound 音量を扱える
	///          実装はこれを override する (既存実装を壊さないため非純粋)。
	/// @param id サウンドID
	/// @param volume ボリューム [0.0, 1.0]
	virtual void playSound(std::string_view id, float volume) {
		(void)volume;
		playSound(id);
	}

	/// @brief サウンドを停止する
	/// @param id サウンドID
	virtual void stopSound(std::string_view id) = 0;

	/// @brief BGMを再生する
	/// @param id BGM ID
	virtual void playMusic(std::string_view id) = 0;

	/// @brief 音量・ループ指定で BGM を再生する
	/// @details 既定は音量・ループを無視して playMusic(id) に委譲する。これらを扱える
	///          実装はこれを override する (既存実装を壊さないため非純粋)。
	/// @param id BGM ID
	/// @param volume ボリューム [0.0, 1.0]
	/// @param loop ループ再生するか
	virtual void playMusic(std::string_view id, float volume, bool loop) {
		(void)volume;
		(void)loop;
		playMusic(id);
	}

	/// @brief BGMを停止する
	virtual void stopMusic() = 0;

	/// @brief マスターボリュームを設定する
	/// @param volume ボリューム [0.0, 1.0]
	virtual void setVolume(float volume) = 0;

	/// @brief 指定サウンドが再生中か判定する
	/// @param id サウンドID
	/// @return 再生中なら true
	[[nodiscard]] virtual bool isPlaying(std::string_view id) const = 0;
};

} // namespace mitiru::audio
