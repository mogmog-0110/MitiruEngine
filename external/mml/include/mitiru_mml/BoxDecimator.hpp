#pragma once
/// @file BoxDecimator.hpp
/// @brief 区間平均 (箱型フィルタ) で入力レートから出力レートへ落とす、流し込み式のデシメーター
/// @details 出力 i は入力の [floor(i*in/out), floor((i+1)*in/out)) の平均。境界は 64bit の整数で
///          求めるので、浮動小数点の丸めで 1 サンプルずれることがない。
///          入力を全部ためずに、塊ごとに流し込める (998 kHz のネイティブ出力を丸ごと保持しない)。

#include <mitiru_mml/MmlTypes.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mitiru_mml
{

/// @brief 入力と出力の区間の境界を整数で数える
class BoxBoundary
{
public:
	BoxBoundary(std::uint32_t inRate, std::uint32_t outRate) noexcept
		: m_inRate(inRate)
		, m_outRate(outRate)
	{
		m_nextEnd = endOf(0);
	}

	/// @brief 出力 index の区間の終端 (入力側の通し番号。この番号の手前まで)
	[[nodiscard]] std::uint64_t endOf(std::uint64_t index) const noexcept
	{
		std::uint64_t e = (index + 1) * m_inRate / m_outRate;
		const std::uint64_t s = index * m_inRate / m_outRate;
		if (e <= s)
		{
			e = s + 1; // 入力レートが出力レートより低い場合も 1 サンプルは使う
		}
		return e;
	}

	/// @brief これまでに入力した総数が N のとき、出力するべきサンプル数
	[[nodiscard]] std::uint64_t outputCountFor(std::uint64_t inputCount) const noexcept
	{
		return inputCount * m_outRate / m_inRate;
	}

	std::uint32_t m_inRate;
	std::uint32_t m_outRate;
	std::uint64_t m_position = 0;  ///< 入力した総数
	std::uint64_t m_index = 0;     ///< 次に出す出力の番号
	std::uint64_t m_nextEnd = 0;   ///< 次に出す出力の区間の終端
};

/// @brief 16bit モノラル用のデシメーター。従来の resample() と同じ値を返す
/// @details 平均は整数の和を個数で割って 0 方向へ切り捨てる。
class BoxDecimatorPcm16
{
public:
	BoxDecimatorPcm16(std::uint32_t inRate, std::uint32_t outRate) noexcept
		: m_b(inRate, outRate)
	{
	}

	/// @brief n 個の入力を流し込み、完成した出力を out の末尾へ足す
	void push(const std::int16_t* data, std::size_t n, PcmBuffer& out)
	{
		while (n > 0)
		{
			const std::uint64_t room = m_b.m_nextEnd - m_b.m_position;
			const std::size_t take = static_cast<std::size_t>(room < n ? room : n);
			std::int32_t sum = 0;
			for (std::size_t i = 0; i < take; ++i)
			{
				sum += data[i];
			}
			m_sum += sum;
			m_count += static_cast<std::int32_t>(take);
			m_b.m_position += take;
			data += take;
			n -= take;
			if (m_b.m_position == m_b.m_nextEnd)
			{
				out.push_back(static_cast<std::int16_t>(m_count ? m_sum / m_count : 0));
				m_sum = 0;
				m_count = 0;
				++m_b.m_index;
				m_b.m_nextEnd = m_b.endOf(m_b.m_index);
			}
		}
	}

	/// @brief 入力を打ち切る。区間が埋まっていても、入力の総数に対して多い出力は捨てる
	void finish(PcmBuffer& out) const
	{
		const std::uint64_t want = m_b.outputCountFor(m_b.m_position);
		if (out.size() > want)
		{
			out.resize(static_cast<std::size_t>(want));
		}
	}

private:
	BoxBoundary m_b;
	std::int32_t m_sum = 0;
	std::int32_t m_count = 0;
};

/// @brief float ステレオ用のデシメーター (interleaved L,R)
class BoxDecimatorStereo
{
public:
	BoxDecimatorStereo(std::uint32_t inRate, std::uint32_t outRate) noexcept
		: m_b(inRate, outRate)
	{
	}

	/// @brief frames フレームの入力 (interleaved) を流し込み、完成した出力を out の末尾へ足す
	void push(const float* data, std::size_t frames, std::vector<float>& out)
	{
		while (frames > 0)
		{
			const std::uint64_t room = m_b.m_nextEnd - m_b.m_position;
			const std::size_t take = static_cast<std::size_t>(room < frames ? room : frames);
			double l = 0.0;
			double r = 0.0;
			for (std::size_t i = 0; i < take; ++i)
			{
				l += static_cast<double>(data[i * 2]);
				r += static_cast<double>(data[i * 2 + 1]);
			}
			m_sumL += l;
			m_sumR += r;
			m_count += static_cast<std::int64_t>(take);
			m_b.m_position += take;
			data += take * 2;
			frames -= take;
			if (m_b.m_position == m_b.m_nextEnd)
			{
				const double inv = 1.0 / static_cast<double>(m_count);
				out.push_back(static_cast<float>(m_sumL * inv));
				out.push_back(static_cast<float>(m_sumR * inv));
				m_sumL = 0.0;
				m_sumR = 0.0;
				m_count = 0;
				++m_b.m_index;
				m_b.m_nextEnd = m_b.endOf(m_b.m_index);
			}
		}
	}

	/// @brief 入力を打ち切る。入力の総数に対して多い出力は捨てる
	void finish(std::vector<float>& out) const
	{
		const std::uint64_t want = m_b.outputCountFor(m_b.m_position) * 2;
		if (out.size() > want)
		{
			out.resize(static_cast<std::size_t>(want));
		}
	}

	/// @brief ここまでに出した出力のうち、あとで入力を打ち切っても finish() に捨てられない最大のフレーム数
	/// @details 出力は区間が埋まった時点で出るので、入力の総数に対して 1 フレーム多いことがある。
	///          ストリーミングはこの数までしか外へ渡さない。
	[[nodiscard]] std::uint64_t finalizedFrames() const noexcept
	{
		return m_b.outputCountFor(m_b.m_position);
	}

private:
	BoxBoundary m_b;
	double m_sumL = 0.0;
	double m_sumR = 0.0;
	std::int64_t m_count = 0;
};

} // namespace mitiru_mml
