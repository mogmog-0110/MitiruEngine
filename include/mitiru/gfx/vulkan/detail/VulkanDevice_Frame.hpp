#pragma once
// mitiru::gfx::VulkanDevice 用の detail header — 直接インクルードしない。gfx/vulkan/VulkanDevice.hpp 経由で取り込む
// 生成・破棄とフレームループ（readPixels / beginFrame / endFrame / リソース生成）

#include <mitiru/gfx/vulkan/VulkanDevice.hpp>

#ifdef MITIRU_HAS_VULKAN

#ifdef MITIRU_HAS_GLFW
inline mitiru::gfx::VulkanDevice::VulkanDevice(mitiru::GlfwWindow* window)
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

inline mitiru::gfx::VulkanDevice::~VulkanDevice()
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

inline std::vector<std::uint8_t> mitiru::gfx::VulkanDevice::readPixels(
	int width, int height) const
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

inline mitiru::gfx::Backend mitiru::gfx::VulkanDevice::backend() const noexcept
{
	return Backend::Vulkan;
}

inline void mitiru::gfx::VulkanDevice::beginFrame()
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

inline void mitiru::gfx::VulkanDevice::endFrame()
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

inline std::unique_ptr<mitiru::gfx::IBuffer> mitiru::gfx::VulkanDevice::createBuffer(
	BufferType bufferType,
	std::uint32_t sizeBytes,
	bool dynamic,
	const void* initialData)
{
	if (m_device == VK_NULL_HANDLE)
	{
		return std::make_unique<VulkanBuffer>(bufferType, sizeBytes);
	}
	return std::make_unique<VulkanBuffer>(
		m_device, m_physicalDevice, bufferType, sizeBytes, dynamic, initialData);
}

inline std::unique_ptr<mitiru::gfx::ICommandList> mitiru::gfx::VulkanDevice::createCommandList()
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

inline uint32_t mitiru::gfx::VulkanDevice::currentFrameIndex() const
{
	return m_currentFrame;
}

inline uint32_t mitiru::gfx::VulkanDevice::frameInFlightCount() const
{
	return MAX_FRAMES_IN_FLIGHT;
}

inline void mitiru::gfx::VulkanDevice::waitForGpu()
{
	if (m_device != VK_NULL_HANDLE)
	{
		vkDeviceWaitIdle(m_device);
	}
}

#endif // MITIRU_HAS_VULKAN
