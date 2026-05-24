#pragma once

/// @file UtilityAI.hpp
/// @brief スコアリングベースのユーティリティAI意思決定システム
///
/// 各アクションに関連付けられた評価関数（Consideration）のスコアを
/// 総合的に評価し、最も高スコアのアクションを選択する。
///
/// @code
/// sgc::ai::UtilitySelector<GameContext> selector;
/// selector.addAction("attack", [](const GameContext&) { /* 攻撃実行 */ })
///     .addConsideration([](const GameContext& ctx) { return ctx.enemyDistance < 5.0f ? 1.0f : 0.2f; })
///     .addConsideration([](const GameContext& ctx) { return ctx.health / 100.0f; }, 1.5f);
/// selector.addAction("flee", [](const GameContext&) { /* 逃走実行 */ })
///     .addConsideration([](const GameContext& ctx) { return 1.0f - ctx.health / 100.0f; });
/// auto result = selector.evaluate(context);
/// @endcode

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace sgc::ai
{

/// @brief 評価関数の結果をスコア（0〜1）に変換するコンセプト
template <typename F, typename Context>
concept ConsiderationFunc = requires(F f, const Context& ctx)
{
	{ f(ctx) } -> std::convertible_to<float>;
};

/// @brief 単一の評価項目
/// @tparam Context 評価に使用するコンテキスト型
template <typename Context>
struct Consideration
{
	/// @brief 評価関数（0.0〜1.0のスコアを返す）
	std::function<float(const Context&)> evaluate;

	/// @brief この評価項目の重み（デフォルト: 1.0）
	float weight = 1.0f;
};

/// @brief アクションの評価結果
struct ActionScore
{
	/// @brief アクション名
	std::string name;

	/// @brief 総合スコア
	float score = 0.0f;

	/// @brief アクションのインデックス
	std::size_t index = 0;
};

/// @brief ユーティリティAIのアクション定義
/// @tparam Context 評価に使用するコンテキスト型
template <typename Context>
class Action
{
public:
	/// @brief アクションを構築する
	/// @param name アクション名
	/// @param callback 実行時コールバック
	Action(std::string name, std::function<void(const Context&)> callback)
		: m_name(std::move(name))
		, m_callback(std::move(callback))
	{
	}

	/// @brief 評価項目を追加する（fluent API）
	/// @param func 評価関数
	/// @param weight 重み（デフォルト: 1.0）
	/// @return このアクションへの参照
	Action& addConsideration(std::function<float(const Context&)> func, float weight = 1.0f)
	{
		m_considerations.push_back({std::move(func), weight});
		return *this;
	}

	/// @brief すべての評価項目のスコアを計算する
	/// @param ctx コンテキスト
	/// @return 加重平均スコア（0.0〜1.0）
	[[nodiscard]] float evaluate(const Context& ctx) const
	{
		if (m_considerations.empty())
		{
			return 0.0f;
		}

		float totalWeight = 0.0f;
		float weightedSum = 0.0f;

		for (const auto& c : m_considerations)
		{
			const float raw = std::clamp(c.evaluate(ctx), 0.0f, 1.0f);
			weightedSum += raw * c.weight;
			totalWeight += c.weight;
		}

		if (totalWeight <= 0.0f)
		{
			return 0.0f;
		}

		return weightedSum / totalWeight;
	}

	/// @brief アクションを実行する
	/// @param ctx コンテキスト
	void execute(const Context& ctx) const
	{
		if (m_callback)
		{
			m_callback(ctx);
		}
	}

	/// @brief アクション名を取得する
	/// @return アクション名
	[[nodiscard]] const std::string& name() const noexcept
	{
		return m_name;
	}

private:
	std::string m_name;
	std::function<void(const Context&)> m_callback;
	std::vector<Consideration<Context>> m_considerations;
};

/// @brief ユーティリティベースのアクション選択器
/// @tparam Context 評価に使用するコンテキスト型
///
/// 登録された全アクションを評価し、最もスコアの高いアクションを選択する。
template <typename Context>
class UtilitySelector
{
public:
	/// @brief アクションを追加する（fluent API）
	/// @param name アクション名
	/// @param callback 実行時コールバック
	/// @return 追加されたActionへの参照（Consideration追加用）
	Action<Context>& addAction(std::string name, std::function<void(const Context&)> callback)
	{
		m_actions.emplace_back(std::move(name), std::move(callback));
		return m_actions.back();
	}

	/// @brief 全アクションを評価し、最高スコアのものを返す
	/// @param ctx コンテキスト
	/// @return 最高スコアのアクション情報（アクションがない場合はnullopt）
	[[nodiscard]] std::optional<ActionScore> evaluate(const Context& ctx) const
	{
		if (m_actions.empty())
		{
			return std::nullopt;
		}

		std::optional<ActionScore> best;

		for (std::size_t i = 0; i < m_actions.size(); ++i)
		{
			const float score = m_actions[i].evaluate(ctx);
			if (!best.has_value() || score > best->score)
			{
				best = ActionScore{m_actions[i].name(), score, i};
			}
		}

		return best;
	}

	/// @brief 全アクションのスコアを取得する
	/// @param ctx コンテキスト
	/// @return 全アクションのスコア一覧（スコア降順）
	[[nodiscard]] std::vector<ActionScore> evaluateAll(const Context& ctx) const
	{
		std::vector<ActionScore> results;
		results.reserve(m_actions.size());

		for (std::size_t i = 0; i < m_actions.size(); ++i)
		{
			results.push_back({m_actions[i].name(), m_actions[i].evaluate(ctx), i});
		}

		std::sort(results.begin(), results.end(),
			[](const ActionScore& a, const ActionScore& b)
			{
				return a.score > b.score;
			});

		return results;
	}

	/// @brief 最高スコアのアクションを評価して実行する
	/// @param ctx コンテキスト
	/// @return 実行されたアクションの情報（アクションがない場合はnullopt）
	std::optional<ActionScore> evaluateAndExecute(const Context& ctx)
	{
		const auto best = evaluate(ctx);
		if (best.has_value())
		{
			m_actions[best->index].execute(ctx);
		}
		return best;
	}

	/// @brief 登録されたアクション数を取得する
	/// @return アクション数
	[[nodiscard]] std::size_t actionCount() const noexcept
	{
		return m_actions.size();
	}

private:
	std::vector<Action<Context>> m_actions;
};

} // namespace sgc::ai
