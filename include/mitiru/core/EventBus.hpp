#pragma once

/// @file EventBus.hpp
/// @brief エンジン全体のPub/Subイベントシステム
/// @details 型安全なイベント発行・購読機構を提供する。
///          同期発行（publish）と遅延発行（publishDeferred）に対応。
///          スレッドセーフオプション付き。

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace mitiru
{

// ── 共通イベント型 ────────────────────────────────

/// @brief ウィンドウリサイズイベント
struct WindowResizeEvent
{
	int width = 0;   ///< 新しい幅（ピクセル）
	int height = 0;  ///< 新しい高さ（ピクセル）
};

/// @brief シーン遷移イベント
struct SceneChangeEvent
{
	std::string fromScene;  ///< 遷移元シーン名
	std::string toScene;    ///< 遷移先シーン名
};

/// @brief 入力イベント種別
enum class InputEventType : uint8_t
{
	KeyDown = 0,  ///< キー押下
	KeyUp,        ///< キー離上
	MouseDown,    ///< マウスボタン押下
	MouseUp,      ///< マウスボタン離上
	MouseMove,    ///< マウス移動
};

/// @brief 入力イベント
struct InputEvent
{
	InputEventType type = InputEventType::KeyDown; ///< イベント種別
	int key = 0;        ///< キーコード
	float mouseX = 0.0f; ///< マウスX座標
	float mouseY = 0.0f; ///< マウスY座標
};

/// @brief オーディオイベント種別
enum class AudioEventType : uint8_t
{
	Play = 0,   ///< 再生開始
	Stop,       ///< 停止
	Pause,      ///< 一時停止
	Resume,     ///< 再開
};

/// @brief オーディオイベント
struct AudioEvent
{
	AudioEventType type = AudioEventType::Play; ///< イベント種別
	int trackId = -1;                           ///< トラックID
};

/// @brief ゲーム状態イベント種別
enum class GameStateEventType : uint8_t
{
	Started = 0,  ///< ゲーム開始
	Paused,       ///< 一時停止
	Resumed,      ///< 再開
	Saved,        ///< セーブ完了
	Loaded,       ///< ロード完了
	Custom,       ///< カスタム
};

/// @brief ゲーム状態イベント
struct GameStateEvent
{
	GameStateEventType type = GameStateEventType::Custom; ///< イベント種別
	std::string data;                                     ///< 任意データ（JSON等）
};

// ── サブスクリプションID ───────────────────────────

/// @brief サブスクリプション識別子
using SubscriptionId = uint64_t;

// ── EventBus ──────────────────────────────────────

/// @brief エンジン全体のPub/Subイベントバス
/// @details 型安全にイベントを発行・購読する。
///          ハンドラは具体的なイベント型を直接受け取る。
///
/// @code
/// mitiru::EventBus bus;
///
/// auto id = bus.subscribe<mitiru::WindowResizeEvent>([](const auto& e) {
///     // e.width, e.height が使える
/// });
///
/// bus.publish(mitiru::WindowResizeEvent{1280, 720});
/// bus.unsubscribe(id);
/// @endcode
class EventBus
{
public:
	/// @brief デフォルトコンストラクタ
	EventBus() = default;

	/// コピー禁止
	EventBus(const EventBus&) = delete;
	EventBus& operator=(const EventBus&) = delete;

	/// ムーブ禁止（mutex保持のため）
	EventBus(EventBus&&) = delete;
	EventBus& operator=(EventBus&&) = delete;

	/// @brief イベントを購読する
	/// @tparam EventType イベントの型
	/// @param handler イベントハンドラ
	/// @return サブスクリプションID（解除に使用）
	template <typename EventType>
	SubscriptionId subscribe(std::function<void(const EventType&)> handler)
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		const auto id = m_nextId++;
		const auto key = std::type_index(typeid(EventType));

		auto wrapper = std::make_unique<TypedHandler<EventType>>(id, std::move(handler));
		m_handlers[key].push_back(std::move(wrapper));

		return id;
	}

	/// @brief サブスクリプションを解除する
	/// @param subscriptionId 解除するサブスクリプションID
	void unsubscribe(SubscriptionId subscriptionId)
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		for (auto& [key, handlers] : m_handlers)
		{
			auto it = std::remove_if(handlers.begin(), handlers.end(),
				[subscriptionId](const std::unique_ptr<HandlerBase>& h) {
					return h->id == subscriptionId;
				});
			handlers.erase(it, handlers.end());
		}
	}

	/// @brief イベントを即座に発行する
	/// @tparam EventType イベントの型
	/// @param event 発行するイベント
	template <typename EventType>
	void publish(const EventType& event)
	{
		std::vector<HandlerBase*> snapshot;

		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			const auto key = std::type_index(typeid(EventType));
			const auto it = m_handlers.find(key);
			if (it == m_handlers.end())
			{
				return;
			}
			snapshot.reserve(it->second.size());
			for (const auto& h : it->second)
			{
				snapshot.push_back(h.get());
			}
		}

		// ロック外でハンドラを呼び出す（デッドロック防止）
		for (auto* handler : snapshot)
		{
			static_cast<TypedHandler<EventType>*>(handler)->invoke(event);
		}
	}

	/// @brief イベントを遅延キューに追加する
	/// @tparam EventType イベントの型
	/// @param event キューに入れるイベント
	/// @details 次のprocessDeferred()呼び出し時に発行される
	template <typename EventType>
	void publishDeferred(const EventType& event)
	{
		const std::lock_guard<std::mutex> lock(m_deferredMutex);
		m_deferredQueue.push_back(std::make_unique<DeferredEvent<EventType>>(event));
	}

	/// @brief 遅延キューのイベントをすべて発行する
	/// @details ゲームループのフレーム末尾で呼び出すことを想定
	void processDeferred()
	{
		std::vector<std::unique_ptr<DeferredEventBase>> pending;

		{
			const std::lock_guard<std::mutex> lock(m_deferredMutex);
			pending.swap(m_deferredQueue);
		}

		for (const auto& deferred : pending)
		{
			deferred->dispatch(*this);
		}
	}

	/// @brief 指定型の購読者数を取得する（テスト・デバッグ用）
	/// @tparam EventType イベントの型
	/// @return 購読者数
	template <typename EventType>
	[[nodiscard]] std::size_t subscriberCount() const
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		const auto key = std::type_index(typeid(EventType));
		const auto it = m_handlers.find(key);
		if (it == m_handlers.end())
		{
			return 0;
		}
		return it->second.size();
	}

	/// @brief すべての購読を解除する
	void clear()
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_handlers.clear();
	}

private:
	// ── ハンドラの型消去機構 ───────────────────

	/// @brief ハンドラ基底（型消去用）
	struct HandlerBase
	{
		SubscriptionId id = 0;
		explicit HandlerBase(SubscriptionId subId) : id(subId) {}
		virtual ~HandlerBase() = default;
	};

	/// @brief 型付きハンドラ
	/// @tparam EventType 処理するイベントの型
	template <typename EventType>
	struct TypedHandler final : HandlerBase
	{
		std::function<void(const EventType&)> handler;

		TypedHandler(SubscriptionId subId, std::function<void(const EventType&)> h)
			: HandlerBase(subId), handler(std::move(h))
		{
		}

		void invoke(const EventType& event) const
		{
			if (handler)
			{
				handler(event);
			}
		}
	};

	// ── 遅延イベントの型消去機構 ─────────────────

	/// @brief 遅延イベント基底
	struct DeferredEventBase
	{
		virtual ~DeferredEventBase() = default;
		virtual void dispatch(EventBus& bus) const = 0;
	};

	/// @brief 型付き遅延イベント
	/// @tparam EventType イベントの型
	template <typename EventType>
	struct DeferredEvent final : DeferredEventBase
	{
		EventType event;

		explicit DeferredEvent(const EventType& e) : event(e) {}

		void dispatch(EventBus& bus) const override
		{
			bus.publish(event);
		}
	};

	// ── メンバ変数 ────────────────────────────

	mutable std::mutex m_mutex;      ///< ハンドラマップ用ミューテックス
	std::mutex m_deferredMutex;      ///< 遅延キュー用ミューテックス
	SubscriptionId m_nextId = 1;     ///< 次のサブスクリプションID

	/// @brief イベント型→ハンドラリストのマップ
	std::unordered_map<std::type_index, std::vector<std::unique_ptr<HandlerBase>>> m_handlers;

	/// @brief 遅延発行キュー
	std::vector<std::unique_ptr<DeferredEventBase>> m_deferredQueue;
};

} // namespace mitiru
