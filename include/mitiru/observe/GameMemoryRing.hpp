#pragma once

/// @file GameMemoryRing.hpp
/// @brief host 所有の固定容量 GameMemory bytes リングバッファ (軸② rewind の基盤)
/// @details
/// 旧 `TimeTravelRecorder<Snapshot>` の後継。作者が手で Snapshot 型を定義する方式を捨て、
/// host が GameMemory の **生バイト列** を毎フレーム push する。後で `at(offset)` で
/// "n フレーム前の GameMemory bytes" を読み戻す。これが rewind inspector の観測
/// (probe で系列化) と rewind (live GameMemory へ memcpy で巻き戻し) の **単一源**
/// (replay の state slot と同一 bytes)。
///
/// 設計判断:
/// - host プロセスが所有する。game DLL は ring を持たない。
/// - frameSize = GameMemory の固定バイト数 (ModuleApi.memorySize)。`configure` で確定し、
///   contiguous な単一 buffer (capacity*frameSize) を 1 度だけ確保する (hot path で
///   alloc しない)。GameMemory が flat POD だから bytes 一致で復元できる。
/// - "0 offset = newest" にして HTML scrubber UI (右端 = 現在) と直結。
/// - frameSize が変わる push (hot reload で sizeof 変動) は無視する。reload 側が
///   `clear()` してから新 frameSize で `configure` し直す。
/// - 例外を投げない: 不正引数は no-op、範囲外 `at` は nullptr。

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mitiru::observe
{

/// @brief 固定容量の GameMemory bytes リング。軸② rewind 基盤
class GameMemoryRing
{
public:
	/// @brief frameSize 確定後 (memorySize>0) に呼ぶ。形が同じなら no-op。
	/// @param frameSize GameMemory のバイト数 (= ModuleApi.memorySize)
	/// @param capacity  保持する最大フレーム数 (既定 300 = 60fps × 5sec)
	void configure(std::uint32_t frameSize, std::size_t capacity = 300)
	{
		if (frameSize == 0 || capacity == 0)
		{
			m_buf.clear();
			m_frameSize = 0;
			m_cap = m_count = m_head = 0;
			return;
		}
		if (frameSize == m_frameSize && capacity == m_cap) { return; }  // 既に同形
		m_frameSize = frameSize;
		m_cap       = capacity;
		m_buf.assign(static_cast<std::size_t>(frameSize) * capacity, std::uint8_t{0});
		m_count = m_head = 0;
	}

	/// @brief 1 フレーム分の GameMemory bytes を最新として記録 (容量超過で最古を破棄)
	/// @details size != frameSize (reload で layout 変動) や未 configure は no-op。
	void push(const void* mem, std::uint32_t size)
	{
		if (mem == nullptr || size == 0 || size != m_frameSize || m_cap == 0) { return; }
		std::uint8_t* slot = &m_buf[m_head * m_frameSize];
		std::memcpy(slot, mem, m_frameSize);
		m_head = (m_head + 1) % m_cap;
		if (m_count < m_cap) { ++m_count; }
	}

	/// @brief 現フレームを基準に N フレーム前の GameMemory bytes を取得
	/// @param offsetFromNewest 0 = newest, 1 = 1 frame ago, ...
	/// @return 範囲内なら frameSize バイトの先頭、範囲外なら nullptr
	/// @note 返り値は次の push / configure / clear まで valid
	[[nodiscard]] const std::uint8_t* at(std::size_t offsetFromNewest) const noexcept
	{
		if (offsetFromNewest >= m_count) { return nullptr; }
		// newest は m_head-1。そこから offset 分だけ巻き戻す。
		const std::size_t idx = (m_head + m_cap - 1 - offsetFromNewest) % m_cap;
		return &m_buf[idx * m_frameSize];
	}

	[[nodiscard]] std::size_t   size() const noexcept { return m_count; }
	[[nodiscard]] std::size_t   capacity() const noexcept { return m_cap; }
	[[nodiscard]] std::uint32_t frameSize() const noexcept { return m_frameSize; }
	[[nodiscard]] bool          empty() const noexcept { return m_count == 0; }

	/// @brief 全フレーム破棄 (rewind 確定 / reload で layout が無効化される時に呼ぶ)
	void clear() noexcept { m_count = m_head = 0; }

private:
	std::vector<std::uint8_t> m_buf;          ///< capacity*frameSize の contiguous ring
	std::size_t               m_cap   = 0;    ///< 最大フレーム数
	std::size_t               m_count = 0;    ///< 現在の保持フレーム数
	std::size_t               m_head  = 0;    ///< 次に書く slot index
	std::uint32_t             m_frameSize = 0;
};

}  // namespace mitiru::observe
