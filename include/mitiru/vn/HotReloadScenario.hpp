#pragma once

/// @file HotReloadScenario.hpp
/// @brief VNシナリオホットリロードシステム
/// @details 開発中にシナリオスクリプトファイルの変更を検知し、ゲームを再起動せずに
///          スクリプトを再読み込みする。差分追跡、再開位置の提案、デベロッパーオーバーレイ
///          を含む開発支援ツールセット。
///
/// @code
/// mitiru::vn::ScenarioHotReloader reloader;
/// reloader.watchFile("scripts/chapter1.vns");
/// reloader.setOnReloaded([](const mitiru::vn::ReloadResult& result) {
///     if (result.success) {
///         // 新しいスクリプトを適用
///     }
/// });
///
/// // 毎フレーム
/// reloader.update();
/// @endcode

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <sys/types.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#endif

namespace mitiru::vn
{

// ════════════════════════════════════════════════════════════════════
//  リロード結果
// ════════════════════════════════════════════════════════════════════

/// @brief スクリプト差分の行変更種別
enum class LineDiffType : std::uint8_t
{
	Unchanged,   ///< 変更なし
	Added,       ///< 追加
	Removed,     ///< 削除
	Modified,    ///< 変更
};

/// @brief 行単位の差分エントリ
struct LineDiff
{
	LineDiffType type = LineDiffType::Unchanged;
	int oldLineNumber = -1;  ///< 旧スクリプトの行番号（-1: 新規追加）
	int newLineNumber = -1;  ///< 新スクリプトの行番号（-1: 削除済み）
	std::string oldText;     ///< 旧テキスト
	std::string newText;     ///< 新テキスト
};

/// @brief ラベル差分情報
struct LabelDiff
{
	std::string labelName;
	bool isNew = false;       ///< 新規ラベル
	bool isRemoved = false;   ///< 削除されたラベル
	bool isMoved = false;     ///< 位置が変わったラベル
	int oldLine = -1;         ///< 旧位置
	int newLine = -1;         ///< 新位置
};

/// @brief リロード結果
struct ReloadResult
{
	bool success = false;                  ///< パース成功かどうか
	std::string filePath;                  ///< リロードしたファイルパス
	std::vector<std::string> errors;       ///< パースエラー一覧
	std::vector<std::string> warnings;     ///< 警告一覧
	std::vector<LineDiff> changedLines;    ///< 行差分
	std::vector<LabelDiff> labelChanges;   ///< ラベル差分
	std::string suggestedLabel;            ///< 推奨再開ラベル
	int suggestedLine = 0;                 ///< 推奨再開行番号
	int totalLinesOld = 0;                 ///< 旧スクリプト総行数
	int totalLinesNew = 0;                 ///< 新スクリプト総行数
};

// ════════════════════════════════════════════════════════════════════
//  ScenarioDiffTracker
// ════════════════════════════════════════════════════════════════════

/// @brief シナリオスクリプトの差分追跡
/// @details 旧/新スクリプトを比較し、行差分・ラベル差分・再開位置の提案を生成する。
class ScenarioDiffTracker
{
public:
	/// @brief 2つのスクリプトの差分を計算する
	/// @param oldScript 旧スクリプト（行分割済み）
	/// @param newScript 新スクリプト（行分割済み）
	/// @param currentLabel 現在のラベル
	/// @param currentLine 現在の行番号
	/// @return 差分結果
	[[nodiscard]] static ReloadResult computeDiff(
		const std::vector<std::string>& oldScript,
		const std::vector<std::string>& newScript,
		const std::string& currentLabel,
		int currentLine)
	{
		ReloadResult result;
		result.success = true;
		result.totalLinesOld = static_cast<int>(oldScript.size());
		result.totalLinesNew = static_cast<int>(newScript.size());

		// ラベル位置を抽出
		auto oldLabels = extractLabels(oldScript);
		auto newLabels = extractLabels(newScript);

		// ラベル差分を計算
		result.labelChanges = computeLabelDiffs(oldLabels, newLabels);

		// 行差分を計算（簡易LCS）
		result.changedLines = computeLineDiffs(oldScript, newScript);

		// 再開位置を推定
		suggestResumePoint(result, currentLabel, currentLine, oldLabels, newLabels);

		return result;
	}

private:
	/// @brief ラベル名 → 行番号のマッピング型
	using LabelMap = std::unordered_map<std::string, int>;

	/// @brief スクリプトからラベル位置を抽出する
	[[nodiscard]] static LabelMap extractLabels(const std::vector<std::string>& lines)
	{
		LabelMap labels;
		for (std::size_t i = 0; i < lines.size(); ++i)
		{
			const auto& line = lines[i];
			auto trimmed = trimWhitespace(line);
			if (trimmed.size() > 7 && trimmed.substr(0, 7) == "@label ")
			{
				auto labelName = trimWhitespace(trimmed.substr(7));
				labels[std::string(labelName)] = static_cast<int>(i);
			}
		}
		return labels;
	}

	/// @brief ラベル差分を計算する
	[[nodiscard]] static std::vector<LabelDiff> computeLabelDiffs(
		const LabelMap& oldLabels, const LabelMap& newLabels)
	{
		std::vector<LabelDiff> diffs;

		// 旧ラベルをチェック
		for (const auto& [name, oldLine] : oldLabels)
		{
			LabelDiff diff;
			diff.labelName = name;
			diff.oldLine = oldLine;

			auto it = newLabels.find(name);
			if (it == newLabels.end())
			{
				diff.isRemoved = true;
				diff.newLine = -1;
			}
			else
			{
				diff.newLine = it->second;
				diff.isMoved = (oldLine != it->second);
			}

			if (diff.isRemoved || diff.isMoved)
			{
				diffs.push_back(diff);
			}
		}

		// 新規ラベルをチェック
		for (const auto& [name, newLine] : newLabels)
		{
			if (oldLabels.find(name) == oldLabels.end())
			{
				LabelDiff diff;
				diff.labelName = name;
				diff.isNew = true;
				diff.oldLine = -1;
				diff.newLine = newLine;
				diffs.push_back(diff);
			}
		}

		return diffs;
	}

	/// @brief 行差分を計算する（簡易版：連続一致区間ベース）
	[[nodiscard]] static std::vector<LineDiff> computeLineDiffs(
		const std::vector<std::string>& oldLines,
		const std::vector<std::string>& newLines)
	{
		std::vector<LineDiff> diffs;

		std::size_t oldIdx = 0;
		std::size_t newIdx = 0;

		while (oldIdx < oldLines.size() && newIdx < newLines.size())
		{
			if (oldLines[oldIdx] == newLines[newIdx])
			{
				// 一致
				++oldIdx;
				++newIdx;
				continue;
			}

			// 不一致 - 新スクリプト側で旧行を探す
			std::size_t newLookAhead = findLine(newLines, oldLines[oldIdx], newIdx + 1, newIdx + 20);
			std::size_t oldLookAhead = findLine(oldLines, newLines[newIdx], oldIdx + 1, oldIdx + 20);

			if (newLookAhead != std::string::npos && (oldLookAhead == std::string::npos || newLookAhead - newIdx <= oldLookAhead - oldIdx))
			{
				// 新スクリプトに行が追加された
				while (newIdx < newLookAhead)
				{
					LineDiff diff;
					diff.type = LineDiffType::Added;
					diff.newLineNumber = static_cast<int>(newIdx);
					diff.newText = newLines[newIdx];
					diffs.push_back(diff);
					++newIdx;
				}
			}
			else if (oldLookAhead != std::string::npos)
			{
				// 旧スクリプトから行が削除された
				while (oldIdx < oldLookAhead)
				{
					LineDiff diff;
					diff.type = LineDiffType::Removed;
					diff.oldLineNumber = static_cast<int>(oldIdx);
					diff.oldText = oldLines[oldIdx];
					diffs.push_back(diff);
					++oldIdx;
				}
			}
			else
			{
				// 行が変更された
				LineDiff diff;
				diff.type = LineDiffType::Modified;
				diff.oldLineNumber = static_cast<int>(oldIdx);
				diff.newLineNumber = static_cast<int>(newIdx);
				diff.oldText = oldLines[oldIdx];
				diff.newText = newLines[newIdx];
				diffs.push_back(diff);
				++oldIdx;
				++newIdx;
			}
		}

		// 残りの新規行
		while (newIdx < newLines.size())
		{
			LineDiff diff;
			diff.type = LineDiffType::Added;
			diff.newLineNumber = static_cast<int>(newIdx);
			diff.newText = newLines[newIdx];
			diffs.push_back(diff);
			++newIdx;
		}

		// 残りの削除行
		while (oldIdx < oldLines.size())
		{
			LineDiff diff;
			diff.type = LineDiffType::Removed;
			diff.oldLineNumber = static_cast<int>(oldIdx);
			diff.oldText = oldLines[oldIdx];
			diffs.push_back(diff);
			++oldIdx;
		}

		return diffs;
	}

	/// @brief 指定範囲内で行を検索する
	[[nodiscard]] static std::size_t findLine(
		const std::vector<std::string>& lines,
		const std::string& target,
		std::size_t from,
		std::size_t to)
	{
		std::size_t end = std::min(to, lines.size());
		for (std::size_t i = from; i < end; ++i)
		{
			if (lines[i] == target) return i;
		}
		return std::string::npos;
	}

	/// @brief 再開位置を推定する
	static void suggestResumePoint(
		ReloadResult& result,
		const std::string& currentLabel,
		int currentLine,
		const LabelMap& oldLabels,
		const LabelMap& newLabels)
	{
		// 現在のラベルが新スクリプトにも存在すればそこから再開
		if (!currentLabel.empty())
		{
			auto it = newLabels.find(currentLabel);
			if (it != newLabels.end())
			{
				result.suggestedLabel = currentLabel;
				result.suggestedLine = it->second;
				return;
			}
		}

		// ラベルが見つからない場合、最も近いラベルを探す
		int bestLine = 0;
		std::string bestLabel;
		for (const auto& [name, line] : newLabels)
		{
			if (line <= currentLine && line > bestLine)
			{
				bestLine = line;
				bestLabel = name;
			}
		}

		if (!bestLabel.empty())
		{
			result.suggestedLabel = bestLabel;
			result.suggestedLine = bestLine;
		}
		else
		{
			result.suggestedLabel.clear();
			result.suggestedLine = 0;
		}
	}

	/// @brief 文字列の前後空白を除去する
	[[nodiscard]] static std::string_view trimWhitespace(std::string_view sv) noexcept
	{
		while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r'))
		{
			sv.remove_prefix(1);
		}
		while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r'))
		{
			sv.remove_suffix(1);
		}
		return sv;
	}
};

// ════════════════════════════════════════════════════════════════════
//  監視対象ファイル情報
// ════════════════════════════════════════════════════════════════════

/// @brief 監視対象ファイルの状態
struct WatchedFile
{
	std::string path;                    ///< ファイルパス
	std::int64_t lastModifiedTime = 0;   ///< 最終更新時刻（stat由来）
	std::vector<std::string> lastLines;  ///< 最後に読み込んだ行内容
	bool valid = false;                  ///< 正常に読み込めたか
};

// ════════════════════════════════════════════════════════════════════
//  ScenarioHotReloader
// ════════════════════════════════════════════════════════════════════

/// @brief シナリオスクリプトのホットリロードマネージャ
/// @details ファイルの変更を定期的にチェックし、変更検知時にコールバックで通知する。
class ScenarioHotReloader
{
public:
	/// @brief リロード通知コールバック型
	using ReloadCallback = std::function<void(const ReloadResult&)>;

	/// @brief エラー通知コールバック型
	using ErrorCallback = std::function<void(const std::string& path, const std::string& error)>;

	/// @brief コンストラクタ
	/// @param checkIntervalMs ファイル変更チェック間隔（ミリ秒、デフォルト500）
	explicit ScenarioHotReloader(int checkIntervalMs = 500) noexcept
		: m_checkIntervalMs(checkIntervalMs)
	{
	}

	/// @brief ファイルを監視対象に追加する
	/// @param path ファイルパス
	/// @return 成功ならtrue
	bool watchFile(const std::string& path)
	{
		WatchedFile wf;
		wf.path = path;
		wf.lastModifiedTime = getFileModTime(path);

		if (wf.lastModifiedTime > 0)
		{
			wf.lastLines = readFileLines(path);
			wf.valid = !wf.lastLines.empty();
		}

		m_watchedFiles[path] = std::move(wf);
		return m_watchedFiles[path].valid;
	}

	/// @brief ファイルの監視を解除する
	/// @param path ファイルパス
	void unwatchFile(const std::string& path)
	{
		m_watchedFiles.erase(path);
	}

	/// @brief 全監視を解除する
	void unwatchAll()
	{
		m_watchedFiles.clear();
	}

	/// @brief リロード通知コールバックを設定する
	void setOnReloaded(ReloadCallback callback) { m_onReloaded = std::move(callback); }

	/// @brief エラー通知コールバックを設定する
	void setOnError(ErrorCallback callback) { m_onError = std::move(callback); }

	/// @brief 現在のスクリプト位置を設定する（再開位置推定用）
	/// @param label 現在のラベル
	/// @param line 現在の行番号
	void setCurrentPosition(const std::string& label, int line) noexcept
	{
		m_currentLabel = label;
		m_currentLine = line;
	}

	/// @brief 毎フレーム呼び出す更新処理
	/// @details 前回チェックからの経過時間が設定間隔を超えたら、
	///          全監視ファイルの変更をチェックする。
	void update()
	{
		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
			now - m_lastCheckTime).count();

		if (elapsed < m_checkIntervalMs) return;

		m_lastCheckTime = now;

		for (auto& [path, wf] : m_watchedFiles)
		{
			checkFile(wf);
		}
	}

	/// @brief 監視中のファイル数を取得する
	[[nodiscard]] std::size_t watchedFileCount() const noexcept
	{
		return m_watchedFiles.size();
	}

	/// @brief チェック間隔を設定する（ミリ秒）
	void setCheckInterval(int intervalMs) noexcept
	{
		m_checkIntervalMs = std::max(50, intervalMs);
	}

	/// @brief 指定ファイルの手動リロードを強制する
	/// @param path ファイルパス
	/// @return リロード結果
	[[nodiscard]] ReloadResult forceReload(const std::string& path)
	{
		auto it = m_watchedFiles.find(path);
		if (it == m_watchedFiles.end())
		{
			ReloadResult result;
			result.success = false;
			result.filePath = path;
			result.errors.push_back("File not in watch list: " + path);
			return result;
		}

		return reloadFile(it->second);
	}

private:
	/// @brief ファイルの変更をチェックする
	void checkFile(WatchedFile& wf)
	{
		std::int64_t currentModTime = getFileModTime(wf.path);

		if (currentModTime <= 0)
		{
			if (wf.valid && m_onError)
			{
				m_onError(wf.path, "File not found or inaccessible");
			}
			wf.valid = false;
			return;
		}

		if (currentModTime != wf.lastModifiedTime)
		{
			wf.lastModifiedTime = currentModTime;
			auto result = reloadFile(wf);
			if (m_onReloaded)
			{
				m_onReloaded(result);
			}
		}
	}

	/// @brief ファイルをリロードして差分を計算する
	[[nodiscard]] ReloadResult reloadFile(WatchedFile& wf)
	{
		auto newLines = readFileLines(wf.path);

		if (newLines.empty())
		{
			ReloadResult result;
			result.success = false;
			result.filePath = wf.path;
			result.errors.push_back("Failed to read file: " + wf.path);
			return result;
		}

		auto result = ScenarioDiffTracker::computeDiff(
			wf.lastLines, newLines, m_currentLabel, m_currentLine);
		result.filePath = wf.path;

		if (result.success)
		{
			wf.lastLines = std::move(newLines);
			wf.valid = true;
		}

		return result;
	}

	/// @brief ファイルの最終更新時刻を取得する
	[[nodiscard]] static std::int64_t getFileModTime(const std::string& path) noexcept
	{
#if defined(_WIN32)
		struct _stat64 st;
		if (_stat64(path.c_str(), &st) != 0) return 0;
		return static_cast<std::int64_t>(st.st_mtime);
#else
		struct stat st;
		if (stat(path.c_str(), &st) != 0) return 0;
		return static_cast<std::int64_t>(st.st_mtime);
#endif
	}

	/// @brief ファイルを行ごとに読み込む
	[[nodiscard]] static std::vector<std::string> readFileLines(const std::string& path)
	{
		std::vector<std::string> lines;
		std::ifstream file(path);
		if (!file.is_open()) return lines;

		std::string line;
		while (std::getline(file, line))
		{
			// CRを除去
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}
			lines.push_back(std::move(line));
		}

		return lines;
	}

	std::unordered_map<std::string, WatchedFile> m_watchedFiles;
	ReloadCallback m_onReloaded;
	ErrorCallback m_onError;
	std::string m_currentLabel;
	int m_currentLine = 0;
	int m_checkIntervalMs = 500;
	std::chrono::steady_clock::time_point m_lastCheckTime;
};

// ════════════════════════════════════════════════════════════════════
//  DeveloperOverlay
// ════════════════════════════════════════════════════════════════════

/// @brief 変数ウォッチエントリ
struct WatchEntry
{
	std::string name;       ///< 変数名
	std::string value;      ///< 現在の値
};

/// @brief デベロッパーオーバーレイの描画データ
struct OverlayRenderData
{
	bool visible = false;                        ///< 表示中かどうか
	float opacity = 0.7f;                        ///< 不透明度
	std::string currentLabel;                    ///< 現在のスクリプトラベル
	int currentLine = 0;                         ///< 現在の行番号
	float fps = 0.0f;                            ///< 現在のFPS
	float frameTimeMs = 0.0f;                    ///< 現在のフレーム時間（ミリ秒）
	std::vector<WatchEntry> watches;             ///< 変数ウォッチリスト
	std::string lastReloadStatus;                ///< 最後のリロードステータス
};

/// @brief シナリオ開発用デベロッパーオーバーレイ
/// @details F12キーでトグル可能な半透明オーバーレイ。
///          スクリプト位置・変数値・FPS・フレーム時間を表示する。
class DeveloperOverlay
{
public:
	/// @brief コンストラクタ
	DeveloperOverlay() noexcept = default;

	/// @brief 表示/非表示をトグルする
	void toggle() noexcept { m_visible = !m_visible; }

	/// @brief 表示状態を設定する
	void setVisible(bool visible) noexcept { m_visible = visible; }

	/// @brief 表示中かどうか
	[[nodiscard]] bool isVisible() const noexcept { return m_visible; }

	/// @brief 不透明度を設定する（0.0〜1.0）
	void setOpacity(float opacity) noexcept
	{
		m_opacity = std::clamp(opacity, 0.0f, 1.0f);
	}

	/// @brief スクリプト位置を更新する
	/// @param label 現在のラベル
	/// @param line 現在の行番号
	void updateScriptPosition(const std::string& label, int line)
	{
		m_currentLabel = label;
		m_currentLine = line;
	}

	/// @brief フレーム時間を更新する
	/// @param deltaTime フレーム時間（秒）
	void updateFrameTiming(float deltaTime) noexcept
	{
		m_frameTimeMs = deltaTime * 1000.0f;
		m_fps = (deltaTime > 0.0f) ? (1.0f / deltaTime) : 0.0f;

		// スムージング
		m_smoothedFps = m_smoothedFps * 0.95f + m_fps * 0.05f;
		m_smoothedFrameTime = m_smoothedFrameTime * 0.95f + m_frameTimeMs * 0.05f;
	}

	/// @brief 変数ウォッチを追加/更新する
	/// @param name 変数名
	/// @param value 変数値
	void setWatch(const std::string& name, const std::string& value)
	{
		for (auto& entry : m_watches)
		{
			if (entry.name == name)
			{
				entry.value = value;
				return;
			}
		}
		m_watches.push_back({name, value});
	}

	/// @brief 変数ウォッチを削除する
	/// @param name 変数名
	void removeWatch(const std::string& name)
	{
		m_watches.erase(
			std::remove_if(m_watches.begin(), m_watches.end(),
				[&name](const WatchEntry& e) { return e.name == name; }),
			m_watches.end());
	}

	/// @brief 全変数ウォッチをクリアする
	void clearWatches() { m_watches.clear(); }

	/// @brief リロードステータスを設定する
	void setReloadStatus(const std::string& status) { m_lastReloadStatus = status; }

	/// @brief 描画に必要なデータを取得する
	/// @return オーバーレイ描画データ
	[[nodiscard]] OverlayRenderData getRenderData() const
	{
		OverlayRenderData data;
		data.visible = m_visible;
		data.opacity = m_opacity;
		data.currentLabel = m_currentLabel;
		data.currentLine = m_currentLine;
		data.fps = m_smoothedFps;
		data.frameTimeMs = m_smoothedFrameTime;
		data.watches = m_watches;
		data.lastReloadStatus = m_lastReloadStatus;
		return data;
	}

	/// @brief オーバーレイ内容をプレーンテキストとして取得する（デバッグ用）
	/// @return 複数行のテキスト
	[[nodiscard]] std::string toText() const
	{
		if (!m_visible) return {};

		std::string text;
		text += "=== Developer Overlay ===\n";
		text += "Label: " + m_currentLabel + "  Line: " + std::to_string(m_currentLine) + "\n";

		char buf[64];
		std::snprintf(buf, sizeof(buf), "FPS: %.1f  Frame: %.2fms\n",
			static_cast<double>(m_smoothedFps),
			static_cast<double>(m_smoothedFrameTime));
		text += buf;

		if (!m_watches.empty())
		{
			text += "--- Watches ---\n";
			for (const auto& w : m_watches)
			{
				text += "  " + w.name + " = " + w.value + "\n";
			}
		}

		if (!m_lastReloadStatus.empty())
		{
			text += "Reload: " + m_lastReloadStatus + "\n";
		}

		return text;
	}

private:
	bool m_visible = false;
	float m_opacity = 0.7f;
	std::string m_currentLabel;
	int m_currentLine = 0;
	float m_fps = 0.0f;
	float m_frameTimeMs = 0.0f;
	float m_smoothedFps = 0.0f;
	float m_smoothedFrameTime = 0.0f;
	std::vector<WatchEntry> m_watches;
	std::string m_lastReloadStatus;
};

} // namespace mitiru::vn
