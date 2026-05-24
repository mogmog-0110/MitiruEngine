#pragma once

/// @file PredictionBuffer.hpp
/// @brief クライアント予測バッファ
///
/// クライアントサイド予測（Client-Side Prediction）と
/// サーバー和解（Server Reconciliation）を実現するためのバッファ。
/// ローカル入力を記録し、サーバーからの確認済み状態で再予測を行う。
///
/// @code
/// sgc::network::PredictionBuffer<Vec2, InputCmd> buffer;
/// buffer.recordInput(tick, input, predictedPos);
///
/// // サーバーから確認が来たら和解する
/// auto resimCount = buffer.reconcile(serverTick, serverPos,
///     [](const Vec2& state, const InputCmd& input) {
///         return simulate(state, input);
///     });
/// @endcode

#include <cstdint>
#include <deque>
#include <functional>

namespace sgc::network
{

/// @brief 入力スナップショット
/// @tparam InputT 入力型
template <typename InputT>
struct InputSnapshot
{
	uint32_t tick;  ///< 入力が発生したティック
	InputT input;   ///< 入力データ
};

/// @brief クライアント予測バッファ
///
/// サーバーからの確認待ちの入力を保持し、サーバーの応答で和解する。
/// @tparam StateT 状態型（位置・速度など）
/// @tparam InputT 入力型（移動コマンドなど）
template <typename StateT, typename InputT>
class PredictionBuffer
{
public:
	/// @brief 予測バッファを構築する
	/// @param maxSize バッファの最大サイズ
	explicit PredictionBuffer(size_t maxSize = 128)
		: m_maxSize(maxSize)
	{
	}

	/// @brief 入力を記録して予測状態を保存する
	/// @param tick ティック番号
	/// @param input 入力データ
	/// @param predictedState 予測された状態
	void recordInput(uint32_t tick, const InputT& input, const StateT& predictedState)
	{
		Entry entry;
		entry.tick = tick;
		entry.input = input;
		entry.state = predictedState;
		m_buffer.push_back(std::move(entry));
		m_latestState = predictedState;

		/// バッファサイズ制限を超えた場合、古いエントリを削除する
		while (m_buffer.size() > m_maxSize)
		{
			m_buffer.pop_front();
		}
	}

	/// @brief サーバーからの確認済み状態で和解する
	///
	/// サーバーが確認したティック以前のエントリを破棄し、
	/// 残りのエントリで状態を再シミュレーションする。
	///
	/// @param serverTick サーバーが確認したティック
	/// @param serverState サーバーが確認した状態
	/// @param simulate 再シミュレーション関数
	/// @return 再予測したティック数
	int32_t reconcile(
		uint32_t serverTick,
		const StateT& serverState,
		std::function<StateT(const StateT&, const InputT&)> simulate)
	{
		/// サーバーティック以前のエントリを破棄する
		while (!m_buffer.empty() && m_buffer.front().tick <= serverTick)
		{
			m_buffer.pop_front();
		}

		/// サーバー状態から再シミュレーションする
		StateT currentState = serverState;
		int32_t resimCount = 0;

		for (auto& entry : m_buffer)
		{
			currentState = simulate(currentState, entry.input);
			entry.state = currentState;
			++resimCount;
		}

		m_latestState = currentState;
		return resimCount;
	}

	/// @brief 最新の予測状態を取得する
	/// @return 最新の状態
	[[nodiscard]] const StateT& latestState() const
	{
		return m_latestState;
	}

	/// @brief バッファ内の未確認入力数を取得する
	/// @return 未確認入力数
	[[nodiscard]] size_t pendingCount() const noexcept
	{
		return m_buffer.size();
	}

	/// @brief バッファをクリアする
	void clear()
	{
		m_buffer.clear();
		m_latestState = StateT{};
	}

private:
	/// @brief バッファエントリ
	struct Entry
	{
		uint32_t tick;   ///< ティック番号
		InputT input;    ///< 入力データ
		StateT state;    ///< 予測状態
	};

	std::deque<Entry> m_buffer;   ///< 予測バッファ
	size_t m_maxSize;             ///< 最大バッファサイズ
	StateT m_latestState{};       ///< 最新の予測状態
};

} // namespace sgc::network
