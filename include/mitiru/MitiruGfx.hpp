#pragma once

/// @file MitiruGfx.hpp
/// @brief GFX device abstractions + all backends

// GFX abstractions
#include <mitiru/gfx/GfxFactory.hpp>
#include <mitiru/gfx/GfxTypes.hpp>
#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/ICommandList.hpp>
#include <mitiru/gfx/IDescriptorHeap.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/gfx/IGpuFence.hpp>
#include <mitiru/gfx/IPipeline.hpp>
#include <mitiru/gfx/IRenderTarget.hpp>
#include <mitiru/gfx/IShader.hpp>
#include <mitiru/gfx/ISwapChain.hpp>
#include <mitiru/gfx/ITexture.hpp>
#include <mitiru/gfx/null/NullDevice.hpp>

#ifdef MITIRU_HAS_DX11
#include <mitiru/gfx/dx11/Dx11Buffer.hpp>
#include <mitiru/gfx/dx11/Dx11CommandList.hpp>
#include <mitiru/gfx/dx11/Dx11Device.hpp>
#include <mitiru/gfx/dx11/Dx11Pipeline.hpp>
#include <mitiru/gfx/dx11/Dx11RenderTarget.hpp>
#include <mitiru/gfx/dx11/Dx11Shader.hpp>
#include <mitiru/gfx/dx11/Dx11SwapChain.hpp>
#include <mitiru/gfx/dx11/Dx11Texture.hpp>
#endif

#ifdef MITIRU_HAS_DX12
#include <mitiru/gfx/dx12/Dx12Buffer.hpp>
#include <mitiru/gfx/dx12/Dx12CommandList.hpp>
#include <mitiru/gfx/dx12/Dx12DescriptorHeap.hpp>
#include <mitiru/gfx/dx12/Dx12Device.hpp>
#include <mitiru/gfx/dx12/Dx12Fence.hpp>
#include <mitiru/gfx/dx12/Dx12Pipeline.hpp>
#include <mitiru/gfx/dx12/Dx12RenderTarget.hpp>
#include <mitiru/gfx/dx12/Dx12Shader.hpp>
#include <mitiru/gfx/dx12/Dx12SwapChain.hpp>
#include <mitiru/gfx/dx12/Dx12Texture.hpp>
#endif

#ifdef MITIRU_HAS_VULKAN
#include <mitiru/gfx/vulkan/VulkanBuffer.hpp>
#include <mitiru/gfx/vulkan/VulkanDevice.hpp>
#include <mitiru/gfx/vulkan/VulkanInstance.hpp>
#include <mitiru/gfx/vulkan/VulkanPhysicalDevice.hpp>
#include <mitiru/gfx/vulkan/VulkanPipeline.hpp>
#include <mitiru/gfx/vulkan/VulkanRenderPass.hpp>
#include <mitiru/gfx/vulkan/VulkanSwapChain.hpp>
#endif

#ifdef __EMSCRIPTEN__
#include <mitiru/gfx/webgl/WebGLDevice.hpp>
#endif
