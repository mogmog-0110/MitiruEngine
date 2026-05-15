#pragma once

/// @file CausalChain.hpp
/// @brief 因果イベント追跡システム
/// @details ゲーム内イベントの因果関係（原因→結果）をチェーンとして記録・追跡する。
///          各イベントはオプションで原因イベントへのリンクを持ち、
///          任意のイベントから因果関係を遡って根本原因を特定できる。
///
/// @code
/// mitiru::observe::CausalChain chain;
/// auto collisionId = chain.record("collision", "Player hit wall", 100);
/// auto damageId = chain.record("damage", "Player took 10 damage", 100,
///                              collisionId, {{"amount", "10"}});
/// auto deathId = chain.record("state_change", "Player died", 101,
///                             damageId, {{"state", "dead"}});
///
/// auto fullChain = chain.getChain(deathId);
/// // -> [collision, damage, state_change]
/// @endcode

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/observe/JsonEscape.hpp>

namespace mitiru::observe
{

/// @brief 因果イベントID型
using CausalEventId = std::uint32_t;

/// @brief 無効な因果イベントID（ルートイベントまたはリンクなし）
inline constexpr CausalEventId INVALID_CAUSAL_EVENT = 0;

/// @brief 因果イベント
/// @details ゲーム内の1つの出来事を表す。オプションで原因イベントへのリンクを持つ。
struct CausalEvent
{
	CausalEventId id = INVALID_CAUSAL_EVENT;                ///< イベントID
	std::string type;                                        ///< イベント種類（"collision", "damage"等）
	std::string description;                                 ///< 人間可読な説明
	std::uint64_t frame = 0;                                 ///< 発生フレーム番号
	CausalEventId causeId = INVALID_CAUSAL_EVENT;            ///< 原因イベントのID（ルートなら0）
	std::map<std::string, std::string> data;                 ///< 任意のkey-valueコンテキスト

	/// @brief JSON文字列に変換する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{\"id\":";
		json += std::to_string(id);
		json += ",\"type\":\"";
		json += jsonEscape(type);
		json += "\",\"description\":\"";
		json += jsonEscape(description);
		json += "\",\"frame\":";
		json += std::to_string(frame);
		json += ",\"causeId\":";
		json += std::to_string(causeId);
		json += ",\"data\":{";
		bool first = true;
		for (const auto& [key, value] : data)
		{
			if (!first)
			{
				json += ",";
			}
			json += "\"" + jsonEscape(key) + "\":\"" + jsonEscape(value) + "\"";
			first = false;
		}
		json += "}}";
		return json;
	}
};

/// @brief 因果イベントチェーン管理クラス
/// @details イベントの記録・因果関係の追跡・クエリ機能を提供する。
///          最大イベント数を超えると古いイベントから自動的に破棄される。
class CausalChain
{
public:
	/// @brief 最大保持イベント数を設定する
	/// @param max 最大イベント数
	void setMaxEvents(std::size_t max) noexcept
	{
		m_maxEvents = max;
	}

	/// @brief イベントを記録する
	/// @param type イベント種類
	/// @param description 人間可読な説明
	/// @param frame 発生フレーム番号
	/// @param causeId 原因イベントのID（デフォルト: INVALID_CAUSAL_EVENT = ルート）
	/// @param data 任意のkey-valueコンテキスト
	/// @return 新規イベントのID
	CausalEventId record(const std::string& type,
	                      const std::string& description,
	                      std::uint64_t frame,
	                      CausalEventId causeId = INVALID_CAUSAL_EVENT,
	                      const std::map<std::string, std::string>& data = {})
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		const CausalEventId newId = m_nextId++;

		m_events.push_back(CausalEvent{
			.id = newId,
			.type = type,
			.description = description,
			.frame = frame,
			.causeId = causeId,
			.data = data
		});

		/// 最大数を超えたら古いイベントを破棄
		if (m_events.size() > m_maxEvents)
		{
			m_events.erase(m_events.begin());
		}

		return newId;
	}

	/// @brief 指定イベントの因果チェーンを取得する（根本原因まで遡る）
	/// @param eventId 起点イベントのID
	/// @return 根本原因から起点イベントまでの因果チェーン（時系列順）
	/// @details causeId を辿って INVALID_CAUSAL_EVENT に到達するまで遡り、
	///          結果を時系列順（根本原因が先頭）に並べて返す。
	[[nodiscard]] std::vector<CausalEvent> getChain(CausalEventId eventId) const
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		std::vector<CausalEvent> chain;
		auto currentId = eventId;

		/// 循環検出用のカウンター（最大イベント数を超えたら打ち切り）
		std::size_t maxDepth = m_events.size();
		std::size_t depth = 0;

		while (currentId != INVALID_CAUSAL_EVENT && depth < maxDepth)
		{
			const auto* event = getEventImpl(currentId);
			if (event == nullptr)
			{
				break;
			}
			chain.push_back(*event);
			currentId = event->causeId;
			++depth;
		}

		/// 時系列順に反転（根本原因を先頭に）
		std::reverse(chain.begin(), chain.end());
		return chain;
	}

	/// @brief 指定イベントの直接的な結果イベントを取得する
	/// @param eventId 原因イベントのID
	/// @return causeId が指定IDと一致する全イベント
	[[nodiscard]] std::vector<CausalEvent> getEffects(CausalEventId eventId) const
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		std::vector<CausalEvent> effects;
		for (const auto& event : m_events)
		{
			if (event.causeId == eventId)
			{
				effects.push_back(event);
			}
		}
		return effects;
	}

	/// @brief 指定種類のイベントを全て取得する
	/// @param type イベント種類
	/// @return 一致する全イベント
	[[nodiscard]] std::vector<CausalEvent> getByType(std::string_view type) const
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		std::vector<CausalEvent> matched;
		for (const auto& event : m_events)
		{
			if (event.type == type)
			{
				matched.push_back(event);
			}
		}
		return matched;
	}

	/// @brief 指定フレーム範囲のイベントを取得する
	/// @param fromFrame 開始フレーム（含む）
	/// @param toFrame 終了フレーム（含む）
	/// @return 指定範囲内の全イベント
	[[nodiscard]] std::vector<CausalEvent> getInRange(
		std::uint64_t fromFrame, std::uint64_t toFrame) const
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		std::vector<CausalEvent> matched;
		for (const auto& event : m_events)
		{
			if (event.frame >= fromFrame && event.frame <= toFrame)
			{
				matched.push_back(event);
			}
		}
		return matched;
	}

	/// @brief IDでイベントを取得する
	/// @param id イベントID
	/// @return イベントへのポインタ（見つからない場合は nullptr）
	[[nodiscard]] const CausalEvent* getEvent(CausalEventId id) const
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		return getEventImpl(id);
	}

	/// @brief 全イベントをクリアする
	void clear()
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_events.clear();
		m_nextId = 1;
	}

	/// @brief 記録されたイベント数を取得する
	/// @return イベント数
	[[nodiscard]] std::size_t eventCount() const
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		return m_events.size();
	}

	/// @brief 全イベントをJSON配列文字列に変換する
	/// @return JSON配列形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		return toJson(m_events);
	}

	/// @brief イベント一覧をJSON配列文字列に変換する
	/// @param events イベントの配列
	/// @return JSON配列形式の文字列
	[[nodiscard]] static std::string toJson(const std::vector<CausalEvent>& events)
	{
		std::string json;
		json += "[";
		for (std::size_t i = 0; i < events.size(); ++i)
		{
			if (i > 0)
			{
				json += ",";
			}
			json += events[i].toJson();
		}
		json += "]";
		return json;
	}

private:
	/// @brief IDでイベントを取得する（ロック不要版）
	[[nodiscard]] const CausalEvent* getEventImpl(CausalEventId id) const noexcept
	{
		for (const auto& event : m_events)
		{
			if (event.id == id)
			{
				return &event;
			}
		}
		return nullptr;
	}

	mutable std::mutex m_mutex;                    ///< 排他制御用ミューテックス
	std::vector<CausalEvent> m_events;            ///< 記録された全イベント
	CausalEventId m_nextId = 1;                    ///< 次に割り当てるイベントID
	std::size_t m_maxEvents = 5000;                ///< 最大保持イベント数
};

} // namespace mitiru::observe
