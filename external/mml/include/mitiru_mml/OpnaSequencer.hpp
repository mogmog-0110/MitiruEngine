#pragma once
/// @file OpnaSequencer.hpp
/// @brief YM2608 (OPNA) 用の MML シーケンサー
/// @details MML 文字列をチップに依存しないイベントに変え、tick 単位のタイムラインで全トラックを同期再生して
///          PCM を作る。OPNA は ChipDriver (OpnaDriver) として載っているので、他のチップと同じ
///          時間軸の仕組みを使う。複数チップを混ぜる曲は MultiChipSequencer を使う。
///
/// @code
/// mitiru_mml::OpnaSequencer seq;
/// seq.addFmTrack("T120 @FM0 O4 L4 CDEF", 0);
/// seq.addSsgTrack("T120 O5 L8 CDEFGAB>C");
/// auto pcm = seq.render();
/// @endcode

#include <mitiru_mml/BoxDecimator.hpp>
#include <mitiru_mml/MmlEventBuilder.hpp>
#include <mitiru_mml/MmlParser.hpp>
#include <mitiru_mml/MmlTypes.hpp>
#include <mitiru_mml/MultiChipSequencer.hpp>
#include <mitiru_mml/OpnaDriver.hpp>
#include <mitiru_mml/OpnaPresets.hpp>
#include <mitiru_mml/StereoBuffer.hpp>
#include <mitiru_mml/Timeline.hpp>
#include <mitiru_mml/WavWriter.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru_mml
{

/// @brief レンダリング結果 (トラック別データ付き)
/// @details ミックス済み PCM に加え、各トラックの PCM と振幅エンベロープを格納する。
///          振幅エンベロープは指定されたフレームレートでサンプリングしたピーク振幅値。
struct RenderResult
{
	PcmBuffer mixed;                                ///< ミックス済み PCM
	std::vector<PcmBuffer> trackPcm;                ///< トラック別 PCM (個別レンダリング)
	std::vector<std::vector<float>> trackAmplitude; ///< トラック別振幅エンベロープ (フレーム単位)
	uint32_t sampleRate = 55930;                    ///< サンプルレート
	float duration = 0.0f;                          ///< 総再生時間 (秒)
};

/// @brief OPNA 用の MML シーケンサー
/// @details FM 6ch + SSG 3ch のトラックを受け付け、OpnaDriver で tick 単位に同期再生する。
class OpnaSequencer
{
public:
	/// @brief tick 分解能 (4 分音符あたりの tick 数)
	static constexpr int TICKS_PER_QUARTER = MmlEventBuilder::TICKS_PER_QUARTER;

	/// @brief 出力サンプルレート (Hz)。OPNA のネイティブ出力 (約 998400 Hz = clock/8) は FM の実効レート
	///        (clock/144) の約 18 倍で、そのままだと WAV が巨大になる。標準レートへ落として返す。
	static constexpr uint32_t OUTPUT_SAMPLE_RATE = 44100;

	/// @brief 描画の挙動を決める設定。false にすると、それぞれ従来 (リファクタリング前) の挙動になる
	struct RenderOptions
	{
		/// @brief true なら、同じ tick のイベントを (tick, トラック番号, トラック内の通し番号) で並べる。
		///        false なら従来どおり tick だけを比べる std::sort で、同じ tick の並びは標準ライブラリの実装しだい。
		///        同じ MML から同じ PCM を得るため、既定は true
		bool stableEventOrder = true;

		/// @brief true なら、SSG トラックの V と P が、同じ番号の FM チャンネルを書き換えない。
		///        false なら従来どおり書き換える (FM の音量が SSG の V で決まってしまう)。既定は true
		bool isolateSsgControl = true;

		/// @brief renderWithAnalysis でトラックを別スレッドで描く。結果は変わらない
		bool parallelAnalysis = true;

	};

	/// @brief FM トラックを追加する
	/// @param mml MML 文字列
	/// @param presetIndex FM 音色プリセット番号 (0-11)
	void addFmTrack(std::string_view mml, int presetIndex = 0)
	{
		TrackInfo info;
		info.mml = std::string(mml);
		info.type = TrackType::FM;
		info.presetIndex = std::clamp(presetIndex, 0, 11);
		m_tracks.push_back(std::move(info));
	}

	/// @brief SSG トラックを追加する
	void addSsgTrack(std::string_view mml)
	{
		TrackInfo info;
		info.mml = std::string(mml);
		info.type = TrackType::SSG;
		m_tracks.push_back(std::move(info));
	}

	/// @brief リズムトラックを追加する
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

	/// @brief 描画の設定を変える
	void setRenderOptions(const RenderOptions& options) noexcept
	{
		m_options = options;
	}

	/// @brief 描画の設定
	[[nodiscard]] const RenderOptions& renderOptions() const noexcept
	{
		return m_options;
	}

	/// @brief 全トラックをレンダリングして PCM を返す
	/// @param loopCount ループ回数 (1=ループなし)
	/// @return 16bit モノラル、44100 Hz
	[[nodiscard]] PcmBuffer render(int loopCount = 1) const
	{
		if (m_tracks.empty()) return {};

		OpnaDriver driver;
		driver.setLfo(true, 3); // 中程度の速度でハードウェア LFO を有効にする

		const std::vector<SeqEvent> events = buildEvents(driver);

		// 従来と同じ長さにするため、tick ごとに切り捨てる (端数を持ち越すと 1 サンプル単位でずれる)
		TickClock clock(driver.sampleRate(), TickClock::Rounding::TruncatePerTick, 120);
		BoxDecimatorPcm16 decimator(driver.sampleRate(), OUTPUT_SAMPLE_RATE);
		PcmBuffer result;
		std::vector<std::int16_t> chunk;

		playTimeline(events, loopCount, clock,
			[&](const SeqEvent& ev) { applySeqEvent(driver, ev); },
			[&](std::uint32_t numSamples)
			{
				chunk.resize(numSamples);
				driver.renderInto(chunk.data(), numSamples);
				decimator.push(chunk.data(), numSamples, result);
			});
		decimator.finish(result);
		return result;
	}

	/// @brief float ステレオで描く (MultiChipSequencer の経路)
	/// @details FM のパンが左右の定位になる。tick の端数は持ち越し、同じ tick のイベントは必ず安定した順で
	///          処理する。FM が 6 本、SSG が 3 本を超えるトラックは鳴らさない。
	///          render() とは混ぜ方 (ソフトクリップを出力レートで行う) が違うので、サンプルは一致しない。
	[[nodiscard]] StereoBuffer renderStereo(int loopCount = 1, uint32_t outputRate = OUTPUT_SAMPLE_RATE) const
	{
		MultiChipSequencer::Options options;
		options.outputRate = outputRate;
		MultiChipSequencer multi(options);
		const int chip = multi.addChip(std::make_unique<OpnaDriver>());

		bool hasRhythm = false;
		for (const auto& track : m_tracks)
		{
			if (track.type == TrackType::Rhythm) hasRhythm = true;
		}
		const int maxFm = hasRhythm ? 3 : OpnaDriver::FM_CHANNELS;

		int fmIndex = 0;
		int ssgIndex = 0;
		for (const auto& track : m_tracks)
		{
			switch (track.type)
			{
			case TrackType::FM:
				if (fmIndex < maxFm)
				{
					multi.addTrack(chip, OpnaDriver::FM_CHANNEL_BASE + fmIndex++, track.mml, track.presetIndex);
				}
				break;
			case TrackType::SSG:
				if (ssgIndex < OpnaDriver::SSG_CHANNELS)
				{
					multi.addTrack(chip, OpnaDriver::SSG_CHANNEL_BASE + ssgIndex++, track.mml);
				}
				break;
			case TrackType::Rhythm:
				multi.addRhythmTrack(chip, OpnaDriver::DRUM_CHANNEL_BASE, track.mml);
				break;
			}
		}
		return multi.render(loopCount);
	}

	/// @brief 出力サンプルレートを取得する (ダウンサンプル後の標準レート)
	[[nodiscard]] uint32_t sampleRate() const
	{
		return OUTPUT_SAMPLE_RATE;
	}

	/// @brief トラック別 PCM・振幅データ付きでレンダリングする
	/// @param amplitudeFrameRate 振幅エンベロープのフレームレート (fps)
	/// @details まずミックス済み PCM を作り、次に各トラックを単独で描いて、トラック別 PCM と
	///          指定フレームレートの振幅エンベロープを計算する。単独の描画は互いに独立なので、
	///          RenderOptions::parallelAnalysis が true なら別スレッドで描く。
	[[nodiscard]] RenderResult renderWithAnalysis(int amplitudeFrameRate = 60) const
	{
		RenderResult result;
		result.sampleRate = sampleRate();

		result.mixed = render();
		result.duration = static_cast<float>(result.mixed.size())
			/ static_cast<float>(result.sampleRate);

		auto renderSolo = [this](std::size_t t)
		{
			OpnaSequencer solo;
			solo.m_options = m_options;
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
			return solo.render();
		};

		std::vector<PcmBuffer> soloPcm(m_tracks.size());
		if (m_options.parallelAnalysis && m_tracks.size() > 1)
		{
			std::vector<std::future<PcmBuffer>> futures;
			for (std::size_t t = 0; t < m_tracks.size(); ++t)
			{
				futures.push_back(std::async(std::launch::async, renderSolo, t));
			}
			for (std::size_t t = 0; t < futures.size(); ++t)
			{
				soloPcm[t] = futures[t].get();
			}
		}
		else
		{
			for (std::size_t t = 0; t < m_tracks.size(); ++t)
			{
				soloPcm[t] = renderSolo(t);
			}
		}

		for (std::size_t t = 0; t < m_tracks.size(); ++t)
		{
			PcmBuffer& trackBuf = soloPcm[t];

			// 指定 fps でピーク値をサンプリングして振幅エンベロープにする
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
					const float a = std::abs(static_cast<float>(trackBuf[j])) / 32767.0f;
					if (a > maxAmp) maxAmp = a;
				}
				amplitude.push_back(maxAmp);
			}

			result.trackPcm.push_back(std::move(trackBuf));
			result.trackAmplitude.push_back(std::move(amplitude));
		}

		return result;
	}

	/// @brief レンダリング結果を WAV ファイルに保存する
	/// @return 保存に成功したら true
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

	/// @brief 全トラックのイベントを作り、並べて返す。FM の音色と SSG のイネーブルもここで設定する
	/// @details FM チャンネルを使い切ったあとのトラックは、従来どおり FM の 3 番に重ねる
	///          (3 番は、リズムトラックがあればキックのチャンネルでもある)。
	[[nodiscard]] std::vector<SeqEvent> buildEvents(OpnaDriver& driver) const
	{
		bool hasRhythm = false;
		for (const auto& track : m_tracks)
		{
			if (track.type == TrackType::Rhythm) hasRhythm = true;
		}
		// FM の旋律用チャンネルの上限。リズムトラックがあれば FM 3-5 をドラム用に取っておく
		const int maxFmMelodic = hasRhythm ? 3 : OpnaDriver::FM_CHANNELS;
		constexpr int OVERFLOW_FM_CHANNEL = 3;

		std::vector<SeqEvent> merged;
		int fmIndex = 0;
		int ssgIndex = 0;

		for (std::size_t t = 0; t < m_tracks.size(); ++t)
		{
			const auto& track = m_tracks[t];
			TrackTarget target;
			target.track = static_cast<std::uint32_t>(t);

			std::vector<SeqEvent> events;
			if (track.type == TrackType::Rhythm)
			{
				target.channel = OpnaDriver::DRUM_CHANNEL_BASE;
				events = MmlEventBuilder::fromRhythm(track.mml, target, OpnaDriver::DRUM_CHANNEL_BASE);
			}
			else
			{
				const CommandList cmds = MmlEventBuilder::expandLoops(MmlParser::parse(track.mml));
				if (track.type == TrackType::FM)
				{
					if (fmIndex < maxFmMelodic)
					{
						target.channel = OpnaDriver::FM_CHANNEL_BASE + fmIndex;
						driver.setVoice(target.channel, track.presetIndex);
						++fmIndex;
					}
					else
					{
						target.channel = OVERFLOW_FM_CHANNEL;
					}
					target.caps = driver.channelCaps(target.channel);
					events = MmlEventBuilder::fromMelodic(cmds, target);
				}
				else
				{
					const int ssgChannel = ssgIndex;
					// SSG は 3 本まで。それを超えたトラックは、鳴らさないが、従来の挙動を再現するために
					// イベントは作る (V と P の副作用が残る)
					target.channel = (ssgChannel < OpnaDriver::SSG_CHANNELS)
						? OpnaDriver::SSG_CHANNEL_BASE + ssgChannel : -1;
					target.caps = driver.channelCaps(OpnaDriver::SSG_CHANNEL_BASE);
					events = MmlEventBuilder::fromMelodic(cmds, target);
					routeSsgControl(events, ssgChannel);
					++ssgIndex;
				}
			}
			merged.insert(merged.end(), events.begin(), events.end());
		}

		if (ssgIndex > 0)
		{
			driver.setSsgEnable(ssgIndex >= 1, ssgIndex >= 2, ssgIndex >= 3);
		}

		if (m_options.stableEventOrder)
		{
			sortSeqEvents(merged);
		}
		else
		{
			std::sort(merged.begin(), merged.end(),
				[](const SeqEvent& a, const SeqEvent& b) { return a.tick < b.tick; });
		}
		return merged;
	}

	/// @brief SSG トラックの V と P の宛先を決める
	/// @details 従来は、SSG トラックの V と P が、同じ番号の FM チャンネルの音量とパンを書き換えていた。
	///          isolateSsgControl が false のとき、その書き換えを setParam の特別な id で再現する。
	///          true のときは、SSG チャンネル自身への指示にする (SSG にパンは無いので無視される)。
	void routeSsgControl(std::vector<SeqEvent>& events, int ssgIndex) const
	{
		if (m_options.isolateSsgControl)
		{
			return;
		}
		for (auto& ev : events)
		{
			if (ev.type != SeqEventType::Volume && ev.type != SeqEventType::Pan)
			{
				continue;
			}
			const int value = ev.a;
			ev.a = (ev.type == SeqEventType::Volume)
				? OpnaDriver::PARAM_LEGACY_FM_VOLUME : OpnaDriver::PARAM_LEGACY_FM_PAN;
			ev.type = SeqEventType::Param;
			ev.channel = ssgIndex;
			ev.b = value;
		}
	}

	std::vector<TrackInfo> m_tracks; ///< トラックリスト
	RenderOptions m_options;
};

} // namespace mitiru_mml
