#pragma once

/// @file MiniaudioKeyedSounds.hpp
/// @brief 名前 (key) で引ける ma_sound の表。ループ音と、番号付きで鳴らした音 (ABI v45 の SoundIntent::handle)。
/// @details 同じ key が鳴っていれば、先頭から鳴らし直さずに音量・ピッチ・パンだけを変える (鳴らしたまま音量を
///          変える用途では、呼ぶたびに先頭へ戻ると調整にならないため)。止めるときに減衰させた音は、ここでは
///          uninit せず reap() で回収する (鳴っている最中に uninit すると device thread が解放済みの領域を参照する)。
///          遮蔽の low-pass を頼まれた音だけ、音とバスの間に ma_lpf_node を 1 つ挟む。HRTF の renderer を
///          渡してあれば (setSpatialRenderer)、3D の音は pan の代わりに MiniaudioSpatialNode で両耳へ置く。

#include <mitiru/audio/detail/MiniaudioInclude.hpp>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <mitiru/audio/SpatialRenderer.hpp>
#include <mitiru/audio/detail/MiniaudioSpatialNode.hpp>
#include <mitiru/debug/WarnOnce.hpp>

namespace mitiru::audio::detail
{

class MiniaudioKeyedSounds
{
public:
	struct Params
	{
		float volume = 1.0f;
		float pitch = 1.0f;     ///< <= 0 は 1.0
		float fadeInSec = 0.0f;
		float pan = 0.0f;       ///< -1 (左) .. 1 (右)
		bool  loop = true;
		float startSec = 0.0f;  ///< 頭からこの秒だけ進めた位置から鳴らす
		float lowpassHz = 0.0f; ///< > 0 でこの周波数より上を削る
		bool  spatial = false;  ///< 3D の音 (setSpatialRenderer があれば direction で両耳へ置く)
		AudioVec3 direction{0.0f, 0.0f, -1.0f};
	};

	/// @brief 3D の音を両耳へ置く renderer の作り方。空なら 3D の音も pan で鳴らす
	using SpatialFactory = std::function<std::unique_ptr<ISpatialRenderer>()>;
	void setSpatialRenderer(SpatialFactory factory) { m_spatialFactory = std::move(factory); }

	/// @brief 鳴らす先。group が nullptr なら endpoint へ直結
	void bind(ma_engine& engine, ma_sound_group* group) noexcept
	{
		m_engine = &engine;
		m_group = group;
	}

	/// @brief key で鳴らす。同じ key が鳴っていれば音量・ピッチ・パン・low-pass だけを変える
	void play(const std::string& key, const std::string& path, const Params& p)
	{
		if (m_engine == nullptr) { return; }
		if (auto it = m_live.find(key); it != m_live.end() && ma_sound_is_playing(it->second.sound.get()))
		{
			update(key, p.volume, p.pitch, p.pan, p.lowpassHz, p.spatial ? &p.direction : nullptr);
			return;
		}
		Entry e;
		e.sound = std::make_unique<ma_sound>();
		if (ma_sound_init_from_file(m_engine, path.c_str(), MA_SOUND_FLAG_DECODE,
		                            m_group, nullptr, e.sound.get()) != MA_SUCCESS)
		{
			mitiru::debug::warnOnce("audio.loop:" + path,
				"音声ファイル " + path + " を読めません。assets からの相対パスと、形式が wav / ogg / mp3 の"
				"どれかかを確かめてください。");
			return;
		}
		ma_sound* s = e.sound.get();
		ma_sound_set_looping(s, p.loop ? MA_TRUE : MA_FALSE);
		ma_sound_set_pitch(s, (p.pitch > 0.0f) ? p.pitch : 1.0f);
		ma_sound_set_pan(s, p.pan);
		if (p.startSec > 0.0f) { ma_sound_seek_to_second(s, p.startSec); }
		// 音量は fader だけに持たせる。update が fader で音量を変えるので、ma_sound_set_volume にも入れると二重に掛かる
		ma_sound_set_fade_in_milliseconds(s, (p.fadeInSec > 0.0f) ? 0.0f : p.volume, p.volume,
		                                  static_cast<ma_uint64>(p.fadeInSec * 1000.0f));
		if (p.spatial) { attachSpatial(e, p.direction); }
		if (p.lowpassHz > 0.0f) { setLowpass(e, p.lowpassHz); }
		ma_sound_start(s);
		retireLive(key);  // 鳴り終わった同じ key の one-shot を片付けてから差し替える
		m_live[key] = std::move(e);
	}

	/// @brief 鳴っている key の音量・ピッチ・パンを変える。lowpassHz は負なら変えない、0 なら外す。
	///        direction は 3D の音の向き (nullptr なら変えない)。なければ何もしない
	void update(const std::string& key, float volume, float pitch, float pan, float lowpassHz = -1.0f,
	            const AudioVec3* direction = nullptr)
	{
		const auto it = m_live.find(key);
		if (it == m_live.end()) { return; }
		Entry& e = it->second;
		ma_sound* s = e.sound.get();
		// 音量は短い ramp で変える。即値で変えると、ザッというずれた音が入る。
		ma_sound_set_fade_in_milliseconds(s, -1.0f, volume, 40);
		ma_sound_set_pitch(s, (pitch > 0.0f) ? pitch : 1.0f);
		ma_sound_set_pan(s, e.spatial ? 0.0f : pan);
		if (e.spatial && direction != nullptr) { e.spatial->setDirection(*direction); }
		if (lowpassHz >= 0.0f) { setLowpass(e, lowpassHz); }
	}

	/// @brief key の音を止める。fadeOutSec > 0 の場合は、減衰させてから止める。
	void stop(const std::string& key, float fadeOutSec)
	{
		const auto it = m_live.find(key);
		if (it == m_live.end() || m_engine == nullptr) { return; }
		ma_sound* s = it->second.sound.get();
		if (fadeOutSec > 0.0f)
		{
			ma_sound_set_fade_in_milliseconds(s, -1.0f, 0.0f, static_cast<ma_uint64>(fadeOutSec * 1000.0f));
			ma_sound_set_stop_time_in_pcm_frames(s, ma_engine_get_time_in_pcm_frames(m_engine)
				+ static_cast<ma_uint64>(fadeOutSec * static_cast<float>(ma_engine_get_sample_rate(m_engine))));
			ma_sound_set_looping(s, MA_FALSE);
			m_fading.push_back(std::move(it->second));
		}
		else
		{
			ma_sound_stop(s);
			destroy(it->second);
		}
		m_live.erase(it);
	}

	/// @brief 減衰中の音を保持する (ボイスの fade-out など、表の外の音も同じ方法で回収する)。
	void retire(std::unique_ptr<ma_sound> s)
	{
		if (!s) { return; }
		Entry e;
		e.sound = std::move(s);
		m_fading.push_back(std::move(e));
	}

	/// @brief 鳴り終わった減衰中の音と、鳴り終わった one-shot の実体を回収する。毎フレーム呼ぶ。
	void reap()
	{
		for (std::size_t i = m_fading.size(); i-- > 0;)
		{
			ma_sound* s = m_fading[i].sound.get();
			if (ma_sound_at_end(s) || !ma_sound_is_playing(s))
			{
				destroy(m_fading[i]);
				m_fading.erase(m_fading.begin() + static_cast<std::ptrdiff_t>(i));
			}
		}
		for (auto it = m_live.begin(); it != m_live.end();)
		{
			if (ma_sound_at_end(it->second.sound.get())) { destroy(it->second); it = m_live.erase(it); }
			else { ++it; }
		}
	}

	[[nodiscard]] bool playing(const std::string& key) const
	{
		const auto it = m_live.find(key);
		return it != m_live.end() && ma_sound_is_playing(it->second.sound.get());
	}

	/// @brief key の音に挟んだ low-pass の周波数 (挟んでいなければ 0)。テスト用
	[[nodiscard]] float lowpassHz(const std::string& key) const
	{
		const auto it = m_live.find(key);
		return (it == m_live.end() || !it->second.lpf) ? 0.0f : it->second.cutoffHz;
	}

	void releaseAll()
	{
		for (auto& kv : m_live) { ma_sound_stop(kv.second.sound.get()); destroy(kv.second); }
		m_live.clear();
		for (auto& e : m_fading) { destroy(e); }
		m_fading.clear();
	}

private:
	struct Entry
	{
		std::unique_ptr<ma_sound>             sound;
		std::unique_ptr<MiniaudioSpatialNode> spatial;
		std::unique_ptr<ma_lpf_node>          lpf;
		float                                 cutoffHz = 0.0f;
	};

	/// 音 → (両耳へ置く node) → (low-pass) → バスの順につなぎ直す
	void rewire(Entry& e)
	{
		ma_node* target = (m_group != nullptr) ? static_cast<ma_node*>(m_group) : ma_engine_get_endpoint(m_engine);
		if (e.lpf) { ma_node_attach_output_bus(e.lpf.get(), 0, target, 0); target = e.lpf.get(); }
		if (e.spatial) { ma_node_attach_output_bus(e.spatial->node(), 0, target, 0); target = e.spatial->node(); }
		ma_node_attach_output_bus(e.sound.get(), 0, target, 0);
	}

	void attachSpatial(Entry& e, const AudioVec3& direction)
	{
		if (!m_spatialFactory) { return; }
		e.spatial = std::make_unique<MiniaudioSpatialNode>();
		if (!e.spatial->init(ma_engine_get_node_graph(m_engine), m_spatialFactory()))
		{
			e.spatial.reset();
			return;
		}
		e.spatial->setDirection(direction);
		ma_sound_set_pan(e.sound.get(), 0.0f);
		rewire(e);
	}

	/// 0 なら削らない (挟んだ node はナイキストの手前まで開く)。初めて頼まれた時だけ node を作って挟む
	void setLowpass(Entry& e, float hz)
	{
		const ma_uint32 rate = ma_engine_get_sample_rate(m_engine);
		const ma_uint32 channels = ma_engine_get_channels(m_engine);
		const double open = static_cast<double>(rate) * 0.45;
		const double cutoff = (hz > 0.0f) ? std::min(static_cast<double>(hz), open) : open;
		if (!e.lpf)
		{
			if (hz <= 0.0f) { return; }
			e.lpf = std::make_unique<ma_lpf_node>();
			const ma_lpf_node_config cfg = ma_lpf_node_config_init(channels, rate, cutoff, 2);
			if (ma_lpf_node_init(ma_engine_get_node_graph(m_engine), &cfg, nullptr, e.lpf.get()) != MA_SUCCESS)
			{
				e.lpf.reset();
				return;
			}
			rewire(e);
		}
		else
		{
			const ma_lpf_config cfg = ma_lpf_config_init(ma_format_f32, channels, rate, cutoff, 2);
			ma_lpf_node_reinit(&cfg, e.lpf.get());
		}
		e.cutoffHz = (hz > 0.0f) ? static_cast<float>(cutoff) : 0.0f;
	}

	/// 音を先に外してから node を外す (音の出力先が先に消えないように)
	static void destroy(Entry& e)
	{
		if (e.sound) { ma_sound_uninit(e.sound.get()); }
		e.spatial.reset();
		if (e.lpf) { ma_lpf_node_uninit(e.lpf.get(), nullptr); }
		e.sound.reset();
		e.lpf.reset();
	}

	void retireLive(const std::string& key)
	{
		const auto it = m_live.find(key);
		if (it == m_live.end()) { return; }
		ma_sound_stop(it->second.sound.get());
		destroy(it->second);
		m_live.erase(it);
	}

	SpatialFactory                         m_spatialFactory;
	ma_engine*                             m_engine = nullptr;
	ma_sound_group*                        m_group = nullptr;
	std::unordered_map<std::string, Entry> m_live;
	std::vector<Entry>                     m_fading;
};

}  // namespace mitiru::audio::detail
