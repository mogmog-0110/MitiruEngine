#pragma once

/// @file WireBytes.hpp
/// @brief 回線に流す数を little endian の決まった byte 数で書き・読む (待合室と host 権威の packet)
///
/// 読む側は相手から来た byte 列を信じない。足りない所を読もうとしたら ok を false にし、以後は 0 を返す。

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mitiru::network
{

inline void putLe(std::vector<std::uint8_t>& out, std::uint64_t v, int n)
{
	for (int i = 0; i < n; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

struct WireWriter
{
	std::vector<std::uint8_t> bytes;

	WireWriter& put(std::uint64_t v, int n)
	{
		putLe(bytes, v, n);
		return *this;
	}
};

struct WireReader
{
	const std::uint8_t* data = nullptr;
	std::size_t size = 0;
	std::size_t at = 0;
	bool ok = true;

	WireReader(const std::uint8_t* d, std::size_t n) noexcept : data(d), size(n) {}
	explicit WireReader(const std::vector<std::uint8_t>& v) noexcept : data(v.data()), size(v.size()) {}

	std::uint64_t get(int n) noexcept
	{
		if (!ok || at + static_cast<std::size_t>(n) > size)
		{
			ok = false;
			return 0;
		}
		std::uint64_t v = 0;
		for (int i = 0; i < n; ++i) v |= static_cast<std::uint64_t>(data[at + static_cast<std::size_t>(i)]) << (8 * i);
		at += static_cast<std::size_t>(n);
		return v;
	}

	/// @brief n byte をそのまま取る。足りなければ null
	const std::uint8_t* take(std::size_t n) noexcept
	{
		if (!ok || n > size - at)
		{
			ok = false;
			return nullptr;
		}
		const std::uint8_t* p = data + at;
		at += n;
		return p;
	}

	[[nodiscard]] std::size_t remaining() const noexcept { return ok ? size - at : 0; }
};

} // namespace mitiru::network
