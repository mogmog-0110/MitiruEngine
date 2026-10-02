#pragma once

/// @file AudioStream.hpp
/// @brief オーディオストリームの抽象インターフェース
/// @details read(buffer, frames) で PCM を逐次取り出す共通の API。
///          ファイルからの復号は FileAudioStream.hpp が、合成音源は SineSynth.hpp が実装する。

#include <cstddef>

namespace mitiru::audio
{

/// @brief オーディオストリームのフォーマット情報
struct AudioFormat
{
	int sampleRate = 44100;   ///< サンプルレート (Hz)
	int channels = 2;         ///< チャンネル数
	int bitsPerSample = 16;   ///< 1サンプルあたりのビット数
};

/// @brief オーディオストリームの抽象インターフェース
/// @details read() はフレーム単位（1 フレーム = channels 個のサンプル）で読み出す。
class IAudioStream
{
public:
	virtual ~IAudioStream() = default;

	/// @return 成功した場合は true
	virtual bool open() = 0;

	virtual void close() = 0;

	/// @brief PCM フレームを読み出す（float 形式 [-1.0, 1.0]）
	/// @param buffer 出力バッファ（channels * frames 個の float を格納できること）
	/// @return 実際に読み出したフレーム数
	virtual std::size_t read(float* buffer, std::size_t frames) = 0;

	/// @param frameOffset 先頭からのフレームオフセット
	/// @return 成功した場合は true
	virtual bool seek(std::size_t frameOffset) = 0;

	[[nodiscard]] virtual AudioFormat format() const noexcept = 0;

	/// @return 総フレーム数（不明な場合は 0）
	[[nodiscard]] virtual std::size_t totalFrames() const noexcept = 0;

	[[nodiscard]] virtual bool isEof() const noexcept = 0;

	[[nodiscard]] virtual bool isOpen() const noexcept = 0;
};

} // namespace mitiru::audio
