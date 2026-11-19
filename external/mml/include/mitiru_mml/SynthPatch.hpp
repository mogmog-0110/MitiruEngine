#pragma once
/// @file SynthPatch.hpp
/// @brief 減算シンセ (ノコギリ、矩形、三角のオシレーター、ユニゾン、フィルター、エンベロープ、LFO) の音色データと、組み込みの音色
/// @details 時間は秒、周波数は Hz、離調はセント、量は 0〜1 で持つ。MML の X パラメーター (modern_param) は
///          ここの値を書き換える。supersaw は、ユニゾン 7 本のノコギリ波にした音色のこと。

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace mitiru_mml
{

/// @brief シンセの音色
struct SynthPatch
{
	/// @brief オシレーターの波形 (MML の Xwave の 0〜4 と同じ並び)
	enum class Wave : std::uint8_t
	{
		Saw = 0,
		Square = 1,
		Triangle = 2,
		Sine = 3,
		Noise = 4,
	};

	/// @brief フィルターの種類 (Xftype の 0〜2 と同じ並び)
	enum class FilterType : std::uint8_t
	{
		LowPass = 0,
		HighPass = 1,
		BandPass = 2,
	};

	/// @brief LFO の波形 (Xlfowave の 0〜4 と同じ並び)
	enum class LfoWave : std::uint8_t
	{
		Sine = 0,
		Triangle = 1,
		Saw = 2,
		Square = 3,
		SampleAndHold = 4,
	};

	const char* name = "init";

	Wave wave1 = Wave::Saw;
	Wave wave2 = Wave::Saw;
	float mix2 = 0.0f;           ///< オシレーター 2 の量
	float detuneCents = 0.0f;    ///< オシレーター 2 の離調
	int tune2Semitones = 0;      ///< オシレーター 2 の移調
	float pulseWidth = 0.5f;     ///< 矩形波のパルス幅
	int unison = 1;              ///< オシレーター 1 の重ねる本数 (1〜7)
	float unisonDetune = 20.0f;  ///< 重ねた本数の両端の離調 (セント)
	float spread = 0.8f;         ///< 重ねた本数の左右の広がり (0〜1)
	float sub = 0.0f;            ///< 1 オクターブ下の正弦波の量
	float noise = 0.0f;          ///< 白色雑音の量

	FilterType filterType = FilterType::LowPass;
	float cutoffHz = 2000.0f;
	float resonance = 0.1f;      ///< 0〜1
	float filterEnvSemitones = 0.0f; ///< フィルターエンベロープ 1.0 のときにカットオフを上げる半音数
	float keyTrack = 0.5f;       ///< 0 = 鍵によらず一定、1 = 鍵に完全に追従

	float ampAttack = 0.005f;
	float ampDecay = 0.2f;
	float ampSustain = 0.7f;
	float ampRelease = 0.1f;
	float filterAttack = 0.0f;
	float filterDecay = 0.2f;
	float filterSustain = 0.0f;
	float filterRelease = 0.1f;

	float lfoRateHz = 5.0f;
	LfoWave lfoWave = LfoWave::Sine;
	float lfoPitchCents = 0.0f;
	float lfoFilterSemitones = 0.0f;
	float lfoAmp = 0.0f;         ///< トレモロの深さ 0〜1
	float lfoPulseWidth = 0.0f;  ///< パルス幅を揺らす深さ
	float lfoDelaySec = 0.0f;    ///< LFO が全開になるまでの時間

	float gain = 0.5f;
};

/// @brief 組み込みの音色の数
inline constexpr int SYNTH_PRESET_COUNT = 10;

/// @brief 組み込みの音色。index は MML の @n に当たる
[[nodiscard]] inline SynthPatch synthPreset(int index)
{
	SynthPatch p;
	switch (index)
	{
	case 0: // saw-bass: サブ付きのノコギリのベース。フィルターが閉じながら鳴る
		p.name = "saw-bass";
		p.wave1 = SynthPatch::Wave::Saw;
		p.sub = 0.6f;
		p.cutoffHz = 260.0f;
		p.resonance = 0.3f;
		p.filterEnvSemitones = 36.0f;
		p.keyTrack = 0.3f;
		p.filterDecay = 0.22f;
		p.filterSustain = 0.1f;
		p.ampAttack = 0.002f;
		p.ampDecay = 0.4f;
		p.ampSustain = 0.8f;
		p.ampRelease = 0.08f;
		p.gain = 0.5f;
		break;
	case 1: // square-lead: 離調した 2 本の矩形波とビブラート
		p.name = "square-lead";
		p.wave1 = SynthPatch::Wave::Square;
		p.wave2 = SynthPatch::Wave::Square;
		p.mix2 = 0.8f;
		p.detuneCents = 9.0f;
		p.cutoffHz = 5200.0f;
		p.resonance = 0.15f;
		p.keyTrack = 0.8f;
		p.ampAttack = 0.006f;
		p.ampSustain = 0.85f;
		p.ampRelease = 0.12f;
		p.lfoRateHz = 5.5f;
		p.lfoPitchCents = 14.0f;
		p.lfoDelaySec = 0.25f;
		p.gain = 0.4f;
		break;
	case 2: // supersaw: ノコギリ 7 本を重ねて左右に広げる
		p.name = "supersaw";
		p.wave1 = SynthPatch::Wave::Saw;
		p.unison = 7;
		p.unisonDetune = 24.0f;
		p.spread = 0.9f;
		p.cutoffHz = 7000.0f;
		p.resonance = 0.08f;
		p.keyTrack = 0.7f;
		p.ampAttack = 0.01f;
		p.ampDecay = 0.5f;
		p.ampSustain = 0.75f;
		p.ampRelease = 0.3f;
		p.gain = 0.45f;
		break;
	case 3: // pad: ゆっくり立ち上がり、フィルターがゆっくり揺れる
		p.name = "pad";
		p.wave1 = SynthPatch::Wave::Saw;
		p.wave2 = SynthPatch::Wave::Saw;
		p.mix2 = 0.9f;
		p.detuneCents = 11.0f;
		p.unison = 3;
		p.unisonDetune = 12.0f;
		p.spread = 0.7f;
		p.cutoffHz = 1500.0f;
		p.resonance = 0.12f;
		p.keyTrack = 0.6f;
		p.ampAttack = 0.5f;
		p.ampDecay = 0.8f;
		p.ampSustain = 0.9f;
		p.ampRelease = 0.9f;
		p.lfoRateHz = 0.25f;
		p.lfoFilterSemitones = 8.0f;
		p.gain = 0.35f;
		break;
	case 4: // pluck: フィルターが一瞬で閉じる短い音
		p.name = "pluck";
		p.wave1 = SynthPatch::Wave::Saw;
		p.wave2 = SynthPatch::Wave::Square;
		p.mix2 = 0.4f;
		p.detuneCents = 6.0f;
		p.cutoffHz = 500.0f;
		p.resonance = 0.2f;
		p.filterEnvSemitones = 48.0f;
		p.keyTrack = 0.6f;
		p.filterDecay = 0.12f;
		p.filterSustain = 0.0f;
		p.ampAttack = 0.001f;
		p.ampDecay = 0.25f;
		p.ampSustain = 0.0f;
		p.ampRelease = 0.06f;
		p.gain = 0.5f;
		break;
	case 5: // sub-bass: 正弦波のサブベース
		p.name = "sub-bass";
		p.wave1 = SynthPatch::Wave::Sine;
		p.sub = 0.0f;
		p.cutoffHz = 900.0f;
		p.resonance = 0.0f;
		p.keyTrack = 0.0f;
		p.ampAttack = 0.004f;
		p.ampSustain = 1.0f;
		p.ampRelease = 0.07f;
		p.gain = 0.7f;
		break;
	case 6: // tri-lead: 三角波の柔らかいリード
		p.name = "tri-lead";
		p.wave1 = SynthPatch::Wave::Triangle;
		p.cutoffHz = 6000.0f;
		p.keyTrack = 0.8f;
		p.ampAttack = 0.012f;
		p.ampSustain = 0.9f;
		p.ampRelease = 0.15f;
		p.lfoRateHz = 5.0f;
		p.lfoPitchCents = 10.0f;
		p.lfoDelaySec = 0.3f;
		p.gain = 0.55f;
		break;
	case 7: // wobble: LFO がフィルターを大きく動かすベース
		p.name = "wobble";
		p.wave1 = SynthPatch::Wave::Saw;
		p.wave2 = SynthPatch::Wave::Square;
		p.mix2 = 0.5f;
		p.detuneCents = 7.0f;
		p.sub = 0.4f;
		p.cutoffHz = 420.0f;
		p.resonance = 0.55f;
		p.keyTrack = 0.2f;
		p.ampSustain = 1.0f;
		p.ampRelease = 0.06f;
		p.lfoRateHz = 3.0f;
		p.lfoFilterSemitones = 30.0f;
		p.gain = 0.45f;
		break;
	case 8: // riser: ノイズのバンドパスを動かして盛り上げる
		p.name = "riser";
		p.wave1 = SynthPatch::Wave::Noise;
		p.filterType = SynthPatch::FilterType::BandPass;
		p.cutoffHz = 400.0f;
		p.resonance = 0.5f;
		p.keyTrack = 0.0f;
		p.ampAttack = 0.05f;
		p.ampSustain = 1.0f;
		p.ampRelease = 0.4f;
		p.gain = 0.5f;
		break;
	case 9: // acid: 共鳴の強いノコギリ
	default:
		p.name = "acid";
		p.wave1 = SynthPatch::Wave::Saw;
		p.cutoffHz = 300.0f;
		p.resonance = 0.75f;
		p.filterEnvSemitones = 40.0f;
		p.keyTrack = 0.4f;
		p.filterDecay = 0.18f;
		p.filterSustain = 0.0f;
		p.ampAttack = 0.002f;
		p.ampDecay = 0.3f;
		p.ampSustain = 0.7f;
		p.ampRelease = 0.05f;
		p.gain = 0.45f;
		break;
	}
	return p;
}

/// @brief 名前から組み込みの音色番号を探す (無ければ -1)
[[nodiscard]] inline int synthPresetIndex(std::string_view name)
{
	for (int i = 0; i < SYNTH_PRESET_COUNT; ++i)
	{
		if (name == synthPreset(i).name)
		{
			return i;
		}
	}
	return -1;
}

} // namespace mitiru_mml
