#pragma once

/// @file GOAP.hpp
/// @brief ゴール指向アクションプランニング（GOAP）
///
/// ビットセットベースのワールドステートと、A*探索による
/// 最小コストのアクションシーケンス計画を提供する。
///
/// @code
/// using namespace sgc::ai;
/// // ワールドステート定義
/// enum Props { HasAxe, HasWood, AtTree, COUNT };
/// WorldState current;
/// current.set(AtTree, true);
/// WorldState goal;
/// goal.set(HasWood, true);
///
/// GOAPAction chopTree("ChopTree", 2.0f);
/// chopTree.setPrecondition(HasAxe, true);
/// chopTree.setPrecondition(AtTree, true);
/// chopTree.setEffect(HasWood, true);
///
/// GOAPPlanner planner;
/// planner.addAction(chopTree);
/// auto plan = planner.plan(current, goal);
/// @endcode

#include <algorithm>
#include <bitset>
#include <cstddef>
#include <functional>
#include <optional>
#include <queue>
#include <string>
#include <vector>

namespace sgc::ai
{

/// @brief ワールドステートの最大プロパティ数
inline constexpr std::size_t MAX_WORLD_PROPS = 64;

/// @brief ビットセットベースのワールドステート表現
///
/// 各プロパティをbool値として管理する。
/// マスクにより「気にするプロパティ」と「気にしないプロパティ」を区別する。
class WorldState
{
public:
	/// @brief プロパティ値を設定する
	/// @param index プロパティインデックス
	/// @param value 値
	void set(std::size_t index, bool value)
	{
		m_values.set(index, value);
		m_mask.set(index, true);
	}

	/// @brief プロパティ値を取得する
	/// @param index プロパティインデックス
	/// @return プロパティの値
	[[nodiscard]] bool get(std::size_t index) const
	{
		return m_values.test(index);
	}

	/// @brief プロパティがマスクに含まれているか
	/// @param index プロパティインデックス
	/// @return マスクに含まれている場合true
	[[nodiscard]] bool hasMask(std::size_t index) const
	{
		return m_mask.test(index);
	}

	/// @brief このステートがゴールステートを満たすか判定する
	/// @param goal ゴールステート
	/// @return ゴールのマスク対象プロパティが全て一致すればtrue
	[[nodiscard]] bool satisfies(const WorldState& goal) const
	{
		// ゴールのマスク範囲内で、値が全て一致するか
		const auto relevant = goal.m_mask;
		return (m_values & relevant) == (goal.m_values & relevant);
	}

	/// @brief ゴールとの不一致プロパティ数を返す（ヒューリスティック用）
	/// @param goal ゴールステート
	/// @return 不一致数
	[[nodiscard]] std::size_t distanceTo(const WorldState& goal) const
	{
		const auto relevant = goal.m_mask;
		const auto diff = (m_values ^ goal.m_values) & relevant;
		return diff.count();
	}

	/// @brief エフェクトを適用して新しいステートを生成する
	/// @param effects 適用するエフェクト
	/// @return エフェクト適用後の新しいステート
	[[nodiscard]] WorldState applyEffects(const WorldState& effects) const
	{
		WorldState result = *this;
		// エフェクトのマスク部分だけ上書き
		for (std::size_t i = 0; i < MAX_WORLD_PROPS; ++i)
		{
			if (effects.m_mask.test(i))
			{
				result.m_values.set(i, effects.m_values.test(i));
				result.m_mask.set(i, true);
			}
		}
		return result;
	}

	/// @brief 等価比較
	[[nodiscard]] bool operator==(const WorldState& other) const
	{
		return m_values == other.m_values && m_mask == other.m_mask;
	}

	/// @brief ハッシュ用にビットセットの内部値を返す
	[[nodiscard]] std::size_t hash() const
	{
		auto h1 = std::hash<std::bitset<MAX_WORLD_PROPS>>{}(m_values);
		auto h2 = std::hash<std::bitset<MAX_WORLD_PROPS>>{}(m_mask);
		return h1 ^ (h2 << 1);
	}

private:
	std::bitset<MAX_WORLD_PROPS> m_values;  ///< プロパティ値
	std::bitset<MAX_WORLD_PROPS> m_mask;    ///< 有効プロパティマスク
};

/// @brief GOAPアクション定義
///
/// 前提条件（preconditions）、効果（effects）、コストを持つ。
class GOAPAction
{
public:
	/// @brief アクションを構築する
	/// @param name アクション名
	/// @param cost アクションのコスト（デフォルト: 1.0）
	explicit GOAPAction(std::string name, float cost = 1.0f)
		: m_name(std::move(name))
		, m_cost(cost)
	{
	}

	/// @brief 前提条件を設定する
	/// @param index プロパティインデックス
	/// @param value 必要な値
	void setPrecondition(std::size_t index, bool value)
	{
		m_preconditions.set(index, value);
	}

	/// @brief 効果を設定する
	/// @param index プロパティインデックス
	/// @param value 効果の値
	void setEffect(std::size_t index, bool value)
	{
		m_effects.set(index, value);
	}

	/// @brief 前提条件を満たすか判定する
	/// @param state 現在のワールドステート
	/// @return 前提条件を全て満たせばtrue
	[[nodiscard]] bool isPreconditionMet(const WorldState& state) const
	{
		return state.satisfies(m_preconditions);
	}

	/// @brief 効果を適用した新しいステートを返す
	/// @param state 現在のワールドステート
	/// @return 効果適用後のステート
	[[nodiscard]] WorldState applyTo(const WorldState& state) const
	{
		return state.applyEffects(m_effects);
	}

	/// @brief アクション名を取得する
	[[nodiscard]] const std::string& name() const noexcept { return m_name; }

	/// @brief コストを取得する
	[[nodiscard]] float cost() const noexcept { return m_cost; }

	/// @brief 前提条件を取得する
	[[nodiscard]] const WorldState& preconditions() const noexcept { return m_preconditions; }

	/// @brief 効果を取得する
	[[nodiscard]] const WorldState& effects() const noexcept { return m_effects; }

private:
	std::string m_name;
	float m_cost;
	WorldState m_preconditions;
	WorldState m_effects;
};

/// @brief プランの結果
struct GOAPPlan
{
	/// @brief 実行すべきアクションのシーケンス
	std::vector<const GOAPAction*> actions;

	/// @brief 合計コスト
	float totalCost = 0.0f;
};

/// @brief A*ベースのGOAPプランナー
///
/// 現在のワールドステートからゴールステートへ到達する
/// 最小コストのアクションシーケンスを探索する。
class GOAPPlanner
{
public:
	/// @brief アクションを登録する
	/// @param action 登録するアクション
	void addAction(GOAPAction action)
	{
		m_actions.push_back(std::move(action));
	}

	/// @brief プランを計算する
	/// @param start 現在のワールドステート
	/// @param goal ゴールステート
	/// @param maxIterations 最大探索回数（デフォルト: 1000）
	/// @return 計画結果（到達不可能な場合はnullopt）
	[[nodiscard]] std::optional<GOAPPlan> plan(
		const WorldState& start,
		const WorldState& goal,
		std::size_t maxIterations = 1000) const
	{
		struct Node
		{
			WorldState state;
			float gCost = 0.0f;              ///< 開始からのコスト
			float fCost = 0.0f;              ///< gCost + ヒューリスティック
			std::vector<std::size_t> path;   ///< アクションインデックス列
		};

		// ゴール判定
		if (start.satisfies(goal))
		{
			return GOAPPlan{{}, 0.0f};
		}

		auto cmp = [](const Node& a, const Node& b)
		{
			return a.fCost > b.fCost;
		};
		std::priority_queue<Node, std::vector<Node>, decltype(cmp)> open(cmp);

		Node startNode;
		startNode.state = start;
		startNode.gCost = 0.0f;
		startNode.fCost = static_cast<float>(start.distanceTo(goal));
		open.push(std::move(startNode));

		std::size_t iterations = 0;

		while (!open.empty() && iterations < maxIterations)
		{
			++iterations;
			auto current = open.top();
			open.pop();

			// ゴール到達チェック（pop時 = A*最適性保証）
			if (current.state.satisfies(goal))
			{
				GOAPPlan result;
				result.totalCost = current.gCost;
				result.actions.reserve(current.path.size());
				for (const auto idx : current.path)
				{
					result.actions.push_back(&m_actions[idx]);
				}
				return result;
			}

			// 各アクションを試行
			for (std::size_t i = 0; i < m_actions.size(); ++i)
			{
				const auto& action = m_actions[i];

				if (!action.isPreconditionMet(current.state))
				{
					continue;
				}

				Node next;
				next.state = action.applyTo(current.state);
				next.gCost = current.gCost + action.cost();
				next.path = current.path;
				next.path.push_back(i);
				next.fCost = next.gCost + static_cast<float>(next.state.distanceTo(goal));
				open.push(std::move(next));
			}
		}

		return std::nullopt;
	}

	/// @brief 登録済みアクション数を取得する
	[[nodiscard]] std::size_t actionCount() const noexcept
	{
		return m_actions.size();
	}

private:
	std::vector<GOAPAction> m_actions;
};

} // namespace sgc::ai
