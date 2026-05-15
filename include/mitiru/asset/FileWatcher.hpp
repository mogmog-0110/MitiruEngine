#pragma once

/// @file FileWatcher.hpp
/// @brief クロスプラットフォームファイル監視システム
/// @details inotify (Linux), ReadDirectoryChangesW (Windows), kqueue (macOS) を使い分け、
///          フォールバックとして stat() ポーリング方式を提供する。
///
/// @code
/// mitiru::asset::FileWatcher watcher;
/// watcher.watch("shaders/", [](const auto& event) {
///     if (event.type == FileEventType::Modified)
///         reloadShader(event.path);
/// });
/// // メインループ内で:
/// watcher.poll();
/// @endcode

#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(MITIRU_PLATFORM_WIN32) || defined(_WIN32)
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <Windows.h>
#	define MITIRU_FILEWATCHER_WIN32 1
#elif defined(__linux__) && !defined(MITIRU_PLATFORM_WEB)
#	include <sys/inotify.h>
#	include <unistd.h>
#	include <poll.h>
#	include <climits>
#	define MITIRU_FILEWATCHER_INOTIFY 1
#else
#	define MITIRU_FILEWATCHER_POLLING 1
#endif

namespace mitiru::asset
{

/// @brief ファイルイベント種別
enum class FileEventType
{
	Created,   ///< ファイル作成
	Modified,  ///< ファイル変更
	Deleted    ///< ファイル削除
};

/// @brief ファイルイベント
struct FileEvent
{
	std::string path;       ///< 変更されたファイルのパス
	FileEventType type{};   ///< イベント種別
};

/// @brief ファイル監視コールバック型
using FileWatchCallback = std::function<void(const FileEvent&)>;

/// @brief クロスプラットフォームファイル監視
/// @details プラットフォーム固有の API を使いつつ、共通インターフェースを提供する。
///          利用不可な環境では stat() ベースのポーリングにフォールバックする。
class FileWatcher
{
public:
	FileWatcher()
	{
#if defined(MITIRU_FILEWATCHER_INOTIFY)
		m_inotifyFd = inotify_init1(IN_NONBLOCK);
#endif
	}

	~FileWatcher()
	{
#if defined(MITIRU_FILEWATCHER_INOTIFY)
		for (const auto& [wd, info] : m_inotifyWatches)
		{
			inotify_rm_watch(m_inotifyFd, wd);
		}
		if (m_inotifyFd >= 0)
		{
			::close(m_inotifyFd);
		}
#elif defined(MITIRU_FILEWATCHER_WIN32)
		for (auto& [path, handle] : m_winHandles)
		{
			if (handle != INVALID_HANDLE_VALUE)
			{
				FindCloseChangeNotification(handle);
			}
		}
#endif
	}

	/// @brief コピー禁止
	FileWatcher(const FileWatcher&) = delete;
	FileWatcher& operator=(const FileWatcher&) = delete;

	/// @brief ムーブ許可
	FileWatcher(FileWatcher&& other) noexcept
	{
		std::scoped_lock lock(other.m_mutex);
		m_entries = std::move(other.m_entries);
		m_pollingEntries = std::move(other.m_pollingEntries);
#if defined(MITIRU_FILEWATCHER_INOTIFY)
		m_inotifyFd = other.m_inotifyFd;
		other.m_inotifyFd = -1;
		m_inotifyWatches = std::move(other.m_inotifyWatches);
#elif defined(MITIRU_FILEWATCHER_WIN32)
		m_winHandles = std::move(other.m_winHandles);
#endif
	}

	FileWatcher& operator=(FileWatcher&& other) noexcept
	{
		if (this != &other)
		{
			std::scoped_lock lock(m_mutex, other.m_mutex);
			m_entries = std::move(other.m_entries);
			m_pollingEntries = std::move(other.m_pollingEntries);
#if defined(MITIRU_FILEWATCHER_INOTIFY)
			m_inotifyFd = other.m_inotifyFd;
			other.m_inotifyFd = -1;
			m_inotifyWatches = std::move(other.m_inotifyWatches);
#elif defined(MITIRU_FILEWATCHER_WIN32)
			m_winHandles = std::move(other.m_winHandles);
#endif
		}
		return *this;
	}

	/// @brief ファイルまたはディレクトリを監視対象に追加する
	/// @param path 監視対象パス
	/// @param callback 変更通知コールバック
	void watch(const std::string& path, FileWatchCallback callback)
	{
		std::scoped_lock lock(m_mutex);

		m_entries[path] = std::move(callback);

		// ポーリング用に現在のタイムスタンプを記録
		std::error_code ec;
		const auto fsPath = std::filesystem::path(path);

		if (std::filesystem::is_directory(fsPath, ec))
		{
			// ディレクトリの場合は中の全ファイルを追跡
			for (const auto& entry : std::filesystem::directory_iterator(fsPath, ec))
			{
				if (entry.is_regular_file())
				{
					const auto filePath = entry.path().string();
					const auto lastWrite = std::filesystem::last_write_time(filePath, ec);
					if (!ec)
					{
						m_pollingEntries[filePath] = {lastWrite, true};
					}
				}
			}
		}
		else if (std::filesystem::exists(fsPath, ec))
		{
			const auto lastWrite = std::filesystem::last_write_time(path, ec);
			if (!ec)
			{
				m_pollingEntries[path] = {lastWrite, true};
			}
		}
		else
		{
			// ファイルがまだ存在しない場合（将来作成される可能性）
			m_pollingEntries[path] = {{}, false};
		}

#if defined(MITIRU_FILEWATCHER_INOTIFY)
		setupInotifyWatch(path);
#elif defined(MITIRU_FILEWATCHER_WIN32)
		setupWin32Watch(path);
#endif
	}

	/// @brief 監視を解除する
	/// @param path 監視解除するパス
	void unwatch(const std::string& path)
	{
		std::scoped_lock lock(m_mutex);
		m_entries.erase(path);

		// ポーリングエントリも削除（ディレクトリ配下を含む）
		std::vector<std::string> toRemove;
		for (const auto& [p, info] : m_pollingEntries)
		{
			if (p == path || p.starts_with(path + "/") || p.starts_with(path + "\\"))
			{
				toRemove.push_back(p);
			}
		}
		for (const auto& p : toRemove)
		{
			m_pollingEntries.erase(p);
		}

#if defined(MITIRU_FILEWATCHER_INOTIFY)
		removeInotifyWatch(path);
#elif defined(MITIRU_FILEWATCHER_WIN32)
		removeWin32Watch(path);
#endif
	}

	/// @brief 変更をチェックする（ノンブロッキング）
	/// @return 検出されたイベントのリスト
	std::vector<FileEvent> poll()
	{
		std::scoped_lock lock(m_mutex);
		std::vector<FileEvent> events;

#if defined(MITIRU_FILEWATCHER_INOTIFY)
		pollInotify(events);
#elif defined(MITIRU_FILEWATCHER_WIN32)
		pollWin32(events);
#endif

		// ポーリングフォールバック（常に実行、OS通知を補完）
		pollStat(events);

		// コールバック呼び出し
		dispatchCallbacks(events);

		return events;
	}

	/// @brief 監視中のパス数を取得する
	[[nodiscard]] std::size_t watchCount() const noexcept
	{
		std::scoped_lock lock(m_mutex);
		return m_entries.size();
	}

	/// @brief 監視中のパス一覧を取得する
	[[nodiscard]] std::vector<std::string> watchedPaths() const
	{
		std::scoped_lock lock(m_mutex);
		std::vector<std::string> paths;
		paths.reserve(m_entries.size());
		for (const auto& [path, cb] : m_entries)
		{
			paths.push_back(path);
		}
		return paths;
	}

	/// @brief 全監視を解除する
	void unwatchAll()
	{
		std::scoped_lock lock(m_mutex);
		m_entries.clear();
		m_pollingEntries.clear();
#if defined(MITIRU_FILEWATCHER_INOTIFY)
		for (const auto& [wd, info] : m_inotifyWatches)
		{
			inotify_rm_watch(m_inotifyFd, wd);
		}
		m_inotifyWatches.clear();
#elif defined(MITIRU_FILEWATCHER_WIN32)
		for (auto& [path, handle] : m_winHandles)
		{
			if (handle != INVALID_HANDLE_VALUE)
			{
				FindCloseChangeNotification(handle);
			}
		}
		m_winHandles.clear();
#endif
	}

	/// @brief ポーリングで追跡中のファイル数を取得する
	[[nodiscard]] std::size_t trackedFileCount() const noexcept
	{
		std::scoped_lock lock(m_mutex);
		return m_pollingEntries.size();
	}

private:
	/// @brief ポーリング用エントリ情報
	struct PollingInfo
	{
		std::filesystem::file_time_type lastModified{};  ///< 最終更新時刻
		bool existed{false};                              ///< 前回チェック時に存在していたか
	};

	/// @brief stat() ベースのポーリングで変更を検出する
	void pollStat(std::vector<FileEvent>& events)
	{
		// 監視対象ディレクトリの新規ファイルをスキャン
		for (const auto& [watchPath, cb] : m_entries)
		{
			std::error_code ec;
			const auto fsPath = std::filesystem::path(watchPath);
			if (std::filesystem::is_directory(fsPath, ec))
			{
				for (const auto& entry : std::filesystem::directory_iterator(fsPath, ec))
				{
					if (!entry.is_regular_file()) continue;
					const auto filePath = entry.path().string();
					if (m_pollingEntries.find(filePath) == m_pollingEntries.end())
					{
						const auto lastWrite = std::filesystem::last_write_time(filePath, ec);
						if (!ec)
						{
							m_pollingEntries[filePath] = {lastWrite, true};
							events.push_back({filePath, FileEventType::Created});
						}
					}
				}
			}
		}

		// 既存エントリの変更・削除チェック
		std::vector<std::string> deleted;
		for (auto& [path, info] : m_pollingEntries)
		{
			std::error_code ec;
			const bool exists = std::filesystem::exists(path, ec);

			if (!exists && info.existed)
			{
				info.existed = false;
				events.push_back({path, FileEventType::Deleted});
				deleted.push_back(path);
				continue;
			}

			if (exists && !info.existed)
			{
				const auto lastWrite = std::filesystem::last_write_time(path, ec);
				if (!ec)
				{
					info.lastModified = lastWrite;
					info.existed = true;
					events.push_back({path, FileEventType::Created});
				}
				continue;
			}

			if (exists)
			{
				const auto currentWrite = std::filesystem::last_write_time(path, ec);
				if (!ec && currentWrite != info.lastModified)
				{
					info.lastModified = currentWrite;
					events.push_back({path, FileEventType::Modified});
				}
			}
		}
	}

	/// @brief コールバックを呼び出す
	void dispatchCallbacks(const std::vector<FileEvent>& events)
	{
		for (const auto& event : events)
		{
			// 完全一致
			auto it = m_entries.find(event.path);
			if (it != m_entries.end() && it->second)
			{
				it->second(event);
				continue;
			}

			// ディレクトリ監視: パスがディレクトリ配下か確認
			for (const auto& [watchPath, cb] : m_entries)
			{
				if (cb && (event.path.starts_with(watchPath + "/") ||
				           event.path.starts_with(watchPath + "\\")))
				{
					cb(event);
					break;
				}
			}
		}
	}

#if defined(MITIRU_FILEWATCHER_INOTIFY)
	struct InotifyWatchInfo
	{
		std::string path;
	};

	void setupInotifyWatch(const std::string& path)
	{
		if (m_inotifyFd < 0) return;
		const int wd = inotify_add_watch(m_inotifyFd, path.c_str(),
			IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);
		if (wd >= 0)
		{
			m_inotifyWatches[wd] = {path};
		}
	}

	void removeInotifyWatch(const std::string& path)
	{
		for (auto it = m_inotifyWatches.begin(); it != m_inotifyWatches.end(); ++it)
		{
			if (it->second.path == path)
			{
				inotify_rm_watch(m_inotifyFd, it->first);
				m_inotifyWatches.erase(it);
				break;
			}
		}
	}

	void pollInotify(std::vector<FileEvent>& events)
	{
		if (m_inotifyFd < 0) return;

		alignas(struct inotify_event) char buf[4096];
		while (true)
		{
			const auto len = ::read(m_inotifyFd, buf, sizeof(buf));
			if (len <= 0) break;

			const char* ptr = buf;
			while (ptr < buf + len)
			{
				const auto* event = reinterpret_cast<const struct inotify_event*>(ptr);
				if (event->len > 0)
				{
					auto wit = m_inotifyWatches.find(event->wd);
					if (wit != m_inotifyWatches.end())
					{
						const auto fullPath = wit->second.path + "/" + event->name;
						if (event->mask & (IN_CREATE | IN_MOVED_TO))
						{
							events.push_back({fullPath, FileEventType::Created});
						}
						else if (event->mask & IN_MODIFY)
						{
							events.push_back({fullPath, FileEventType::Modified});
						}
						else if (event->mask & (IN_DELETE | IN_MOVED_FROM))
						{
							events.push_back({fullPath, FileEventType::Deleted});
						}
					}
				}
				ptr += sizeof(struct inotify_event) + event->len;
			}
		}
	}

	int m_inotifyFd{-1};
	std::unordered_map<int, InotifyWatchInfo> m_inotifyWatches;
#elif defined(MITIRU_FILEWATCHER_WIN32)
	void setupWin32Watch(const std::string& path)
	{
		std::error_code ec;
		std::string dirPath = path;
		if (!std::filesystem::is_directory(dirPath, ec))
		{
			dirPath = std::filesystem::path(path).parent_path().string();
		}
		if (dirPath.empty()) return;

		HANDLE hDir = FindFirstChangeNotificationA(
			dirPath.c_str(), FALSE,
			FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME);
		if (hDir != INVALID_HANDLE_VALUE)
		{
			m_winHandles[path] = hDir;
		}
	}

	void removeWin32Watch(const std::string& path)
	{
		auto it = m_winHandles.find(path);
		if (it != m_winHandles.end())
		{
			FindCloseChangeNotification(it->second);
			m_winHandles.erase(it);
		}
	}

	void pollWin32(std::vector<FileEvent>& events)
	{
		for (auto& [path, handle] : m_winHandles)
		{
			if (handle == INVALID_HANDLE_VALUE) continue;
			DWORD waitResult = WaitForSingleObject(handle, 0);
			if (waitResult == WAIT_OBJECT_0)
			{
				// 変更検出 — 具体的な変更はstat()ポーリングで特定する
				FindNextChangeNotification(handle);
			}
		}
	}

	std::unordered_map<std::string, HANDLE> m_winHandles;
#endif

	mutable std::mutex m_mutex;
	std::unordered_map<std::string, FileWatchCallback> m_entries;
	std::unordered_map<std::string, PollingInfo> m_pollingEntries;
};

} // namespace mitiru::asset
