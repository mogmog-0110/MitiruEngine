#pragma once

/// @file AssetBrowser.hpp
/// @brief アセットブラウザーデータモデル
/// @details ファイルシステムに依存しない抽象的なアセット一覧管理を提供する。
///          テクスチャ、メッシュ、オーディオ、スクリプト、シーンの分類に対応する。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru::editor
{

/// @brief アセット種別
enum class AssetType : std::uint8_t
{
	Texture,  ///< テクスチャ
	Mesh,     ///< メッシュ
	Audio,    ///< オーディオ
	Script,   ///< スクリプト
	Scene,    ///< シーン
	Unknown,  ///< 不明
};

/// @brief アセットエントリ情報
/// @details 1つのアセットファイルのパス、種別、サイズ、更新日時を保持する。
struct AssetEntry
{
	std::string path;                ///< ファイルパス
	AssetType type = AssetType::Unknown; ///< アセット種別
	std::uint64_t sizeBytes = 0;     ///< ファイルサイズ（バイト）
	std::uint64_t lastModified = 0;  ///< 最終更新タイムスタンプ
	bool isDirectory = false;        ///< ディレクトリか
};

/// @brief アセットブラウザー
/// @details アセットエントリの一覧管理、ナビゲーション、フィルタリング、検索を提供する。
///          実際のファイルシステムには依存せず、vector<AssetEntry>を入力として受け取る。
///
/// @code
/// mitiru::editor::AssetBrowser browser;
/// browser.setRootPath("/assets");
/// browser.setEntries({ ... });
/// browser.navigate("textures");
/// auto filtered = browser.getFilteredEntries();
/// @endcode
class AssetBrowser
{
public:
	/// @brief ルートパスを設定する
	/// @param rootPath アセットルートパス
	void setRootPath(std::string_view rootPath)
	{
		m_rootPath = std::string(rootPath);
		m_currentPath = m_rootPath;
	}

	/// @brief アセットエントリ一覧を設定する
	/// @param entries アセットエントリのリスト
	void setEntries(std::vector<AssetEntry> entries)
	{
		m_entries = std::move(entries);
	}

	/// @brief 現在のパスを取得する
	/// @return 現在のディレクトリパス
	[[nodiscard]] const std::string& currentPath() const noexcept
	{
		return m_currentPath;
	}

	/// @brief ルートパスを取得する
	/// @return ルートディレクトリパス
	[[nodiscard]] const std::string& rootPath() const noexcept
	{
		return m_rootPath;
	}

	/// @brief 全アセットエントリを取得する
	/// @return 設定されているアセットエントリのリスト
	[[nodiscard]] const std::vector<AssetEntry>& entries() const noexcept
	{
		return m_entries;
	}

	/// @brief サブディレクトリに移動する
	/// @param subdir サブディレクトリ名
	void navigate(std::string_view subdir)
	{
		if (m_currentPath.empty() || m_currentPath.back() == '/')
		{
			m_currentPath += std::string(subdir);
		}
		else
		{
			m_currentPath += "/" + std::string(subdir);
		}
	}

	/// @brief 親ディレクトリに戻る
	/// @return 移動に成功した場合 true（ルートの場合はfalse）
	bool goUp()
	{
		if (m_currentPath == m_rootPath || m_currentPath.empty())
		{
			return false;
		}

		const auto pos = m_currentPath.find_last_of('/');
		if (pos == std::string::npos || pos == 0)
		{
			m_currentPath = m_rootPath;
		}
		else
		{
			m_currentPath = m_currentPath.substr(0, pos);
		}

		/// ルートより上には行かない
		if (m_currentPath.size() < m_rootPath.size())
		{
			m_currentPath = m_rootPath;
		}

		return true;
	}

	/// @brief アセット種別フィルターを設定する
	/// @param type フィルターする種別
	void setTypeFilter(AssetType type) noexcept
	{
		m_typeFilter = type;
		m_hasTypeFilter = true;
	}

	/// @brief 種別フィルターを解除する
	void clearTypeFilter() noexcept
	{
		m_hasTypeFilter = false;
	}

	/// @brief 検索クエリを設定する
	/// @param query 検索文字列（部分一致）
	void setSearchQuery(std::string_view query)
	{
		m_searchQuery = std::string(query);
	}

	/// @brief 検索クエリを取得する
	/// @return 現在の検索クエリ
	[[nodiscard]] const std::string& searchQuery() const noexcept
	{
		return m_searchQuery;
	}

	/// @brief フィルタリング・検索適用後のエントリ一覧を取得する
	/// @return フィルター済みのアセットエントリリスト
	[[nodiscard]] std::vector<AssetEntry> getFilteredEntries() const
	{
		std::vector<AssetEntry> result;

		for (const auto& entry : m_entries)
		{
			if (!matchesCurrentPath(entry))
			{
				continue;
			}
			if (m_hasTypeFilter && entry.type != m_typeFilter)
			{
				continue;
			}
			if (!m_searchQuery.empty() && !matchesSearch(entry))
			{
				continue;
			}
			result.push_back(entry);
		}

		return result;
	}

	/// @brief 現在のパスにあるエントリ数を取得する
	/// @return フィルター済みエントリ数
	[[nodiscard]] std::size_t filteredCount() const
	{
		return getFilteredEntries().size();
	}

private:
	/// @brief エントリが現在のパスに含まれるか判定する
	/// @param entry チェック対象のエントリ
	/// @return 現在のパスに含まれる場合 true
	[[nodiscard]] bool matchesCurrentPath(const AssetEntry& entry) const
	{
		if (m_currentPath.empty())
		{
			return true;
		}
		return entry.path.find(m_currentPath) == 0;
	}

	/// @brief エントリが検索クエリに一致するか判定する
	/// @param entry チェック対象のエントリ
	/// @return 一致する場合 true
	[[nodiscard]] bool matchesSearch(const AssetEntry& entry) const
	{
		return entry.path.find(m_searchQuery) != std::string::npos;
	}

	std::string m_rootPath;          ///< ルートパス
	std::string m_currentPath;       ///< 現在のパス
	std::vector<AssetEntry> m_entries; ///< アセットエントリ一覧
	AssetType m_typeFilter = AssetType::Unknown; ///< 種別フィルター
	bool m_hasTypeFilter = false;     ///< 種別フィルターが有効か
	std::string m_searchQuery;        ///< 検索クエリ
};

} // namespace mitiru::editor
