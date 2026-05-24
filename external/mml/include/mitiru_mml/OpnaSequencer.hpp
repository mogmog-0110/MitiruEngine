#pragma once
/// @file OpnaSequencer.hpp
/// @brief YM2608 (OPNA) チップエミュレーション対応MMLシーケンサー
/// @details MML文字列を解析してOPNAチップのレジスタ操作に変換し、
///          tick単位のタイムラインで全トラックを同期再生してPCMを生成する。
///
/// @code
/// mitiru_mml::OpnaSequencer seq;
/// seq.addFmTrack("T120 @FM0 O4 L4 CDEF", 0);
/// seq.addSsgTrack("T120 O5 L8 CDEFGAB>C");
/// auto pcm = seq.render();
/// @endcode

#include <mitiru_mml/MmlTypes.hpp>
#include <mitiru_mml/MmlParser.hpp>
#include <mitiru_mml/OpnaDriver.hpp>
#include <mitiru_mml/OpnaPresets.hpp>
#include <mitiru_mml/WavWriter.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru_mml
{

/// @brief レンダリング結果（トラック別データ付き）
/// @details ミックス済みPCMに加え、各トラックのPCMと振幅エンベロープを格納する。
///          振幅エンベロープは指定されたフレームレートでサンプリングされたピーク振幅値。
struct RenderResult
{
	PcmBuffer mixed;                                ///< ミックス済みPCM
	std::vector<PcmBuffer> trackPcm;                ///< トラック別PCM（個別レンダリング）
	std::vector<std::vector<float>> trackAmplitude; ///< トラック別振幅エンベロープ（フレーム単位）
	uint32_t sampleRate = 55930;                    ///< サンプルレート
	float duration = 0.0f;                          ///< 総再生時間（秒）
};

/// @brief OPNAチップエミュレーション対応シーケンサー
/// @details FM6ch + SSG3ch のMMLトラックを受け付け、
///          OpnaDriverを使ってtick単位で同期再生する。
class OpnaSequencer
{
public:
	/// @brief tick分解能（4分音符あたりのtick数）
	static constexpr int TICKS_PER_QUARTER = 48;

	/// @brief FMトラックを追加する
	/// @param mml MML文字列
	/// @param presetIndex FM音色プリセット番号 (0-11)
	void addFmTrack(std::string_view mml, int presetIndex = 0)
	{
		TrackInfo info;
		info.mml = std::string(mml);
		info.type = TrackType::FM;
		info.presetIndex = std::clamp(presetIndex, 0, 11);
		m_tracks.push_back(std::move(info));
	}

	/// @brief SSGトラックを追加する
	/// @param mml MML文字列
	void addSsgTrack(std::string_view mml)
	{
		TrackInfo info;
		info.mml = std::string(mml);
		info.type = TrackType::SSG;
		m_tracks.push_back(std::move(info));
	}

	/// @brief リズムトラックを追加する（将来用）
	/// @param mml MML文字列
	void addRhythmTrack(std::string_view mml)
	{
		TrackInfo info;
		info.mml = std::string(mml);
		info.type = TrackType::Rhythm;
		m_tracks.push_back(std::move(info));
	}

	/// @brief 全トラックをクリアする
	void clear()
	{
		m_tracks.clear();
	}

	/// @brief トラック数を返す
	[[nodiscard]] int trackCount() const noexcept
	{
		return static_cast<int>(m_tracks.size());
	}

	/// @brief 全トラックをレンダリングしてPCMバッファを返す
	/// @param loopCount ループ回数（1=ループなし）
	/// @return 16bitモノラルPCMバッファ
	[[nodiscard]] PcmBuffer render(int loopCount = 1) const
	{
		if (m_tracks.empty()) return {};

		OpnaDriver driver;

		// ハードウェアLFOを有効にする（中程度の速度）
		driver.setLfo(true, 3);

		// リズムトラックがあるか確認する（FMチャンネル3-5をドラム用に予約）
		bool hasRhythm = false;
		for (const auto& track : m_tracks)
		{
			if (track.type == TrackType::Rhythm) hasRhythm = true;
		}

		// FM melodicチャンネル上限: リズムトラックがあれば0-2、なければ0-5
		const int maxFmMelodic = hasRhythm ? 3 : OpnaDriver::FM_CHANNELS;

		// 各トラックをイベントリストに変換する
		std::vector<EventList> allEvents;
		int fmChIndex = 0;
		int ssgChIndex = 0;

		for (const auto& track : m_tracks)
		{
			if (track.type == TrackType::Rhythm)
			{
				// リズムトラックは専用パーサーで解析する（B/S/H/T/Y/I文字をドラムとして処理）
				auto events = parseRhythmMml(track.mml);
				allEvents.push_back(std::move(events));
			}
			else
			{
				auto cmds = expandLoops(MmlParser::parse(track.mml));
				int assignedCh = 0;

				if (track.type == TrackType::FM)
				{
					assignedCh = fmChIndex;
					if (fmChIndex < maxFmMelodic)
					{
						// FM音色を設定する
						const auto& voice = opna_presets::getPreset(track.presetIndex);
						driver.setFmVoice(fmChIndex, voice);
						++fmChIndex;
					}
				}
				else if (track.type == TrackType::SSG)
				{
					assignedCh = ssgChIndex;
					++ssgChIndex;
				}

				auto events = commandsToEvents(cmds, track.type, assignedCh);
				allEvents.push_back(std::move(events));
			}
		}

		// SSGイネーブルを設定する
		if (ssgChIndex > 0)
		{
			driver.setSsgEnable(
				ssgChIndex >= 1,
				ssgChIndex >= 2,
				ssgChIndex >= 3);
		}

		// 全イベントを1つのリストに統合してソートする
		EventList merged;
		for (const auto& evts : allEvents)
		{
			merged.insert(merged.end(), evts.begin(), evts.end());
		}
		std::sort(merged.begin(), merged.end(),
			[](const Event& a, const Event& b)
			{
				return a.tick < b.tick;
			});

		// 最終tickを取得する
		int totalTicks = 0;
		for (const auto& ev : merged)
		{
			totalTicks = std::max(totalTicks, ev.tick);
		}

		// デフォルトテンポからサンプル計算の初期値を設定する
		int currentTempo = 120;
		for (const auto& ev : merged)
		{
			if (ev.eventType == EventType::Tempo)
			{
				currentTempo = ev.value;
				break;
			}
		}

		// ループポイントのtick位置を検出する
		int loopPointTick = 0;
		for (const auto& ev : merged)
		{
			if (ev.eventType == EventType::LoopPointMark)
			{
				loopPointTick = ev.tick;
				break;
			}
		}

		// tick単位でイベントを処理しながらサンプルを生成する
		PcmBuffer result;
		int tempo = 120;

		// テンポからtickあたりのサンプル数を計算する
		auto samplesPerTick = [&]() -> uint32_t
		{
			// 1四分音符 = 60/tempo 秒
			// 1 tick = (60/tempo) / TICKS_PER_QUARTER 秒
			const float secondsPerTick =
				60.0f / static_cast<float>(tempo) / static_cast<float>(TICKS_PER_QUARTER);
			return static_cast<uint32_t>(
				static_cast<float>(driver.sampleRate()) * secondsPerTick);
		};

		const int effectiveLoopCount = std::max(1, loopCount);
		for (int loop = 0; loop < effectiveLoopCount; ++loop)
		{
			// ループ2回目以降はloopPointTickから開始する
			const int startTick = (loop == 0) ? 0 : loopPointTick;
			int eventIdx = 0;
			// startTickに合わせてイベントインデックスをスキップする
			while (eventIdx < static_cast<int>(merged.size())
				&& merged[eventIdx].tick < startTick)
			{
				++eventIdx;
			}

			for (int tick = startTick; tick <= totalTicks; ++tick)
			{
				// このtickのイベントを処理する
				while (eventIdx < static_cast<int>(merged.size())
					&& merged[eventIdx].tick == tick)
				{
					const auto& ev = merged[eventIdx];
					processEvent(driver, ev, tempo);
					++eventIdx;
				}

				// このtick分のサンプルを生成する
				const uint32_t numSamples = samplesPerTick();
				if (numSamples > 0)
				{
					auto samples = driver.renderSamples(numSamples);
					result.insert(result.end(), samples.begin(), samples.end());
				}
			}
		}

		return result;
	}

	/// @brief サンプルレートを取得する（OPNAチップのネイティブレート）
	[[nodiscard]] uint32_t sampleRate() const
	{
		// OpnaDriverを一時生成してレート取得
		OpnaDriver temp;
		return temp.sampleRate();
	}

	/// @brief トラック別PCM・振幅データ付きレンダリングを行う
	/// @param amplitudeFrameRate 振幅エンベロープのフレームレート（fps）
	/// @return トラック別データを含むレンダリング結果
	/// @details まずミックス済みPCMを生成し、次に各トラックを個別にレンダリングして
	///          トラック別PCMと指定フレームレートでの振幅エンベロープを計算する。
	[[nodiscard]] RenderResult renderWithAnalysis(int amplitudeFrameRate = 60) const
	{
		RenderResult result;
		result.sampleRate = sampleRate();

		// ミックス済みPCMを生成する
		result.mixed = render();
		result.duration = static_cast<float>(result.mixed.size())
			/ static_cast<float>(result.sampleRate);

		// 各トラックを個別レンダリングして振幅エンベロープを計算する
		for (std::size_t t = 0; t < m_tracks.size(); ++t)
		{
			OpnaSequencer solo;
			const auto& track = m_tracks[t];

			if (track.type == TrackType::FM)
			{
				solo.addFmTrack(track.mml, track.presetIndex);
			}
			else if (track.type == TrackType::SSG)
			{
				solo.addSsgTrack(track.mml);
			}
			else
			{
				solo.addRhythmTrack(track.mml);
			}

			auto trackBuf = solo.render();

			// 振幅エンベロープを計算する（指定fpsでピーク値をサンプリング）
			const int samplesPerFrame = static_cast<int>(result.sampleRate)
				/ std::max(1, amplitudeFrameRate);
			std::vector<float> amplitude;

			for (std::size_t i = 0; i < trackBuf.size();
				i += static_cast<std::size_t>(samplesPerFrame))
			{
				float maxAmp = 0.0f;
				const std::size_t end = std::min(
					i + static_cast<std::size_t>(samplesPerFrame),
					trackBuf.size());

				for (std::size_t j = i; j < end; ++j)
				{
					const float a = std::abs(
						static_cast<float>(trackBuf[j])) / 32767.0f;
					if (a > maxAmp) maxAmp = a;
				}
				amplitude.push_back(maxAmp);
			}

			result.trackPcm.push_back(std::move(trackBuf));
			result.trackAmplitude.push_back(std::move(amplitude));
		}

		return result;
	}

	/// @brief レンダリング結果をWAVファイルに保存する
	/// @param filePath 出力ファイルパス
	/// @param loopCount ループ回数（1=ループなし）
	/// @return 保存に成功したらtrue
	bool exportWav(const std::string& filePath, int loopCount = 1) const
	{
		auto pcm = render(loopCount);
		if (pcm.empty()) return false;
		auto wav = WavWriter::toWav(pcm, sampleRate());
		std::ofstream ofs(filePath, std::ios::binary);
		if (!ofs) return false;
		ofs.write(reinterpret_cast<const char*>(wav.data()),
			static_cast<std::streamsize>(wav.size()));
		return ofs.good();
	}

private:
	/// @brief トラック種別
	enum class TrackType
	{
		FM,
		SSG,
		Rhythm
	};

	/// @brief トラック情報
	struct TrackInfo
	{
		std::string mml;
		TrackType type = TrackType::FM;
		int presetIndex = 0;
	};

	/// @brief イベント種別
	enum class EventType
	{
		FmNoteOn,
		FmNoteOff,
		SsgNoteOn,
		SsgNoteOff,
		Tempo,
		Volume,
		FmFreqChange,    ///< FM周波数変更のみ（レガート接続）
		SsgFreqChange,   ///< SSG周波数変更のみ（レガート接続）
		FmVelocity,      ///< FMベロシティ変更（TL調整）
		SsgEnvelope,     ///< SSGハードウェアエンベロープ設定
		RhythmHit,       ///< FMドラム合成キーオン
		RhythmOff,       ///< FMドラム合成キーオフ
		Pan,             ///< パン定位変更
		LoopPointMark,   ///< ループポイントマーカー
	};

	/// @brief タイムラインイベント
	struct Event
	{
		int tick = 0;            ///< 発生tick
		EventType eventType;     ///< イベント種別
		int channel = 0;         ///< チャンネル番号
		int value = 0;           ///< MIDIノート番号 / テンポ値 / 音量
		float freq = 0.0f;       ///< SSG用周波数
		int extra = 0;           ///< 追加値（エンベロープ形状、ベロシティ等）
		int extra2 = 0;          ///< 追加値2（エンベロープ周期等）
	};

	/// @brief イベントリスト
	using EventList = std::vector<Event>;

	/// @brief ループコマンドを展開する（前処理）
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
					std::size_t startIdx = loopStack.back();
					loopStack.pop_back();
					int repeatCount = std::max(1, cmds[i].value) - 1;
					std::size_t bodyLen = result.size() - startIdx;
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

	/// @brief リズムMML文字列を直接解析してイベントリストに変換する
	/// @param mml リズムMML文字列（B=Kick, S=Snare, H=HiHat, T=Tom, Y=Cymbal, I=RimShot, R=Rest）
	/// @return イベントリスト
	[[nodiscard]] static EventList parseRhythmMml(std::string_view mml)
	{
		EventList events;
		int currentTick = 0;
		int defaultLength = 8;
		int tempo = 120;

		std::size_t pos = 0;
		while (pos < mml.size())
		{
			const char c = mml[pos];

			// 空白をスキップする
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
			{
				++pos;
				continue;
			}

			// テンポ
			if (c == 'T' || c == 't')
			{
				++pos;
				int val = 120;
				if (pos < mml.size() && std::isdigit(static_cast<unsigned char>(mml[pos])))
				{
					val = 0;
					while (pos < mml.size() && std::isdigit(static_cast<unsigned char>(mml[pos])))
					{
						val = val * 10 + (mml[pos] - '0');
						++pos;
					}
				}
				tempo = std::clamp(val, 20, 300);
				Event ev;
				ev.tick = currentTick;
				ev.eventType = EventType::Tempo;
				ev.value = tempo;
				events.push_back(ev);
				continue;
			}

			// デフォルト音長
			if (c == 'L' || c == 'l')
			{
				++pos;
				int val = 8;
				if (pos < mml.size() && std::isdigit(static_cast<unsigned char>(mml[pos])))
				{
					val = 0;
					while (pos < mml.size() && std::isdigit(static_cast<unsigned char>(mml[pos])))
					{
						val = val * 10 + (mml[pos] - '0');
						++pos;
					}
				}
				defaultLength = std::max(1, val);
				continue;
			}

			// ドラム楽器: B=Kick(0), S=Snare(1), H=HiHat(2), T=Tom(3), Y=Cymbal(4), I=RimShot(5)
			int drumInst = -1;
			if (c == 'B' || c == 'b') drumInst = 0;
			else if (c == 'S' || c == 's') drumInst = 1;
			else if (c == 'H' || c == 'h') drumInst = 2;
			// 大文字Tとtは特別扱い: テンポコマンドは上で処理済みなのでここに来るのは楽器
			// ただしTの後に数字が来る場合はテンポとして処理済みのはず
			else if (c == 'F' || c == 'f') drumInst = 3; // Tom（Tをテンポと衝突させないためFに変更は不可）
			else if (c == 'Y' || c == 'y') drumInst = 4;
			else if (c == 'I' || c == 'i') drumInst = 5;

			if (drumInst >= 0)
			{
				++pos;
				// オプショナルな音長を読み取る
				int len = 0;
				if (pos < mml.size() && std::isdigit(static_cast<unsigned char>(mml[pos])))
				{
					while (pos < mml.size() && std::isdigit(static_cast<unsigned char>(mml[pos])))
					{
						len = len * 10 + (mml[pos] - '0');
						++pos;
					}
				}
				bool dotted = false;
				if (pos < mml.size() && mml[pos] == '.')
				{
					dotted = true;
					++pos;
				}
				if (len == 0) len = defaultLength;
				const int ticks = lengthToTicks(len, dotted);

				// ドラムヒットイベント
				Event hit;
				hit.tick = currentTick;
				hit.eventType = EventType::RhythmHit;
				hit.value = drumInst;
				events.push_back(hit);

				// 自動キーオフ（2 tick後）
				Event off;
				off.tick = currentTick + 2;
				off.eventType = EventType::RhythmOff;
				off.value = drumInst;
				events.push_back(off);

				currentTick += ticks;
				continue;
			}

			// 休符
			if (c == 'R' || c == 'r')
			{
				++pos;
				int len = 0;
				if (pos < mml.size() && std::isdigit(static_cast<unsigned char>(mml[pos])))
				{
					while (pos < mml.size() && std::isdigit(static_cast<unsigned char>(mml[pos])))
					{
						len = len * 10 + (mml[pos] - '0');
						++pos;
					}
				}
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

			// 不明文字はスキップする
			++pos;
		}

		return events;
	}

	/// @brief MMLコマンド列をタイムラインイベントに変換する
	/// @param commands MMLコマンド列
	/// @param type トラック種別
	/// @param channel 割り当てチャンネル番号
	/// @return イベントリスト
	[[nodiscard]] static EventList commandsToEvents(
		const CommandList& commands,
		TrackType type,
		int channel)
	{
		EventList events;
		int currentTick = 0;
		int octave = 4;
		int defaultLength = 4;
		int tempo = 120;
		int volume = 12;
		int velocity = -1;            ///< ノート毎ベロシティ（-1=未設定、0-15）
		bool inCrescendo = false;     ///< クレッシェンド中
		bool inDecrescendo = false;   ///< デクレッシェンド中
		int crescStartVol = 0;        ///< 動的変化開始音量
		int crescNoteCount = 0;       ///< 動的変化中のノート数
		bool inGrace = false;         ///< グレースノート収集中
		std::vector<MmlCommand> graceNotes; ///< グレースノートバッファ
		int prevMidiNote = -1;        ///< 前回のMIDIノート（レガート/ポルタメント用）
		int ssgEnvShape = -1;         ///< SSGエンベロープ形状（-1=未設定）
		int ssgEnvPeriod = 0;         ///< SSGエンベロープ周期

		for (std::size_t i = 0; i < commands.size(); ++i)
		{
			const auto& cmd = commands[i];

			switch (cmd.type)
			{
			case CommandType::Tempo:
			{
				tempo = std::clamp(cmd.value, 20, 300);
				Event ev;
				ev.tick = currentTick;
				ev.eventType = EventType::Tempo;
				ev.value = tempo;
				events.push_back(ev);
				break;
			}

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
			{
				volume = std::clamp(cmd.value, 0, 15);
				Event ev;
				ev.tick = currentTick;
				ev.eventType = EventType::Volume;
				ev.channel = channel;
				ev.value = volume;
				events.push_back(ev);
				break;
			}

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
			{
				Event ev;
				ev.tick = currentTick;
				ev.eventType = EventType::Pan;
				ev.channel = channel;
				ev.value = cmd.value;
				events.push_back(ev);
				break;
			}

			case CommandType::LoopPoint:
			{
				Event ev;
				ev.tick = currentTick;
				ev.eventType = EventType::LoopPointMark;
				events.push_back(ev);
				break;
			}

			case CommandType::Note:
			{
				// グレースノート収集中なら一時バッファに貯める
				if (inGrace)
				{
					graceNotes.push_back(cmd);
					break;
				}

				const int len = (cmd.duration > 0) ? cmd.duration : defaultLength;
				int totalTicks = lengthToTicks(len, cmd.dotted);

				// レガート検出: tiedがtrueで次のコマンドが異なる音程のNote
				bool isLegato = false;
				if (cmd.tied && i + 1 < commands.size())
				{
					const auto& next = commands[i + 1];
					if (next.type == CommandType::Note && next.value != cmd.value)
					{
						isLegato = true;
					}
				}

				// タイ処理（同一音程のタイ）: 持続時間を累積する
				if (!isLegato)
				{
					std::size_t j = i;
					while (j < commands.size() && commands[j].tied && j + 1 < commands.size())
					{
						++j;
						if (commands[j].type == CommandType::Note)
						{
							// 異なる音程の場合はレガートとして別途処理するため中断
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
				const float freq = 440.0f * std::pow(2.0f,
					(static_cast<float>(midiNote) - 69.0f) / 12.0f);

				// クレッシェンド/デクレッシェンド中は音量を段階的に変える
				if (inCrescendo || inDecrescendo)
				{
					++crescNoteCount;
					int targetVol = inCrescendo ? 15 : 0;
					int curVol = crescStartVol + (targetVol - crescStartVol)
						* crescNoteCount / std::max(1, crescNoteCount + 2);
					curVol = std::clamp(curVol, 0, 15);
					Event volEv;
					volEv.tick = currentTick;
					volEv.eventType = EventType::Volume;
					volEv.channel = channel;
					volEv.value = curVol;
					events.push_back(volEv);
				}

				// ベロシティ適用（設定されていれば音量を一時的に変更）
				if (velocity >= 0)
				{
					Event velEv;
					velEv.tick = currentTick;
					velEv.eventType = EventType::Volume;
					velEv.channel = channel;
					velEv.value = velocity;
					events.push_back(velEv);
					velocity = -1; // ノート毎なのでリセットする
				}

				// グレースノート処理: 直前に溜めたグレースノートを短い音として出力
				int graceTickTotal = 0;
				if (!graceNotes.empty())
				{
					const int graceLen = 32; // 32分音符
					for (const auto& gn : graceNotes)
					{
						const int gTicks = lengthToTicks(graceLen, false);
						const int gMidi = (octave + 1) * 12 + gn.value;
						const float gFreq = 440.0f * std::pow(2.0f,
							(static_cast<float>(gMidi) - 69.0f) / 12.0f);

						if (type == TrackType::FM)
						{
							Event gOn;
							gOn.tick = currentTick;
							gOn.eventType = EventType::FmNoteOn;
							gOn.channel = channel;
							gOn.value = gMidi;
							events.push_back(gOn);

							Event gOff;
							gOff.tick = currentTick + std::max(1, gTicks - 1);
							gOff.eventType = EventType::FmNoteOff;
							gOff.channel = channel;
							events.push_back(gOff);
						}
						else if (type == TrackType::SSG)
						{
							Event gOn;
							gOn.tick = currentTick;
							gOn.eventType = EventType::SsgNoteOn;
							gOn.channel = channel;
							gOn.value = volume;
							gOn.freq = gFreq;
							events.push_back(gOn);

							Event gOff;
							gOff.tick = currentTick + std::max(1, gTicks - 1);
							gOff.eventType = EventType::SsgNoteOff;
							gOff.channel = channel;
							events.push_back(gOff);
						}
						currentTick += gTicks;
						graceTickTotal += gTicks;
					}
					graceNotes.clear();
				}

				// メインノートの長さからグレースノート分を差し引く
				const int mainTicks = std::max(1, totalTicks - graceTickTotal);
				const int gateTicks = std::max(1, mainTicks - 1);

				// ポルタメント処理: 前のノートから現在のノートへピッチスライド
				if (cmd.portamento && prevMidiNote >= 0 && type == TrackType::FM)
				{
					// ポルタメントは周波数変更イベントの列で実現する
					const int steps = 6; // 補間ステップ数
					const int slideTicks = std::min(gateTicks / 2, steps * 2);
					const int tickPerStep = std::max(1, slideTicks / steps);

					for (int s = 0; s < steps; ++s)
					{
						const float t = static_cast<float>(s + 1)
							/ static_cast<float>(steps);
						const int interpNote = prevMidiNote
							+ static_cast<int>(t * static_cast<float>(midiNote - prevMidiNote));
						Event slide;
						slide.tick = currentTick + s * tickPerStep;
						slide.eventType = EventType::FmFreqChange;
						slide.channel = channel;
						slide.value = interpNote;
						events.push_back(slide);
					}
				}

				// レガート: key-offをスキップしFmFreqChangeのみ出す
				if (cmd.portamento && prevMidiNote >= 0 && type == TrackType::FM)
				{
					// ポルタメントの最後にターゲット周波数を設定
					// （上のループですでにカバー済み）
				}
				else if (isLegato && type == TrackType::FM && prevMidiNote >= 0)
				{
					// レガート: NoteOffを出さずにFreqChangeのみ
					Event freqCh;
					freqCh.tick = currentTick;
					freqCh.eventType = EventType::FmFreqChange;
					freqCh.channel = channel;
					freqCh.value = midiNote;
					events.push_back(freqCh);
				}
				else if (type == TrackType::FM)
				{
					Event noteOn;
					noteOn.tick = currentTick;
					noteOn.eventType = EventType::FmNoteOn;
					noteOn.channel = channel;
					noteOn.value = midiNote;
					events.push_back(noteOn);

					Event noteOff;
					noteOff.tick = currentTick + gateTicks;
					noteOff.eventType = EventType::FmNoteOff;
					noteOff.channel = channel;
					events.push_back(noteOff);
				}
				else if (type == TrackType::SSG)
				{
					// SSGエンベロープ設定イベント
					if (ssgEnvShape >= 0)
					{
						Event envEv;
						envEv.tick = currentTick;
						envEv.eventType = EventType::SsgEnvelope;
						envEv.channel = channel;
						envEv.value = ssgEnvShape;
						envEv.extra = ssgEnvPeriod;
						events.push_back(envEv);
					}

					Event noteOn;
					noteOn.tick = currentTick;
					noteOn.eventType = EventType::SsgNoteOn;
					noteOn.channel = channel;
					noteOn.value = volume;
					noteOn.freq = freq;
					events.push_back(noteOn);

					Event noteOff;
					noteOff.tick = currentTick + gateTicks;
					noteOff.eventType = EventType::SsgNoteOff;
					noteOff.channel = channel;
					events.push_back(noteOff);
				}

				prevMidiNote = midiNote;
				currentTick += mainTicks;
				break;
			}

			case CommandType::Rest:
			{
				const int len = (cmd.duration > 0) ? cmd.duration : defaultLength;
				const int ticks = lengthToTicks(len, cmd.dotted);
				currentTick += ticks;
				// クレッシェンド/デクレッシェンド終了
				inCrescendo = false;
				inDecrescendo = false;
				break;
			}

			default:
				break;
			}
		}

		return events;
	}

	/// @brief 音長値をtick数に変換する
	/// @param lengthVal L値（1=全音符, 4=四分音符, 8=八分音符...）
	/// @param dotted 付点
	/// @return tick数
	[[nodiscard]] static int lengthToTicks(int lengthVal, bool dotted) noexcept
	{
		if (lengthVal <= 0) lengthVal = 4;
		// 全音符 = TICKS_PER_QUARTER * 4
		int ticks = (TICKS_PER_QUARTER * 4) / lengthVal;
		if (dotted)
		{
			ticks = ticks * 3 / 2;
		}
		return std::max(1, ticks);
	}

	/// @brief イベントを処理してOPNAレジスタに書き込む
	/// @param driver OPNAドライバー
	/// @param ev イベント
	/// @param tempo 現在のテンポ（参照渡しで更新される）
	static void processEvent(OpnaDriver& driver, const Event& ev, int& tempo)
	{
		switch (ev.eventType)
		{
		case EventType::Tempo:
			tempo = ev.value;
			break;

		case EventType::FmNoteOn:
			driver.fmNoteOn(ev.channel, ev.value);
			break;

		case EventType::FmNoteOff:
			driver.fmNoteOff(ev.channel);
			break;

		case EventType::SsgNoteOn:
			driver.setSsgTone(ev.channel, ev.freq,
				static_cast<uint8_t>(std::clamp(ev.value, 0, 15)));
			break;

		case EventType::SsgNoteOff:
			driver.ssgNoteOff(ev.channel);
			break;

		case EventType::Volume:
			// FM: TL値に変換（15=最大→TL=0, 0=無音→TL=127）
			{
				const uint8_t tl = static_cast<uint8_t>(
					127 - (std::clamp(ev.value, 0, 15) * 127 / 15));
				driver.setFmVolume(ev.channel, tl);
			}
			break;

		case EventType::FmFreqChange:
		{
			// key-offせずに周波数のみ変更する（レガート/ポルタメント）
			const bool hi = ev.channel >= 3;
			const int ch = ev.channel % 3;
			auto [fnum, block] = OpnaDriver::noteToFnumBlock(ev.value);
			auto wr = [&](uint8_t reg, uint8_t val)
			{
				if (hi) driver.writeRegHi(reg, val);
				else driver.writeReg(reg, val);
			};
			wr(static_cast<uint8_t>(0xA4 + ch),
				static_cast<uint8_t>((block << 3) | ((fnum >> 8) & 0x07)));
			wr(static_cast<uint8_t>(0xA0 + ch),
				static_cast<uint8_t>(fnum & 0xFF));
			break;
		}

		case EventType::SsgFreqChange:
		{
			// SSG周波数のみ変更する（レガート）
			if (ev.channel >= 0 && ev.channel < OpnaDriver::SSG_CHANNELS && ev.freq > 0.0f)
			{
				const uint32_t effClock = OpnaDriver::CLOCK / 4;
				uint16_t period = static_cast<uint16_t>(
					static_cast<float>(effClock) / (16.0f * ev.freq));
				if (period == 0) period = 1;
				driver.writeReg(static_cast<uint8_t>(ev.channel * 2),
					static_cast<uint8_t>(period & 0xFF));
				driver.writeReg(static_cast<uint8_t>(ev.channel * 2 + 1),
					static_cast<uint8_t>((period >> 8) & 0x0F));
			}
			break;
		}

		case EventType::FmVelocity:
		{
			const uint8_t tl = static_cast<uint8_t>(
				127 - (std::clamp(ev.value, 0, 15) * 127 / 15));
			driver.setFmVolume(ev.channel, tl);
			break;
		}

		case EventType::SsgEnvelope:
		{
			driver.setSsgEnvelope(ev.channel,
				static_cast<uint8_t>(ev.value),
				static_cast<uint16_t>(ev.extra));
			break;
		}

		case EventType::RhythmHit:
		{
			// FMドラム合成: 楽器をFMチャンネル3-5にマッピングする
			switch (ev.value)
			{
			case 0: driver.synthKick(3); break;   // BassDrum → FM ch3
			case 1: driver.synthSnare(4); break;  // Snare → FM ch4
			case 2: driver.synthHihat(5); break;  // HiHat → FM ch5
			case 3: driver.synthKick(3); break;   // Tom → kick variant
			case 4: driver.synthHihat(5); break;  // Cymbal → hihat variant
			case 5: driver.synthSnare(4); break;  // RimShot → snare variant
			}
			break;
		}

		case EventType::RhythmOff:
		{
			// FMドラム合成キーオフ
			switch (ev.value)
			{
			case 0: case 3: driver.fmNoteOff(3); break;
			case 1: case 5: driver.fmNoteOff(4); break;
			case 2: case 4: driver.fmNoteOff(5); break;
			}
			break;
		}

		case EventType::Pan:
		{
			driver.setFmPan(ev.channel, ev.value);
			break;
		}

		case EventType::LoopPointMark:
			// ループポイントマーカー（render()側で処理する）
			break;
		}
	}

	std::vector<TrackInfo> m_tracks; ///< トラックリスト
};

} // namespace mitiru_mml
