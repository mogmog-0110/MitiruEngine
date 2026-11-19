#pragma once
/// @file ChipDriver.hpp
/// @brief チップの口 層 2: 音符やイベントをレジスタ書き込みに変える Driver
/// @details チップに依存しないイベント (NoteOn、NoteOff、Voice、Volume など) を受け取り、
///          その Device のレジスタ書き込みへ変える。チャンネルの種類 (FM、SSG、ドラムなど) の
///          違いは Driver の中で吸収し、シーケンサーは区別しない。
///          チップを足すときは、Device と Driver を 1 組ずつ作る。

#include <mitiru_mml/ChipDevice.hpp>
#include <mitiru_mml/Timeline.hpp>

#include <cstdint>

namespace mitiru_mml
{

/// @brief チャンネル 1 本の能力 (イベントを作る側が、使ってよい表現を決めるのに使う)
struct ChannelCaps
{
	bool valid = false;           ///< このチャンネル番号が存在する
	bool pitchSlide = false;      ///< setPitch() で発音中の音程だけを変えられる (レガートとポルタメント)
	bool hardwareEnvelope = false; ///< setParam() の ENVELOPE を受ける
	bool volumeEvents = true;     ///< setVolume() を発音中にも受ける (false なら NoteOn の velocity だけが効く)
	bool extendedSyntax = false;  ///< 拡張文法 (@n の音色、X パラメーター、和音、アルペジオ、Q) を MML から受ける。既存のチップは false
};

/// @brief setParam() の id のうち、複数のチップで共通に使う物
namespace chip_param
{
inline constexpr int ENVELOPE = 1; ///< value = 形状、value2 = 周期 (SSG のハードウェアエンベロープ)
inline constexpr int FIRST_CHIP_SPECIFIC = 256; ///< これ以降はチップごとに自由に決める
} // namespace chip_param

/// @brief 1 個の Device を音楽の言葉で操作する層
/// @details チャンネル番号は Driver が決める連番 (OPNA なら FM 0〜5、SSG 6〜8、ドラム 9〜14)。
///          音量は MML の V と同じ 0〜15、パンは 0=左・1=中央・2=右、音程は MIDI のノート番号。
class ChipDriver
{
public:
	virtual ~ChipDriver() = default;

	/// @brief 操作する Device
	[[nodiscard]] virtual ChipDevice& device() noexcept = 0;

	/// @brief Device と Driver 自身の状態を電源投入直後に戻し、チップを使える状態にする初期設定を書く
	virtual void reset() = 0;

	/// @brief チャンネルの総数 (番号は 0 から channelCount() - 1)
	[[nodiscard]] virtual int channelCount() const noexcept = 0;

	/// @brief チャンネルの能力
	[[nodiscard]] virtual ChannelCaps channelCaps(int channel) const noexcept = 0;

	/// @brief 発音を始める
	/// @param key MIDI のノート番号 (60 = C4)。ドラムのチャンネルでは無視される
	/// @param velocity 音量 0〜15。発音中の音量を変えられないチャンネルは、これだけが効く
	virtual void noteOn(int channel, int key, int velocity) = 0;

	/// @brief 発音を止める (リリースに入る)
	virtual void noteOff(int channel) = 0;

	/// @brief 音色を選ぶ
	virtual void setVoice(int channel, int voice) = 0;

	/// @brief 音量を変える (0〜15)
	virtual void setVolume(int channel, int volume) = 0;

	/// @brief 定位を変える (0=左、1=中央、2=右)
	virtual void setPan(int channel, int pan) = 0;

	/// @brief 発音を切らずに音程だけを変える (pitchSlide が true のチャンネルだけ)
	virtual void setPitch(int channel, int key) = 0;

	/// @brief チップ固有のパラメーターを設定する
	virtual void setParam(int channel, int id, int value, int value2)
	{
		static_cast<void>(channel);
		static_cast<void>(id);
		static_cast<void>(value);
		static_cast<void>(value2);
	}
};

/// @brief タイムラインのイベントを Driver の呼び出しに変える
/// @details Tempo と LoopPoint は時間軸の仕事なので、ここでは何もしない。
inline void applySeqEvent(ChipDriver& driver, const SeqEvent& ev)
{
	switch (ev.type)
	{
	case SeqEventType::NoteOn: driver.noteOn(ev.channel, ev.a, ev.b); break;
	case SeqEventType::NoteOff: driver.noteOff(ev.channel); break;
	case SeqEventType::Pitch: driver.setPitch(ev.channel, ev.a); break;
	case SeqEventType::Volume: driver.setVolume(ev.channel, ev.a); break;
	case SeqEventType::Pan: driver.setPan(ev.channel, ev.a); break;
	case SeqEventType::Voice: driver.setVoice(ev.channel, ev.a); break;
	case SeqEventType::Param: driver.setParam(ev.channel, ev.a, ev.b, ev.c); break;
	case SeqEventType::Tempo:
	case SeqEventType::LoopPoint:
		break;
	}
}

} // namespace mitiru_mml
