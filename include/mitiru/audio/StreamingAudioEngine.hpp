#pragma once

/// @file StreamingAudioEngine.hpp
/// @brief ストリーミングオーディオエンジン
/// @details WAVファイルをチャンク単位で読み込み、バックグラウンドスレッドで
///          リングバッファに充填するストリーミング再生エンジン。
///          play/pause/stop/seek操作をサポートし、IAudioOutputに
///          PCMデータを供給する。大容量ファイルをメモリに全読み込みせず、
///          一定サイズのリングバッファ経由で逐次再生する。

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <mitiru/audio/AudioStream.hpp>
#include <mitiru/audio/IAudioOutput.hpp>
#include <mitiru/audio/RingBuffer.hpp>
#include <mitiru/debug/TracyZones.hpp>

namespace mitiru::audio
{

/// @brief ストリーミング再生の状態
enum class StreamState : uint8_t
{
	Stopped = 0,   ///< 停止中
	Playing,       ///< 再生中
	Paused,        ///< 一時停止中
};

/// @brief ストリーミングオーディオエンジン
/// @details バックグラウンドスレッドがIAudioStreamからPCMデータを読み出し、
///          ロックフリーリングバッファに充填する。メインスレッドまたは
///          オーディオコールバックスレッドがリングバッファからデータを取り出し、
///          IAudioOutputに書き込む。
///
/// @code
/// auto output = createPlatformAudioOutput();
/// mitiru::audio::StreamingAudioEngine engine(std::move(output));
///
/// auto stream = std::make_unique<mitiru::audio::WavAudioStream>("bgm.wav");
/// stream->open();
/// engine.play(std::move(stream));
///
/// // ゲームループ内で毎フレーム呼び出し
/// engine.update();
///
/// engine.pause();
/// engine.resume();
/// engine.seek(44100 * 30); // 30秒位置にシーク
/// engine.stop();
/// @endcode
class StreamingAudioEngine
{
public:
	/// @brief デフォルトのリングバッファサイズ（フレーム数）
	static constexpr std::size_t DEFAULT_RING_BUFFER_FRAMES = 16384;

	/// @brief バックグラウンドスレッドの1回あたり読み込みフレーム数
	static constexpr std::size_t FILL_CHUNK_FRAMES = 2048;

	/// @brief バックグラウンドスレッドのスリープ間隔
	static constexpr std::chrono::milliseconds FILL_SLEEP_INTERVAL{5};

	/// @brief コンストラクタ
	/// @param output オーディオ出力バックエンド（所有権を移動）
	/// @param ringBufferFrames リングバッファのフレーム数
	explicit StreamingAudioEngine(
		std::unique_ptr<IAudioOutput> output = nullptr,
		std::size_t ringBufferFrames = DEFAULT_RING_BUFFER_FRAMES)
		: m_output(std::move(output))
		, m_ringBufferFrames(ringBufferFrames)
	{
	}

	/// @brief デストラクタ（再生停止・スレッド終了を保証）
	~StreamingAudioEngine()
	{
		stop();
	}

	/// @brief コピー禁止
	StreamingAudioEngine(const StreamingAudioEngine&) = delete;
	/// @brief コピー代入禁止
	StreamingAudioEngine& operator=(const StreamingAudioEngine&) = delete;
	/// @brief ムーブ禁止（スレッド所有のため）
	StreamingAudioEngine(StreamingAudioEngine&&) = delete;
	/// @brief ムーブ代入禁止
	StreamingAudioEngine& operator=(StreamingAudioEngine&&) = delete;

	/// @brief ストリーミング再生を開始する
	/// @param stream 再生するオーディオストリーム（所有権を移動、open済みであること）
	/// @param loop ループ再生するか
	/// @return 成功した場合 true
	bool play(std::unique_ptr<IAudioStream> stream, bool loop = false)
	{
		if (!stream || !stream->isOpen())
		{
			return false;
		}

		stop();

		const auto fmt = stream->format();
		const std::size_t channels = static_cast<std::size_t>(fmt.channels);

		/// リングバッファはサンプル単位（フレーム数 * チャンネル数）
		m_ringBuffer = std::make_unique<RingBuffer<float>>(m_ringBufferFrames * channels);

		{
			const std::lock_guard<std::mutex> lock(m_streamMutex);
			m_stream = std::move(stream);
		}

		m_format = fmt;
		m_looping.store(loop, std::memory_order_relaxed);
		m_state.store(StreamState::Playing, std::memory_order_release);
		m_seekRequest.store(false, std::memory_order_relaxed);
		m_stopRequested.store(false, std::memory_order_relaxed);
		m_streamFinished.store(false, std::memory_order_relaxed);

		/// オーディオ出力の初期化
		if (m_output && !m_output->isInitialized())
		{
			m_output->initialize(fmt.sampleRate, fmt.channels, fmt.sampleRate / 10);
		}

		/// バックグラウンド充填スレッドを起動
		m_fillThread = std::thread([this] { fillThreadFunc(); });

		return true;
	}

	/// @brief 再生を一時停止する
	void pause() noexcept
	{
		StreamState expected = StreamState::Playing;
		m_state.compare_exchange_strong(expected, StreamState::Paused,
			std::memory_order_release, std::memory_order_relaxed);
	}

	/// @brief 一時停止を解除して再生を再開する
	void resume() noexcept
	{
		StreamState expected = StreamState::Paused;
		m_state.compare_exchange_strong(expected, StreamState::Playing,
			std::memory_order_release, std::memory_order_relaxed);
	}

	/// @brief 再生を停止する
	void stop()
	{
		m_stopRequested.store(true, std::memory_order_release);
		m_state.store(StreamState::Stopped, std::memory_order_release);

		if (m_fillThread.joinable())
		{
			m_fillThread.join();
		}

		{
			const std::lock_guard<std::mutex> lock(m_streamMutex);
			m_stream.reset();
		}

		m_ringBuffer.reset();
		m_stopRequested.store(false, std::memory_order_relaxed);
	}

	/// @brief 指定フレーム位置にシークする
	/// @param frameOffset 先頭からのフレームオフセット
	void seek(std::size_t frameOffset) noexcept
	{
		m_seekTarget.store(frameOffset, std::memory_order_relaxed);
		m_seekRequest.store(true, std::memory_order_release);
	}

	/// @brief 毎フレーム更新（リングバッファからオーディオ出力にデータを転送）
	/// @details ゲームループ内で毎フレーム呼び出す。リングバッファからデータを読み出し、
	///          IAudioOutputに書き込む。出力がない場合はデータを消費するだけ。
	void update()
	{
		MITIRU_ZONE_NAMED("Audio::Streaming::Update");
		if (m_state.load(std::memory_order_acquire) != StreamState::Playing)
		{
			return;
		}

		if (!m_ringBuffer)
		{
			return;
		}

		const std::size_t channels = static_cast<std::size_t>(m_format.channels);
		const std::size_t samplesToRead = FILL_CHUNK_FRAMES * channels;

		m_outputBuffer.resize(samplesToRead);
		const std::size_t samplesRead = m_ringBuffer->read(
			m_outputBuffer.data(), samplesToRead);

		if (samplesRead > 0 && m_output && m_output->isInitialized())
		{
			m_output->write(m_outputBuffer.data(), samplesRead);
		}

		/// ストリーム終了かつバッファ空なら停止
		if (m_streamFinished.load(std::memory_order_acquire) && m_ringBuffer->empty())
		{
			m_state.store(StreamState::Stopped, std::memory_order_release);
		}
	}

	/// @brief ボリュームを設定する
	/// @param volume ボリューム [0.0, 1.0]
	void setVolume(float volume) noexcept
	{
		m_volume.store(std::clamp(volume, 0.0f, 1.0f), std::memory_order_relaxed);
	}

	/// @brief ボリュームを取得する
	/// @return ボリューム [0.0, 1.0]
	[[nodiscard]] float volume() const noexcept
	{
		return m_volume.load(std::memory_order_relaxed);
	}

	/// @brief 現在の再生状態を取得する
	/// @return 再生状態
	[[nodiscard]] StreamState state() const noexcept
	{
		return m_state.load(std::memory_order_acquire);
	}

	/// @brief 再生中かどうかを判定する
	/// @return 再生中なら true
	[[nodiscard]] bool isPlaying() const noexcept
	{
		return m_state.load(std::memory_order_acquire) == StreamState::Playing;
	}

	/// @brief 一時停止中かどうかを判定する
	/// @return 一時停止中なら true
	[[nodiscard]] bool isPaused() const noexcept
	{
		return m_state.load(std::memory_order_acquire) == StreamState::Paused;
	}

	/// @brief 現在のフォーマット情報を取得する
	/// @return オーディオフォーマット
	[[nodiscard]] AudioFormat format() const noexcept
	{
		return m_format;
	}

	/// @brief リングバッファ内の読み出し可能サンプル数を取得する
	/// @return 読み出し可能サンプル数
	[[nodiscard]] std::size_t bufferedSamples() const noexcept
	{
		if (!m_ringBuffer)
		{
			return 0;
		}
		return m_ringBuffer->availableRead();
	}

	/// @brief ループ再生フラグを設定する
	/// @param loop ループ再生するか
	void setLooping(bool loop) noexcept
	{
		m_looping.store(loop, std::memory_order_relaxed);
	}

	/// @brief ループ再生フラグを取得する
	/// @return ループ再生なら true
	[[nodiscard]] bool isLooping() const noexcept
	{
		return m_looping.load(std::memory_order_relaxed);
	}

	/// @brief オーディオ出力バックエンドを設定する
	/// @param output オーディオ出力バックエンド（所有権を移動）
	void setOutput(std::unique_ptr<IAudioOutput> output)
	{
		m_output = std::move(output);
	}

private:
	/// @brief バックグラウンド充填スレッドのメイン関数
	/// @details ストリームからデータを読み出し、リングバッファに書き込む。
	///          シーク要求の処理、ループ再生の巻き戻し、停止要求の監視を行う。
	void fillThreadFunc()
	{
		const std::size_t channels = static_cast<std::size_t>(m_format.channels);
		const std::size_t chunkSamples = FILL_CHUNK_FRAMES * channels;
		std::vector<float> readBuffer(chunkSamples);

		while (!m_stopRequested.load(std::memory_order_acquire))
		{
			/// シーク要求の処理
			if (m_seekRequest.load(std::memory_order_acquire))
			{
				const std::size_t target = m_seekTarget.load(std::memory_order_relaxed);
				m_seekRequest.store(false, std::memory_order_release);

				const std::lock_guard<std::mutex> lock(m_streamMutex);
				if (m_stream)
				{
					m_stream->seek(target);
				}
				m_ringBuffer->reset();
				m_streamFinished.store(false, std::memory_order_relaxed);
			}

			/// 一時停止中はスリープ
			if (m_state.load(std::memory_order_acquire) == StreamState::Paused)
			{
				std::this_thread::sleep_for(FILL_SLEEP_INTERVAL);
				continue;
			}

			/// リングバッファに空きがあればデータを充填
			if (m_ringBuffer->availableWrite() >= chunkSamples)
			{
				std::size_t framesRead = 0;
				{
					const std::lock_guard<std::mutex> lock(m_streamMutex);
					if (m_stream && !m_stream->isEof())
					{
						framesRead = m_stream->read(readBuffer.data(), FILL_CHUNK_FRAMES);
					}
				}

				if (framesRead > 0)
				{
					const std::size_t samplesRead = framesRead * channels;

					/// ボリューム適用
					const float vol = m_volume.load(std::memory_order_relaxed);
					if (vol < 1.0f)
					{
						for (std::size_t i = 0; i < samplesRead; ++i)
						{
							readBuffer[i] *= vol;
						}
					}

					m_ringBuffer->write(readBuffer.data(), samplesRead);
				}
				else
				{
					/// ストリーム終端に到達
					bool isEof = false;
					{
						const std::lock_guard<std::mutex> lock(m_streamMutex);
						isEof = !m_stream || m_stream->isEof();
					}

					if (isEof)
					{
						if (m_looping.load(std::memory_order_relaxed))
						{
							/// ループ再生: 先頭に巻き戻す
							const std::lock_guard<std::mutex> lock(m_streamMutex);
							if (m_stream)
							{
								m_stream->seek(0);
							}
						}
						else
						{
							m_streamFinished.store(true, std::memory_order_release);
							/// バッファが消費されるまで待機
							while (!m_stopRequested.load(std::memory_order_acquire) &&
								!m_ringBuffer->empty())
							{
								std::this_thread::sleep_for(FILL_SLEEP_INTERVAL);
							}
							break;
						}
					}
				}
			}
			else
			{
				/// バッファが十分に埋まっている場合はスリープ
				std::this_thread::sleep_for(FILL_SLEEP_INTERVAL);
			}
		}
	}

	std::unique_ptr<IAudioOutput> m_output;               ///< オーディオ出力バックエンド
	std::unique_ptr<IAudioStream> m_stream;                ///< オーディオストリーム
	std::mutex m_streamMutex;                              ///< ストリームアクセス用ミューテックス
	std::unique_ptr<RingBuffer<float>> m_ringBuffer;       ///< リングバッファ
	std::size_t m_ringBufferFrames;                        ///< リングバッファのフレーム数
	AudioFormat m_format;                                  ///< 現在のフォーマット情報
	std::thread m_fillThread;                              ///< バックグラウンド充填スレッド
	std::vector<float> m_outputBuffer;                     ///< 出力用一時バッファ

	std::atomic<StreamState> m_state{StreamState::Stopped}; ///< 再生状態
	std::atomic<float> m_volume{1.0f};                      ///< ボリューム
	std::atomic<bool> m_looping{false};                     ///< ループ再生フラグ
	std::atomic<bool> m_stopRequested{false};               ///< 停止要求フラグ
	std::atomic<bool> m_seekRequest{false};                 ///< シーク要求フラグ
	std::atomic<std::size_t> m_seekTarget{0};               ///< シーク先フレームオフセット
	std::atomic<bool> m_streamFinished{false};              ///< ストリーム終端到達フラグ
};

} // namespace mitiru::audio
