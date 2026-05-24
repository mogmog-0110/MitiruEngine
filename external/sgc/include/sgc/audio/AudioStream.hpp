#pragma once

/// @file AudioStream.hpp
/// @brief ストリーミングオーディオサポート
///
/// リングバッファによるダブルバッファリング戦略でオーディオストリームを管理する。
/// IStreamSourceインターフェースを実装することで任意のデコーダーと統合可能。
///
/// @code
/// class WavStream : public sgc::audio::IStreamSource { /* ... */ };
/// sgc::audio::AudioStreamPlayer player(std::make_unique<WavStream>("bgm.wav"), 4096);
/// player.play();
/// while (player.state() == sgc::audio::StreamState::Playing)
/// {
///     player.update(); // バッファ補充
///     auto buf = player.frontBuffer();
///     // buf.data(), buf.size() をオーディオデバイスに送信
/// }
/// @endcode

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace sgc::audio
{

/// @brief ストリームソースインターフェース
///
/// PCMデータの読み込みとシークを抽象化する。
/// WAV/OGG/MP3等のデコーダーがこれを実装する。
class IStreamSource
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IStreamSource() = default;

	/// @brief PCMフレームを読み込む
	/// @param buffer 出力バッファ（フレーム数分確保済み）
	/// @param frames 読み込みたいフレーム数
	/// @return 実際に読み込んだフレーム数
	virtual std::size_t read(float* buffer, std::size_t frames) = 0;

	/// @brief 指定フレームへシークする
	/// @param frame シーク先フレーム位置
	/// @return シークできたらtrue
	virtual bool seek(std::size_t frame) = 0;

	/// @brief 総フレーム数を返す
	/// @return 総フレーム数（不明の場合は0）
	[[nodiscard]] virtual std::size_t totalFrames() const = 0;

	/// @brief 終端に達したかを返す
	/// @return 終端ならtrue
	[[nodiscard]] virtual bool isEnd() const = 0;
};

/// @brief ストリーム状態
enum class StreamState
{
	Stopped,    ///< 停止中
	Playing,    ///< 再生中
	Paused,     ///< 一時停止中
	Buffering   ///< バッファ充填中
};

/// @brief ループポイント設定
struct LoopPoints
{
	std::size_t startFrame = 0;   ///< ループ開始フレーム
	std::size_t endFrame = 0;     ///< ループ終了フレーム（0 = 末尾）
	bool enabled = false;         ///< ループ有効フラグ
};

/// @brief ストリーミングオーディオプレイヤー
///
/// ダブルバッファリング方式で音声をストリーミング再生する。
/// update()で裏バッファにデータを補充し、swapBuffers()で表裏を交換する。
class AudioStreamPlayer
{
public:
	/// @brief コンストラクタ
	/// @param source ストリームソース（所有権を移動）
	/// @param bufferSize バッファサイズ（フレーム数、デフォルト4096）
	explicit AudioStreamPlayer(
		std::unique_ptr<IStreamSource> source,
		std::size_t bufferSize = 4096)
		: m_source(std::move(source))
		, m_bufferSize(bufferSize > 0 ? bufferSize : 4096)
	{
		m_frontBuffer.resize(m_bufferSize, 0.0f);
		m_backBuffer.resize(m_bufferSize, 0.0f);
	}

	/// @brief 再生を開始する
	void play()
	{
		if (!m_source) return;
		if (m_state == StreamState::Paused)
		{
			m_state = StreamState::Playing;
			return;
		}
		m_source->seek(0);
		m_currentFrame = 0;
		m_state = StreamState::Playing;
		fillBuffer(m_frontBuffer);
		fillBuffer(m_backBuffer);
	}

	/// @brief 再生を停止する
	void stop()
	{
		m_state = StreamState::Stopped;
		m_currentFrame = 0;
		if (m_source)
		{
			m_source->seek(0);
		}
	}

	/// @brief 一時停止する
	void pause()
	{
		if (m_state == StreamState::Playing)
		{
			m_state = StreamState::Paused;
		}
	}

	/// @brief 一時停止を解除する
	void resume()
	{
		if (m_state == StreamState::Paused)
		{
			m_state = StreamState::Playing;
		}
	}

	/// @brief 指定フレームへシークする
	/// @param frame シーク先フレーム
	/// @return シークできたらtrue
	bool seek(std::size_t frame)
	{
		if (!m_source) return false;
		if (!m_source->seek(frame)) return false;
		m_currentFrame = frame;
		/// シーク後にバッファを再充填
		fillBuffer(m_frontBuffer);
		fillBuffer(m_backBuffer);
		return true;
	}

	/// @brief バッファを更新する（毎フレーム呼び出し）
	///
	/// 裏バッファにデータを補充し、ループ処理を行う。
	void update()
	{
		if (m_state != StreamState::Playing) return;
		if (!m_source) return;

		/// 裏バッファを補充
		const std::size_t filled = fillBuffer(m_backBuffer);

		if (filled == 0 && m_source->isEnd())
		{
			if (m_loopPoints.enabled)
			{
				const std::size_t loopStart = m_loopPoints.startFrame;
				m_source->seek(loopStart);
				m_currentFrame = loopStart;
				fillBuffer(m_backBuffer);
			}
			else
			{
				m_state = StreamState::Stopped;
			}
		}
	}

	/// @brief 表裏バッファを交換する
	void swapBuffers()
	{
		std::swap(m_frontBuffer, m_backBuffer);
	}

	/// @brief 表バッファ（読み取り用）を返す
	/// @return 表バッファへのconst参照
	[[nodiscard]] const std::vector<float>& frontBuffer() const noexcept
	{
		return m_frontBuffer;
	}

	/// @brief 現在の再生状態を返す
	[[nodiscard]] StreamState state() const noexcept
	{
		return m_state;
	}

	/// @brief 現在の再生フレーム位置を返す
	[[nodiscard]] std::size_t currentFrame() const noexcept
	{
		return m_currentFrame;
	}

	/// @brief 総フレーム数を返す
	[[nodiscard]] std::size_t totalFrames() const noexcept
	{
		return m_source ? m_source->totalFrames() : 0;
	}

	/// @brief バッファサイズ（フレーム数）を返す
	[[nodiscard]] std::size_t bufferSize() const noexcept
	{
		return m_bufferSize;
	}

	/// @brief ループポイントを設定する
	/// @param points ループ設定
	void setLoopPoints(const LoopPoints& points)
	{
		m_loopPoints = points;
	}

	/// @brief ループポイントを取得する
	[[nodiscard]] const LoopPoints& loopPoints() const noexcept
	{
		return m_loopPoints;
	}

	/// @brief ストリームソースが有効かを返す
	[[nodiscard]] bool hasSource() const noexcept
	{
		return m_source != nullptr;
	}

private:
	/// @brief バッファにデータを充填する
	/// @param buffer 充填先バッファ
	/// @return 充填したフレーム数
	std::size_t fillBuffer(std::vector<float>& buffer)
	{
		if (!m_source) return 0;

		std::size_t framesNeeded = m_bufferSize;
		std::size_t totalFilled = 0;

		/// ループ終了フレームを考慮
		if (m_loopPoints.enabled && m_loopPoints.endFrame > 0)
		{
			const std::size_t remaining = (m_currentFrame < m_loopPoints.endFrame)
				? (m_loopPoints.endFrame - m_currentFrame)
				: 0;
			framesNeeded = std::min(framesNeeded, remaining);
		}

		if (framesNeeded > 0)
		{
			totalFilled = m_source->read(buffer.data(), framesNeeded);
			m_currentFrame += totalFilled;
		}

		/// 未充填部分をゼロ埋め
		std::fill(buffer.begin() + static_cast<std::ptrdiff_t>(totalFilled), buffer.end(), 0.0f);

		return totalFilled;
	}

	std::unique_ptr<IStreamSource> m_source;   ///< ストリームソース
	std::size_t m_bufferSize;                  ///< バッファサイズ（フレーム数）
	std::vector<float> m_frontBuffer;          ///< 表バッファ
	std::vector<float> m_backBuffer;           ///< 裏バッファ
	StreamState m_state = StreamState::Stopped; ///< 再生状態
	std::size_t m_currentFrame = 0;            ///< 現在の再生位置
	LoopPoints m_loopPoints{};                 ///< ループポイント設定
};

} // namespace sgc::audio
