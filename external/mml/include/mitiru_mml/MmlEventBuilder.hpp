#pragma once
/// @file MmlEventBuilder.hpp
/// @brief MML のコマンド列を、チップに依存しないタイムラインのイベントに変える
/// @details チャンネルの能力 (ChannelCaps) だけを見て表現を選ぶ。FM か SSG かといったチップの
///          知識は持たない。従来 OpnaSequencer が持っていた変換をチップ非依存にしたもの。

#include <mitiru_mml/ChipDriver.hpp>
#include <mitiru_mml/MmlParser.hpp>
#include <mitiru_mml/MmlTypes.hpp>
#include <mitiru_mml/ModernTrackState.hpp>
#include <mitiru_mml/Timeline.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace mitiru_mml
{

/// @brief トラック 1 本の宛先
struct TrackTarget
{
	std::uint32_t track = 0;   ///< トラック番号 (イベントの順序に使う)
	std::uint32_t chip = 0;    ///< 宛先のチップ番号
	int channel = 0;           ///< 宛先のチャンネル番号 (Driver が決めた連番)
	ChannelCaps caps;          ///< そのチャンネルの能力
	bool extended = false;     ///< 拡張文法 (@n の音色、X、和音、アルペジオ、Q) を有効にする。caps.extendedSyntax が true のチャンネルでは自動で有効
};

/// @brief MML からイベント列を作る
class MmlEventBuilder
{
public:
	/// @brief tick の基準 (四分音符あたり)
	static constexpr int TICKS_PER_QUARTER = static_cast<int>(TickClock::TICKS_PER_QUARTER);

	/// @brief ループコマンド ([ ]N) を展開する
	[[nodiscard]] static CommandList expandLoops(const CommandList& cmds)
	{
		CommandList result;
		std::vector<std::size_t> loopStack;

		for (std::size_t i = 0; i < cmds.size(); ++i)
		{
			if (cmds[i].type == CommandType::LoopStart)
			{
				loopStack.push_back(result.size());
			}
			else if (cmds[i].type == CommandType::LoopEnd)
			{
				if (!loopStack.empty())
				{
					const std::size_t startIdx = loopStack.back();
					loopStack.pop_back();
					const int repeatCount = std::max(1, cmds[i].value) - 1;
					const std::size_t bodyLen = result.size() - startIdx;
					for (int r = 0; r < repeatCount; ++r)
					{
						for (std::size_t j = 0; j < bodyLen; ++j)
						{
							result.push_back(result[startIdx + j]);
						}
					}
				}
			}
			else
			{
				result.push_back(cmds[i]);
			}
		}
		return result;
	}

	/// @brief 音長値を tick 数に変える
	/// @param lengthVal L 値 (1=全音符、4=四分音符、8=八分音符 ...)
	[[nodiscard]] static int lengthToTicks(int lengthVal, bool dotted) noexcept
	{
		if (lengthVal <= 0) lengthVal = 4;
		int ticks = (TICKS_PER_QUARTER * 4) / lengthVal;
		if (dotted)
		{
			ticks = ticks * 3 / 2;
		}
		return std::max(1, ticks);
	}

	/// @brief 旋律用の MML (音符、休符、T O L V ...) をイベント列にする
	/// @details 同じ tick のイベントの並びは、作った順 (order) が決める。
	[[nodiscard]] static std::vector<SeqEvent> fromMelodic(const CommandList& commands, const TrackTarget& target)
	{
		Sink sink(target);
		int currentTick = 0;
		int octave = 4;
		int defaultLength = 4;
		int volume = 12;
		int velocity = -1;            // ノート毎ベロシティ。-1 は未設定
		bool inCrescendo = false;
		bool inDecrescendo = false;
		int crescStartVol = 0;
		int crescNoteCount = 0;
		bool inGrace = false;
		std::vector<MmlCommand> graceNotes;
		int prevMidiNote = -1;        // レガートとポルタメントの起点
		int ssgEnvShape = -1;         // ハードウェアエンベロープの形状。-1 は未設定
		int ssgEnvPeriod = 0;
		const bool extended = target.extended || target.caps.extendedSyntax;
		ModernTrackState ext(mixSeed(target.track, target.chip, static_cast<std::uint32_t>(target.channel)));
		const auto emitRaw = [&sink](int tick, SeqEventType type, int channel, int a, int b, int c)
		{
			sink.emit(tick, type, channel, a, b, c);
		};

		for (std::size_t i = 0; i < commands.size(); ++i)
		{
			const auto& cmd = commands[i];

			switch (cmd.type)
			{
			case CommandType::Tempo:
				sink.emit(currentTick, SeqEventType::Tempo, 0, std::clamp(cmd.value, 20, 300));
				break;

			case CommandType::Octave:
				octave = std::clamp(cmd.value, 1, 8);
				break;

			case CommandType::OctaveUp:
				octave = std::min(8, octave + 1);
				break;

			case CommandType::OctaveDown:
				octave = std::max(1, octave - 1);
				break;

			case CommandType::Length:
				defaultLength = std::max(1, cmd.value);
				break;

			case CommandType::Volume:
				volume = std::clamp(cmd.value, 0, 15);
				sink.emit(currentTick, SeqEventType::Volume, sink.channel(), volume);
				break;

			case CommandType::Velocity:
				velocity = std::clamp(cmd.value, 0, 15);
				break;

			case CommandType::CrescStart:
				inCrescendo = true;
				inDecrescendo = false;
				crescStartVol = volume;
				crescNoteCount = 0;
				break;

			case CommandType::DecrescStart:
				inDecrescendo = true;
				inCrescendo = false;
				crescStartVol = volume;
				crescNoteCount = 0;
				break;

			case CommandType::GraceStart:
				inGrace = true;
				graceNotes.clear();
				break;

			case CommandType::GraceEnd:
				inGrace = false;
				break;

			case CommandType::SsgEnvelope:
				ssgEnvShape = cmd.value;
				ssgEnvPeriod = cmd.extra;
				break;

			case CommandType::Pan:
				sink.emit(currentTick, SeqEventType::Pan, sink.channel(), cmd.value);
				break;

			case CommandType::LoopPoint:
				sink.emit(currentTick, SeqEventType::LoopPoint, 0);
				break;

			case CommandType::Waveform:
				if (extended)
				{
					sink.emit(currentTick, SeqEventType::Voice, sink.channel(), cmd.value);
				}
				break;

			case CommandType::Param:
				if (extended)
				{
					ext.applyParam(emitRaw, currentTick, sink.channel(), cmd);
				}
				break;

			case CommandType::Quantize:
				if (extended)
				{
					ext.setQuantize(cmd.value);
				}
				break;

			case CommandType::Note:
			{
				// グレースノート収集中は一時バッファに貯める
				if (inGrace)
				{
					graceNotes.push_back(cmd);
					break;
				}

				const int len = (cmd.duration > 0) ? cmd.duration : defaultLength;
				int totalTicks = lengthToTicks(len, cmd.dotted);

				// レガート: タイが付いていて、次のコマンドが異なる音程の音符
				bool isLegato = false;
				if (cmd.tied && i + 1 < commands.size())
				{
					const auto& next = commands[i + 1];
					if (next.type == CommandType::Note && next.value != cmd.value)
					{
						isLegato = true;
					}
				}

				// 同じ音程のタイは、持続時間を足していく
				if (!isLegato)
				{
					std::size_t j = i;
					while (j < commands.size() && commands[j].tied && j + 1 < commands.size())
					{
						++j;
						if (commands[j].type == CommandType::Note)
						{
							if (commands[j].value != cmd.value)
							{
								--j;
								isLegato = true;
								break;
							}
							const int tieLen = (commands[j].duration > 0)
								? commands[j].duration : defaultLength;
							totalTicks += lengthToTicks(tieLen, commands[j].dotted);
						}
					}
					if (!isLegato) i = j;
				}

				const int midiNote = (octave + 1) * 12 + cmd.value;

				// クレッシェンド/デクレッシェンド中は音量を段階的に変える
				if (inCrescendo || inDecrescendo)
				{
					++crescNoteCount;
					const int targetVol = inCrescendo ? 15 : 0;
					int curVol = crescStartVol + (targetVol - crescStartVol)
						* crescNoteCount / std::max(1, crescNoteCount + 2);
					curVol = std::clamp(curVol, 0, 15);
					sink.emit(currentTick, SeqEventType::Volume, sink.channel(), curVol);
				}

				// ノート毎ベロシティは、その 1 音だけ音量を変える
				if (velocity >= 0)
				{
					sink.emit(currentTick, SeqEventType::Volume, sink.channel(), velocity);
					velocity = -1;
				}

				// グレースノートは、直前に貯めた音を 32 分音符で先に鳴らす
				int graceTickTotal = 0;
				if (!graceNotes.empty())
				{
					const int graceLen = 32;
					for (const auto& gn : graceNotes)
					{
						const int gTicks = lengthToTicks(graceLen, false);
						const int gMidi = (octave + 1) * 12 + gn.value;
						sink.emit(currentTick, SeqEventType::NoteOn, sink.channel(), gMidi, volume);
						sink.emit(currentTick + std::max(1, gTicks - 1), SeqEventType::NoteOff, sink.channel());
						currentTick += gTicks;
						graceTickTotal += gTicks;
					}
					graceNotes.clear();
				}

				const int mainTicks = std::max(1, totalTicks - graceTickTotal);
				const int gateTicks = std::max(1, mainTicks - 1);
				const bool canSlide = sink.caps().pitchSlide && prevMidiNote >= 0;

				if (cmd.portamento && canSlide)
				{
					// ポルタメントは、音程だけを変えるイベントの列で表す (キーオンはしない)
					const int steps = 6;
					const int slideTicks = std::min(gateTicks / 2, steps * 2);
					const int tickPerStep = std::max(1, slideTicks / steps);
					for (int s = 0; s < steps; ++s)
					{
						const float t = static_cast<float>(s + 1) / static_cast<float>(steps);
						const int interpNote = prevMidiNote
							+ static_cast<int>(t * static_cast<float>(midiNote - prevMidiNote));
						sink.emit(currentTick + s * tickPerStep, SeqEventType::Pitch, sink.channel(), interpNote);
					}
				}
				else if (isLegato && canSlide)
				{
					sink.emit(currentTick, SeqEventType::Pitch, sink.channel(), midiNote);
				}
				else if (extended)
				{
					ext.emitNote(emitRaw, currentTick, sink.channel(), midiNote, cmd.extra, volume, mainTicks);
				}
				else
				{
					if (sink.caps().hardwareEnvelope && ssgEnvShape >= 0)
					{
						sink.emit(currentTick, SeqEventType::Param, sink.channel(),
							chip_param::ENVELOPE, ssgEnvShape, ssgEnvPeriod);
					}
					sink.emit(currentTick, SeqEventType::NoteOn, sink.channel(), midiNote, volume);
					sink.emit(currentTick + gateTicks, SeqEventType::NoteOff, sink.channel());
				}

				prevMidiNote = midiNote;
				currentTick += mainTicks;
				break;
			}

			case CommandType::Rest:
			{
				const int len = (cmd.duration > 0) ? cmd.duration : defaultLength;
				currentTick += lengthToTicks(len, cmd.dotted);
				inCrescendo = false;
				inDecrescendo = false;
				break;
			}

			default:
				break;
			}
		}

		return sink.take();
	}

	/// @brief リズム用の MML (B=キック S=スネア H=ハイハット F=タム Y=シンバル I=リムショット R=休符) を
	///        イベント列にする
	/// @param drumChannelBase ドラム d (0〜5) を鳴らすチャンネル番号は drumChannelBase + d
	[[nodiscard]] static std::vector<SeqEvent> fromRhythm(
		std::string_view mml, const TrackTarget& target, int drumChannelBase)
	{
		Sink sink(target);
		int currentTick = 0;
		int defaultLength = 8;

		std::size_t pos = 0;
		while (pos < mml.size())
		{
			const char c = mml[pos];

			if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
			{
				++pos;
				continue;
			}

			if (c == 'T' || c == 't')
			{
				++pos;
				const int val = readInt(mml, pos, 120);
				sink.emit(currentTick, SeqEventType::Tempo, 0, std::clamp(val, 20, 300));
				continue;
			}

			if (c == 'L' || c == 'l')
			{
				++pos;
				defaultLength = std::max(1, readInt(mml, pos, 8));
				continue;
			}

			int drumInst = -1;
			if (c == 'B' || c == 'b') drumInst = 0;
			else if (c == 'S' || c == 's') drumInst = 1;
			else if (c == 'H' || c == 'h') drumInst = 2;
			else if (c == 'F' || c == 'f') drumInst = 3; // T はテンポと衝突するので、タムは F
			else if (c == 'Y' || c == 'y') drumInst = 4;
			else if (c == 'I' || c == 'i') drumInst = 5;

			if (drumInst >= 0)
			{
				++pos;
				int len = readInt(mml, pos, 0);
				bool dotted = false;
				if (pos < mml.size() && mml[pos] == '.')
				{
					dotted = true;
					++pos;
				}
				if (len == 0) len = defaultLength;
				const int ticks = lengthToTicks(len, dotted);

				const int channel = drumChannelBase + drumInst;
				sink.emit(currentTick, SeqEventType::NoteOn, channel, 0, 15);
				sink.emit(currentTick + 2, SeqEventType::NoteOff, channel); // 2 tick 後に自動でキーオフ
				currentTick += ticks;
				continue;
			}

			if (c == 'R' || c == 'r')
			{
				++pos;
				int len = readInt(mml, pos, 0);
				bool dotted = false;
				if (pos < mml.size() && mml[pos] == '.')
				{
					dotted = true;
					++pos;
				}
				if (len == 0) len = defaultLength;
				currentTick += lengthToTicks(len, dotted);
				continue;
			}

			++pos; // 不明な文字は読み飛ばす
		}

		return sink.take();
	}

private:
	/// @brief トラックの通し番号を振りながらイベントを集める
	class Sink
	{
	public:
		explicit Sink(const TrackTarget& target) : m_target(target) {}

		[[nodiscard]] int channel() const noexcept { return m_target.channel; }
		[[nodiscard]] const ChannelCaps& caps() const noexcept { return m_target.caps; }

		void emit(int tick, SeqEventType type, int channel, int a = 0, int b = 0, int c = 0)
		{
			SeqEvent ev;
			ev.tick = tick;
			ev.track = m_target.track;
			ev.order = static_cast<std::uint32_t>(m_events.size());
			ev.chip = m_target.chip;
			ev.type = type;
			ev.channel = channel;
			ev.a = a;
			ev.b = b;
			ev.c = c;
			m_events.push_back(ev);
		}

		[[nodiscard]] std::vector<SeqEvent> take() { return std::move(m_events); }

	private:
		TrackTarget m_target;
		std::vector<SeqEvent> m_events;
	};

	static int readInt(std::string_view s, std::size_t& pos, int defaultVal)
	{
		if (pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos])))
		{
			return defaultVal;
		}
		int val = 0;
		while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos])))
		{
			val = val * 10 + (s[pos] - '0');
			++pos;
		}
		return val;
	}
};

} // namespace mitiru_mml
