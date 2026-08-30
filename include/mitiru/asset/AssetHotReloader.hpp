#pragma once

/// @file AssetHotReloader.hpp
/// @brief 汎用アセットホットリロードシステム
/// @details FileWatcher を利用して複数カテゴリのアセット（シェーダー、UIテンプレート等）を
///          監視し、変更検知時にカテゴリ別コールバックを呼び出す。
///          実際の再コンパイルやリロード処理はコールバック側の責務。
///
/// @code
/// mitiru::asset::AssetHotReloader reloader;
///
/// // シェーダー監視
/// reloader.watchShader("shaders/main.vert",
///     [&](const std::string& path, const std::string& content) {
///         pipeline.recompileShader(path, content);
///     });
///
/// // UIテンプレート監視
/// reloader.watchUiTemplate("ui/main_menu.rml",
///     [&](const std::string& path, const std::string& content) {
///         rmlContext->ReloadDocument(path);
///     });
///
/// // メインループ内で:
/// reloader.update();
/// @endcode

#include "FileWatcher.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace mitiru::asset
{

/// @brief アセットカテゴリ
enum class AssetCategory : std::uint8_t
{
	Shader,      ///< シェーダーファイル (.hlsl, .glsl, .vert, .frag, .comp)
	UiTemplate,  ///< UIテンプレート (.rml, .rcss)
	Scenario,    ///< シナリオスクリプト (.vns)
	Custom,      ///< ユーザー定義カテゴリ
};

/// @brief アセットリロードイベント
struct AssetReloadEvent
{
	std::string path;                           ///< ファイルパス
	AssetCategory category{AssetCategory::Custom}; ///< カテゴリ
	bool success{false};                        ///< リロード成功フラグ
	std::string message;                        ///< 結果メッセージ
	std::chrono::steady_clock::time_point timestamp{
		std::chrono::steady_clock::now()};      ///< タイムスタンプ
};

/// @brief アセット変更コールバック型（パスと新しいファイル内容を受け取る）
using AssetChangedCallback = std::function<void(const std::string& path, const std::string& newContent)>;

/// @brief 汎用アセットホットリロードコーディネーター
/// @details FileWatcher でファイルを監視し、変更時にカテゴリ別コールバックを呼び出す。
///          フェイルセーフ: コールバック内で例外が発生しても他のアセット監視は継続する。
class AssetHotReloader
{
public:
	AssetHotReloader() = default;

	// コピー禁止
	AssetHotReloader(const AssetHotReloader&) = delete;
	AssetHotReloader& operator=(const AssetHotReloader&) = delete;

	/// @brief シェーダーファイルを監視登録する
	/// @param filepath シェーダーファイルパス (.hlsl, .glsl, .vert, .frag, .comp)
	/// @param onChanged 変更時コールバック（パスと新しいソースコードを受け取る）
	void watchShader(const std::string& filepath, AssetChangedCallback onChanged)
	{
		watchAsset(filepath, AssetCategory::Shader, std::move(onChanged));
	}

	/// @brief UIテンプレートファイルを監視登録する
	/// @param filepath UIテンプレートファイルパス (.rml, .rcss)
	/// @param onChanged 変更時コールバック（パスと新しい内容を受け取る）
	void watchUiTemplate(const std::string& filepath, AssetChangedCallback onChanged)
	{
		watchAsset(filepath, AssetCategory::UiTemplate, std::move(onChanged));
	}

	/// @brief シナリオファイルを監視登録する
	/// @param filepath シナリオファイルパス (.vns)
	/// @param onChanged 変更時コールバック
	void watchScenario(const std::string& filepath, AssetChangedCallback onChanged)
	{
		watchAsset(filepath, AssetCategory::Scenario, std::move(onChanged));
	}

	/// @brief カスタムカテゴリのファイルを監視登録する
	/// @param filepath ファイルパス
	/// @param onChanged 変更時コールバック
	void watchCustom(const std::string& filepath, AssetChangedCallback onChanged)
	{
		watchAsset(filepath, AssetCategory::Custom, std::move(onChanged));
	}

	/// @brief 指定ディレクトリ内の特定カテゴリファイルを一括監視する
	/// @param directory ディレクトリパス
	/// @param category カテゴリ
	/// @param onChanged 変更時コールバック
	void watchDirectory(const std::string& directory, AssetCategory category,
	                    AssetChangedCallback onChanged)
	{
		std::error_code ec;
		const auto dirPath = std::filesystem::path(directory);
		if (!std::filesystem::is_directory(dirPath, ec)) return;

		for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec))
		{
			if (!entry.is_regular_file()) continue;
			const auto ext = entry.path().extension().string();
			if (matchesCategory(ext, category))
			{
				watchAsset(entry.path().string(), category, onChanged);
			}
		}
	}

	/// @brief ファイルの監視を解除する
	/// @param filepath 解除するファイルパス
	void unwatch(const std::string& filepath)
	{
		std::scoped_lock lock(m_mutex);
		m_assets.erase(filepath);
	}

	/// @brief 全監視を解除する
	void unwatchAll()
	{
		std::scoped_lock lock(m_mutex);
		m_assets.clear();
		m_watchedDirs.clear();
		m_watcher.unwatchAll();
		// pending events もクリア
		std::queue<FileEvent> empty;
		{
			std::scoped_lock plock(m_pendingMutex);
			std::swap(m_pendingEvents, empty);
		}
	}

	/// @brief フレーム更新。保留中のリロードを処理する
	/// @details メインループから毎フレーム呼び出す。
	void update()
	{
		// FileWatcher のポーリング
		m_watcher.poll();

		// 保留イベントを処理
		std::queue<FileEvent> events;
		{
			std::scoped_lock lock(m_pendingMutex);
			std::swap(events, m_pendingEvents);
		}

		std::scoped_lock lock(m_mutex);

		while (!events.empty())
		{
			const auto event = std::move(events.front());
			events.pop();

			// 変更・作成イベントのみ処理
			if (event.type == FileEventType::Deleted) continue;

			processFileChange(event.path);
		}

		// ポーリングフォールバック: 登録済みアセットを直接チェック
		for (auto& [filepath, entry] : m_assets)
		{
			std::error_code ec;
			if (!std::filesystem::exists(filepath, ec)) continue;

			const auto content = readFile(filepath);
			if (content.empty() || content == entry.lastContent) continue;

			reloadAsset(filepath, entry, content);
		}
	}

	/// @brief 強制リロードを実行する
	/// @param filepath リロードするファイルパス
	/// @return 成功ならtrue
	bool forceReload(const std::string& filepath)
	{
		std::scoped_lock lock(m_mutex);

		auto it = m_assets.find(filepath);
		if (it == m_assets.end()) return false;

		const auto content = readFile(filepath);
		if (content.empty()) return false;

		return reloadAsset(filepath, it->second, content);
	}

	/// @brief 登録済みアセット数を取得する
	[[nodiscard]] std::size_t assetCount() const noexcept
	{
		std::scoped_lock lock(m_mutex);
		return m_assets.size();
	}

	/// @brief カテゴリ別の登録済みアセット数を取得する
	[[nodiscard]] std::size_t assetCount(AssetCategory category) const noexcept
	{
		std::scoped_lock lock(m_mutex);
		std::size_t count = 0;
		for (const auto& [path, entry] : m_assets)
		{
			if (entry.category == category) ++count;
		}
		return count;
	}

	/// @brief リロードイベントログを取得する
	[[nodiscard]] std::vector<AssetReloadEvent> reloadLog() const
	{
		std::scoped_lock lock(m_mutex);
		return m_log;
	}

	/// @brief リロードログをクリアする
	void clearLog()
	{
		std::scoped_lock lock(m_mutex);
		m_log.clear();
	}

	/// @brief 成功リロード回数を取得する
	[[nodiscard]] uint64_t successCount() const noexcept
	{
		std::scoped_lock lock(m_mutex);
		return m_successCount;
	}

	/// @brief 失敗リロード回数を取得する
	[[nodiscard]] uint64_t failureCount() const noexcept
	{
		std::scoped_lock lock(m_mutex);
		return m_failureCount;
	}

	/// @brief FileWatcher への参照を取得する
	[[nodiscard]] FileWatcher& watcher() noexcept { return m_watcher; }
	[[nodiscard]] const FileWatcher& watcher() const noexcept { return m_watcher; }

private:
	/// @brief アセットエントリ
	struct AssetEntry
	{
		std::string filepath;
		AssetCategory category{AssetCategory::Custom};
		AssetChangedCallback callback;
		std::string lastContent;
	};

	/// @brief アセットを監視登録する（内部共通処理）
	void watchAsset(const std::string& filepath, AssetCategory category,
	                AssetChangedCallback callback)
	{
		std::scoped_lock lock(m_mutex);

		AssetEntry entry;
		entry.filepath = filepath;
		entry.category = category;
		entry.callback = std::move(callback);

		// 初回読み込み
		std::error_code ec;
		if (std::filesystem::exists(filepath, ec))
		{
			entry.lastContent = readFile(filepath);
		}

		m_assets[filepath] = std::move(entry);

		// FileWatcher にディレクトリを登録
		const auto dirPath = std::filesystem::path(filepath).parent_path().string();
		if (!dirPath.empty() && m_watchedDirs.find(dirPath) == m_watchedDirs.end())
		{
			m_watcher.watch(dirPath, [this](const FileEvent& event) {
				std::scoped_lock lock(m_pendingMutex);
				m_pendingEvents.push(event);
			});
			m_watchedDirs.insert(dirPath);
		}
		else if (dirPath.empty())
		{
			m_watcher.watch(filepath, [this](const FileEvent& event) {
				std::scoped_lock lock(m_pendingMutex);
				m_pendingEvents.push(event);
			});
		}
	}

	/// @brief ファイル変更を処理する
	void processFileChange(const std::string& changedPath)
	{
		std::error_code ec;
		std::string normalizedChanged = changedPath;
		try
		{
			const auto canonical = std::filesystem::canonical(changedPath, ec);
			if (!ec) normalizedChanged = canonical.string();
		}
		catch (...) {}

		for (auto& [filepath, entry] : m_assets)
		{
			std::string normalizedEntry = filepath;
			try
			{
				const auto canonical = std::filesystem::canonical(filepath, ec);
				if (!ec) normalizedEntry = canonical.string();
			}
			catch (...) {}

			if (normalizedChanged == normalizedEntry || changedPath == filepath)
			{
				const auto content = readFile(filepath);
				if (!content.empty() && content != entry.lastContent)
				{
					reloadAsset(filepath, entry, content);
				}
			}
		}
	}

	/// @brief アセットをリロードする
	/// @return 成功ならtrue
	bool reloadAsset(const std::string& filepath, AssetEntry& entry,
	                 const std::string& newContent)
	{
		AssetReloadEvent logEntry;
		logEntry.path = filepath;
		logEntry.category = entry.category;

		try
		{
			if (entry.callback)
			{
				entry.callback(filepath, newContent);
			}
			entry.lastContent = newContent;
			++m_successCount;

			logEntry.success = true;
			logEntry.message = "reloaded successfully";
		}
		catch (const std::exception& e)
		{
			++m_failureCount;
			logEntry.success = false;
			logEntry.message = std::string("reload failed: ") + e.what();
		}
		catch (...)
		{
			++m_failureCount;
			logEntry.success = false;
			logEntry.message = "reload failed: unknown exception";
		}

		m_log.push_back(std::move(logEntry));
		return logEntry.success;
	}

	/// @brief 拡張子がカテゴリに一致するか判定する
	[[nodiscard]] static bool matchesCategory(const std::string& ext, AssetCategory category) noexcept
	{
		switch (category)
		{
		case AssetCategory::Shader:
			return ext == ".hlsl" || ext == ".glsl" || ext == ".vert" ||
			       ext == ".frag" || ext == ".comp";
		case AssetCategory::UiTemplate:
			return ext == ".rml" || ext == ".rcss";
		case AssetCategory::Scenario:
			return ext == ".vns";
		case AssetCategory::Custom:
			return true;  // カスタムは全拡張子を受け入れる
		}
		return false;
	}

	/// @brief ファイルを読み込む
	[[nodiscard]] static std::string readFile(const std::string& filepath)
	{
		std::error_code ec;
		if (!std::filesystem::exists(filepath, ec)) return {};

		const auto fileSize = std::filesystem::file_size(filepath, ec);
		if (ec || fileSize == 0) return {};

		std::ifstream ifs(filepath, std::ios::binary);
		if (!ifs.is_open()) return {};

		std::string content(static_cast<std::size_t>(fileSize), '\0');
		ifs.read(content.data(), static_cast<std::streamsize>(fileSize));
		return content;
	}

	mutable std::mutex m_mutex;
	std::mutex m_pendingMutex;
	FileWatcher m_watcher;
	std::unordered_map<std::string, AssetEntry> m_assets;
	std::set<std::string> m_watchedDirs;
	std::queue<FileEvent> m_pendingEvents;
	std::vector<AssetReloadEvent> m_log;
	uint64_t m_successCount{0};
	uint64_t m_failureCount{0};
};

} // namespace mitiru::asset
