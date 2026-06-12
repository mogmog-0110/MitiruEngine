#pragma once
// mitiru::gfx::VulkanDevice 用の detail header — 直接インクルードしない。gfx/vulkan/VulkanDevice.hpp 経由で取り込む
// スワップチェーン系（生成・破棄・再生成・フォーマット選択）

#include <mitiru/gfx/vulkan/VulkanDevice.hpp>

#ifdef MITIRU_HAS_VULKAN

// ── Swap Chain（スワップチェーン） ────────────────────────────────────────────

inline mitiru::gfx::VulkanDevice::SwapChainSupportDetails
mitiru::gfx::VulkanDevice::querySwapChainSupport() const
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

inline VkSurfaceFormatKHR mitiru::gfx::VulkanDevice::chooseSwapSurfaceFormat(
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

inline VkPresentModeKHR mitiru::gfx::VulkanDevice::chooseSwapPresentMode(
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

inline VkExtent2D mitiru::gfx::VulkanDevice::chooseSwapExtent(
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

inline void mitiru::gfx::VulkanDevice::createSwapChain()
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

inline void mitiru::gfx::VulkanDevice::createImageViews()
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

inline void mitiru::gfx::VulkanDevice::createRenderPass()
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

inline void mitiru::gfx::VulkanDevice::createFramebuffers()
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

inline void mitiru::gfx::VulkanDevice::destroySwapChain()
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

inline void mitiru::gfx::VulkanDevice::recreateSwapChain()
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

#endif // MITIRU_HAS_VULKAN
