#pragma once

/// @file MiniaudioBusTree.hpp
/// @brief 音 → バスの group → 効果の鎖 → 合流の node → master の順につなぐバスの木。
/// @details BGM、効果音、台詞はそれぞれ group を持つ。台詞は残響への送りが 0 なので、残響でぼやけない。
/// Ui と Ambient は IAudioEngine に経路がなく、SoundIntentRouter が音量を掛けて効果音の group に流す。
/// group と合流の node の間には、効果の鎖として 1 入力 1 出力のステレオ node を差せる。
/// 合流の node は 3 本のバスを足し、各バスから送られた音に残響を掛け、戻りの大きさを掛けて足す。
/// 場所ごとの響きは戻りで切り替えるため、送りを 0 にしなくても残響の尾は切り替え中に小さくなる。
/// master の効果は合流の node と endpoint の間に差す。
/// Ui・Ambient・User6・User7 は、効果を掛けると決めたときだけ addSubBus で効果音の group の子の group を作る。作らなければ、音は今までどおり効果音の group に入る
/// (効果音の音量・ダッキング・残響への送りも、子の group はそのまま受ける)。子の group の鎖は、効果音の group の入口へつなぐ。
/// 出力が 2 本の node は使わない。miniaudio は出力ごとの読み出しが 1 周期ずつそろう前提のため、1 回の読み出しが node のキャッシュ (処理の大きさ。デバイスを開かない engine は既定の 480 フレーム、機器を開く engine は 192 フレーム) を超えると、もう片方の出力が失われ、本流の音も遅れる。

#include <mitiru/audio/detail/MiniaudioInclude.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <vector>

#include <mitiru/audio/mix/BusDucker.hpp>
#include <mitiru/audio/mix/Freeverb.hpp>
#include <mitiru/debug/WarnOnce.hpp>

namespace mitiru::audio::detail
{

class MiniaudioBusTree
{
public:
	using Bus = mix::MixBus;

	static constexpr std::size_t kEnd = static_cast<std::size_t>(-1);

	MiniaudioBusTree() = default;
	MiniaudioBusTree(const MiniaudioBusTree&) = delete;
	MiniaudioBusTree& operator=(const MiniaudioBusTree&) = delete;
	~MiniaudioBusTree() { uninit(); }

	/// @return 作れたら true。失敗時も音は endpoint へ直結で鳴り、group() は nullptr を返す
	bool init(ma_engine& engine)
	{
		if (ma_engine_get_channels(&engine) != 2)
		{
			mitiru::debug::warnOnce("audio.sfxbus.channels",
				"効果音のバスはステレオの engine でだけ作れるので、残響とダッキングは効きません");
			return false;
		}
		m_engine = &engine;
		m_mix.verb.init(ma_engine_get_sample_rate(&engine));
		if (!initMix(ma_engine_get_node_graph(&engine))) { uninit(); return false; }
		ma_node_attach_output_bus(&m_mix.base, 0, ma_engine_get_endpoint(&engine), 0);
		for (Bus bus : kBuses)
		{
			if (!initBus(bus)) { uninit(); return false; }
		}
		m_mix.send[index(Bus::Sfx)].store(1.0f, std::memory_order_relaxed);
		m_ready = true;
		return true;
	}

	/// @brief 音を先に uninit してから呼ぶ
	void uninit()
	{
		if (m_engine == nullptr) { return; }
		for (std::size_t i = m_slots.size(); i-- > 0;)   // 子の group を先に
		{
			BusSlot& s = m_slots[i];
			if (s.groupReady) { ma_sound_group_uninit(&s.group); s.groupReady = false; }
			s.chain.clear();
		}
		if (m_mixReady) { ma_node_uninit(&m_mix.base, nullptr); m_mixReady = false; }
		m_ready = false;
		m_engine = nullptr;
	}

	/// @brief 音を作るときに渡す group。Music、Sfx、Voice 以外か、木を作れていなければ nullptr を返し、endpoint へ直結する
	[[nodiscard]] ma_sound_group* group(Bus bus) noexcept
	{
		return (m_ready && isTreeBus(bus)) ? &slot(bus).group : nullptr;
	}

	/// @brief 効果音の音を作るときに渡す group。bus の子の group を作ってあればそれ、なければ効果音の group
	[[nodiscard]] ma_sound_group* soundGroup(Bus bus) noexcept
	{
		if (m_ready && isSubBus(bus) && slotConst(bus).groupReady) { return &slot(bus).group; }
		return group(Bus::Sfx);
	}

	/// @brief Ui・Ambient・User6・User7 の子の group を作る。音を鳴らす前に呼ぶ。作れた (すでにある) なら true
	bool addSubBus(Bus bus)
	{
		if (!m_ready || !isSubBus(bus)) { return false; }
		BusSlot& s = slot(bus);
		if (s.groupReady) { return true; }
		const ma_uint32 groupFlags = MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH;
		if (ma_sound_group_init(m_engine, groupFlags, &slot(Bus::Sfx).group, &s.group) != MA_SUCCESS) { return false; }
		s.groupReady = true;
		rewire(bus);
		return true;
	}

	/// @brief group と合流の node の間に、1 入力 1 出力のステレオ node を差す。position は挿入位置で、kEnd なら末尾
	/// @return 差せたら true
	bool insertEffect(Bus bus, ma_node* effect, std::size_t position = kEnd)
	{
		if (!hasChain(bus) || effect == nullptr) { return false; }
		BusSlot& s = slot(bus);
		if (std::find(s.chain.begin(), s.chain.end(), effect) != s.chain.end()) { return false; }
		s.chain.insert(s.chain.begin() + static_cast<std::ptrdiff_t>(std::min(position, s.chain.size())), effect);
		rewire(bus);
		return true;
	}

	bool removeEffect(Bus bus, ma_node* effect)
	{
		if (!hasChain(bus)) { return false; }
		BusSlot& s = slot(bus);
		const auto it = std::find(s.chain.begin(), s.chain.end(), effect);
		if (it == s.chain.end()) { return false; }
		s.chain.erase(it);
		ma_node_detach_output_bus(effect, 0);
		rewire(bus);
		return true;
	}

	/// @brief バス全体の音量。ダッキング時は 1 フレーム程度の短い傾きでつなぐ
	void setGain(Bus bus, float gain)
	{
		if (!hasChain(bus)) { return; }
		BusSlot& s = slot(bus);
		if (gain == s.gain) { return; }
		ma_sound_group_set_fade_in_milliseconds(&s.group, -1.0f, gain, 16);
		s.gain = gain;
	}

	/// @brief バスから残響への送り。0 なら送らない。既定は効果音だけ 1
	void setReverbSend(Bus bus, float send) noexcept
	{
		if (isTreeBus(bus)) { m_mix.send[index(bus)].store(send, std::memory_order_relaxed); }
	}

	/// @brief 残響の戻りの大きさ。場所ごとの残響は mix::ReverbZoneTracker が決める
	void setReverbReturn(float send) noexcept { m_mix.ret.store(send, std::memory_order_relaxed); }

	void setReverb(float roomSize, float damping) noexcept
	{
		m_mix.room.store(roomSize, std::memory_order_relaxed);
		m_mix.damp.store(damping, std::memory_order_relaxed);
	}

	[[nodiscard]] float gain(Bus bus) const noexcept { return isTreeBus(bus) ? slotConst(bus).gain : 1.0f; }
	[[nodiscard]] float reverbSend(Bus bus) const noexcept
	{
		return isTreeBus(bus) ? m_mix.send[index(bus)].load(std::memory_order_relaxed) : 0.0f;
	}
	[[nodiscard]] float reverbReturn() const noexcept { return m_mix.ret.load(std::memory_order_relaxed); }
	[[nodiscard]] bool ready() const noexcept { return m_ready; }

private:
	static constexpr std::array<Bus, 3> kBuses{Bus::Music, Bus::Sfx, Bus::Voice};
	static constexpr ma_uint32 kStereo = 2;
	static constexpr ma_uint32 kChunk = 1024;

	[[nodiscard]] static constexpr bool isTreeBus(Bus bus) noexcept
	{
		return bus == Bus::Music || bus == Bus::Sfx || bus == Bus::Voice;
	}

	[[nodiscard]] static constexpr bool isSubBus(Bus bus) noexcept
	{
		return bus == Bus::Ui || bus == Bus::Ambient || bus == Bus::User6 || bus == Bus::User7;
	}

	/// 鎖を差せる (BGM・効果音・台詞か、作ってある子の group)
	[[nodiscard]] bool hasChain(Bus bus) const noexcept
	{
		return m_ready && (isTreeBus(bus) || (isSubBus(bus) && slotConst(bus).groupReady));
	}

	struct BusSlot
	{
		ma_sound_group        group{};
		std::vector<ma_node*> chain;
		float                 gain = 1.0f;
		bool                  groupReady = false;
	};

	/// 3 本のバスを足し、送られた音から残響を作って足す node。音声スレッドの外からは atomic だけを書く
	struct MixNode
	{
		ma_node_base                      base{};
		mix::Freeverb                     verb;
		std::array<std::atomic<float>, 3> send{};
		std::atomic<float>                ret{0.0f};
		std::atomic<float>                room{0.5f};
		std::atomic<float>                damp{0.5f};
		std::array<float, 3>              appliedSend{};   ///< ここから下は音声スレッドだけが触る
		float                             appliedRet = 0.0f;
		float                             sent[2 * kChunk]{};
		float                             wet[2 * kChunk]{};
	};

	/// 添字 0〜2 は合流の node の入力と同じ (Music、Sfx、Voice)。3 以降は子の group
	[[nodiscard]] static constexpr std::size_t index(Bus bus) noexcept { return static_cast<std::size_t>(bus) - 1; }
	[[nodiscard]] BusSlot& slot(Bus bus) noexcept { return m_slots[index(bus)]; }
	[[nodiscard]] const BusSlot& slotConst(Bus bus) const noexcept { return m_slots[index(bus)]; }

	static void processMix(ma_node* node, const float** in, ma_uint32* frameCountIn, float** out,
		ma_uint32* frameCountOut)
	{
		auto* self = static_cast<MixNode*>(node);
		const float room = self->room.load(std::memory_order_relaxed);
		const float damp = self->damp.load(std::memory_order_relaxed);
		if (room != self->verb.roomSize()) { self->verb.setRoomSize(room); }
		if (damp != self->verb.damping()) { self->verb.setDamping(damp); }
		const ma_uint32 frames = std::min(*frameCountIn, *frameCountOut);
		const std::array<float, 3> sendFrom = self->appliedSend;
		std::array<float, 3> sendTo{};
		for (std::size_t b = 0; b < 3; ++b) { sendTo[b] = self->send[b].load(std::memory_order_relaxed); }
		const float retFrom = self->appliedRet;
		const float retTo = self->ret.load(std::memory_order_relaxed);
		for (ma_uint32 done = 0; done < frames;)
		{
			const ma_uint32 n = std::min(frames - done, kChunk);
			std::fill_n(self->sent, n * 2, 0.0f);
			for (std::size_t b = 0; b < 3; ++b)
			{
				if (sendFrom[b] == 0.0f && sendTo[b] == 0.0f) { continue; }
				const float* src = in[b] + done * 2;
				for (ma_uint32 i = 0; i < n; ++i)
				{
					const float g = sendFrom[b] + (sendTo[b] - sendFrom[b]) * static_cast<float>(done + i) / static_cast<float>(frames);
					self->sent[i * 2] += src[i * 2] * g;
					self->sent[i * 2 + 1] += src[i * 2 + 1] * g;
				}
			}
			self->verb.process(self->sent, self->wet, n);
			for (ma_uint32 i = 0; i < n; ++i)
			{
				const std::size_t at = static_cast<std::size_t>(done + i) * 2;
				const float g = retFrom + (retTo - retFrom) * static_cast<float>(done + i) / static_cast<float>(frames);
				out[0][at] = in[0][at] + in[1][at] + in[2][at] + g * self->wet[i * 2];
				out[0][at + 1] = in[0][at + 1] + in[1][at + 1] + in[2][at + 1] + g * self->wet[i * 2 + 1];
			}
			done += n;
		}
		self->appliedSend = sendTo;
		self->appliedRet = retTo;
		frameCountOut[0] = frames;
	}

	bool initMix(ma_node_graph* graph)
	{
		// 入力が途切れても残響の尾を最後まで出すため、処理を止めない
		static ma_node_vtable vtable{&processMix, nullptr, 3, 1, MA_NODE_FLAG_CONTINUOUS_PROCESSING};
		ma_uint32 inChannels[3] = {kStereo, kStereo, kStereo};
		ma_uint32 outChannel = kStereo;
		ma_node_config cfg = ma_node_config_init();
		cfg.vtable = &vtable;
		cfg.pInputChannels = inChannels;
		cfg.pOutputChannels = &outChannel;
		m_mixReady = ma_node_init(graph, &cfg, nullptr, &m_mix.base) == MA_SUCCESS;
		return m_mixReady;
	}

	bool initBus(Bus bus)
	{
		BusSlot& s = slot(bus);
		const ma_uint32 groupFlags = MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH;
		if (ma_sound_group_init(m_engine, groupFlags, nullptr, &s.group) != MA_SUCCESS) { return false; }
		s.groupReady = true;
		rewire(bus);
		return true;
	}

	/// つなぎ直している間、その枝は音声スレッドから見て 1 ブロック分途切れることがある
	void rewire(Bus bus)
	{
		BusSlot& s = slot(bus);
		ma_node* prev = &s.group;
		for (ma_node* fx : s.chain)
		{
			ma_node_attach_output_bus(prev, 0, fx, 0);
			prev = fx;
		}
		if (isSubBus(bus)) { ma_node_attach_output_bus(prev, 0, &slot(Bus::Sfx).group, 0); }
		else { ma_node_attach_output_bus(prev, 0, &m_mix.base, static_cast<ma_uint32>(index(bus))); }
	}

	ma_engine*             m_engine = nullptr;
	std::array<BusSlot, 7> m_slots;
	MixNode                m_mix;
	bool                   m_mixReady = false;
	bool                   m_ready = false;
};

}  // namespace mitiru::audio::detail
