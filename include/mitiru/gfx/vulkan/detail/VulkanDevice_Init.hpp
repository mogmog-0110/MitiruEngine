#pragma once
// mitiru::gfx::VulkanDevice 用の detail header — 直接インクルードしない。gfx/vulkan/VulkanDevice.hpp 経由で取り込む
// 初期化系（instance / surface / 物理・論理デバイス / コマンドプール / 同期 / メモリ）

#include <mitiru/gfx/vulkan/VulkanDevice.hpp>

#ifdef MITIRU_HAS_VULKAN

// ── Instance（インスタンス） ──────────────────────────────────────────────

inline void mitiru::gfx::VulkanDevice::createInstance()
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

inline void mitiru::gfx::VulkanDevice::createSurface()
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

inline void mitiru::gfx::VulkanDevice::pickPhysicalDevice()
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

inline void mitiru::gfx::VulkanDevice::createLogicalDevice()
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

// ── Command Pool / Buffers（コマンドプール・バッファ） ────────────────────────────────

inline void mitiru::gfx::VulkanDevice::createCommandPool()
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

inline void mitiru::gfx::VulkanDevice::createCommandBuffers()
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

inline void mitiru::gfx::VulkanDevice::destroyCommandPool()
{
	if (m_commandPool != VK_NULL_HANDLE)
	{
		vkDestroyCommandPool(m_device, m_commandPool, nullptr);
		m_commandPool = VK_NULL_HANDLE;
	}
}

// ── Sync Objects（同期オブジェクト） ──────────────────────────────────────────

inline void mitiru::gfx::VulkanDevice::createSyncObjects()
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

inline void mitiru::gfx::VulkanDevice::destroySyncObjects()
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

inline std::optional<uint32_t> mitiru::gfx::VulkanDevice::findMemoryType(
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

#endif // MITIRU_HAS_VULKAN
