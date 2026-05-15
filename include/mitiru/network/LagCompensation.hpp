#pragma once

/// @file LagCompensation.hpp
/// @brief ラグ補償システム
/// @details マルチプレイヤーゲームにおけるサーバーサイドのラグ補償を実装する。
///          過去の状態履歴をリングバッファで保持し、クライアントの遅延に基づいて
///          ワールド状態を過去にリワインドする機能を提供する。
///          サーバーサイドのヒット判定やアクション検証に使用する。
///
/// @code
/// mitiru::network::LagCompensator<WorldState> comp;
/// comp.recordState(currentTime, worldState);
///
/// // クライアントの入力タイムスタンプに基づいてリワインド
/// auto pastState = comp.rewindTo(clientTimestamp);
/// if (pastState) { /* 過去の状態でヒット判定 */ }
///
/// // 補間も可能
/// auto interp = comp.interpolate(t1, t2, 0.5);
/// @endcode

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace mitiru::network
{

/// @brief タイムスタンプ型（ミリ秒）
using Timestamp = std::uint64_t;

/// @brief タイムスタンプ付き状態スナップショット
/// @tparam State 状態型
template <typename State>
struct TimestampedState
{
	Timestamp time = 0;  ///< タイムスタンプ（ミリ秒）
	State state{};       ///< 状態データ
};

/// @brief ラグ補償マネージャー
/// @tparam State 状態型（コピー構築可能であること）
/// @details リングバッファで過去N個の状態を保持し、
///          任意のタイムスタンプにリワインドまたは補間できる。
template <typename State>
class LagCompensator
{
public:
	/// @brief デフォルトの状態補間関数型
	using LerpFunc = std::function<State(const State&, const State&, float)>;

	/// @brief コンストラクタ
	/// @param capacity リングバッファの最大サイズ（デフォルト: 120 = 60fps×2秒）
	explicit LagCompensator(std::size_t capacity = kDefaultCapacity)
		: m_capacity(capacity)
	{
		m_buffer.reserve(capacity);
	}

	/// @brief 補間関数を設定する
	/// @param func 線形補間関数 f(stateA, stateB, alpha) → State
	void setLerpFunction(LerpFunc func)
	{
		m_lerpFunc = std::move(func);
	}

	/// @brief 状態を記録する
	/// @param time タイムスタンプ（ミリ秒）
	/// @param state 記録する状態
	/// @details タイムスタンプは単調増加であること。
	///          バッファが満杯の場合、最古のエントリを上書きする。
	void recordState(Timestamp time, State state)
	{
		TimestampedState<State> entry{time, std::move(state)};

		if (m_buffer.size() < m_capacity)
		{
			m_buffer.push_back(std::move(entry));
		}
		else
		{
			m_buffer[m_writeIndex] = std::move(entry);
		}

		m_writeIndex = (m_writeIndex + 1) % m_capacity;
	}

	/// @brief 指定タイムスタンプの状態を取得する（最も近い過去の状態）
	/// @param time 対象タイムスタンプ
	/// @return 見つかった状態（履歴が空なら nullopt）
	[[nodiscard]] std::optional<State> rewindTo(Timestamp time) const
	{
		if (m_buffer.empty())
		{
			return std::nullopt;
		}

		/// 最も近い過去の状態を探す
		const TimestampedState<State>* best = nullptr;

		for (const auto& entry : m_buffer)
		{
			if (entry.time <= time)
			{
				if (best == nullptr || entry.time > best->time)
				{
					best = &entry;
				}
			}
		}

		/// 過去の状態が見つからない場合、最古の状態を返す
		if (best == nullptr)
		{
			best = &m_buffer[0];
			for (const auto& entry : m_buffer)
			{
				if (entry.time < best->time)
				{
					best = &entry;
				}
			}
		}

		return best->state;
	}

	/// @brief 2つのタイムスタンプ間で補間した状態を取得する
	/// @param t1 開始タイムスタンプ
	/// @param t2 終了タイムスタンプ
	/// @param alpha 補間係数 [0.0, 1.0]（0.0 = t1の状態, 1.0 = t2の状態）
	/// @return 補間された状態（補間関数が未設定または履歴不足なら nullopt）
	[[nodiscard]] std::optional<State> interpolate(
		Timestamp t1, Timestamp t2, float alpha) const
	{
		if (!m_lerpFunc)
		{
			return std::nullopt;
		}

		auto state1 = rewindTo(t1);
		auto state2 = rewindTo(t2);

		if (!state1 || !state2)
		{
			return std::nullopt;
		}

		alpha = std::clamp(alpha, 0.0f, 1.0f);
		return m_lerpFunc(*state1, *state2, alpha);
	}

	/// @brief 指定タイムスタンプに最も近い2つの状態の間で補間する
	/// @param time 対象タイムスタンプ
	/// @return 補間された状態（補間関数が未設定なら nullopt）
	/// @details time が2つの記録済み状態の間にある場合、
	///          その2つの状態間で自動的に補間係数を計算する。
	[[nodiscard]] std::optional<State> interpolateAt(Timestamp time) const
	{
		if (!m_lerpFunc || m_buffer.empty())
		{
			return std::nullopt;
		}

		/// タイムスタンプでソートされた順序で前後の状態を探す
		const TimestampedState<State>* before = nullptr;
		const TimestampedState<State>* after = nullptr;

		for (const auto& entry : m_buffer)
		{
			if (entry.time <= time)
			{
				if (before == nullptr || entry.time > before->time)
				{
					before = &entry;
				}
			}
			if (entry.time >= time)
			{
				if (after == nullptr || entry.time < after->time)
				{
					after = &entry;
				}
			}
		}

		if (before == nullptr || after == nullptr)
		{
			/// 範囲外の場合、最も近い状態を返す
			return rewindTo(time);
		}

		if (before->time == after->time)
		{
			return before->state;
		}

		const float alpha = static_cast<float>(time - before->time)
			/ static_cast<float>(after->time - before->time);

		return m_lerpFunc(before->state, after->state, alpha);
	}

	/// @brief 記録済み状態数を返す
	[[nodiscard]] std::size_t stateCount() const noexcept
	{
		return m_buffer.size();
	}

	/// @brief リングバッファの容量を返す
	[[nodiscard]] std::size_t capacity() const noexcept
	{
		return m_capacity;
	}

	/// @brief 最古のタイムスタンプを返す
	/// @return 最古のタイムスタンプ（バッファが空なら0）
	[[nodiscard]] Timestamp oldestTimestamp() const noexcept
	{
		if (m_buffer.empty()) return 0;

		Timestamp oldest = m_buffer[0].time;
		for (const auto& entry : m_buffer)
		{
			if (entry.time < oldest)
			{
				oldest = entry.time;
			}
		}
		return oldest;
	}

	/// @brief 最新のタイムスタンプを返す
	/// @return 最新のタイムスタンプ（バッファが空なら0）
	[[nodiscard]] Timestamp newestTimestamp() const noexcept
	{
		if (m_buffer.empty()) return 0;

		Timestamp newest = m_buffer[0].time;
		for (const auto& entry : m_buffer)
		{
			if (entry.time > newest)
			{
				newest = entry.time;
			}
		}
		return newest;
	}

	/// @brief 全履歴をクリアする
	void clear() noexcept
	{
		m_buffer.clear();
		m_writeIndex = 0;
	}

private:
	/// @brief デフォルトの履歴容量（60fps × 2秒 = 120フレーム）
	static constexpr std::size_t kDefaultCapacity = 120;

	std::vector<TimestampedState<State>> m_buffer; ///< リングバッファ
	std::size_t m_capacity;                         ///< 最大容量
	std::size_t m_writeIndex = 0;                   ///< 次の書き込み位置
	LerpFunc m_lerpFunc;                            ///< 補間関数
};

} // namespace mitiru::network
