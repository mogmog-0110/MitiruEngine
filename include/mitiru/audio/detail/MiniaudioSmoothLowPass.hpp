#pragma once

/// @file MiniaudioSmoothLowPass.hpp
/// @brief 周波数を音声の thread の中で少しずつ動かす 2 次の low-pass の miniaudio ノード (曲のこもり、効果音の遮蔽とバスの low-pass)
/// @details ゲームの thread が周波数を atomic に書き、音声の thread が 64 フレームごとに log2 の上で約 15 ms かけて動かして
///          係数を作る。ma_lpf_node_reinit は音声の thread と同時に係数を書き換えてスイープの間に雑音を出すので使わない。
///          開いている間もフィルタを回して出力だけを素通しにし、閉じ始めの段差を防ぐ。

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

#include <mitiru/audio/detail/MiniaudioInclude.hpp>

namespace mitiru::audio::detail
{

class MiniaudioSmoothLowPass
{
public:
	MiniaudioSmoothLowPass() = default;
	MiniaudioSmoothLowPass(const MiniaudioSmoothLowPass&) = delete;
	MiniaudioSmoothLowPass& operator=(const MiniaudioSmoothLowPass&) = delete;
	~MiniaudioSmoothLowPass() { uninit(); }

	/// @return 作れたら true。出力先は呼ぶ側が node() でつなぐ
	bool init(ma_node_graph* graph, ma_uint32 channels, ma_uint32 sampleRate)
	{
		m_node.channels = channels;
		m_node.rate = static_cast<float>(sampleRate);
		m_node.openLog2 = std::log2(m_node.rate * kOpenShare);
		m_node.currentLog2 = m_node.openLog2;
		m_node.targetLog2.store(m_node.openLog2, std::memory_order_relaxed);
		m_node.state.assign(static_cast<std::size_t>(channels) * 2u, 0.0f);
		m_node.smoothing = 1.0f - std::exp(-static_cast<float>(kBlockFrames) / (kSmoothSec * m_node.rate));
		static ma_node_vtable vtable{&process, nullptr, 1, 1, MA_NODE_FLAG_CONTINUOUS_PROCESSING};
		ma_node_config cfg = ma_node_config_init();
		cfg.vtable = &vtable;
		cfg.pInputChannels = &m_node.channels;
		cfg.pOutputChannels = &m_node.channels;
		m_ready = ma_node_init(graph, &cfg, nullptr, &m_node.base) == MA_SUCCESS;
		return m_ready;
	}

	void uninit()
	{
		if (m_ready) { ma_node_uninit(&m_node.base, nullptr); }
		m_ready = false;
	}

	[[nodiscard]] ma_node* node() noexcept { return m_ready ? &m_node.base : nullptr; }

	/// @param hz 0 以下または開いている所より上なら素通し
	/// @param immediate true なら動かさず、その周波数から始める。まだ鳴らしていない音に挟むときに使う
	void setCutoff(float hz, bool immediate = false) noexcept
	{
		const float target = (hz > 0.0f) ? std::min(std::log2(std::max(hz, kMinHz)), m_node.openLog2) : m_node.openLog2;
		m_node.targetLog2.store(target, std::memory_order_relaxed);
		if (immediate) { m_node.snap.store(true, std::memory_order_release); }
	}

private:
	static constexpr ma_uint32 kBlockFrames = 64;
	static constexpr float kSmoothSec = 0.015f;
	static constexpr float kMinHz = 10.0f;
	/// これより上は開いているとみなす。2 次フィルタはここでもわずかに削るため、開いている間は素通しにする
	static constexpr float kOpenShare = 0.45f;
	static constexpr float kButterworthQ = 0.70710678f;

	struct Node
	{
		ma_node_base       base{};
		ma_uint32          channels = 2;
		float              rate = 48000.0f;
		float              openLog2 = 14.0f;
		float              currentLog2 = 14.0f;   ///< 音声の thread だけが触る
		float              smoothing = 0.1f;      ///< 1 ブロックで目標までの残りを詰める割合
		float              wet = 0.0f;            ///< 前のブロックの終わりのフィルタの出力の割合 (0 = 素通し)
		std::atomic<float> targetLog2{14.0f};
		std::atomic<bool>  snap{false};           ///< 次のブロックで目標へ一気に移る
		std::vector<float> state;                 ///< チャンネルごとに z1, z2 (転置した直接形 II)
	};

	struct Coeffs
	{
		float b0, b1, b2, a1, a2;
	};

	[[nodiscard]] static Coeffs lowPass(float hz, float rate) noexcept
	{
		const float w0 = 6.28318530718f * hz / rate;
		const float alpha = std::sin(w0) / (2.0f * kButterworthQ);
		const float c = std::cos(w0);
		const float a0 = 1.0f + alpha;
		return {(1.0f - c) * 0.5f / a0, (1.0f - c) / a0, (1.0f - c) * 0.5f / a0, -2.0f * c / a0, (1.0f - alpha) / a0};
	}

	/// wet を前のブロックの値からこのブロックの値まで標本ごとに直線で動かし、素通しとの切り替えによる段差を防ぐ
	static void processBlock(Node& self, const float* in, float* out, ma_uint32 frames, const Coeffs& k, float wet)
	{
		const ma_uint32 ch = self.channels;
		const float from = self.wet;
		for (ma_uint32 i = 0; i < frames; ++i)
		{
			const float w = from + (wet - from) * static_cast<float>(i + 1) / static_cast<float>(frames);
			for (ma_uint32 c = 0; c < ch; ++c)
			{
				const std::size_t at = static_cast<std::size_t>(i) * ch + c;
				float& z1 = self.state[c * 2];
				float& z2 = self.state[c * 2 + 1];
				const float x = in[at];
				const float y = k.b0 * x + z1;
				z1 = k.b1 * x - k.a1 * y + z2;
				z2 = k.b2 * x - k.a2 * y;
				out[at] = x + (y - x) * w;
			}
		}
		self.wet = wet;
	}

	static void process(ma_node* node, const float** in, ma_uint32* frameCountIn, float** out, ma_uint32* frameCountOut)
	{
		auto* self = static_cast<Node*>(node);
		const ma_uint32 frames = (*frameCountIn < *frameCountOut) ? *frameCountIn : *frameCountOut;
		const float target = self->targetLog2.load(std::memory_order_relaxed);
		if (self->snap.exchange(false, std::memory_order_acquire)) { self->currentLog2 = target; }
		for (ma_uint32 done = 0; done < frames; done += kBlockFrames)
		{
			const ma_uint32 n = (frames - done < kBlockFrames) ? frames - done : kBlockFrames;
			self->currentLog2 += (target - self->currentLog2) * self->smoothing;
			if (std::fabs(target - self->currentLog2) < 1e-3f) { self->currentLog2 = target; }
			// 開いた所から半オクターブ閉じるまでに、素通しからフィルタの出力へ移る
			const float wet = std::clamp((self->openLog2 - self->currentLog2) * 2.0f, 0.0f, 1.0f);
			const std::size_t at = static_cast<std::size_t>(done) * self->channels;
			processBlock(*self, in[0] + at, out[0] + at, n, lowPass(std::exp2(self->currentLog2), self->rate), wet);
		}
		frameCountOut[0] = frames;
	}

	Node m_node;
	bool m_ready = false;
};

}  // namespace mitiru::audio::detail
