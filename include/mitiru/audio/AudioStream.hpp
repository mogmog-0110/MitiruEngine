#pragma once

/// @file AudioStream.hpp
/// @brief オーディオストリーム抽象インターフェースと具象実装
/// @details read(buffer, frames) APIによるオーディオデータの逐次読み出しを抽象化する。
///          WAVファイルとRaw PCMの具象実装を提供する。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru::audio
{

/// @brief オーディオストリームのフォーマット情報
struct AudioFormat
{
	int sampleRate = 44100;   ///< サンプルレート (Hz)
	int channels = 2;         ///< チャンネル数
	int bitsPerSample = 16;   ///< 1サンプルあたりのビット数
};

/// @brief オーディオストリーム抽象インターフェース
/// @details ファイルやメモリ上のオーディオデータを逐次読み出すための共通API。
///          read()はフレーム単位（1フレーム = channels個のサンプル）で読み出す。
class IAudioStream
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IAudioStream() = default;

	/// @brief ストリームを開く
	/// @return 成功した場合 true
	virtual bool open() = 0;

	/// @brief ストリームを閉じる
	virtual void close() = 0;

	/// @brief PCMフレームを読み出す（float形式 [-1.0, 1.0]）
	/// @param buffer 出力バッファ（channels * frames 個の float を格納できること）
	/// @param frames 読み出すフレーム数
	/// @return 実際に読み出したフレーム数
	virtual std::size_t read(float* buffer, std::size_t frames) = 0;

	/// @brief ストリーム先頭からのオフセットにシークする
	/// @param frameOffset 先頭からのフレームオフセット
	/// @return 成功した場合 true
	virtual bool seek(std::size_t frameOffset) = 0;

	/// @brief フォーマット情報を取得する
	/// @return オーディオフォーマット
	[[nodiscard]] virtual AudioFormat format() const noexcept = 0;

	/// @brief 総フレーム数を取得する
	/// @return 総フレーム数（不明な場合は 0）
	[[nodiscard]] virtual std::size_t totalFrames() const noexcept = 0;

	/// @brief ストリーム終端に達したかを判定する
	/// @return 終端なら true
	[[nodiscard]] virtual bool isEof() const noexcept = 0;

	/// @brief ストリームが正常に開かれているかを判定する
	/// @return 開かれていれば true
	[[nodiscard]] virtual bool isOpen() const noexcept = 0;
};

/// @brief WAVファイルストリーム
/// @details PCM 16-bit WAVファイルをチャンク単位で読み出す。
///          ファイル全体をメモリに読み込まず、必要な分だけ逐次読み出す。
///
/// @code
/// mitiru::audio::WavAudioStream stream("music.wav");
/// if (stream.open())
/// {
///     auto fmt = stream.format();
///     std::vector<float> buf(fmt.channels * 1024);
///     while (!stream.isEof())
///     {
///         auto frames = stream.read(buf.data(), 1024);
///     }
///     stream.close();
/// }
/// @endcode
class WavAudioStream : public IAudioStream
{
public:
	/// @brief コンストラクタ
	/// @param filePath WAVファイルのパス
	explicit WavAudioStream(std::string_view filePath)
		: m_filePath(filePath)
	{
	}

	/// @brief デストラクタ
	~WavAudioStream() override
	{
		close();
	}

	/// @brief コピー禁止
	WavAudioStream(const WavAudioStream&) = delete;
	/// @brief コピー代入禁止
	WavAudioStream& operator=(const WavAudioStream&) = delete;
	/// @brief ムーブコンストラクタ
	WavAudioStream(WavAudioStream&&) noexcept = default;
	/// @brief ムーブ代入演算子
	WavAudioStream& operator=(WavAudioStream&&) noexcept = default;

	/// @brief WAVファイルを開きヘッダーを解析する
	/// @return 成功した場合 true
	bool open() override
	{
		close();

		m_file.open(m_filePath, std::ios::binary);
		if (!m_file.is_open())
		{
			return false;
		}

		if (!parseWavHeader())
		{
			close();
			return false;
		}

		m_isOpen = true;
		m_currentFrame = 0;
		return true;
	}

	/// @brief ストリームを閉じる
	void close() override
	{
		if (m_file.is_open())
		{
			m_file.close();
		}
		m_isOpen = false;
		m_currentFrame = 0;
	}

	/// @brief PCMフレームを読み出す（float形式 [-1.0, 1.0]）
	/// @param buffer 出力バッファ
	/// @param frames 読み出すフレーム数
	/// @return 実際に読み出したフレーム数
	std::size_t read(float* buffer, std::size_t frames) override
	{
		if (!m_isOpen || m_currentFrame >= m_totalFrames)
		{
			return 0;
		}

		const std::size_t remainingFrames = m_totalFrames - m_currentFrame;
		const std::size_t framesToRead = std::min(frames, remainingFrames);
		const std::size_t samplesPerFrame = static_cast<std::size_t>(m_format.channels);
		const std::size_t bytesPerSample = static_cast<std::size_t>(m_format.bitsPerSample) / 8;
		const std::size_t bytesToRead = framesToRead * samplesPerFrame * bytesPerSample;

		m_readBuffer.resize(bytesToRead);
		m_file.read(reinterpret_cast<char*>(m_readBuffer.data()),
			static_cast<std::streamsize>(bytesToRead));

		const auto bytesRead = static_cast<std::size_t>(m_file.gcount());
		const std::size_t framesRead = bytesRead / (samplesPerFrame * bytesPerSample);
		const std::size_t totalSamples = framesRead * samplesPerFrame;

		/// 16-bit PCM → float [-1.0, 1.0] 変換
		if (m_format.bitsPerSample == 16)
		{
			for (std::size_t i = 0; i < totalSamples; ++i)
			{
				const auto sample = static_cast<int16_t>(
					static_cast<uint16_t>(m_readBuffer[i * 2]) |
					(static_cast<uint16_t>(m_readBuffer[i * 2 + 1]) << 8));
				buffer[i] = static_cast<float>(sample) / 32768.0f;
			}
		}
		else if (m_format.bitsPerSample == 8)
		{
			/// 8-bit unsigned PCM → float [-1.0, 1.0] 変換
			for (std::size_t i = 0; i < totalSamples; ++i)
			{
				buffer[i] = (static_cast<float>(m_readBuffer[i]) - 128.0f) / 128.0f;
			}
		}

		m_currentFrame += framesRead;
		return framesRead;
	}

	/// @brief 指定フレーム位置にシークする
	/// @param frameOffset 先頭からのフレームオフセット
	/// @return 成功した場合 true
	bool seek(std::size_t frameOffset) override
	{
		if (!m_isOpen)
		{
			return false;
		}

		const std::size_t clampedOffset = std::min(frameOffset, m_totalFrames);
		const std::size_t samplesPerFrame = static_cast<std::size_t>(m_format.channels);
		const std::size_t bytesPerSample = static_cast<std::size_t>(m_format.bitsPerSample) / 8;
		const auto byteOffset = static_cast<std::streamoff>(
			m_dataOffset + clampedOffset * samplesPerFrame * bytesPerSample);

		m_file.clear();
		m_file.seekg(byteOffset, std::ios::beg);
		m_currentFrame = clampedOffset;
		return m_file.good();
	}

	/// @brief フォーマット情報を取得する
	/// @return オーディオフォーマット
	[[nodiscard]] AudioFormat format() const noexcept override
	{
		return m_format;
	}

	/// @brief 総フレーム数を取得する
	/// @return 総フレーム数
	[[nodiscard]] std::size_t totalFrames() const noexcept override
	{
		return m_totalFrames;
	}

	/// @brief ストリーム終端に達したかを判定する
	/// @return 終端なら true
	[[nodiscard]] bool isEof() const noexcept override
	{
		return !m_isOpen || m_currentFrame >= m_totalFrames;
	}

	/// @brief ストリームが正常に開かれているかを判定する
	/// @return 開かれていれば true
	[[nodiscard]] bool isOpen() const noexcept override
	{
		return m_isOpen;
	}

	/// @brief 現在のフレーム位置を取得する
	/// @return 現在のフレームオフセット
	[[nodiscard]] std::size_t currentFrame() const noexcept
	{
		return m_currentFrame;
	}

private:
	/// @brief WAVヘッダーのチャンクID
	struct ChunkHeader
	{
		char id[4];
		uint32_t size;
	};

	/// @brief WAVヘッダーを解析する
	/// @return 成功した場合 true
	bool parseWavHeader()
	{
		/// RIFF ヘッダー読み込み
		ChunkHeader riffHeader{};
		m_file.read(reinterpret_cast<char*>(&riffHeader), sizeof(riffHeader));
		if (!m_file.good() || std::memcmp(riffHeader.id, "RIFF", 4) != 0)
		{
			return false;
		}

		/// WAVEフォーマット確認
		char waveId[4]{};
		m_file.read(waveId, 4);
		if (!m_file.good() || std::memcmp(waveId, "WAVE", 4) != 0)
		{
			return false;
		}

		bool foundFmt = false;
		bool foundData = false;

		/// チャンクを順に読み込む
		while (m_file.good() && !(foundFmt && foundData))
		{
			ChunkHeader chunkHeader{};
			m_file.read(reinterpret_cast<char*>(&chunkHeader), sizeof(chunkHeader));
			if (!m_file.good())
			{
				break;
			}

			if (std::memcmp(chunkHeader.id, "fmt ", 4) == 0)
			{
				if (!parseFmtChunk(chunkHeader.size))
				{
					return false;
				}
				foundFmt = true;
			}
			else if (std::memcmp(chunkHeader.id, "data", 4) == 0)
			{
				m_dataOffset = static_cast<std::size_t>(m_file.tellg());
				m_dataSize = chunkHeader.size;

				const std::size_t samplesPerFrame = static_cast<std::size_t>(m_format.channels);
				const std::size_t bytesPerSample = static_cast<std::size_t>(m_format.bitsPerSample) / 8;
				const std::size_t bytesPerFrame = samplesPerFrame * bytesPerSample;
				m_totalFrames = (bytesPerFrame > 0) ? (m_dataSize / bytesPerFrame) : 0;

				foundData = true;
			}
			else
			{
				/// 未知のチャンクはスキップ
				m_file.seekg(chunkHeader.size, std::ios::cur);
			}
		}

		return foundFmt && foundData;
	}

	/// @brief fmt チャンクを解析する
	/// @param chunkSize チャンクサイズ
	/// @return 成功した場合 true
	bool parseFmtChunk(uint32_t chunkSize)
	{
		if (chunkSize < 16)
		{
			return false;
		}

		uint16_t audioFormat = 0;
		uint16_t numChannels = 0;
		uint32_t sampleRate = 0;
		uint32_t byteRate = 0;
		uint16_t blockAlign = 0;
		uint16_t bitsPerSample = 0;

		m_file.read(reinterpret_cast<char*>(&audioFormat), 2);
		m_file.read(reinterpret_cast<char*>(&numChannels), 2);
		m_file.read(reinterpret_cast<char*>(&sampleRate), 4);
		m_file.read(reinterpret_cast<char*>(&byteRate), 4);
		m_file.read(reinterpret_cast<char*>(&blockAlign), 2);
		m_file.read(reinterpret_cast<char*>(&bitsPerSample), 2);

		/// PCM フォーマット（1）のみサポート
		if (audioFormat != 1)
		{
			return false;
		}

		m_format.sampleRate = static_cast<int>(sampleRate);
		m_format.channels = static_cast<int>(numChannels);
		m_format.bitsPerSample = static_cast<int>(bitsPerSample);

		/// fmtチャンクの残りをスキップ
		if (chunkSize > 16)
		{
			m_file.seekg(chunkSize - 16, std::ios::cur);
		}

		static_cast<void>(byteRate);
		static_cast<void>(blockAlign);

		return m_file.good();
	}

	std::string m_filePath;                ///< WAVファイルパス
	std::ifstream m_file;                  ///< ファイルストリーム
	AudioFormat m_format;                  ///< フォーマット情報
	std::size_t m_dataOffset = 0;          ///< dataチャンクのファイル内オフセット
	uint32_t m_dataSize = 0;               ///< dataチャンクのバイトサイズ
	std::size_t m_totalFrames = 0;         ///< 総フレーム数
	std::size_t m_currentFrame = 0;        ///< 現在のフレーム位置
	bool m_isOpen = false;                 ///< ストリームが開かれているか
	std::vector<uint8_t> m_readBuffer;     ///< 一時読み込みバッファ
};

/// @brief Raw PCMストリーム
/// @details フォーマット情報を外部から指定し、ヘッダーなしのRaw PCMファイルを
///          逐次読み出す。テストデータやカスタムフォーマットのファイルに使用する。
///
/// @code
/// mitiru::audio::AudioFormat fmt;
/// fmt.sampleRate = 44100;
/// fmt.channels = 2;
/// fmt.bitsPerSample = 16;
/// mitiru::audio::RawPcmAudioStream stream("raw_audio.pcm", fmt);
/// if (stream.open())
/// {
///     std::vector<float> buf(fmt.channels * 1024);
///     stream.read(buf.data(), 1024);
///     stream.close();
/// }
/// @endcode
class RawPcmAudioStream : public IAudioStream
{
public:
	/// @brief コンストラクタ
	/// @param filePath Raw PCMファイルのパス
	/// @param fmt オーディオフォーマット情報
	RawPcmAudioStream(std::string_view filePath, AudioFormat fmt)
		: m_filePath(filePath)
		, m_format(fmt)
	{
	}

	/// @brief デストラクタ
	~RawPcmAudioStream() override
	{
		close();
	}

	/// @brief コピー禁止
	RawPcmAudioStream(const RawPcmAudioStream&) = delete;
	/// @brief コピー代入禁止
	RawPcmAudioStream& operator=(const RawPcmAudioStream&) = delete;
	/// @brief ムーブコンストラクタ
	RawPcmAudioStream(RawPcmAudioStream&&) noexcept = default;
	/// @brief ムーブ代入演算子
	RawPcmAudioStream& operator=(RawPcmAudioStream&&) noexcept = default;

	/// @brief Raw PCMファイルを開く
	/// @return 成功した場合 true
	bool open() override
	{
		close();

		m_file.open(m_filePath, std::ios::binary | std::ios::ate);
		if (!m_file.is_open())
		{
			return false;
		}

		const auto fileSize = static_cast<std::size_t>(m_file.tellg());
		m_file.seekg(0, std::ios::beg);

		const std::size_t samplesPerFrame = static_cast<std::size_t>(m_format.channels);
		const std::size_t bytesPerSample = static_cast<std::size_t>(m_format.bitsPerSample) / 8;
		const std::size_t bytesPerFrame = samplesPerFrame * bytesPerSample;
		m_totalFrames = (bytesPerFrame > 0) ? (fileSize / bytesPerFrame) : 0;

		m_isOpen = true;
		m_currentFrame = 0;
		return true;
	}

	/// @brief ストリームを閉じる
	void close() override
	{
		if (m_file.is_open())
		{
			m_file.close();
		}
		m_isOpen = false;
		m_currentFrame = 0;
	}

	/// @brief PCMフレームを読み出す（float形式 [-1.0, 1.0]）
	/// @param buffer 出力バッファ
	/// @param frames 読み出すフレーム数
	/// @return 実際に読み出したフレーム数
	std::size_t read(float* buffer, std::size_t frames) override
	{
		if (!m_isOpen || m_currentFrame >= m_totalFrames)
		{
			return 0;
		}

		const std::size_t remainingFrames = m_totalFrames - m_currentFrame;
		const std::size_t framesToRead = std::min(frames, remainingFrames);
		const std::size_t samplesPerFrame = static_cast<std::size_t>(m_format.channels);
		const std::size_t bytesPerSample = static_cast<std::size_t>(m_format.bitsPerSample) / 8;
		const std::size_t bytesToRead = framesToRead * samplesPerFrame * bytesPerSample;

		m_readBuffer.resize(bytesToRead);
		m_file.read(reinterpret_cast<char*>(m_readBuffer.data()),
			static_cast<std::streamsize>(bytesToRead));

		const auto bytesRead = static_cast<std::size_t>(m_file.gcount());
		const std::size_t framesRead = bytesRead / (samplesPerFrame * bytesPerSample);
		const std::size_t totalSamples = framesRead * samplesPerFrame;

		if (m_format.bitsPerSample == 16)
		{
			for (std::size_t i = 0; i < totalSamples; ++i)
			{
				const auto sample = static_cast<int16_t>(
					static_cast<uint16_t>(m_readBuffer[i * 2]) |
					(static_cast<uint16_t>(m_readBuffer[i * 2 + 1]) << 8));
				buffer[i] = static_cast<float>(sample) / 32768.0f;
			}
		}
		else if (m_format.bitsPerSample == 8)
		{
			for (std::size_t i = 0; i < totalSamples; ++i)
			{
				buffer[i] = (static_cast<float>(m_readBuffer[i]) - 128.0f) / 128.0f;
			}
		}

		m_currentFrame += framesRead;
		return framesRead;
	}

	/// @brief 指定フレーム位置にシークする
	/// @param frameOffset 先頭からのフレームオフセット
	/// @return 成功した場合 true
	bool seek(std::size_t frameOffset) override
	{
		if (!m_isOpen)
		{
			return false;
		}

		const std::size_t clampedOffset = std::min(frameOffset, m_totalFrames);
		const std::size_t samplesPerFrame = static_cast<std::size_t>(m_format.channels);
		const std::size_t bytesPerSample = static_cast<std::size_t>(m_format.bitsPerSample) / 8;
		const auto byteOffset = static_cast<std::streamoff>(
			clampedOffset * samplesPerFrame * bytesPerSample);

		m_file.clear();
		m_file.seekg(byteOffset, std::ios::beg);
		m_currentFrame = clampedOffset;
		return m_file.good();
	}

	/// @brief フォーマット情報を取得する
	[[nodiscard]] AudioFormat format() const noexcept override
	{
		return m_format;
	}

	/// @brief 総フレーム数を取得する
	[[nodiscard]] std::size_t totalFrames() const noexcept override
	{
		return m_totalFrames;
	}

	/// @brief ストリーム終端に達したかを判定する
	[[nodiscard]] bool isEof() const noexcept override
	{
		return !m_isOpen || m_currentFrame >= m_totalFrames;
	}

	/// @brief ストリームが正常に開かれているかを判定する
	[[nodiscard]] bool isOpen() const noexcept override
	{
		return m_isOpen;
	}

private:
	std::string m_filePath;               ///< ファイルパス
	std::ifstream m_file;                 ///< ファイルストリーム
	AudioFormat m_format;                 ///< フォーマット情報
	std::size_t m_totalFrames = 0;        ///< 総フレーム数
	std::size_t m_currentFrame = 0;       ///< 現在のフレーム位置
	bool m_isOpen = false;                ///< ストリームが開かれているか
	std::vector<uint8_t> m_readBuffer;    ///< 一時読み込みバッファ
};

} // namespace mitiru::audio
