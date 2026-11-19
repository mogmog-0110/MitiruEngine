#pragma once
/// @file ChipStreamSource.hpp
/// @brief チップごとの引き出し式音源と、複数の音源を足す処理
/// @details MultiChipSequencer::render と ModernSong::render と同じ手順を 1 tick ずつ進める。
/// generate() の単位、箱型フィルターへ渡す区切り、時計の端数の持ち越しを一括レンダリングと揃える。
/// 取り出すフレーム数や Device が処理単位の大きさに左右されても、一括レンダリングと同じビット列を出力する。
/// 取り出すフレーム数は、内部の FIFO から取り出す量だけに影響する。

#include <mitiru_mml/BoxDecimator.hpp>
#include <mitiru_mml/ChipDriver.hpp>
#include <mitiru_mml/LayerGains.hpp>
#include <mitiru_mml/Timeline.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace mitiru_mml
{

/// @brief チップ 1 つの出力を、出力レートの float ステレオで少しずつ返す
/// @details Driver は所有しない。作成前に Driver の reset() と setVoice() を呼ぶ。
class ChipStreamSource
{
public:
	/// @brief 一括レンダリングの 2 経路の違い
	struct Config
	{
		std::uint32_t outputRate = 44100;
		int initialBpm = 120;
		std::uint64_t tailNativeFrames = 0;  ///< 最後のイベントのあとに、ネイティブレートで描き足すフレーム数
		bool allowDirect = false;            ///< ネイティブ (を整数倍にした) レートが出力と同じなら、箱型フィルターを通さない
	};

	/// @param events このチップの宛先 (chipIndex) 以外のイベントも含む、並べ済みの全イベント
	ChipStreamSource(ChipDriver& driver, std::uint32_t chipIndex,
		std::shared_ptr<const std::vector<SeqEvent>> events, int loopCount, const Config& config)
		: m_driver(&driver)
		, m_device(&driver.device())
		, m_chipIndex(chipIndex)
		, m_events(std::move(events))
		, m_clock(m_device->nativeRate(), TickClock::Rounding::CarryRemainder, config.initialBpm)
		, m_cursor(*m_events, loopCount)
		, m_decimator(m_device->nativeRate() * repeatFor(m_device->nativeRate(), config.outputRate), config.outputRate)
		, m_tailLeft(config.tailNativeFrames)
	{
		const std::uint32_t native = m_device->nativeRate();
		m_repeat = repeatFor(native, config.outputRate);
		m_direct = config.allowDirect && (native * m_repeat == config.outputRate);
	}

	[[nodiscard]] bool done() const noexcept
	{
		return m_done;
	}

	/// @brief 外へ渡せるフレーム数。打ち切りで捨てられる可能性があるフレームは含めない
	[[nodiscard]] std::uint64_t available() const noexcept
	{
		const std::uint64_t buffered = (m_fifo.size() - m_head) / 2;
		const std::uint64_t emitted = m_consumed + buffered;
		const std::uint64_t safe = m_direct ? emitted : std::min(emitted, m_decimator.finalizedFrames());
		return safe - m_consumed;
	}

	/// @brief available() が minFrames 以上になるか、最後まで進める
	void produce(std::uint64_t minFrames)
	{
		while (available() < minFrames && step())
		{
		}
	}

	/// @brief 外へ渡せる先頭フレームへのポインター。L、R の順で available() フレーム読める
	[[nodiscard]] const float* peek() const noexcept
	{
		return m_fifo.data() + m_head;
	}

	void consume(std::uint64_t frames)
	{
		m_consumed += frames;
		m_head += static_cast<std::size_t>(frames) * 2;
		if (m_head >= 32768 && m_head * 2 >= m_fifo.size())
		{
			m_fifo.erase(m_fifo.begin(), m_fifo.begin() + static_cast<std::ptrdiff_t>(m_head));
			m_head = 0;
		}
	}

private:
	[[nodiscard]] static std::uint32_t repeatFor(std::uint32_t native, std::uint32_t outputRate) noexcept
	{
		return (native >= outputRate) ? 1u : (outputRate + native - 1) / native;
	}

	bool step()
	{
		if (m_done)
		{
			return false;
		}
		if (!m_cursor.finished())
		{
			const std::uint32_t samples = m_cursor.step(m_clock, [this](const SeqEvent& ev)
				{
					if (ev.chip == m_chipIndex)
					{
						applySeqEvent(*m_driver, ev);
					}
				});
			if (samples > 0)
			{
				renderFrames(samples);
			}
			return true;
		}
		if (m_tailLeft > 0)
		{
			const auto chunk = static_cast<std::uint32_t>(std::min<std::uint64_t>(m_tailLeft, 4096));
			renderFrames(chunk);
			m_tailLeft -= chunk;
			return true;
		}
		m_done = true;
		return false;
	}

	void renderFrames(std::uint32_t frames)
	{
		m_block.resize(static_cast<std::size_t>(frames) * 2);
		m_device->generate(m_block.data(), frames);
		if (m_direct)
		{
			m_fifo.insert(m_fifo.end(), m_block.begin(), m_block.end());
		}
		else if (m_repeat == 1)
		{
			m_decimator.push(m_block.data(), frames, m_fifo);
		}
		else
		{
			m_held.resize(static_cast<std::size_t>(frames) * m_repeat * 2);
			for (std::size_t i = 0; i < frames; ++i)
			{
				for (std::uint32_t r = 0; r < m_repeat; ++r)
				{
					m_held[(i * m_repeat + r) * 2] = m_block[i * 2];
					m_held[(i * m_repeat + r) * 2 + 1] = m_block[i * 2 + 1];
				}
			}
			m_decimator.push(m_held.data(), static_cast<std::size_t>(frames) * m_repeat, m_fifo);
		}
	}

	ChipDriver* m_driver;
	ChipDevice* m_device;
	std::uint32_t m_chipIndex;
	std::shared_ptr<const std::vector<SeqEvent>> m_events;
	TickClock m_clock;
	TimelineCursor m_cursor;
	BoxDecimatorStereo m_decimator;
	std::uint64_t m_tailLeft;
	std::uint32_t m_repeat = 1;
	bool m_direct = false;
	bool m_done = false;
	std::vector<float> m_fifo;
	std::size_t m_head = 0;
	std::uint64_t m_consumed = 0;
	std::vector<float> m_block;
	std::vector<float> m_held;
};

/// @brief 音源をチップ番号順に足す
/// @details ゲインを掛けてから足す順序は、MultiChipSequencer::render と ModernSong のグループ処理と同じ。先に終わった音源は無音として足す。
class ChipMixStream
{
public:
	void addSource(ChipStreamSource source, float gain)
	{
		m_sources.push_back(Entry{std::move(source), gain});
	}

	[[nodiscard]] std::size_t sourceCount() const noexcept
	{
		return m_sources.size();
	}

	/// @brief 最大 frames フレームを out に L、R の順で書く
	/// @param layers null でなければ、音源 i のゲインのあとに layers[i] の層のゲインを掛ける
	/// @return 書いたフレーム数。最後まで来たときは frames より少なくなる
	[[nodiscard]] std::size_t read(float* out, std::size_t frames, std::vector<LayerGainTrack>* layers = nullptr)
	{
		if (m_sources.empty() || frames == 0)
		{
			return 0;
		}
		bool allDone = true;
		std::uint64_t longest = 0;
		for (auto& e : m_sources)
		{
			e.source.produce(frames);
			allDone = allDone && e.source.done();
			longest = std::max(longest, e.source.available());
		}
		const std::size_t n = allDone ? static_cast<std::size_t>(std::min<std::uint64_t>(frames, longest)) : frames;
		std::fill(out, out + n * 2, 0.0f);
		for (std::size_t s = 0; s < m_sources.size(); ++s)
		{
			Entry& e = m_sources[s];
			const auto avail = static_cast<std::size_t>(std::min<std::uint64_t>(n, e.source.available()));
			const float* src = e.source.peek();
			LayerGainTrack* track = layers ? &(*layers)[s] : nullptr;
			for (std::size_t i = 0; i < n; ++i)
			{
				const float layerGain = track ? track->next() : 1.0f;
				if (i >= avail)
				{
					continue;
				}
				float l = src[i * 2] * e.gain;
				float r = src[i * 2 + 1] * e.gain;
				if (layerGain != 1.0f)
				{
					l *= layerGain;
					r *= layerGain;
				}
				out[i * 2] += l;
				out[i * 2 + 1] += r;
			}
			e.source.consume(avail);
		}
		return n;
	}

	/// @brief もう 1 フレームも出ないか。確認のために少し進めることがある
	[[nodiscard]] bool exhausted()
	{
		for (auto& e : m_sources)
		{
			e.source.produce(1);
			if (e.source.available() > 0)
			{
				return false;
			}
		}
		return true;
	}

private:
	struct Entry
	{
		ChipStreamSource source;
		float gain;
	};

	std::vector<Entry> m_sources;
};

} // namespace mitiru_mml
