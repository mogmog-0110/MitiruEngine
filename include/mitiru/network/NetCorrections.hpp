#pragma once

/// @file NetCorrections.hpp
/// @brief オンライン予測の修正前後の GameMemory を描画のために持つ
///
/// engine は GameMemory 内の位置を特定できないため、修正量を計算できない。
/// 修正前後の image を組で残し、ゲームの描画が両方から位置を読んで差を求める (NetSmoothing.hpp)。
/// 2 つは同じフレームの状態なので、差は前のフレームからの外挿値ではなく修正量そのものになる。
/// 組は PC ごとに異なるためシミュレーションには入れず、描画だけが読む。
/// 古い組は kMaxNetCorrections フレームで消える。

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::network
{

using module::kMaxNetCorrections;
using module::NetCorrectionsView;

/// @brief host が持つ。image の領域は reset で先に確保し、毎フレームは確保しない
class NetCorrectionRing
{
public:
	void reset(std::uint32_t memorySize)
	{
		m_size = memorySize;
		m_images.assign(static_cast<std::size_t>(memorySize) * 2u * kMaxNetCorrections, 0);
		m_count = 0;
		m_head = 0;
		m_view = NetCorrectionsView{};
		m_view.memorySize = memorySize;
	}

	/// @brief 描画フレームの最初に 1 回呼ぶ。組の経過フレーム数を 1 増やし、kMaxNetCorrections に達した組を捨てる
	void beginFrame() noexcept
	{
		int kept = 0;
		for (int i = 0; i < m_count; ++i)
		{
			if (++m_age[slotOf(i)] < kMaxNetCorrections) kept = i + 1;
		}
		m_count = kept;
		refreshView();
	}

	/// @return 組を残したときは true。2 つが同じなら、予測が外れていても状態は変わらなかったため残さない
	bool push(const void* before, const void* after) noexcept
	{
		if (m_size == 0 || std::memcmp(before, after, m_size) == 0) return false;
		m_head = (m_head + kMaxNetCorrections - 1) % kMaxNetCorrections;
		std::memcpy(imageOf(m_head, 0), before, m_size);
		std::memcpy(imageOf(m_head, 1), after, m_size);
		m_age[static_cast<std::size_t>(m_head)] = 0;
		if (m_count < kMaxNetCorrections) ++m_count;
		++m_view.serial;
		refreshView();
		return true;
	}

	/// @brief 補間をやめて状態を即座に切り替えたときに呼ぶ。切り替え後に以前の差を足すと、別の場面の位置がずれて見える
	void snap() noexcept
	{
		if (m_count > 0) ++m_view.snaps;
		m_count = 0;
		refreshView();
	}

	[[nodiscard]] const NetCorrectionsView& view() const noexcept { return m_view; }
	[[nodiscard]] std::uint32_t memorySize() const noexcept { return m_size; }

private:
	[[nodiscard]] std::size_t slotOf(int i) const noexcept
	{
		return static_cast<std::size_t>((m_head + i) % kMaxNetCorrections);
	}

	[[nodiscard]] std::uint8_t* imageOf(int slot, int which) noexcept
	{
		return m_images.data() + (static_cast<std::size_t>(slot) * 2u + static_cast<std::size_t>(which)) * m_size;
	}

	void refreshView() noexcept
	{
		m_view.count = static_cast<std::uint32_t>(m_count);
		for (int i = 0; i < m_count; ++i)
		{
			const std::size_t s = slotOf(i);
			m_view.ageFrames[i] = m_age[s];
			m_view.before[i] = imageOf(static_cast<int>(s), 0);
			m_view.after[i] = imageOf(static_cast<int>(s), 1);
		}
	}

	std::uint32_t m_size = 0;
	std::vector<std::uint8_t> m_images;
	std::uint16_t m_age[kMaxNetCorrections] = {};
	int m_count = 0;
	int m_head = 0;   ///< 一番新しい組の slot
	NetCorrectionsView m_view{};
};

} // namespace mitiru::network
