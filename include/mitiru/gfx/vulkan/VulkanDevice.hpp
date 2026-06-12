#pragma once

/// @file VulkanDevice.hpp
/// @brief Vulkanバックエンド実装
/// @details Vulkan GPUデバイスのIDevice実装。
///          VkInstance・VkDevice・VkSwapchainKHRの生成と管理を行う。
///          MITIRU_HAS_VULKANが定義されている場合のみコンパイルされる。
///          VulkanDevice の実装本体は末尾 include の detail/VulkanDevice_*.hpp

#ifdef MITIRU_HAS_VULKAN

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include <vulkan/vulkan.h>

#ifdef MITIRU_HAS_GLFW
#include <GLFW/glfw3.h>
#include <mitiru/platform/glfw/GlfwWindow.hpp>
#endif

#include <sgc/types/Color.hpp>

#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/ICommandList.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/gfx/IPipeline.hpp>
#include <mitiru/gfx/IRenderTarget.hpp>
#include <mitiru/gfx/vulkan/VulkanPhysicalDevice.hpp>

namespace mitiru::gfx
{

/// @brief Vulkan用バッファ実装
/// @details VkBufferとVkDeviceMemoryをRAIIで管理する。
class VulkanBuffer final : public IBuffer
{
public:
	/// @brief コンストラクタ（デバイスなし、メタデータのみ保持）
	VulkanBuffer(BufferType bufferType, std::uint32_t sizeBytes) noexcept
		: m_type(bufferType)
		, m_size(sizeBytes)
	{
	}

	/// @brief コンストラクタ（実Vulkanバッファ）
	VulkanBuffer(
		VkDevice device,
		VkPhysicalDevice physDevice,
		BufferType bufferType,
		std::uint32_t sizeBytes,
		bool dynamic,
		const void* initialData)
		: m_device(device)
		, m_type(bufferType)
		, m_size(sizeBytes)
	{
		VkBufferUsageFlags usage = 0;
		switch (bufferType)
		{
		case BufferType::Vertex:
			usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
			break;
		case BufferType::Index:
			usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
			break;
		case BufferType::Constant:
			usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			break;
		}
		if (!dynamic)
		{
			usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		}

		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = sizeBytes;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateBuffer(device, &bufferInfo, nullptr, &m_buffer) != VK_SUCCESS)
		{
			throw std::runtime_error("vkCreateBuffer failed");
		}

		VkMemoryRequirements memReqs;
		vkGetBufferMemoryRequirements(device, m_buffer, &memReqs);

		VkMemoryPropertyFlags memProps =
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

		auto memTypeIndex = findMemoryType(physDevice, memReqs.memoryTypeBits, memProps);
		if (!memTypeIndex.has_value())
		{
			vkDestroyBuffer(device, m_buffer, nullptr);
			m_buffer = VK_NULL_HANDLE;
			throw std::runtime_error("Failed to find suitable memory type");
		}

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReqs.size;
		allocInfo.memoryTypeIndex = memTypeIndex.value();

		if (vkAllocateMemory(device, &allocInfo, nullptr, &m_memory) != VK_SUCCESS)
		{
			vkDestroyBuffer(device, m_buffer, nullptr);
			m_buffer = VK_NULL_HANDLE;
			throw std::runtime_error("vkAllocateMemory failed");
		}

		vkBindBufferMemory(device, m_buffer, m_memory, 0);

		if (initialData)
		{
			void* mapped = nullptr;
			vkMapMemory(device, m_memory, 0, sizeBytes, 0, &mapped);
			std::memcpy(mapped, initialData, sizeBytes);
			vkUnmapMemory(device, m_memory);
		}
	}

	~VulkanBuffer() override
	{
		if (m_device != VK_NULL_HANDLE)
		{
			if (m_buffer != VK_NULL_HANDLE)
			{
				vkDestroyBuffer(m_device, m_buffer, nullptr);
			}
			if (m_memory != VK_NULL_HANDLE)
			{
				vkFreeMemory(m_device, m_memory, nullptr);
			}
		}
	}

	VulkanBuffer(const VulkanBuffer&) = delete;
	VulkanBuffer& operator=(const VulkanBuffer&) = delete;
	VulkanBuffer(VulkanBuffer&&) = delete;
	VulkanBuffer& operator=(VulkanBuffer&&) = delete;

	[[nodiscard]] std::uint32_t size() const noexcept override { return m_size; }
	[[nodiscard]] BufferType type() const noexcept override { return m_type; }
	[[nodiscard]] VkBuffer handle() const noexcept { return m_buffer; }

private:
	[[nodiscard]] static std::optional<uint32_t> findMemoryType(
		VkPhysicalDevice physDevice,
		uint32_t typeFilter,
		VkMemoryPropertyFlags properties) noexcept
	{
		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties(physDevice, &memProperties);

		for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i)
		{
			if ((typeFilter & (1u << i)) &&
				(memProperties.memoryTypes[i].propertyFlags & properties) == properties)
			{
				return i;
			}
		}
		return std::nullopt;
	}

	VkDevice m_device = VK_NULL_HANDLE;
	VkBuffer m_buffer = VK_NULL_HANDLE;
	VkDeviceMemory m_memory = VK_NULL_HANDLE;
	BufferType m_type;
	std::uint32_t m_size;
};

/// @brief Vulkan用コマンドリスト実装
/// @details VkCommandBufferをラップする。
class VulkanCommandList final : public ICommandList
{
public:
	VulkanCommandList() = default;

	explicit VulkanCommandList(VkCommandBuffer cmdBuf) noexcept
		: m_commandBuffer(cmdBuf)
	{
	}

	void begin() override
	{
		if (m_commandBuffer == VK_NULL_HANDLE) return;
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
	}

	void end() override
	{
		if (m_commandBuffer != VK_NULL_HANDLE)
		{
			vkEndCommandBuffer(m_commandBuffer);
		}
	}

	void setRenderTarget(IRenderTarget*) override {}
	void clearRenderTarget(const sgc::Colorf&) override {}
	void setPipeline(IPipeline*) override {}

	void setVertexBuffer(IBuffer* buffer) override
	{
		if (m_commandBuffer == VK_NULL_HANDLE) return;
		auto* vkBuf = dynamic_cast<VulkanBuffer*>(buffer);
		if (!vkBuf) return;
		VkBuffer buffers[] = { vkBuf->handle() };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, buffers, offsets);
	}

	void setIndexBuffer(IBuffer* buffer) override
	{
		if (m_commandBuffer == VK_NULL_HANDLE) return;
		auto* vkBuf = dynamic_cast<VulkanBuffer*>(buffer);
		if (!vkBuf) return;
		vkCmdBindIndexBuffer(m_commandBuffer, vkBuf->handle(), 0, VK_INDEX_TYPE_UINT32);
	}

	void drawIndexed(std::uint32_t indexCount, std::uint32_t startIndex, std::int32_t baseVertex) override
	{
		if (m_commandBuffer != VK_NULL_HANDLE)
		{
			vkCmdDrawIndexed(m_commandBuffer, indexCount, 1, startIndex, baseVertex, 0);
		}
	}

	void draw(std::uint32_t vertexCount, std::uint32_t startVertex) override
	{
		if (m_commandBuffer != VK_NULL_HANDLE)
		{
			vkCmdDraw(m_commandBuffer, vertexCount, 1, startVertex, 0);
		}
	}

	[[nodiscard]] VkCommandBuffer handle() const noexcept { return m_commandBuffer; }

private:
	VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
};

/// @brief Vulkan GPUデバイス実装
/// @details VkInstance・VkDevice・VkSwapchainKHRを管理するフルデバイス実装。
///          GlfwWindowからVkSurfaceKHRを生成し、トリプルバッファリングで描画する。
///          実装本体は detail/VulkanDevice_*.hpp（クラス外 inline 定義）。
///
/// @code
/// auto window = std::make_unique<GlfwWindow>("Vulkan App", 1280, 720);
/// auto device = std::make_unique<VulkanDevice>(window.get());
/// device->beginFrame();
/// // Vulkan描画コマンド...
/// device->endFrame();
/// @endcode
class VulkanDevice final : public IDevice
{
public:
	static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

#ifdef MITIRU_HAS_GLFW
	/// @brief GlfwWindowからVulkanデバイスを生成する
	/// @param window GLFWウィンドウ（nullptrの場合はruntime_error）
	explicit VulkanDevice(mitiru::GlfwWindow* window);
#endif

	~VulkanDevice() override;

	VulkanDevice(const VulkanDevice&) = delete;
	VulkanDevice& operator=(const VulkanDevice&) = delete;
	VulkanDevice(VulkanDevice&&) = delete;
	VulkanDevice& operator=(VulkanDevice&&) = delete;

	[[nodiscard]] std::vector<std::uint8_t> readPixels(
		int width, int height) const override;

	[[nodiscard]] Backend backend() const noexcept override;

	void beginFrame() override;

	void endFrame() override;

	[[nodiscard]] std::unique_ptr<IBuffer> createBuffer(
		BufferType bufferType,
		std::uint32_t sizeBytes,
		bool dynamic,
		const void* initialData) override;

	[[nodiscard]] std::unique_ptr<ICommandList> createCommandList() override;

	[[nodiscard]] uint32_t currentFrameIndex() const override;

	[[nodiscard]] uint32_t frameInFlightCount() const override;

	void waitForGpu() override;

private:
	// ── Instance（インスタンス） ──────────────────────────────────────────────

	void createInstance();

	// ── Surface（サーフェス） ───────────────────────────────────────────────

	void createSurface();

	// ── Physical Device（物理デバイス） ───────────────────────────────────────

	void pickPhysicalDevice();

	// ── Logical Device（論理デバイス） ────────────────────────────────────────

	void createLogicalDevice();

	// ── Swap Chain（スワップチェーン） ────────────────────────────────────────────

	struct SwapChainSupportDetails
	{
		VkSurfaceCapabilitiesKHR capabilities{};
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> presentModes;
	};

	[[nodiscard]] SwapChainSupportDetails querySwapChainSupport() const;

	[[nodiscard]] static VkSurfaceFormatKHR chooseSwapSurfaceFormat(
		const std::vector<VkSurfaceFormatKHR>& availableFormats) noexcept;

	[[nodiscard]] static VkPresentModeKHR chooseSwapPresentMode(
		const std::vector<VkPresentModeKHR>& availablePresentModes) noexcept;

	[[nodiscard]] VkExtent2D chooseSwapExtent(
		const VkSurfaceCapabilitiesKHR& capabilities) const noexcept;

	void createSwapChain();

	void createImageViews();

	void createRenderPass();

	void createFramebuffers();

	void destroySwapChain();

	void recreateSwapChain();

	// ── Command Pool / Buffers（コマンドプール・バッファ） ────────────────────────────────

	void createCommandPool();

	void createCommandBuffers();

	void destroyCommandPool();

	// ── Sync Objects（同期オブジェクト） ──────────────────────────────────────────

	void createSyncObjects();

	void destroySyncObjects();

	// ── Memory（メモリ） ────────────────────────────────────────────────

	[[nodiscard]] std::optional<uint32_t> findMemoryType(
		uint32_t typeFilter,
		VkMemoryPropertyFlags properties) const noexcept;

	// ── Member variables（メンバー変数） ──────────────────────────────────────

	mitiru::GlfwWindow* m_glfwWindow = nullptr;

	/// Vulkan コアオブジェクト
	VkInstance m_instance = VK_NULL_HANDLE;
	VkSurfaceKHR m_surface = VK_NULL_HANDLE;
	VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
	VkQueue m_graphicsQueue = VK_NULL_HANDLE;
	VkQueue m_presentQueue = VK_NULL_HANDLE;
	QueueFamilyIndices m_queueFamilies;

	/// スワップチェーン
	VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
	std::vector<VkImage> m_swapChainImages;
	std::vector<VkImageView> m_swapChainImageViews;
	std::vector<VkFramebuffer> m_swapChainFramebuffers;
	VkFormat m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
	VkExtent2D m_swapChainExtent{};
	VkRenderPass m_renderPass = VK_NULL_HANDLE;

	/// コマンドプールとバッファ
	VkCommandPool m_commandPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> m_commandBuffers;

	/// 同期オブジェクト
	std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> m_imageAvailableSemaphores{};
	std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> m_renderFinishedSemaphores{};
	std::array<VkFence, MAX_FRAMES_IN_FLIGHT> m_inFlightFences{};

	/// フレーム状態
	uint32_t m_currentFrame = 0;
	uint32_t m_currentImageIndex = 0;
};

} // namespace mitiru::gfx

// 実装本体（クラス外 inline 定義）— 末尾で detail を取り込む
#include <mitiru/gfx/vulkan/detail/VulkanDevice_Frame.hpp>
#include <mitiru/gfx/vulkan/detail/VulkanDevice_Init.hpp>
#include <mitiru/gfx/vulkan/detail/VulkanDevice_SwapChain.hpp>

#endif // MITIRU_HAS_VULKAN
