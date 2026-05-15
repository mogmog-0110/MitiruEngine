#pragma once

/// @file VulkanFramebuffer.hpp
/// @brief Vulkanフレームバッファ RAII管理
/// @details VkFramebufferをRAIIで管理する。
///          MITIRU_HAS_VULKANが定義されている場合のみコンパイルされる。

#ifdef MITIRU_HAS_VULKAN

#include <stdexcept>
#include <vector>

#include <vulkan/vulkan.h>

namespace mitiru::gfx
{

/// @brief フレームバッファ設定
/// @details フレームバッファ生成パラメータを保持する。GPU不要でテスト可能。
struct FramebufferConfig
{
	uint32_t width = 0;                             ///< バッファ幅（ピクセル）
	uint32_t height = 0;                            ///< バッファ高さ（ピクセル）
	uint32_t attachmentCount = 0;                   ///< アタッチメント数

	/// @brief 設定が有効か確認する
	/// @return 幅・高さ・アタッチメント数がすべて0より大きければtrue
	[[nodiscard]] bool isValid() const noexcept
	{
		return width > 0 && height > 0 && attachmentCount > 0;
	}

	/// @brief カラーのみのフレームバッファ設定を作成する
	/// @param w 幅
	/// @param h 高さ
	/// @return FramebufferConfig
	[[nodiscard]] static FramebufferConfig colorOnly(uint32_t w, uint32_t h)
	{
		FramebufferConfig cfg;
		cfg.width = w;
		cfg.height = h;
		cfg.attachmentCount = 1;
		return cfg;
	}

	/// @brief カラー+深度のフレームバッファ設定を作成する
	/// @param w 幅
	/// @param h 高さ
	/// @return FramebufferConfig
	[[nodiscard]] static FramebufferConfig colorDepth(uint32_t w, uint32_t h)
	{
		FramebufferConfig cfg;
		cfg.width = w;
		cfg.height = h;
		cfg.attachmentCount = 2;
		return cfg;
	}
};

/// @brief Vulkanフレームバッファ RAIIラッパー
/// @details VkFramebufferを単一オブジェクトとして管理する。
///
/// @code
/// VulkanFramebuffer framebuffer;
/// framebuffer.initialize(device, renderPass, { colorView, depthView }, 1280, 720);
/// // 使用: framebuffer.handle()
/// @endcode
class VulkanFramebuffer
{
public:
	VulkanFramebuffer() = default;

	~VulkanFramebuffer()
	{
		destroy();
	}

	VulkanFramebuffer(const VulkanFramebuffer&) = delete;
	VulkanFramebuffer& operator=(const VulkanFramebuffer&) = delete;

	VulkanFramebuffer(VulkanFramebuffer&& other) noexcept
		: m_device(other.m_device)
		, m_framebuffer(other.m_framebuffer)
	{
		other.m_device = VK_NULL_HANDLE;
		other.m_framebuffer = VK_NULL_HANDLE;
	}

	VulkanFramebuffer& operator=(VulkanFramebuffer&& other) noexcept
	{
		if (this != &other)
		{
			destroy();
			m_device = other.m_device;
			m_framebuffer = other.m_framebuffer;
			other.m_device = VK_NULL_HANDLE;
			other.m_framebuffer = VK_NULL_HANDLE;
		}
		return *this;
	}

	/// @brief フレームバッファを初期化する
	/// @param device 論理デバイス
	/// @param renderPass レンダーパス
	/// @param attachments アタッチメントイメージビューのリスト
	/// @param width バッファ幅（ピクセル）
	/// @param height バッファ高さ（ピクセル）
	/// @throws std::runtime_error フレームバッファ生成に失敗した場合
	void initialize(
		VkDevice device,
		VkRenderPass renderPass,
		const std::vector<VkImageView>& attachments,
		uint32_t width,
		uint32_t height)
	{
		destroy();
		m_device = device;

		VkFramebufferCreateInfo framebufferInfo{};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = renderPass;
		framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		framebufferInfo.pAttachments = attachments.data();
		framebufferInfo.width = width;
		framebufferInfo.height = height;
		framebufferInfo.layers = 1;

		if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &m_framebuffer) != VK_SUCCESS)
		{
			throw std::runtime_error("VulkanFramebuffer: vkCreateFramebuffer failed");
		}
	}

	/// @brief フレームバッファリソースを解放する
	void destroy() noexcept
	{
		if (m_framebuffer != VK_NULL_HANDLE)
		{
			vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
			m_framebuffer = VK_NULL_HANDLE;
		}
		m_device = VK_NULL_HANDLE;
	}

	/// @brief フレームバッファハンドルを取得する
	/// @return VkFramebuffer
	[[nodiscard]] VkFramebuffer handle() const noexcept { return m_framebuffer; }

	/// @brief 初期化済みか確認する
	/// @return フレームバッファが有効ならtrue
	[[nodiscard]] bool isInitialized() const noexcept { return m_framebuffer != VK_NULL_HANDLE; }

private:
	VkDevice m_device = VK_NULL_HANDLE;                ///< 論理デバイス（非所有）
	VkFramebuffer m_framebuffer = VK_NULL_HANDLE;      ///< フレームバッファ
};

} // namespace mitiru::gfx

#endif // MITIRU_HAS_VULKAN
