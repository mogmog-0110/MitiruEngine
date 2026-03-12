#pragma once

/// @file GlfwVulkanSurface.hpp
/// @brief GLFW+Vulkanサーフェス生成
/// @details GLFWウィンドウからVkSurfaceKHRを生成するユーティリティ。
///          MITIRU_HAS_GLFWとMITIRU_HAS_VULKANの両方が定義されている場合に使用可能。

#include <cstdint>
#include <string>
#include <vector>

namespace mitiru
{

/// @brief GLFW+Vulkanサーフェス生成パラメータ
/// @details GLFWウィンドウからVkSurfaceKHRを生成する際のパラメータ。
///          実際のGLFW/Vulkan APIに依存せず、テストで使用可能。
struct GlfwVulkanSurfaceDesc
{
	bool requirePresentation = true;  ///< プレゼンテーション機能を要求するか

	/// @brief GLFWが必要とするVulkanインスタンス拡張名リストを取得する
	/// @return 拡張名のリスト
	/// @note 実行時にはglfwGetRequiredInstanceExtensionsを使用すること。
	///       この関数はコンパイル時のデフォルト値を返す。
	[[nodiscard]] static std::vector<std::string> defaultRequiredExtensions()
	{
		std::vector<std::string> extensions;
		extensions.emplace_back("VK_KHR_surface");

#ifdef _WIN32
		extensions.emplace_back("VK_KHR_win32_surface");
#elif defined(__linux__)
		extensions.emplace_back("VK_KHR_xcb_surface");
#elif defined(__APPLE__)
		extensions.emplace_back("VK_EXT_metal_surface");
#endif

		return extensions;
	}
};

/// @brief サーフェス生成結果
/// @details サーフェス生成の成否とエラーメッセージを保持する。
struct SurfaceCreateResult
{
	bool success = false;             ///< 生成に成功したか
	std::string errorMessage;         ///< エラーメッセージ（失敗時のみ）

	/// @brief 成功結果を生成する
	/// @return 成功を示すSurfaceCreateResult
	[[nodiscard]] static SurfaceCreateResult ok() noexcept
	{
		SurfaceCreateResult result;
		result.success = true;
		return result;
	}

	/// @brief 失敗結果を生成する
	/// @param message エラーメッセージ
	/// @return 失敗を示すSurfaceCreateResult
	[[nodiscard]] static SurfaceCreateResult fail(const std::string& message)
	{
		SurfaceCreateResult result;
		result.success = false;
		result.errorMessage = message;
		return result;
	}
};

#if defined(MITIRU_HAS_GLFW) && defined(MITIRU_HAS_VULKAN)

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

/// @brief GLFWウィンドウからVulkanサーフェスを生成するユーティリティ
/// @details glfwCreateWindowSurfaceのラッパー。
///
/// @code
/// GlfwVulkanSurface surfaceUtil;
/// auto extensions = surfaceUtil.getRequiredExtensions();
/// // VulkanInstance生成時にextensionsを指定 ...
/// VkSurfaceKHR surface;
/// auto result = surfaceUtil.createSurface(instance, window, &surface);
/// if (!result.success)
/// {
///     // エラー処理
/// }
/// @endcode
class GlfwVulkanSurface
{
public:
	/// @brief GLFWが必要とするVulkanインスタンス拡張を取得する
	/// @return 拡張名のリスト
	[[nodiscard]] static std::vector<std::string> getRequiredExtensions()
	{
		std::uint32_t count = 0;
		const char** glfwExtensions =
			glfwGetRequiredInstanceExtensions(&count);

		std::vector<std::string> result;
		result.reserve(count);
		for (std::uint32_t i = 0; i < count; ++i)
		{
			result.emplace_back(glfwExtensions[i]);
		}
		return result;
	}

	/// @brief GLFWウィンドウからVulkanサーフェスを生成する
	/// @param instance VkInstanceハンドル
	/// @param window GLFWウィンドウハンドル
	/// @param outSurface 生成されたサーフェスの出力先
	/// @return サーフェス生成結果
	[[nodiscard]] static SurfaceCreateResult createSurface(
		VkInstance instance,
		GLFWwindow* window,
		VkSurfaceKHR* outSurface)
	{
		if (!instance || !window || !outSurface)
		{
			return SurfaceCreateResult::fail(
				"Invalid parameters: instance, window, and outSurface must not be null");
		}

		const VkResult result =
			glfwCreateWindowSurface(instance, window, nullptr, outSurface);

		if (result != VK_SUCCESS)
		{
			return SurfaceCreateResult::fail(
				"glfwCreateWindowSurface failed with VkResult: "
				+ std::to_string(static_cast<int>(result)));
		}

		return SurfaceCreateResult::ok();
	}
};

#endif // defined(MITIRU_HAS_GLFW) && defined(MITIRU_HAS_VULKAN)

} // namespace mitiru
