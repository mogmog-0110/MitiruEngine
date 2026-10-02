#pragma once

/// @file MusicRenderer.hpp
/// @brief MusicDirector の命令を指定のサンプルで実行し、音源を混ぜる
/// @details push / popFinished は命令を出すスレッド、render は音声スレッドから呼ぶ。それぞれ 1 本に限る。
/// 命令の時刻には director の時計を使う。最初の命令で render の位置と director の時刻との差を決め、先読みの lookahead を足す。以後はその差を保つ。
/// デバイスを開かずに読む場合、render の位置と director の時刻は一致し、差は 0 になる。
/// 時刻を過ぎて届いた命令はすぐ実行し、lateCommands() に数える。render はメモリを確保しない。

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include <mitiru/audio/RingBuffer.hpp>
#include <mitiru/audio/music/MusicCommand.hpp>

namespace mitiru::audio::music
{

class MusicRenderer
{
public:
	static constexpr std::size_t kMaxInstances = 16;
	static constexpr std::size_t kMaxLayers = 16;
	static constexpr std::size_t kChunkFrames = 512;

	explicit MusicRenderer(std::uint32_t channels, std::int64_t lookahead = 0)
		: m_channels(channels > 0 ? channels : 2)
		, m_lookahead(lookahead)
		, m_commands(256)
		, m_finished(kMaxInstances * 4)
		, m_scratch(kChunkFrames * m_channels)
		, m_gain(kChunkFrames)
	{
		m_layers.fill(Ramp{});
	}

	/// @brief 命令を渡す。待ち行列が一杯なら false を返す
	bool push(const MusicCommand& c) noexcept { return m_commands.write(&c, 1) == 1; }

	/// @brief 鳴り終わった、または止めた instance の番号を取り出す。取り出した instance の読み手は以後参照されない
	std::size_t popFinished(std::uint32_t* ids, std::size_t max) noexcept { return m_finished.read(ids, max); }

	/// @brief out へ frames フレームを上書きする
	void render(float* out, std::uint64_t frames) noexcept
	{
		std::fill(out, out + frames * m_channels, 0.0f);
		const std::int64_t base = m_pos.load(std::memory_order_relaxed);
		const std::int64_t end = base + static_cast<std::int64_t>(frames);
		std::int64_t pos = base;
		while (pos < end)
		{
			applyDue(pos);
			const std::int64_t next = m_hasHeld ? std::min(end, m_held.at) : end;
			mixSpan(out + (pos - base) * m_channels, pos, next - pos);
			pos = next;
		}
		m_pos.store(end, std::memory_order_release);
	}

	[[nodiscard]] std::int64_t position() const noexcept { return m_pos.load(std::memory_order_acquire); }
	[[nodiscard]] std::uint64_t lateCommands() const noexcept { return m_late.load(std::memory_order_relaxed); }
	[[nodiscard]] std::uint64_t droppedInstances() const noexcept { return m_dropped.load(std::memory_order_relaxed); }

private:
	struct Ramp
	{
		float        from = 1.0f;
		float        to = 1.0f;
		std::int64_t start = 0;
		std::int64_t length = 0;

		[[nodiscard]] float at(std::int64_t t) const noexcept
		{
			if (t >= start + length) { return to; }
			if (t <= start) { return from; }
			return from + (to - from) * static_cast<float>(t - start) / static_cast<float>(length);
		}
		void retarget(std::int64_t t, float target, std::int64_t len) noexcept
		{
			from = at(t);
			to = target;
			start = t;
			length = std::max<std::int64_t>(len, 0);
		}
	};

	struct Instance
	{
		std::uint32_t id = 0;
		bool          active = false;
		bool          stopping = false;
		std::int64_t  stopEnd = 0;
		Ramp          gain;
		std::uint8_t  stemCount = 0;
		std::array<MusicStemVoice, kMaxStemsPerInstance> stems{};
		std::array<bool, kMaxStemsPerInstance> ended{};
	};

	bool fetch() noexcept
	{
		if (m_commands.read(&m_held, 1) != 1) { return false; }
		if (!m_latched)
		{
			m_offset = std::max<std::int64_t>(0, m_pos.load(std::memory_order_relaxed) - m_held.at) + m_lookahead;
			m_latched = true;
		}
		m_held.at += m_offset;
		m_hasHeld = true;
		return true;
	}

	void applyDue(std::int64_t pos) noexcept
	{
		while (m_hasHeld || fetch())
		{
			if (m_held.at > pos) { return; }
			if (m_held.at < pos) { m_late.fetch_add(1, std::memory_order_relaxed); m_held.at = pos; }
			apply(m_held);
			m_hasHeld = false;
		}
	}

	void apply(const MusicCommand& c) noexcept
	{
		switch (c.kind)
		{
			case MusicCommandKind::StartInstance: start(c); break;
			case MusicCommandKind::StopInstance:  stop(c); break;
			case MusicCommandKind::LayerGain:
				if (c.layer >= 0 && static_cast<std::size_t>(c.layer) < kMaxLayers)
				{
					m_layers[static_cast<std::size_t>(c.layer)].retarget(c.at, c.gain, c.length);
				}
				break;
			case MusicCommandKind::MasterGain: m_master.retarget(c.at, c.gain, c.length); break;
		}
	}

	void start(const MusicCommand& c) noexcept
	{
		const auto slot = std::find_if(m_instances.begin(), m_instances.end(), [](const Instance& i) { return !i.active; });
		if (slot == m_instances.end())
		{
			m_dropped.fetch_add(1, std::memory_order_relaxed);
			m_finished.write(&c.instance, 1);  // 読み手を返してもらうため、鳴らさずに終わったことにする
			return;
		}
		Instance& inst = *slot;
		inst = Instance{};
		inst.id = c.instance;
		inst.active = true;
		inst.gain = (c.length > 0) ? Ramp{0.0f, c.gain, c.at, c.length} : Ramp{c.gain, c.gain, c.at, 0};
		inst.stemCount = std::min<std::uint8_t>(c.stemCount, static_cast<std::uint8_t>(kMaxStemsPerInstance));
		std::copy_n(c.stems.begin(), inst.stemCount, inst.stems.begin());
	}

	void stop(const MusicCommand& c) noexcept
	{
		for (Instance& inst : m_instances)
		{
			if (!inst.active || (c.instance != 0 && inst.id != c.instance)) { continue; }
			if (c.length <= 0) { finish(inst); continue; }
			inst.gain.retarget(c.at, 0.0f, c.length);
			inst.stopping = true;
			inst.stopEnd = c.at + c.length;
		}
	}

	void finish(Instance& inst) noexcept
	{
		inst.active = false;
		m_finished.write(&inst.id, 1);
	}

	void mixSpan(float* dst, std::int64_t t0, std::int64_t frames) noexcept
	{
		for (std::int64_t done = 0; done < frames;)
		{
			const auto n = static_cast<std::size_t>(std::min<std::int64_t>(frames - done, kChunkFrames));
			for (Instance& inst : m_instances)
			{
				if (inst.active) { mixInstance(inst, dst + done * m_channels, t0 + done, n); }
			}
			done += static_cast<std::int64_t>(n);
		}
	}

	void mixInstance(Instance& inst, float* dst, std::int64_t t0, std::size_t n) noexcept
	{
		for (std::size_t i = 0; i < n; ++i)
		{
			const auto t = t0 + static_cast<std::int64_t>(i);
			m_gain[i] = inst.gain.at(t) * m_master.at(t);
		}
		bool anyLeft = false;
		for (std::size_t s = 0; s < inst.stemCount; ++s)
		{
			if (inst.ended[s] || inst.stems[s].reader == nullptr) { continue; }
			const std::uint64_t got = inst.stems[s].reader->read(m_scratch.data(), n);
			if (got < n) { inst.ended[s] = true; } else { anyLeft = true; }
			addStem(inst.stems[s], dst, t0, static_cast<std::size_t>(got));
		}
		if (!anyLeft || (inst.stopping && t0 + static_cast<std::int64_t>(n) >= inst.stopEnd)) { finish(inst); }
	}

	void addStem(const MusicStemVoice& stem, float* dst, std::int64_t t0, std::size_t n) noexcept
	{
		const bool layered = stem.layer >= 0 && static_cast<std::size_t>(stem.layer) < kMaxLayers;
		const Ramp& layer = layered ? m_layers[static_cast<std::size_t>(stem.layer)] : m_unity;
		for (std::size_t i = 0; i < n; ++i)
		{
			const float g = m_gain[i] * stem.gain * layer.at(t0 + static_cast<std::int64_t>(i));
			for (std::uint32_t ch = 0; ch < m_channels; ++ch)
			{
				dst[i * m_channels + ch] += m_scratch[i * m_channels + ch] * g;
			}
		}
	}

	std::uint32_t                         m_channels;
	std::int64_t                          m_lookahead;
	RingBuffer<MusicCommand>              m_commands;
	RingBuffer<std::uint32_t>             m_finished;
	std::vector<float>                    m_scratch;
	std::vector<float>                    m_gain;
	std::array<Instance, kMaxInstances>   m_instances{};
	std::array<Ramp, kMaxLayers>          m_layers{};
	Ramp                                  m_master;
	Ramp                                  m_unity;
	MusicCommand                          m_held{};
	bool                                  m_hasHeld = false;
	bool                                  m_latched = false;
	std::int64_t                          m_offset = 0;
	std::atomic<std::int64_t>             m_pos{0};
	std::atomic<std::uint64_t>            m_late{0};
	std::atomic<std::uint64_t>            m_dropped{0};
};

}  // namespace mitiru::audio::music
