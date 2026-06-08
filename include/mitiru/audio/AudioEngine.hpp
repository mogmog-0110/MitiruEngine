#pragma once

/// @file AudioEngine.hpp
/// @brief オーディオエンジンインターフェース
/// @details ゲームオーディオの再生・停止・ボリューム制御を抽象化する。

#include <string>
#include <string_view>
#include <vector>

#include <mitiru/audio/AudioMeter.hpp>

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

	// ── v6 拡張 (#19/#20): pitch / fade-in / fade-out。既存実装は default で旧経路に
	//    フォールバック (pitch/fade を無視) するので override しない実装はそのまま動く。
	virtual void playSoundEx(std::string_view id, float volume, float pitchScale, float fadeInSec)
	{
		(void)pitchScale; (void)fadeInSec;
		playSound(id, volume);
	}
	virtual void stopSoundFade(std::string_view id, float fadeOutSec)
	{
		(void)fadeOutSec;
		stopSound(id);
	}
	virtual void playMusicEx(std::string_view id, float volume, bool loop, float fadeInSec)
	{
		(void)fadeInSec;
		playMusic(id, volume, loop);
	}
	virtual void stopMusicFade(float fadeOutSec)
	{
		(void)fadeOutSec;
		stopMusic();
	}

	/// @brief 毎フレーム 1 回呼ばれる定期メンテナンス (任意、既定 no-op)。
	/// @details 終了した one-shot voice の回収や、fade-out 完了後の voice 解放など、
	///          「再生のたび」ではなく「時間経過で」掃除すべきものをここで行う (#51)。
	///          固定ステップ (約 60Hz) の cadence で呼ばれる前提。
	virtual void update() {}

	/// @brief 再生中チャンネルのメーター読みを列挙する (任意)
	/// @details mitiru_mixer 窓の per-channel VU 用。既定は空 = 列挙非対応の
	///          実装はそのまま動く (非純粋)。再生中 voice を持つ実装が override する。
	/// @return チャンネルごとの { 種別, 実効レベル } の配列
	[[nodiscard]] virtual std::vector<ChannelMeter> meterChannels() const { return {}; }

	/// @brief マスター再生クロック (秒)。デバイスが再生したサンプル位置を返す (ADR 0008 拡張)。
	/// @details リズムゲーム等がフレーム積算ではなく音声クロック基準で判定するために使う。
	///          再生位置を持たないバックエンド (Null/headless 等) は既定 0 を返す → game 側は
	///          0 のときフレーム dt 積算にフォールバックする。
	[[nodiscard]] virtual double masterTimeSec() const noexcept { return 0.0; }
};

} // namespace mitiru::audio
