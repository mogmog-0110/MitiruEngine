#pragma once

/// @file OggDecoder.hpp
/// @brief OGG/Vorbisデコーダーインターフェースとストリーミングデコーダー実装
/// @details オーディオコーデックの抽象インターフェースを定義し、
///          PCMストリーミングデコーダー（ADPCM風）とOGG/Vorbisデコーダースタブを提供する。
///          Vorbisライブラリが利用可能な場合は MITIRU_HAS_VORBIS を定義して
///          本物のVorbisデコーダーを有効化できる。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/audio/AudioStream.hpp>

#ifdef MITIRU_HAS_VORBIS
#include <vorbis/vorbisfile.h>
#endif

namespace mitiru::audio
{

/// @brief オーディオデコーダー抽象インターフェース
/// @details 圧縮オーディオフォーマット（OGG、ADPCM等）をデコードし、
///          IAudioStreamと同じフレームベースのread APIでPCMデータを提供する。
class IAudioDecoder
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IAudioDecoder() = default;

	/// @brief デコーダーを開く
	/// @param filePath 入力ファイルのパス
	/// @return 成功した場合 true
	virtual bool open(std::string_view filePath) = 0;

	/// @brief デコーダーを閉じる
	virtual void close() = 0;

	/// @brief PCMフレームをデコードして読み出す（float形式 [-1.0, 1.0]）
	/// @param buffer 出力バッファ
	/// @param frames 読み出すフレーム数
	/// @return 実際にデコードしたフレーム数
	virtual std::size_t read(float* buffer, std::size_t frames) = 0;

	/// @brief 指定フレーム位置にシークする
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

	/// @brief デコーダーが正常に開かれているかを判定する
	/// @return 開かれていれば true
	[[nodiscard]] virtual bool isOpen() const noexcept = 0;

	/// @brief コーデック名を取得する
	/// @return コーデック識別文字列
	[[nodiscard]] virtual const char* codecName() const noexcept = 0;
};

/// @brief ADPCMストリーミングデコーダー
/// @details 簡易的なIMA ADPCM風のデコーダー。4bitエンコードされたオーディオデータを
///          PCM 16bitに展開する。ファイルフォーマットは独自ヘッダー付きで、
///          先頭16バイトにフォーマット情報を格納する。
///
///          ヘッダーレイアウト（16バイト）:
///          - [0..3]  マジック "ADPC"
///          - [4..5]  チャンネル数（uint16_t LE）
///          - [6..9]  サンプルレート（uint32_t LE）
///          - [10..13] 総フレーム数（uint32_t LE）
///          - [14..15] 予約
///
/// @code
/// mitiru::audio::AdpcmDecoder decoder;
/// if (decoder.open("music.adpcm"))
/// {
///     std::vector<float> buf(decoder.format().channels * 1024);
///     decoder.read(buf.data(), 1024);
///     decoder.close();
/// }
/// @endcode
class AdpcmDecoder : public IAudioDecoder
{
public:
	/// @brief デストラクタ
	~AdpcmDecoder() override
	{
		close();
	}

	/// @brief コピー禁止
	AdpcmDecoder(const AdpcmDecoder&) = delete;
	/// @brief コピー代入禁止
	AdpcmDecoder& operator=(const AdpcmDecoder&) = delete;
	/// @brief デフォルトコンストラクタ
	AdpcmDecoder() = default;
	/// @brief ムーブコンストラクタ
	AdpcmDecoder(AdpcmDecoder&&) noexcept = default;
	/// @brief ムーブ代入演算子
	AdpcmDecoder& operator=(AdpcmDecoder&&) noexcept = default;

	/// @brief ADPCMファイルを開きヘッダーを解析する
	/// @param filePath 入力ファイルのパス
	/// @return 成功した場合 true
	bool open(std::string_view filePath) override
	{
		close();

		m_file.open(std::string(filePath), std::ios::binary);
		if (!m_file.is_open())
		{
			return false;
		}

		if (!parseHeader())
		{
			close();
			return false;
		}

		m_isOpen = true;
		m_currentFrame = 0;
		resetDecoderState();
		return true;
	}

	/// @brief デコーダーを閉じる
	void close() override
	{
		if (m_file.is_open())
		{
			m_file.close();
		}
		m_isOpen = false;
		m_currentFrame = 0;
	}

	/// @brief ADPCMデータをデコードしてPCMフレームとして読み出す
	/// @param buffer 出力バッファ（float [-1.0, 1.0]）
	/// @param frames 読み出すフレーム数
	/// @return 実際にデコードしたフレーム数
	std::size_t read(float* buffer, std::size_t frames) override
	{
		if (!m_isOpen || m_currentFrame >= m_totalFrames)
		{
			return 0;
		}

		const std::size_t remainingFrames = m_totalFrames - m_currentFrame;
		const std::size_t framesToDecode = std::min(frames, remainingFrames);
		const std::size_t channels = static_cast<std::size_t>(m_format.channels);

		/// ADPCM: 1フレームあたり channels個のニブル(4bit) = channels/2 バイト
		/// ただし奇数チャンネルの場合はパディング
		const std::size_t nibbles = framesToDecode * channels;
		const std::size_t bytesToRead = (nibbles + 1) / 2;

		m_readBuffer.resize(bytesToRead);
		m_file.read(reinterpret_cast<char*>(m_readBuffer.data()),
			static_cast<std::streamsize>(bytesToRead));

		const auto bytesRead = static_cast<std::size_t>(m_file.gcount());
		const std::size_t nibblesRead = bytesRead * 2;
		const std::size_t framesDecoded = std::min(framesToDecode,
			nibblesRead / channels);

		std::size_t nibbleIdx = 0;
		for (std::size_t f = 0; f < framesDecoded; ++f)
		{
			for (std::size_t ch = 0; ch < channels; ++ch)
			{
				const uint8_t byteVal = m_readBuffer[nibbleIdx / 2];
				const int nibble = (nibbleIdx % 2 == 0)
					? (byteVal & 0x0F)
					: ((byteVal >> 4) & 0x0F);

				const int16_t sample = decodeNibble(nibble, ch);
				buffer[f * channels + ch] = static_cast<float>(sample) / 32768.0f;
				++nibbleIdx;
			}
		}

		m_currentFrame += framesDecoded;
		return framesDecoded;
	}

	/// @brief 指定フレーム位置にシークする
	/// @param frameOffset 先頭からのフレームオフセット
	/// @return 成功した場合 true（先頭へのシークのみ完全サポート）
	bool seek(std::size_t frameOffset) override
	{
		if (!m_isOpen)
		{
			return false;
		}

		/// ADPCMはステートフルなので、先頭シークのみ正確にサポート
		if (frameOffset == 0)
		{
			m_file.clear();
			m_file.seekg(static_cast<std::streamoff>(m_dataOffset), std::ios::beg);
			m_currentFrame = 0;
			resetDecoderState();
			return true;
		}

		/// 途中シークは先頭からリシークして読み飛ばす
		m_file.clear();
		m_file.seekg(static_cast<std::streamoff>(m_dataOffset), std::ios::beg);
		m_currentFrame = 0;
		resetDecoderState();

		const std::size_t channels = static_cast<std::size_t>(m_format.channels);
		std::vector<float> skipBuffer(channels * 1024);
		std::size_t remaining = std::min(frameOffset, m_totalFrames);
		while (remaining > 0)
		{
			const std::size_t chunk = std::min(remaining, std::size_t{1024});
			const std::size_t decoded = read(skipBuffer.data(), chunk);
			if (decoded == 0)
			{
				break;
			}
			remaining -= decoded;
		}

		return m_currentFrame == std::min(frameOffset, m_totalFrames);
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

	/// @brief デコーダーが正常に開かれているかを判定する
	[[nodiscard]] bool isOpen() const noexcept override
	{
		return m_isOpen;
	}

	/// @brief コーデック名を取得する
	[[nodiscard]] const char* codecName() const noexcept override
	{
		return "IMA-ADPCM";
	}

private:
	/// @brief IMA ADPCMステップサイズテーブル
	static constexpr int16_t STEP_TABLE[89] = {
		7, 8, 9, 10, 11, 12, 13, 14,
		16, 17, 19, 21, 23, 25, 28, 31,
		34, 37, 41, 45, 50, 55, 60, 66,
		73, 80, 88, 97, 107, 118, 130, 143,
		157, 173, 190, 209, 230, 253, 279, 307,
		337, 371, 408, 449, 494, 544, 598, 658,
		724, 796, 876, 963, 1060, 1166, 1282, 1411,
		1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
		3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
		7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
		15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
		32767
	};

	/// @brief IMA ADPCMインデックステーブル
	static constexpr int INDEX_TABLE[16] = {
		-1, -1, -1, -1, 2, 4, 6, 8,
		-1, -1, -1, -1, 2, 4, 6, 8
	};

	/// @brief チャンネルごとのデコーダー状態
	struct ChannelState
	{
		int16_t predictor = 0;
		int stepIndex = 0;
	};

	/// @brief デコーダー状態をリセットする
	void resetDecoderState()
	{
		m_channelStates.assign(static_cast<std::size_t>(m_format.channels), ChannelState{});
	}

	/// @brief ニブル（4bit）をデコードして16bitサンプルを返す
	/// @param nibble 4bitニブル値 [0..15]
	/// @param channel チャンネルインデックス
	/// @return デコードされた16bitサンプル
	int16_t decodeNibble(int nibble, std::size_t channel) noexcept
	{
		auto& state = m_channelStates[channel];
		const int16_t step = STEP_TABLE[state.stepIndex];

		int diff = step >> 3;
		if (nibble & 4) diff += step;
		if (nibble & 2) diff += step >> 1;
		if (nibble & 1) diff += step >> 2;
		if (nibble & 8) diff = -diff;

		int predictor = state.predictor + diff;
		predictor = std::clamp(predictor, -32768, 32767);
		state.predictor = static_cast<int16_t>(predictor);

		state.stepIndex += INDEX_TABLE[nibble & 0x0F];
		state.stepIndex = std::clamp(state.stepIndex, 0, 88);

		return state.predictor;
	}

	/// @brief ヘッダーを解析する
	/// @return 成功した場合 true
	bool parseHeader()
	{
		char magic[4]{};
		m_file.read(magic, 4);
		if (!m_file.good() || std::memcmp(magic, "ADPC", 4) != 0)
		{
			return false;
		}

		uint16_t numChannels = 0;
		uint32_t sampleRate = 0;
		uint32_t totalFrames = 0;
		uint16_t reserved = 0;

		m_file.read(reinterpret_cast<char*>(&numChannels), 2);
		m_file.read(reinterpret_cast<char*>(&sampleRate), 4);
		m_file.read(reinterpret_cast<char*>(&totalFrames), 4);
		m_file.read(reinterpret_cast<char*>(&reserved), 2);

		if (!m_file.good() || numChannels == 0 || sampleRate == 0)
		{
			return false;
		}

		m_format.channels = static_cast<int>(numChannels);
		m_format.sampleRate = static_cast<int>(sampleRate);
		m_format.bitsPerSample = 4;
		m_totalFrames = static_cast<std::size_t>(totalFrames);
		m_dataOffset = static_cast<std::size_t>(m_file.tellg());

		return true;
	}

	std::ifstream m_file;                          ///< ファイルストリーム
	AudioFormat m_format;                          ///< フォーマット情報
	std::size_t m_dataOffset = 0;                  ///< データ開始オフセット
	std::size_t m_totalFrames = 0;                 ///< 総フレーム数
	std::size_t m_currentFrame = 0;                ///< 現在のフレーム位置
	bool m_isOpen = false;                         ///< 開かれているか
	std::vector<uint8_t> m_readBuffer;             ///< 一時読み込みバッファ
	std::vector<ChannelState> m_channelStates;     ///< チャンネルごとのデコーダー状態
};

#ifdef MITIRU_HAS_VORBIS

/// @brief OGG/Vorbisデコーダー
/// @details libvorbisfileを使用してOGG Vorbisファイルをデコードする。
///          MITIRU_HAS_VORBIS が定義されている場合のみコンパイルされる。
///          libvorbisfile (vorbisfile.h) がインクルードパスに存在し、
///          vorbisfile ライブラリがリンクされていることが前提。
///
/// @code
/// #define MITIRU_HAS_VORBIS
/// #include <mitiru/audio/OggDecoder.hpp>
///
/// mitiru::audio::OggVorbisDecoder decoder;
/// if (decoder.open("music.ogg"))
/// {
///     auto fmt = decoder.format();
///     std::vector<float> buf(fmt.channels * 1024);
///     decoder.read(buf.data(), 1024);
///     decoder.close();
/// }
/// @endcode
class OggVorbisDecoder : public IAudioDecoder
{
public:
	/// @brief デフォルトコンストラクタ
	OggVorbisDecoder() = default;

	/// @brief デストラクタ
	~OggVorbisDecoder() override
	{
		close();
	}

	/// @brief コピー禁止
	OggVorbisDecoder(const OggVorbisDecoder&) = delete;
	/// @brief コピー代入禁止
	OggVorbisDecoder& operator=(const OggVorbisDecoder&) = delete;

	/// @brief OGGファイルを開く
	/// @param filePath OGGファイルのパス
	/// @return 成功した場合 true
	bool open(std::string_view filePath) override
	{
		close();

		if (ov_fopen(std::string(filePath).c_str(), &m_vorbisFile) != 0)
		{
			return false;
		}

		const vorbis_info* info = ov_info(&m_vorbisFile, -1);
		if (!info)
		{
			ov_clear(&m_vorbisFile);
			return false;
		}

		m_format.channels = info->channels;
		m_format.sampleRate = static_cast<int>(info->rate);
		m_format.bitsPerSample = 16;

		const ogg_int64_t total = ov_pcm_total(&m_vorbisFile, -1);
		m_totalFrames = (total > 0) ? static_cast<std::size_t>(total) : 0;

		m_isOpen = true;
		m_currentFrame = 0;
		return true;
	}

	/// @brief デコーダーを閉じる
	void close() override
	{
		if (m_isOpen)
		{
			ov_clear(&m_vorbisFile);
			m_isOpen = false;
			m_currentFrame = 0;
		}
	}

	/// @brief OGGデータをデコードしてPCMフレームとして読み出す
	/// @param buffer 出力バッファ（float [-1.0, 1.0]）
	/// @param frames 読み出すフレーム数
	/// @return 実際にデコードしたフレーム数
	std::size_t read(float* buffer, std::size_t frames) override
	{
		if (!m_isOpen)
		{
			return 0;
		}

		const std::size_t channels = static_cast<std::size_t>(m_format.channels);
		const std::size_t totalSamples = frames * channels;
		float** pcmChannels = nullptr;
		int currentSection = 0;
		std::size_t framesRead = 0;

		while (framesRead < frames)
		{
			const auto remaining = static_cast<int>(frames - framesRead);
			const long decoded = ov_read_float(
				&m_vorbisFile, &pcmChannels, remaining, &currentSection);

			if (decoded <= 0)
			{
				break;
			}

			/// ov_read_float は非インターリーブで返すので、インターリーブに変換
			const auto decodedFrames = static_cast<std::size_t>(decoded);
			for (std::size_t f = 0; f < decodedFrames; ++f)
			{
				for (std::size_t ch = 0; ch < channels; ++ch)
				{
					buffer[(framesRead + f) * channels + ch] = pcmChannels[ch][f];
				}
			}
			framesRead += decodedFrames;
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

		if (ov_pcm_seek(&m_vorbisFile, static_cast<ogg_int64_t>(frameOffset)) != 0)
		{
			return false;
		}

		m_currentFrame = frameOffset;
		return true;
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

	/// @brief デコーダーが正常に開かれているかを判定する
	[[nodiscard]] bool isOpen() const noexcept override
	{
		return m_isOpen;
	}

	/// @brief コーデック名を取得する
	[[nodiscard]] const char* codecName() const noexcept override
	{
		return "OGG/Vorbis";
	}

private:
	OggVorbis_File m_vorbisFile{};          ///< libvorbisfileハンドル
	AudioFormat m_format;                   ///< フォーマット情報
	std::size_t m_totalFrames = 0;          ///< 総フレーム数
	std::size_t m_currentFrame = 0;         ///< 現在のフレーム位置
	bool m_isOpen = false;                  ///< 開かれているか
};

#endif // MITIRU_HAS_VORBIS

/// @brief デコーダーをIAudioStreamインターフェースにアダプトする
/// @details IAudioDecoderの実装をIAudioStreamとして使用可能にするアダプター。
///          StreamingAudioEngineがデコーダーとストリームを統一的に扱えるようにする。
///
/// @code
/// auto decoder = std::make_unique<mitiru::audio::AdpcmDecoder>();
/// decoder->open("music.adpcm");
/// auto stream = std::make_unique<mitiru::audio::DecoderStreamAdapter>(std::move(decoder));
/// // stream は IAudioStream として使用可能
/// @endcode
class DecoderStreamAdapter : public IAudioStream
{
public:
	/// @brief コンストラクタ
	/// @param decoder デコーダーインスタンス（所有権を移動）
	explicit DecoderStreamAdapter(std::unique_ptr<IAudioDecoder> decoder)
		: m_decoder(std::move(decoder))
	{
	}

	/// @brief コピー禁止
	DecoderStreamAdapter(const DecoderStreamAdapter&) = delete;
	/// @brief コピー代入禁止
	DecoderStreamAdapter& operator=(const DecoderStreamAdapter&) = delete;
	/// @brief ムーブコンストラクタ
	DecoderStreamAdapter(DecoderStreamAdapter&&) noexcept = default;
	/// @brief ムーブ代入演算子
	DecoderStreamAdapter& operator=(DecoderStreamAdapter&&) noexcept = default;

	/// @brief ストリームを開く（デコーダーが既に開かれている前提）
	/// @return デコーダーが有効なら true
	bool open() override
	{
		return m_decoder && m_decoder->isOpen();
	}

	/// @brief ストリームを閉じる
	void close() override
	{
		if (m_decoder)
		{
			m_decoder->close();
		}
	}

	/// @brief PCMフレームを読み出す
	/// @param buffer 出力バッファ
	/// @param frames 読み出すフレーム数
	/// @return 実際に読み出したフレーム数
	std::size_t read(float* buffer, std::size_t frames) override
	{
		if (!m_decoder)
		{
			return 0;
		}
		return m_decoder->read(buffer, frames);
	}

	/// @brief 指定フレーム位置にシークする
	/// @param frameOffset 先頭からのフレームオフセット
	/// @return 成功した場合 true
	bool seek(std::size_t frameOffset) override
	{
		if (!m_decoder)
		{
			return false;
		}
		return m_decoder->seek(frameOffset);
	}

	/// @brief フォーマット情報を取得する
	[[nodiscard]] AudioFormat format() const noexcept override
	{
		if (!m_decoder)
		{
			return {};
		}
		return m_decoder->format();
	}

	/// @brief 総フレーム数を取得する
	[[nodiscard]] std::size_t totalFrames() const noexcept override
	{
		if (!m_decoder)
		{
			return 0;
		}
		return m_decoder->totalFrames();
	}

	/// @brief ストリーム終端に達したかを判定する
	[[nodiscard]] bool isEof() const noexcept override
	{
		return !m_decoder || m_decoder->isEof();
	}

	/// @brief ストリームが正常に開かれているかを判定する
	[[nodiscard]] bool isOpen() const noexcept override
	{
		return m_decoder && m_decoder->isOpen();
	}

private:
	std::unique_ptr<IAudioDecoder> m_decoder;   ///< 内部デコーダー
};

} // namespace mitiru::audio
