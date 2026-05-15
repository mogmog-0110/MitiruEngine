#pragma once

/// @file CGGallery.hpp
/// @brief CGギャラリーとシーンリプレイシステム
/// @details ゲーム中に表示されたCGの鑑賞モードと、特定シーンの再生機能を提供する。
///          アンロック状態の追跡、カテゴリ分類、全画面ビューア、
///          複数バリアントのブラウジング、直列化をサポートする。
///
/// @code
/// mitiru::vn::CGGallery gallery;
/// gallery.addEntry({"cg_001", "tex_sakura_01", "seen_sakura_route", false,
///                    "Chapter1", "thumb_sakura_01"});
/// gallery.markSeen("cg_001");
///
/// float pct = gallery.completionPercentage();
/// std::string json = gallery.toJson();
/// @endcode

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace mitiru::vn
{

// ── レイアウト設定 ──────────────────────────────────────────────

/// @brief CGギャラリーのレイアウト設定
struct CGGalleryLayout
{
	int columnsPerRow = 4;            ///< 1行あたりの列数
	int rowsPerPage = 3;              ///< 1ページあたりの行数
	float thumbnailWidth = 160.0f;    ///< サムネイル幅
	float thumbnailHeight = 100.0f;   ///< サムネイル高さ
	float thumbnailSpacing = 12.0f;   ///< サムネイル間隔
	float categoryTabHeight = 32.0f;  ///< カテゴリタブの高さ
	float lockIconSize = 32.0f;       ///< ロックアイコンのサイズ

	/// @brief 1ページあたりの項目数
	[[nodiscard]] int itemsPerPage() const noexcept
	{
		return columnsPerRow * rowsPerPage;
	}
};

// ── CGエントリ ──────────────────────────────────────────────────

/// @brief CGギャラリーの1項目
struct CGEntry
{
	std::string id;               ///< CG識別子
	std::string textureId;        ///< テクスチャ識別子
	std::string unlockCondition;  ///< アンロック条件（FlagManager式）
	bool isUnlocked = false;      ///< アンロック済みか
	std::string category;         ///< カテゴリ名
	std::string thumbnailId;      ///< サムネイルテクスチャID
	std::vector<std::string> variants; ///< バリアント（差分CG）のテクスチャID群
};

// ── CGビューア状態 ──────────────────────────────────────────────

/// @brief 全画面CGビューアの操作状態
struct CGViewerState
{
	std::string currentCgId;        ///< 表示中のCG ID
	std::uint32_t variantIndex = 0; ///< 現在のバリアントインデックス
	float zoomLevel = 1.0f;         ///< ズーム倍率
	float panX = 0.0f;             ///< パン位置X
	float panY = 0.0f;             ///< パン位置Y
	bool isOpen = false;           ///< ビューアが開いているか

	/// @brief ビューアを開く
	/// @param cgId 表示するCG ID
	void open(const std::string& cgId) noexcept
	{
		currentCgId = cgId;
		variantIndex = 0;
		zoomLevel = 1.0f;
		panX = 0.0f;
		panY = 0.0f;
		isOpen = true;
	}

	/// @brief ビューアを閉じる
	void close() noexcept
	{
		isOpen = false;
		zoomLevel = 1.0f;
		panX = 0.0f;
		panY = 0.0f;
	}

	/// @brief ズームを適用する
	/// @param delta ズーム差分（正=拡大、負=縮小）
	void applyZoom(float delta) noexcept
	{
		zoomLevel = std::clamp(zoomLevel + delta, 0.5f, 4.0f);
	}

	/// @brief パンを適用する
	/// @param dx X方向移動量
	/// @param dy Y方向移動量
	void applyPan(float dx, float dy) noexcept
	{
		panX += dx;
		panY += dy;
	}
};

// ── CGギャラリー ────────────────────────────────────────────────

/// @brief CGギャラリー管理クラス
/// @details CG項目の登録、アンロック追跡、カテゴリ分類、統計を提供する。
class CGGallery
{
	std::vector<CGEntry> m_entries;
	std::unordered_map<std::string, std::size_t> m_idIndex; ///< ID→インデックスマップ
	CGViewerState m_viewer;
	CGGalleryLayout m_layout;

public:
	// ── 登録 ────────────────────────────────────────────────

	/// @brief CGを登録する
	/// @param entry CG項目
	void addEntry(CGEntry entry)
	{
		const auto id = entry.id;
		m_idIndex[id] = m_entries.size();
		m_entries.push_back(std::move(entry));
	}

	/// @brief CGを一括登録する
	/// @param entries CG項目群
	void addEntries(std::vector<CGEntry> entries)
	{
		for (auto& entry : entries)
		{
			addEntry(std::move(entry));
		}
	}

	// ── アンロック ──────────────────────────────────────────

	/// @brief CGをアンロック済みにマークする
	/// @param id CG ID
	/// @return 新たにアンロックされたならtrue
	bool markSeen(const std::string& id)
	{
		auto* entry = findEntry(id);
		if (!entry || entry->isUnlocked) { return false; }
		entry->isUnlocked = true;
		return true;
	}

	/// @brief CGがアンロック済みか確認する
	/// @param id CG ID
	/// @return アンロック済みならtrue
	[[nodiscard]] bool isUnlocked(const std::string& id) const
	{
		const auto* entry = findEntry(id);
		return entry && entry->isUnlocked;
	}

	// ── 参照 ────────────────────────────────────────────────

	/// @brief 全エントリを取得する
	[[nodiscard]] const std::vector<CGEntry>& entries() const noexcept
	{
		return m_entries;
	}

	/// @brief IDでエントリを取得する
	/// @param id CG ID
	/// @return エントリ（存在しなければnullopt）
	[[nodiscard]] std::optional<CGEntry> getEntry(const std::string& id) const
	{
		const auto* entry = findEntry(id);
		if (!entry) { return std::nullopt; }
		return *entry;
	}

	/// @brief カテゴリ一覧を取得する
	[[nodiscard]] std::vector<std::string> categories() const
	{
		std::vector<std::string> result;
		for (const auto& entry : m_entries)
		{
			if (std::find(result.begin(), result.end(), entry.category) == result.end())
			{
				result.push_back(entry.category);
			}
		}
		return result;
	}

	/// @brief 指定カテゴリのエントリを取得する
	/// @param category カテゴリ名
	[[nodiscard]] std::vector<const CGEntry*> entriesByCategory(const std::string& category) const
	{
		std::vector<const CGEntry*> result;
		for (const auto& entry : m_entries)
		{
			if (entry.category == category)
			{
				result.push_back(&entry);
			}
		}
		return result;
	}

	// ── 統計 ────────────────────────────────────────────────

	/// @brief 総エントリ数
	[[nodiscard]] std::size_t totalCount() const noexcept
	{
		return m_entries.size();
	}

	/// @brief アンロック済み数
	[[nodiscard]] std::size_t unlockedCount() const noexcept
	{
		return static_cast<std::size_t>(
			std::count_if(m_entries.begin(), m_entries.end(),
				[](const CGEntry& e) { return e.isUnlocked; }));
	}

	/// @brief 総合完了率 [0,100]
	[[nodiscard]] float completionPercentage() const noexcept
	{
		if (m_entries.empty()) { return 100.0f; }
		return static_cast<float>(unlockedCount()) / static_cast<float>(m_entries.size()) * 100.0f;
	}

	/// @brief カテゴリ別完了率 [0,100]
	/// @param category カテゴリ名
	[[nodiscard]] float completionPercentage(const std::string& category) const
	{
		std::size_t total = 0;
		std::size_t unlocked = 0;
		for (const auto& entry : m_entries)
		{
			if (entry.category == category)
			{
				++total;
				if (entry.isUnlocked) { ++unlocked; }
			}
		}
		if (total == 0) { return 100.0f; }
		return static_cast<float>(unlocked) / static_cast<float>(total) * 100.0f;
	}

	// ── ビューア ────────────────────────────────────────────

	/// @brief ビューア状態への参照
	[[nodiscard]] CGViewerState& viewer() noexcept { return m_viewer; }
	[[nodiscard]] const CGViewerState& viewer() const noexcept { return m_viewer; }

	// ── レイアウト設定 ──────────────────────────────────────

	/// @brief レイアウト設定を設定する
	/// @param layout レイアウト設定
	void setLayout(CGGalleryLayout layout) noexcept
	{
		m_layout = layout;
	}

	/// @brief レイアウト設定への参照
	[[nodiscard]] CGGalleryLayout& layout() noexcept { return m_layout; }
	[[nodiscard]] const CGGalleryLayout& layout() const noexcept { return m_layout; }

	/// @brief 指定ページのエントリ範囲を取得する
	/// @param page ページ番号（0始まり）
	/// @return 該当ページのエントリ群
	[[nodiscard]] std::vector<const CGEntry*> entriesForPage(std::size_t page) const
	{
		std::vector<const CGEntry*> result;
		const auto perPage = static_cast<std::size_t>(m_layout.itemsPerPage());
		const auto start = page * perPage;
		const auto end = std::min(start + perPage, m_entries.size());
		for (std::size_t i = start; i < end; ++i)
		{
			result.push_back(&m_entries[i]);
		}
		return result;
	}

	/// @brief 総ページ数を取得する
	[[nodiscard]] std::size_t pageCount() const noexcept
	{
		const auto perPage = static_cast<std::size_t>(m_layout.itemsPerPage());
		if (perPage == 0 || m_entries.empty()) { return 0; }
		return (m_entries.size() + perPage - 1) / perPage;
	}

	/// @brief CGビューアを開く
	/// @param id CG ID
	/// @return 正常に開けたらtrue
	bool openViewer(const std::string& id)
	{
		const auto* entry = findEntry(id);
		if (!entry || !entry->isUnlocked) { return false; }
		m_viewer.open(id);
		return true;
	}

	/// @brief ビューアで次のバリアントを表示する
	/// @return 切り替えに成功したらtrue
	bool nextVariant()
	{
		if (!m_viewer.isOpen) { return false; }
		const auto* entry = findEntry(m_viewer.currentCgId);
		if (!entry) { return false; }
		const auto maxIndex = static_cast<std::uint32_t>(entry->variants.size());
		if (m_viewer.variantIndex + 1 <= maxIndex)
		{
			++m_viewer.variantIndex;
			return true;
		}
		return false;
	}

	/// @brief ビューアで前のバリアントを表示する
	/// @return 切り替えに成功したらtrue
	bool prevVariant()
	{
		if (!m_viewer.isOpen || m_viewer.variantIndex == 0) { return false; }
		--m_viewer.variantIndex;
		return true;
	}

	/// @brief ビューアで現在表示中のテクスチャIDを取得する
	[[nodiscard]] std::string currentViewerTexture() const
	{
		if (!m_viewer.isOpen) { return ""; }
		const auto* entry = findEntry(m_viewer.currentCgId);
		if (!entry) { return ""; }
		if (m_viewer.variantIndex == 0)
		{
			return entry->textureId;
		}
		const auto vi = m_viewer.variantIndex - 1;
		if (vi < entry->variants.size())
		{
			return entry->variants[vi];
		}
		return entry->textureId;
	}

	// ── 直列化 ──────────────────────────────────────────────

	/// @brief アンロック状態をJSON文字列に出力する
	[[nodiscard]] std::string toJson() const
	{
		std::string json = "{\"unlocked\":[";
		bool first = true;
		for (const auto& entry : m_entries)
		{
			if (entry.isUnlocked)
			{
				if (!first) { json += ","; }
				json += "\"" + escapeJson(entry.id) + "\"";
				first = false;
			}
		}
		json += "]}";
		return json;
	}

	/// @brief アンロック状態をJSON文字列から復元する
	/// @param json JSON文字列
	/// @return 成功ならtrue
	bool fromJson(std::string_view json)
	{
		// 全エントリのアンロック状態をリセット
		for (auto& entry : m_entries)
		{
			entry.isUnlocked = false;
		}

		// "unlocked":[...] を解析
		auto arrayStart = json.find('[');
		auto arrayEnd = json.rfind(']');
		if (arrayStart == std::string_view::npos || arrayEnd == std::string_view::npos)
		{
			return false;
		}

		auto content = json.substr(arrayStart + 1, arrayEnd - arrayStart - 1);
		std::size_t pos = 0;
		while (pos < content.size())
		{
			auto quoteStart = content.find('"', pos);
			if (quoteStart == std::string_view::npos) { break; }
			auto quoteEnd = content.find('"', quoteStart + 1);
			if (quoteEnd == std::string_view::npos) { break; }
			auto id = std::string(content.substr(quoteStart + 1, quoteEnd - quoteStart - 1));
			markSeen(id);
			pos = quoteEnd + 1;
		}

		return true;
	}

private:
	[[nodiscard]] CGEntry* findEntry(const std::string& id)
	{
		auto it = m_idIndex.find(id);
		if (it == m_idIndex.end()) { return nullptr; }
		return &m_entries[it->second];
	}

	[[nodiscard]] const CGEntry* findEntry(const std::string& id) const
	{
		auto it = m_idIndex.find(id);
		if (it == m_idIndex.end()) { return nullptr; }
		return &m_entries[it->second];
	}

	[[nodiscard]] static std::string escapeJson(const std::string& s)
	{
		std::string result;
		result.reserve(s.size() + 4);
		for (char c : s)
		{
			switch (c)
			{
			case '"':  result += "\\\""; break;
			case '\\': result += "\\\\"; break;
			default:   result += c; break;
			}
		}
		return result;
	}
};

// ── シーンリプレイ ──────────────────────────────────────────────

/// @brief シーンリプレイの1項目
struct SceneReplayEntry
{
	std::string id;               ///< シーン識別子
	std::string title;            ///< 表示タイトル
	std::string chapterName;      ///< 所属チャプター名
	std::string scriptPosition;   ///< スクリプト上の再生開始位置
	std::string unlockCondition;  ///< アンロック条件
	bool isUnlocked = false;      ///< アンロック済みか
};

/// @brief シーンリプレイ管理クラス
/// @details ゲーム中の特定シーンを再度閲覧するための管理機能を提供する。
class SceneReplayManager
{
	std::vector<SceneReplayEntry> m_entries;
	std::unordered_map<std::string, std::size_t> m_idIndex;

public:
	/// @brief シーンを登録する
	/// @param entry シーン項目
	void addEntry(SceneReplayEntry entry)
	{
		const auto id = entry.id;
		m_idIndex[id] = m_entries.size();
		m_entries.push_back(std::move(entry));
	}

	/// @brief シーンをアンロック済みにマークする
	/// @param id シーンID
	/// @return 新たにアンロックされたならtrue
	bool markSeen(const std::string& id)
	{
		auto it = m_idIndex.find(id);
		if (it == m_idIndex.end()) { return false; }
		auto& entry = m_entries[it->second];
		if (entry.isUnlocked) { return false; }
		entry.isUnlocked = true;
		return true;
	}

	/// @brief シーンがアンロック済みか確認する
	[[nodiscard]] bool isUnlocked(const std::string& id) const
	{
		auto it = m_idIndex.find(id);
		if (it == m_idIndex.end()) { return false; }
		return m_entries[it->second].isUnlocked;
	}

	/// @brief 再生開始位置を取得する
	/// @param id シーンID
	/// @return スクリプト位置（存在しなければ空文字列）
	[[nodiscard]] std::string getScriptPosition(const std::string& id) const
	{
		auto it = m_idIndex.find(id);
		if (it == m_idIndex.end()) { return ""; }
		return m_entries[it->second].scriptPosition;
	}

	/// @brief 全エントリを取得する
	[[nodiscard]] const std::vector<SceneReplayEntry>& entries() const noexcept
	{
		return m_entries;
	}

	/// @brief チャプター別にエントリを取得する
	/// @param chapter チャプター名
	[[nodiscard]] std::vector<const SceneReplayEntry*> entriesByChapter(const std::string& chapter) const
	{
		std::vector<const SceneReplayEntry*> result;
		for (const auto& entry : m_entries)
		{
			if (entry.chapterName == chapter)
			{
				result.push_back(&entry);
			}
		}
		return result;
	}

	/// @brief アンロック済みシーン数
	[[nodiscard]] std::size_t unlockedCount() const noexcept
	{
		return static_cast<std::size_t>(
			std::count_if(m_entries.begin(), m_entries.end(),
				[](const SceneReplayEntry& e) { return e.isUnlocked; }));
	}

	/// @brief 完了率 [0,100]
	[[nodiscard]] float completionPercentage() const noexcept
	{
		if (m_entries.empty()) { return 100.0f; }
		return static_cast<float>(unlockedCount()) / static_cast<float>(m_entries.size()) * 100.0f;
	}

	/// @brief アンロック状態をJSON文字列に出力する
	[[nodiscard]] std::string toJson() const
	{
		std::string json = "{\"unlocked\":[";
		bool first = true;
		for (const auto& entry : m_entries)
		{
			if (entry.isUnlocked)
			{
				if (!first) { json += ","; }
				json += "\"" + entry.id + "\"";
				first = false;
			}
		}
		json += "]}";
		return json;
	}

	/// @brief アンロック状態をJSON文字列から復元する
	/// @param json JSON文字列
	/// @return 成功ならtrue
	bool fromJson(std::string_view json)
	{
		for (auto& entry : m_entries)
		{
			entry.isUnlocked = false;
		}

		auto arrayStart = json.find('[');
		auto arrayEnd = json.rfind(']');
		if (arrayStart == std::string_view::npos || arrayEnd == std::string_view::npos)
		{
			return false;
		}

		auto content = json.substr(arrayStart + 1, arrayEnd - arrayStart - 1);
		std::size_t pos = 0;
		while (pos < content.size())
		{
			auto quoteStart = content.find('"', pos);
			if (quoteStart == std::string_view::npos) { break; }
			auto quoteEnd = content.find('"', quoteStart + 1);
			if (quoteEnd == std::string_view::npos) { break; }
			auto id = std::string(content.substr(quoteStart + 1, quoteEnd - quoteStart - 1));
			markSeen(id);
			pos = quoteEnd + 1;
		}

		return true;
	}
};

} // namespace mitiru::vn
