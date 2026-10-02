#pragma once

/// @file FileAudioStream.hpp
/// @brief 音声ファイルを復号しながら読み取る IAudioStream
/// @details 復号は miniaudio の ma_decoder に任せる。形式は中身から判定され、
///          WAV (dr_wav) / MP3 (dr_mp3) / FLAC (dr_flac) / Ogg Vorbis (stb_vorbis) を読み込める。
///          出力は常に float [-1, 1]、サンプルレートとチャンネル数はファイルのまま。

#include <mitiru/audio/detail/MiniaudioInclude.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include <mitiru/audio/AudioStream.hpp>

namespace mitiru::audio
{

class FileAudioStream : public IAudioStream
{
public:
	explicit FileAudioStream(std::string_view filePath)
		: m_filePath(filePath)
	{
	}

	~FileAudioStream() override { close(); }

	FileAudioStream(const FileAudioStream&) = delete;
	FileAudioStream& operator=(const FileAudioStream&) = delete;
	// ma_decoder は自身を指すポインタを内部に保持するため、ヒープに置き、ポインタだけを動かす。
	FileAudioStream(FileAudioStream&&) noexcept = default;
	FileAudioStream& operator=(FileAudioStream&&) noexcept = default;

	bool open() override
	{
		close();
		auto decoder = std::make_unique<ma_decoder>();
		const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
		if (ma_decoder_init_file(m_filePath.c_str(), &config, decoder.get()) != MA_SUCCESS)
		{
			return false;
		}
		ma_uint64 length = 0;
		if (ma_decoder_get_length_in_pcm_frames(decoder.get(), &length) != MA_SUCCESS)
		{
			length = 0;
		}
		m_format = AudioFormat{static_cast<int>(decoder->outputSampleRate),
		                       static_cast<int>(decoder->outputChannels), 32};
		m_totalFrames = static_cast<std::size_t>(length);
		m_decoder = std::move(decoder);
		m_eof = false;
		return true;
	}

	void close() override
	{
		if (m_decoder)
		{
			ma_decoder_uninit(m_decoder.get());
			m_decoder.reset();
		}
		m_eof = false;
	}

	std::size_t read(float* buffer, std::size_t frames) override
	{
		if (!m_decoder || m_eof || frames == 0)
		{
			return 0;
		}
		ma_uint64 framesRead = 0;
		const ma_result result =
			ma_decoder_read_pcm_frames(m_decoder.get(), buffer, frames, &framesRead);
		// 要求より少なく返った時点で終端とする。MA_AT_END を待つと、0 フレームの読み出しが 1 回余計に必要になる。
		if (result != MA_SUCCESS || framesRead < frames)
		{
			m_eof = true;
		}
		return static_cast<std::size_t>(framesRead);
	}

	bool seek(std::size_t frameOffset) override
	{
		if (!m_decoder)
		{
			return false;
		}
		if (ma_decoder_seek_to_pcm_frame(m_decoder.get(), frameOffset) != MA_SUCCESS)
		{
			return false;
		}
		m_eof = (m_totalFrames > 0 && frameOffset >= m_totalFrames);
		return true;
	}

	/// @details bitsPerSample は復号後の float の幅 (32)。元ファイルのビット深度ではない。
	[[nodiscard]] AudioFormat format() const noexcept override { return m_format; }

	[[nodiscard]] std::size_t totalFrames() const noexcept override { return m_totalFrames; }

	[[nodiscard]] bool isEof() const noexcept override { return !m_decoder || m_eof; }

	[[nodiscard]] bool isOpen() const noexcept override { return m_decoder != nullptr; }

private:
	std::string m_filePath;
	std::unique_ptr<ma_decoder> m_decoder;
	AudioFormat m_format{};
	std::size_t m_totalFrames = 0;
	bool m_eof = false;
};

} // namespace mitiru::audio
