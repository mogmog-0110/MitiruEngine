#pragma once

/// @file EventDispatcher.hpp
/// @brief 型安全なイベントディスパッチャー（1:Nイベントバス）
///
/// イベント型をキーとして、リスナーを登録・発火するパブリッシュ/サブスクライブシステム。
/// 仮想関数を使わず、std::function + std::type_indexで実現する。
/// スレッドセーフ（shared_mutexによる読み書きロック）。
///
/// @note Observer (Signal/Slot)との使い分け:
///   - EventDispatcher: 1:Nのイベントバス。グローバルイベントや疎結合な通知に使用。
///   - Observer (Signal/Slot): 1:1の直接通知。コンポーネント間の直接的な依存関係に使用。

#include <algorithm>
#include <any>
#include <cstdint>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace sgc
{

/// @brief イベントリスナーの登録ID（解除に使用）
using ListenerId = std::uint64_t;

/// @brief リスナー優先度
enum class Priority : std::int32_t
{
	Low = 0,      ///< 低優先度
	Normal = 1,   ///< 通常優先度（デフォルト）
	High = 2      ///< 高優先度
};

/// @brief 型安全なイベントディスパッチャー
///
/// 任意のイベント型に対してリスナーを登録し、イベント発火時に通知する。
///
/// @code
/// struct DamageEvent { int amount; };
/// struct HealEvent { int amount; };
///
/// sgc::EventDispatcher dispatcher;
///
/// auto id = dispatcher.on<DamageEvent>([](const DamageEvent& e) {
///     // ダメージ処理
/// });
///
/// dispatcher.emit(DamageEvent{50});
/// dispatcher.off(id);
/// @endcode
class EventDispatcher
{
public:
	/// @brief イベントリスナーを登録する
	/// @tparam EventType イベントの型
	/// @param callback イベント受信時に呼ばれるコールバック
	/// @param priority 優先度（高い順にディスパッチされる）
	/// @return リスナーID（off()で解除に使用）
	template <typename EventType>
	ListenerId on(std::function<void(const EventType&)> callback,
				  Priority priority = Priority::Normal)
	{
		std::unique_lock lock(m_mutex);
		const auto id = m_nextId++;
		const auto typeIdx = std::type_index(typeid(EventType));

		m_listeners[typeIdx].push_back({
			id,
			[cb = std::move(callback)](const std::any& event)
			{
				cb(std::any_cast<const EventType&>(event));
			},
			static_cast<std::int32_t>(priority),
			false  // once
		});

		return id;
	}

	/// @brief 1回限りのイベントリスナーを登録する
	///
	/// 最初のイベント受信後に自動的に解除される。
	///
	/// @tparam EventType イベントの型
	/// @param callback イベント受信時に呼ばれるコールバック
	/// @param priority 優先度
	/// @return リスナーID
	template <typename EventType>
	ListenerId once(std::function<void(const EventType&)> callback,
					Priority priority = Priority::Normal)
	{
		std::unique_lock lock(m_mutex);
		const auto id = m_nextId++;
		const auto typeIdx = std::type_index(typeid(EventType));

		m_listeners[typeIdx].push_back({
			id,
			[cb = std::move(callback)](const std::any& event)
			{
				cb(std::any_cast<const EventType&>(event));
			},
			static_cast<std::int32_t>(priority),
			true  // once
		});

		return id;
	}

	/// @brief リスナーを解除する
	/// @param id on()で取得したリスナーID
	void off(ListenerId id)
	{
		std::unique_lock lock(m_mutex);
		for (auto& [typeIdx, entries] : m_listeners)
		{
			auto it = std::remove_if(entries.begin(), entries.end(),
				[id](const ListenerEntry& entry) { return entry.id == id; });
			entries.erase(it, entries.end());
		}
	}

	/// @brief イベントを発火する
	///
	/// スレッドセーフ。リスナーリストをコピー後、優先度順にソートしてディスパッチする。
	/// コールバック内でoff()が呼ばれても安全。
	/// once()リスナーは発火後に自動解除される。
	///
	/// @tparam EventType イベントの型
	/// @param event 発火するイベント
	template <typename EventType>
	void emit(const EventType& event)
	{
		std::vector<ListenerEntry> listeners;
		{
			std::shared_lock lock(m_mutex);
			const auto typeIdx = std::type_index(typeid(EventType));
			const auto it = m_listeners.find(typeIdx);
			if (it == m_listeners.end()) return;
			listeners = it->second;
		}

		// 優先度でソート（降順: High→Normal→Low）
		std::stable_sort(listeners.begin(), listeners.end(),
			[](const ListenerEntry& a, const ListenerEntry& b)
			{
				return a.priority > b.priority;
			});

		// once()リスナーのIDを収集
		std::vector<ListenerId> onceIds;

		for (const auto& entry : listeners)
		{
			entry.callback(event);
			if (entry.once)
			{
				onceIds.push_back(entry.id);
			}
		}

		// once()リスナーを解除
		if (!onceIds.empty())
		{
			std::unique_lock lock(m_mutex);
			const auto typeIdx = std::type_index(typeid(EventType));
			auto mapIt = m_listeners.find(typeIdx);
			if (mapIt != m_listeners.end())
			{
				auto& entries = mapIt->second;
				for (const auto onceId : onceIds)
				{
					auto it = std::remove_if(entries.begin(), entries.end(),
						[onceId](const ListenerEntry& e) { return e.id == onceId; });
					entries.erase(it, entries.end());
				}
			}
		}
	}

	/// @brief 指定イベント型のリスナーを全て解除する
	/// @tparam EventType イベントの型
	template <typename EventType>
	void clearListeners()
	{
		std::unique_lock lock(m_mutex);
		m_listeners.erase(std::type_index(typeid(EventType)));
	}

	/// @brief 全リスナーを解除する
	void clearAll()
	{
		std::unique_lock lock(m_mutex);
		m_listeners.clear();
	}

private:
	/// @brief リスナーエントリ（型消去済み）
	struct ListenerEntry
	{
		ListenerId id;
		std::function<void(const std::any&)> callback;
		std::int32_t priority{1};  ///< 優先度（大きいほど先に実行）
		bool once{false};          ///< 1回限りフラグ
	};

	mutable std::shared_mutex m_mutex;  ///< スレッド安全性のためのミューテックス
	std::unordered_map<std::type_index, std::vector<ListenerEntry>> m_listeners;
	ListenerId m_nextId = 1;
};

} // namespace sgc
