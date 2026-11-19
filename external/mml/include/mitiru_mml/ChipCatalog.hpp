#pragma once
/// @file ChipCatalog.hpp
/// @brief チップの種類の一覧と、名前から種類、音色、チャンネルを引く表 (曲のテキストと ModernSong が共有する)
/// @details 古いチップ (OPNA、OPM、OPL 系、SN76489、AY、NES) は makeDriver で Driver を作れる。
///          現代的なチップ (synth、drums、sampler) は出力レートや SF2 を受けるので、ModernSong が作る。

#include <mitiru_mml/Ay8910Driver.hpp>
#include <mitiru_mml/NesApuDriver.hpp>
#include <mitiru_mml/OplDriver.hpp>
#include <mitiru_mml/OpmDriver.hpp>
#include <mitiru_mml/OpnaDriver.hpp>
#include <mitiru_mml/Sn76489Driver.hpp>
#include <mitiru_mml/SynthPatch.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru_mml
{

/// @brief トラックが載るチップの種類
enum class ChipKind
{
	OpnaFm,     ///< YM2608 の FM (チャンネル 0〜5)
	OpnaSsg,    ///< YM2608 の SSG (チャンネル 6〜8)
	OpnaRhythm, ///< YM2608 の FM で合成するドラム (B S H F Y I のリズム MML)
	Opm,        ///< YM2151
	Opl,        ///< YM3526
	Opl2,       ///< YM3812
	Opl3,       ///< YMF262
	Sn76489,    ///< SN76489
	Ay8910,     ///< AY-3-8910 / YM2149
	Nes,        ///< NES の APU
	Synth,      ///< 減算シンセ (ModernSong が鳴らす)
	Drums,      ///< ドラムシンセ (ModernSong が鳴らす)
	Sampler,    ///< SF2 サンプラー (ModernSong が鳴らす)
};

namespace chip_catalog
{

[[nodiscard]] inline std::string lower(std::string_view text)
{
	std::string out(text);
	for (char& c : out)
	{
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return out;
}

[[nodiscard]] inline std::optional<int> toInt(std::string_view text)
{
	if (text.empty() || text.size() > 6)
	{
		return std::nullopt;
	}
	int value = 0;
	for (const char c : text)
	{
		if (c < '0' || c > '9')
		{
			return std::nullopt;
		}
		value = value * 10 + (c - '0');
	}
	return value;
}

[[nodiscard]] inline std::optional<ChipKind> kindFromName(std::string_view rawName)
{
	const std::string name = lower(rawName);
	struct Alias
	{
		const char* name;
		ChipKind kind;
	};
	static constexpr Alias ALIASES[] = {
		{"opna", ChipKind::OpnaFm}, {"opna-fm", ChipKind::OpnaFm}, {"ym2608", ChipKind::OpnaFm},
		{"opna-ssg", ChipKind::OpnaSsg}, {"ssg", ChipKind::OpnaSsg},
		{"opna-rhythm", ChipKind::OpnaRhythm}, {"rhythm", ChipKind::OpnaRhythm},
		{"opm", ChipKind::Opm}, {"ym2151", ChipKind::Opm},
		{"opl", ChipKind::Opl}, {"ym3526", ChipKind::Opl},
		{"opl2", ChipKind::Opl2}, {"ym3812", ChipKind::Opl2},
		{"opl3", ChipKind::Opl3}, {"ymf262", ChipKind::Opl3},
		{"sn76489", ChipKind::Sn76489}, {"psg", ChipKind::Sn76489}, {"sn", ChipKind::Sn76489},
		{"ay8910", ChipKind::Ay8910}, {"ay", ChipKind::Ay8910}, {"ym2149", ChipKind::Ay8910},
		{"nes", ChipKind::Nes}, {"apu", ChipKind::Nes}, {"2a03", ChipKind::Nes},
		{"synth", ChipKind::Synth},
		{"drums", ChipKind::Drums},
		{"sampler", ChipKind::Sampler}, {"sample", ChipKind::Sampler}, {"sf2", ChipKind::Sampler},
	};
	for (const auto& alias : ALIASES)
	{
		if (name == alias.name)
		{
			return alias.kind;
		}
	}
	return std::nullopt;
}

/// @brief 同じチップの実体を共有する種類は同じ番号になる (OPNA の FM、SSG、リズムは 1 個の YM2608)
[[nodiscard]] inline int chipGroup(ChipKind kind) noexcept
{
	switch (kind)
	{
	case ChipKind::OpnaFm:
	case ChipKind::OpnaSsg:
	case ChipKind::OpnaRhythm: return 0;
	default: return 1 + static_cast<int>(kind) - static_cast<int>(ChipKind::Opm);
	}
}

/// @brief 古いチップの Driver を作る。現代的なチップ (synth、drums、sampler) は nullptr
[[nodiscard]] inline std::unique_ptr<ChipDriver> makeDriver(ChipKind kind)
{
	switch (kind)
	{
	case ChipKind::OpnaFm:
	case ChipKind::OpnaSsg:
	case ChipKind::OpnaRhythm: return std::make_unique<OpnaDriver>();
	case ChipKind::Opm: return std::make_unique<OpmDriver>();
	case ChipKind::Opl: return std::make_unique<OplDriver>(OplVariant::Opl);
	case ChipKind::Opl2: return std::make_unique<OplDriver>(OplVariant::Opl2);
	case ChipKind::Opl3: return std::make_unique<OplDriver>(OplVariant::Opl3);
	case ChipKind::Sn76489: return std::make_unique<Sn76489Driver>();
	case ChipKind::Ay8910: return std::make_unique<Ay8910Driver>();
	case ChipKind::Nes: return std::make_unique<NesApuDriver>();
	case ChipKind::Synth:
	case ChipKind::Drums:
	case ChipKind::Sampler: return nullptr;
	}
	return nullptr;
}

/// @brief チップごとの音色の数 (番号の上限)。音色の無い種類は 0
[[nodiscard]] inline int voiceCount(ChipKind kind) noexcept
{
	switch (kind)
	{
	case ChipKind::OpnaFm: return 12;
	case ChipKind::Opm: return opm_presets::COUNT;
	case ChipKind::Opl:
	case ChipKind::Opl2:
	case ChipKind::Opl3: return opl_presets::COUNT;
	case ChipKind::Sn76489: return 2;
	case ChipKind::Ay8910: return 3;
	case ChipKind::Nes: return 4;
	case ChipKind::Synth: return SYNTH_PRESET_COUNT;
	case ChipKind::Sampler: return 128;
	default: return 0;
	}
}

/// @brief 音色の名前から番号を求める。数字だけの文字列は番号として読む。読めなければ、範囲外でも nullopt
[[nodiscard]] inline std::optional<int> voiceFromName(ChipKind kind, std::string_view rawText)
{
	if (const auto number = toInt(rawText))
	{
		return (*number < voiceCount(kind)) ? number : std::nullopt;
	}
	const std::string text = lower(rawText);
	const auto lookup = [&text](const auto& names) -> std::optional<int>
	{
		for (std::size_t i = 0; i < names.size(); ++i)
		{
			if (text == names[i])
			{
				return static_cast<int>(i);
			}
		}
		return std::nullopt;
	};
	switch (kind)
	{
	case ChipKind::OpnaFm:
	{
		static constexpr std::array<std::string_view, 12> NAMES = {"piano", "bell", "brass", "strings",
			"organ", "epiano", "bass", "flute", "koto", "synthlead", "vibes", "distguitar"};
		return lookup(NAMES);
	}
	case ChipKind::Opm:
		return opm_presets::findPreset(text) >= 0 ? std::optional<int>(opm_presets::findPreset(text)) : std::nullopt;
	case ChipKind::Opl:
	case ChipKind::Opl2:
	case ChipKind::Opl3:
		return opl_presets::findPreset(text) >= 0 ? std::optional<int>(opl_presets::findPreset(text)) : std::nullopt;
	case ChipKind::Sn76489:
	{
		static constexpr std::array<std::string_view, 2> NAMES = {"white", "periodic"};
		return lookup(NAMES);
	}
	case ChipKind::Ay8910:
	{
		static constexpr std::array<std::string_view, 3> NAMES = {"tone", "noise", "both"};
		return lookup(NAMES);
	}
	case ChipKind::Nes:
	{
		static constexpr std::array<std::string_view, 4> NAMES = {"duty50", "duty25", "duty12", "duty75"};
		if (const auto duty = lookup(NAMES))
		{
			return duty;
		}
		if (text == "long")
		{
			return 0;
		}
		if (text == "short")
		{
			return 1;
		}
		return std::nullopt;
	}
	case ChipKind::Synth:
	{
		const int index = synthPresetIndex(text);
		return index >= 0 ? std::optional<int>(index) : std::nullopt;
	}
	default:
		return std::nullopt;
	}
}

/// @brief チャンネルの名前か番号を読む
[[nodiscard]] inline std::optional<int> channelFromName(ChipKind kind, std::string_view rawText)
{
	if (const auto number = toInt(rawText))
	{
		return number;
	}
	const std::string text = lower(rawText);
	if (kind == ChipKind::Sn76489 && text == "noise")
	{
		return Sn76489Driver::NOISE_CHANNEL;
	}
	if (kind == ChipKind::Nes)
	{
		if (text == "pulse1") return NesApuDriver::PULSE_1;
		if (text == "pulse2") return NesApuDriver::PULSE_2;
		if (text == "triangle") return NesApuDriver::TRIANGLE;
		if (text == "noise") return NesApuDriver::NOISE;
	}
	if (kind == ChipKind::Ay8910 && text.size() == 1 && text[0] >= 'a' && text[0] <= 'c')
	{
		return text[0] - 'a';
	}
	return std::nullopt;
}

/// @brief ModernSong だけが鳴らすチップ (出力レートや SF2 を受けるので、ここでは Driver を作らない)
[[nodiscard]] inline bool isModernKind(ChipKind kind) noexcept
{
	return kind == ChipKind::Synth || kind == ChipKind::Drums || kind == ChipKind::Sampler;
}

/// @brief 旋律用のチャンネルの範囲 [first, last] (自動で割り当てる対象)
struct ChannelRange
{
	int first;
	int last;
};

[[nodiscard]] inline ChannelRange autoRange(ChipKind kind, bool hasRhythm) noexcept
{
	switch (kind)
	{
	case ChipKind::OpnaFm: return {OpnaDriver::FM_CHANNEL_BASE, OpnaDriver::FM_CHANNEL_BASE + (hasRhythm ? 3 : OpnaDriver::FM_CHANNELS) - 1};
	case ChipKind::OpnaSsg: return {OpnaDriver::SSG_CHANNEL_BASE, OpnaDriver::SSG_CHANNEL_BASE + OpnaDriver::SSG_CHANNELS - 1};
	case ChipKind::OpnaRhythm: return {OpnaDriver::DRUM_CHANNEL_BASE, OpnaDriver::DRUM_CHANNEL_BASE};
	case ChipKind::Opm: return {0, OpmDriver::CHANNELS - 1};
	case ChipKind::Opl:
	case ChipKind::Opl2: return {0, 8};
	case ChipKind::Opl3: return {0, 17};
	case ChipKind::Sn76489: return {0, Sn76489Driver::TONE_CHANNELS - 1};
	case ChipKind::Ay8910: return {0, Ay8910Driver::CHANNELS - 1};
	case ChipKind::Nes: return {NesApuDriver::PULSE_1, NesApuDriver::TRIANGLE};
	case ChipKind::Synth:
	case ChipKind::Drums:
	case ChipKind::Sampler: break;
	}
	return {0, -1};
}

[[nodiscard]] inline std::string trim(std::string_view text)
{
	std::size_t b = 0;
	std::size_t e = text.size();
	while (b < e && (text[b] == ' ' || text[b] == '\t' || text[b] == '\r'))
	{
		++b;
	}
	while (e > b && (text[e - 1] == ' ' || text[e - 1] == '\t' || text[e - 1] == '\r'))
	{
		--e;
	}
	return std::string(text.substr(b, e - b));
}

[[nodiscard]] inline std::vector<std::string> splitWords(std::string_view text)
{
	std::vector<std::string> words;
	std::size_t i = 0;
	while (i < text.size())
	{
		while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
		{
			++i;
		}
		const std::size_t start = i;
		while (i < text.size() && text[i] != ' ' && text[i] != '\t')
		{
			++i;
		}
		if (i > start)
		{
			words.emplace_back(text.substr(start, i - start));
		}
	}
	return words;
}

} // namespace chip_catalog

} // namespace mitiru_mml
