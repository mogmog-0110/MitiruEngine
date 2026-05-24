#pragma once

/// @file Strategy.hpp
/// @brief 戦略パターン — 実行時にアルゴリズムを切り替える
///
/// std::functionをラップし、戦略の有無チェックとクリアを提供する。
/// ゲームのAI行動選択・ダメージ計算式切り替え等に使用する。
///
/// @code
/// sgc::Strategy<int(int, int)> damageCalc;
/// damageCalc.setStrategy([](int atk, int def) { return atk - def; });
/// int dmg = damageCalc.execute(50, 20);  // 30
///
/// damageCalc.setStrategy([](int atk, int def) { return atk * 2 - def; });
/// dmg = damageCalc.execute(50, 20);  // 80
/// @endcode

#include <functional>
#include <stdexcept>

namespace sgc
{

/// @brief 戦略パターン（一次特殊化宣言）
/// @tparam Signature 関数シグネチャ R(Args...)
template <typename Signature>
class Strategy;

/// @brief 戦略パターン — 実行時にアルゴリズムを切り替える
/// @tparam R 戻り値の型
/// @tparam Args 引数の型
template <typename R, typename... Args>
class Strategy<R(Args...)>
{
public:
	/// @brief 戦略関数の型
	using StrategyFunc = std::function<R(Args...)>;

	/// @brief 戦略を設定する
	/// @param func 新しい戦略関数
	void setStrategy(StrategyFunc func)
	{
		m_strategy = std::move(func);
	}

	/// @brief 戦略を実行する
	/// @param args 引数
	/// @return 戦略の実行結果
	/// @throw std::runtime_error 戦略が未設定の場合
	R execute(Args... args) const
	{
		if (!m_strategy)
		{
			throw std::runtime_error("Strategy: no strategy set");
		}
		return m_strategy(std::forward<Args>(args)...);
	}

	/// @brief 戦略が設定されているか確認する
	/// @return 設定されていればtrue
	[[nodiscard]] bool hasStrategy() const noexcept
	{
		return static_cast<bool>(m_strategy);
	}

	/// @brief 戦略をクリアする
	void clear() noexcept
	{
		m_strategy = nullptr;
	}

private:
	StrategyFunc m_strategy;  ///< 現在の戦略関数
};

} // namespace sgc
