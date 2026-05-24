#pragma once

/// @file InputBuffer.hpp
/// @brief 入力バッファ — コマンド入力検出
///
/// 一定時間内のアクション入力履歴を記録し、
/// シーケンス（格ゲーコマンド等）のマッチングを行う。
/// 内部でRingBufferを使用する。
///
/// @code
/// sgc::InputBuffer<30> buf;
/// buf.record(actionId, timestamp);
/// // ...
/// std::array<sgc::ActionId, 3> combo = {down, downForward, forward};
/// if (buf.matchSequence(combo, 0.5f)) { fireHadouken(); }
/// @endcode

#include <cstddef>
#include <span>

#include "sgc/core/RingBuffer.hpp"

namespace sgc
{

/// @brief 入力フレーム（アクションIDとタイムスタンプのペア）
struct InputFrame
{
	std::uint64_t actionId{0};  ///< アクションID
	float timestamp{0.0f};      ///< 入力時刻（秒）
};

/// @brief 入力バッファ — 直近N件の入力を記録する
/// @tparam N バッファサイズ（フレーム数）
template <std::size_t N = 60>
class InputBuffer
{
public:
	/// @brief 入力を記録する
	/// @param actionId アクションID
	/// @param timestamp 現在時刻（秒）
	void record(std::uint64_t actionId, float timestamp)
	{
		m_buffer.pushBack(InputFrame{actionId, timestamp});
	}

	/// @brief シーケンスが最近の入力に一致するか判定する
	///
	/// 入力バッファ内で、指定されたアクションIDが順番に
	/// maxInterval以内の間隔で記録されているか確認する。
	///
	/// @param sequence 検出するアクションIDの並び
	/// @param maxInterval 各入力間の最大許容間隔（秒）
	/// @return シーケンスが一致すればtrue
	[[nodiscard]] bool matchSequence(
		std::span<const std::uint64_t> sequence,
		float maxInterval) const
	{
		if (sequence.empty()) return true;
		if (m_buffer.empty()) return false;

		// バッファ末尾から逆順にシーケンスを探す
		std::size_t seqIdx = sequence.size();
		float lastTimestamp = 0.0f;
		bool foundFirst = false;

		// バッファを末尾から走査
		for (std::size_t i = m_buffer.size(); i > 0; --i)
		{
			const auto& frame = m_buffer[i - 1];

			if (seqIdx == 0) break;

			if (frame.actionId == sequence[seqIdx - 1])
			{
				if (foundFirst)
				{
					// 前の入力との間隔チェック
					if (lastTimestamp - frame.timestamp > maxInterval)
					{
						return false;
					}
				}
				lastTimestamp = frame.timestamp;
				foundFirst = true;
				--seqIdx;
			}
		}

		return seqIdx == 0;
	}

	/// @brief バッファをクリアする
	void clear()
	{
		m_buffer.clear();
	}

	/// @brief バッファ内の入力数
	/// @return 入力数
	[[nodiscard]] std::size_t size() const noexcept
	{
		return m_buffer.size();
	}

private:
	RingBuffer<InputFrame, N> m_buffer;  ///< 入力リングバッファ
};

} // namespace sgc
