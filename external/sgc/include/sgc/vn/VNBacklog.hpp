#pragma once

/// @file VNBacklog.hpp
/// @brief 拡張テキストバックログ
///
/// VNの既読テキスト履歴をタイムスタンプ付きで記録する。
/// 既存のBacklog.hppに対し、タイムスタンプ・選択肢テキスト・
/// 検索機能を追加した拡張版。
///
/// @code
/// using namespace sgc::vn;
/// VNBacklog log(100);
/// log.addEntry({"Sakura", "Hello!", std::nullopt, 1.5f});
/// log.addEntry({"", "", "Yes", 2.0f});
/// auto entries = log.getEntries();
/// @endcode

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace sgc::vn
{

/// @brief 拡張バックログエントリ
///
/// タイムスタンプと選択肢テキストを含むバックログエントリ。
struct VNBacklogEntry
{
	std::string speaker;                         ///< 話者名
	std::string text;                            ///< テキスト内容
	std::optional<std::string> choiceText;       ///< 選択した選択肢テキスト（nulloptなら選択肢なし）
	float timestamp = 0.0f;                      ///< ゲーム内タイムスタンプ（秒）
};

/// @brief 拡張テキストバックログ
///
/// 固定容量の循環バッファとして動作する。
/// タイムスタンプ付きエントリの追加・取得・検索を提供する。
class VNBacklog
{
public:
	/// @brief デフォルトの最大エントリ数
	static constexpr std::size_t DEFAULT_MAX_SIZE = 500;

	/// @brief コンストラクタ
	/// @param maxSize 最大エントリ数
	explicit VNBacklog(std::size_t maxSize = DEFAULT_MAX_SIZE)
		: m_maxSize(maxSize)
	{
	}

	/// @brief エントリを追加する
	///
	/// 最大数を超えた場合、最も古いエントリが削除される。
	///
	/// @param entry 追加するエントリ
	void addEntry(VNBacklogEntry entry)
	{
		m_entries.push_back(std::move(entry));
		while (m_entries.size() > m_maxSize)
		{
			m_entries.pop_front();
		}
	}

	/// @brief 全エントリを取得する
	/// @return エントリのベクター
	[[nodiscard]] std::vector<VNBacklogEntry> getEntries() const
	{
		return {m_entries.begin(), m_entries.end()};
	}

	/// @brief 最新のN件を取得する
	/// @param count 取得するエントリ数
	/// @return エントリのベクター
	[[nodiscard]] std::vector<VNBacklogEntry> getRecentEntries(std::size_t count) const
	{
		if (count >= m_entries.size())
		{
			return {m_entries.begin(), m_entries.end()};
		}
		const auto start = m_entries.end() - static_cast<std::ptrdiff_t>(count);
		return {start, m_entries.end()};
	}

	/// @brief テキスト内容で検索する
	/// @param keyword 検索キーワード
	/// @return キーワードを含むエントリのベクター
	[[nodiscard]] std::vector<VNBacklogEntry> search(const std::string& keyword) const
	{
		std::vector<VNBacklogEntry> results;
		for (const auto& entry : m_entries)
		{
			if (entry.text.find(keyword) != std::string::npos ||
				entry.speaker.find(keyword) != std::string::npos)
			{
				results.push_back(entry);
			}
		}
		return results;
	}

	/// @brief すべてのエントリを削除する
	void clear()
	{
		m_entries.clear();
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

	/// @brief 最大サイズを取得する
	/// @return 最大エントリ数
	[[nodiscard]] std::size_t maxSize() const noexcept
	{
		return m_maxSize;
	}

private:
	std::deque<VNBacklogEntry> m_entries;  ///< エントリバッファ
	std::size_t m_maxSize;                 ///< 最大エントリ数
};

} // namespace sgc::vn
