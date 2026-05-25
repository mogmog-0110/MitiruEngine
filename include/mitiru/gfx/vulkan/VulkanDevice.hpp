#pragma once

/// @file VulkanDevice.hpp
/// @brief Vulkanバックエンド実装
/// @details Vulkan GPUデバイスのIDevice実装。
///          VkInstance・VkDevice・VkSwapchainKHRの生成と管理を行う。
///          MITIRU_HAS_VULKANが定義されている場合のみコンパイルされる。

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
	explicit VulkanDevice(mitiru::GlfwWindow* window)
		: m_glfwWindow(window)
	{
		if (!window)
		{
			throw std::runtime_error("VulkanDevice requires a non-null GlfwWindow");
		}

		createInstance();
		createSurface();
		pickPhysicalDevice();
		createLogicalDevice();
		createSwapChain();
		createCommandPool();
		createCommandBuffers();
		createSyncObjects();
	}
#endif

	~VulkanDevice() override
	{
		if (m_device != VK_NULL_HANDLE)
		{
			vkDeviceWaitIdle(m_device);
		}

		destroySyncObjects();
		destroyCommandPool();
		destroySwapChain();

		if (m_device != VK_NULL_HANDLE)
		{
			vkDestroyDevice(m_device, nullptr);
		}
		if (m_surface != VK_NULL_HANDLE)
		{
			vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
		}
		if (m_instance != VK_NULL_HANDLE)
		{
			vkDestroyInstance(m_instance, nullptr);
		}
	}

	VulkanDevice(const VulkanDevice&) = delete;
	VulkanDevice& operator=(const VulkanDevice&) = delete;
	VulkanDevice(VulkanDevice&&) = delete;
	VulkanDevice& operator=(VulkanDevice&&) = delete;

	[[nodiscard]] std::vector<std::uint8_t> readPixels(
		int width, int height) const override
	{
		const auto pixelCount =
			static_cast<std::size_t>(width) *
			static_cast<std::size_t>(height);
		std::vector<std::uint8_t> data(pixelCount * 4, 0);

		if (m_device == VK_NULL_HANDLE || m_swapChainImages.empty())
		{
			for (std::size_t i = 0; i < pixelCount; ++i)
			{
				data[i * 4 + 3] = 255;
			}
			return data;
		}

		/// ステージングバッファを作成してフレームバッファからコピーする
		VkBufferCreateInfo bufInfo{};
		bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufInfo.size = pixelCount * 4;
		bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkBuffer stagingBuffer = VK_NULL_HANDLE;
		VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

		/// RAII スコープガード — 関数脱出時にステージングリソースを確実に解放する
		struct StagingGuard
		{
			VkDevice dev;
			VkBuffer& buf;
			VkDeviceMemory& mem;
			~StagingGuard()
			{
				if (buf != VK_NULL_HANDLE) { vkDestroyBuffer(dev, buf, nullptr); buf = VK_NULL_HANDLE; }
				if (mem != VK_NULL_HANDLE) { vkFreeMemory(dev, mem, nullptr); mem = VK_NULL_HANDLE; }
			}
		} stagingGuard{m_device, stagingBuffer, stagingMemory};

		if (vkCreateBuffer(m_device, &bufInfo, nullptr, &stagingBuffer) != VK_SUCCESS)
		{
			for (std::size_t i = 0; i < pixelCount; ++i)
			{
				data[i * 4 + 3] = 255;
			}
			return data;
		}

		VkMemoryRequirements memReqs;
		vkGetBufferMemoryRequirements(m_device, stagingBuffer, &memReqs);

		auto memType = findMemoryType(
			memReqs.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		if (!memType.has_value())
		{
			vkDestroyBuffer(m_device, stagingBuffer, nullptr);
			for (std::size_t i = 0; i < pixelCount; ++i)
			{
				data[i * 4 + 3] = 255;
			}
			return data;
		}

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReqs.size;
		allocInfo.memoryTypeIndex = memType.value();

		if (vkAllocateMemory(m_device, &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS)
		{
			vkDestroyBuffer(m_device, stagingBuffer, nullptr);
			for (std::size_t i = 0; i < pixelCount; ++i)
			{
				data[i * 4 + 3] = 255;
			}
			return data;
		}

		vkBindBufferMemory(m_device, stagingBuffer, stagingMemory, 0);

		/// 一時コマンドバッファでイメージをコピーする
		VkCommandBufferAllocateInfo cmdAllocInfo{};
		cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmdAllocInfo.commandPool = m_commandPool;
		cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmdAllocInfo.commandBufferCount = 1;

		VkCommandBuffer cmdBuf;
		vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &cmdBuf);

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(cmdBuf, &beginInfo);

		/// イメージレイアウトを転送元に遷移する
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = m_swapChainImages[m_currentImageIndex];
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		vkCmdPipelineBarrier(
			cmdBuf,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		VkBufferImageCopy region{};
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.layerCount = 1;
		region.imageExtent = {
			static_cast<uint32_t>(width),
			static_cast<uint32_t>(height),
			1
		};

		vkCmdCopyImageToBuffer(
			cmdBuf,
			m_swapChainImages[m_currentImageIndex],
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			stagingBuffer,
			1, &region);

		/// レイアウトをプレゼントに戻す
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

		vkCmdPipelineBarrier(
			cmdBuf,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		vkEndCommandBuffer(cmdBuf);

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmdBuf;

		vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(m_graphicsQueue);

		vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmdBuf);

		/// マップしてデータを読み取る
		void* mapped = nullptr;
		vkMapMemory(m_device, stagingMemory, 0, pixelCount * 4, 0, &mapped);
		std::memcpy(data.data(), mapped, pixelCount * 4);
		vkUnmapMemory(m_device, stagingMemory);

		/// stagingGuard が自動的にバッファとメモリを解放する
		return data;
	}

	[[nodiscard]] Backend backend() const noexcept override
	{
		return Backend::Vulkan;
	}

	void beginFrame() override
	{
		vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE,
			std::numeric_limits<uint64_t>::max());

		VkResult result = vkAcquireNextImageKHR(
			m_device, m_swapChain,
			std::numeric_limits<uint64_t>::max(),
			m_imageAvailableSemaphores[m_currentFrame],
			VK_NULL_HANDLE,
			&m_currentImageIndex);

		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			recreateSwapChain();
			return;
		}

		vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);
		vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);

		/// コマンドバッファ記録を開始する
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		vkBeginCommandBuffer(m_commandBuffers[m_currentFrame], &beginInfo);

		/// レンダーパスを開始する
		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = m_renderPass;
		renderPassInfo.framebuffer = m_swapChainFramebuffers[m_currentImageIndex];
		renderPassInfo.renderArea.offset = {0, 0};
		renderPassInfo.renderArea.extent = m_swapChainExtent;

		VkClearValue clearColor = {{{m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3]}}};
		renderPassInfo.clearValueCount = 1;
		renderPassInfo.pClearValues = &clearColor;

		vkCmdBeginRenderPass(
			m_commandBuffers[m_currentFrame],
			&renderPassInfo,
			VK_SUBPASS_CONTENTS_INLINE);

		/// ビューポートとシザーを設定する
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(m_swapChainExtent.width);
		viewport.height = static_cast<float>(m_swapChainExtent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(m_commandBuffers[m_currentFrame], 0, 1, &viewport);

		VkRect2D scissor{};
		scissor.offset = {0, 0};
		scissor.extent = m_swapChainExtent;
		vkCmdSetScissor(m_commandBuffers[m_currentFrame], 0, 1, &scissor);
	}

	void endFrame() override
	{
		vkCmdEndRenderPass(m_commandBuffers[m_currentFrame]);
		vkEndCommandBuffer(m_commandBuffers[m_currentFrame]);

		/// コマンドを送信する
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphores[m_currentFrame] };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &m_commandBuffers[m_currentFrame];

		VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphores[m_currentFrame] };
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]) != VK_SUCCESS)
		{
			throw std::runtime_error("vkQueueSubmit failed");
		}

		/// プレゼントする
		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = signalSemaphores;

		VkSwapchainKHR swapChains[] = { m_swapChain };
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = swapChains;
		presentInfo.pImageIndices = &m_currentImageIndex;

		VkResult result = vkQueuePresentKHR(m_presentQueue, &presentInfo);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
		{
			recreateSwapChain();
		}

		m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
	}

	[[nodiscard]] std::unique_ptr<IBuffer> createBuffer(
		BufferType bufferType,
		std::uint32_t sizeBytes,
		bool dynamic,
		const void* initialData) override
	{
		if (m_device == VK_NULL_HANDLE)
		{
			return std::make_unique<VulkanBuffer>(bufferType, sizeBytes);
		}
		return std::make_unique<VulkanBuffer>(
			m_device, m_physicalDevice, bufferType, sizeBytes, dynamic, initialData);
	}

	[[nodiscard]] std::unique_ptr<ICommandList> createCommandList() override
	{
		if (m_device == VK_NULL_HANDLE || m_commandPool == VK_NULL_HANDLE)
		{
			return std::make_unique<VulkanCommandList>();
		}

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = m_commandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = 1;

		VkCommandBuffer cmdBuf;
		if (vkAllocateCommandBuffers(m_device, &allocInfo, &cmdBuf) != VK_SUCCESS)
		{
			return std::make_unique<VulkanCommandList>();
		}
		return std::make_unique<VulkanCommandList>(cmdBuf);
	}

	[[nodiscard]] uint32_t currentFrameIndex() const override
	{
		return m_currentFrame;
	}

	[[nodiscard]] uint32_t frameInFlightCount() const override
	{
		return MAX_FRAMES_IN_FLIGHT;
	}

	void waitForGpu() override
	{
		if (m_device != VK_NULL_HANDLE)
		{
			vkDeviceWaitIdle(m_device);
		}
	}

private:
	// ── Instance（インスタンス） ──────────────────────────────────────────────

	void createInstance()
	{
		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "MitiruApp";
		appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.pEngineName = "MitiruEngine";
		appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
		appInfo.apiVersion = VK_API_VERSION_1_0;

		/// 必要な拡張を取得する
		std::vector<const char*> extensions;

#ifdef MITIRU_HAS_GLFW
		uint32_t glfwExtCount = 0;
		const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
		for (uint32_t i = 0; i < glfwExtCount; ++i)
		{
			extensions.push_back(glfwExts[i]);
		}
#endif

		/// macOS MoltenVK対応
#ifdef __APPLE__
		extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
		extensions.push_back("VK_KHR_get_physical_device_properties2");
#endif

		VkInstanceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;
		createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
		createInfo.ppEnabledExtensionNames = extensions.data();
		createInfo.enabledLayerCount = 0;

#ifdef __APPLE__
		createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

		if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS)
		{
			throw std::runtime_error("vkCreateInstance failed");
		}
	}

	// ── Surface（サーフェス） ───────────────────────────────────────────────

	void createSurface()
	{
#ifdef MITIRU_HAS_GLFW
		if (glfwCreateWindowSurface(m_instance, m_glfwWindow->nativeWindow(),
			nullptr, &m_surface) != VK_SUCCESS)
		{
			throw std::runtime_error("glfwCreateWindowSurface failed");
		}
#endif
	}

	// ── Physical Device（物理デバイス） ───────────────────────────────────────

	void pickPhysicalDevice()
	{
		uint32_t deviceCount = 0;
		vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
		if (deviceCount == 0)
		{
			throw std::runtime_error("No Vulkan-capable GPU found");
		}

		std::vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

		/// デバイスをスコアリングして最適なものを選択する
		std::vector<PhysicalDeviceInfo> deviceInfos;
		deviceInfos.reserve(deviceCount);

		for (const auto& dev : devices)
		{
			VkPhysicalDeviceProperties props;
			vkGetPhysicalDeviceProperties(dev, &props);

			VkPhysicalDeviceMemoryProperties memProps;
			vkGetPhysicalDeviceMemoryProperties(dev, &memProps);

			PhysicalDeviceInfo info;
			info.deviceName = props.deviceName;
			info.vendorId = props.vendorID;
			info.deviceId = props.deviceID;
			info.apiVersion = props.apiVersion;
			info.driverVersion = props.driverVersion;

			switch (props.deviceType)
			{
			case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
				info.gpuType = GpuType::DiscreteGpu; break;
			case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
				info.gpuType = GpuType::IntegratedGpu; break;
			case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
				info.gpuType = GpuType::VirtualGpu; break;
			case VK_PHYSICAL_DEVICE_TYPE_CPU:
				info.gpuType = GpuType::Cpu; break;
			default:
				info.gpuType = GpuType::Unknown; break;
			}

			/// 専用ビデオメモリを計算する
			for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i)
			{
				if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
				{
					info.dedicatedVideoMemory += memProps.memoryHeaps[i].size;
				}
			}

			/// キューファミリを探索する
			uint32_t queueFamilyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, nullptr);
			std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, queueFamilies.data());

			for (uint32_t i = 0; i < queueFamilyCount; ++i)
			{
				if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
				{
					info.queueFamilies.graphics = i;
				}
				if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
				{
					info.queueFamilies.compute = i;
				}
				if (queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT)
				{
					info.queueFamilies.transfer = i;
				}

				VkBool32 presentSupport = VK_FALSE;
				vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, m_surface, &presentSupport);
				if (presentSupport == VK_TRUE)
				{
					info.queueFamilies.present = i;
				}
			}

			/// デバイス拡張を取得する
			uint32_t extCount = 0;
			vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
			std::vector<VkExtensionProperties> availableExts(extCount);
			vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, availableExts.data());
			for (const auto& ext : availableExts)
			{
				info.supportedExtensions.emplace_back(ext.extensionName);
			}

			deviceInfos.push_back(std::move(info));
		}

		auto bestIdx = selectBestDevice(deviceInfos);
		if (!bestIdx.has_value() || !deviceInfos[bestIdx.value()].queueFamilies.isComplete())
		{
			throw std::runtime_error("No suitable Vulkan GPU found");
		}

		m_physicalDevice = devices[bestIdx.value()];
		m_queueFamilies = deviceInfos[bestIdx.value()].queueFamilies;
	}

	// ── Logical Device（論理デバイス） ────────────────────────────────────────

	void createLogicalDevice()
	{
		/// ユニークなキューファミリインデックスを収集する
		std::vector<uint32_t> uniqueQueueFamilies;
		uniqueQueueFamilies.push_back(m_queueFamilies.graphics.value());
		if (m_queueFamilies.present.value() != m_queueFamilies.graphics.value())
		{
			uniqueQueueFamilies.push_back(m_queueFamilies.present.value());
		}

		float queuePriority = 1.0f;
		std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
		for (uint32_t queueFamily : uniqueQueueFamilies)
		{
			VkDeviceQueueCreateInfo queueCreateInfo{};
			queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueCreateInfo.queueFamilyIndex = queueFamily;
			queueCreateInfo.queueCount = 1;
			queueCreateInfo.pQueuePriorities = &queuePriority;
			queueCreateInfos.push_back(queueCreateInfo);
		}

		VkPhysicalDeviceFeatures deviceFeatures{};

		std::vector<const char*> deviceExtensions = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		};

		/// macOS MoltenVK対応
#ifdef __APPLE__
		deviceExtensions.push_back("VK_KHR_portability_subset");
#endif

		VkDeviceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
		createInfo.pQueueCreateInfos = queueCreateInfos.data();
		createInfo.pEnabledFeatures = &deviceFeatures;
		createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
		createInfo.ppEnabledExtensionNames = deviceExtensions.data();

		if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS)
		{
			throw std::runtime_error("vkCreateDevice failed");
		}

		vkGetDeviceQueue(m_device, m_queueFamilies.graphics.value(), 0, &m_graphicsQueue);
		vkGetDeviceQueue(m_device, m_queueFamilies.present.value(), 0, &m_presentQueue);
	}

	// ── Swap Chain（スワップチェーン） ────────────────────────────────────────────

	struct SwapChainSupportDetails
	{
		VkSurfaceCapabilitiesKHR capabilities{};
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> presentModes;
	};

	[[nodiscard]] SwapChainSupportDetails querySwapChainSupport() const
	{
		SwapChainSupportDetails details;
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &details.capabilities);

		uint32_t formatCount = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
		if (formatCount > 0)
		{
			details.formats.resize(formatCount);
			vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, details.formats.data());
		}

		uint32_t presentModeCount = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, nullptr);
		if (presentModeCount > 0)
		{
			details.presentModes.resize(presentModeCount);
			vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, details.presentModes.data());
		}

		return details;
	}

	[[nodiscard]] static VkSurfaceFormatKHR chooseSwapSurfaceFormat(
		const std::vector<VkSurfaceFormatKHR>& availableFormats) noexcept
	{
		for (const auto& format : availableFormats)
		{
			if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
				format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				return format;
			}
		}
		return availableFormats[0];
	}

	[[nodiscard]] static VkPresentModeKHR chooseSwapPresentMode(
		const std::vector<VkPresentModeKHR>& availablePresentModes) noexcept
	{
		for (const auto& mode : availablePresentModes)
		{
			if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
			{
				return mode;
			}
		}
		return VK_PRESENT_MODE_FIFO_KHR;
	}

	[[nodiscard]] VkExtent2D chooseSwapExtent(
		const VkSurfaceCapabilitiesKHR& capabilities) const noexcept
	{
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
		{
			return capabilities.currentExtent;
		}

		VkExtent2D actualExtent = {
			static_cast<uint32_t>(m_glfwWindow->width()),
			static_cast<uint32_t>(m_glfwWindow->height())
		};

		actualExtent.width = std::clamp(
			actualExtent.width,
			capabilities.minImageExtent.width,
			capabilities.maxImageExtent.width);
		actualExtent.height = std::clamp(
			actualExtent.height,
			capabilities.minImageExtent.height,
			capabilities.maxImageExtent.height);

		return actualExtent;
	}

	void createSwapChain()
	{
		auto swapChainSupport = querySwapChainSupport();
		auto surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
		auto presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
		auto extent = chooseSwapExtent(swapChainSupport.capabilities);

		uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
		if (swapChainSupport.capabilities.maxImageCount > 0 &&
			imageCount > swapChainSupport.capabilities.maxImageCount)
		{
			imageCount = swapChainSupport.capabilities.maxImageCount;
		}

		VkSwapchainCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = m_surface;
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = surfaceFormat.format;
		createInfo.imageColorSpace = surfaceFormat.colorSpace;
		createInfo.imageExtent = extent;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

		uint32_t queueFamilyIndices[] = {
			m_queueFamilies.graphics.value(),
			m_queueFamilies.present.value()
		};

		if (m_queueFamilies.graphics.value() != m_queueFamilies.present.value())
		{
			createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			createInfo.queueFamilyIndexCount = 2;
			createInfo.pQueueFamilyIndices = queueFamilyIndices;
		}
		else
		{
			createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		}

		createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;
		createInfo.oldSwapchain = VK_NULL_HANDLE;

		if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapChain) != VK_SUCCESS)
		{
			throw std::runtime_error("vkCreateSwapchainKHR failed");
		}

		vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, nullptr);
		m_swapChainImages.resize(imageCount);
		vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, m_swapChainImages.data());

		m_swapChainImageFormat = surfaceFormat.format;
		m_swapChainExtent = extent;

		createImageViews();
		createRenderPass();
		createFramebuffers();
	}

	void createImageViews()
	{
		m_swapChainImageViews.resize(m_swapChainImages.size());

		for (std::size_t i = 0; i < m_swapChainImages.size(); ++i)
		{
			VkImageViewCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			createInfo.image = m_swapChainImages[i];
			createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			createInfo.format = m_swapChainImageFormat;
			createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			createInfo.subresourceRange.baseMipLevel = 0;
			createInfo.subresourceRange.levelCount = 1;
			createInfo.subresourceRange.baseArrayLayer = 0;
			createInfo.subresourceRange.layerCount = 1;

			if (vkCreateImageView(m_device, &createInfo, nullptr, &m_swapChainImageViews[i]) != VK_SUCCESS)
			{
				throw std::runtime_error("vkCreateImageView failed");
			}
		}
	}

	void createRenderPass()
	{
		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = m_swapChainImageFormat;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference colorAttachmentRef{};
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;

		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &colorAttachment;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = &dependency;

		if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS)
		{
			throw std::runtime_error("vkCreateRenderPass failed");
		}
	}

	void createFramebuffers()
	{
		m_swapChainFramebuffers.resize(m_swapChainImageViews.size());

		for (std::size_t i = 0; i < m_swapChainImageViews.size(); ++i)
		{
			VkImageView attachments[] = { m_swapChainImageViews[i] };

			VkFramebufferCreateInfo framebufferInfo{};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = m_renderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = attachments;
			framebufferInfo.width = m_swapChainExtent.width;
			framebufferInfo.height = m_swapChainExtent.height;
			framebufferInfo.layers = 1;

			if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_swapChainFramebuffers[i]) != VK_SUCCESS)
			{
				throw std::runtime_error("vkCreateFramebuffer failed");
			}
		}
	}

	void destroySwapChain()
	{
		for (auto framebuffer : m_swapChainFramebuffers)
		{
			if (framebuffer != VK_NULL_HANDLE)
			{
				vkDestroyFramebuffer(m_device, framebuffer, nullptr);
			}
		}
		m_swapChainFramebuffers.clear();

		if (m_renderPass != VK_NULL_HANDLE)
		{
			vkDestroyRenderPass(m_device, m_renderPass, nullptr);
			m_renderPass = VK_NULL_HANDLE;
		}

		for (auto imageView : m_swapChainImageViews)
		{
			if (imageView != VK_NULL_HANDLE)
			{
				vkDestroyImageView(m_device, imageView, nullptr);
			}
		}
		m_swapChainImageViews.clear();

		if (m_swapChain != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
			m_swapChain = VK_NULL_HANDLE;
		}
	}

	void recreateSwapChain()
	{
		/// ウィンドウ最小化時はサイズが0になるため待機する
		int width = m_glfwWindow->width();
		int height = m_glfwWindow->height();
		if (width == 0 || height == 0)
		{
			return;
		}

		vkDeviceWaitIdle(m_device);
		destroySwapChain();
		createSwapChain();
	}

	// ── Command Pool / Buffers（コマンドプール・バッファ） ────────────────────────────────

	void createCommandPool()
	{
		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = m_queueFamilies.graphics.value();

		if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS)
		{
			throw std::runtime_error("vkCreateCommandPool failed");
		}
	}

	void createCommandBuffers()
	{
		m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = m_commandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

		if (vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS)
		{
			throw std::runtime_error("vkAllocateCommandBuffers failed");
		}
	}

	void destroyCommandPool()
	{
		if (m_commandPool != VK_NULL_HANDLE)
		{
			vkDestroyCommandPool(m_device, m_commandPool, nullptr);
			m_commandPool = VK_NULL_HANDLE;
		}
	}

	// ── Sync Objects（同期オブジェクト） ──────────────────────────────────────────

	void createSyncObjects()
	{
		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
				vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS ||
				vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS)
			{
				throw std::runtime_error("Failed to create sync objects");
			}
		}
	}

	void destroySyncObjects()
	{
		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			if (m_imageAvailableSemaphores[i] != VK_NULL_HANDLE)
			{
				vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
			}
			if (m_renderFinishedSemaphores[i] != VK_NULL_HANDLE)
			{
				vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
			}
			if (m_inFlightFences[i] != VK_NULL_HANDLE)
			{
				vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
			}
		}
	}

	// ── Memory（メモリ） ────────────────────────────────────────────────

	[[nodiscard]] std::optional<uint32_t> findMemoryType(
		uint32_t typeFilter,
		VkMemoryPropertyFlags properties) const noexcept
	{
		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

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

#endif // MITIRU_HAS_VULKAN
