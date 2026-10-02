#pragma once

/// @file MiniaudioSpatialNode.hpp
/// @brief 1 本の声を ISpatialRenderer（Steam Audio の HRTF など）で両耳へ配置する miniaudio の node
/// @details 入力をモノラルにまとめ、renderer の blockFrames 単位（0 なら 256）で畳み込む。miniaudio から渡される長さは毎回異なるため、1 ブロック分ためて処理し、
///          出力は 1 ブロック遅れる。setDirection は音声スレッドが次のブロックで読む。距離減衰は上流で適用済みのため、ここでは向きだけを使う。作業域は作成時に確保し、process では確保しない。

#include <mitiru/audio/detail/MiniaudioInclude.hpp>

#include <atomic>
#include <memory>
#include <vector>

#include <mitiru/audio/SpatialRenderer.hpp>

namespace mitiru::audio::detail
{

class MiniaudioSpatialNode
{
public:
	MiniaudioSpatialNode() = default;
	~MiniaudioSpatialNode() { uninit(); }
	MiniaudioSpatialNode(const MiniaudioSpatialNode&) = delete;
	MiniaudioSpatialNode& operator=(const MiniaudioSpatialNode&) = delete;

	/// @return 作成できたら true。入力と出力はステレオ
	bool init(ma_node_graph* graph, std::unique_ptr<ISpatialRenderer> renderer)
	{
		if (!renderer) { return false; }
		m_node.renderer = std::move(renderer);
		const std::size_t block = m_node.renderer->blockFrames();
		m_node.block = (block > 0) ? block : 256;
		m_node.mono.assign(m_node.block, 0.0f);
		m_node.ready.assign(m_node.block * 2, 0.0f);
		m_node.readPos = 0;
		m_node.fill = 0;
		static ma_node_vtable vtable{&process, nullptr, 1, 1, 0};
		ma_node_config cfg = ma_node_config_init();
		cfg.vtable = &vtable;
		cfg.pInputChannels = &m_channels;
		cfg.pOutputChannels = &m_channels;
		m_ready = ma_node_init(graph, &cfg, nullptr, &m_node.base) == MA_SUCCESS;
		return m_ready;
	}

	void uninit()
	{
		if (m_ready) { ma_node_uninit(&m_node.base, nullptr); }
		m_ready = false;
	}

	[[nodiscard]] ma_node* node() noexcept { return m_ready ? &m_node.base : nullptr; }

	/// @brief リスナー空間での向き（+x は右、+y は上、-z は前）
	void setDirection(const AudioVec3& d) noexcept
	{
		m_node.dx.store(d.x, std::memory_order_relaxed);
		m_node.dy.store(d.y, std::memory_order_relaxed);
		m_node.dz.store(d.z, std::memory_order_relaxed);
	}

	[[nodiscard]] std::size_t latencyFrames() const noexcept { return m_node.block; }

private:
	struct Node
	{
		ma_node_base                      base{};
		std::unique_ptr<ISpatialRenderer> renderer;
		std::size_t                       block = 256;
		std::vector<float>                mono;   ///< ためている入力 (モノラル)
		std::vector<float>                ready;  ///< 処理済みで、これから出す 1 ブロック (ステレオ)
		std::size_t                       fill = 0;
		std::size_t                       readPos = 0;
		std::atomic<float>                dx{0.0f};
		std::atomic<float>                dy{0.0f};
		std::atomic<float>                dz{-1.0f};
	};

	static void process(ma_node* node, const float** in, ma_uint32* frameCountIn, float** out, ma_uint32* frameCountOut)
	{
		auto* self = static_cast<Node*>(node);
		const ma_uint32 frames = (*frameCountIn < *frameCountOut) ? *frameCountIn : *frameCountOut;
		for (ma_uint32 i = 0; i < frames; ++i)
		{
			out[0][i * 2] = self->ready[self->readPos * 2];
			out[0][i * 2 + 1] = self->ready[self->readPos * 2 + 1];
			++self->readPos;
			self->mono[self->fill++] = 0.5f * (in[0][i * 2] + in[0][i * 2 + 1]);
			if (self->fill == self->block) { renderBlock(*self); }
		}
		frameCountOut[0] = frames;
	}

	static void renderBlock(Node& n)
	{
		const AudioVec3 dir{n.dx.load(std::memory_order_relaxed), n.dy.load(std::memory_order_relaxed),
		                    n.dz.load(std::memory_order_relaxed)};
		if (!n.renderer->process(n.mono.data(), n.ready.data(), n.block, dir, 1.0f))
		{
			for (std::size_t i = 0; i < n.block; ++i) { n.ready[i * 2] = n.ready[i * 2 + 1] = n.mono[i]; }
		}
		n.fill = 0;
		n.readPos = 0;
	}

	Node      m_node;
	ma_uint32 m_channels = 2;
	bool      m_ready = false;
};

}  // namespace mitiru::audio::detail
