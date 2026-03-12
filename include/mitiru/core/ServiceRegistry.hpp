#pragma once

/// @file ServiceRegistry.hpp
/// @brief サービスレジストリ
/// @details エンジンサブシステムを型安全に登録・取得するサービスロケータ。
///          シングルトンではなくインスタンスベースで運用する。

#include <memory>
#include <typeindex>
#include <unordered_map>

namespace mitiru
{

/// @brief サービスレジストリ（インスタンスベース）
/// @details shared_ptr による型消去ストレージで任意のサブシステムを管理する。
///          IDevice, IWindow, FrameTimer 等のエンジンサービスを登録・取得できる。
///
/// @code
/// mitiru::ServiceRegistry registry;
///
/// auto device = std::make_shared<mitiru::gfx::NullDevice>();
/// registry.registerService<mitiru::gfx::IDevice>(device);
///
/// auto* dev = registry.get<mitiru::gfx::IDevice>();
/// @endcode
class ServiceRegistry
{
public:
	/// @brief デフォルトコンストラクタ
	ServiceRegistry() = default;

	/// コピー禁止
	ServiceRegistry(const ServiceRegistry&) = delete;
	ServiceRegistry& operator=(const ServiceRegistry&) = delete;

	/// ムーブ許可
	ServiceRegistry(ServiceRegistry&&) noexcept = default;
	ServiceRegistry& operator=(ServiceRegistry&&) noexcept = default;

	/// @brief サービスを登録する
	/// @tparam T サービスの型（インターフェース型を推奨）
	/// @param service 登録するサービスの shared_ptr
	template <typename T>
	void registerService(std::shared_ptr<T> service)
	{
		const auto key = std::type_index(typeid(T));
		m_services[key] = std::move(service);
	}

	/// @brief サービスを取得する
	/// @tparam T サービスの型
	/// @return 登録済みサービスへのポインタ（未登録時は nullptr）
	template <typename T>
	[[nodiscard]] T* get() const
	{
		const auto key = std::type_index(typeid(T));
		const auto it = m_services.find(key);
		if (it == m_services.end())
		{
			return nullptr;
		}
		return static_cast<T*>(it->second.get());
	}

	/// @brief サービスが登録済みかどうかを判定する
	/// @tparam T サービスの型
	/// @return 登録済みなら true
	template <typename T>
	[[nodiscard]] bool has() const
	{
		const auto key = std::type_index(typeid(T));
		return m_services.find(key) != m_services.end();
	}

	/// @brief サービスの登録を解除する
	/// @tparam T サービスの型
	/// @return 解除に成功した場合 true
	template <typename T>
	bool unregister()
	{
		const auto key = std::type_index(typeid(T));
		return m_services.erase(key) > 0;
	}

	/// @brief 全サービスの登録を解除する
	void clear()
	{
		m_services.clear();
	}

	/// @brief 登録済みサービス数を取得する
	/// @return サービス数
	[[nodiscard]] std::size_t size() const noexcept
	{
		return m_services.size();
	}

	/// @brief サービスが1つも登録されていないか判定する
	/// @return 空なら true
	[[nodiscard]] bool empty() const noexcept
	{
		return m_services.empty();
	}

private:
	/// @brief 型インデックスをキーとしたサービスマップ
	std::unordered_map<std::type_index, std::shared_ptr<void>> m_services;
};

} // namespace mitiru
