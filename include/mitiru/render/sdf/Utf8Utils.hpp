#pragma once

/// @file Utf8Utils.hpp
/// @brief SDF用UTF-8デコーダおよびユーティリティ関数

#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace mitiru::render
{

namespace sdf_detail
{

/// @brief UTF-8文字列からコードポイントを順次取得する
class Utf8Decoder
{
	const char* m_ptr = nullptr;
	const char* m_end = nullptr;

public:
	Utf8Decoder(const char* str, std::size_t len) noexcept
		: m_ptr(str)
		, m_end(str + len)
	{
	}

	explicit Utf8Decoder(std::string_view sv) noexcept
		: m_ptr(sv.data())
		, m_end(sv.data() + sv.size())
	{
	}

	[[nodiscard]] bool hasNext() const noexcept
	{
		return m_ptr < m_end;
	}

	[[nodiscard]] std::uint32_t next() noexcept
	{
		if (m_ptr >= m_end)
		{
			return 0;
		}

		const auto b0 = static_cast<std::uint8_t>(*m_ptr);

		if (b0 < 0x80)
		{
			++m_ptr;
			return b0;
		}

		if ((b0 & 0xE0) == 0xC0)
		{
			if (m_ptr + 1 >= m_end) { m_ptr = m_end; return 0xFFFD; }
			const auto b1 = static_cast<std::uint8_t>(m_ptr[1]);
			if ((b1 & 0xC0) != 0x80) { ++m_ptr; return 0xFFFD; }
			m_ptr += 2;
			const std::uint32_t cp = (static_cast<std::uint32_t>(b0 & 0x1F) << 6)
				| static_cast<std::uint32_t>(b1 & 0x3F);
			return (cp >= 0x80) ? cp : 0xFFFD;
		}

		if ((b0 & 0xF0) == 0xE0)
		{
			if (m_ptr + 2 >= m_end) { m_ptr = m_end; return 0xFFFD; }
			const auto b1 = static_cast<std::uint8_t>(m_ptr[1]);
			const auto b2 = static_cast<std::uint8_t>(m_ptr[2]);
			if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) { ++m_ptr; return 0xFFFD; }
			m_ptr += 3;
			const std::uint32_t cp = (static_cast<std::uint32_t>(b0 & 0x0F) << 12)
				| (static_cast<std::uint32_t>(b1 & 0x3F) << 6)
				| static_cast<std::uint32_t>(b2 & 0x3F);
			return (cp >= 0x800) ? cp : 0xFFFD;
		}

		if ((b0 & 0xF8) == 0xF0)
		{
			if (m_ptr + 3 >= m_end) { m_ptr = m_end; return 0xFFFD; }
			const auto b1 = static_cast<std::uint8_t>(m_ptr[1]);
			const auto b2 = static_cast<std::uint8_t>(m_ptr[2]);
			const auto b3 = static_cast<std::uint8_t>(m_ptr[3]);
			if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80)
			{
				++m_ptr;
				return 0xFFFD;
			}
			m_ptr += 4;
			const std::uint32_t cp = (static_cast<std::uint32_t>(b0 & 0x07) << 18)
				| (static_cast<std::uint32_t>(b1 & 0x3F) << 12)
				| (static_cast<std::uint32_t>(b2 & 0x3F) << 6)
				| static_cast<std::uint32_t>(b3 & 0x3F);
			return (cp >= 0x10000 && cp <= 0x10FFFF) ? cp : 0xFFFD;
		}

		++m_ptr;
		return 0xFFFD;
	}
};

/// @brief UTF-8文字列をコードポイント配列に変換する
[[nodiscard]] inline std::vector<std::uint32_t> toCodepoints(std::string_view sv)
{
	std::vector<std::uint32_t> result;
	result.reserve(sv.size());
	Utf8Decoder dec(sv);
	while (dec.hasNext())
	{
		result.push_back(dec.next());
	}
	return result;
}

/// @brief 2のべき乗に切り上げる（最小64）
[[nodiscard]] inline int nextPow2(int v) noexcept
{
	int r = 64;
	while (r < v)
	{
		r *= 2;
	}
	return r;
}

/// @brief smoothstep関数（SDF描画の基礎）
[[nodiscard]] inline float smoothstep(float edge0, float edge1, float x) noexcept
{
	const float t = std::clamp((x - edge0) / (edge1 - edge0 + 1e-8f), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

} // namespace sdf_detail

} // namespace mitiru::render
