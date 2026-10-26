#pragma once

/// @file MiniaudioBusDsp.hpp
/// @brief mix.json の eq / compressor / limiter を、バスの木の効果の鎖と master の出力に差す
/// @details バスの EQ とコンプレッサは miniaudio の 1 入力 1 出力のステレオ node として鎖に差す。鎖の順は EQ、コンプレッサ。
///          Master の EQ とコンプレッサ、limiter は node にせず、engine が読んだ出力の並びを直接処理する。ClipWatch が limiter の前の音を数えられるよう、
///          limiter は数えた後に掛ける (processMaster → ClipWatch → limitMaster)。
///          設定が空なら node も処理も作らない。何も書かない mix.json では、木も出力も導入前と同じバイト列になる。
///          ゲームの thread が書くのは atomic だけで、音声の thread が 64 フレームごとに読んで係数を作る。

#include <mitiru/audio/detail/MiniaudioInclude.hpp>

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <mitiru/audio/detail/MiniaudioBusTree.hpp>
#include <mitiru/audio/detail/MiniaudioKeyedSounds.hpp>
#include <mitiru/audio/mix/BusDspConfig.hpp>
#include <mitiru/debug/WarnOnce.hpp>

namespace mitiru::audio::detail
{

/// ゲームの thread が書き、音声の thread が読む EQ の帯域。8 本の途中で読まれても、どの組み合わせも有効な設定になる
class AtomicEqBands
{
public:
	void store(const mix::EqBands& bands) noexcept
	{
		for (std::size_t i = 0; i < mix::kEqMaxBands; ++i)
		{
			m_type[i].store(static_cast<std::uint8_t>(bands[i].type), std::memory_order_relaxed);
			m_hz[i].store(bands[i].hz, std::memory_order_relaxed);
			m_gain[i].store(bands[i].gainDb, std::memory_order_relaxed);
			m_q[i].store(bands[i].q, std::memory_order_relaxed);
		}
	}

	[[nodiscard]] mix::EqBands load() const noexcept
	{
		mix::EqBands out;
		for (std::size_t i = 0; i < mix::kEqMaxBands; ++i)
		{
			out[i] = {static_cast<mix::EqBandType>(m_type[i].load(std::memory_order_relaxed)), m_hz[i].load(std::memory_order_relaxed),
			          m_gain[i].load(std::memory_order_relaxed), m_q[i].load(std::memory_order_relaxed)};
		}
		return out;
	}

private:
	std::array<std::atomic<std::uint8_t>, mix::kEqMaxBands> m_type{};
	std::array<std::atomic<float>, mix::kEqMaxBands>        m_hz{};
	std::array<std::atomic<float>, mix::kEqMaxBands>        m_gain{};
	std::array<std::atomic<float>, mix::kEqMaxBands>        m_q{};
};

/// 音声の thread で、atomic の目標が変わっていたら EqProcessor に渡してから処理する
class EqFollower
{
public:
	void init(float sampleRate, std::uint32_t channels, float fadeSec, const mix::EqBands& first)
	{
		m_eq.init(sampleRate, channels, fadeSec);
		m_eq.setTarget(first, true);
		m_applied = first;
		m_target.store(first);
	}

	void setBands(const mix::EqBands& bands) noexcept { m_target.store(bands); }

	void process(const float* in, float* out, std::uint32_t frames) noexcept
	{
		const mix::EqBands now = m_target.load();
		if (!(now == m_applied)) { m_eq.setTarget(now); m_applied = now; }
		m_eq.process(in, out, frames);
	}

private:
	mix::EqProcessor m_eq;
	AtomicEqBands    m_target;
	mix::EqBands     m_applied{};
};

namespace dspnode
{
inline bool initStereoNode(ma_node_graph* graph, ma_node_vtable* vtable, ma_node_base* base)
{
	static ma_uint32 channels = 2;
	ma_node_config cfg = ma_node_config_init();
	cfg.vtable = vtable;
	cfg.pInputChannels = &channels;
	cfg.pOutputChannels = &channels;
	return ma_node_init(graph, &cfg, nullptr, base) == MA_SUCCESS;
}
}  // namespace dspnode

class MiniaudioEqNode
{
public:
	MiniaudioEqNode() = default;
	MiniaudioEqNode(const MiniaudioEqNode&) = delete;
	MiniaudioEqNode& operator=(const MiniaudioEqNode&) = delete;
	~MiniaudioEqNode() { uninit(); }

	bool init(ma_node_graph* graph, std::uint32_t sampleRate, const mix::EqBands& first, float fadeSec)
	{
		m_node.follower.init(static_cast<float>(sampleRate), 2, fadeSec, first);
		static ma_node_vtable vtable{&process, nullptr, 1, 1, MA_NODE_FLAG_CONTINUOUS_PROCESSING};
		m_ready = dspnode::initStereoNode(graph, &vtable, &m_node.base);
		return m_ready;
	}

	void uninit()
	{
		if (m_ready) { ma_node_uninit(&m_node.base, nullptr); }
		m_ready = false;
	}

	[[nodiscard]] ma_node* node() noexcept { return m_ready ? &m_node.base : nullptr; }
	void setBands(const mix::EqBands& bands) noexcept { m_node.follower.setBands(bands); }

private:
	struct Node
	{
		ma_node_base base{};
		EqFollower   follower;
	};

	static void process(ma_node* node, const float** in, ma_uint32* frameCountIn, float** out, ma_uint32* frameCountOut)
	{
		auto* self = static_cast<Node*>(node);
		const ma_uint32 frames = std::min(*frameCountIn, *frameCountOut);
		self->follower.process(in[0], out[0], frames);
		frameCountOut[0] = frames;
	}

	Node m_node;
	bool m_ready = false;
};

class MiniaudioCompressorNode
{
public:
	MiniaudioCompressorNode() = default;
	MiniaudioCompressorNode(const MiniaudioCompressorNode&) = delete;
	MiniaudioCompressorNode& operator=(const MiniaudioCompressorNode&) = delete;
	~MiniaudioCompressorNode() { uninit(); }

	/// 設定は鎖に差す前に決める。mix.json は静的なので、差した後には変えない
	bool init(ma_node_graph* graph, std::uint32_t sampleRate, const mix::CompressorSettings& settings)
	{
		m_node.comp.init(static_cast<float>(sampleRate), 2);
		m_node.comp.setSettings(settings);
		static ma_node_vtable vtable{&process, nullptr, 1, 1, MA_NODE_FLAG_CONTINUOUS_PROCESSING};
		m_ready = dspnode::initStereoNode(graph, &vtable, &m_node.base);
		return m_ready;
	}

	void uninit()
	{
		if (m_ready) { ma_node_uninit(&m_node.base, nullptr); }
		m_ready = false;
	}

	[[nodiscard]] ma_node* node() noexcept { return m_ready ? &m_node.base : nullptr; }

private:
	struct Node
	{
		ma_node_base    base{};
		mix::Compressor comp;
	};

	static void process(ma_node* node, const float** in, ma_uint32* frameCountIn, float** out, ma_uint32* frameCountOut)
	{
		auto* self = static_cast<Node*>(node);
		const ma_uint32 frames = std::min(*frameCountIn, *frameCountOut);
		self->comp.process(in[0], out[0], frames);
		frameCountOut[0] = frames;
	}

	Node m_node;
	bool m_ready = false;
};

class MiniaudioBusDsp
{
public:
	MiniaudioBusDsp() = default;
	MiniaudioBusDsp(const MiniaudioBusDsp&) = delete;
	MiniaudioBusDsp& operator=(const MiniaudioBusDsp&) = delete;

	/// @brief 設定のとおりに鎖へ node を差し、master の処理を始める。engine ごとに 1 回だけ呼ぶ。音を鳴らす前に呼ぶこと
	/// @details Ui / Ambient / User6 / User7 に掛けるときは、そのバスの group を効果音の group の子として作り、
	///          keyed の音の行き先に登録する (SoundPlayback::bus で選ばれた音だけがそこを通る)
	void configure(ma_engine& engine, MiniaudioBusTree& buses, MiniaudioKeyedSounds& keyed, const mix::BusDspConfig& config)
	{
		if (m_configured || !config.any()) { return; }
		if (!buses.ready())
		{
			mitiru::debug::warnOnce("audio.dsp.tree", "バスの木を作れていないので、mix.json の eq / compressor / limiter は効きません");
			return;
		}
		m_configured = true;
		m_config = config;
		const std::uint32_t rate = ma_engine_get_sample_rate(&engine);
		ma_node_graph* graph = ma_engine_get_node_graph(&engine);
		for (std::size_t b = 1; b < mix::kMixBusCount; ++b)
		{
			const auto bus = static_cast<mix::MixBus>(b);
			if (!config.hasEq(bus) && !config.compressor[b]) { continue; }
			if (buses.addSubBus(bus)) { keyed.setBusGroup(static_cast<std::uint8_t>(b), buses.soundGroup(bus)); }
			if (config.hasEq(bus)) { addEq(graph, rate, buses, bus); }
			if (config.compressor[b]) { addCompressor(graph, rate, buses, bus, *config.compressor[b]); }
		}
		startMaster(static_cast<float>(rate));
		warnIfBandAboveNyquistShare(config, rate);
	}

	/// @brief 実際に差せたものの名前 ("eq:music" など)。差せなかったものは含まない。--audio-capture の記録に残す
	[[nodiscard]] const std::vector<std::string>& activeNames() const noexcept { return m_names; }

	/// @brief 聞き手のいる場所に合わせて EQ を切り替える。zone は BusDspConfig::eqZones の添字、-1 は既定
	void setEqZone(int zone) noexcept
	{
		if (zone == m_zone) { return; }
		m_zone = zone;
		for (std::size_t b = 1; b < mix::kMixBusCount; ++b)
		{
			if (m_eq[b]) { m_eq[b]->setBands(m_config.eqFor(static_cast<mix::MixBus>(b), zone)); }
		}
		if (m_config.hasEq(mix::MixBus::Master)) { m_masterEq.setBands(m_config.eqFor(mix::MixBus::Master, zone)); }
	}

	/// @brief 鎖から外して壊す。木を壊す前に呼ぶ
	void uninit(MiniaudioBusTree& buses)
	{
		for (std::size_t b = 1; b < mix::kMixBusCount; ++b)
		{
			const auto bus = static_cast<mix::MixBus>(b);
			if (m_comp[b]) { buses.removeEffect(bus, m_comp[b]->node()); m_comp[b].reset(); }
			if (m_eq[b]) { buses.removeEffect(bus, m_eq[b]->node()); m_eq[b].reset(); }
		}
		m_flags.store(0, std::memory_order_release);
		m_names.clear();
		m_configured = false;
	}

	/// @brief 音声の thread で、Master の EQ とコンプレッサを掛ける。ClipWatch が数える前に呼ぶ
	void processMaster(float* buf, std::uint32_t frames) noexcept
	{
		const unsigned flags = m_flags.load(std::memory_order_acquire);
		if ((flags & kMasterEq) != 0) { m_masterEq.process(buf, buf, frames); }
		if ((flags & kMasterComp) != 0) { m_masterComp.process(buf, buf, frames); }
	}

	/// @brief 音声の thread で、limiter を掛ける。ClipWatch が数えた後に呼ぶ
	void limitMaster(float* buf, std::uint32_t frames) noexcept
	{
		if ((m_flags.load(std::memory_order_acquire) & kLimiter) != 0) { m_limiter.process(buf, frames); }
	}

	[[nodiscard]] const mix::BusDspConfig& config() const noexcept { return m_config; }

private:
	static constexpr unsigned kMasterEq = 1;
	static constexpr unsigned kMasterComp = 2;
	static constexpr unsigned kLimiter = 4;
	static constexpr const char* kBusNames[mix::kMixBusCount] = {"master", "music", "sfx", "voice", "ui", "ambient", "user6", "user7"};

	/// mix.json の hz の検証は 48 kHz を前提にしている。それより低い engine では、上限 (出力の周波数の 45%) へ丸められるので知らせる
	static void warnIfBandAboveNyquistShare(const mix::BusDspConfig& config, std::uint32_t rate)
	{
		const float limit = static_cast<float>(rate) * 0.45f;
		auto above = [limit](const mix::EqBusSet& set) {
			for (const mix::EqBands& bands : set.bands) { for (const mix::EqBand& b : bands) { if (b.type != mix::EqBandType::Off && b.hz > limit) { return true; } } }
			return false;
		};
		bool any = above(config.eq);
		for (const mix::EqZoneSet& z : config.eqZones) { any = any || above(z.set); }
		if (any)
		{
			mitiru::debug::warnOnce("audio.dsp.hz", "mix.json の eq の hz が出力の周波数 (" + std::to_string(rate) + " Hz) の 45% を超えているので、その上限に丸めます");
		}
	}

	void addEq(ma_node_graph* graph, std::uint32_t rate, MiniaudioBusTree& buses, mix::MixBus bus)
	{
		const auto b = static_cast<std::size_t>(bus);
		if (bus == mix::MixBus::Master) { return; }
		auto node = std::make_unique<MiniaudioEqNode>();
		if (!node->init(graph, rate, m_config.eqFor(bus, -1), m_config.eqFadeSec) || !buses.insertEffect(bus, node->node())) { return; }
		m_eq[b] = std::move(node);
		m_names.push_back(std::string("eq:") + kBusNames[b]);
	}

	void addCompressor(ma_node_graph* graph, std::uint32_t rate, MiniaudioBusTree& buses, mix::MixBus bus,
	                   const mix::CompressorSettings& settings)
	{
		if (bus == mix::MixBus::Master) { return; }
		auto node = std::make_unique<MiniaudioCompressorNode>();
		if (!node->init(graph, rate, settings) || !buses.insertEffect(bus, node->node())) { return; }
		m_comp[static_cast<std::size_t>(bus)] = std::move(node);
		m_names.push_back(std::string("compressor:") + kBusNames[static_cast<std::size_t>(bus)]);
	}

	void startMaster(float rate)
	{
		unsigned flags = 0;
		if (m_config.hasEq(mix::MixBus::Master))
		{
			m_masterEq.init(rate, 2, m_config.eqFadeSec, m_config.eqFor(mix::MixBus::Master, -1));
			flags |= kMasterEq;
			m_names.push_back("eq:master");
		}
		if (const auto& c = m_config.compressor[0]; c)
		{
			m_masterComp.init(rate, 2);
			m_masterComp.setSettings(*c);
			flags |= kMasterComp;
			m_names.push_back("compressor:master");
		}
		if (m_config.limiter)
		{
			m_limiter.init(rate, 2);
			m_limiter.setSettings(*m_config.limiter);
			flags |= kLimiter;
			m_names.push_back("limiter");
		}
		m_flags.store(flags, std::memory_order_release);
	}

	mix::BusDspConfig m_config;
	bool              m_configured = false;
	int               m_zone = -1;
	std::array<std::unique_ptr<MiniaudioEqNode>, mix::kMixBusCount>         m_eq;
	std::array<std::unique_ptr<MiniaudioCompressorNode>, mix::kMixBusCount> m_comp;
	EqFollower        m_masterEq;
	mix::Compressor   m_masterComp;
	mix::Limiter      m_limiter;
	std::atomic<unsigned> m_flags{0};
	std::vector<std::string> m_names;
};

}  // namespace mitiru::audio::detail
