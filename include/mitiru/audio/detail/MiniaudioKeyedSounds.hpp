#pragma once

/// @file MiniaudioKeyedSounds.hpp
/// @brief 名前 (key) で引ける ma_sound の表。ループ音と、番号付きで鳴らした音 (ABI v45 の SoundIntent::handle)。
/// @details 同じ key が鳴っていれば、先頭から鳴らし直さずに音量・ピッチ・パンだけを変える (鳴らしたまま音量を
///          変える用途では、呼ぶたびに先頭へ戻ると調整にならないため)。止めるときに減衰させた音は、ここでは
///          uninit せず reap() で回収する (鳴っている最中に uninit すると device thread が解放済みの領域を参照する)。

#include <mitiru/audio/detail/MiniaudioInclude.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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
	};

	/// @brief key で鳴らす。同じ key が鳴っていれば音量・ピッチ・パンだけを変える。
	void play(ma_engine& engine, const std::string& key, const std::string& path, const Params& p)
	{
		if (auto it = m_live.find(key); it != m_live.end() && ma_sound_is_playing(it->second.get()))
		{
			update(key, p.volume, p.pitch, p.pan);
			return;
		}
		auto snd = std::make_unique<ma_sound>();
		if (ma_sound_init_from_file(&engine, path.c_str(), MA_SOUND_FLAG_DECODE,
		                            nullptr, nullptr, snd.get()) != MA_SUCCESS)
		{
			mitiru::debug::warnOnceFix("audio.loop:" + path,
				"音声ファイル " + path + " が見つからない/読めない",
				"パスが assets 相対で間違っているか、対応フォーマット外",
				"パスを確認し、対応フォーマット (wav/ogg/mp3 等) に変換する");
			return;
		}
		ma_sound_set_looping(snd.get(), p.loop ? MA_TRUE : MA_FALSE);
		ma_sound_set_volume(snd.get(), p.volume);
		ma_sound_set_pitch(snd.get(), (p.pitch > 0.0f) ? p.pitch : 1.0f);
		ma_sound_set_pan(snd.get(), p.pan);
		if (p.fadeInSec > 0.0f)
		{
			ma_sound_set_fade_in_milliseconds(snd.get(), 0.0f, p.volume, static_cast<ma_uint64>(p.fadeInSec * 1000.0f));
		}
		ma_sound_start(snd.get());
		retireLive(key);  // 鳴り終わった前の同じ key (one-shot) を片付けてから差し替える
		m_live[key] = std::move(snd);
	}

	/// @brief 鳴っている key の音量・ピッチ・パンを変える。なければ何もしない。
	void update(const std::string& key, float volume, float pitch, float pan)
	{
		const auto it = m_live.find(key);
		if (it == m_live.end()) { return; }
		// 音量は短い ramp で変える。即値で変えると、ザッというずれた音が入る。
		ma_sound_set_fade_in_milliseconds(it->second.get(), -1.0f, volume, 40);
		ma_sound_set_pitch(it->second.get(), (pitch > 0.0f) ? pitch : 1.0f);
		ma_sound_set_pan(it->second.get(), pan);
	}

	/// @brief key の音を止める。fadeOutSec > 0 の場合は、減衰させてから止める。
	void stop(ma_engine& engine, const std::string& key, float fadeOutSec)
	{
		const auto it = m_live.find(key);
		if (it == m_live.end()) { return; }
		ma_sound* s = it->second.get();
		if (fadeOutSec > 0.0f)
		{
			ma_sound_set_fade_in_milliseconds(s, -1.0f, 0.0f, static_cast<ma_uint64>(fadeOutSec * 1000.0f));
			ma_sound_set_stop_time_in_pcm_frames(s, ma_engine_get_time_in_pcm_frames(&engine)
				+ static_cast<ma_uint64>(fadeOutSec * static_cast<float>(ma_engine_get_sample_rate(&engine))));
			ma_sound_set_looping(s, MA_FALSE);
			m_fading.push_back(std::move(it->second));
		}
		else
		{
			ma_sound_stop(s);
			ma_sound_uninit(s);
		}
		m_live.erase(it);
	}

	/// @brief 減衰中の音を保持する (ボイスの fade-out など、表の外の音も同じ方法で回収する)。
	void retire(std::unique_ptr<ma_sound> s) { if (s) { m_fading.push_back(std::move(s)); } }

	/// @brief 鳴り終わった減衰中の音と、鳴り終わった one-shot の実体を回収する。毎フレーム呼ぶ。
	void reap()
	{
		for (std::size_t i = m_fading.size(); i-- > 0;)
		{
			if (ma_sound_at_end(m_fading[i].get()) || !ma_sound_is_playing(m_fading[i].get()))
			{
				ma_sound_uninit(m_fading[i].get());
				m_fading.erase(m_fading.begin() + static_cast<std::ptrdiff_t>(i));
			}
		}
		for (auto it = m_live.begin(); it != m_live.end();)
		{
			if (ma_sound_at_end(it->second.get())) { ma_sound_uninit(it->second.get()); it = m_live.erase(it); }
			else { ++it; }
		}
	}

	[[nodiscard]] bool playing(const std::string& key) const
	{
		const auto it = m_live.find(key);
		return it != m_live.end() && ma_sound_is_playing(it->second.get());
	}

	void releaseAll()
	{
		for (auto& kv : m_live) { ma_sound_stop(kv.second.get()); ma_sound_uninit(kv.second.get()); }
		m_live.clear();
		for (auto& s : m_fading) { ma_sound_uninit(s.get()); }
		m_fading.clear();
	}

private:
	void retireLive(const std::string& key)
	{
		const auto it = m_live.find(key);
		if (it == m_live.end()) { return; }
		ma_sound_stop(it->second.get());
		ma_sound_uninit(it->second.get());
		m_live.erase(it);
	}

	std::unordered_map<std::string, std::unique_ptr<ma_sound>> m_live;
	std::vector<std::unique_ptr<ma_sound>> m_fading;
};

}  // namespace mitiru::audio::detail
