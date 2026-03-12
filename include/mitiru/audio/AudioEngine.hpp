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

	/// @brief サウンドを停止する
	/// @param id サウンドID
	virtual void stopSound(std::string_view id) = 0;

	/// @brief BGMを再生する
	/// @param id BGM ID
	virtual void playMusic(std::string_view id) = 0;

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
