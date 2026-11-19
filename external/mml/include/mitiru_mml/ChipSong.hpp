#pragma once
/// @file ChipSong.hpp
/// @brief 曲のテキスト (#track の行) を読み、古いチップのトラックを MultiChipSequencer に載せて鳴らす
/// @details 読み取りは SongText と同じ (ModernSongScript と同じテキストの形)。MML のパーサー (MmlParser) には
///          手を入れず、`#track` の行を取り除いて、続く MML の行をそのままパーサーに渡す。`#track` が 1 つも
///          無いテキストは、全体が OPNA の FM トラック 1 本 (OpnaSequencer::addFmTrack(mml, 0) の
///          renderStereo() と 1 サンプルまで同じ出力) になる。OpnaSequencer 自体には手を入れていないので、
///          従来の render() の出力は変わらない。
///          ChipSong が鳴らすのは古いチップ (opna、opm、opl、opl2、opl3、sn76489、ay8910、nes) だけ。
///          synth、drums、sampler の行と、グループ、ダッキング、リミッター、ステムなどの指示は、
///          ModernSongScript が読む。ChipSong は、# の指示の行を ignored に残し、現代的なチップのトラックは
///          エラーにする (黙って捨てない)。
///
/// @code
/// const char* text =
///     "#track chip=opm voice=lead\n"
///     "T140 O5 L8 CDEFGAB>C\n"
///     "#track chip=sn76489 pan=right\n"
///     "O3 L4 CCGG\n"
///     "#track chip=opna-ssg\n"
///     "O4 L8 EEEE\n";
/// auto result = mitiru_mml::ChipSong::build(text);
/// if (result.ok())
/// {
///     auto stereo = result.sequencer->render();
/// }
/// @endcode
///
/// 行の書き方は SongText.hpp。ch は、省略するとそのチップの旋律用のチャンネルを宣言順に割り当てる。
/// noise や triangle などの名前も使える。

#include <mitiru_mml/ChipCatalog.hpp>
#include <mitiru_mml/MultiChipSequencer.hpp>
#include <mitiru_mml/SongText.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mitiru_mml
{

/// @brief テキストを読んだ結果 (チップを作る前の段階)
struct ChipSongParseResult
{
	std::vector<ChipTrackDecl> tracks;
	std::vector<std::string> errors;    ///< 読めなかった行の説明
	std::vector<std::string> ignored;   ///< 読み飛ばした `#` の行
};

/// @brief 組み立てた結果
struct ChipSongResult
{
	std::unique_ptr<MultiChipSequencer> sequencer; ///< errors が空でなければ nullptr
	std::vector<std::string> errors;
	std::vector<std::string> ignored;

	[[nodiscard]] bool ok() const noexcept
	{
		return sequencer != nullptr && errors.empty();
	}
};

/// @brief 曲のテキストを読んで、MultiChipSequencer を作る
class ChipSong
{
public:
	/// @brief テキストを `#track` の宣言の並びに読む (チップはまだ作らない)
	[[nodiscard]] static ChipSongParseResult parse(std::string_view text)
	{
		SongTextResult read = SongText::parse(text);
		ChipSongParseResult result;
		result.tracks = std::move(read.tracks);
		result.errors = std::move(read.errors);
		for (const auto& directive : read.directives)
		{
			result.ignored.push_back(directive.text);
		}
		return result;
	}

	/// @brief 読んだ結果からシーケンサーを作る。チャンネルが足りないなどの問題は errors に入り、sequencer は空になる
	[[nodiscard]] static ChipSongResult build(const ChipSongParseResult& parsed,
		const MultiChipSequencer::Options& options = MultiChipSequencer::Options{})
	{
		using namespace chip_catalog;
		ChipSongResult result;
		result.errors = parsed.errors;
		result.ignored = parsed.ignored;

		bool hasRhythm = false;
		for (const auto& t : parsed.tracks)
		{
			hasRhythm = hasRhythm || (t.kind == ChipKind::OpnaRhythm);
		}

		auto sequencer = std::make_unique<MultiChipSequencer>(options);
		std::array<int, 16> chipIndex;
		chipIndex.fill(-1);
		std::array<std::vector<int>, 16> usedChannels;

		for (const auto& t : parsed.tracks)
		{
			if (isModernKind(t.kind))
			{
				result.errors.push_back(SongText::prefix(t.line)
					+ "synth、drums、sampler のトラックは ChipSong では鳴らせません (ModernSongScript で読んでください)");
				continue;
			}
			const int group = chipGroup(t.kind);
			if (chipIndex[static_cast<std::size_t>(group)] < 0)
			{
				chipIndex[static_cast<std::size_t>(group)] = sequencer->addChip(makeDriver(t.kind));
			}
			const int chip = chipIndex[static_cast<std::size_t>(group)];
			auto& used = usedChannels[static_cast<std::size_t>(group)];

			const int channel = pickChannel(t, hasRhythm, used, result.errors);
			if (channel < 0)
			{
				continue;
			}
			used.push_back(channel);

			std::string mml;
			if (t.pan >= 0)
			{
				mml += "P" + std::to_string(t.pan) + " ";
			}
			if (t.volume >= 0)
			{
				mml += "V" + std::to_string(t.volume) + " ";
			}
			mml += t.mml;

			try
			{
				if (t.kind == ChipKind::OpnaRhythm)
				{
					sequencer->addRhythmTrack(chip, channel, t.mml);
				}
				else
				{
					sequencer->addTrack(chip, channel, mml, t.voice);
				}
			}
			catch (const std::invalid_argument& e)
			{
				result.errors.push_back(SongText::prefix(t.line) + e.what());
			}
		}

		if (result.errors.empty())
		{
			result.sequencer = std::move(sequencer);
		}
		return result;
	}

	/// @brief テキストから直接作る
	[[nodiscard]] static ChipSongResult build(std::string_view text,
		const MultiChipSequencer::Options& options = MultiChipSequencer::Options{})
	{
		return build(parse(text), options);
	}

private:
	/// @brief トラックのチャンネルを決める。決められなければ -1 を返し、errors に理由を足す
	[[nodiscard]] static int pickChannel(const ChipTrackDecl& track, bool hasRhythm, const std::vector<int>& used,
		std::vector<std::string>& errors)
	{
		using namespace chip_catalog;
		const std::string prefix = std::to_string(track.line) + " 行目: ";
		const auto isUsed = [&used](int c) { return std::find(used.begin(), used.end(), c) != used.end(); };
		if (track.channel >= 0)
		{
			// OPNA の FM 3〜5 は、リズムのトラックがあるとドラムが使う
			if (hasRhythm && track.kind == ChipKind::OpnaFm && track.channel >= 3 && track.channel < OpnaDriver::SSG_CHANNEL_BASE)
			{
				errors.push_back(prefix + "リズムのトラックがあるので、FM のチャンネル 3〜5 は使えません");
				return -1;
			}
			if (isUsed(track.channel))
			{
				errors.push_back(prefix + "チャンネル " + std::to_string(track.channel) + " はすでに使われています");
				return -1;
			}
			return track.channel;
		}
		const ChannelRange range = autoRange(track.kind, hasRhythm);
		for (int c = range.first; c <= range.last; ++c)
		{
			if (!isUsed(c))
			{
				return c;
			}
		}
		errors.push_back(prefix + "このチップの旋律用のチャンネルが足りません (最大 "
			+ std::to_string(range.last - range.first + 1) + " 本)");
		return -1;
	}
};

} // namespace mitiru_mml
