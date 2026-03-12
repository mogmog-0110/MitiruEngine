#pragma once

/// @file CoverageTracker.hpp
/// @brief コードパスカバレッジ追跡
/// @details ゲームロジック内の特定パスが実行されたかを記録し、
///          AIエージェントがカバレッジ情報を取得するために使用する。

#include <string>
#include <unordered_set>
#include <vector>

namespace mitiru::validate
{

/// @brief コードパスカバレッジトラッカー
/// @details 事前に登録されたパスIDに対して、実行済みかどうかを追跡する。
///
/// @code
/// mitiru::validate::CoverageTracker tracker;
/// tracker.registerPath("player_jump");
/// tracker.registerPath("enemy_spawn");
/// tracker.markVisited("player_jump");
/// float cov = tracker.coverage(); // 0.5
/// @endcode
class CoverageTracker
{
public:
	/// @brief パスを登録する（未訪問状態）
	/// @param pathId パスの識別文字列
	void registerPath(const std::string& pathId)
	{
		m_allPaths.insert(pathId);
	}

	/// @brief パスを訪問済みとしてマークする
	/// @param pathId パスの識別文字列
	/// @note 未登録のパスも自動的に登録される
	void markVisited(const std::string& pathId)
	{
		m_allPaths.insert(pathId);
		m_visitedPaths.insert(pathId);
	}

	/// @brief 指定パスが訪問済みか判定する
	/// @param pathId パスの識別文字列
	/// @return 訪問済みなら true
	[[nodiscard]] bool isVisited(const std::string& pathId) const
	{
		return m_visitedPaths.find(pathId) != m_visitedPaths.end();
	}

	/// @brief カバレッジ率を取得する
	/// @return 0.0〜1.0 のカバレッジ率（登録パスが0の場合は1.0）
	[[nodiscard]] float coverage() const noexcept
	{
		if (m_allPaths.empty())
		{
			return 1.0f;
		}
		return static_cast<float>(m_visitedPaths.size())
			/ static_cast<float>(m_allPaths.size());
	}

	/// @brief 全登録パスを取得する
	/// @return 全パスIDのベクタ
	[[nodiscard]] std::vector<std::string> allPaths() const
	{
		return {m_allPaths.begin(), m_allPaths.end()};
	}

	/// @brief 訪問済みパスを取得する
	/// @return 訪問済みパスIDのベクタ
	[[nodiscard]] std::vector<std::string> visitedPaths() const
	{
		return {m_visitedPaths.begin(), m_visitedPaths.end()};
	}

	/// @brief 未訪問パスを取得する
	/// @return 未訪問パスIDのベクタ
	[[nodiscard]] std::vector<std::string> unvisitedPaths() const
	{
		std::vector<std::string> result;
		for (const auto& path : m_allPaths)
		{
			if (m_visitedPaths.find(path) == m_visitedPaths.end())
			{
				result.push_back(path);
			}
		}
		return result;
	}

	/// @brief カバレッジ情報をJSON文字列として返す
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"coverage\":" + std::to_string(coverage()) + ",";
		json += "\"totalPaths\":" + std::to_string(m_allPaths.size()) + ",";
		json += "\"visitedCount\":" + std::to_string(m_visitedPaths.size()) + ",";

		/// 未訪問パス一覧
		json += "\"unvisited\":[";
		const auto unvisited = unvisitedPaths();
		for (std::size_t i = 0; i < unvisited.size(); ++i)
		{
			if (i > 0)
			{
				json += ",";
			}
			json += "\"" + unvisited[i] + "\"";
		}
		json += "]";

		json += "}";
		return json;
	}

	/// @brief 全データをリセットする
	void reset() noexcept
	{
		m_allPaths.clear();
		m_visitedPaths.clear();
	}

	/// @brief 訪問記録のみリセットする（登録パスは維持）
	void resetVisited() noexcept
	{
		m_visitedPaths.clear();
	}

private:
	std::unordered_set<std::string> m_allPaths;       ///< 全登録パス
	std::unordered_set<std::string> m_visitedPaths;   ///< 訪問済みパス
};

/// @brief カバレッジマークマクロ
/// @details コード中に埋め込み、実行されたことをトラッカーに通知する。
/// @param tracker CoverageTracker インスタンス
/// @param pathName パス名文字列リテラル
#define MITIRU_COVERAGE_MARK(tracker, pathName) \
	do { (tracker).markVisited(pathName); } while (false)

} // namespace mitiru::validate
