#pragma once

/// @file FileWatcher.hpp
/// @brief DLL、HTML、アセットをホットリロードするため、ファイルとフォルダの変更を main thread で受け取る。
/// @details OS の通知は見直すファイルの候補を選ぶためだけに使い、変更の有無は mtime で決める。
/// リンカによる分割書き込みや一時ファイル経由の保存に備え、通知が settle の間止んでから mtime を調べ、1 変更につき 1 回だけ返す。
/// efsw がないビルドと Backend::Polling では、poll() が 250 ms ごとに監視対象の mtime を調べる。

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(MITIRU_HAS_EFSW)
#	include <efsw/efsw.hpp>
#endif

namespace mitiru::asset
{

class FileWatcher
{
public:
	enum class Backend
	{
		Native,   ///< OS 通知 (efsw)。efsw が無いビルドでは Polling になる
		Polling,  ///< 250ms ごとに mtime を総なめ
	};

	using Clock = std::chrono::steady_clock;

	explicit FileWatcher(Backend backend = Backend::Native,
	                     std::chrono::milliseconds settle = std::chrono::milliseconds{200})
		: m_settle(settle)
	{
#if defined(MITIRU_HAS_EFSW)
		if (backend == Backend::Native)
		{
			m_native = std::make_unique<Native>();
		}
#else
		(void)backend;
#endif
	}

	FileWatcher(const FileWatcher&) = delete;
	FileWatcher& operator=(const FileWatcher&) = delete;
	FileWatcher(FileWatcher&&) = delete;
	FileWatcher& operator=(FileWatcher&&) = delete;

	[[nodiscard]] Backend backend() const noexcept
	{
#if defined(MITIRU_HAS_EFSW)
		if (m_native) { return Backend::Native; }
#endif
		return Backend::Polling;
	}

	/// @brief 1 ファイルを監視する。呼び出した時点の mtime を基準とし、現在の内容は変更として返さない。
	/// @return 親フォルダを監視できない場合は false
	bool watchFile(const std::filesystem::path& file)
	{
		const auto abs = normalized(file);
		m_files.push_back(WatchedFile{file, abs});
		remember(abs);
		return watchFolder(abs.parent_path(), false);
	}

	/// @brief フォルダ配下を再帰的に監視する。extensions が空なら全ファイルを対象とし、大文字と小文字は区別しない。
	/// 既存のファイルは基準として記録し、以後に追加または変更されたファイルだけを返す。
	bool watchDirectory(const std::filesystem::path& dir, std::vector<std::string> extensions = {})
	{
		for (auto& ext : extensions) { ext = lower(ext); }
		WatchedDir watched{normalized(dir), std::move(extensions)};
		std::error_code ec;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(watched.root, ec))
		{
			if (ec) { break; }
			if (watched.accepts(entry.path())) { remember(entry.path()); }
		}
		const auto root = watched.root;
		m_dirs.push_back(std::move(watched));
		return watchFolder(root, true);
	}

	/// @brief 直前の poll() 以降に変更または追加されたファイルを返す。毎フレーム呼び出せる。
	/// @details watchFile() の対象は渡されたパスで返し、watchDirectory() の対象は見つかったパスで返す。
	[[nodiscard]] std::vector<std::filesystem::path> poll()
	{
		std::vector<std::filesystem::path> changed;
		for (const auto& candidate : takeCandidates())
		{
			if (mtimeChanged(candidate)) { changed.push_back(reportedPath(candidate)); }
		}
		return changed;
	}

private:
	struct WatchedFile
	{
		std::filesystem::path given;     ///< 呼び出し側に返すパス
		std::filesystem::path absolute;
	};

	struct WatchedDir
	{
		std::filesystem::path    root;
		std::vector<std::string> extensions;

		[[nodiscard]] bool accepts(const std::filesystem::path& p) const
		{
			std::error_code ec;
			if (!std::filesystem::is_regular_file(p, ec)) { return false; }
			if (extensions.empty()) { return true; }
			const auto ext = lower(p.extension().string());
			return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
		}
	};

	static std::string lower(std::string s)
	{
		for (auto& c : s)
		{
			if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c - 'A' + 'a'); }
		}
		return s;
	}

	static std::filesystem::path normalized(const std::filesystem::path& p)
	{
		std::error_code ec;
		auto abs = std::filesystem::absolute(p, ec);
		return (ec ? p : abs).lexically_normal();
	}

	/// Windows ではパスの大文字と小文字を区別せず、通知と指定したパスの綴りが違っても同じキーにする。
	static std::string keyOf(const std::filesystem::path& p)
	{
		const auto u8 = p.lexically_normal().generic_u8string();
		std::string key(u8.begin(), u8.end());
#if defined(_WIN32)
		key = lower(std::move(key));
#endif
		return key;
	}

	static bool isUnder(const std::string& key, const std::string& rootKey)
	{
		if (key.size() <= rootKey.size() || key.compare(0, rootKey.size(), rootKey) != 0) { return false; }
		return rootKey.back() == '/' || key[rootKey.size()] == '/';
	}

	void remember(const std::filesystem::path& p)
	{
		std::error_code ec;
		const auto mtime = std::filesystem::last_write_time(p, ec);
		if (!ec) { m_mtimes[keyOf(p)] = mtime; }
	}

	/// 消えたファイルは基準から外し、作り直されたときは新しいファイルとして返す。
	bool mtimeChanged(const std::filesystem::path& p)
	{
		const auto key = keyOf(p);
		std::error_code ec;
		const auto mtime = std::filesystem::last_write_time(p, ec);
		if (ec)
		{
			m_mtimes.erase(key);
			return false;
		}
		const auto it = m_mtimes.find(key);
		if (it != m_mtimes.end() && it->second == mtime) { return false; }
		m_mtimes[key] = mtime;
		return true;
	}

	std::filesystem::path reportedPath(const std::filesystem::path& p) const
	{
		const auto key = keyOf(p);
		for (const auto& f : m_files)
		{
			if (keyOf(f.absolute) == key) { return f.given; }
		}
		return p;
	}

	std::vector<std::filesystem::path> takeCandidates()
	{
#if defined(MITIRU_HAS_EFSW)
		if (m_native) { return candidatesFromEvents(m_native->takeSettled(Clock::now(), m_settle)); }
#endif
		return candidatesFromScan();
	}

	std::vector<std::filesystem::path> candidatesFromScan()
	{
		const auto now = Clock::now();
		if (m_scanned && now - m_lastScan < std::chrono::milliseconds{250}) { return {}; }
		m_scanned = true;
		m_lastScan = now;
		std::vector<std::filesystem::path> out;
		for (const auto& f : m_files) { out.push_back(f.absolute); }
		for (const auto& d : m_dirs)
		{
			std::error_code ec;
			for (const auto& entry : std::filesystem::recursive_directory_iterator(d.root, ec))
			{
				if (ec) { break; }
				if (d.accepts(entry.path())) { out.push_back(entry.path()); }
			}
		}
		return out;
	}

	/// 一時ファイル経由の保存では通知された名前が監視対象と一致しないため、通知が来たフォルダ内の監視ファイルをすべて見直す。
	std::vector<std::filesystem::path> candidatesFromEvents(const std::vector<std::filesystem::path>& events) const
	{
		std::vector<std::filesystem::path> out;
		if (events.empty()) { return out; }
		std::unordered_set<std::string> seen;
		const auto add = [&](const std::filesystem::path& p) {
			if (seen.insert(keyOf(p)).second) { out.push_back(p); }
		};
		for (const auto& ev : events)
		{
			const auto evKey = keyOf(ev);
			const auto folderKey = keyOf(ev.parent_path());
			for (const auto& f : m_files)
			{
				if (keyOf(f.absolute.parent_path()) == folderKey) { add(f.absolute); }
			}
			for (const auto& d : m_dirs)
			{
				if (isUnder(evKey, keyOf(d.root)) && d.accepts(ev)) { add(ev); }
			}
		}
		return out;
	}

#if defined(MITIRU_HAS_EFSW)
	/// efsw の通知スレッドから来たイベントを溜め、settle の間止んだものだけを main thread に渡す。
	class Native final : public efsw::FileWatchListener
	{
	public:
		bool addFolder(const std::filesystem::path& dir, bool recursive)
		{
			const auto u8 = dir.u8string();
			const std::string utf8(u8.begin(), u8.end());
			const auto id = m_efsw->addWatch(utf8, this, recursive);
			// 監視スレッドは最初の登録後に起動する。2 回目以降の watch() は何もしない。
			m_efsw->watch();
			return id >= 0 || id == efsw::Errors::FileRepeated;
		}

		void handleFileAction(efsw::WatchID, const std::string& dir, const std::string& filename,
		                      efsw::Action, const std::string&) override
		{
			const std::u8string u8dir(dir.begin(), dir.end());
			const std::u8string u8name(filename.begin(), filename.end());
			auto path = (std::filesystem::path(u8dir) / std::filesystem::path(u8name)).lexically_normal();
			std::lock_guard lock(m_mutex);
			m_pending[std::move(path)] = Clock::now();
		}

		std::vector<std::filesystem::path> takeSettled(Clock::time_point now, std::chrono::milliseconds settle)
		{
			std::vector<std::filesystem::path> out;
			std::lock_guard lock(m_mutex);
			for (auto it = m_pending.begin(); it != m_pending.end();)
			{
				if (now - it->second < settle) { ++it; continue; }
				out.push_back(it->first);
				it = m_pending.erase(it);
			}
			return out;
		}

	private:
		struct PathHash
		{
			std::size_t operator()(const std::filesystem::path& p) const noexcept
			{
				return std::filesystem::hash_value(p);
			}
		};

		std::mutex m_mutex;
		std::unordered_map<std::filesystem::path, Clock::time_point, PathHash> m_pending;
		// 最後に宣言して最初に破棄することで、通知スレッドを m_mutex と m_pending より先に止める。
		std::unique_ptr<efsw::FileWatcher> m_efsw = std::make_unique<efsw::FileWatcher>();
	};
#endif

	bool watchFolder(const std::filesystem::path& dir, bool recursive)
	{
		std::error_code ec;
		if (!std::filesystem::is_directory(dir, ec)) { return false; }
#if defined(MITIRU_HAS_EFSW)
		if (m_native) { return m_native->addFolder(dir, recursive); }
#else
		(void)recursive;
#endif
		return true;
	}

	std::chrono::milliseconds m_settle;
	std::vector<WatchedFile> m_files;
	std::vector<WatchedDir> m_dirs;
	std::unordered_map<std::string, std::filesystem::file_time_type> m_mtimes;
	Clock::time_point m_lastScan{};
	bool m_scanned = false;
#if defined(MITIRU_HAS_EFSW)
	std::unique_ptr<Native> m_native;
#endif
};

}  // namespace mitiru::asset
