#pragma once

/// @file IAudioPlayer.hpp
/// @brief 抽象オーディオプレイヤーインターフェース
///
/// フレームワーク非依存のオーディオAPIを定義する。
/// Siv3D、DxLib等の具体的なオーディオ実装はこのインターフェースを実装する。
///
/// @code
/// class MyAudio : public sgc::IAudioPlayer {
/// public:
///     void playBgm(std::string_view path, float volume) override { /* ... */ }
///     void stopBgm(float fadeOutSeconds) override { /* ... */ }
///     // ...
/// };
/// @endcode

#include <string_view>

namespace sgc
{

/// @brief 抽象オーディオプレイヤーインターフェース
///
/// BGM・SE・マスターボリュームの操作を抽象化する。
/// フレームワーク固有の実装はこのインターフェースを継承して提供する。
class IAudioPlayer
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IAudioPlayer() = default;

	// ── BGM ──────────────────────────────────────────────

	/// @brief BGMを再生する
	/// @param path 音声ファイルのパス
	/// @param volume 再生ボリューム [0.0, 1.0]（デフォルト: 1.0）
	virtual void playBgm(std::string_view path, float volume = 1.0f) = 0;

	/// @brief BGMを停止する
	/// @param fadeOutSeconds フェードアウト時間（秒）。0の場合は即時停止
	virtual void stopBgm(float fadeOutSeconds = 0.0f) = 0;

	/// @brief BGMを一時停止する
	virtual void pauseBgm() = 0;

	/// @brief 一時停止中のBGMを再開する
	virtual void resumeBgm() = 0;

	/// @brief BGMのボリュームを設定する
	/// @param volume ボリューム [0.0, 1.0]
	virtual void setBgmVolume(float volume) = 0;

	/// @brief BGMが再生中か
	/// @return 再生中ならtrue
	[[nodiscard]] virtual bool isBgmPlaying() const = 0;

	// ── SE ───────────────────────────────────────────────

	/// @brief SEを再生する
	/// @param path 音声ファイルのパス
	/// @param volume 再生ボリューム [0.0, 1.0]（デフォルト: 1.0）
	/// @return SE再生ハンドル（stopSeで停止に使用）
	virtual int playSe(std::string_view path, float volume = 1.0f) = 0;

	/// @brief 指定ハンドルのSEを停止する
	/// @param handle playSe()で取得したハンドル
	virtual void stopSe(int handle) = 0;

	/// @brief 全SEを停止する
	virtual void stopAllSe() = 0;

	/// @brief SEのボリュームを設定する
	/// @param volume ボリューム [0.0, 1.0]
	virtual void setSeVolume(float volume) = 0;

	// ── マスター ─────────────────────────────────────────

	/// @brief マスターボリュームを設定する
	/// @param volume マスターボリューム [0.0, 1.0]
	virtual void setMasterVolume(float volume) = 0;
};

} // namespace sgc
