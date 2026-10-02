#pragma once

/// @file MiniaudioSfxBus.hpp
/// @brief 効果音をまとめて通す miniaudio のバス。音量 (ダッキング) と残響 (Freeverb) への送りを持つ
/// @details 効果音 → ma_sound_group → 残響の node (出力 = 入力 + 送り × 残響) → endpoint。
/// 送りと残響の響き (部屋の大きさ、高域の減り) は、音声スレッドが次の塊を処理するときに取り込む。
/// 送りは塊の最初から最後まで線形に動かす。
/// BGM とボイスは、台詞が残響でぼやけないようにここを通さない

#include <mitiru/audio/detail/MiniaudioInclude.hpp>

#include <atomic>

#include <mitiru/audio/mix/Freeverb.hpp>

namespace mitiru::audio::detail
{

class MiniaudioSfxBus
{
public:
	MiniaudioSfxBus() = default;
	MiniaudioSfxBus(const MiniaudioSfxBus&) = delete;
	MiniaudioSfxBus& operator=(const MiniaudioSfxBus&) = delete;

	/// @return 作れたら true。失敗しても効果音は endpoint へ直結で鳴る (group() が nullptr)
	bool init(ma_engine& engine)
	{
		const ma_uint32 channels = ma_engine_get_channels(&engine);
		if (channels != 2) { return false; }  // 残響はステレオで組んでいる
		m_reverb.verb.init(ma_engine_get_sample_rate(&engine));
		if (!initReverbNode(ma_engine_get_node_graph(&engine), channels)) { return false; }
		const ma_uint32 groupFlags = MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH;
		if (ma_sound_group_init(&engine, groupFlags, nullptr, &m_group) != MA_SUCCESS)
		{
			ma_node_uninit(&m_reverb.base, nullptr);
			return false;
		}
		ma_node_attach_output_bus(&m_group, 0, &m_reverb.base, 0);
		ma_node_attach_output_bus(&m_reverb.base, 0, ma_engine_get_endpoint(&engine), 0);
		m_ready = true;
		return true;
	}

	/// @brief 効果音を先に uninit してから呼ぶ
	void uninit()
	{
		if (!m_ready) { return; }
		ma_sound_group_uninit(&m_group);
		ma_node_uninit(&m_reverb.base, nullptr);
		m_ready = false;
	}

	/// @brief 効果音を作るときに渡す group。作れていなければ nullptr (endpoint へ直結)
	[[nodiscard]] ma_sound_group* group() noexcept { return m_ready ? &m_group : nullptr; }

	/// @brief バス全体の音量 (ダッキング)。1 フレーム程度の短い傾きでつなぐ
	void setGain(float gain)
	{
		if (!m_ready || gain == m_gain) { return; }
		ma_sound_group_set_fade_in_milliseconds(&m_group, -1.0f, gain, 16);
		m_gain = gain;
	}

	void setSend(float send) noexcept { m_reverb.send.store(send, std::memory_order_relaxed); }

	void setReverb(float roomSize, float damping) noexcept
	{
		m_reverb.room.store(roomSize, std::memory_order_relaxed);
		m_reverb.damp.store(damping, std::memory_order_relaxed);
	}

	[[nodiscard]] float gain() const noexcept { return m_gain; }
	[[nodiscard]] float send() const noexcept { return m_reverb.send.load(std::memory_order_relaxed); }

private:
	struct ReverbNode
	{
		ma_node_base       base{};
		mix::Freeverb      verb;
		std::atomic<float> send{0.0f};
		std::atomic<float> room{0.5f};
		std::atomic<float> damp{0.5f};
		float              appliedSend = 0.0f;  ///< 音声スレッドだけが触る
		float              wet[2 * 1024]{};     ///< 残響の作業域 (音声スレッドだけが触る)
	};

	static void processReverb(ma_node* node, const float** in, ma_uint32* frameCountIn, float** out,
		ma_uint32* frameCountOut)
	{
		auto* self = static_cast<ReverbNode*>(node);
		const float room = self->room.load(std::memory_order_relaxed);
		const float damp = self->damp.load(std::memory_order_relaxed);
		if (room != self->verb.roomSize()) { self->verb.setRoomSize(room); }
		if (damp != self->verb.damping()) { self->verb.setDamping(damp); }
		const ma_uint32 frames = (*frameCountIn < *frameCountOut) ? *frameCountIn : *frameCountOut;
		const float from = self->appliedSend;
		const float to = self->send.load(std::memory_order_relaxed);
		for (ma_uint32 done = 0; done < frames;)
		{
			const ma_uint32 n = (frames - done < 1024u) ? frames - done : 1024u;
			self->verb.process(in[0] + done * 2, self->wet, n);
			for (ma_uint32 i = 0; i < n; ++i)
			{
				const float g = from + (to - from) * static_cast<float>(done + i) / static_cast<float>(frames);
				out[0][(done + i) * 2] = in[0][(done + i) * 2] + g * self->wet[i * 2];
				out[0][(done + i) * 2 + 1] = in[0][(done + i) * 2 + 1] + g * self->wet[i * 2 + 1];
			}
			done += n;
		}
		self->appliedSend = to;
		frameCountOut[0] = frames;
	}

	bool initReverbNode(ma_node_graph* graph, ma_uint32 channels)
	{
		// 効果音の入力が途切れても残響の尾を最後まで出すため、処理を止めない
		static ma_node_vtable vtable{&processReverb, nullptr, 1, 1, MA_NODE_FLAG_CONTINUOUS_PROCESSING};
		ma_node_config cfg = ma_node_config_init();
		cfg.vtable = &vtable;
		m_channels = channels;
		cfg.pInputChannels = &m_channels;
		cfg.pOutputChannels = &m_channels;
		return ma_node_init(graph, &cfg, nullptr, &m_reverb.base) == MA_SUCCESS;
	}

	ma_sound_group m_group{};
	ReverbNode     m_reverb;
	ma_uint32      m_channels = 2;
	float          m_gain = 1.0f;
	bool           m_ready = false;
};

}  // namespace mitiru::audio::detail
