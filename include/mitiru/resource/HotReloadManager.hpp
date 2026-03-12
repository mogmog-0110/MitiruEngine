#pragma once

/// @file HotReloadManager.hpp
/// @brief ホットリロードマネージャー
/// @details ファイルの変更を検知し、コールバックで通知するホットリロード機能。
///          std::filesystem を使用してファイルの更新時刻を監視する。

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mitiru::resource
{

/// @brief ホットリロードマネージャー
/// @details ファイルパスを監視し、変更が検知されたらコールバックを呼び出す。
///          pollChanges() でポーリングベースの監視を行う。
///
/// @code
/// mitiru::resource::HotReloadManager hotReload;
/// hotReload.watch("config.json", [](const std::string& path) {
///     reloadConfig(path);
/// });
/// // メインループ内で:
/// hotReload.pollChanges();
/// @endcode
class HotReloadManager
{
public:
	/// @brief ファイル変更コールバック型
	using Callback = std::function<void(const std::string&)>;

	/// @brief ファイルを監視対象に追加する
	/// @param path 監視するファイルパス
	/// @param callback 変更検知時のコールバック
	void watch(const std::string& path, Callback callback)
	{
		WatchEntry entry;
		entry.path = path;
		entry.callback = std::move(callback);

		/// 現在の更新時刻を記録
		std::error_code ec;
		const auto lastWrite = std::filesystem::last_write_time(path, ec);
		if (!ec)
		{
			entry.lastModified = lastWrite;
		}

		m_entries[path] = std::move(entry);
	}

	/// @brief ファイルの監視を解除する
	/// @param path 監視解除するファイルパス
	void unwatch(const std::string& path)
	{
		m_entries.erase(path);
	}

	/// @brief 全監視ファイルの変更をチェックする
	/// @return 変更が検知されたファイルパスのベクタ
	std::vector<std::string> pollChanges()
	{
		std::vector<std::string> changed;

		for (auto& [path, entry] : m_entries)
		{
			std::error_code ec;
			const auto currentWrite = std::filesystem::last_write_time(path, ec);

			if (ec)
			{
				continue;  ///< ファイルが存在しない等
			}

			if (currentWrite != entry.lastModified)
			{
				entry.lastModified = currentWrite;
				changed.push_back(path);

				if (entry.callback)
				{
					entry.callback(path);
				}
			}
		}

		return changed;
	}

	/// @brief 監視中のファイル数を取得する
	/// @return 監視ファイル数
	[[nodiscard]] std::size_t watchCount() const noexcept
	{
		return m_entries.size();
	}

	/// @brief 監視中のファイルパス一覧を取得する
	/// @return パスのベクタ
	[[nodiscard]] std::vector<std::string> watchedPaths() const
	{
		std::vector<std::string> paths;
		paths.reserve(m_entries.size());
		for (const auto& [path, entry] : m_entries)
		{
			paths.push_back(path);
		}
		return paths;
	}

	/// @brief 全監視を解除する
	void unwatchAll()
	{
		m_entries.clear();
	}

private:
	/// @brief 監視エントリ
	struct WatchEntry
	{
		std::string path;                                    ///< ファイルパス
		Callback callback;                                   ///< 変更コールバック
		std::filesystem::file_time_type lastModified;        ///< 最終更新時刻
	};

	/// @brief パス → 監視エントリ
	std::unordered_map<std::string, WatchEntry> m_entries;
};

} // namespace mitiru::resource
