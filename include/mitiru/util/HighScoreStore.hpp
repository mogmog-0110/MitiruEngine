#pragma once

/// @file HighScoreStore.hpp
/// @brief インメモリハイスコアストア
/// @details ゲーム名をキーにしたスコア保存・読み込み機能を提供する。
///          ファイルI/Oなしのインメモリ実装（テスト容易性を優先）。

#include <string>
#include <string_view>
#include <unordered_map>

namespace mitiru::util
{

/// @brief インメモリハイスコアストア
/// @details ゲーム名をキーにハイスコアを保存する。
///          シングルトンインスタンスでゲーム間共有が可能。
///
/// @code
/// auto& store = mitiru::util::HighScoreStore::instance();
/// store.save("snake", 100);
/// int hi = store.load("snake"); // 100
/// store.save("snake", 50);      // 更新されない（低い）
/// int hi2 = store.load("snake"); // 100
/// @endcode
class HighScoreStore
{
public:
	/// @brief スコアを保存する（既存より高い場合のみ更新）
	/// @param game ゲーム名
	/// @param score スコア
	void save(std::string_view game, int score)
	{
		auto key = std::string(game);
		auto it = m_scores.find(key);
		if (it == m_scores.end() || score > it->second)
		{
			m_scores[key] = score;
		}
	}

	/// @brief スコアを読み込む
	/// @param game ゲーム名
	/// @return 保存済みスコア（未登録なら0）
	[[nodiscard]] int load(std::string_view game) const
	{
		const auto it = m_scores.find(std::string(game));
		return (it != m_scores.end()) ? it->second : 0;
	}

	/// @brief 指定スコアがハイスコアか判定する
	/// @param game ゲーム名
	/// @param score 判定するスコア
	/// @return 現在のハイスコアより高ければtrue
	[[nodiscard]] bool isHighScore(std::string_view game, int score) const
	{
		return score > load(game);
	}

	/// @brief シングルトンインスタンスを取得する
	/// @return HighScoreStoreの参照
	[[nodiscard]] static HighScoreStore& instance()
	{
		static HighScoreStore s_instance;
		return s_instance;
	}

	/// @brief 全スコアをクリアする（テスト用）
	void clear() noexcept
	{
		m_scores.clear();
	}

private:
	std::unordered_map<std::string, int> m_scores;   ///< ゲーム名→ハイスコア
};

} // namespace mitiru::util
