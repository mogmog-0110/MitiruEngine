#include <catch2/catch_test_macros.hpp>

#include <mitiru/gfx/vulkan/VulkanInstance.hpp>
#include <mitiru/gfx/vulkan/VulkanPhysicalDevice.hpp>
#include <mitiru/gfx/vulkan/VulkanRenderPass.hpp>
#include <mitiru/gfx/vulkan/VulkanPipeline.hpp>

// ============================================================
// VulkanAppInfo
// ============================================================

TEST_CASE("VulkanAppInfo - default values", "[vulkan]")
{
	const mitiru::gfx::VulkanAppInfo info;

	REQUIRE(info.appName == "MitiruApp");
	REQUIRE(info.appVersion == 1);
	REQUIRE(info.engineName == "MitiruEngine");
	REQUIRE(info.engineVersion == 1);
	REQUIRE(info.apiVersion == 0);
}

// ============================================================
// VulkanValidationConfig
// ============================================================

TEST_CASE("VulkanValidationConfig - debug preset", "[vulkan]")
{
	const auto config = mitiru::gfx::VulkanValidationConfig::debug();

	REQUIRE(config.enableValidation == true);
	REQUIRE(config.enableDebugMessenger == true);
	REQUIRE(config.requiredLayers.size() == 1);
	REQUIRE(config.requiredLayers[0] == "VK_LAYER_KHRONOS_validation");
}

TEST_CASE("VulkanValidationConfig - release preset", "[vulkan]")
{
	const auto config = mitiru::gfx::VulkanValidationConfig::release();

	REQUIRE(config.enableValidation == false);
	REQUIRE(config.enableDebugMessenger == false);
	REQUIRE(config.requiredLayers.empty());
}

// ============================================================
// VulkanInstanceDesc
// ============================================================

TEST_CASE("VulkanInstanceDesc - hasExtension", "[vulkan]")
{
	mitiru::gfx::VulkanInstanceDesc desc;
	desc.requiredExtensions.push_back("VK_KHR_surface");

	REQUIRE(desc.hasExtension("VK_KHR_surface") == true);
	REQUIRE(desc.hasExtension("VK_KHR_swapchain") == false);
}

TEST_CASE("VulkanInstanceDesc - addExtension avoids duplicates", "[vulkan]")
{
	mitiru::gfx::VulkanInstanceDesc desc;
	desc.addExtension("VK_KHR_surface");
	desc.addExtension("VK_KHR_surface");

	REQUIRE(desc.requiredExtensions.size() == 1);
}

TEST_CASE("VulkanInstanceDesc - addSurfaceExtensions", "[vulkan]")
{
	mitiru::gfx::VulkanInstanceDesc desc;
	desc.addSurfaceExtensions();

	REQUIRE(desc.hasExtension("VK_KHR_surface") == true);
}

// ============================================================
// QueueFamilyIndices
// ============================================================

TEST_CASE("QueueFamilyIndices - empty is not complete", "[vulkan]")
{
	const mitiru::gfx::QueueFamilyIndices indices;

	REQUIRE(indices.isComplete() == false);
	REQUIRE(indices.hasAll() == false);
}

TEST_CASE("QueueFamilyIndices - graphics and present is complete", "[vulkan]")
{
	mitiru::gfx::QueueFamilyIndices indices;
	indices.graphics = 0;
	indices.present = 0;

	REQUIRE(indices.isComplete() == true);
	REQUIRE(indices.hasAll() == false);
}

TEST_CASE("QueueFamilyIndices - all families set", "[vulkan]")
{
	mitiru::gfx::QueueFamilyIndices indices;
	indices.graphics = 0;
	indices.compute = 1;
	indices.transfer = 2;
	indices.present = 0;

	REQUIRE(indices.isComplete() == true);
	REQUIRE(indices.hasAll() == true);
}

// ============================================================
// PhysicalDeviceInfo
// ============================================================

TEST_CASE("PhysicalDeviceInfo - supportsExtension", "[vulkan]")
{
	mitiru::gfx::PhysicalDeviceInfo info;
	info.supportedExtensions.push_back("VK_KHR_swapchain");
	info.supportedExtensions.push_back("VK_KHR_maintenance1");

	REQUIRE(info.supportsExtension("VK_KHR_swapchain") == true);
	REQUIRE(info.supportsExtension("VK_EXT_debug_utils") == false);
}

// ============================================================
// scorePhysicalDevice
// ============================================================

TEST_CASE("scorePhysicalDevice - discrete GPU scores higher than integrated", "[vulkan]")
{
	mitiru::gfx::PhysicalDeviceInfo discrete;
	discrete.gpuType = mitiru::gfx::GpuType::DiscreteGpu;
	discrete.queueFamilies.graphics = 0;
	discrete.queueFamilies.present = 0;

	mitiru::gfx::PhysicalDeviceInfo integrated;
	integrated.gpuType = mitiru::gfx::GpuType::IntegratedGpu;
	integrated.queueFamilies.graphics = 0;
	integrated.queueFamilies.present = 0;

	REQUIRE(mitiru::gfx::scorePhysicalDevice(discrete) >
		mitiru::gfx::scorePhysicalDevice(integrated));
}

TEST_CASE("scorePhysicalDevice - more VRAM scores higher", "[vulkan]")
{
	mitiru::gfx::PhysicalDeviceInfo bigVram;
	bigVram.gpuType = mitiru::gfx::GpuType::DiscreteGpu;
	bigVram.dedicatedVideoMemory = 8ULL * 1024 * 1024 * 1024;

	mitiru::gfx::PhysicalDeviceInfo smallVram;
	smallVram.gpuType = mitiru::gfx::GpuType::DiscreteGpu;
	smallVram.dedicatedVideoMemory = 2ULL * 1024 * 1024 * 1024;

	REQUIRE(mitiru::gfx::scorePhysicalDevice(bigVram) >
		mitiru::gfx::scorePhysicalDevice(smallVram));
}

TEST_CASE("scorePhysicalDevice - swapchain extension adds score", "[vulkan]")
{
	mitiru::gfx::PhysicalDeviceInfo withSwapchain;
	withSwapchain.gpuType = mitiru::gfx::GpuType::DiscreteGpu;
	withSwapchain.supportedExtensions.push_back("VK_KHR_swapchain");

	mitiru::gfx::PhysicalDeviceInfo withoutSwapchain;
	withoutSwapchain.gpuType = mitiru::gfx::GpuType::DiscreteGpu;

	REQUIRE(mitiru::gfx::scorePhysicalDevice(withSwapchain) >
		mitiru::gfx::scorePhysicalDevice(withoutSwapchain));
}

// ============================================================
// selectBestDevice
// ============================================================

TEST_CASE("selectBestDevice - empty list returns nullopt", "[vulkan]")
{
	const std::vector<mitiru::gfx::PhysicalDeviceInfo> devices;
	REQUIRE(mitiru::gfx::selectBestDevice(devices) == std::nullopt);
}

TEST_CASE("selectBestDevice - selects discrete GPU over integrated", "[vulkan]")
{
	std::vector<mitiru::gfx::PhysicalDeviceInfo> devices(2);
	devices[0].deviceName = "Integrated GPU";
	devices[0].gpuType = mitiru::gfx::GpuType::IntegratedGpu;
	devices[1].deviceName = "Discrete GPU";
	devices[1].gpuType = mitiru::gfx::GpuType::DiscreteGpu;

	const auto best = mitiru::gfx::selectBestDevice(devices);
	REQUIRE(best.has_value());
	REQUIRE(best.value() == 1);
}

TEST_CASE("selectBestDevice - single device returns index 0", "[vulkan]")
{
	std::vector<mitiru::gfx::PhysicalDeviceInfo> devices(1);
	devices[0].deviceName = "Only GPU";
	devices[0].gpuType = mitiru::gfx::GpuType::DiscreteGpu;

	const auto best = mitiru::gfx::selectBestDevice(devices);
	REQUIRE(best.has_value());
	REQUIRE(best.value() == 0);
}

// ============================================================
// RenderPassDesc
// ============================================================

TEST_CASE("AttachmentDesc - default values", "[vulkan]")
{
	const mitiru::gfx::AttachmentDesc desc;

	REQUIRE(desc.format == mitiru::gfx::PixelFormat::RGBA8);
	REQUIRE(desc.sampleCount == 1);
	REQUIRE(desc.loadOp == mitiru::gfx::AttachmentLoadOp::Clear);
	REQUIRE(desc.storeOp == mitiru::gfx::AttachmentStoreOp::Store);
}

TEST_CASE("SubpassDesc - hasDepthStencil", "[vulkan]")
{
	mitiru::gfx::SubpassDesc subpass;
	REQUIRE(subpass.hasDepthStencil() == false);

	subpass.depthStencilAttachment = 1;
	REQUIRE(subpass.hasDepthStencil() == true);
}

TEST_CASE("RenderPassDesc - simpleColor creates valid pass", "[vulkan]")
{
	const auto desc = mitiru::gfx::RenderPassDesc::simpleColor();

	REQUIRE(desc.attachments.size() == 1);
	REQUIRE(desc.subpasses.size() == 1);
	REQUIRE(desc.dependencies.size() == 1);
	REQUIRE(desc.attachments[0].format == mitiru::gfx::PixelFormat::BGRA8);
	REQUIRE(desc.attachments[0].finalLayout == mitiru::gfx::ImageLayout::PresentSrc);
	REQUIRE(desc.subpasses[0].colorAttachments.size() == 1);
	REQUIRE(desc.subpasses[0].hasDepthStencil() == false);
}

TEST_CASE("RenderPassDesc - colorDepth creates pass with depth", "[vulkan]")
{
	const auto desc = mitiru::gfx::RenderPassDesc::colorDepth();

	REQUIRE(desc.attachments.size() == 2);
	REQUIRE(desc.subpasses.size() == 1);
	REQUIRE(desc.attachments[1].format == mitiru::gfx::PixelFormat::Depth24Stencil8);
	REQUIRE(desc.subpasses[0].hasDepthStencil() == true);
	REQUIRE(desc.subpasses[0].depthStencilAttachment == 1);
}

TEST_CASE("SubpassDependency - externalToFirst", "[vulkan]")
{
	const auto dep = mitiru::gfx::SubpassDependency::externalToFirst();

	REQUIRE(dep.srcSubpass == UINT32_MAX);
	REQUIRE(dep.dstSubpass == 0);
}

// ============================================================
// GraphicsPipelineDesc
// ============================================================

TEST_CASE("GraphicsPipelineDesc - default2D preset", "[vulkan]")
{
	const auto desc = mitiru::gfx::GraphicsPipelineDesc::default2D();

	REQUIRE(desc.topology == mitiru::gfx::PrimitiveTopology::TriangleList);
	REQUIRE(desc.rasterizer.cullMode == mitiru::gfx::CullMode::None);
	REQUIRE(desc.depthStencil.depthTestEnable == false);
	REQUIRE(desc.depthStencil.depthWriteEnable == false);
	REQUIRE(desc.blendMode == mitiru::gfx::BlendMode::Alpha);
}

TEST_CASE("GraphicsPipelineDesc - default3D preset", "[vulkan]")
{
	const auto desc = mitiru::gfx::GraphicsPipelineDesc::default3D();

	REQUIRE(desc.topology == mitiru::gfx::PrimitiveTopology::TriangleList);
	REQUIRE(desc.rasterizer.cullMode == mitiru::gfx::CullMode::Back);
	REQUIRE(desc.depthStencil.depthTestEnable == true);
	REQUIRE(desc.depthStencil.depthWriteEnable == true);
	REQUIRE(desc.blendMode == mitiru::gfx::BlendMode::None);
}

TEST_CASE("RasterizerState - default values", "[vulkan]")
{
	const mitiru::gfx::RasterizerState state;

	REQUIRE(state.polygonMode == mitiru::gfx::PolygonMode::Fill);
	REQUIRE(state.cullMode == mitiru::gfx::CullMode::Back);
	REQUIRE(state.frontFace == mitiru::gfx::FrontFace::CounterClockwise);
	REQUIRE(state.depthClampEnable == false);
	REQUIRE(state.lineWidth == 1.0f);
}

TEST_CASE("MultisampleState - default values", "[vulkan]")
{
	const mitiru::gfx::MultisampleState state;

	REQUIRE(state.sampleCount == 1);
	REQUIRE(state.sampleShadingEnable == false);
	REQUIRE(state.minSampleShading == 1.0f);
}

TEST_CASE("DepthStencilState - default values", "[vulkan]")
{
	const mitiru::gfx::DepthStencilState state;

	REQUIRE(state.depthTestEnable == true);
	REQUIRE(state.depthWriteEnable == true);
	REQUIRE(state.depthCompareOp == mitiru::gfx::CompareOp::Less);
	REQUIRE(state.stencilTestEnable == false);
}

TEST_CASE("VertexInputState - empty by default", "[vulkan]")
{
	const mitiru::gfx::VertexInputState state;

	REQUIRE(state.bindings.empty());
	REQUIRE(state.attributes.empty());
}

TEST_CASE("ShaderStageDesc - default entry point is main", "[vulkan]")
{
	const mitiru::gfx::ShaderStageDesc desc;

	REQUIRE(desc.type == mitiru::gfx::ShaderType::Vertex);
	REQUIRE(desc.code.empty());
	REQUIRE(desc.entryPoint == "main");
}

TEST_CASE("PipelineLayoutDesc - default values", "[vulkan]")
{
	const mitiru::gfx::PipelineLayoutDesc desc;

	REQUIRE(desc.pushConstantRanges.empty());
	REQUIRE(desc.descriptorSetLayoutCount == 0);
}
