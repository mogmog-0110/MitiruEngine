#pragma once

/// @file VulkanDepthBuffer.hpp
/// @brief Vulkan深度バッファ RAII管理
/// @details VkImage・VkDeviceMemory・VkImageViewをRAIIで管理する深度バッファ実装。
///          フォーマットはVK_FORMAT_D32_SFLOATを優先し、利用不可の場合はフォールバックする。
///          MITIRU_HAS_VULKANが定義されている場合のみコンパイルされる。

#include <cstdint>

namespace mitiru::gfx
{

/// @brief Vulkan深度バッファ設定
/// @details 深度バッファ生成パラメータを保持する。GPU不要でテスト可能。
struct DepthBufferConfig
{
	uint32_t width = 0;   ///< バッファ幅（ピクセル）
	uint32_t height = 0;  ///< バッファ高さ（ピクセル）

	/// @brief 幅と高さが有効か確認する
	/// @return 両方0より大きければtrue
	[[nodiscard]] bool isValid() const noexcept
	{
		return width > 0 && height > 0;
	}
};

} // namespace mitiru::gfx

#ifdef MITIRU_HAS_VULKAN

#include <optional>
#include <stdexcept>
#include <vector>

#include <vulkan/vulkan.h>

namespace mitiru::gfx
{

/// @brief 深度フォーマット選択の優先リスト
/// @details D32_SFLOAT → D32_SFLOAT_S8_UINT → D24_UNORM_S8_UINT の順で選択する。
inline const std::vector<VkFormat> kDepthFormatCandidates = {
	VK_FORMAT_D32_SFLOAT,
	VK_FORMAT_D32_SFLOAT_S8_UINT,
	VK_FORMAT_D24_UNORM_S8_UINT,
};

/// @brief Vulkan深度バッファ RAIIラッパー
/// @details VkImage・VkDeviceMemory・VkImageViewをRAIIで管理する。
///          深度フォーマットはVK_FORMAT_D32_SFLOATを優先し、
///          デバイスが対応していない場合はフォールバックを試みる。
///
/// @code
/// VulkanDepthBuffer depthBuf;
/// depthBuf.initialize(device, physDevice, 1280, 720);
/// // 使用: depthBuf.imageView()
/// @endcode
class VulkanDepthBuffer
{
public:
	VulkanDepthBuffer() = default;

	~VulkanDepthBuffer()
	{
		destroy();
	}

	VulkanDepthBuffer(const VulkanDepthBuffer&) = delete;
	VulkanDepthBuffer& operator=(const VulkanDepthBuffer&) = delete;

	VulkanDepthBuffer(VulkanDepthBuffer&& other) noexcept
		: m_device(other.m_device)
		, m_image(other.m_image)
		, m_memory(other.m_memory)
		, m_imageView(other.m_imageView)
		, m_format(other.m_format)
	{
		other.m_device = VK_NULL_HANDLE;
		other.m_image = VK_NULL_HANDLE;
		other.m_memory = VK_NULL_HANDLE;
		other.m_imageView = VK_NULL_HANDLE;
		other.m_format = VK_FORMAT_UNDEFINED;
	}

	VulkanDepthBuffer& operator=(VulkanDepthBuffer&& other) noexcept
	{
		if (this != &other)
		{
			destroy();
			m_device = other.m_device;
			m_image = other.m_image;
			m_memory = other.m_memory;
			m_imageView = other.m_imageView;
			m_format = other.m_format;
			other.m_device = VK_NULL_HANDLE;
			other.m_image = VK_NULL_HANDLE;
			other.m_memory = VK_NULL_HANDLE;
			other.m_imageView = VK_NULL_HANDLE;
			other.m_format = VK_FORMAT_UNDEFINED;
		}
		return *this;
	}

	/// @brief 深度バッファを初期化する
	/// @param device 論理デバイス
	/// @param physDevice 物理デバイス
	/// @param width バッファ幅（ピクセル）
	/// @param height バッファ高さ（ピクセル）
	/// @throws std::runtime_error 深度フォーマットが見つからない、またはリソース生成に失敗した場合
	void initialize(VkDevice device, VkPhysicalDevice physDevice, uint32_t width, uint32_t height)
	{
		destroy();
		m_device = device;
		m_format = findSupportedFormat(physDevice);

		// イメージ生成
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent.width = width;
		imageInfo.extent.height = height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = m_format;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateImage(device, &imageInfo, nullptr, &m_image) != VK_SUCCESS)
		{
			throw std::runtime_error("VulkanDepthBuffer: vkCreateImage failed");
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
			throw std::runtime_error("VulkanDepthBuffer: suitable memory type not found");
		}

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReqs.size;
		allocInfo.memoryTypeIndex = memTypeIdx.value();

		if (vkAllocateMemory(device, &allocInfo, nullptr, &m_memory) != VK_SUCCESS)
		{
			vkDestroyImage(device, m_image, nullptr);
			m_image = VK_NULL_HANDLE;
			throw std::runtime_error("VulkanDepthBuffer: vkAllocateMemory failed");
		}

		vkBindImageMemory(device, m_image, m_memory, 0);

		// イメージビュー生成
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = m_image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = m_format;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
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
			throw std::runtime_error("VulkanDepthBuffer: vkCreateImageView failed");
		}
	}

	/// @brief 深度バッファリソースを解放する
	void destroy() noexcept
	{
		if (m_device == VK_NULL_HANDLE) return;

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
	}

	/// @brief イメージビューを取得する
	/// @return VkImageView（未初期化の場合VK_NULL_HANDLE）
	[[nodiscard]] VkImageView imageView() const noexcept { return m_imageView; }

	/// @brief 深度フォーマットを取得する
	/// @return 選択されたVkFormat
	[[nodiscard]] VkFormat format() const noexcept { return m_format; }

	/// @brief 初期化済みか確認する
	/// @return イメージビューが有効ならtrue
	[[nodiscard]] bool isInitialized() const noexcept { return m_imageView != VK_NULL_HANDLE; }

	/// @brief デバイスが対応する最適な深度フォーマットを選択する
	/// @param physDevice 物理デバイス
	/// @return 選択されたVkFormat
	/// @throws std::runtime_error 対応フォーマットが見つからない場合
	[[nodiscard]] static VkFormat findSupportedFormat(VkPhysicalDevice physDevice)
	{
		for (const VkFormat candidate : kDepthFormatCandidates)
		{
			VkFormatProperties props;
			vkGetPhysicalDeviceFormatProperties(physDevice, candidate, &props);

			if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
			{
				return candidate;
			}
		}
		throw std::runtime_error("VulkanDepthBuffer: no supported depth format found");
	}

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
	VkImage m_image = VK_NULL_HANDLE;          ///< 深度イメージ
	VkDeviceMemory m_memory = VK_NULL_HANDLE;  ///< デバイスメモリ
	VkImageView m_imageView = VK_NULL_HANDLE;  ///< イメージビュー
	VkFormat m_format = VK_FORMAT_UNDEFINED;   ///< 深度フォーマット
};

} // namespace mitiru::gfx

#endif // MITIRU_HAS_VULKAN
