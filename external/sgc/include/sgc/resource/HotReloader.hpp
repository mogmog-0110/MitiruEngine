#pragma once

/// @file HotReloader.hpp
/// @brief ファイル変更検知とホットリロード
///
/// 登録されたファイルの変更を定期的にポーリングし、
/// 変更が検出されたらコールバックを呼び出す。
/// スレッドを使用せず、手動poll()で動作する。
///
/// @code
/// sgc::resource::HotReloader reloader;
/// reloader.watch("config.json", [](const std::filesystem::path& p) {
///     // ファイルが変更された時の処理
/// });
/// // ゲームループ内で
/// reloader.poll();
/// @endcode

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sgc::resource
{

/// @brief ファイル監視エントリ
struct FileWatchEntry
{
	std::filesystem::path path;                        ///< 監視対象パス
	std::filesystem::file_time_type lastModified{};    ///< 最終更新時刻
	std::function<void(const std::filesystem::path&)> callback;  ///< 変更時コールバック
};

/// @brief ホットリローダー
///
/// ファイルの変更を手動ポーリングで検出し、コールバックを発火する。
/// スレッドを使わないシンプルな設計。
class HotReloader
{
public:
	/// @brief コンストラクタ
	/// @param pollIntervalMs ポーリング間隔（ミリ秒、デフォルト1000ms）
	explicit HotReloader(int pollIntervalMs = 1000)
		: m_pollInterval(std::chrono::milliseconds(pollIntervalMs))
	{
	}

	/// @brief ファイルを監視対象に追加する
	/// @param path ファイルパス
	/// @param callback 変更検出時のコールバック
	/// @return 登録成功時true（ファイルが存在しない場合はfalse）
	bool watch(const std::filesystem::path& path,
	           std::function<void(const std::filesystem::path&)> callback)
	{
		std::error_code ec;
		const auto lastWrite = std::filesystem::last_write_time(path, ec);
		if (ec)
		{
			return false;
		}

		FileWatchEntry entry;
		entry.path = path;
		entry.lastModified = lastWrite;
		entry.callback = std::move(callback);

		m_entries[path.string()] = std::move(entry);
		return true;
	}

	/// @brief ファイルの監視を解除する
	/// @param path ファイルパス
	void unwatch(const std::filesystem::path& path)
	{
		m_entries.erase(path.string());
	}

	/// @brief 全監視対象をチェックし、変更があればコールバックを呼ぶ
	/// @return 変更が検出されたファイル数
	size_t poll()
	{
		const auto now = std::chrono::steady_clock::now();

		// ポーリング間隔チェック
		if (now - m_lastPollTime < m_pollInterval)
		{
			return 0;
		}
		m_lastPollTime = now;

		return forcePoll();
	}

	/// @brief 間隔を無視して即座に全ファイルをチェックする
	/// @return 変更が検出されたファイル数
	size_t forcePoll()
	{
		size_t changedCount = 0;

		for (auto& [key, entry] : m_entries)
		{
			std::error_code ec;
			const auto currentTime = std::filesystem::last_write_time(entry.path, ec);
			if (ec)
			{
				continue;  // ファイルにアクセスできない場合はスキップ
			}

			if (currentTime != entry.lastModified)
			{
				entry.lastModified = currentTime;
				if (entry.callback)
				{
					entry.callback(entry.path);
				}
				++changedCount;
			}
		}

		return changedCount;
	}

	/// @brief ポーリング間隔を設定する
	/// @param ms ミリ秒
	void setPollInterval(int ms)
	{
		m_pollInterval = std::chrono::milliseconds(ms);
	}

	/// @brief ポーリング間隔を取得する（ミリ秒）
	[[nodiscard]] int pollIntervalMs() const noexcept
	{
		return static_cast<int>(m_pollInterval.count());
	}

	/// @brief 監視中のファイル数を取得する
	[[nodiscard]] size_t watchCount() const noexcept { return m_entries.size(); }

	/// @brief 監視中のパス一覧を取得する
	[[nodiscard]] std::vector<std::filesystem::path> watchedPaths() const
	{
		std::vector<std::filesystem::path> paths;
		paths.reserve(m_entries.size());
		for (const auto& [key, entry] : m_entries)
		{
			paths.push_back(entry.path);
		}
		return paths;
	}

	/// @brief 全監視をクリアする
	void clear() { m_entries.clear(); }

private:
	std::unordered_map<std::string, FileWatchEntry> m_entries;
	std::chrono::milliseconds m_pollInterval;
	std::chrono::steady_clock::time_point m_lastPollTime;
};

} // namespace sgc::resource
