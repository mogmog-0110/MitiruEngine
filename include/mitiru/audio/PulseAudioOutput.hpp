#pragma once

/// @file PulseAudioOutput.hpp
/// @brief PulseAudioバックエンドによるオーディオ出力（Linux）
/// @details PulseAudio Simple APIを使用したfloat PCM出力。
///          RAII管理によるpa_simpleハンドルの安全な管理と、
///          スレッドセーフなリングバッファによる非同期再生を提供する。

#ifdef MITIRU_PLATFORM_UNIX

#ifdef MITIRU_HAS_PULSEAUDIO

#include <mitiru/audio/IAudioOutput.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <pulse/simple.h>
#include <pulse/error.h>

namespace mitiru::audio
{

/// @brief pa_simpleハンドルのRAIIラッパー
/// @details PulseAudioの接続を安全に管理する。
///          デストラクタで自動的にpa_simple_freeを呼び出す。
class PulseSimpleHandle
{
public:
	/// @brief デフォルトコンストラクタ（未接続状態）
	PulseSimpleHandle() noexcept = default;

	/// @brief pa_simpleハンドルを受け取るコンストラクタ
	/// @param handle pa_simpleポインタ（所有権を取得）
	explicit PulseSimpleHandle(pa_simple* handle) noexcept
		: m_handle(handle)
	{
	}

	/// @brief デストラクタ（自動解放）
	~PulseSimpleHandle()
	{
		reset();
	}

	/// @brief コピー禁止
	PulseSimpleHandle(const PulseSimpleHandle&) = delete;
	/// @brief コピー代入禁止
	PulseSimpleHandle& operator=(const PulseSimpleHandle&) = delete;

	/// @brief ムーブコンストラクタ
	PulseSimpleHandle(PulseSimpleHandle&& other) noexcept
		: m_handle(other.m_handle)
	{
		other.m_handle = nullptr;
	}

	/// @brief ムーブ代入演算子
	PulseSimpleHandle& operator=(PulseSimpleHandle&& other) noexcept
	{
		if (this != &other)
		{
			reset();
			m_handle = other.m_handle;
			other.m_handle = nullptr;
		}
		return *this;
	}

	/// @brief ハンドルを解放する
	void reset() noexcept
	{
		if (m_handle)
		{
			pa_simple_free(m_handle);
			m_handle = nullptr;
		}
	}

	/// @brief ハンドルを取得する
	/// @return pa_simpleポインタ（nullptr可）
	[[nodiscard]] pa_simple* get() const noexcept
	{
		return m_handle;
	}

	/// @brief 有効なハンドルを保持しているか
	/// @return 有効なら true
	[[nodiscard]] explicit operator bool() const noexcept
	{
		return m_handle != nullptr;
	}

private:
	pa_simple* m_handle = nullptr;
};

/// @brief スレッドセーフなリングバッファ
/// @details float PCMサンプルの非同期受け渡しに使用する。
///          プロデューサー（write側）とコンシューマー（再生スレッド側）が
///          ロックフリーに近い形で動作する。
class AudioRingBuffer
{
public:
	/// @brief コンストラクタ
	/// @param capacity バッファ容量（サンプル数）
	explicit AudioRingBuffer(std::size_t capacity)
		: m_buffer(capacity, 0.0f)
		, m_capacity(capacity)
	{
	}

	/// @brief サンプルを書き込む
	/// @param data 書き込むサンプル配列
	/// @param count サンプル数
	/// @return 実際に書き込まれたサンプル数
	std::size_t write(const float* data, std::size_t count)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		const std::size_t available = m_capacity - m_size;
		const std::size_t toWrite = std::min(count, available);

		for (std::size_t i = 0; i < toWrite; ++i)
		{
			m_buffer[(m_writePos + i) % m_capacity] = data[i];
		}
		m_writePos = (m_writePos + toWrite) % m_capacity;
		m_size += toWrite;

		return toWrite;
	}

	/// @brief サンプルを読み出す
	/// @param data 読み出し先バッファ
	/// @param count 読み出すサンプル数
	/// @return 実際に読み出されたサンプル数
	std::size_t read(float* data, std::size_t count)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		const std::size_t toRead = std::min(count, m_size);

		for (std::size_t i = 0; i < toRead; ++i)
		{
			data[i] = m_buffer[(m_readPos + i) % m_capacity];
		}
		m_readPos = (m_readPos + toRead) % m_capacity;
		m_size -= toRead;

		/// 読めなかった分は無音で埋める
		if (toRead < count)
		{
			std::memset(data + toRead, 0, (count - toRead) * sizeof(float));
		}

		return toRead;
	}

	/// @brief 現在のバッファ内サンプル数を取得する
	/// @return サンプル数
	[[nodiscard]] std::size_t size() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_size;
	}

	/// @brief バッファ容量を取得する
	/// @return 容量（サンプル数）
	[[nodiscard]] std::size_t capacity() const noexcept
	{
		return m_capacity;
	}

	/// @brief バッファをクリアする
	void clear()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_readPos = 0;
		m_writePos = 0;
		m_size = 0;
	}

private:
	std::vector<float> m_buffer;
	std::size_t m_capacity;
	std::size_t m_readPos = 0;
	std::size_t m_writePos = 0;
	std::size_t m_size = 0;
	mutable std::mutex m_mutex;
};

/// @brief PulseAudioバックエンドによるオーディオ出力
/// @details PulseAudio Simple APIを使用してfloat PCMデータを出力する。
///          内部で再生スレッドを起動し、リングバッファ経由で非同期に再生する。
///          デフォルトシンクを自動検出する。
///
/// @code
/// mitiru::audio::PulseAudioOutput output;
/// if (output.initialize(44100, 2, 4096))
/// {
///     std::vector<float> samples(4096);
///     // ... サンプル生成 ...
///     output.write(samples.data(), samples.size());
/// }
/// output.shutdown();
/// @endcode
class PulseAudioOutput : public IAudioOutput
{
public:
	/// @brief デフォルトコンストラクタ
	PulseAudioOutput() noexcept = default;

	/// @brief デストラクタ（自動シャットダウン）
	~PulseAudioOutput() override
	{
		shutdown();
	}

	/// @brief コピー禁止
	PulseAudioOutput(const PulseAudioOutput&) = delete;
	/// @brief コピー代入禁止
	PulseAudioOutput& operator=(const PulseAudioOutput&) = delete;
	/// @brief ムーブ禁止（再生スレッド管理のため）
	PulseAudioOutput(PulseAudioOutput&&) = delete;
	/// @brief ムーブ代入禁止
	PulseAudioOutput& operator=(PulseAudioOutput&&) = delete;

	/// @brief PulseAudioに接続して初期化する
	/// @param sr サンプルレート (Hz)
	/// @param ch チャンネル数
	/// @param bs バッファサイズ（サンプル数）
	/// @return 成功した場合 true
	bool initialize(int sr, int ch, int bs) override
	{
		if (m_initialized.load()) return false;
		if (sr <= 0 || ch <= 0 || bs <= 0) return false;

		m_sampleRate = sr;
		m_channels = ch;
		m_bufferSize = bs;

		/// PulseAudioサンプルスペックを設定
		pa_sample_spec spec = {};
		spec.format = PA_SAMPLE_FLOAT32LE;
		spec.rate = static_cast<uint32_t>(sr);
		spec.channels = static_cast<uint8_t>(ch);

		/// デフォルトシンクに接続（dev=nullptr で自動検出）
		int error = 0;
		pa_simple* raw = pa_simple_new(
			nullptr,            ///< サーバー名（nullptr = デフォルト）
			"MitiruEngine",     ///< アプリケーション名
			PA_STREAM_PLAYBACK, ///< ストリーム方向
			nullptr,            ///< デバイス名（nullptr = デフォルトシンク）
			"audio-output",     ///< ストリーム名
			&spec,              ///< サンプルスペック
			nullptr,            ///< チャンネルマップ（nullptr = デフォルト）
			nullptr,            ///< バッファリング属性（nullptr = デフォルト）
			&error);

		if (!raw)
		{
			return false;
		}

		m_handle = PulseSimpleHandle(raw);

		/// リングバッファを初期化（バッファサイズの4倍の容量）
		const std::size_t ringCapacity =
			static_cast<std::size_t>(bs) * static_cast<std::size_t>(ch) * 4;
		m_ringBuffer = std::make_unique<AudioRingBuffer>(ringCapacity);

		/// 再生スレッドを起動
		m_running.store(true);
		m_initialized.store(true);
		m_playbackThread = std::thread(&PulseAudioOutput::playbackLoop, this);

		return true;
	}

	/// @brief PCMサンプルをリングバッファに書き込む
	/// @param samples float PCMサンプル配列
	/// @param count サンプル数（全チャンネル合計）
	/// @return 成功した場合 true
	bool write(const float* samples, std::size_t count) override
	{
		if (!m_initialized.load()) return false;
		if (samples == nullptr || count == 0) return false;

		const std::size_t written = m_ringBuffer->write(samples, count);
		m_hasData.store(true);
		return written > 0;
	}

	/// @brief 再生中かどうかを判定する
	/// @return データがバッファにあり再生スレッドが稼働中なら true
	[[nodiscard]] bool isPlaying() const override
	{
		return m_initialized.load() && m_hasData.load();
	}

	/// @brief PulseAudio接続をシャットダウンする
	void shutdown() override
	{
		if (!m_initialized.load()) return;

		/// 再生スレッドを停止
		m_running.store(false);
		if (m_playbackThread.joinable())
		{
			m_playbackThread.join();
		}

		/// PulseAudioのバッファをフラッシュ
		if (m_handle)
		{
			int error = 0;
			pa_simple_flush(m_handle.get(), &error);
		}

		m_handle.reset();
		m_ringBuffer.reset();
		m_initialized.store(false);
		m_hasData.store(false);
	}

	/// @brief 初期化済みかどうか
	[[nodiscard]] bool isInitialized() const override
	{
		return m_initialized.load();
	}

	/// @brief サンプルレートを取得する
	[[nodiscard]] int sampleRate() const override
	{
		return m_sampleRate;
	}

	/// @brief チャンネル数を取得する
	[[nodiscard]] int channels() const override
	{
		return m_channels;
	}

	/// @brief バッファサイズを取得する
	[[nodiscard]] int bufferSize() const override
	{
		return m_bufferSize;
	}

	/// @brief バックエンド名を取得する
	/// @return "PulseAudio"
	[[nodiscard]] const char* backendName() const override
	{
		return "PulseAudio";
	}

private:
	/// @brief 再生スレッドのメインループ
	/// @details リングバッファからサンプルを読み出してPulseAudioに送信する
	void playbackLoop()
	{
		const std::size_t chunkSize =
			static_cast<std::size_t>(m_bufferSize) *
			static_cast<std::size_t>(m_channels);
		std::vector<float> chunk(chunkSize, 0.0f);

		while (m_running.load())
		{
			const std::size_t available = m_ringBuffer->size();
			if (available < chunkSize)
			{
				m_hasData.store(false);
				/// 短いスリープでCPU消費を抑える
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}

			m_ringBuffer->read(chunk.data(), chunkSize);

			int error = 0;
			const int result = pa_simple_write(
				m_handle.get(),
				chunk.data(),
				chunkSize * sizeof(float),
				&error);

			if (result < 0)
			{
				/// 書き込みエラー時は再生を停止
				m_running.store(false);
				break;
			}
		}
	}

	PulseSimpleHandle m_handle;                             ///< PulseAudioハンドル
	std::unique_ptr<AudioRingBuffer> m_ringBuffer;          ///< リングバッファ
	std::thread m_playbackThread;                           ///< 再生スレッド
	std::atomic<bool> m_initialized{false};                 ///< 初期化済みフラグ
	std::atomic<bool> m_running{false};                     ///< スレッド実行フラグ
	std::atomic<bool> m_hasData{false};                     ///< データ有無フラグ
	int m_sampleRate = 0;                                   ///< サンプルレート
	int m_channels = 0;                                     ///< チャンネル数
	int m_bufferSize = 0;                                   ///< バッファサイズ
};

} // namespace mitiru::audio

#endif // MITIRU_HAS_PULSEAUDIO
#endif // MITIRU_PLATFORM_UNIX
