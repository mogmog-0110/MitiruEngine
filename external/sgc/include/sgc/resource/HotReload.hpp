#pragma once

/// @file HotReload.hpp
/// @brief ファイル変更監視（ポーリング方式）
///
/// std::filesystem の last_write_time を使用したポータブルなファイル監視。
/// 毎フレーム poll() を呼んで変更を検出する。
///
/// @code
/// sgc::FileWatcher watcher;
/// watcher.watch("assets/player.png", [](const std::string& path) {
///     reloadTexture(path);
/// });
///
/// // ゲームループ内
/// watcher.poll();
/// @endcode

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>

namespace sgc
{

/// @brief ファイル変更コールバック型
using FileChangedCallback = std::function<void(const std::string&)>;

/// @brief ファイル変更監視（ポーリング方式）
///
/// 登録されたファイルの最終更新時刻をポーリングで監視し、
/// 変更があればコールバックを呼ぶ。
class FileWatcher
{
public:
	/// @brief ファイルを監視対象に追加する
	/// @param path ファイルパス
	/// @param callback 変更時コールバック
	void watch(const std::string& path, FileChangedCallback callback)
	{
		WatchEntry entry;
		entry.callback = std::move(callback);

		std::error_code ec;
		if (std::filesystem::exists(path, ec))
		{
			entry.lastWriteTime = std::filesystem::last_write_time(path, ec);
		}

		m_entries[path] = std::move(entry);
	}

	/// @brief ファイルの監視を解除する
	/// @param path ファイルパス
	void unwatch(const std::string& path)
	{
		m_entries.erase(path);
	}

	/// @brief 全監視ファイルの変更を確認する
	///
	/// 変更があったファイルのコールバックを呼ぶ。
	/// 毎フレーム呼ぶことを想定。
	void poll()
	{
		for (auto& [path, entry] : m_entries)
		{
			std::error_code ec;
			if (!std::filesystem::exists(path, ec)) continue;

			auto currentTime = std::filesystem::last_write_time(path, ec);
			if (ec) continue;

			if (currentTime != entry.lastWriteTime)
			{
				entry.lastWriteTime = currentTime;
				if (entry.callback) entry.callback(path);
			}
		}
	}

	/// @brief 監視中のファイル数
	/// @return ファイル数
	[[nodiscard]] std::size_t watchCount() const noexcept
	{
		return m_entries.size();
	}

	/// @brief 全監視を解除する
	void clear()
	{
		m_entries.clear();
	}

private:
	/// @brief 監視エントリ
	struct WatchEntry
	{
		FileChangedCallback callback;                                  ///< 変更コールバック
		std::filesystem::file_time_type lastWriteTime{};               ///< 最終更新時刻
	};

	std::unordered_map<std::string, WatchEntry> m_entries;  ///< 監視エントリ
};

} // namespace sgc
