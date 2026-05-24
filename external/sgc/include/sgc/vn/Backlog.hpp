#pragma once

/// @file Backlog.hpp
/// @brief 対話履歴バッファ
///
/// ビジュアルノベルの既読テキストを記録する履歴バッファ。
/// 最大エントリ数を超えた場合、最も古いエントリが自動削除される。
///
/// @code
/// using namespace sgc::vn;
/// Backlog log;
/// log.addEntry({"Sakura", "Hello!", ""});
/// log.addEntry({"Sakura", "How are you?", ""});
/// auto history = log.entries();
/// @endcode

#include <cstddef>
#include <deque>
#include <span>
#include <string>
#include <vector>

namespace sgc::vn
{

/// @brief バックログエントリ
struct BacklogEntry
{
	std::string speaker;      ///< 話者名
	std::string text;         ///< テキスト内容
	std::string choiceMade;   ///< 選択した選択肢（空なら選択肢なし）
};

/// @brief 対話履歴バッファ
///
/// 固定容量のリングバッファとして動作する。
/// エントリ数が最大値を超えた場合、最も古いエントリが削除される。
class Backlog
{
public:
	/// @brief デフォルトの最大エントリ数
	static constexpr std::size_t DEFAULT_MAX_ENTRIES = 500;

	/// @brief コンストラクタ
	/// @param maxEntries 最大エントリ数
	explicit Backlog(std::size_t maxEntries = DEFAULT_MAX_ENTRIES)
		: m_maxEntries(maxEntries)
	{
	}

	/// @brief エントリを追加する
	///
	/// 最大数を超えた場合、最も古いエントリが削除される。
	///
	/// @param entry 追加するエントリ
	void addEntry(BacklogEntry entry)
	{
		m_entries.push_back(std::move(entry));
		while (m_entries.size() > m_maxEntries)
		{
			m_entries.pop_front();
		}
	}

	/// @brief エントリ一覧を取得する
	///
	/// dequeの内部バッファは連続とは限らないため、
	/// ベクターにコピーしてspanで返す。
	///
	/// @return エントリのスパン
	/// @note 返されるspanはBacklogオブジェクトの寿命に依存する
	[[nodiscard]] const std::deque<BacklogEntry>& entries() const noexcept
	{
		return m_entries;
	}

	/// @brief エントリ数を取得する
	/// @return 現在のエントリ数
	[[nodiscard]] std::size_t size() const noexcept
	{
		return m_entries.size();
	}

	/// @brief 空かどうかを判定する
	/// @return 空ならtrue
	[[nodiscard]] bool empty() const noexcept
	{
		return m_entries.empty();
	}

	/// @brief すべてのエントリを削除する
	void clear()
	{
		m_entries.clear();
	}

private:
	std::deque<BacklogEntry> m_entries;   ///< エントリバッファ
	std::size_t m_maxEntries;             ///< 最大エントリ数
};

} // namespace sgc::vn
