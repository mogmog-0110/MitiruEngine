#pragma once

/// @file HotReloadWatcher.hpp
/// @brief 汎用ファイルホットリロード監視システム
/// @details 個別ファイルおよびディレクトリの変更を検知し、デバウンス付きで
///          コールバック通知を行う。シェーダー、テクスチャ、スクリプト、
///          UIスキン、シーンファイルなどの自動リロードに使用する。

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mitiru::resource
{

/// @brief ファイル変更種別
enum class FileChangeType : std::uint8_t
{
	Modified,  ///< 更新された
	Created,   ///< 新規作成された
	Deleted,   ///< 削除された
};

/// @brief ファイル変更イベント
struct FileChangeEvent
{
	std::string path;                                    ///< ファイルパス
	FileChangeType type = FileChangeType::Modified;      ///< 変更種別
	std::chrono::steady_clock::time_point timestamp;     ///< 検知タイムスタンプ
};

/// @brief ファイル変更コールバック型
using FileChangeCallback = std::function<void(const FileChangeEvent&)>;

/// @brief 汎用ファイルホットリロード監視
/// @details watchFile / watchDirectory でファイルを監視し、
///          update() でタイムスタンプをポーリング、変更検知時にコールバックを発火する。
///          デバウンス機能により、連続的な変更を一度にまとめる。
///
/// @code
/// mitiru::resource::HotReloadWatcher watcher;
/// watcher.setDebounceDelay(std::chrono::milliseconds(200));
///
/// watcher.watchFile("shaders/basic.hlsl", [](const FileChangeEvent& e) {
///     reloadShader(e.path);
/// });
///
/// watcher.watchDirectory("assets/textures", ".png", [](const FileChangeEvent& e) {
///     reloadTexture(e.path);
/// });
///
/// // メインループ内:
/// auto events = watcher.update();
/// @endcode
class HotReloadWatcher
{
public:
	/// @brief デバウンス遅延を設定する
	/// @param delay デバウンス遅延時間（デフォルト: 200ms）
	void setDebounceDelay(std::chrono::milliseconds delay) noexcept
	{
		m_debounceDelay = delay;
	}

	/// @brief デバウンス遅延を取得する
	[[nodiscard]] std::chrono::milliseconds debounceDelay() const noexcept
	{
		return m_debounceDelay;
	}

	/// @brief 個別ファイルを監視する
	/// @param path 監視するファイルパス
	/// @param callback 変更検知時のコールバック
	void watchFile(const std::string& path, FileChangeCallback callback)
	{
		WatchEntry entry;
		entry.path = path;
		entry.callback = std::move(callback);
		entry.isDirectory = false;

		std::error_code ec;
		if (std::filesystem::exists(path, ec))
		{
			entry.lastModified = std::filesystem::last_write_time(path, ec);
			entry.existed = true;
		}
		else
		{
			entry.existed = false;
		}

		m_entries[path] = std::move(entry);
	}

	/// @brief ディレクトリ内の指定拡張子ファイルを監視する
	/// @param dirPath ディレクトリパス
	/// @param extension 拡張子フィルター（例: ".hlsl", ".png"）
	/// @param callback 変更検知時のコールバック
	void watchDirectory(const std::string& dirPath,
						const std::string& extension,
						FileChangeCallback callback)
	{
		DirectoryWatch dirWatch;
		dirWatch.dirPath = dirPath;
		dirWatch.extension = extension;
		dirWatch.callback = std::move(callback);

		/// 既存ファイルのスナップショットを取得
		std::error_code ec;
		if (std::filesystem::is_directory(dirPath, ec))
		{
			for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec))
			{
				if (entry.is_regular_file() && matchesExtension(entry.path(), extension))
				{
					const auto pathStr = entry.path().string();
					std::error_code writeEc;
					dirWatch.fileTimestamps[pathStr] =
						std::filesystem::last_write_time(entry.path(), writeEc);
				}
			}
		}

		m_directoryWatches[dirPath + "|" + extension] = std::move(dirWatch);
	}

	/// @brief タイムスタンプをチェックし、変更があればコールバックを発火する
	/// @return 今回のupdate()で発生した変更イベントのリスト
	std::vector<FileChangeEvent> update()
	{
		const auto now = std::chrono::steady_clock::now();
		std::vector<FileChangeEvent> events;

		/// 個別ファイルのチェック
		for (auto& [path, entry] : m_entries)
		{
			checkFileEntry(path, entry, now, events);
		}

		/// ディレクトリ監視のチェック
		for (auto& [key, dirWatch] : m_directoryWatches)
		{
			checkDirectoryWatch(dirWatch, now, events);
		}

		return events;
	}

	/// @brief ファイルの監視を解除する
	/// @param path 解除するファイルパス
	void unwatch(const std::string& path)
	{
		m_entries.erase(path);
	}

	/// @brief 全監視を解除する
	void unwatchAll()
	{
		m_entries.clear();
		m_directoryWatches.clear();
	}

	/// @brief 個別監視中のファイル数を取得する
	[[nodiscard]] std::size_t watchedFileCount() const noexcept
	{
		return m_entries.size();
	}

	/// @brief ディレクトリ監視数を取得する
	[[nodiscard]] std::size_t watchedDirectoryCount() const noexcept
	{
		return m_directoryWatches.size();
	}

	/// @brief 監視中のファイルパス一覧を取得する
	[[nodiscard]] std::vector<std::string> watchedFilePaths() const
	{
		std::vector<std::string> paths;
		paths.reserve(m_entries.size());
		for (const auto& [path, entry] : m_entries)
		{
			paths.push_back(path);
		}
		return paths;
	}

	/// @brief 監視中のディレクトリパス一覧を取得する
	[[nodiscard]] std::vector<std::string> watchedDirectoryPaths() const
	{
		std::vector<std::string> paths;
		for (const auto& [key, dirWatch] : m_directoryWatches)
		{
			paths.push_back(dirWatch.dirPath);
		}
		return paths;
	}

	/// @brief 指定パスが監視中か判定する
	/// @param path ファイルパス
	/// @return 監視中の場合 true
	[[nodiscard]] bool isWatching(const std::string& path) const
	{
		return m_entries.find(path) != m_entries.end();
	}

	// ----- 便利メソッド: よくある拡張子グループ -----

	/// @brief シェーダーファイルを監視する
	/// @param dirPath シェーダーディレクトリ
	/// @param callback コールバック
	void watchShaders(const std::string& dirPath, FileChangeCallback callback)
	{
		watchDirectory(dirPath, ".hlsl", callback);
	}

	/// @brief テクスチャファイルを監視する（.png）
	/// @param dirPath テクスチャディレクトリ
	/// @param callback コールバック
	void watchTextures(const std::string& dirPath, FileChangeCallback callback)
	{
		watchDirectory(dirPath, ".png", callback);
	}

	/// @brief スクリプトファイルを監視する
	/// @param dirPath スクリプトディレクトリ
	/// @param callback コールバック
	void watchScripts(const std::string& dirPath, FileChangeCallback callback)
	{
		watchDirectory(dirPath, ".vns", callback);
	}

	/// @brief JSONファイル（UIスキン、シーン等）を監視する
	/// @param dirPath JSONディレクトリ
	/// @param callback コールバック
	void watchJsonFiles(const std::string& dirPath, FileChangeCallback callback)
	{
		watchDirectory(dirPath, ".json", callback);
	}

private:
	/// @brief 個別ファイル監視エントリ
	struct WatchEntry
	{
		std::string path;
		FileChangeCallback callback;
		std::filesystem::file_time_type lastModified;
		std::chrono::steady_clock::time_point lastEventTime;
		bool isDirectory = false;
		bool existed = false;
	};

	/// @brief ディレクトリ監視エントリ
	struct DirectoryWatch
	{
		std::string dirPath;
		std::string extension;
		FileChangeCallback callback;
		std::unordered_map<std::string, std::filesystem::file_time_type> fileTimestamps;
	};

	/// @brief 拡張子が一致するか判定する
	[[nodiscard]] static bool matchesExtension(
		const std::filesystem::path& filePath,
		const std::string& extension)
	{
		return filePath.extension().string() == extension;
	}

	/// @brief 個別ファイルエントリをチェックする
	void checkFileEntry(
		const std::string& path,
		WatchEntry& entry,
		std::chrono::steady_clock::time_point now,
		std::vector<FileChangeEvent>& events)
	{
		std::error_code ec;
		const bool exists = std::filesystem::exists(path, ec);

		if (!exists && entry.existed)
		{
			/// ファイル削除検知
			if (shouldFireEvent(entry.lastEventTime, now))
			{
				FileChangeEvent event;
				event.path = path;
				event.type = FileChangeType::Deleted;
				event.timestamp = now;
				events.push_back(event);

				if (entry.callback)
				{
					entry.callback(event);
				}

				entry.existed = false;
				entry.lastEventTime = now;
			}
			return;
		}

		if (exists && !entry.existed)
		{
			/// ファイル作成検知
			if (shouldFireEvent(entry.lastEventTime, now))
			{
				entry.lastModified = std::filesystem::last_write_time(path, ec);
				entry.existed = true;

				FileChangeEvent event;
				event.path = path;
				event.type = FileChangeType::Created;
				event.timestamp = now;
				events.push_back(event);

				if (entry.callback)
				{
					entry.callback(event);
				}

				entry.lastEventTime = now;
			}
			return;
		}

		if (exists)
		{
			/// 更新チェック
			const auto currentWrite = std::filesystem::last_write_time(path, ec);
			if (!ec && currentWrite != entry.lastModified)
			{
				if (shouldFireEvent(entry.lastEventTime, now))
				{
					entry.lastModified = currentWrite;

					FileChangeEvent event;
					event.path = path;
					event.type = FileChangeType::Modified;
					event.timestamp = now;
					events.push_back(event);

					if (entry.callback)
					{
						entry.callback(event);
					}

					entry.lastEventTime = now;
				}
			}
		}
	}

	/// @brief ディレクトリ監視をチェックする
	void checkDirectoryWatch(
		DirectoryWatch& dirWatch,
		std::chrono::steady_clock::time_point now,
		std::vector<FileChangeEvent>& events)
	{
		std::error_code ec;
		if (!std::filesystem::is_directory(dirWatch.dirPath, ec))
		{
			return;
		}

		/// 現在のファイル一覧を取得
		std::unordered_map<std::string, std::filesystem::file_time_type> currentFiles;

		for (const auto& entry : std::filesystem::directory_iterator(dirWatch.dirPath, ec))
		{
			if (entry.is_regular_file() && matchesExtension(entry.path(), dirWatch.extension))
			{
				const auto pathStr = entry.path().string();
				std::error_code writeEc;
				currentFiles[pathStr] = std::filesystem::last_write_time(entry.path(), writeEc);
			}
		}

		/// 新規 / 更新ファイルの検知
		for (const auto& [filePath, writeTime] : currentFiles)
		{
			const auto prevIt = dirWatch.fileTimestamps.find(filePath);

			if (prevIt == dirWatch.fileTimestamps.end())
			{
				/// 新規ファイル
				FileChangeEvent event;
				event.path = filePath;
				event.type = FileChangeType::Created;
				event.timestamp = now;
				events.push_back(event);

				if (dirWatch.callback)
				{
					dirWatch.callback(event);
				}
			}
			else if (writeTime != prevIt->second)
			{
				/// 更新ファイル
				FileChangeEvent event;
				event.path = filePath;
				event.type = FileChangeType::Modified;
				event.timestamp = now;
				events.push_back(event);

				if (dirWatch.callback)
				{
					dirWatch.callback(event);
				}
			}
		}

		/// 削除ファイルの検知
		for (const auto& [filePath, writeTime] : dirWatch.fileTimestamps)
		{
			if (currentFiles.find(filePath) == currentFiles.end())
			{
				FileChangeEvent event;
				event.path = filePath;
				event.type = FileChangeType::Deleted;
				event.timestamp = now;
				events.push_back(event);

				if (dirWatch.callback)
				{
					dirWatch.callback(event);
				}
			}
		}

		/// スナップショットを更新
		dirWatch.fileTimestamps = std::move(currentFiles);
	}

	/// @brief デバウンスを考慮してイベント発火すべきか判定する
	[[nodiscard]] bool shouldFireEvent(
		std::chrono::steady_clock::time_point lastEvent,
		std::chrono::steady_clock::time_point now) const noexcept
	{
		if (lastEvent == std::chrono::steady_clock::time_point{})
		{
			return true;
		}
		return (now - lastEvent) >= m_debounceDelay;
	}

	/// @brief 個別ファイル監視エントリ
	std::unordered_map<std::string, WatchEntry> m_entries;

	/// @brief ディレクトリ監視エントリ
	std::unordered_map<std::string, DirectoryWatch> m_directoryWatches;

	/// @brief デバウンス遅延（デフォルト: 200ms）
	std::chrono::milliseconds m_debounceDelay{200};
};

} // namespace mitiru::resource
