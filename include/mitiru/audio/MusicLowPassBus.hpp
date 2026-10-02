#pragma once

/// @file MusicLowPassBus.hpp
/// @brief BGM だけを通す low-pass の分岐 (miniaudio の ma_lpf_node)
/// @details 一時停止中やメニューを開いている間に BGM を「こもらせる」ための 1 段。
///          効果音はここを通らないので、こもった BGM の上で操作音ははっきり鳴る。
///          無効にしている間は BGM を endpoint へ直結し、フィルタを経由させない
///          (最大カットオフでも 2 次のフィルタは高域をわずかに削るため)。

#include <mitiru/audio/MusicLowPassParams.hpp>
#include <mitiru/audio/detail/MiniaudioInclude.hpp>

namespace mitiru::audio
{

class MusicLowPassBus
{
public:
	/// 2 次の ma_lpf は Butterworth (Q = 1/√2) で、MusicLowPassParams の qLinear と同じ形
	static constexpr ma_uint32 kOrder = 2;

	/// @return ノードを作れたら true。失敗しても BGM は直結のまま鳴る
	bool init(ma_engine& engine)
	{
		m_engine = &engine;
		const ma_uint32 channels = ma_engine_get_channels(&engine);
		const ma_uint32 rate = ma_engine_get_sample_rate(&engine);
		const ma_lpf_node_config config =
			ma_lpf_node_config_init(channels, rate, static_cast<double>(rate) * 0.45, kOrder);
		if (ma_lpf_node_init(ma_engine_get_node_graph(&engine), &config, nullptr, &m_node) != MA_SUCCESS)
		{
			return false;
		}
		ma_node_attach_output_bus(&m_node, 0, ma_engine_get_endpoint(&engine), 0);
		m_ready = true;
		return true;
	}

	void uninit()
	{
		if (m_ready) { ma_lpf_node_uninit(&m_node, nullptr); }
		m_ready = false;
	}

	/// @param cutoffHz 0 以下で無効 (直結)。有効にすると、それ以降に route() した BGM がフィルタを通る
	void setCutoff(float cutoffHz)
	{
		if (!m_ready) { return; }
		const ma_uint32 rate = ma_engine_get_sample_rate(m_engine);
		m_params = musicLowPassParams(cutoffHz, static_cast<float>(rate));
		if (m_params.bypass) { return; }
		const ma_lpf_config config = ma_lpf_config_init(
			ma_format_f32, ma_engine_get_channels(m_engine), rate,
			static_cast<double>(m_params.frequencyHz), kOrder);
		ma_lpf_node_reinit(&config, &m_node);
	}

	/// @brief BGM の出力先を現在の設定 (フィルタ経由 / 直結) に合わせてつなぎ直す
	void route(ma_sound& music)
	{
		if (m_engine == nullptr) { return; }
		if (m_ready && !m_params.bypass)
		{
			ma_node_attach_output_bus(&music, 0, &m_node, 0);
		}
		else
		{
			ma_node_attach_output_bus(&music, 0, ma_engine_get_endpoint(m_engine), 0);
		}
	}

private:
	ma_engine* m_engine = nullptr;
	ma_lpf_node m_node{};
	bool m_ready = false;
	MusicLowPassParams m_params{};
};

} // namespace mitiru::audio
