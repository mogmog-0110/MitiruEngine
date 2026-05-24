#pragma once

/// @file ServiceLocator.hpp
/// @brief 型安全なサービスロケータパターン
///
/// グローバルなシングルトンの代替。テスト時にサービスの差し替えが容易。

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>

namespace sgc
{

/// @brief 型安全なサービスロケータ
///
/// インターフェース型をキーにしてサービスインスタンスを管理する。
/// DIコンテナの簡易版として使用できる。
///
/// @code
/// struct IAudio { virtual void play(const char* name) = 0; virtual ~IAudio() = default; };
/// struct SDLAudio : IAudio { void play(const char* name) override { /* ... */ } };
///
/// sgc::ServiceLocator locator;
/// locator.provide<IAudio>(std::make_shared<SDLAudio>());
///
/// auto& audio = locator.get<IAudio>();
/// audio.play("bgm");
/// @endcode
class ServiceLocator
{
public:
	/// @brief サービスを登録する
	/// @tparam T サービスのインターフェース型
	/// @param service サービスインスタンス（shared_ptr）
	template <typename T>
	void provide(std::shared_ptr<T> service)
	{
		std::unique_lock lock(m_mutex);
		m_services[std::type_index(typeid(T))] = std::move(service);
	}

	/// @brief サービスを取得する
	/// @tparam T サービスのインターフェース型
	/// @return サービスへの参照
	/// @throws std::runtime_error サービスが未登録の場合
	template <typename T>
	[[nodiscard]] T& get() const
	{
		std::shared_lock lock(m_mutex);
		const auto it = m_services.find(std::type_index(typeid(T)));
		if (it == m_services.end())
		{
			throw std::runtime_error("ServiceLocator: service not registered");
		}
		return *std::static_pointer_cast<T>(it->second);
	}

	/// @brief サービスが登録されているか判定する
	/// @tparam T サービスのインターフェース型
	/// @return 登録済みなら true
	template <typename T>
	[[nodiscard]] bool has() const
	{
		std::shared_lock lock(m_mutex);
		return m_services.contains(std::type_index(typeid(T)));
	}

	/// @brief サービスの登録を解除する
	/// @tparam T サービスのインターフェース型
	template <typename T>
	void remove()
	{
		std::unique_lock lock(m_mutex);
		m_services.erase(std::type_index(typeid(T)));
	}

	/// @brief 全サービスをクリアする
	void clear()
	{
		std::unique_lock lock(m_mutex);
		m_services.clear();
	}

private:
	mutable std::shared_mutex m_mutex;  ///< スレッド安全性のためのミューテックス
	std::unordered_map<std::type_index, std::shared_ptr<void>> m_services;
};

} // namespace sgc
