#pragma once

/// @file OfflineMixCapture.hpp
/// @brief デバイスを開かない engine のミックスを固定ステップに合わせて読み、WAV へ書く (--audio-capture)
/// @details 1 ステップで読む長さは n 番目の境界 floor(n * rate / stepsPerSecond) の差で決める。端数を
///          ステップ間で持ち越すので、48 kHz / 60 Hz でも 44.1 kHz / 60 Hz でも WAV の時刻と
///          シミュレーションの時刻 (ステップ番号 / stepsPerSecond) が長く回してもずれない。
///          サンプルは float32 のまま書く。1.0 を超えた分を丸めると、出力で割れる音が WAV から消えるため。

#include <mitiru/audio/detail/MiniaudioInclude.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mitiru::audio
{

class OfflineMixCapture
{
public:
	/// @brief ミックスを読む関数。out へ frames フレーム (インターリーブ) を書き、書けた数を返す
	using Source = std::function<ma_uint64(float* out, ma_uint64 frames)>;

	OfflineMixCapture(Source source, std::uint32_t sampleRate, std::uint32_t channels,
	                  std::uint32_t stepsPerSecond)
		: m_source(std::move(source))
		, m_sampleRate(sampleRate)
		, m_channels(channels)
		, m_stepsPerSecond(stepsPerSecond > 0 ? stepsPerSecond : 60)
	{
		const std::uint64_t maxFrames = (sampleRate + m_stepsPerSecond - 1) / m_stepsPerSecond;
		m_buffer.resize(static_cast<std::size_t>(maxFrames * channels));
	}

	~OfflineMixCapture() { close(); }

	OfflineMixCapture(const OfflineMixCapture&) = delete;
	OfflineMixCapture& operator=(const OfflineMixCapture&) = delete;

	/// @return WAV を開けたら true
	[[nodiscard]] bool open(const std::string& path)
	{
		close();
		const ma_encoder_config config =
			ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, m_channels, m_sampleRate);
		m_open = ma_encoder_init_file(path.c_str(), &config, &m_encoder) == MA_SUCCESS;
		return m_open;
	}

	/// @brief 1 ステップ分のミックスを読んで書く。source が短く返した分は無音で埋める (WAV の時刻を保つため)
	void step()
	{
		const ma_uint64 frames = boundary(m_steps + 1) - boundary(m_steps);
		++m_steps;
		if (!m_open || frames == 0) { return; }
		const ma_uint64 got = m_source ? m_source(m_buffer.data(), frames) : 0;
		for (ma_uint64 i = got * m_channels; i < frames * m_channels; ++i)
		{
			m_buffer[static_cast<std::size_t>(i)] = 0.0f;
		}
		ma_uint64 written = 0;
		ma_encoder_write_pcm_frames(&m_encoder, m_buffer.data(), frames, &written);
		m_framesWritten += written;
	}

	/// @brief WAV のヘッダを確定して閉じる。二度呼んでもよい
	void close()
	{
		if (!m_open) { return; }
		ma_encoder_uninit(&m_encoder);
		m_open = false;
	}

	[[nodiscard]] bool isOpen() const noexcept { return m_open; }
	[[nodiscard]] std::uint64_t steps() const noexcept { return m_steps; }
	[[nodiscard]] std::uint64_t framesWritten() const noexcept { return m_framesWritten; }
	[[nodiscard]] std::uint32_t sampleRate() const noexcept { return m_sampleRate; }

private:
	[[nodiscard]] ma_uint64 boundary(std::uint64_t step) const noexcept
	{
		return step * m_sampleRate / m_stepsPerSecond;
	}

	Source             m_source;
	std::uint32_t      m_sampleRate;
	std::uint32_t      m_channels;
	std::uint32_t      m_stepsPerSecond;
	std::vector<float> m_buffer;
	ma_encoder         m_encoder{};
	bool               m_open = false;
	std::uint64_t      m_steps = 0;
	std::uint64_t      m_framesWritten = 0;
};

}  // namespace mitiru::audio
