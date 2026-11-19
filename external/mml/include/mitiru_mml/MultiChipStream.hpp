#pragma once
/// @file MultiChipStream.hpp
/// @brief MultiChipSequencer の曲を小さな塊ずつ取り出すストリーミング
/// @details read() が呼ばれた分だけチップを進める。チップへの generate() は一括処理と同じ tick の区切りで呼ぶため、分割単位を変えても render() と同じ float / PCM になる。
/// 層はチップごとに 1 つあり、実行中に音量を変更できる。詳細は LayerGains.hpp を参照。
///
/// 層を外してもチップは鳴り続けるため、戻したときにほかの層とずれない。ゲインの変化は出力フレーム番号で決まり、実時間や乱数には依存しない。
/// startFrame が processedFrames() より前の場合は、次のフレームから変化する。この場合だけ、結果が read() の分割単位に依存する。

#include <mitiru_mml/ChipStreamSource.hpp>
#include <mitiru_mml/LayerGains.hpp>
#include <mitiru_mml/MultiChipSequencer.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace mitiru_mml
{

class MultiChipStream
{
public:
	/// @brief チップを reset() し、各トラックの音色を設定して出力を開始する
	/// @param sequencer stream より長く保持し、使用中はほかの render() や stream から使わない
	/// @param loopCount 曲を流す回数。render() の引数と同じ
	explicit MultiChipStream(MultiChipSequencer& sequencer, int loopCount = 1)
		: m_options(sequencer.m_options)
	{
		if (sequencer.m_trackEvents.empty())
		{
			m_layers = LayerControl(sequencer.m_chips.size(), TickGrid{});
			return;
		}
		std::vector<SeqEvent> merged;
		for (const auto& list : sequencer.m_trackEvents)
		{
			merged.insert(merged.end(), list.begin(), list.end());
		}
		sortSeqEvents(merged);
		const auto events = std::make_shared<const std::vector<SeqEvent>>(std::move(merged));

		ChipStreamSource::Config config;
		config.outputRate = m_options.outputRate;
		config.initialBpm = 120;
		config.tailNativeFrames = 0;
		config.allowDirect = false;
		for (std::size_t c = 0; c < sequencer.m_chips.size(); ++c)
		{
			ChipDriver& driver = *sequencer.m_chips[c].driver;
			driver.reset();
			for (const auto& v : sequencer.m_voices)
			{
				if (static_cast<std::size_t>(v.chip) == c)
				{
					driver.setVoice(v.channel, v.voice);
				}
			}
			m_mix.addSource(ChipStreamSource(driver, static_cast<std::uint32_t>(c), events, loopCount, config),
				sequencer.m_chips[c].gain);
		}
		m_layers = LayerControl(sequencer.m_chips.size(), TickGrid(*events, loopCount, m_options.outputRate, config.initialBpm));
	}

	[[nodiscard]] std::uint32_t sampleRate() const noexcept
	{
		return m_options.outputRate;
	}

	/// @brief frames フレームを float ステレオ（L, R, L, R...）で取り出す。値は render() の対応する部分と同じ
	/// @param out frames * 2 個の float を書き込める領域
	/// @return 書き込んだフレーム数。曲の終わりでは frames より少なく、終了後は 0
	[[nodiscard]] std::size_t read(float* out, std::size_t frames)
	{
		const std::size_t n = m_mix.read(out, frames, &m_layers.tracks());
		for (std::size_t i = 0; i < n * 2; ++i)
		{
			out[i] = MultiChipSequencer::masterSample(out[i], m_options);
		}
		m_position += n;
		return n;
	}

	/// @brief frames フレームを 16 bit ステレオで取り出す。変換は StereoBuffer::toPcm16Stereo と同じ
	[[nodiscard]] std::size_t readPcm16(std::int16_t* out, std::size_t frames)
	{
		m_scratch.resize(frames * 2);
		const std::size_t n = read(m_scratch.data(), frames);
		for (std::size_t i = 0; i < n * 2; ++i)
		{
			out[i] = StereoBuffer::floatToPcm16(m_scratch[i]);
		}
		return n;
	}

	[[nodiscard]] bool finished()
	{
		return m_mix.exhausted();
	}

	[[nodiscard]] std::uint64_t position() const noexcept
	{
		return m_position;
	}

	/// @brief 層はチップごとに 1 つあり、番号は addChip() の戻り値と同じ
	[[nodiscard]] std::size_t layerCount() const noexcept
	{
		return m_layers.layerCount();
	}

	/// @brief startFrame から rampFrames かけて、層のゲインを target に変える。0 は無音、1 は元の音量
	/// @throws std::out_of_range 層の番号が範囲外のとき
	void scheduleLayerGain(std::size_t layer, std::uint64_t startFrame, float target, std::uint32_t rampFrames,
		FadeCurve curve = FadeCurve::EqualPower)
	{
		m_layers.schedule(layer, startFrame, target, rampFrames, curve);
	}

	/// @brief atOrAfterFrame 以降の最初の小節の頭から、層のゲインを変える
	/// @param ticksPerBar 1 小節の tick 数。既定値は 4/4 拍子の 192。拍の頭には TickGrid::TICKS_PER_BEAT を使う
	/// @return 変化を始めるフレーム番号。曲の終わりまでに境界がなければ atOrAfterFrame
	std::uint64_t scheduleLayerGainAtBar(std::size_t layer, std::uint64_t atOrAfterFrame, float target,
		std::uint32_t rampFrames, FadeCurve curve = FadeCurve::EqualPower, std::int64_t ticksPerBar = TickGrid::TICKS_PER_BAR_4_4)
	{
		return m_layers.scheduleAtBoundary(layer, atOrAfterFrame, target, rampFrames, curve, ticksPerBar);
	}

	[[nodiscard]] std::uint64_t nextBarFrame(std::uint64_t atOrAfterFrame, std::int64_t ticksPerBar = TickGrid::TICKS_PER_BAR_4_4) const
	{
		return m_layers.nextBoundary(atOrAfterFrame, ticksPerBar);
	}

	/// @brief ゲインを処理済みのフレーム数。これ以降を指す命令は、read() の分割単位によらず同じ結果になる
	[[nodiscard]] std::uint64_t processedFrames() const noexcept
	{
		return m_layers.processedFrames();
	}

private:
	MultiChipSequencer::Options m_options;
	ChipMixStream m_mix;
	LayerControl m_layers;
	std::uint64_t m_position = 0;
	std::vector<float> m_scratch;
};

} // namespace mitiru_mml
