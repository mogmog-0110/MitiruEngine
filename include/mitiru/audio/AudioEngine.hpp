#pragma once

/// @file AudioEngine.hpp
/// @brief オーディオエンジンインターフェース
/// @details ゲームオーディオの再生・停止・ボリューム制御を抽象化する。

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/audio/AudioMeter.hpp>

namespace mitiru::audio
{

/// @brief IAudioEngine::playSoundInstance に渡す 1 本分の鳴らし方 (音量・パンはバスと 3D を掛けた後の値)。
struct SoundPlayback
{
	std::uint32_t handle = 0;     ///< 0 = 番号なし (ループなら id で引く)
	float         volume = 1.0f;
	float         pitch = 1.0f;
	float         fadeInSec = 0.0f;
	float         pan = 0.0f;     ///< -1 (左) .. 1 (右)
	bool          loop = false;
};

/// @brief オーディオエンジン抽象インターフェース
/// @details サウンドエフェクト（SE）と BGM の再生を統一的に制御する。
///          具体的な実装はプラットフォーム固有のサブクラスで提供する。
class IAudioEngine
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IAudioEngine() = default;

	/// @brief サウンドを再生する
	/// @param id サウンド ID
	virtual void playSound(std::string_view id) = 0;

	/// @brief 音量を指定してサウンドを再生する
	/// @details 既定では音量を無視して playSound(id) に委譲する。サウンドごとの音量を扱える
	///          実装はこれを override する (既存実装を壊さないため非純粋)。
	/// @param id サウンド ID
	/// @param volume ボリューム [0.0, 1.0]
	virtual void playSound(std::string_view id, float volume) {
		(void)volume;
		playSound(id);
	}

	/// @brief サウンドを停止する
	/// @param id サウンド ID
	virtual void stopSound(std::string_view id) = 0;

	/// @brief BGM を再生する
	/// @param id BGM ID
	virtual void playMusic(std::string_view id) = 0;

	/// @brief 音量とループを指定して BGM を再生する
	/// @details 既定では音量とループを無視して playMusic(id) に委譲する。これらを扱える
	///          実装はこれを override する (既存実装を壊さないため非純粋)。
	/// @param id BGM ID
	/// @param volume ボリューム [0.0, 1.0]
	/// @param loop ループ再生するか
	virtual void playMusic(std::string_view id, float volume, bool loop) {
		(void)volume;
		(void)loop;
		playMusic(id);
	}

	/// @brief BGM を停止する
	virtual void stopMusic() = 0;

	/// @brief マスターボリュームを設定する
	/// @param volume ボリューム [0.0, 1.0]
	virtual void setVolume(float volume) = 0;

	/// @brief 指定したサウンドが再生中か判定する
	/// @param id サウンド ID
	/// @return 再生中なら true
	[[nodiscard]] virtual bool isPlaying(std::string_view id) const = 0;

	// v6 拡張 (#19/#20)。pitch / fade-in / fade-out。既存実装では、既定で従来の経路に
	//    フォールバックし (pitch/fade を無視)、override しない実装もそのまま動く。
	virtual void playSoundEx(std::string_view id, float volume, float pitchScale, float fadeInSec)
	{
		(void)pitchScale; (void)fadeInSec;
		playSound(id, volume);
	}
	/// @brief 効果音をループ再生する (v22)。止めるまで鳴り続ける。
	/// @details 長押しのように、長さが入力で決まる音を再生するための入口。既定では one-shot として
	///          鳴らす (対応していない実装でも無音にはならない)。
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

	/// @brief 毎フレーム 1 回呼ばれる定期メンテナンス (任意、既定では何もしない)。
	/// @details 終了した one-shot voice の回収や、fade-out 完了後の voice 解放など、
	///          「再生のたび」ではなく「時間経過で」後始末すべきものをここで行う (#51)。
	///          固定ステップ (約 60 Hz) の周期で呼ばれる前提。
	virtual void update() {}

	/// @brief 再生中のチャンネルについて、メーターの読みを列挙する (任意)
	/// @details mitiru_mixer 窓のチャンネルごとの VU 用。既定では空を返すため、列挙に対応していない
	///          実装もそのまま動く (非純粋)。再生中の voice を持つ実装が override する。
	/// @return チャンネルごとの { 種別, 実効レベル } の配列
	[[nodiscard]] virtual std::vector<ChannelMeter> meterChannels() const { return {}; }

	/// @brief マスター再生クロック (秒)。デバイスが再生したサンプル位置を返す。
	/// @details リズムゲームなどがフレーム積算ではなく音声クロックを基準に判定するために使う。
	///          再生位置を持たないバックエンド (Null/headless など) は既定で 0 を返し、game 側は
	///          0 のときフレームの dt 積算にフォールバックする。
	[[nodiscard]] virtual double masterTimeSec() const noexcept { return 0.0; }

	// v19 拡張 (oscar-rythm リズム同期)。出力レイテンシ / BGM transport / 予約
	//    いずれも既定 (何もしない / 0) で、対応しない backend もそのまま動く (非純粋)。

	/// @brief 出力レイテンシ (秒)。デバイスバッファに積んでから実際に音が出るまでの遅延。
	/// @details masterTimeSec() はデバイスへ送った位置 (耳で聞こえる位置より先行) なので、判定を耳で聞こえる位置に
	///          補正したいリズムゲームに供給する。耳で聞こえる位置 = masterTimeSec() - outputLatencySec()。
	///          取得できない backend は 0 を返す。
	[[nodiscard]] virtual double outputLatencySec() const noexcept { return 0.0; }

	/// @brief 再生中の BGM を一時停止する (再生位置は保持し、resumeMusic で続きから再生する)。
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

	/// @brief playSoundScheduled が atSec をサンプル精度で反映するか (#F2)。
	/// @details 既定は false。playSoundScheduled の既定実装は atSec を無視して即時再生に
	///          フォールバックするため、呼び出し側 (hud.playAt 経路) は、これが false のとき
	///          「予約は機能せず即時再生になる」ことを warnOnce で伝えるべき。実際にサンプル
	///          精度で予約する実装 (Miniaudio/WebAudio) だけが true を返す。
	[[nodiscard]] virtual bool supportsScheduledPlayback() const noexcept { return false; }

	// ── Voice (SoundIntent category=2、K1): BGM / SE と独立した同時 1 本の台詞スロット ──

	/// @brief ボイスを再生する。前のボイスが再生中なら、重ねずに差し替える。
	/// @details 専用スロットを持たない backend は既定で SE として再生する (再生はされるが
	///          差し替えにはならず、meterChannels にも "voice" として出ない)。
	virtual void playVoiceEx(std::string_view id, float volume, float pitchScale, float fadeInSec)
	{
		playSoundEx(id, volume, pitchScale, fadeInSec);
	}
	/// @brief 再生中のボイスを止める。fadeOutSec > 0 の場合は、減衰させてから止める。
	virtual void stopVoiceFade(float fadeOutSec) { (void)fadeOutSec; }

	/// @brief BGM だけに low-pass を適用する (cutoffHz <= 0 で外す)。Engine が一時停止している間に使う。
	/// @details 対応しない backend は既定で何もしない (BGM はそのまま再生される)。
	virtual void setMusicLowPass(float cutoffHz) { (void)cutoffHz; }

	// ── 番号付きの再生・パン・BGM の音量 (ABI v45、ENGINE_REQUESTS #15) ──

	/// @brief SE を 1 本再生する。後から変更したり止めたりする場合は、handle が 0 でなければその番号、0 なら id
	///        (ループ音のみ) で特定する。同じ番号 (handle 0 なら同じ id のループ) が再生中なら、先頭から再生し直さず
	///        音量、ピッチ、パンだけを変更する。
	/// @details 既定ではパンと番号を無視して従来の経路で再生する (番号ごとに停止できない backend でも音は出る)。
	virtual void playSoundInstance(std::string_view id, const SoundPlayback& p)
	{
		if (p.loop) { playSoundLoop(id, p.volume, p.pitch, p.fadeInSec); }
		else        { playSoundEx(id, p.volume, p.pitch, p.fadeInSec); }
	}
	/// @brief 再生中の音 (playSoundInstance と同じ特定方法) の音量、ピッチ、パンを変更する。既定では何もしない。
	virtual void updateSoundInstance(std::uint32_t handle, std::string_view id, float volume, float pitch, float pan)
	{
		(void)handle; (void)id; (void)volume; (void)pitch; (void)pan;
	}
	/// @brief 番号 handle の音を止める (fadeOutSec > 0 の場合は、減衰させてから止める)。既定では id で止める。
	virtual void stopSoundInstance(std::uint32_t handle, std::string_view id, float fadeOutSec)
	{
		(void)handle;
		stopSoundFade(id, fadeOutSec);
	}
	/// @brief 再生中の BGM の音量を変更する (バス音量を掛け直す)。既定では何もしない。
	virtual void setMusicVolume(float volume) { (void)volume; }
};

} // namespace mitiru::audio
