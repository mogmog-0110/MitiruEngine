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

#include <stdexcept>
#include <vulkan/vulkan.h>

/// @brief VkInstanceのRAIIラッパー
/// @details Vulkanインスタンスの生成・破棄をRAIIで管理する。
///          バリデーションレイヤーとデバッグメッセンジャーの設定も担当する。
///          macOS MoltenVK環境ではVK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAMEを
///          自動的に有効化する。
///
/// @code
/// VulkanInstanceDesc desc;
/// desc.appInfo.appName = "MyGame";
/// desc.validation = VulkanValidationConfig::debug();
/// desc.addSurfaceExtensions();
///
/// VulkanInstance instance(desc);
/// VkInstance vkInst = instance.handle();
/// @endcode
class VulkanInstance
{
public:
	/// @brief コンストラクタ
	/// @param desc インスタンス生成パラメータ
	/// @throws std::runtime_error vkCreateInstanceが失敗した場合
	explicit VulkanInstance(const VulkanInstanceDesc& desc)
		: m_desc(desc)
	{
		const auto& ai = desc.appInfo;

		VkApplicationInfo appInfo{};
		appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName   = ai.appName.c_str();
		appInfo.applicationVersion = ai.appVersion;
		appInfo.pEngineName        = ai.engineName.c_str();
		appInfo.engineVersion      = ai.engineVersion;
		appInfo.apiVersion         = (ai.apiVersion != 0) ? ai.apiVersion : VK_API_VERSION_1_0;

		/// descで要求された拡張をコピーし、macOS用拡張を追加する
		std::vector<const char*> extensions;
		extensions.reserve(desc.requiredExtensions.size() + 2);
		for (const auto& ext : desc.requiredExtensions)
		{
			extensions.push_back(ext.c_str());
		}

#ifdef __APPLE__
		extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
		extensions.push_back("VK_KHR_get_physical_device_properties2");
#endif

		/// バリデーションレイヤー名をchar*に変換する
		std::vector<const char*> layers;
		if (desc.validation.enableValidation)
		{
			layers.reserve(desc.validation.requiredLayers.size());
			for (const auto& layer : desc.validation.requiredLayers)
			{
				layers.push_back(layer.c_str());
			}
		}

		/// デバッグメッセンジャー設定（インスタンス生成時に有効化する場合）
		VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
		if (desc.validation.enableDebugMessenger)
		{
			debugCreateInfo.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			debugCreateInfo.messageSeverity =
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			debugCreateInfo.messageType     =
				VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			debugCreateInfo.pfnUserCallback = debugCallback;
		}

		VkInstanceCreateInfo createInfo{};
		createInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo        = &appInfo;
		createInfo.enabledExtensionCount   = static_cast<std::uint32_t>(extensions.size());
		createInfo.ppEnabledExtensionNames = extensions.data();
		createInfo.enabledLayerCount       = static_cast<std::uint32_t>(layers.size());
		createInfo.ppEnabledLayerNames     = layers.empty() ? nullptr : layers.data();

#ifdef __APPLE__
		createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

		if (desc.validation.enableDebugMessenger)
		{
			createInfo.pNext = &debugCreateInfo;
		}

		if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS)
		{
			throw std::runtime_error("vkCreateInstance failed");
		}

		/// デバッグメッセンジャーを登録する（拡張が利用可能な場合のみ）
		if (desc.validation.enableDebugMessenger)
		{
			createDebugMessenger(debugCreateInfo);
		}
	}

	/// @brief デストラクタ
	~VulkanInstance()
	{
		if (m_debugMessenger != VK_NULL_HANDLE)
		{
			destroyDebugMessenger();
		}
		if (m_instance != VK_NULL_HANDLE)
		{
			vkDestroyInstance(m_instance, nullptr);
		}
	}

	/// コピー禁止
	VulkanInstance(const VulkanInstance&) = delete;
	VulkanInstance& operator=(const VulkanInstance&) = delete;

	/// ムーブ禁止
	VulkanInstance(VulkanInstance&&) = delete;
	VulkanInstance& operator=(VulkanInstance&&) = delete;

	/// @brief 内部のVkInstanceハンドルを取得する
	/// @return VkInstanceハンドル
	[[nodiscard]] VkInstance handle() const noexcept
	{
		return m_instance;
	}

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
	/// @brief デバッグメッセンジャーを生成する
	/// @param createInfo デバッグメッセンジャー生成情報
	void createDebugMessenger(const VkDebugUtilsMessengerCreateInfoEXT& createInfo)
	{
		auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
			vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
		if (func)
		{
			func(m_instance, &createInfo, nullptr, &m_debugMessenger);
		}
	}

	/// @brief デバッグメッセンジャーを破棄する
	void destroyDebugMessenger()
	{
		auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
			vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
		if (func)
		{
			func(m_instance, m_debugMessenger, nullptr);
		}
		m_debugMessenger = VK_NULL_HANDLE;
	}

	/// @brief デバッグメッセンジャーコールバック
	static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT,
		VkDebugUtilsMessageTypeFlagsEXT,
		const VkDebugUtilsMessengerCallbackDataEXT*,
		void*) noexcept
	{
		return VK_FALSE;
	}

	VulkanInstanceDesc             m_desc;
	VkInstance                     m_instance      = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT       m_debugMessenger = VK_NULL_HANDLE;
};

#endif // MITIRU_HAS_VULKAN

} // namespace mitiru::gfx
