#pragma once

/// @file VulkanInstance.hpp
/// @brief VkInstanceラッパー
/// @details Vulkanインスタンスの生成・バリデーションレイヤー管理を提供する。
///          MITIRU_HAS_VULKANが定義されている場合のみコンパイルされる。

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace mitiru::gfx
{

/// @brief Vulkanインスタンス生成に必要なアプリケーション情報
struct VulkanAppInfo
{
	std::string appName = "MitiruApp";      ///< アプリケーション名
	std::uint32_t appVersion = 1;           ///< アプリケーションバージョン
	std::string engineName = "MitiruEngine"; ///< エンジン名
	std::uint32_t engineVersion = 1;        ///< エンジンバージョン
	std::uint32_t apiVersion = 0;           ///< Vulkan APIバージョン（0 = VK_API_VERSION_1_0）
};

/// @brief バリデーションレイヤー設定
struct VulkanValidationConfig
{
	bool enableValidation = false;                       ///< バリデーションレイヤーの有効化
	bool enableDebugMessenger = false;                   ///< デバッグメッセンジャーの有効化
	std::vector<std::string> requiredLayers;             ///< 必須レイヤー名リスト

	/// @brief デフォルトのデバッグ設定を取得する
	/// @return 標準バリデーション有効の設定
	[[nodiscard]] static VulkanValidationConfig debug() noexcept
	{
		VulkanValidationConfig config;
		config.enableValidation = true;
		config.enableDebugMessenger = true;
		config.requiredLayers.emplace_back("VK_LAYER_KHRONOS_validation");
		return config;
	}

	/// @brief リリース用の設定を取得する（バリデーション無効）
	/// @return 全て無効の設定
	[[nodiscard]] static VulkanValidationConfig release() noexcept
	{
		return VulkanValidationConfig{};
	}
};

/// @brief Vulkanインスタンス生成パラメータ
struct VulkanInstanceDesc
{
	VulkanAppInfo appInfo;                               ///< アプリケーション情報
	VulkanValidationConfig validation;                   ///< バリデーション設定
	std::vector<std::string> requiredExtensions;         ///< 必須インスタンス拡張名リスト

	/// @brief 拡張が要求リストに含まれているか確認する
	/// @param extensionName 拡張名
	/// @return 含まれていればtrue
	[[nodiscard]] bool hasExtension(const std::string& extensionName) const noexcept
	{
		return std::find(
			requiredExtensions.begin(),
			requiredExtensions.end(),
			extensionName) != requiredExtensions.end();
	}

	/// @brief 拡張を追加する（重複しない場合のみ）
	/// @param extensionName 拡張名
	void addExtension(const std::string& extensionName)
	{
		if (!hasExtension(extensionName))
		{
			requiredExtensions.push_back(extensionName);
		}
	}

	/// @brief サーフェス関連の標準拡張を追加する
	/// @details VK_KHR_surfaceを追加する。
	///          プラットフォーム固有の拡張（VK_KHR_win32_surface等）は別途追加が必要。
	void addSurfaceExtensions()
	{
		addExtension("VK_KHR_surface");
	}
};

#ifdef MITIRU_HAS_VULKAN

/// @brief VkInstanceのRAIIラッパー
/// @details Vulkanインスタンスの生成・破棄をRAIIで管理する。
///          バリデーションレイヤーとデバッグメッセンジャーの設定も担当する。
///
/// @code
/// VulkanInstanceDesc desc;
/// desc.appInfo.appName = "MyGame";
/// desc.validation = VulkanValidationConfig::debug();
/// desc.addSurfaceExtensions();
///
/// VulkanInstance instance(desc);
/// auto* vkInstance = instance.handle();
/// @endcode
class VulkanInstance
{
public:
	/// @brief コンストラクタ
	/// @param desc インスタンス生成パラメータ
	explicit VulkanInstance(const VulkanInstanceDesc& desc)
		: m_desc(desc)
	{
		/// TODO: vkCreateInstance呼び出し
		/// TODO: デバッグメッセンジャーの設定
	}

	/// @brief デストラクタ
	~VulkanInstance()
	{
		/// TODO: vkDestroyInstance呼び出し
		/// TODO: デバッグメッセンジャーの破棄
	}

	/// コピー禁止
	VulkanInstance(const VulkanInstance&) = delete;
	VulkanInstance& operator=(const VulkanInstance&) = delete;

	/// ムーブ禁止
	VulkanInstance(VulkanInstance&&) = delete;
	VulkanInstance& operator=(VulkanInstance&&) = delete;

	/// @brief 生成時のパラメータを取得する
	/// @return インスタンス生成パラメータへのconst参照
	[[nodiscard]] const VulkanInstanceDesc& desc() const noexcept
	{
		return m_desc;
	}

	/// @brief バリデーションレイヤーが有効かどうかを判定する
	/// @return 有効ならtrue
	[[nodiscard]] bool isValidationEnabled() const noexcept
	{
		return m_desc.validation.enableValidation;
	}

private:
	VulkanInstanceDesc m_desc;  ///< 生成パラメータ
};

#endif // MITIRU_HAS_VULKAN

} // namespace mitiru::gfx
