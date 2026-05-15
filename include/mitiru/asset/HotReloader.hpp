#pragma once

/// @file HotReloader.hpp
/// @brief シェーダーホットリロードコーディネーター
/// @details FileWatcher と ShaderCache を連携させ、シェーダーファイルの変更を
///          検出して自動的に再コンパイル・差し替えを行う。
///          コンパイル失敗時は旧シェーダーを保持するフェイルセーフ設計。
///
/// @code
/// mitiru::asset::ShaderCache cache;
/// mitiru::asset::HotReloader reloader(cache);
/// reloader.registerShader("shaders/main.vert", gfx::ShaderType::Vertex,
///     [](const ShaderHandle& h) { pipeline.setVertexShader(h); });
/// // メインループ内で:
/// reloader.update();
/// @endcode

#include "FileWatcher.hpp"
#include "ShaderCache.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace mitiru::asset
{

/// @brief リロードイベントのログエントリ
struct ReloadLogEntry
{
	std::string path;              ///< シェーダーファイルパス
	bool success{false};           ///< リロード成功フラグ
	std::string message;           ///< 結果メッセージ
	std::chrono::steady_clock::time_point timestamp{
		std::chrono::steady_clock::now()};  ///< タイムスタンプ
};

/// @brief リロードコールバック型（新しいハンドルを受け取る）
using ReloadCallback = std::function<void(const ShaderHandle&)>;

/// @brief シェーダーホットリロードコーディネーター
/// @details FileWatcher でシェーダーファイルを監視し、変更時に ShaderCache で
///          再コンパイルして結果をコールバックで通知する。
class HotReloader
{
public:
	/// @brief ShaderCache参照で構築
	/// @param cache 使用するシェーダーキャッシュ
	explicit HotReloader(ShaderCache& cache)
		: m_cache(cache)
	{
	}

	/// @brief シェーダーファイルを監視登録する
	/// @param filepath シェーダーファイルのパス
	/// @param type シェーダー種別
	/// @param callback リロード成功時のコールバック
	void registerShader(const std::string& filepath, gfx::ShaderType type,
	                    ReloadCallback callback = nullptr)
	{
		std::scoped_lock lock(m_mutex);

		ShaderEntry entry;
		entry.filepath = filepath;
		entry.type = type;
		entry.callback = std::move(callback);

		// 初回コンパイルを試みる
		std::error_code ec;
		if (std::filesystem::exists(filepath, ec))
		{
			const auto source = readFile(filepath);
			if (!source.empty())
			{
				auto result = m_cache.getOrCompile(source, type);
				if (result.success)
				{
					entry.currentHandle = result.handle;
					entry.lastSource = source;
				}
			}
		}

		m_shaders[filepath] = std::move(entry);

		// FileWatcher にファイルまたはその親ディレクトリを登録
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
			// ファイル自体を直接監視
			m_watcher.watch(filepath, [this](const FileEvent& event) {
				std::scoped_lock lock(m_pendingMutex);
				m_pendingEvents.push(event);
			});
		}
	}

	/// @brief シェーダーファイルの監視を解除する
	/// @param filepath 解除するファイルパス
	void unregisterShader(const std::string& filepath)
	{
		std::scoped_lock lock(m_mutex);
		m_shaders.erase(filepath);
	}

	/// @brief フレーム更新 — 保留中のリロードを処理する
	/// @details メインループから毎フレーム呼び出す。
	///          FileWatcher をポーリングし、変更があればリコンパイルする。
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

		// ポーリングフォールバック: 登録済みシェーダーを直接チェック
		for (auto& [filepath, entry] : m_shaders)
		{
			std::error_code ec;
			if (!std::filesystem::exists(filepath, ec)) continue;

			const auto source = readFile(filepath);
			if (source.empty() || source == entry.lastSource) continue;

			recompileShader(filepath, entry, source);
		}
	}

	/// @brief 登録済みシェーダー数を取得する
	[[nodiscard]] std::size_t shaderCount() const noexcept
	{
		std::scoped_lock lock(m_mutex);
		return m_shaders.size();
	}

	/// @brief リロードログを取得する
	[[nodiscard]] std::vector<ReloadLogEntry> reloadLog() const
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

	/// @brief 特定シェーダーの現在のハンドルを取得する
	/// @param filepath シェーダーファイルパス
	/// @return 有効なハンドル、未登録ならnullopt
	[[nodiscard]] std::optional<ShaderHandle> currentHandle(const std::string& filepath) const
	{
		std::scoped_lock lock(m_mutex);
		auto it = m_shaders.find(filepath);
		if (it == m_shaders.end()) return std::nullopt;
		if (!it->second.currentHandle.valid()) return std::nullopt;
		return it->second.currentHandle;
	}

	/// @brief 強制リロードを実行する
	/// @param filepath 再コンパイルするファイルパス
	/// @return リコンパイル結果
	ShaderCompileResult forceReload(const std::string& filepath)
	{
		std::scoped_lock lock(m_mutex);

		auto it = m_shaders.find(filepath);
		if (it == m_shaders.end())
		{
			return {false, {}, "shader not registered: " + filepath, {}};
		}

		const auto source = readFile(filepath);
		if (source.empty())
		{
			return {false, {}, "failed to read file: " + filepath, {}};
		}

		// キャッシュの既存エントリを無効化して再コンパイル強制
		if (it->second.currentHandle.valid())
		{
			m_cache.invalidate(it->second.currentHandle.sourceHash);
		}

		return recompileShader(filepath, it->second, source);
	}

	/// @brief FileWatcher への参照を取得する
	[[nodiscard]] FileWatcher& watcher() noexcept { return m_watcher; }
	[[nodiscard]] const FileWatcher& watcher() const noexcept { return m_watcher; }

private:
	/// @brief シェーダーエントリ
	struct ShaderEntry
	{
		std::string filepath;
		gfx::ShaderType type{gfx::ShaderType::Vertex};
		ReloadCallback callback;
		ShaderHandle currentHandle{};
		std::string lastSource;
	};

	/// @brief ファイル変更を処理する
	void processFileChange(const std::string& changedPath)
	{
		// 正規化して比較
		std::error_code ec;
		std::string normalizedChanged = changedPath;
		try
		{
			const auto canonical = std::filesystem::canonical(changedPath, ec);
			if (!ec)
			{
				normalizedChanged = canonical.string();
			}
		}
		catch (...) {}

		for (auto& [filepath, entry] : m_shaders)
		{
			std::string normalizedEntry = filepath;
			try
			{
				const auto canonical = std::filesystem::canonical(filepath, ec);
				if (!ec)
				{
					normalizedEntry = canonical.string();
				}
			}
			catch (...) {}

			if (normalizedChanged == normalizedEntry ||
			    changedPath == filepath)
			{
				const auto source = readFile(filepath);
				if (!source.empty() && source != entry.lastSource)
				{
					recompileShader(filepath, entry, source);
				}
			}
		}
	}

	/// @brief シェーダーを再コンパイルする
	/// @return コンパイル結果
	ShaderCompileResult recompileShader(const std::string& filepath,
	                                     ShaderEntry& entry,
	                                     const std::string& newSource)
	{
		// 旧キャッシュを無効化
		if (entry.currentHandle.valid())
		{
			m_cache.invalidate(entry.currentHandle.sourceHash);
		}

		auto result = m_cache.getOrCompile(newSource, entry.type);

		ReloadLogEntry logEntry;
		logEntry.path = filepath;

		if (result.success)
		{
			entry.currentHandle = result.handle;
			entry.lastSource = newSource;
			++m_successCount;

			logEntry.success = true;
			logEntry.message = "recompiled successfully";

			if (entry.callback)
			{
				entry.callback(result.handle);
			}
		}
		else
		{
			// フェイルセーフ: 旧シェーダーを維持
			++m_failureCount;

			logEntry.success = false;
			logEntry.message = "compile failed: " + result.errorMessage +
			                   " (keeping previous shader)";
		}

		m_log.push_back(std::move(logEntry));
		return result;
	}

	/// @brief ファイルを読み込む
	/// @param filepath ファイルパス
	/// @return ファイル内容（失敗時は空文字列）
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
	ShaderCache& m_cache;
	FileWatcher m_watcher;
	std::unordered_map<std::string, ShaderEntry> m_shaders;
	std::set<std::string> m_watchedDirs;
	std::queue<FileEvent> m_pendingEvents;
	std::vector<ReloadLogEntry> m_log;
	uint64_t m_successCount{0};
	uint64_t m_failureCount{0};
};

} // namespace mitiru::asset
