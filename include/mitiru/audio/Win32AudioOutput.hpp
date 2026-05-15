#pragma once

/// @file Win32AudioOutput.hpp
/// @brief Win32 waveOut APIによるオーディオ出力
/// @details waveOut APIを使用したPCMオーディオのダブルバッファリング出力。
///          COM初期化不要でシンプルにPCMデータを再生できる。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mitiru::audio
{

/// @brief オーディオ出力の設定
struct AudioOutputConfig
{
	std::uint32_t sampleRate = 44100;      ///< サンプルレート (Hz)
	std::uint16_t channels = 2;            ///< チャンネル数
	std::uint16_t bitsPerSample = 16;      ///< サンプルあたりのビット数

	/// @brief 1サンプルのバイト数を計算する
	/// @return bytes per sample (channels * bitsPerSample / 8)
	[[nodiscard]] constexpr std::uint32_t bytesPerSample() const noexcept
	{
		return static_cast<std::uint32_t>(channels) *
			(static_cast<std::uint32_t>(bitsPerSample) / 8u);
	}

	/// @brief 1秒分のバイト数を計算する
	/// @return bytes per second
	[[nodiscard]] constexpr std::uint32_t bytesPerSecond() const noexcept
	{
		return sampleRate * bytesPerSample();
	}

	/// @brief 指定ミリ秒分のバッファサイズをバイト単位で計算する
	/// @param milliseconds 時間（ミリ秒）
	/// @return バッファサイズ（バイト）
	[[nodiscard]] constexpr std::uint32_t bufferSizeForMs(std::uint32_t milliseconds) const noexcept
	{
		return (bytesPerSecond() * milliseconds) / 1000u;
	}
};

/// @brief Win32 waveOut APIによるオーディオ出力
/// @details ダブルバッファリングでPCMデータを出力する。
///          init() → submitBuffer() → ... → shutdown() の順で使用する。
///
/// @code
/// mitiru::audio::Win32AudioOutput output;
/// mitiru::audio::AudioOutputConfig config;
/// config.sampleRate = 44100;
/// config.channels = 2;
/// config.bitsPerSample = 16;
///
/// if (output.init(config))
/// {
///     // PCMデータを生成して送信
///     std::vector<std::int16_t> samples(config.sampleRate);
///     output.submitBuffer(samples.data(), samples.size() * sizeof(std::int16_t));
/// }
/// output.shutdown();
/// @endcode
class Win32AudioOutput
{
public:
	/// @brief バッファ数（ダブルバッファリング）
	static constexpr int NUM_BUFFERS = 2;

	/// @brief デフォルトコンストラクタ
	Win32AudioOutput() noexcept = default;

	/// @brief デストラクタ（自動シャットダウン）
	~Win32AudioOutput()
	{
		shutdown();
	}

	/// @brief コピー禁止
	Win32AudioOutput(const Win32AudioOutput&) = delete;
	/// @brief コピー代入禁止
	Win32AudioOutput& operator=(const Win32AudioOutput&) = delete;
	/// @brief ムーブ禁止（WAVEHDRがポインタで管理されるため）
	Win32AudioOutput(Win32AudioOutput&&) = delete;
	/// @brief ムーブ代入禁止
	Win32AudioOutput& operator=(Win32AudioOutput&&) = delete;

	/// @brief オーディオ出力を初期化する
	/// @param config オーディオ設定
	/// @return 成功した場合 true
	bool init(const AudioOutputConfig& config) noexcept
	{
		if (m_initialized) return false;

		m_config = config;

		/// WAVEFORMATEX構造体を設定
		WAVEFORMATEX wfx = {};
		wfx.wFormatTag = WAVE_FORMAT_PCM;
		wfx.nChannels = config.channels;
		wfx.nSamplesPerSec = config.sampleRate;
		wfx.nBlockAlign = static_cast<WORD>(config.bytesPerSample());
		wfx.nAvgBytesPerSec = config.bytesPerSecond();
		wfx.wBitsPerSample = config.bitsPerSample;

		/// waveOutデバイスを開く
		const MMRESULT result = waveOutOpen(
			&m_hWaveOut,
			WAVE_MAPPER,
			&wfx,
			0,     ///< コールバックなし（ポーリング方式）
			0,
			CALLBACK_NULL);

		if (result != MMSYSERR_NOERROR)
		{
			return false;
		}

		m_initialized = true;
		return true;
	}

	/// @brief PCMデータバッファを送信する
	/// @param data PCMデータへのポインタ
	/// @param sizeBytes データサイズ（バイト）
	/// @return 成功した場合 true
	bool submitBuffer(const void* data, std::uint32_t sizeBytes) noexcept
	{
		if (!m_initialized || !data || sizeBytes == 0) return false;

		/// 現在のバッファインデックスを取得
		auto& hdr = m_headers[static_cast<std::size_t>(m_currentBuffer)];
		auto& buf = m_buffers[static_cast<std::size_t>(m_currentBuffer)];

		/// 前回のバッファが未完了なら完了を待つ
		if (hdr.dwFlags & WHDR_PREPARED)
		{
			waveOutUnprepareHeader(m_hWaveOut, &hdr, sizeof(WAVEHDR));
		}

		/// バッファにデータをコピー
		buf.resize(sizeBytes);
		std::memcpy(buf.data(), data, sizeBytes);

		/// WAVEHDRを設定
		std::memset(&hdr, 0, sizeof(WAVEHDR));
		hdr.lpData = reinterpret_cast<LPSTR>(buf.data());
		hdr.dwBufferLength = sizeBytes;

		/// バッファを準備して送信
		MMRESULT result = waveOutPrepareHeader(
			m_hWaveOut, &hdr, sizeof(WAVEHDR));
		if (result != MMSYSERR_NOERROR) return false;

		result = waveOutWrite(m_hWaveOut, &hdr, sizeof(WAVEHDR));
		if (result != MMSYSERR_NOERROR) return false;

		/// 次のバッファに切り替え
		m_currentBuffer = (m_currentBuffer + 1) % NUM_BUFFERS;
		return true;
	}

	/// @brief 再生中かどうかを判定する
	/// @return いずれかのバッファが再生中なら true
	[[nodiscard]] bool isPlaying() const noexcept
	{
		if (!m_initialized) return false;

		for (const auto& hdr : m_headers)
		{
			if ((hdr.dwFlags & WHDR_DONE) == 0 &&
				(hdr.dwFlags & WHDR_PREPARED) != 0)
			{
				return true;
			}
		}
		return false;
	}

	/// @brief ボリュームを設定する
	/// @param volume ボリューム [0.0, 1.0]
	void setVolume(float volume) noexcept
	{
		m_volume = std::clamp(volume, 0.0f, 1.0f);

		if (m_initialized)
		{
			const auto level = static_cast<DWORD>(m_volume * 0xFFFF);
			const DWORD dwVolume = (level & 0xFFFF) | ((level & 0xFFFF) << 16);
			waveOutSetVolume(m_hWaveOut, dwVolume);
		}
	}

	/// @brief 現在のボリュームを取得する
	/// @return ボリューム [0.0, 1.0]
	[[nodiscard]] float volume() const noexcept
	{
		return m_volume;
	}

	/// @brief オーディオ出力をシャットダウンする
	void shutdown() noexcept
	{
		if (!m_initialized) return;

		waveOutReset(m_hWaveOut);

		for (auto& hdr : m_headers)
		{
			if (hdr.dwFlags & WHDR_PREPARED)
			{
				waveOutUnprepareHeader(m_hWaveOut, &hdr, sizeof(WAVEHDR));
			}
		}

		waveOutClose(m_hWaveOut);
		m_hWaveOut = nullptr;
		m_initialized = false;
	}

	/// @brief 初期化済みかどうかを判定する
	/// @return 初期化済みなら true
	[[nodiscard]] bool isInitialized() const noexcept
	{
		return m_initialized;
	}

	/// @brief 現在の設定を取得する
	/// @return オーディオ設定
	[[nodiscard]] const AudioOutputConfig& config() const noexcept
	{
		return m_config;
	}

private:
	HWAVEOUT m_hWaveOut = nullptr;                         ///< waveOutハンドル
	std::array<WAVEHDR, NUM_BUFFERS> m_headers{};          ///< WAVEHDRバッファ
	std::array<std::vector<std::uint8_t>, NUM_BUFFERS> m_buffers; ///< データバッファ
	int m_currentBuffer = 0;                               ///< 現在のバッファインデックス
	AudioOutputConfig m_config;                            ///< オーディオ設定
	float m_volume = 1.0f;                                 ///< ボリューム [0.0, 1.0]
	bool m_initialized = false;                            ///< 初期化済みフラグ
};

} // namespace mitiru::audio

#endif // _WIN32
