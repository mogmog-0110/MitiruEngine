#pragma once

/// @file PlayerAnalytics.hpp
/// @brief VNプレイヤー行動分析システム
/// @details プレイヤーの選択傾向・読了速度・離脱地点・ルート攻略率を追跡する。
///          リングバッファによるイベント蓄積と、JSON形式でのエクスポートを提供する。
///          A/Bテストやストーリー分岐の最適化に活用できる。
///
/// @code
/// mitiru::vn::PlayerAnalytics analytics;
/// analytics.recordChoice("chapter1", 0, 3);  // 3択中0番を選択
/// analytics.recordLineRead("chapter1", 5, 800);  // 5行目を800msで読了
/// analytics.recordSceneEnter("chapter1");
/// analytics.recordSceneExit("chapter1");
/// analytics.recordRouteComplete("route_a", "ending_true");
///
/// auto stats = analytics.getChoiceStats("chapter1");
/// auto speed = analytics.getAverageReadSpeed();
/// auto drops = analytics.getDropOffPoints();
/// std::string json = analytics.toJson();
/// @endcode

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace mitiru::vn
{

// ════════════════════════════════════════════════════════════════════
//  イベント定義
// ════════════════════════════════════════════════════════════════════

/// @brief 分析イベントの種別
enum class AnalyticsEventType : std::uint8_t
{
	Choice,         ///< 選択肢の選択
	LineRead,       ///< テキスト行の読了
	SceneEnter,     ///< シーンへの遷移
	SceneExit,      ///< シーンからの退出
	RouteComplete,  ///< ルート/エンディング到達
	SkipUsed,       ///< スキップ機能使用
	AutoUsed,       ///< オート機能使用
	SessionStart,   ///< セッション開始
	SessionEnd,     ///< セッション終了
};

/// @brief 分析イベントデータ
struct AnalyticsEvent
{
	AnalyticsEventType type = AnalyticsEventType::LineRead;
	std::int64_t timestampMs = 0;      ///< Unixエポックからのミリ秒
	std::string sceneId;               ///< 関連シーンID
	std::string routeId;               ///< 関連ルートID（RouteComplete用）
	std::string endingId;              ///< 関連エンディングID（RouteComplete用）
	int choiceIndex = -1;              ///< 選択したインデックス（Choice用）
	int totalChoices = 0;              ///< 選択肢の総数（Choice用）
	int lineIndex = -1;                ///< 行インデックス（LineRead用）
	int readTimeMs = 0;                ///< 読了時間ミリ秒（LineRead用）
};

// ════════════════════════════════════════════════════════════════════
//  統計データ構造
// ════════════════════════════════════════════════════════════════════

/// @brief 選択肢ごとの統計情報
struct ChoiceStatEntry
{
	int choiceIndex = 0;               ///< 選択肢インデックス
	int selectionCount = 0;            ///< 選択された回数
	float percentage = 0.0f;           ///< 選択割合（0.0〜100.0）
};

/// @brief シーン単位の選択統計
struct SceneChoiceStats
{
	std::string sceneId;
	int totalSelections = 0;           ///< 合計選択回数
	std::vector<ChoiceStatEntry> entries; ///< 各選択肢の統計
};

/// @brief 離脱地点情報
struct DropOffPoint
{
	std::string sceneId;
	int exitCount = 0;                 ///< 退出回数
	int enterCount = 0;                ///< 入場回数
	float exitRate = 0.0f;             ///< 離脱率（0.0〜100.0）
};

// ════════════════════════════════════════════════════════════════════
//  リングバッファ
// ════════════════════════════════════════════════════════════════════

/// @brief 固定容量リングバッファ
/// @tparam T 要素型
template <typename T>
class RingBuffer
{
public:
	/// @brief コンストラクタ
	/// @param capacity 最大要素数
	explicit RingBuffer(std::size_t capacity = 10000)
		: m_buffer(capacity)
		, m_capacity(capacity)
	{
	}

	/// @brief 要素を追加する（容量超過時は最古の要素を上書き）
	/// @param item 追加する要素
	void push(const T& item)
	{
		m_buffer[m_writePos % m_capacity] = item;
		++m_writePos;
		if (m_size < m_capacity)
		{
			++m_size;
		}
	}

	/// @brief 要素を追加する（ムーブ版）
	void push(T&& item)
	{
		m_buffer[m_writePos % m_capacity] = std::move(item);
		++m_writePos;
		if (m_size < m_capacity)
		{
			++m_size;
		}
	}

	/// @brief 現在の要素数
	[[nodiscard]] std::size_t size() const noexcept { return m_size; }

	/// @brief 容量
	[[nodiscard]] std::size_t capacity() const noexcept { return m_capacity; }

	/// @brief 空かどうか
	[[nodiscard]] bool empty() const noexcept { return m_size == 0; }

	/// @brief インデックスで要素にアクセス（0が最古）
	/// @param index アクセスインデックス
	/// @return 要素への参照
	[[nodiscard]] const T& at(std::size_t index) const
	{
		if (index >= m_size)
		{
			static T empty{};
			return empty;
		}
		std::size_t actualIndex = 0;
		if (m_size == m_capacity)
		{
			actualIndex = (m_writePos + index) % m_capacity;
		}
		else
		{
			actualIndex = index;
		}
		return m_buffer[actualIndex];
	}

	/// @brief 全要素を古い順に返す
	[[nodiscard]] std::vector<T> toVector() const
	{
		std::vector<T> result;
		result.reserve(m_size);
		for (std::size_t i = 0; i < m_size; ++i)
		{
			result.push_back(at(i));
		}
		return result;
	}

	/// @brief 全要素を削除する
	void clear() noexcept
	{
		m_size = 0;
		m_writePos = 0;
	}

private:
	std::vector<T> m_buffer;
	std::size_t m_capacity = 0;
	std::size_t m_writePos = 0;
	std::size_t m_size = 0;
};

// ════════════════════════════════════════════════════════════════════
//  PlayerAnalytics 本体
// ════════════════════════════════════════════════════════════════════

/// @brief VNプレイヤー行動分析システム
/// @details イベントをリングバッファに蓄積し、統計クエリとJSONエクスポートを提供する。
class PlayerAnalytics
{
public:
	/// @brief コンストラクタ
	/// @param maxEvents リングバッファの最大イベント数
	explicit PlayerAnalytics(std::size_t maxEvents = 10000)
		: m_events(maxEvents)
	{
	}

	// ── イベント記録 ──────────────────────────────────────────

	/// @brief 選択肢の選択を記録する
	/// @param sceneId シーンID
	/// @param choiceIndex 選択したインデックス（0始まり）
	/// @param totalChoices 選択肢の総数
	void recordChoice(const std::string& sceneId, int choiceIndex, int totalChoices)
	{
		AnalyticsEvent ev;
		ev.type = AnalyticsEventType::Choice;
		ev.timestampMs = currentTimestampMs();
		ev.sceneId = sceneId;
		ev.choiceIndex = choiceIndex;
		ev.totalChoices = totalChoices;
		m_events.push(std::move(ev));

		// 集計データ更新
		auto& stats = m_choiceAggregates[sceneId];
		if (choiceIndex >= 0 && choiceIndex < totalChoices)
		{
			if (stats.size() < static_cast<std::size_t>(totalChoices))
			{
				stats.resize(static_cast<std::size_t>(totalChoices), 0);
			}
			++stats[static_cast<std::size_t>(choiceIndex)];
		}
		++m_totalChoiceSelections[sceneId];
	}

	/// @brief テキスト行の読了を記録する
	/// @param sceneId シーンID
	/// @param lineIndex 行インデックス
	/// @param readTimeMs 読了時間（ミリ秒）
	void recordLineRead(const std::string& sceneId, int lineIndex, int readTimeMs)
	{
		AnalyticsEvent ev;
		ev.type = AnalyticsEventType::LineRead;
		ev.timestampMs = currentTimestampMs();
		ev.sceneId = sceneId;
		ev.lineIndex = lineIndex;
		ev.readTimeMs = readTimeMs;
		m_events.push(std::move(ev));

		m_totalReadTimeMs += static_cast<std::int64_t>(readTimeMs);
		++m_totalLinesRead;
	}

	/// @brief シーン入場を記録する
	/// @param sceneId シーンID
	void recordSceneEnter(const std::string& sceneId)
	{
		AnalyticsEvent ev;
		ev.type = AnalyticsEventType::SceneEnter;
		ev.timestampMs = currentTimestampMs();
		ev.sceneId = sceneId;
		m_events.push(std::move(ev));

		++m_sceneEnterCounts[sceneId];
		m_currentScene = sceneId;
	}

	/// @brief シーン退出を記録する
	/// @param sceneId シーンID
	void recordSceneExit(const std::string& sceneId)
	{
		AnalyticsEvent ev;
		ev.type = AnalyticsEventType::SceneExit;
		ev.timestampMs = currentTimestampMs();
		ev.sceneId = sceneId;
		m_events.push(std::move(ev));

		++m_sceneExitCounts[sceneId];
		if (m_currentScene == sceneId)
		{
			m_currentScene.clear();
		}
	}

	/// @brief ルート/エンディング完了を記録する
	/// @param routeId ルートID
	/// @param endingId エンディングID
	void recordRouteComplete(const std::string& routeId, const std::string& endingId)
	{
		AnalyticsEvent ev;
		ev.type = AnalyticsEventType::RouteComplete;
		ev.timestampMs = currentTimestampMs();
		ev.routeId = routeId;
		ev.endingId = endingId;
		m_events.push(std::move(ev));

		++m_routeCompletions[routeId];
		m_endingCompletions[routeId + "/" + endingId]++;
	}

	/// @brief スキップ機能使用を記録する
	void recordSkipUsed()
	{
		AnalyticsEvent ev;
		ev.type = AnalyticsEventType::SkipUsed;
		ev.timestampMs = currentTimestampMs();
		ev.sceneId = m_currentScene;
		m_events.push(std::move(ev));

		++m_skipCount;
	}

	/// @brief オート機能使用を記録する
	void recordAutoUsed()
	{
		AnalyticsEvent ev;
		ev.type = AnalyticsEventType::AutoUsed;
		ev.timestampMs = currentTimestampMs();
		ev.sceneId = m_currentScene;
		m_events.push(std::move(ev));

		++m_autoCount;
	}

	/// @brief セッション開始を記録する
	void recordSessionStart()
	{
		AnalyticsEvent ev;
		ev.type = AnalyticsEventType::SessionStart;
		ev.timestampMs = currentTimestampMs();
		m_events.push(std::move(ev));

		m_sessionStartMs = ev.timestampMs;
		++m_totalSessions;
	}

	/// @brief セッション終了を記録する
	void recordSessionEnd()
	{
		AnalyticsEvent ev;
		ev.type = AnalyticsEventType::SessionEnd;
		ev.timestampMs = currentTimestampMs();
		m_events.push(std::move(ev));

		if (m_sessionStartMs > 0)
		{
			std::int64_t sessionDuration = ev.timestampMs - m_sessionStartMs;
			m_totalPlayTimeMs += sessionDuration;
			m_lastSessionDurationMs = sessionDuration;
			m_sessionStartMs = 0;
		}
	}

	// ── 統計クエリ ──────────────────────────────────────────

	/// @brief シーンの選択肢統計を取得する
	/// @param sceneId シーンID
	/// @return 選択肢ごとの割合を含む統計
	[[nodiscard]] SceneChoiceStats getChoiceStats(const std::string& sceneId) const
	{
		SceneChoiceStats result;
		result.sceneId = sceneId;

		auto aggIt = m_choiceAggregates.find(sceneId);
		auto totalIt = m_totalChoiceSelections.find(sceneId);
		if (aggIt == m_choiceAggregates.end() || totalIt == m_totalChoiceSelections.end())
		{
			return result;
		}

		result.totalSelections = totalIt->second;
		const auto& counts = aggIt->second;
		result.entries.reserve(counts.size());

		for (std::size_t i = 0; i < counts.size(); ++i)
		{
			ChoiceStatEntry entry;
			entry.choiceIndex = static_cast<int>(i);
			entry.selectionCount = counts[i];
			entry.percentage = (result.totalSelections > 0)
				? (static_cast<float>(counts[i]) / static_cast<float>(result.totalSelections) * 100.0f)
				: 0.0f;
			result.entries.push_back(entry);
		}

		return result;
	}

	/// @brief 全シーンの選択肢統計を取得する
	/// @return シーンIDをキーとした統計マップ
	[[nodiscard]] std::map<std::string, SceneChoiceStats> getAllChoiceStats() const
	{
		std::map<std::string, SceneChoiceStats> result;
		for (const auto& [sceneId, _] : m_choiceAggregates)
		{
			result[sceneId] = getChoiceStats(sceneId);
		}
		return result;
	}

	/// @brief 平均読了速度を取得する
	/// @return 1行あたりの平均読了時間（ミリ秒）。データがない場合は0
	[[nodiscard]] float getAverageReadTimeMs() const noexcept
	{
		if (m_totalLinesRead == 0) return 0.0f;
		return static_cast<float>(m_totalReadTimeMs) / static_cast<float>(m_totalLinesRead);
	}

	/// @brief 平均読了速度を文字毎秒で取得する（推定：1行平均30文字として計算）
	/// @param avgCharsPerLine 1行あたりの推定文字数（デフォルト30）
	/// @return 文字毎秒
	[[nodiscard]] float getAverageReadSpeed(float avgCharsPerLine = 30.0f) const noexcept
	{
		float avgMs = getAverageReadTimeMs();
		if (avgMs <= 0.0f) return 0.0f;
		return avgCharsPerLine / (avgMs / 1000.0f);
	}

	/// @brief 離脱率の高いシーンを取得する
	/// @param maxResults 最大結果数（デフォルト10）
	/// @return 離脱率降順の離脱地点リスト
	[[nodiscard]] std::vector<DropOffPoint> getDropOffPoints(std::size_t maxResults = 10) const
	{
		std::vector<DropOffPoint> points;

		for (const auto& [sceneId, exitCount] : m_sceneExitCounts)
		{
			DropOffPoint point;
			point.sceneId = sceneId;
			point.exitCount = exitCount;

			auto enterIt = m_sceneEnterCounts.find(sceneId);
			point.enterCount = (enterIt != m_sceneEnterCounts.end()) ? enterIt->second : 0;
			point.exitRate = (point.enterCount > 0)
				? (static_cast<float>(point.exitCount) / static_cast<float>(point.enterCount) * 100.0f)
				: 0.0f;

			points.push_back(point);
		}

		std::sort(points.begin(), points.end(),
			[](const DropOffPoint& a, const DropOffPoint& b) {
				return a.exitRate > b.exitRate;
			});

		if (points.size() > maxResults)
		{
			points.resize(maxResults);
		}

		return points;
	}

	/// @brief ルート完了率を取得する
	/// @return ルートIDをキーとした完了回数マップ
	[[nodiscard]] std::map<std::string, int> getRouteCompletionCounts() const
	{
		return std::map<std::string, int>(m_routeCompletions.begin(), m_routeCompletions.end());
	}

	/// @brief ルート完了率を取得する（全セッション比）
	/// @return ルートIDをキーとした完了率（0.0〜100.0）
	[[nodiscard]] std::map<std::string, float> getRouteCompletionRates() const
	{
		std::map<std::string, float> result;
		if (m_totalSessions == 0) return result;

		for (const auto& [routeId, count] : m_routeCompletions)
		{
			result[routeId] = static_cast<float>(count) / static_cast<float>(m_totalSessions) * 100.0f;
		}
		return result;
	}

	/// @brief スキップ使用回数を取得する
	[[nodiscard]] int skipCount() const noexcept { return m_skipCount; }

	/// @brief オート使用回数を取得する
	[[nodiscard]] int autoCount() const noexcept { return m_autoCount; }

	/// @brief 総プレイ時間をミリ秒で取得する
	[[nodiscard]] std::int64_t totalPlayTimeMs() const noexcept { return m_totalPlayTimeMs; }

	/// @brief 最後のセッション時間をミリ秒で取得する
	[[nodiscard]] std::int64_t lastSessionDurationMs() const noexcept { return m_lastSessionDurationMs; }

	/// @brief 総セッション数を取得する
	[[nodiscard]] int totalSessions() const noexcept { return m_totalSessions; }

	/// @brief 総読了行数を取得する
	[[nodiscard]] std::int64_t totalLinesRead() const noexcept { return m_totalLinesRead; }

	/// @brief 最も人気のある選択肢を取得する（全シーン横断）
	/// @return {sceneId, choiceIndex, selectionCount} の上位リスト
	[[nodiscard]] std::vector<ChoiceStatEntry> getMostPopularChoices(std::size_t maxResults = 10) const
	{
		std::vector<std::pair<std::string, ChoiceStatEntry>> all;

		for (const auto& [sceneId, counts] : m_choiceAggregates)
		{
			for (std::size_t i = 0; i < counts.size(); ++i)
			{
				ChoiceStatEntry entry;
				entry.choiceIndex = static_cast<int>(i);
				entry.selectionCount = counts[i];
				all.push_back({sceneId, entry});
			}
		}

		std::sort(all.begin(), all.end(),
			[](const auto& a, const auto& b) {
				return a.second.selectionCount > b.second.selectionCount;
			});

		std::vector<ChoiceStatEntry> result;
		std::size_t count = std::min(maxResults, all.size());
		for (std::size_t i = 0; i < count; ++i)
		{
			result.push_back(all[i].second);
		}
		return result;
	}

	/// @brief イベントリングバッファへの参照を取得する
	[[nodiscard]] const RingBuffer<AnalyticsEvent>& events() const noexcept { return m_events; }

	// ── JSONエクスポート ─────────────────────────────────────────

	/// @brief 全分析データをJSON文字列としてエクスポートする
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json = "{";

		// サマリー
		json += "\"summary\":{";
		json += "\"totalSessions\":" + std::to_string(m_totalSessions);
		json += ",\"totalPlayTimeMs\":" + std::to_string(m_totalPlayTimeMs);
		json += ",\"totalLinesRead\":" + std::to_string(m_totalLinesRead);
		json += ",\"averageReadTimeMs\":" + floatToString(getAverageReadTimeMs());
		json += ",\"skipCount\":" + std::to_string(m_skipCount);
		json += ",\"autoCount\":" + std::to_string(m_autoCount);
		json += "}";

		// 選択肢統計
		json += ",\"choiceStats\":{";
		bool firstScene = true;
		for (const auto& [sceneId, counts] : m_choiceAggregates)
		{
			if (!firstScene) json += ",";
			json += "\"" + escapeJson(sceneId) + "\":[";
			auto stats = getChoiceStats(sceneId);
			bool firstEntry = true;
			for (const auto& entry : stats.entries)
			{
				if (!firstEntry) json += ",";
				json += "{\"index\":" + std::to_string(entry.choiceIndex);
				json += ",\"count\":" + std::to_string(entry.selectionCount);
				json += ",\"percentage\":" + floatToString(entry.percentage) + "}";
				firstEntry = false;
			}
			json += "]";
			firstScene = false;
		}
		json += "}";

		// 離脱地点
		json += ",\"dropOffPoints\":[";
		auto drops = getDropOffPoints(50);
		bool firstDrop = true;
		for (const auto& point : drops)
		{
			if (!firstDrop) json += ",";
			json += "{\"sceneId\":\"" + escapeJson(point.sceneId) + "\"";
			json += ",\"enterCount\":" + std::to_string(point.enterCount);
			json += ",\"exitCount\":" + std::to_string(point.exitCount);
			json += ",\"exitRate\":" + floatToString(point.exitRate) + "}";
			firstDrop = false;
		}
		json += "]";

		// ルート完了
		json += ",\"routeCompletions\":{";
		bool firstRoute = true;
		for (const auto& [routeId, count] : m_routeCompletions)
		{
			if (!firstRoute) json += ",";
			json += "\"" + escapeJson(routeId) + "\":" + std::to_string(count);
			firstRoute = false;
		}
		json += "}";

		// 最新イベント
		json += ",\"recentEvents\":[";
		auto allEvents = m_events.toVector();
		std::size_t eventStart = (allEvents.size() > 100) ? allEvents.size() - 100 : 0;
		bool firstEvent = true;
		for (std::size_t i = eventStart; i < allEvents.size(); ++i)
		{
			if (!firstEvent) json += ",";
			json += eventToJson(allEvents[i]);
			firstEvent = false;
		}
		json += "]";

		json += "}";
		return json;
	}

	// ── リセット ────────────────────────────────────────────────

	/// @brief 全データをリセットする
	void reset()
	{
		m_events.clear();
		m_choiceAggregates.clear();
		m_totalChoiceSelections.clear();
		m_sceneEnterCounts.clear();
		m_sceneExitCounts.clear();
		m_routeCompletions.clear();
		m_endingCompletions.clear();
		m_totalReadTimeMs = 0;
		m_totalLinesRead = 0;
		m_skipCount = 0;
		m_autoCount = 0;
		m_totalPlayTimeMs = 0;
		m_lastSessionDurationMs = 0;
		m_sessionStartMs = 0;
		m_totalSessions = 0;
		m_currentScene.clear();
	}

private:
	/// @brief 現在のタイムスタンプをミリ秒で取得する
	[[nodiscard]] static std::int64_t currentTimestampMs() noexcept
	{
		auto now = std::chrono::system_clock::now();
		auto epoch = now.time_since_epoch();
		return std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count();
	}

	/// @brief JSON文字列をエスケープする
	[[nodiscard]] static std::string escapeJson(const std::string& s)
	{
		std::string result;
		result.reserve(s.size());
		for (char c : s)
		{
			switch (c)
			{
			case '"':  result += "\\\""; break;
			case '\\': result += "\\\\"; break;
			case '\n': result += "\\n";  break;
			case '\r': result += "\\r";  break;
			case '\t': result += "\\t";  break;
			default:   result += c;      break;
			}
		}
		return result;
	}

	/// @brief 浮動小数点数を文字列化する（小数点2桁）
	[[nodiscard]] static std::string floatToString(float value)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(value));
		return std::string(buf);
	}

	/// @brief イベント種別を文字列化する
	[[nodiscard]] static std::string eventTypeName(AnalyticsEventType type)
	{
		switch (type)
		{
		case AnalyticsEventType::Choice:        return "choice";
		case AnalyticsEventType::LineRead:       return "lineRead";
		case AnalyticsEventType::SceneEnter:     return "sceneEnter";
		case AnalyticsEventType::SceneExit:      return "sceneExit";
		case AnalyticsEventType::RouteComplete:  return "routeComplete";
		case AnalyticsEventType::SkipUsed:       return "skipUsed";
		case AnalyticsEventType::AutoUsed:       return "autoUsed";
		case AnalyticsEventType::SessionStart:   return "sessionStart";
		case AnalyticsEventType::SessionEnd:     return "sessionEnd";
		}
		return "unknown";
	}

	/// @brief 単一イベントをJSON文字列化する
	[[nodiscard]] static std::string eventToJson(const AnalyticsEvent& ev)
	{
		std::string json = "{";
		json += "\"type\":\"" + eventTypeName(ev.type) + "\"";
		json += ",\"timestampMs\":" + std::to_string(ev.timestampMs);

		if (!ev.sceneId.empty())
		{
			json += ",\"sceneId\":\"" + escapeJson(ev.sceneId) + "\"";
		}
		if (!ev.routeId.empty())
		{
			json += ",\"routeId\":\"" + escapeJson(ev.routeId) + "\"";
		}
		if (!ev.endingId.empty())
		{
			json += ",\"endingId\":\"" + escapeJson(ev.endingId) + "\"";
		}
		if (ev.choiceIndex >= 0)
		{
			json += ",\"choiceIndex\":" + std::to_string(ev.choiceIndex);
			json += ",\"totalChoices\":" + std::to_string(ev.totalChoices);
		}
		if (ev.lineIndex >= 0)
		{
			json += ",\"lineIndex\":" + std::to_string(ev.lineIndex);
			json += ",\"readTimeMs\":" + std::to_string(ev.readTimeMs);
		}

		json += "}";
		return json;
	}

	// ── メンバー ─────────────────────────────────────────────────

	RingBuffer<AnalyticsEvent> m_events;                                    ///< イベントリングバッファ

	// 選択肢集計: sceneId → [choiceIndex → count]
	std::unordered_map<std::string, std::vector<int>> m_choiceAggregates;
	std::unordered_map<std::string, int> m_totalChoiceSelections;

	// シーン入退出集計
	std::unordered_map<std::string, int> m_sceneEnterCounts;
	std::unordered_map<std::string, int> m_sceneExitCounts;

	// ルート完了集計
	std::unordered_map<std::string, int> m_routeCompletions;
	std::unordered_map<std::string, int> m_endingCompletions;

	// 読了速度集計
	std::int64_t m_totalReadTimeMs = 0;
	std::int64_t m_totalLinesRead = 0;

	// 機能使用集計
	int m_skipCount = 0;
	int m_autoCount = 0;

	// セッション管理
	std::int64_t m_totalPlayTimeMs = 0;
	std::int64_t m_lastSessionDurationMs = 0;
	std::int64_t m_sessionStartMs = 0;
	int m_totalSessions = 0;

	// 現在の状態
	std::string m_currentScene;
};

} // namespace mitiru::vn
