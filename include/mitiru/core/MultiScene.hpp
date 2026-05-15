#pragma once

/// @file MultiScene.hpp
/// @brief マルチシーン管理 — 複数のSceneを同時に開いて切り替える
/// @details エディタで複数シーンをタブ形式で開き、アクティブシーンを切り替える。
///          各シーンは独立したSceneインスタンスを持つ。
///
/// @code
/// mitiru::MultiSceneManager mgr;
/// int idx = mgr.openScene("Level1");
/// mgr.setActiveScene(idx);
/// mgr.activeScene().addMesh("Player", "models/player.obj");
/// mgr.saveScene(idx, "scenes/level1.json");
/// @endcode

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <mitiru/core/SceneDocument.hpp>

namespace mitiru
{

// =============================================================================
// SceneEntry — 内部管理用シーンエントリ
// =============================================================================

/// @brief マルチシーンマネージャが管理するシーンエントリ（内部用）
struct SceneEntry
{
	std::string name;                 ///< シーン名
	std::string filePath;             ///< ファイルパス（空 = 未保存）
	std::unique_ptr<Scene> scene;     ///< シーンデータ

	/// @brief デフォルトコンストラクタ
	SceneEntry() = default;

	/// @brief 名前付きコンストラクタ
	explicit SceneEntry(const std::string& sceneName)
		: name(sceneName)
		, scene(std::make_unique<Scene>())
	{
	}
};

// =============================================================================
// MultiSceneManager — 複数シーンの管理
// =============================================================================

/// @brief マルチシーンマネージャ
/// @details 複数のSceneを同時に開き、アクティブシーンを切り替える。
///          エディタのタブUI連携を前提とした設計。
class MultiSceneManager
{
public:
	// ── シーンの開閉 ──

	/// @brief 新しい空シーンを開く
	/// @param name シーン名
	/// @return シーンインデックス
	int openScene(const std::string& name)
	{
		auto entry = std::make_unique<SceneEntry>(name);
		m_scenes.push_back(std::move(entry));
		const int idx = static_cast<int>(m_scenes.size()) - 1;

		// 最初のシーンならアクティブにする
		if (m_scenes.size() == 1)
		{
			m_activeIndex = 0;
		}

		return idx;
	}

	/// @brief シーンを閉じる
	/// @param index シーンインデックス
	void closeScene(int index)
	{
		if (!isValidIndex(index)) return;

		m_scenes.erase(m_scenes.begin() + index);

		// アクティブインデックスを調整する
		if (m_scenes.empty())
		{
			m_activeIndex = -1;
		}
		else if (m_activeIndex >= static_cast<int>(m_scenes.size()))
		{
			m_activeIndex = static_cast<int>(m_scenes.size()) - 1;
		}
		else if (m_activeIndex > index)
		{
			--m_activeIndex;
		}
		else if (m_activeIndex == index)
		{
			m_activeIndex = std::min(m_activeIndex,
			                         static_cast<int>(m_scenes.size()) - 1);
		}
	}

	// ── アクティブシーン ──

	/// @brief アクティブシーンを設定する
	/// @param index シーンインデックス
	void setActiveScene(int index)
	{
		if (isValidIndex(index))
		{
			m_activeIndex = index;
		}
	}

	/// @brief アクティブシーンのインデックスを取得する
	/// @return インデックス（シーンが無い場合 -1）
	[[nodiscard]] int activeSceneIndex() const noexcept
	{
		return m_activeIndex;
	}

	/// @brief アクティブシーンへの参照を取得する
	/// @return アクティブシーンの参照
	/// @pre シーンが1つ以上開かれていること
	[[nodiscard]] Scene& activeScene()
	{
		return *m_scenes[static_cast<std::size_t>(m_activeIndex)]->scene;
	}

	/// @brief アクティブシーンへの参照を取得する（const版）
	[[nodiscard]] const Scene& activeScene() const
	{
		return *m_scenes[static_cast<std::size_t>(m_activeIndex)]->scene;
	}

	/// @brief アクティブシーンへのポインタを取得する
	/// @return シーンが無い場合 nullptr
	[[nodiscard]] Scene* activeScenePtr() noexcept
	{
		if (m_activeIndex < 0) return nullptr;
		return m_scenes[static_cast<std::size_t>(m_activeIndex)]->scene.get();
	}

	// ── シーンアクセス ──

	/// @brief インデックスでシーンを取得する
	[[nodiscard]] Scene& sceneAt(int index)
	{
		return *m_scenes[static_cast<std::size_t>(index)]->scene;
	}

	/// @brief インデックスでシーンを取得する（const版）
	[[nodiscard]] const Scene& sceneAt(int index) const
	{
		return *m_scenes[static_cast<std::size_t>(index)]->scene;
	}

	/// @brief シーン数を取得する
	[[nodiscard]] int sceneCount() const noexcept
	{
		return static_cast<int>(m_scenes.size());
	}

	/// @brief シーンが開かれていないか判定する
	[[nodiscard]] bool empty() const noexcept
	{
		return m_scenes.empty();
	}

	/// @brief シーン名を取得する
	[[nodiscard]] const std::string& sceneName(int index) const
	{
		return m_scenes[static_cast<std::size_t>(index)]->name;
	}

	/// @brief シーンのファイルパスを取得する
	[[nodiscard]] const std::string& scenePath(int index) const
	{
		return m_scenes[static_cast<std::size_t>(index)]->filePath;
	}

	// ── シーン操作 ──

	/// @brief シーン名を変更する
	void renameScene(int index, const std::string& name)
	{
		if (isValidIndex(index))
		{
			m_scenes[static_cast<std::size_t>(index)]->name = name;
		}
	}

	/// @brief シーンを複製する
	/// @param index 複製元のインデックス
	/// @return 新しいシーンのインデックス
	int duplicateScene(int index)
	{
		if (!isValidIndex(index)) return -1;

		const auto& src = m_scenes[static_cast<std::size_t>(index)];
		const auto newName = src->name + "_copy";

		auto entry = std::make_unique<SceneEntry>(newName);

		// シーン内容をJSON経由でコピーする
		const auto json = src->scene->toJson();
		entry->scene->fromJson(json);

		m_scenes.push_back(std::move(entry));
		return static_cast<int>(m_scenes.size()) - 1;
	}

	/// @brief シーンが変更されているか判定する
	[[nodiscard]] bool isSceneDirty(int index) const
	{
		if (!isValidIndex(index)) return false;
		return m_scenes[static_cast<std::size_t>(index)]->scene->isDirty();
	}

	// ── 保存・読み込み ──

	/// @brief シーンをファイルに保存する
	/// @param index シーンインデックス
	/// @param path ファイルパス
	/// @return 成功時 true
	bool saveScene(int index, const std::string& path)
	{
		if (!isValidIndex(index)) return false;

		auto& entry = m_scenes[static_cast<std::size_t>(index)];
		const bool ok = entry->scene->saveToFile(path);
		if (ok)
		{
			entry->filePath = path;
			entry->scene->clearDirty();
		}
		return ok;
	}

	/// @brief ファイルからシーンをロードして新規タブで開く
	/// @param path ファイルパス
	/// @return シーンインデックス（失敗時 -1）
	int loadScene(const std::string& path)
	{
		auto entry = std::make_unique<SceneEntry>();
		entry->scene = std::make_unique<Scene>();
		if (!entry->scene->loadFromFile(path))
		{
			return -1;
		}

		// ファイル名からシーン名を推定する
		entry->filePath = path;
		entry->name = extractFileName(path);

		m_scenes.push_back(std::move(entry));
		const int idx = static_cast<int>(m_scenes.size()) - 1;

		if (m_scenes.size() == 1)
		{
			m_activeIndex = 0;
		}

		return idx;
	}

	/// @brief 全シーンをディレクトリに保存する
	/// @param dirPath ディレクトリパス
	void saveAll(const std::string& dirPath)
	{
		for (std::size_t i = 0; i < m_scenes.size(); ++i)
		{
			auto& entry = m_scenes[i];
			const auto path = entry->filePath.empty()
				? dirPath + "/" + entry->name + ".json"
				: entry->filePath;
			entry->scene->saveToFile(path);
			entry->filePath = path;
			entry->scene->clearDirty();
		}
	}

	/// @brief 名前でシーンを検索する
	/// @return インデックス（見つからなければ -1）
	[[nodiscard]] int findByName(const std::string& name) const
	{
		for (std::size_t i = 0; i < m_scenes.size(); ++i)
		{
			if (m_scenes[i]->name == name)
			{
				return static_cast<int>(i);
			}
		}
		return -1;
	}

private:
	std::vector<std::unique_ptr<SceneEntry>> m_scenes; ///< 開いているシーン
	int m_activeIndex = -1;                             ///< アクティブシーン

	/// @brief 有効なインデックスか判定する
	[[nodiscard]] bool isValidIndex(int index) const noexcept
	{
		return index >= 0 && index < static_cast<int>(m_scenes.size());
	}

	/// @brief ファイルパスからファイル名を抽出する（拡張子なし）
	static std::string extractFileName(const std::string& path)
	{
		auto slashPos = path.find_last_of("/\\");
		auto name = (slashPos != std::string::npos)
			? path.substr(slashPos + 1)
			: path;

		auto dotPos = name.find_last_of('.');
		if (dotPos != std::string::npos)
		{
			name = name.substr(0, dotPos);
		}
		return name;
	}
};

} // namespace mitiru
