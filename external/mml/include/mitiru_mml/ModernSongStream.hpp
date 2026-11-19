#pragma once
/// @file ModernSongStream.hpp
/// @brief ModernSong の曲を小さな塊ずつ取り出すストリーミング
/// @details 曲全体を先に生成せず、read() で取り出す分だけチップを進める。区切り方を変えても、ModernSong::render() の mix と同じ float になり、readPcm16() は toPcm16() と同じ PCM になる。
/// グループごとのチップ、ダッキング、グループとマスターのゲイン、ステレオ幅、リミッター、ディザは、一括版と同じ式を同じ順序で 1 フレームずつ処理する。処理は StreamingDynamics.hpp にある。
/// トラックの send= (リバーブ、ディレイ、コーラス) も一括版と同じ SendBus を通す。送りのあるトラックは、そのトラックだけを描くチップをもう 1 組持つ。
/// リミッターの先読みは既定で 3 ms で、その分だけ出力が入力より遅れる。
///
/// @par ゲームの状態による層の切り替え
/// 層はグループ 1 つに 1 つ。外した層も処理を続けるため、戻したときに小節がずれない。
/// 層のゲインは、グループのゲインとダッキングのあと、グループを足す前に掛かる。外した kick の #duck も低音を下げ続ける。
/// 層を外すと、そのグループのトラックの送りも止まる (エフェクトに残った残響は、そのまま鳴り終わる)。
/// ゲインの動きは出力のフレーム番号で決まり、実時間と乱数は使わない。
/// startFrame が processedFrames() より前なら、次のフレームから動き始める。この場合だけ、結果が read() の区切り方に依存する。
/// processedFrames() は position() より、リミッターの先読みと内部のまとまりの分だけ先に進む。ゲーム側では position() に十分な余裕を足したフレームか、小節の頭を指定する。

#include <mitiru_mml/ChipStreamSource.hpp>
#include <mitiru_mml/LayerGains.hpp>
#include <mitiru_mml/ModernSong.hpp>
#include <mitiru_mml/StreamingDynamics.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mitiru_mml
{

class ModernSongStream
{
public:
	/// @brief 曲の設定をコピーし、グループごとのチップを作る
	/// @throws std::invalid_argument ModernSong::render() と同じ条件を満たさないとき
	explicit ModernSongStream(const ModernSong& song)
		: m_options(song.m_options)
		, m_master(dbToLinear(song.m_options.masterGainDb))
		, m_rng(0x2545F491u)
	{
		const ModernSong::Prepared prep = song.prepare();
		const std::size_t groupCount = song.m_groups.size();

		for (std::size_t g = 0; g < groupCount; ++g)
		{
			m_groupNames.push_back(song.m_groups[g].name);
			m_groupGain.push_back(dbToLinear(song.m_groups[g].gainDb));
		}

		const int initialTempo = song.initialTempo();
		m_drivers.reserve(groupCount);
		for (std::size_t g = 0; g < groupCount; ++g)
		{
			m_drivers.push_back(song.makeGroupDrivers(g, prep.plan));
			const auto events = std::make_shared<const std::vector<SeqEvent>>(prep.groupEvents[g]);
			ChipMixStream mix;
			for (std::size_t k = 0; k < m_drivers[g].size(); ++k)
			{
				if (!m_drivers[g][k])
				{
					continue;
				}
				ChipDriver& driver = *m_drivers[g][k];
				ChipStreamSource::Config config;
				config.outputRate = m_options.sampleRate;
				config.initialBpm = initialTempo;
				config.tailNativeFrames = static_cast<std::uint64_t>(
					m_options.tailSeconds * static_cast<double>(driver.device().nativeRate()));
				config.allowDirect = true;
				mix.addSource(ChipStreamSource(driver, static_cast<std::uint32_t>(k), events, m_options.loopCount, config), 1.0f);
			}
			m_groups.push_back(std::move(mix));
		}

		const ModernSong::SendPlan sendPlan = song.planSends();
		if (sendPlan.any())
		{
			m_bus.emplace(m_options.sendReverb, m_options.sendDelay, m_options.sendChorus, m_options.sampleRate, sendPlan.used);
			m_sendSources.reserve(sendPlan.tracks.size());
			for (const std::size_t t : sendPlan.tracks)
			{
				SendSource send;
				send.group = static_cast<std::size_t>(song.findGroup(song.m_tracks[t].group));
				send.amounts = song.m_tracks[t].sends;
				send.drivers = song.makeTrackDrivers(t, prep.plan);
				const auto events = std::make_shared<const std::vector<SeqEvent>>(song.isolatedEvents(t, prep));
				for (std::size_t k = 0; k < send.drivers.size(); ++k)
				{
					if (!send.drivers[k])
					{
						continue;
					}
					ChipDriver& driver = *send.drivers[k];
					ChipStreamSource::Config config;
					config.outputRate = m_options.sampleRate;
					config.initialBpm = initialTempo;
					config.tailNativeFrames = static_cast<std::uint64_t>(
						m_options.tailSeconds * static_cast<double>(driver.device().nativeRate()));
					config.allowDirect = true;
					send.stream.addSource(ChipStreamSource(driver, static_cast<std::uint32_t>(k), events, m_options.loopCount, config), 1.0f);
				}
				m_sendSources.push_back(std::move(send));
			}
		}

		std::map<std::string, std::vector<std::uint64_t>> triggerCache;
		for (const auto& rule : song.m_ducks)
		{
			DuckRuntime runtime;
			for (const auto& target : rule.targets)
			{
				runtime.targets.push_back(static_cast<std::size_t>(song.findGroup(target)));
			}
			if (rule.signal)
			{
				runtime.keyGroup = static_cast<std::size_t>(song.findGroup(rule.by));
				runtime.sidechain.emplace(m_options.sampleRate, rule.sidechain);
				m_needKeys = true;
			}
			else
			{
				if (triggerCache.find(rule.by) == triggerCache.end())
				{
					triggerCache[rule.by] = song.triggerPositions(prep.trackEvents, prep.shared, rule.by);
				}
				runtime.curve.emplace(triggerCache[rule.by], m_options.sampleRate, rule.duck);
			}
			m_ducks.push_back(std::move(runtime));
		}

		if (m_options.limiter)
		{
			m_limiter.emplace(m_options.limit, m_options.sampleRate);
		}

		std::vector<SeqEvent> all;
		for (const auto& list : prep.trackEvents)
		{
			all.insert(all.end(), list.begin(), list.end());
		}
		sortSeqEvents(all);
		m_layers = LayerControl(groupCount, TickGrid(all, m_options.loopCount, m_options.sampleRate, initialTempo));

		m_raw.resize(groupCount);
		m_keys.resize(groupCount);
	}

	[[nodiscard]] std::uint32_t sampleRate() const noexcept
	{
		return m_options.sampleRate;
	}

	/// @brief frames フレームを float ステレオ (L, R, L, R...) で取り出す。値は render() の mix の続きと同じ
	/// @param out frames * 2 個の float が入る領域
	/// @return 書き込んだフレーム数。曲の終わりでは frames より少なくなり、終了後は 0
	[[nodiscard]] std::size_t read(float* out, std::size_t frames)
	{
		fill(frames);
		const std::size_t n = std::min(frames, readyFrames());
		std::copy(m_ready.begin() + static_cast<std::ptrdiff_t>(m_readyHead),
			m_ready.begin() + static_cast<std::ptrdiff_t>(m_readyHead + n * 2), out);
		dropReady(n);
		m_position += n;
		return n;
	}

	/// @brief frames フレームを 16 bit ステレオで取り出す。Options::dither が true ならディザを加え、toPcm16() と同じ方法で変換する
	/// @details ディザの乱数は取り出した順に進むため、read() と混ぜず、どちらか一方を使い続ける。
	[[nodiscard]] std::size_t readPcm16(std::int16_t* out, std::size_t frames)
	{
		m_scratch.resize(frames * 2);
		const std::size_t n = read(m_scratch.data(), frames);
		for (std::size_t i = 0; i < n * 2; ++i)
		{
			if (!m_options.dither)
			{
				out[i] = StereoBuffer::floatToPcm16(m_scratch[i]);
				continue;
			}
			const float noise = 0.5f * (m_rng.nextSigned() + m_rng.nextSigned());
			const float scaled = std::clamp(m_scratch[i], -1.0f, 1.0f) * 32767.0f + noise;
			out[i] = static_cast<std::int16_t>(std::clamp(std::lrint(scaled), -32768L, 32767L));
		}
		return n;
	}

	[[nodiscard]] bool finished()
	{
		fill(1);
		return readyFrames() == 0;
	}

	/// @brief ここまでに取り出したフレーム数
	[[nodiscard]] std::uint64_t position() const noexcept
	{
		return m_position;
	}

	/// @brief ここまでのリミッターの最大ゲインリダクション。単位は dB で 0 以下。リミッターが無ければ 0
	[[nodiscard]] float limiterReductionDb() const noexcept
	{
		return m_limiter ? m_limiter->reductionDb() : 0.0f;
	}

	/// @brief 層の数。グループの数と同じ
	[[nodiscard]] std::size_t layerCount() const noexcept
	{
		return m_layers.layerCount();
	}

	/// @brief グループ名に対応する層の番号。無ければ layerCount()
	[[nodiscard]] std::size_t findLayer(const std::string& groupName) const
	{
		const auto it = std::find(m_groupNames.begin(), m_groupNames.end(), groupName);
		return static_cast<std::size_t>(it - m_groupNames.begin());
	}

	/// @brief startFrame から rampFrames フレームかけて、層のゲインを target に変える。0 は無音、1 は元の音量
	/// @throws std::out_of_range 層の番号が範囲外のとき
	void scheduleLayerGain(std::size_t layer, std::uint64_t startFrame, float target, std::uint32_t rampFrames,
		FadeCurve curve = FadeCurve::EqualPower)
	{
		m_layers.schedule(layer, startFrame, target, rampFrames, curve);
	}

	/// @brief atOrAfterFrame 以降で最初の小節の頭から、層のゲインを動かす
	/// @param ticksPerBar 1 小節の tick 数。既定は 4/4 拍子の 192。拍の頭を使う場合は TickGrid::TICKS_PER_BEAT
	/// @return 動き始めるフレーム番号。曲の最後までに境界が無ければ atOrAfterFrame
	std::uint64_t scheduleLayerGainAtBar(std::size_t layer, std::uint64_t atOrAfterFrame, float target,
		std::uint32_t rampFrames, FadeCurve curve = FadeCurve::EqualPower, std::int64_t ticksPerBar = TickGrid::TICKS_PER_BAR_4_4)
	{
		return m_layers.scheduleAtBoundary(layer, atOrAfterFrame, target, rampFrames, curve, ticksPerBar);
	}

	[[nodiscard]] std::uint64_t nextBarFrame(std::uint64_t atOrAfterFrame, std::int64_t ticksPerBar = TickGrid::TICKS_PER_BAR_4_4) const
	{
		return m_layers.nextBoundary(atOrAfterFrame, ticksPerBar);
	}

	/// @brief 層のゲインを処理済みのフレーム数。これ以降を指定した命令は、処理の区切り方によらず同じ結果になる
	[[nodiscard]] std::uint64_t processedFrames() const noexcept
	{
		return m_layers.processedFrames();
	}

private:
	static constexpr std::size_t BLOCK_FRAMES = 1024;

	/// @brief send= のあるトラック 1 本の、そのトラックだけを描くチップとストリーム
	struct SendSource
	{
		std::size_t group = 0;
		SendBus::Amounts amounts{};
		ModernSong::DriverSet drivers;
		ChipMixStream stream;
		std::vector<float> raw;
	};

	struct DuckRuntime
	{
		std::vector<std::size_t> targets;
		std::size_t keyGroup = 0;
		std::optional<StreamingDuckCurve> curve;
		std::optional<StreamingSidechain> sidechain;
	};

	[[nodiscard]] std::size_t readyFrames() const noexcept
	{
		return (m_ready.size() - m_readyHead) / 2;
	}

	void dropReady(std::size_t frames)
	{
		m_readyHead += frames * 2;
		if (m_readyHead >= 65536 && m_readyHead * 2 >= m_ready.size())
		{
			m_ready.erase(m_ready.begin(), m_ready.begin() + static_cast<std::ptrdiff_t>(m_readyHead));
			m_readyHead = 0;
		}
	}

	void fill(std::size_t frames)
	{
		while (readyFrames() < frames && !m_inputEnded)
		{
			processBlock();
		}
	}

	void pullLimiter()
	{
		const std::size_t n = m_limiter->available();
		m_ready.insert(m_ready.end(), m_limiter->peek(), m_limiter->peek() + n * 2);
		m_limiter->consume(n);
	}

	void processBlock()
	{
		const std::size_t groupCount = m_groups.size();
		std::size_t frames = 0;
		for (std::size_t g = 0; g < groupCount; ++g)
		{
			m_raw[g].assign(BLOCK_FRAMES * 2, 0.0f);
			frames = std::max(frames, m_groups[g].read(m_raw[g].data(), BLOCK_FRAMES));
		}
		if (frames == 0)
		{
			m_inputEnded = true;
			if (m_limiter)
			{
				m_limiter->finish();
				pullLimiter();
			}
			return;
		}

		for (SendSource& send : m_sendSources)
		{
			send.raw.assign(BLOCK_FRAMES * 2, 0.0f);
			(void)send.stream.read(send.raw.data(), BLOCK_FRAMES);
		}

		// グループのゲインを掛けたあと、ダッキングを掛ける前の音をダッキングの検出に使う
		for (std::size_t g = 0; g < groupCount; ++g)
		{
			for (std::size_t i = 0; i < frames * 2; ++i)
			{
				m_raw[g][i] *= m_groupGain[g];
			}
		}
		for (SendSource& send : m_sendSources)
		{
			for (std::size_t i = 0; i < frames * 2; ++i)
			{
				send.raw[i] *= m_groupGain[send.group];
			}
		}
		if (m_needKeys)
		{
			m_keys = m_raw;
		}

		m_gains.resize(frames);
		for (auto& rule : m_ducks)
		{
			for (std::size_t i = 0; i < frames; ++i)
			{
				m_gains[i] = rule.sidechain
					? rule.sidechain->next(m_keys[rule.keyGroup][i * 2], m_keys[rule.keyGroup][i * 2 + 1])
					: rule.curve->next();
			}
			for (const std::size_t target : rule.targets)
			{
				for (std::size_t i = 0; i < frames; ++i)
				{
					m_raw[target][i * 2] *= m_gains[i];
					m_raw[target][i * 2 + 1] *= m_gains[i];
				}
				for (SendSource& send : m_sendSources)
				{
					if (send.group != target)
					{
						continue;
					}
					for (std::size_t i = 0; i < frames; ++i)
					{
						send.raw[i * 2] *= m_gains[i];
						send.raw[i * 2 + 1] *= m_gains[i];
					}
				}
			}
		}

		for (std::size_t g = 0; g < groupCount; ++g)
		{
			LayerGainTrack& track = m_layers.tracks()[g];
			m_layerGain.resize(frames);
			for (std::size_t i = 0; i < frames; ++i)
			{
				m_layerGain[i] = track.next();
			}
			applyLayerGain(m_raw[g], frames);
			for (SendSource& send : m_sendSources)
			{
				if (send.group == g)
				{
					applyLayerGain(send.raw, frames);
				}
			}
		}

		m_mix.sampleRate = m_options.sampleRate;
		m_mix.samples.assign(frames * 2, 0.0f);
		for (std::size_t g = 0; g < groupCount; ++g)
		{
			for (std::size_t i = 0; i < frames * 2; ++i)
			{
				m_mix.samples[i] += m_raw[g][i];
			}
		}
		if (m_bus)
		{
			m_bus->begin(frames);
			for (const SendSource& send : m_sendSources)
			{
				m_bus->addTap(send.raw.data(), frames, send.amounts);
			}
			m_bus->process(m_mix.samples.data(), frames);
		}
		for (float& s : m_mix.samples)
		{
			s *= m_master;
		}
		if (std::fabs(m_options.stereoWidth - 1.0f) > 1.0e-6f)
		{
			applyStereoWidth(m_mix, m_options.stereoWidth);
		}

		if (m_limiter)
		{
			m_limiter->push(m_mix.samples.data(), frames);
			pullLimiter();
		}
		else
		{
			m_ready.insert(m_ready.end(), m_mix.samples.begin(), m_mix.samples.end());
		}
	}

	/// @brief m_layerGain をバッファの先頭 frames フレームに掛ける (1 の所は触らない)
	void applyLayerGain(std::vector<float>& buffer, std::size_t frames) const
	{
		for (std::size_t i = 0; i < frames; ++i)
		{
			if (m_layerGain[i] != 1.0f)
			{
				buffer[i * 2] *= m_layerGain[i];
				buffer[i * 2 + 1] *= m_layerGain[i];
			}
		}
	}

	ModernSong::Options m_options;
	float m_master;
	ModernRng m_rng;
	std::vector<std::string> m_groupNames;
	std::vector<float> m_groupGain;
	std::vector<ModernSong::DriverSet> m_drivers;
	std::vector<ChipMixStream> m_groups;
	std::vector<DuckRuntime> m_ducks;
	std::optional<SendBus> m_bus;
	std::vector<SendSource> m_sendSources;
	std::vector<float> m_layerGain;
	bool m_needKeys = false;
	std::optional<StreamingLimiter> m_limiter;
	LayerControl m_layers;

	std::vector<std::vector<float>> m_raw;
	std::vector<std::vector<float>> m_keys;
	std::vector<float> m_gains;
	StereoBuffer m_mix;
	std::vector<float> m_ready;
	std::size_t m_readyHead = 0;
	std::vector<float> m_scratch;
	bool m_inputEnded = false;
	std::uint64_t m_position = 0;
};

} // namespace mitiru_mml
