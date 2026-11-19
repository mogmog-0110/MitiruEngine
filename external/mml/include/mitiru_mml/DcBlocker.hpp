#pragma once
/// @file DcBlocker.hpp
/// @brief 直流成分を取り除く 1 次の高域通過フィルター (レトロな矩形波チップの Device 用)
/// @details SN76489、AY-3-8910、NES APU は矩形波が 0 以上の側だけに振れるので、音の出始めと止まりに
///          直流の段差が出る。実機では出力に結合コンデンサーが入っているので、同じ働きをする。

#include <array>
#include <cmath>
#include <cstdint>

namespace mitiru_mml
{

/// @brief ステレオ用の直流除去フィルター。y[n] = x[n] - x[n-1] + r * y[n-1]
class DcBlocker
{
public:
	/// @param rate このフィルターに入るサンプルのレート (Hz)
	/// @param cutoffHz 遮断周波数。可聴域の低音を削らないよう低くしてある
	explicit DcBlocker(std::uint32_t rate, double cutoffHz = 10.0)
		: m_coefficient(static_cast<float>(1.0 - 2.0 * 3.14159265358979323846 * cutoffHz / static_cast<double>(rate)))
	{
	}

	void reset() noexcept
	{
		m_x1.fill(0.0f);
		m_y1.fill(0.0f);
	}

	/// @brief 1 サンプル通す (channel は 0 か 1)
	[[nodiscard]] float process(std::size_t channel, float x) noexcept
	{
		float y = x - m_x1[channel] + m_coefficient * m_y1[channel];
		// 無音が続いて値が非正規化数になると演算が遅くなるので、十分小さければ 0 にする
		if (std::abs(y) < 1.0e-18f)
		{
			y = 0.0f;
		}
		m_x1[channel] = x;
		m_y1[channel] = y;
		return y;
	}

private:
	float m_coefficient;
	std::array<float, 2> m_x1{};
	std::array<float, 2> m_y1{};
};

} // namespace mitiru_mml
