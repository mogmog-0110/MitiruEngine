#pragma once
/// @file MmlTypes.hpp
/// @brief MML共通型定義

#include <cstdint>
#include <vector>

namespace mitiru_mml
{

/// @brief 波形タイプ
enum class WaveType : std::uint8_t
{
	Square = 0,      ///< 矩形波（デューティ50%）
	Triangle = 1,    ///< 三角波
	Sine = 2,        ///< 正弦波
	Sawtooth = 3,    ///< 鋸歯波
	Noise = 4,       ///< ノイズ
	FmPreset0 = 5,   ///< FM合成プリセット0（ピアノ風）
	FmPreset1 = 6,   ///< FM合成プリセット1（ベル風）
	FmPreset2 = 7,   ///< FM合成プリセット2（ブラス風）
	FmPreset3 = 8,   ///< FM合成プリセット3（ストリングス風）
	FmPreset4 = 9,   ///< FM合成プリセット4（オルガン風）
	FmPreset5 = 10,  ///< FM合成プリセット5（エレピ風）
	FmPreset6 = 11,  ///< FM合成プリセット6（ベース風）
	FmPreset7 = 12   ///< FM合成プリセット7（フルート風）
};

/// @brief MMLコマンド種別
enum class CommandType : std::uint8_t
{
	Note,       ///< 音符（noteNum + duration）
	Rest,       ///< 休符（duration）
	Tempo,      ///< テンポ変更
	Octave,     ///< オクターブ設定
	OctaveUp,   ///< オクターブ+1
	OctaveDown, ///< オクターブ-1
	Length,     ///< デフォルト音長変更
	Volume,     ///< 音量変更
	Waveform,   ///< 波形変更
	Quantize,   ///< ゲートタイム（Q値）
	Tie,        ///< タイ（次の音符と接続）
	Loop,       ///< ループ（将来用）
	Duty,       ///< デューティ比変更 (W)
	Detune,     ///< デチューン量 (H) セント単位
	Adsr,       ///< ADSRエンベロープ設定
	Vibrato,    ///< LFOビブラート (M)
	FmWave,     ///< FM合成波形選択 (@FM)
	LoopStart,  ///< ループ開始 ([)
	LoopEnd,    ///< ループ終了 (]N)
	Velocity,       ///< ノート毎のベロシティ (v小文字)
	Portamento,     ///< ポルタメント (_)
	GraceStart,     ///< グレースノート開始 ({)
	GraceEnd,       ///< グレースノート終了 (})
	CrescStart,     ///< クレッシェンド開始 (()
	DecrescStart,   ///< デクレッシェンド開始 ())
	Tremolo,        ///< トレモロ (Y速度,深さ)
	SsgEnvelope,    ///< SSGハードウェアエンベロープ (SE形状,周期)
	Pan,            ///< パン定位 (P0=左, P1=中央, P2=右)
	LoopPoint,      ///< ループポイント ($)
};

/// @brief MMLコマンド
struct MmlCommand
{
	CommandType type = CommandType::Rest;
	int value = 0;          ///< ノート番号 / テンポ値 / オクターブ値 etc.
	int duration = 0;       ///< tick数（0=デフォルト使用）
	bool dotted = false;    ///< 付点
	bool tied = false;      ///< タイ（次の音符と接続）
	int extra = 0;          ///< 拡張値1（ADSR数値、Vibrato depth等）
	int extra2 = 0;         ///< 拡張値2（ビブラート遅延、SSGエンベロープ周期等）
	bool slur = false;      ///< スラー/レガートフラグ（&で異なる音程間接続時）
	bool portamento = false;///< ポルタメントフラグ（_接頭辞）
};

/// @brief 1トラック分のコマンド列
using CommandList = std::vector<MmlCommand>;

/// @brief PCMサンプルバッファ（16bit モノラル）
using PcmBuffer = std::vector<std::int16_t>;

/// @brief ADSRエンベロープパラメータ
struct AdsrEnvelope
{
	float attack = 0.005f;    ///< アタック時間（秒）
	float decay = 0.05f;      ///< ディケイ時間（秒）
	float sustain = 0.7f;     ///< サステインレベル [0,1]
	float release = 0.05f;    ///< リリース時間（秒）
};

/// @brief FM合成オペレータパラメータ（2オペレータ: キャリア + モジュレータ）
/// @details OPNA (YM2608) スタイルのFM合成。キャリア・モジュレータともに正弦波を使用。
///          モジュレータのADSRエンベロープにより音色が時間変化する（PC-98サウンドの核心）。
struct FmPreset
{
	float ratio = 1.0f;       ///< モジュレータ周波数比（キャリア周波数に対する倍率）
	float index = 2.0f;       ///< 変調指数（ラジアン）— OPNA風。0=純正弦波、8=最大歪み
	float feedback = 0.0f;    ///< 自己フィードバック量 [0,1]（ブラス・ディストーション音色に重要）
	AdsrEnvelope modAdsr{0.001f, 0.1f, 0.3f, 0.2f};  ///< モジュレータ用ADSRエンベロープ
};

/// @brief オーディオ設定
struct AudioConfig
{
	std::uint32_t sampleRate = 44100;   ///< サンプルレート
	float masterVolume = 0.5f;          ///< マスターボリューム [0,1]
};

} // namespace mitiru_mml
