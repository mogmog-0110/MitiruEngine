#pragma once

/// @file UIEventLog.hpp
/// @brief UIイベントログ
/// @details UIノードの状態変化を記録し、フレーム・ノード・種別によるクエリを提供する。
///          最大イベント数を超過すると古いイベントから自動削除するリングバッファ方式。

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <mitiru/ui/UINode.hpp>
#include <mitiru/observe/JsonEscape.hpp>

namespace mitiru::ui
{

/// @brief UIイベントの種別
enum class UIEventType : std::uint8_t
{
	ValueChanged,      ///< プログレスバー等の値が変化した
	TextChanged,       ///< ラベル等のテキストが変化した
	VisibilityChanged, ///< 要素の表示・非表示が切り替わった
	Created,           ///< 新しい要素が追加された
	Destroyed,         ///< 要素が削除された
	Pressed,           ///< ボタンが押された
	BoundsChanged      ///< 要素の位置・サイズが変化した
};

/// @brief 単一のUIイベント
/// @details ノードの状態変化を記録するエントリ。変更前後の値を文字列で保持する。
struct UIEvent
{
	UIEventType type;      ///< イベント種別
	UINodeId nodeId;       ///< 対象ノードID
	std::string nodeName;  ///< 対象ノード名
	UIRole nodeRole;       ///< 対象ノードのロール
	std::uint64_t frame;   ///< イベント発生フレーム
	std::string oldValue;  ///< 変更前の値（文字列表現）
	std::string newValue;  ///< 変更後の値（文字列表現）

	/// @brief イベントをJSON文字列に変換する
	/// @return JSON文字列
	[[nodiscard]] std::string toJson() const
	{
		auto typeStr = [](UIEventType t) -> const char* {
			switch (t)
			{
			case UIEventType::ValueChanged:      return "ValueChanged";
			case UIEventType::TextChanged:        return "TextChanged";
			case UIEventType::VisibilityChanged:  return "VisibilityChanged";
			case UIEventType::Created:            return "Created";
			case UIEventType::Destroyed:          return "Destroyed";
			case UIEventType::Pressed:            return "Pressed";
			case UIEventType::BoundsChanged:      return "BoundsChanged";
			}
			return "Unknown";
		};

		std::string json = "{";
		json += "\"type\":\"" + std::string(typeStr(type)) + "\"";
		json += ",\"nodeId\":" + std::to_string(nodeId);
		json += ",\"nodeName\":\"" + observe::jsonEscape(nodeName) + "\"";
		json += ",\"nodeRole\":" + std::to_string(static_cast<int>(nodeRole));
		json += ",\"frame\":" + std::to_string(frame);
		json += ",\"oldValue\":\"" + observe::jsonEscape(oldValue) + "\"";
		json += ",\"newValue\":\"" + observe::jsonEscape(newValue) + "\"";
		json += "}";
		return json;
	}
};

/// @brief UIイベントログ
/// @details UIイベントをリングバッファ方式で記録し、多彩なクエリを提供する。
///          最大保持数を超えると古いイベントから自動的に削除される。
///
/// @code
/// mitiru::ui::UIEventLog log;
/// log.setMaxEvents(500);
///
/// mitiru::ui::UIEvent ev;
/// ev.type = mitiru::ui::UIEventType::ValueChanged;
/// ev.nodeId = 42;
/// ev.nodeName = "hp_bar";
/// ev.nodeRole = mitiru::ui::UIRole::HealthBar;
/// ev.frame = 120;
/// ev.oldValue = "1.0";
/// ev.newValue = "0.75";
/// log.record(std::move(ev));
///
/// auto hpEvents = log.eventsForNode(42);
/// @endcode
class UIEventLog
{
	std::vector<UIEvent> m_events;   ///< イベントバッファ
	std::size_t m_maxEvents = 1000;  ///< 最大保持イベント数

public:
	/// @brief 最大保持イベント数を設定する
	/// @param max 最大イベント数
	void setMaxEvents(std::size_t max)
	{
		m_maxEvents = max;
		trimOldest();
	}

	/// @brief イベントを記録する
	/// @param event 記録するイベント
	/// @note 最大保持数を超過した場合、最も古いイベントが削除される
	void record(UIEvent event)
	{
		m_events.push_back(std::move(event));
		trimOldest();
	}

	/// @brief 全イベントをクリアする
	void clear()
	{
		m_events.clear();
	}

	/// @brief 全イベントを取得する
	/// @return イベントのconst参照
	[[nodiscard]] const std::vector<UIEvent>& events() const noexcept
	{
		return m_events;
	}

	/// @brief 指定ノードIDのイベントを取得する
	/// @param id 対象ノードID
	/// @return マッチしたイベントのリスト
	[[nodiscard]] std::vector<UIEvent> eventsForNode(UINodeId id) const
	{
		std::vector<UIEvent> result;
		for (const auto& ev : m_events)
		{
			if (ev.nodeId == id)
			{
				result.push_back(ev);
			}
		}
		return result;
	}

	/// @brief 指定フレーム以降のイベントを取得する
	/// @param frame 開始フレーム番号（この値を含む）
	/// @return マッチしたイベントのリスト
	[[nodiscard]] std::vector<UIEvent> eventsSinceFrame(std::uint64_t frame) const
	{
		std::vector<UIEvent> result;
		for (const auto& ev : m_events)
		{
			if (ev.frame >= frame)
			{
				result.push_back(ev);
			}
		}
		return result;
	}

	/// @brief 指定種別のイベントを取得する
	/// @param type イベント種別
	/// @return マッチしたイベントのリスト
	[[nodiscard]] std::vector<UIEvent> eventsByType(UIEventType type) const
	{
		std::vector<UIEvent> result;
		for (const auto& ev : m_events)
		{
			if (ev.type == type)
			{
				result.push_back(ev);
			}
		}
		return result;
	}

	/// @brief 記録されたイベント数を取得する
	/// @return イベント数
	[[nodiscard]] std::size_t eventCount() const noexcept
	{
		return m_events.size();
	}

	/// @brief 全イベントをJSON配列文字列に変換する
	/// @return JSON配列文字列
	[[nodiscard]] std::string toJson() const
	{
		return toJson(m_events);
	}

	/// @brief 指定イベントリストをJSON配列文字列に変換する
	/// @param events JSON化するイベントリスト
	/// @return JSON配列文字列
	[[nodiscard]] std::string toJson(const std::vector<UIEvent>& events) const
	{
		std::string json = "[";
		for (std::size_t i = 0; i < events.size(); ++i)
		{
			if (i > 0) { json += ","; }
			json += events[i].toJson();
		}
		json += "]";
		return json;
	}

private:
	/// @brief 最大保持数を超過した古いイベントを削除する
	void trimOldest()
	{
		if (m_events.size() > m_maxEvents)
		{
			const auto excess = m_events.size() - m_maxEvents;
			m_events.erase(m_events.begin(),
				m_events.begin() + static_cast<std::ptrdiff_t>(excess));
		}
	}
};

} // namespace mitiru::ui
