#pragma once

/// @file VulkanTexture.hpp
/// @brief Vulkanテクスチャ RAII管理
/// @details VkImage・VkImageView・VkDeviceMemory・VkSamplerをRAIIで管理する汎用テクスチャ実装。
///          カラーフォーマットと深度フォーマットの両方をサポートする。
///          MITIRU_HAS_VULKANが定義されている場合のみコンパイルされる。

#ifdef MITIRU_HAS_VULKAN

#include <optional>
#include <stdexcept>

#include <vulkan/vulkan.h>

namespace mitiru::gfx
{

/// @brief テクスチャ設定
/// @details テクスチャ生成パラメータを保持する。GPU不要でテスト可能。
struct TextureConfig
{
	uint32_t width = 0;                                        ///< テクスチャ幅
	uint32_t height = 0;                                       ///< テクスチャ高さ
	VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;               ///< ピクセルフォーマット
	VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;     ///< 使用フラグ
	VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; ///< アスペクトマスク

	/// @brief 設定が有効か確認する
	/// @return 幅と高さが0より大きければtrue
	[[nodiscard]] bool isValid() const noexcept
	{
		return width > 0 && height > 0;
	}

	/// @brief カラーテクスチャ用設定を作成する
	/// @param w 幅
	/// @param h 高さ
	/// @param fmt フォーマット（デフォルト: RGBA8）
	/// @return TextureConfig
	[[nodiscard]] static TextureConfig color(uint32_t w, uint32_t h,
		VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM)
	{
		TextureConfig cfg;
		cfg.width = w;
		cfg.height = h;
		cfg.format = fmt;
		cfg.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		cfg.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		return cfg;
	}

	/// @brief 深度テクスチャ用設定を作成する
	/// @param w 幅
	/// @param h 高さ
	/// @param fmt フォーマット（デフォルト: D32_SFLOAT）
	/// @return TextureConfig
	[[nodiscard]] static TextureConfig depth(uint32_t w, uint32_t h,
		VkFormat fmt = VK_FORMAT_D32_SFLOAT)
	{
		TextureConfig cfg;
		cfg.width = w;
		cfg.height = h;
		cfg.format = fmt;
		cfg.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		cfg.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		return cfg;
	}
};

/// @brief Vulkanテクスチャ RAIIラッパー
/// @details VkImage・VkImageView・VkDeviceMemory・VkSamplerを一括管理する。
///          カラーテクスチャと深度テクスチャ両方に対応する。
///
/// @code
/// VulkanTexture texture;
/// texture.initialize(device, physDevice, 512, 512,
///     VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
/// // 使用: texture.imageView(), texture.sampler()
/// @endcode
class VulkanTexture
{
public:
	VulkanTexture() = default;

	~VulkanTexture()
	{
		destroy();
	}

	VulkanTexture(const VulkanTexture&) = delete;
	VulkanTexture& operator=(const VulkanTexture&) = delete;

	VulkanTexture(VulkanTexture&& other) noexcept
		: m_device(other.m_device)
		, m_image(other.m_image)
		, m_memory(other.m_memory)
		, m_imageView(other.m_imageView)
		, m_sampler(other.m_sampler)
		, m_format(other.m_format)
		, m_aspectMask(other.m_aspectMask)
	{
		other.m_device = VK_NULL_HANDLE;
		other.m_image = VK_NULL_HANDLE;
		other.m_memory = VK_NULL_HANDLE;
		other.m_imageView = VK_NULL_HANDLE;
		other.m_sampler = VK_NULL_HANDLE;
		other.m_format = VK_FORMAT_UNDEFINED;
		other.m_aspectMask = 0;
	}

	VulkanTexture& operator=(VulkanTexture&& other) noexcept
	{
		if (this != &other)
		{
			destroy();
			m_device = other.m_device;
			m_image = other.m_image;
			m_memory = other.m_memory;
			m_imageView = other.m_imageView;
			m_sampler = other.m_sampler;
			m_format = other.m_format;
			m_aspectMask = other.m_aspectMask;
			other.m_device = VK_NULL_HANDLE;
			other.m_image = VK_NULL_HANDLE;
			other.m_memory = VK_NULL_HANDLE;
			other.m_imageView = VK_NULL_HANDLE;
			other.m_sampler = VK_NULL_HANDLE;
			other.m_format = VK_FORMAT_UNDEFINED;
			other.m_aspectMask = 0;
		}
		return *this;
	}

	/// @brief テクスチャを初期化する
	/// @param device 論理デバイス
	/// @param physDevice 物理デバイス
	/// @param width テクスチャ幅
	/// @param height テクスチャ高さ
	/// @param format ピクセルフォーマット
	/// @param usage 使用フラグ
	/// @param aspectMask イメージアスペクト（カラー/深度）
	/// @throws std::runtime_error リソース生成に失敗した場合
	void initialize(
		VkDevice device,
		VkPhysicalDevice physDevice,
		uint32_t width,
		uint32_t height,
		VkFormat format,
		VkImageUsageFlags usage,
		VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT)
	{
		destroy();
		m_device = device;
		m_format = format;
		m_aspectMask = aspectMask;

		// イメージ生成
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent = { width, height, 1 };
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = format;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = usage;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateImage(device, &imageInfo, nullptr, &m_image) != VK_SUCCESS)
		{
			throw std::runtime_error("VulkanTexture: vkCreateImage failed");
		}

		// メモリ確保
		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(device, m_image, &memReqs);

		const auto memTypeIdx = findMemoryType(physDevice, memReqs.memoryTypeBits,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (!memTypeIdx.has_value())
		{
			vkDestroyImage(device, m_image, nullptr);
			m_image = VK_NULL_HANDLE;
			throw std::runtime_error("VulkanTexture: suitable memory type not found");
		}

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReqs.size;
		allocInfo.memoryTypeIndex = memTypeIdx.value();

		if (vkAllocateMemory(device, &allocInfo, nullptr, &m_memory) != VK_SUCCESS)
		{
			vkDestroyImage(device, m_image, nullptr);
			m_image = VK_NULL_HANDLE;
			throw std::runtime_error("VulkanTexture: vkAllocateMemory failed");
		}

		vkBindImageMemory(device, m_image, m_memory, 0);

		// イメージビュー生成
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = m_image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.subresourceRange.aspectMask = aspectMask;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		if (vkCreateImageView(device, &viewInfo, nullptr, &m_imageView) != VK_SUCCESS)
		{
			vkFreeMemory(device, m_memory, nullptr);
			m_memory = VK_NULL_HANDLE;
			vkDestroyImage(device, m_image, nullptr);
			m_image = VK_NULL_HANDLE;
			throw std::runtime_error("VulkanTexture: vkCreateImageView failed");
		}

		// カラーテクスチャの場合のみサンプラー生成
		if (aspectMask & VK_IMAGE_ASPECT_COLOR_BIT)
		{
			VkSamplerCreateInfo samplerInfo{};
			samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			samplerInfo.magFilter = VK_FILTER_LINEAR;
			samplerInfo.minFilter = VK_FILTER_LINEAR;
			samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			samplerInfo.anisotropyEnable = VK_FALSE;
			samplerInfo.maxAnisotropy = 1.0f;
			samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
			samplerInfo.unnormalizedCoordinates = VK_FALSE;
			samplerInfo.compareEnable = VK_FALSE;
			samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
			samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

			if (vkCreateSampler(device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS)
			{
				vkDestroyImageView(device, m_imageView, nullptr);
				m_imageView = VK_NULL_HANDLE;
				vkFreeMemory(device, m_memory, nullptr);
				m_memory = VK_NULL_HANDLE;
				vkDestroyImage(device, m_image, nullptr);
				m_image = VK_NULL_HANDLE;
				throw std::runtime_error("VulkanTexture: vkCreateSampler failed");
			}
		}
	}

	/// @brief テクスチャリソースを解放する
	void destroy() noexcept
	{
		if (m_device == VK_NULL_HANDLE) return;

		if (m_sampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(m_device, m_sampler, nullptr);
			m_sampler = VK_NULL_HANDLE;
		}
		if (m_imageView != VK_NULL_HANDLE)
		{
			vkDestroyImageView(m_device, m_imageView, nullptr);
			m_imageView = VK_NULL_HANDLE;
		}
		if (m_memory != VK_NULL_HANDLE)
		{
			vkFreeMemory(m_device, m_memory, nullptr);
			m_memory = VK_NULL_HANDLE;
		}
		if (m_image != VK_NULL_HANDLE)
		{
			vkDestroyImage(m_device, m_image, nullptr);
			m_image = VK_NULL_HANDLE;
		}
		m_device = VK_NULL_HANDLE;
		m_format = VK_FORMAT_UNDEFINED;
		m_aspectMask = 0;
	}

	/// @brief イメージを取得する
	/// @return VkImage
	[[nodiscard]] VkImage image() const noexcept { return m_image; }

	/// @brief イメージビューを取得する
	/// @return VkImageView
	[[nodiscard]] VkImageView imageView() const noexcept { return m_imageView; }

	/// @brief サンプラーを取得する
	/// @return VkSampler（深度テクスチャの場合VK_NULL_HANDLE）
	[[nodiscard]] VkSampler sampler() const noexcept { return m_sampler; }

	/// @brief フォーマットを取得する
	/// @return VkFormat
	[[nodiscard]] VkFormat format() const noexcept { return m_format; }

	/// @brief 初期化済みか確認する
	/// @return イメージビューが有効ならtrue
	[[nodiscard]] bool isInitialized() const noexcept { return m_imageView != VK_NULL_HANDLE; }

private:
	[[nodiscard]] static std::optional<uint32_t> findMemoryType(
		VkPhysicalDevice physDevice,
		uint32_t typeFilter,
		VkMemoryPropertyFlags properties) noexcept
	{
		VkPhysicalDeviceMemoryProperties memProps;
		vkGetPhysicalDeviceMemoryProperties(physDevice, &memProps);

		for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
		{
			if ((typeFilter & (1u << i)) &&
				(memProps.memoryTypes[i].propertyFlags & properties) == properties)
			{
				return i;
			}
		}
		return std::nullopt;
	}

	VkDevice m_device = VK_NULL_HANDLE;        ///< 論理デバイス（非所有）
	VkImage m_image = VK_NULL_HANDLE;          ///< Vulkanイメージ
	VkDeviceMemory m_memory = VK_NULL_HANDLE;  ///< デバイスメモリ
	VkImageView m_imageView = VK_NULL_HANDLE;  ///< イメージビュー
	VkSampler m_sampler = VK_NULL_HANDLE;      ///< サンプラー（カラーテクスチャのみ）
	VkFormat m_format = VK_FORMAT_UNDEFINED;   ///< フォーマット
	VkImageAspectFlags m_aspectMask = 0;       ///< イメージアスペクト
};

} // namespace mitiru::gfx

#endif // MITIRU_HAS_VULKAN
