#pragma once

/// @file DirtyRows.hpp
/// @brief アトラス内で GPU に未送信の行範囲。

#include <algorithm>

namespace mitiru::text
{

class DirtyRows
{
public:
	void mark(int firstRow, int rowCount) noexcept
	{
		if (m_end <= m_begin)
		{
			m_begin = firstRow;
			m_end = firstRow + rowCount;
			return;
		}
		m_begin = std::min(m_begin, firstRow);
		m_end = std::max(m_end, firstRow + rowCount);
	}

	/// @brief 未送信の行範囲を返す。範囲がなければ false を返す。
	[[nodiscard]] bool get(int& firstRow, int& rowCount) const noexcept
	{
		if (m_end <= m_begin) { return false; }
		firstRow = m_begin;
		rowCount = m_end - m_begin;
		return true;
	}

	void clear() noexcept { m_begin = m_end = 0; }

private:
	int m_begin = 0;
	int m_end = 0;
};

} // namespace mitiru::text
