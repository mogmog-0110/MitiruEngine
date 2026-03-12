#pragma once

/// @file EventBridge.hpp
/// @brief sgcイベント統合ブリッジ
/// @details sgcのEventDispatcherをMitiruエンジンに統合する。
///          型安全なイベント発行・購読を提供する。

#include <cstdint>
#include <functional>
#include <string>

#include <sgc/patterns/EventDispatcher.hpp>

namespace mitiru::bridge
{

/// @brief sgcイベント統合ブリッジ
/// @details EventDispatcherをラップし、型安全なイベント発行・購読を提供する。
///
/// @code
/// struct DamageEvent { int amount; };
///
/// mitiru::bridge::EventBridge events;
///
/// auto id = events.on<DamageEvent>([](const DamageEvent& e) {
///     // ダメージ処理
/// });
///
/// events.emit(DamageEvent{50});
/// events.off(id);
/// @endcode
class EventBridge
{
public:
	/// @brief イベントを発火する
	/// @tparam T イベント型
	/// @param event 発火するイベント
	template <typename T>
	void emit(const T& event)
	{
		m_dispatcher.emit<T>(event);
	}

	/// @brief イベントリスナーを登録する
	/// @tparam T イベント型
	/// @param handler イベント受信時に呼ばれるコールバック
	/// @return リスナーID（off()で解除に使用）
	template <typename T>
	sgc::ListenerId on(std::function<void(const T&)> handler)
	{
		return m_dispatcher.on<T>(std::move(handler));
	}

	/// @brief リスナーを解除する
	/// @param listenerId on()で取得したリスナーID
	void off(sgc::ListenerId listenerId)
	{
		m_dispatcher.off(listenerId);
	}

	/// @brief 内部ディスパッチャーへの読み取り専用アクセス
	/// @return EventDispatcherへのconst参照
	[[nodiscard]] const sgc::EventDispatcher& dispatcher() const noexcept
	{
		return m_dispatcher;
	}

	// ── シリアライズ ────────────────────────────────────────

	/// @brief イベントブリッジ状態をJSON文字列として返す
	/// @return JSON形式の文字列
	/// @note EventDispatcherは型消去されたリスナーを保持するため、
	///       詳細な状態をシリアライズすることは困難。
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"type\":\"EventBridge\"";
		json += "}";
		return json;
	}

private:
	sgc::EventDispatcher m_dispatcher;  ///< イベントディスパッチャー
};

} // namespace mitiru::bridge
