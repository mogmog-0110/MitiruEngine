#pragma once

/// @file SystemMessage.hpp
/// @brief システム間メッセージバス
///
/// ECSシステム間の疎結合な通信を実現するメッセージバス。
/// 型安全なpublish/subscribeパターンを提供する。
///
/// @code
/// struct DamageEvent { int entityId; float amount; };
///
/// sgc::ecs::MessageBus bus;
/// bus.subscribe<DamageEvent>([](const DamageEvent& e) {
///     // ダメージ処理
/// });
///
/// bus.publish(DamageEvent{42, 10.0f});
/// @endcode

#include <functional>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace sgc::ecs
{

/// @brief システム間メッセージの基底
///
/// 具体的なメッセージ型はこの構造体を継承する必要はないが、
/// 型安全性のためのマーカーとして使用できる。
struct IMessage
{
	/// @brief 仮想デストラクタ
	virtual ~IMessage() = default;
};

/// @brief メッセージバス（システム間通信）
///
/// 型ベースのpublish/subscribeパターンで、
/// ECSシステム間の疎結合な通信を実現する。
class MessageBus
{
public:
	/// @brief メッセージハンドラ型
	/// @tparam T メッセージ型
	template <typename T>
	using Handler = std::function<void(const T&)>;

	/// @brief メッセージハンドラを登録する
	/// @tparam T メッセージ型
	/// @param handler ハンドラ関数
	template <typename T>
	void subscribe(Handler<T> handler)
	{
		const auto key = std::type_index(typeid(T));
		m_handlers[key].push_back(
			[h = std::move(handler)](const void* msg)
			{
				h(*static_cast<const T*>(msg));
			}
		);
	}

	/// @brief メッセージを発行する
	/// @tparam T メッセージ型
	/// @param message 発行するメッセージ
	template <typename T>
	void publish(const T& message)
	{
		const auto key = std::type_index(typeid(T));
		const auto it = m_handlers.find(key);
		if (it == m_handlers.end()) return;

		for (const auto& handler : it->second)
		{
			handler(&message);
		}
	}

	/// @brief 全ハンドラを削除する
	void clear()
	{
		m_handlers.clear();
	}

	/// @brief 登録されているメッセージ型の数を返す
	/// @return メッセージ型数
	[[nodiscard]] std::size_t typeCount() const noexcept
	{
		return m_handlers.size();
	}

private:
	/// @brief 型インデックス → ハンドラリスト
	std::unordered_map<
		std::type_index,
		std::vector<std::function<void(const void*)>>
	> m_handlers;
};

} // namespace sgc::ecs
