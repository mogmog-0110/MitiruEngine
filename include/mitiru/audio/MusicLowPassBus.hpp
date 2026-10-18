#pragma once

/// @file MusicLowPassBus.hpp
/// @brief BGM だけを通す low-pass の 1 段。一時停止のこもりと、ゲームの hud.musicLowPass のスイープに使う
/// @details 効果音はここを通らないので、こもった BGM の上で操作音ははっきり鳴る。BGM はいつもこの段を通し、周波数だけを
///          動かす (つなぎ替えると音声の thread とぶつかって欠けるため)。

#include <mitiru/audio/MusicLowPassParams.hpp>
#include <mitiru/audio/detail/MiniaudioInclude.hpp>
#include <mitiru/audio/detail/MiniaudioSmoothLowPass.hpp>

namespace mitiru::audio
{

class MusicLowPassBus
{
public:
	/// @return ノードを作れたら true。失敗しても BGM は直結のまま鳴る
	bool init(ma_engine& engine)
	{
		m_engine = &engine;
		m_rate = static_cast<float>(ma_engine_get_sample_rate(&engine));
		if (!m_filter.init(ma_engine_get_node_graph(&engine), ma_engine_get_channels(&engine), ma_engine_get_sample_rate(&engine)))
		{
			return false;
		}
		ma_node_attach_output_bus(m_filter.node(), 0, ma_engine_get_endpoint(&engine), 0);
		return true;
	}

	void uninit() { m_filter.uninit(); }

	/// @param cutoffHz 0 以下で外す。音声の thread が 15 ms ほどかけて動かす
	void setCutoff(float cutoffHz)
	{
		const MusicLowPassParams p = musicLowPassParams(cutoffHz, m_rate);
		m_filter.setCutoff(p.bypass ? 0.0f : p.frequencyHz);
	}

	/// @brief BGM の出力先をフィルタにつなぐ (作れなかったときは endpoint)
	void route(ma_sound& music)
	{
		if (m_engine == nullptr) { return; }
		ma_node* target = (m_filter.node() != nullptr) ? m_filter.node() : ma_engine_get_endpoint(m_engine);
		ma_node_attach_output_bus(&music, 0, target, 0);
	}

private:
	ma_engine* m_engine = nullptr;
	float m_rate = 48000.0f;
	detail::MiniaudioSmoothLowPass m_filter;
};

} // namespace mitiru::audio
