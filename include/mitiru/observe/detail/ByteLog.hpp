#pragma once

/// @file ByteLog.hpp
/// @brief 巻き戻しのリングが差分を詰める byte の置き場。広げるときに 0 で埋めない
/// @details リングは書いた範囲 (スロットの offset と長さ) しか読まないので、0 埋めは要らない。
///          std::vector<std::uint8_t> は広げるたびに新しい領域を全部 0 で埋め、置き場が数 MB になると
///          その 1 フレームだけ数 ms 重くなる。

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace mitiru::observe::detail
{

class ByteLog
{
public:
	ByteLog() = default;
	/// @brief 中身は決めない (書く前に読まないこと)
	explicit ByteLog(std::size_t size) : m_data(size > 0 ? new std::uint8_t[size] : nullptr), m_size(size) {}

	[[nodiscard]] std::size_t size() const noexcept { return m_size; }
	[[nodiscard]] bool empty() const noexcept { return m_size == 0; }
	[[nodiscard]] std::uint8_t* data() noexcept { return m_data.get(); }
	[[nodiscard]] const std::uint8_t* data() const noexcept { return m_data.get(); }
	[[nodiscard]] std::uint8_t& operator[](std::size_t i) noexcept { return m_data[i]; }
	[[nodiscard]] const std::uint8_t& operator[](std::size_t i) const noexcept { return m_data[i]; }

	void clear() noexcept
	{
		m_data.reset();
		m_size = 0;
	}

	void swap(ByteLog& other) noexcept
	{
		std::swap(m_data, other.m_data);
		std::swap(m_size, other.m_size);
	}

private:
	std::unique_ptr<std::uint8_t[]> m_data;
	std::size_t m_size = 0;
};

} // namespace mitiru::observe::detail
