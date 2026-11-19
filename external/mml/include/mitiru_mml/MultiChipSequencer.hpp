#pragma once
/// @file MultiChipSequencer.hpp
/// @brief 複数のチップ (ChipDriver) を同じ時間軸で鳴らし、float ステレオで返すシーケンサー
/// @details 各チップは自分のネイティブレートで進む。tick からサンプル数への変換は整数で、
///          端数は次の tick に持ち越す (TickClock::Rounding::CarryRemainder)。チップごとの出力は
///          箱型フィルターで出力レートに落とし、チップ番号の順に float で足す。
///          チップ同士は独立しているので、チップごとに別スレッドで描いても結果は同じになる。
///
/// @code
/// mitiru_mml::MultiChipSequencer seq;
/// const int opna = seq.addChip(std::make_unique<mitiru_mml::OpnaDriver>());
/// seq.addTrack(opna, 0, "T120 O4 L4 CDEF", 2);
/// auto stereo = seq.render();            // 44100 Hz、float ステレオ
/// auto pcm = stereo.toPcm16Stereo();
/// @endcode

#include <mitiru_mml/BoxDecimator.hpp>
#include <mitiru_mml/ChipDriver.hpp>
#include <mitiru_mml/MmlEventBuilder.hpp>
#include <mitiru_mml/MmlParser.hpp>
#include <mitiru_mml/StereoBuffer.hpp>
#include <mitiru_mml/Timeline.hpp>

#include <algorithm>
#include <cstdint>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mitiru_mml
{

class MultiChipStream;

/// @brief 複数チップのシーケンサー
class MultiChipSequencer
{
	friend class MultiChipStream;

public:
	/// @brief 描画の設定
	struct Options
	{
		std::uint32_t outputRate = 44100; ///< 出力のサンプルレート (Hz)
		float masterGain = 0.8f;          ///< チップを足し合わせた後にかける係数
		bool softClip = true;             ///< 足し合わせた後に三次のソフトクリップをかける
		bool parallelChips = false;       ///< チップごとに別スレッドで描く (結果は変わらない)
	};

	explicit MultiChipSequencer(const Options& options = Options{}) : m_options(options) {}

	/// @brief チップを足す
	/// @param gain このチップの出力にかける係数
	/// @return チップ番号 (addTrack の第 1 引数)
	int addChip(std::unique_ptr<ChipDriver> driver, float gain = 1.0f)
	{
		if (!driver)
		{
			throw std::invalid_argument("MultiChipSequencer::addChip: driver is null");
		}
		m_chips.push_back(ChipSlot{std::move(driver), gain});
		return static_cast<int>(m_chips.size()) - 1;
	}

	/// @brief 旋律のトラックを足す
	/// @param chip チップ番号
	/// @param channel Driver のチャンネル番号
	/// @param mml MML 文字列
	/// @param voice 音色番号 (描画の最初に setVoice で設定する)
	/// @throws std::invalid_argument チップ番号かチャンネル番号が範囲外のとき
	void addTrack(int chip, int channel, std::string_view mml, int voice = 0)
	{
		const ChannelCaps caps = capsOf(chip, channel);
		TrackTarget target;
		target.track = static_cast<std::uint32_t>(m_trackEvents.size());
		target.chip = static_cast<std::uint32_t>(chip);
		target.channel = channel;
		target.caps = caps;
		m_trackEvents.push_back(MmlEventBuilder::fromMelodic(
			MmlEventBuilder::expandLoops(MmlParser::parse(mml)), target));
		m_voices.push_back(VoiceSetup{chip, channel, voice});
	}

	/// @brief ドラム用 MML のトラックを足す
	/// @param drumChannelBase ドラム d (0〜5) を鳴らすチャンネルは drumChannelBase + d
	void addRhythmTrack(int chip, int drumChannelBase, std::string_view mml)
	{
		static_cast<void>(capsOf(chip, drumChannelBase));
		TrackTarget target;
		target.track = static_cast<std::uint32_t>(m_trackEvents.size());
		target.chip = static_cast<std::uint32_t>(chip);
		target.channel = drumChannelBase;
		m_trackEvents.push_back(MmlEventBuilder::fromRhythm(mml, target, drumChannelBase));
	}

	/// @brief トラック数
	[[nodiscard]] int trackCount() const noexcept
	{
		return static_cast<int>(m_trackEvents.size());
	}

	/// @brief チップ数
	[[nodiscard]] int chipCount() const noexcept
	{
		return static_cast<int>(m_chips.size());
	}

	/// @brief 全トラックを描いて float ステレオで返す
	/// @param loopCount 曲を何回流すか (2 回目以降は最初の LoopPoint から)
	[[nodiscard]] StereoBuffer render(int loopCount = 1)
	{
		StereoBuffer out;
		out.sampleRate = m_options.outputRate;
		if (m_trackEvents.empty())
		{
			return out;
		}

		std::vector<SeqEvent> events;
		for (const auto& list : m_trackEvents)
		{
			events.insert(events.end(), list.begin(), list.end());
		}
		sortSeqEvents(events);

		std::vector<std::vector<float>> perChip(m_chips.size());
		if (m_options.parallelChips && m_chips.size() > 1)
		{
			std::vector<std::future<std::vector<float>>> futures;
			for (std::size_t c = 0; c < m_chips.size(); ++c)
			{
				futures.push_back(std::async(std::launch::async,
					[this, c, &events, loopCount]() { return renderChip(c, events, loopCount); }));
			}
			for (std::size_t c = 0; c < futures.size(); ++c)
			{
				perChip[c] = futures[c].get();
			}
		}
		else
		{
			for (std::size_t c = 0; c < m_chips.size(); ++c)
			{
				perChip[c] = renderChip(c, events, loopCount);
			}
		}

		// チップ番号の順に足す。足す順が決まっているので、スレッドの完了順に左右されない
		for (std::size_t c = 0; c < perChip.size(); ++c)
		{
			out.growTo(perChip[c].size() / 2);
			const float gain = m_chips[c].gain;
			for (std::size_t i = 0; i < perChip[c].size(); ++i)
			{
				out.samples[i] += perChip[c][i] * gain;
			}
		}
		applyMaster(out);
		return out;
	}

private:
	struct ChipSlot
	{
		std::unique_ptr<ChipDriver> driver;
		float gain = 1.0f;
	};

	struct VoiceSetup
	{
		int chip = 0;
		int channel = 0;
		int voice = 0;
	};

	[[nodiscard]] ChannelCaps capsOf(int chip, int channel) const
	{
		if (chip < 0 || chip >= static_cast<int>(m_chips.size()))
		{
			throw std::invalid_argument("MultiChipSequencer: chip index " + std::to_string(chip)
				+ " is out of range (chipCount = " + std::to_string(m_chips.size()) + ")");
		}
		const auto& driver = *m_chips[static_cast<std::size_t>(chip)].driver;
		const ChannelCaps caps = (channel >= 0 && channel < driver.channelCount())
			? driver.channelCaps(channel) : ChannelCaps{};
		if (!caps.valid)
		{
			throw std::invalid_argument("MultiChipSequencer: channel " + std::to_string(channel)
				+ " does not exist on chip " + std::to_string(chip));
		}
		return caps;
	}

	/// @brief 1 チップぶんを最後まで描く (他のチップには触れない)
	[[nodiscard]] std::vector<float> renderChip(
		std::size_t chip, const std::vector<SeqEvent>& events, int loopCount) const
	{
		ChipDriver& driver = *m_chips[chip].driver;
		ChipDevice& device = driver.device();
		driver.reset();
		for (const auto& v : m_voices)
		{
			if (static_cast<std::size_t>(v.chip) == chip)
			{
				driver.setVoice(v.channel, v.voice);
			}
		}

		// ネイティブレートが出力より低いチップは、同じフレームを繰り返して出力以上に揃える
		const std::uint32_t native = device.nativeRate();
		const std::uint32_t repeat = (native >= m_options.outputRate)
			? 1u : (m_options.outputRate + native - 1) / native;

		TickClock clock(native, TickClock::Rounding::CarryRemainder);
		BoxDecimatorStereo decimator(native * repeat, m_options.outputRate);
		std::vector<float> out;
		std::vector<float> block;
		std::vector<float> held;
		const auto chipIndex = static_cast<std::uint32_t>(chip);

		playTimeline(events, loopCount, clock,
			[&](const SeqEvent& ev)
			{
				if (ev.chip == chipIndex)
				{
					applySeqEvent(driver, ev);
				}
			},
			[&](std::uint32_t frames)
			{
				block.resize(static_cast<std::size_t>(frames) * 2);
				device.generate(block.data(), frames);
				if (repeat == 1)
				{
					decimator.push(block.data(), frames, out);
					return;
				}
				held.resize(static_cast<std::size_t>(frames) * repeat * 2);
				for (std::size_t i = 0; i < frames; ++i)
				{
					for (std::uint32_t r = 0; r < repeat; ++r)
					{
						held[(i * repeat + r) * 2] = block[i * 2];
						held[(i * repeat + r) * 2 + 1] = block[i * 2 + 1];
					}
				}
				decimator.push(held.data(), static_cast<std::size_t>(frames) * repeat, out);
			});
		decimator.finish(out);
		return out;
	}

	/// @brief 足し合わせた 1 サンプルに掛ける仕上げ。MultiChipStream も同じ式を使う
	[[nodiscard]] static float masterSample(float s, const Options& options) noexcept
	{
		float x = s * options.masterGain;
		if (options.softClip)
		{
			x = std::clamp(x, -1.0f, 1.0f);
			x = 1.5f * x - 0.5f * x * x * x;
		}
		return x;
	}

	void applyMaster(StereoBuffer& buffer) const
	{
		for (float& s : buffer.samples)
		{
			s = masterSample(s, m_options);
		}
	}

	Options m_options;
	std::vector<ChipSlot> m_chips;
	std::vector<std::vector<SeqEvent>> m_trackEvents;
	std::vector<VoiceSetup> m_voices;
};

} // namespace mitiru_mml
