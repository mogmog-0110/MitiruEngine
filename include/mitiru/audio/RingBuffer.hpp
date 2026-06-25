#pragma once

/// @file RingBuffer.hpp
/// @brief ロックフリーリングバッファ（オーディオストリーミング用）
/// @details プロデューサー（ファイル読み込みスレッド）とコンシューマー（オーディオコールバック）間で
///          PCMサンプルデータを安全に受け渡すためのSPSC（Single Producer Single Consumer）
///          ロックフリーリングバッファ。std::atomicによるメモリオーダリングで同期する。

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>

namespace mitiru::audio
{

/// @brief SPSC ロックフリーリングバッファ
/// @tparam T 要素型（通常 float または int16_t）
/// @details プロデューサーとコンシューマーが別スレッドで動作する場合に
///          ロックなしで安全にデータを受け渡す。容量は2の累乗に切り上げられ、
///          ビットマスクによる高速なインデックス計算を行う。
///
/// @code
/// mitiru::audio::RingBuffer<float> ring(8192);
/// // プロデューサースレッド
/// std::vector<float> chunk(1024);
/// ring.write(chunk.data(), chunk.size());
/// // コンシューマースレッド
/// std::vector<float> out(512);
/// auto read = ring.read(out.data(), out.size());
/// @endcode
template <typename T>
class RingBuffer
{
	static_assert(std::is_trivially_copyable_v<T>,
		"RingBuffer requires trivially copyable types");

public:
	/// @brief コンストラクタ
	/// @param capacity 最低限必要な要素数（内部で2の累乗に切り上げ）
	explicit RingBuffer(std::size_t capacity)
		: m_capacity(nextPowerOf2(capacity))
		, m_mask(m_capacity - 1)
		, m_buffer(std::make_unique<T[]>(m_capacity))
	{
	}

	/// @brief コピー禁止
	RingBuffer(const RingBuffer&) = delete;
	/// @brief コピー代入禁止
	RingBuffer& operator=(const RingBuffer&) = delete;
	/// @brief ムーブコンストラクタ
	RingBuffer(RingBuffer&&) noexcept = default;
	/// @brief ムーブ代入演算子
	RingBuffer& operator=(RingBuffer&&) noexcept = default;

	/// @brief デストラクタ
	~RingBuffer() = default;

	/// @brief バッファにデータを書き込む（プロデューサー側）
	/// @param data 書き込むデータの先頭ポインタ
	/// @param count 書き込む要素数
	/// @return 実際に書き込んだ要素数
	std::size_t write(const T* data, std::size_t count) noexcept
	{
		const std::size_t writePos = m_writePos.load(std::memory_order_relaxed);
		const std::size_t readPos = m_readPos.load(std::memory_order_acquire);
		const std::size_t available = m_capacity - (writePos - readPos);
		const std::size_t toWrite = std::min(count, available);

		if (toWrite == 0)
		{
			return 0;
		}

		const std::size_t writeIdx = writePos & m_mask;
		const std::size_t firstChunk = std::min(toWrite, m_capacity - writeIdx);
		const std::size_t secondChunk = toWrite - firstChunk;

		std::memcpy(&m_buffer[writeIdx], data, firstChunk * sizeof(T));
		if (secondChunk > 0)
		{
			std::memcpy(&m_buffer[0], data + firstChunk, secondChunk * sizeof(T));
		}

		m_writePos.store(writePos + toWrite, std::memory_order_release);
		return toWrite;
	}

	/// @brief バッファからデータを読み出す（コンシューマー側）
	/// @param data 読み出し先バッファの先頭ポインタ
	/// @param count 読み出す要素数
	/// @return 実際に読み出した要素数
	std::size_t read(T* data, std::size_t count) noexcept
	{
		const std::size_t readPos = m_readPos.load(std::memory_order_relaxed);
		const std::size_t writePos = m_writePos.load(std::memory_order_acquire);
		const std::size_t available = writePos - readPos;
		const std::size_t toRead = std::min(count, available);

		if (toRead == 0)
		{
			return 0;
		}

		const std::size_t readIdx = readPos & m_mask;
		const std::size_t firstChunk = std::min(toRead, m_capacity - readIdx);
		const std::size_t secondChunk = toRead - firstChunk;

		std::memcpy(data, &m_buffer[readIdx], firstChunk * sizeof(T));
		if (secondChunk > 0)
		{
			std::memcpy(data + firstChunk, &m_buffer[0], secondChunk * sizeof(T));
		}

		m_readPos.store(readPos + toRead, std::memory_order_release);
		return toRead;
	}

	/// @brief 読み出し可能な要素数を取得する
	/// @return 読み出し可能な要素数
	[[nodiscard]] std::size_t availableRead() const noexcept
	{
		const std::size_t writePos = m_writePos.load(std::memory_order_acquire);
		const std::size_t readPos = m_readPos.load(std::memory_order_relaxed);
		return writePos - readPos;
	}

	/// @brief 書き込み可能な要素数を取得する
	/// @return 書き込み可能な要素数
	[[nodiscard]] std::size_t availableWrite() const noexcept
	{
		const std::size_t writePos = m_writePos.load(std::memory_order_relaxed);
		const std::size_t readPos = m_readPos.load(std::memory_order_acquire);
		return m_capacity - (writePos - readPos);
	}

	/// @brief バッファの総容量を取得する
	/// @return 総容量（要素数）
	[[nodiscard]] std::size_t capacity() const noexcept
	{
		return m_capacity;
	}

	/// @brief バッファが空かどうかを判定する
	/// @return 空なら true
	[[nodiscard]] bool empty() const noexcept
	{
		return availableRead() == 0;
	}

	/// @brief バッファが満杯かどうかを判定する
	/// @return 満杯なら true
	[[nodiscard]] bool full() const noexcept
	{
		return availableWrite() == 0;
	}

	/// @brief バッファをリセットする（両スレッド停止時のみ安全）
	void reset() noexcept
	{
		m_readPos.store(0, std::memory_order_relaxed);
		m_writePos.store(0, std::memory_order_relaxed);
	}

private:
	/// @brief 2の累乗に切り上げる
	/// @param n 入力値
	/// @return n以上の最小の2の累乗
	[[nodiscard]] static constexpr std::size_t nextPowerOf2(std::size_t n) noexcept
	{
		if (n == 0)
		{
			return 1;
		}
		--n;
		n |= n >> 1;
		n |= n >> 2;
		n |= n >> 4;
		n |= n >> 8;
		n |= n >> 16;
		n |= n >> 32;
		return n + 1;
	}

	std::size_t m_capacity;                           ///< バッファ容量（2の累乗、構築後不変）
	std::size_t m_mask;                               ///< インデックスマスク (capacity - 1、不変)
	std::unique_ptr<T[]> m_buffer;                    ///< データバッファ（不変ポインタ）

	/// 各カーソルを別キャッシュラインへ（producer/consumer の false sharing 回避）。
	alignas(64) std::atomic<std::size_t> m_readPos{0};   ///< 読み出し位置（consumer が書く）
	alignas(64) std::atomic<std::size_t> m_writePos{0};  ///< 書き込み位置（producer が書く）
};

} // namespace mitiru::audio
