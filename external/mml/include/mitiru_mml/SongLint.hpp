#pragma once
/// @file SongLint.hpp
/// @brief 曲のテキストの検査。音域外の音、音量の飽和、ループ点の不整合、未定義のマクロや変数を、行と列つきで報告する
/// @details SongText と同じ読み取りを通すので、検査するのはマクロと変数を展開したあとの MML で、
///          報告する位置は、展開される前のテキストの行と列 (マクロの中の問題は、呼び出した位置)。
///          検査だけで、曲の読み方も鳴らし方も変えない。ビルダーが黙って丸める値 (オクターブ 1〜8、V と v の 15、
///          テンポ 20〜300、音域のチップごとの限界) を、書いたとおりに鳴らないものとして知らせる。
///
///          音域の限界は、各 Driver の音程の変換 (keyToPeriod など) が選んだ値を周波数に戻して、
///          目標との差が 50 セントを超える音 (範囲の端で止まった音と、細かさが足りない音) とする。

#include <mitiru_mml/ChipCatalog.hpp>
#include <mitiru_mml/MmlEventBuilder.hpp>
#include <mitiru_mml/MmlParser.hpp>
#include <mitiru_mml/SongText.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace mitiru_mml
{

/// @brief 報告の重さ。Error は書いたとおりに鳴らないか読み取れないもの、Warning は鳴るが意図と違う可能性があるもの
enum class DiagnosticSeverity
{
	Warning,
	Error,
};

struct SongDiagnostic
{
	DiagnosticSeverity severity = DiagnosticSeverity::Warning;
	int line = 0;       ///< 1 から。0 は位置なし
	int column = 0;     ///< 1 から。0 は列なし
	std::string message;

	/// @brief 「N 行目 M 列: ...」の形
	[[nodiscard]] std::string text() const
	{
		return SongProblem{line, column, message}.text();
	}
};

struct TrackTicks
{
	std::string name;
	long totalTicks = 0;      ///< 全部を鳴らす長さ (四分音符 48 tick。ループは繰り返しを含む)
	long loopTick = -1;       ///< 最初の $ の位置。無ければ -1
};

struct SongValidation
{
	std::vector<SongDiagnostic> diagnostics;
	std::vector<TrackTicks> tracks;

	/// @brief 報告を追加する。同じ位置の同じ報告は、繰り返しやマクロの展開で複数回出ても 1 つにまとめる
	void add(SongDiagnostic diagnostic)
	{
		const auto key = std::make_tuple(static_cast<int>(diagnostic.severity), diagnostic.line, diagnostic.column, diagnostic.message);
		if (m_seen.insert(key).second)
		{
			diagnostics.push_back(std::move(diagnostic));
		}
	}

	[[nodiscard]] bool hasErrors() const noexcept
	{
		return std::any_of(diagnostics.begin(), diagnostics.end(), [](const SongDiagnostic& d)
			{
				return d.severity == DiagnosticSeverity::Error;
			});
	}

	[[nodiscard]] std::size_t count(DiagnosticSeverity severity) const noexcept
	{
		return static_cast<std::size_t>(std::count_if(diagnostics.begin(), diagnostics.end(), [severity](const SongDiagnostic& d)
			{
				return d.severity == severity;
			}));
	}

	/// @brief 指定した重さの報告を「N 行目 M 列: ...」の形で返す
	[[nodiscard]] std::vector<std::string> texts(DiagnosticSeverity severity) const
	{
		std::vector<std::string> out;
		for (const SongDiagnostic& d : diagnostics)
		{
			if (d.severity == severity)
			{
				out.push_back(d.text());
			}
		}
		return out;
	}

private:
	std::set<std::tuple<int, int, int, std::string>> m_seen;
};

/// @brief チップごとに正しい音程で鳴らせる MIDI ノートの範囲
struct NoteRange
{
	int low = 0;
	int high = 127;
};

class SongLint
{
public:
	/// @brief 周波数の誤差が 50 セントを超える音を音域外とする
	static constexpr double RANGE_TOLERANCE_CENTS = 50.0;

	[[nodiscard]] static SongValidation check(std::string_view text)
	{
		SongValidation result;
		const SongTextResult song = SongText::parse(text);
		for (const SongProblem& problem : song.problems)
		{
			result.add({DiagnosticSeverity::Error, problem.line, problem.column, problem.message});
		}
		for (const ChipTrackDecl& track : song.tracks)
		{
			checkTrack(song, track, result);
		}
		checkLoopPoints(song, result);
		checkUnused(song, result);
		std::stable_sort(result.diagnostics.begin(), result.diagnostics.end(), [](const SongDiagnostic& a, const SongDiagnostic& b)
			{
				return std::tie(a.line, a.column) < std::tie(b.line, b.column);
			});
		return result;
	}

	/// @brief チップが正しい音程で鳴らせる範囲。確認しないチップは nullopt
	[[nodiscard]] static std::optional<NoteRange> noteRange(const ChipTrackDecl& track)
	{
		switch (track.kind)
		{
		case ChipKind::OpnaFm:
		case ChipKind::OpnaSsg:
		case ChipKind::Opm:
		case ChipKind::Opl:
		case ChipKind::Opl2:
		case ChipKind::Opl3:
			break;
		case ChipKind::Sn76489:
			if (track.channel == Sn76489Driver::NOISE_CHANNEL) return std::nullopt;
			break;
		case ChipKind::Ay8910:
			if (track.voiceGiven && track.voice == 1) return std::nullopt; // ノイズだけ
			break;
		case ChipKind::Nes:
			if (track.channel == NesApuDriver::NOISE) return std::nullopt;
			break;
		default:
			return std::nullopt;
		}
		const bool triangle = (track.kind == ChipKind::Nes && track.channel == NesApuDriver::TRIANGLE);
		return scanRange(track.kind, triangle);
	}

	/// @brief MIDI ノート番号を MML の表記 (O4 C+) に変換する
	[[nodiscard]] static std::string noteName(int midi)
	{
		static constexpr std::array<const char*, 12> NAMES = {"C", "C+", "D", "D+", "E", "F", "F+", "G", "G+", "A", "A+", "B"};
		const int octave = midi / 12 - 1;
		return "O" + std::to_string(octave) + " " + NAMES[static_cast<std::size_t>(midi % 12)];
	}

private:
	/// @brief 音のコマンドが展開前のテキストにあった行と列
	struct Where
	{
		int line = 0;
		int column = 0;
		int macro = -1;
	};

	[[nodiscard]] static double targetHz(int key) noexcept
	{
		return 440.0 * std::pow(2.0, (static_cast<double>(key) - 69.0) / 12.0);
	}

	/// @brief Driver が key に選ぶ値を周波数に戻す。範囲の端の値になった音は誤差が大きくなる
	[[nodiscard]] static double playedHz(ChipKind kind, bool triangle, int key)
	{
		switch (kind)
		{
		case ChipKind::OpnaFm:
		{
			const auto fb = OpnaDriver::noteToFnumBlock(key);
			if (fb.fnum == 0)
			{
				return 0.0;
			}
			return static_cast<double>(fb.fnum) * static_cast<double>(OpnaDriver::CLOCK)
				* std::pow(2.0, static_cast<double>(fb.block) - 1.0) / (144.0 * static_cast<double>(1 << 20));
		}
		case ChipKind::OpnaSsg:
		{
			const double effClock = static_cast<double>(OpnaDriver::CLOCK / 4);
			const double period = std::floor(effClock / (16.0 * targetHz(key)));
			if (period < 1.0 || period > 4095.0)
			{
				return 0.0;
			}
			return effClock / (16.0 * period);
		}
		case ChipKind::Opm:
			return (key >= 13 && key <= 108) ? targetHz(key) : 0.0; // OPM は C# から数える 96 音
		case ChipKind::Opl:
		case ChipKind::Opl2:
		case ChipKind::Opl3:
			return OplDriver::fnumBlockToHz(OplDriver::keyToFnumBlock(key));
		case ChipKind::Sn76489:
			return Sn76489Driver::periodToHz(Sn76489Driver::keyToPeriod(key));
		case ChipKind::Ay8910:
			return Ay8910Driver::periodToHz(Ay8910Driver::keyToPeriod(key));
		case ChipKind::Nes:
			return NesApuDriver::timerToHz(NesApuDriver::keyToTimer(key, triangle), triangle);
		default:
			return targetHz(key);
		}
	}

	[[nodiscard]] static NoteRange scanRange(ChipKind kind, bool triangle)
	{
		int low = -1;
		int high = -1;
		for (int key = 0; key <= 127; ++key)
		{
			const double hz = playedHz(kind, triangle, key);
			if (hz <= 0.0)
			{
				continue;
			}
			const double cents = std::abs(1200.0 * std::log2(hz / targetHz(key)));
			if (cents <= RANGE_TOLERANCE_CENTS)
			{
				low = (low < 0) ? key : low;
				high = key;
			}
		}
		return {low < 0 ? 0 : low, high < 0 ? 127 : high};
	}

	[[nodiscard]] static Where whereOf(const ChipTrackDecl& track, const MmlCommand& cmd)
	{
		if (cmd.offset >= 0 && static_cast<std::size_t>(cmd.offset) < track.origins.size())
		{
			const SourceOrigin& o = track.origins[static_cast<std::size_t>(cmd.offset)];
			return {o.line, o.column, o.macro};
		}
		return {track.line, 0, -1};
	}

	static void report(const SongTextResult& song, SongValidation& out, DiagnosticSeverity severity, const Where& where,
		std::string message)
	{
		if (where.macro >= 0 && static_cast<std::size_t>(where.macro) < song.definitions.macros().size())
		{
			message += " (マクロ '" + song.definitions.macros()[static_cast<std::size_t>(where.macro)].name + "' の中)";
		}
		out.add({severity, where.line, where.column, std::move(message)});
	}

	/// @brief 命令列を実際に鳴る順序で 1 回たどり、音域、音量、テンポ、長さを調べる
	/// @details 繰り返しの前後でオクターブや L が変わる曲 (`O4 [>C]2 C`) も、ループを繰り返して調べる。状態が変わらなくなったら、残りの回は長さだけを掛ける。状態にはオクターブ、L、グレースの内外を含む。同じ位置の同じ報告は 1 つにまとめる。
	class Walk
	{
	public:
		Walk(const SongTextResult& song, const ChipTrackDecl& track, const CommandList& commands, SongValidation& out)
			: m_song(song), m_track(track), m_commands(commands), m_out(out), m_range(noteRange(track)),
			  m_extended(chip_catalog::isModernKind(track.kind))
		{
			matchLoops();
		}

		/// @return トラックの長さ (tick)
		long run(long& loopTick)
		{
			walk(0, m_commands.size());
			loopTick = m_loopTick;
			return m_tick;
		}

	private:
		struct State
		{
			int octave;
			int length;
			bool grace;
			bool operator==(const State& other) const noexcept
			{
				return octave == other.octave && length == other.length && grace == other.grace;
			}
		};

		static constexpr long MAX_STEPS = 5000000;

		[[nodiscard]] State state() const noexcept
		{
			return {m_octave, m_defaultLength, m_inGrace};
		}

		/// @brief LoopStart ごとに対応する LoopEnd の添字を求める。対応方法は MmlEventBuilder::expandLoops と同じ
		void matchLoops()
		{
			m_endOf.assign(m_commands.size(), -1);
			std::vector<std::size_t> stack;
			for (std::size_t i = 0; i < m_commands.size(); ++i)
			{
				if (m_commands[i].type == CommandType::LoopStart)
				{
					stack.push_back(i);
				}
				else if (m_commands[i].type == CommandType::LoopEnd && !stack.empty())
				{
					m_endOf[stack.back()] = static_cast<long>(i);
					stack.pop_back();
				}
			}
		}

		void warn(const MmlCommand& cmd, std::string message)
		{
			report(m_song, m_out, DiagnosticSeverity::Warning, whereOf(m_track, cmd), std::move(message));
		}

		void walk(std::size_t from, std::size_t to)
		{
			for (std::size_t i = from; i < to; ++i)
			{
				if (++m_steps > MAX_STEPS)
				{
					return;
				}
				const MmlCommand& cmd = m_commands[i];
				if (cmd.type == CommandType::LoopStart && m_endOf[i] >= 0)
				{
					repeat(i, static_cast<std::size_t>(m_endOf[i]));
					i = static_cast<std::size_t>(m_endOf[i]);
					continue;
				}
				visit(cmd);
			}
		}

		void repeat(std::size_t open, std::size_t close)
		{
			const int count = std::max(1, m_commands[close].value);
			for (int round = 0; round < count; ++round)
			{
				const State before = state();
				const long startTick = m_tick;
				walk(open + 1, close);
				if (state() == before && round + 1 < count)
				{
					m_tick += (m_tick - startTick) * static_cast<long>(count - round - 1);
					return;
				}
			}
		}

		void visit(const MmlCommand& cmd)
		{
			switch (cmd.type)
			{
			case CommandType::Tempo:
				if (cmd.value < 20 || cmd.value > 300)
				{
					warn(cmd, "テンポ T" + std::to_string(cmd.value) + " は 20〜300 の外です (" + std::to_string(std::clamp(cmd.value, 20, 300)) + " に丸められます)");
				}
				break;
			case CommandType::Octave:
				if (cmd.value < 1 || cmd.value > 8)
				{
					warn(cmd, "O" + std::to_string(cmd.value) + " はオクターブの範囲 1〜8 の外です (" + std::to_string(std::clamp(cmd.value, 1, 8)) + " に丸められます)");
				}
				m_octave = std::clamp(cmd.value, 1, 8);
				break;
			case CommandType::OctaveUp:
				if (m_octave >= 8)
				{
					warn(cmd, "> でオクターブ 8 より上へは上がれません (8 のままです)");
				}
				m_octave = std::min(8, m_octave + 1);
				break;
			case CommandType::OctaveDown:
				if (m_octave <= 1)
				{
					warn(cmd, "< でオクターブ 1 より下へは下がれません (1 のままです)");
				}
				m_octave = std::max(1, m_octave - 1);
				break;
			case CommandType::Length:
				m_defaultLength = std::max(1, cmd.value);
				break;
			case CommandType::Volume:
			case CommandType::Velocity:
				if (cmd.value > 15)
				{
					warn(cmd, std::string(cmd.type == CommandType::Volume ? "V" : "v") + std::to_string(cmd.value)
						+ " は音量の最大 15 を超えて飽和します (15 に丸められます)");
				}
				break;
			case CommandType::GraceStart:
				m_inGrace = true;
				break;
			case CommandType::GraceEnd:
				m_inGrace = false;
				break;
			case CommandType::Note:
				checkNote(m_song, m_track, cmd, m_octave, m_range, m_extended, m_out);
				if (!m_inGrace)
				{
					m_tick += lengthTicks(cmd, m_defaultLength);
				}
				break;
			case CommandType::Rest:
				m_tick += lengthTicks(cmd, m_defaultLength);
				break;
			case CommandType::LoopPoint:
				if (m_loopTick < 0)
				{
					m_loopTick = m_tick;
				}
				break;
			case CommandType::Param:
				if (!m_extended)
				{
					warn(cmd, "X のコマンドは、このトラックのチップでは使われません (読み捨てられます)");
				}
				else
				{
					checkParam(m_song, m_track, cmd, m_out);
				}
				break;
			default:
				break;
			}
		}

		const SongTextResult& m_song;
		const ChipTrackDecl& m_track;
		const CommandList& m_commands;
		SongValidation& m_out;
		std::optional<NoteRange> m_range;
		bool m_extended;
		std::vector<long> m_endOf;
		int m_octave = 4;
		int m_defaultLength = 4;
		bool m_inGrace = false;
		long m_tick = 0;
		long m_loopTick = -1;
		long m_steps = 0;
	};

	/// @brief ループを繰り返す前の命令列で、ループの括弧と $ の並びを調べる。繰り返しの数と位置もここで調べる
	static void checkLoopSyntax(const SongTextResult& song, const ChipTrackDecl& track, const CommandList& commands,
		SongValidation& out)
	{
		std::vector<const MmlCommand*> open;
		int points = 0;
		for (const MmlCommand& cmd : commands)
		{
			const Where where = whereOf(track, cmd);
			switch (cmd.type)
			{
			case CommandType::LoopStart:
				open.push_back(&cmd);
				break;
			case CommandType::LoopEnd:
				if (open.empty())
				{
					report(song, out, DiagnosticSeverity::Error, where, "] に対応する [ がありません (読み捨てられます)");
					break;
				}
				open.pop_back();
				if (cmd.value <= 1)
				{
					report(song, out, DiagnosticSeverity::Warning, where,
						"]" + std::to_string(cmd.value) + " は繰り返しになりません (本体は 1 回だけ鳴ります)");
				}
				break;
			case CommandType::LoopPoint:
				++points;
				if (!open.empty())
				{
					report(song, out, DiagnosticSeverity::Warning, where,
						"ループ点 $ が [ ] の中にあります (繰り返しのたびに $ が増え、使われるのは最初の 1 つだけです)");
				}
				if (points > 1)
				{
					report(song, out, DiagnosticSeverity::Warning, where, "ループ点 $ が 2 つ目です (使われるのは 1 つ目の $ だけです)");
				}
				break;
			default:
				break;
			}
		}
		for (const MmlCommand* unclosed : open)
		{
			report(song, out, DiagnosticSeverity::Error, whereOf(track, *unclosed),
				"[ に対応する ] がありません (閉じないループは 1 回だけ鳴ります)");
		}
	}

	static void checkTrack(const SongTextResult& song, const ChipTrackDecl& track, SongValidation& out)
	{
		TrackTicks summary;
		summary.name = track.name;
		if (!track.rhythm)
		{
			// リズムの文法は B S H の並びだけを読むため、音符の検査は当てはまらない
			const CommandList commands = MmlParser::parse(track.mml);
			checkLoopSyntax(song, track, commands, out);
			Walk walk(song, track, commands, out);
			summary.totalTicks = walk.run(summary.loopTick);
		}
		out.tracks.push_back(summary);
	}

	[[nodiscard]] static long lengthTicks(const MmlCommand& cmd, int defaultLength)
	{
		const int length = (cmd.duration > 0) ? cmd.duration : defaultLength;
		return MmlEventBuilder::lengthToTicks(length, cmd.dotted);
	}

	static void checkNote(const SongTextResult& song, const ChipTrackDecl& track, const MmlCommand& cmd, int octave,
		const std::optional<NoteRange>& range, bool extended, SongValidation& out)
	{
		const Where where = whereOf(track, cmd);
		if (cmd.extra != 0 && !extended)
		{
			report(song, out, DiagnosticSeverity::Warning, where, "和音は、このトラックのチップでは最初の音だけが鳴ります");
		}
		if (!range)
		{
			return;
		}
		const int midi = (octave + 1) * 12 + cmd.value;
		if (midi < range->low || midi > range->high)
		{
			report(song, out, DiagnosticSeverity::Warning, where,
				"音 " + noteName(midi) + " (MIDI " + std::to_string(midi) + ") は、このチップの音域 "
				+ noteName(range->low) + "〜" + noteName(range->high) + " の外です (音程が半音の半分以上ずれます)");
		}
	}

	static void checkParam(const SongTextResult& song, const ChipTrackDecl& track, const MmlCommand& cmd, SongValidation& out)
	{
		const ModernParamInfo* info = findModernParam(cmd.value);
		if (info == nullptr || info->choices != nullptr)
		{
			return;
		}
		const auto outside = [info](int v)
		{
			return v != modern_param::FROM_CURRENT && (v < info->minValue || v > info->maxValue);
		};
		for (const int value : {cmd.extra, cmd.extra2})
		{
			if (outside(value))
			{
				report(song, out, DiagnosticSeverity::Warning, whereOf(track, cmd),
					std::string("X") + info->name + " の値 " + std::to_string(value) + " は "
					+ std::to_string(info->minValue) + "〜" + std::to_string(info->maxValue) + " の外です (範囲に丸められます)");
				return;
			}
		}
	}

	static void checkLoopPoints(const SongTextResult& song, SongValidation& out)
	{
		const TrackTicks* first = nullptr;
		for (const TrackTicks& t : out.tracks)
		{
			if (t.loopTick >= 0 && (first == nullptr || t.loopTick < first->loopTick))
			{
				first = &t; // 曲全体が戻るのは、いちばん早い $。同じ位置なら先のトラック
			}
		}
		if (first == nullptr)
		{
			return;
		}
		std::vector<SongDiagnostic> found;
		for (std::size_t i = 0; i < out.tracks.size(); ++i)
		{
			const TrackTicks& t = out.tracks[i];
			const ChipTrackDecl& decl = song.tracks[i];
			if (decl.rhythm)
			{
				continue;
			}
			if (t.loopTick < 0)
			{
				found.push_back({DiagnosticSeverity::Warning, decl.line, 0,
					"トラック '" + t.name + "' に $ がありません (トラック '" + first->name + "' の $ で曲全体が戻ります)"});
			}
			else if (t.loopTick != first->loopTick)
			{
				found.push_back({DiagnosticSeverity::Warning, decl.line, 0,
					"トラック '" + t.name + "' の $ は " + std::to_string(t.loopTick) + " tick で、トラック '" + first->name + "' の "
					+ std::to_string(first->loopTick) + " tick と違います (曲が戻るのは、いちばん早い $ です)"});
			}
			if (t.totalTicks != first->totalTicks)
			{
				found.push_back({DiagnosticSeverity::Warning, decl.line, 0,
					"トラック '" + t.name + "' の長さ " + std::to_string(t.totalTicks) + " tick が、トラック '" + first->name + "' の "
					+ std::to_string(first->totalTicks) + " tick と違います (ループすると、戻るたびに音がずれます)"});
			}
		}
		for (SongDiagnostic& d : found)
		{
			out.add(std::move(d));
		}
	}

	static void checkUnused(const SongTextResult& song, SongValidation& out)
	{
		for (const MacroDef& m : song.definitions.macros())
		{
			if (m.uses == 0)
			{
				out.add({DiagnosticSeverity::Warning, m.at.line, m.at.column,
					"マクロ '" + m.name + "' は使われていません"});
			}
		}
		for (const VariableDef& v : song.definitions.variables())
		{
			if (v.uses == 0)
			{
				out.add({DiagnosticSeverity::Warning, v.at.line, v.at.column,
					"変数 '" + v.name + "' は使われていません"});
			}
		}
	}
};

} // namespace mitiru_mml
