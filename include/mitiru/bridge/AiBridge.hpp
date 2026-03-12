#pragma once

/// @file AiBridge.hpp
/// @brief sgc AI統合ブリッジ（スタブ）
/// @details sgcのAI機能（ビヘイビアツリー等）をMitiruエンジンに統合する。
///          現段階ではスタブ実装。

#include <string>
#include <unordered_map>
#include <vector>

namespace mitiru::bridge
{

/// @brief AI状態
enum class AiState
{
	Idle,       ///< アイドル
	Running,    ///< 実行中
	Success,    ///< 成功
	Failure     ///< 失敗
};

/// @brief AIブリッジ（スタブ）
/// @details ビヘイビアツリーの登録と状態管理を行うスタブ実装。
///          将来的にsgcのAI機能と統合される。
///
/// @code
/// mitiru::bridge::AiBridge ai;
/// ai.registerBehaviorTree("patrol", nullptr);
/// ai.setState("patrol", AiState::Running);
/// @endcode
class AiBridge
{
public:
	/// @brief ビヘイビアツリーを登録する
	/// @param name ビヘイビアツリー名
	/// @param tree ビヘイビアツリーへのポインタ（スタブのためvoid*）
	void registerBehaviorTree(const std::string& name, void* tree)
	{
		m_trees[name] = Entry{tree, AiState::Idle};
	}

	/// @brief ビヘイビアツリーの登録を解除する
	/// @param name ビヘイビアツリー名
	void unregisterBehaviorTree(const std::string& name)
	{
		m_trees.erase(name);
	}

	/// @brief ビヘイビアツリーの状態を設定する
	/// @param name ビヘイビアツリー名
	/// @param state 新しい状態
	void setState(const std::string& name, AiState state)
	{
		const auto it = m_trees.find(name);
		if (it != m_trees.end())
		{
			it->second.state = state;
		}
	}

	/// @brief ビヘイビアツリーの状態を取得する
	/// @param name ビヘイビアツリー名
	/// @return AI状態（未登録の場合は Idle）
	[[nodiscard]] AiState getState(const std::string& name) const
	{
		const auto it = m_trees.find(name);
		if (it == m_trees.end())
		{
			return AiState::Idle;
		}
		return it->second.state;
	}

	/// @brief 登録されているビヘイビアツリー名一覧を取得する
	/// @return 名前のベクタ
	[[nodiscard]] std::vector<std::string> registeredNames() const
	{
		std::vector<std::string> names;
		names.reserve(m_trees.size());
		for (const auto& [name, entry] : m_trees)
		{
			names.push_back(name);
		}
		return names;
	}

	/// @brief AI状態をJSON文字列として返す
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"treeCount\":" + std::to_string(m_trees.size()) + ",";
		json += "\"trees\":[";
		bool first = true;
		for (const auto& [name, entry] : m_trees)
		{
			if (!first)
			{
				json += ",";
			}
			json += "{\"name\":\"" + name + "\",\"state\":\"" + stateToString(entry.state) + "\"}";
			first = false;
		}
		json += "]";
		json += "}";
		return json;
	}

private:
	/// @brief AI状態を文字列に変換する
	/// @param state AI状態
	/// @return 文字列表現
	[[nodiscard]] static std::string stateToString(AiState state)
	{
		switch (state)
		{
		case AiState::Idle:    return "Idle";
		case AiState::Running: return "Running";
		case AiState::Success: return "Success";
		case AiState::Failure: return "Failure";
		}
		return "Unknown";
	}

	/// @brief 内部エントリ
	struct Entry
	{
		void* tree = nullptr;     ///< ビヘイビアツリーポインタ（スタブ）
		AiState state = AiState::Idle;  ///< 現在の状態
	};

	/// @brief 名前 → エントリ
	std::unordered_map<std::string, Entry> m_trees;
};

} // namespace mitiru::bridge
