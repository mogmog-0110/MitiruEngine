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
	/// @brief 効果音をループ再生する (v22)。止めるまで鳴り続ける。
	/// @details 長押しのように長さが入力で決まる音のための入口。既定は one-shot に
	///          落として鳴らす (対応していない実装でも無音にはならない)。
	virtual void playSoundLoop(std::string_view id, float volume, float pitchScale, float fadeInSec)
	{
		playSoundEx(id, volume, pitchScale, fadeInSec);
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

	/// @brief マスター再生クロック (秒)。デバイスが再生したサンプル位置を返す。
	/// @details リズムゲーム等がフレーム積算ではなく音声クロック基準で判定するために使う。
	///          再生位置を持たないバックエンド (Null/headless 等) は既定 0 を返す → game 側は
	///          0 のときフレーム dt 積算にフォールバックする。
	[[nodiscard]] virtual double masterTimeSec() const noexcept { return 0.0; }

	// ── v19 拡張 (oscar-rythm リズム同期): 出力レイテンシ / BGM transport / 予約 ──
	//    いずれも既定 (no-op / 0) で、対応しない backend はそのまま動く (非純粋)。

	/// @brief 出力レイテンシ (秒)。デバイスバッファに積んでから実際に音が出るまでの遅延。
	/// @details masterTimeSec() はデバイスへ送った位置 (耳より先行) なので、判定を耳基準へ
	///          補正したいリズムゲームに供給する。耳の位置 = masterTimeSec() - outputLatencySec()。
	///          取得できない backend は 0 を返す。
	[[nodiscard]] virtual double outputLatencySec() const noexcept { return 0.0; }

	/// @brief 再生中の BGM を一時停止する (再生位置は保持。resumeMusic で続きから)。
	virtual void pauseMusic() {}
	/// @brief pauseMusic で止めた BGM を続きから再開する。
	virtual void resumeMusic() {}
	/// @brief 再生中の BGM を指定位置 (秒) へシークする。
	virtual void seekMusic(double positionSec) { (void)positionSec; }

	/// @brief 効果音を「マスタークロック上の時刻 atSec」にサンプル精度で予約再生する。
	/// @details atSec は masterTimeSec() と同じクロックの絶対時刻。フレーム量子化を避け、
	///          backend のサンプル単位で発火させる。予約に対応しない backend は既定で即時再生に
	///          フォールバックする (ジッタは乗るが鳴る)。
	/// @param id サウンドID  @param atSec 発火時刻 (秒)  @param volume 音量  @param pitchScale ピッチ
	virtual void playSoundScheduled(std::string_view id, double atSec, float volume, float pitchScale)
	{
		(void)atSec;
		playSoundEx(id, volume, pitchScale, 0.0f);
	}
};

} // namespace mitiru::audio
