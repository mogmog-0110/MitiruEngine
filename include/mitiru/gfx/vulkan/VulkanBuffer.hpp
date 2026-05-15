#pragma once

/// @file VulkanBuffer.hpp
/// @brief Vulkanバッファ スタブ実装（単独ヘッダー）
/// @details VkBufferをラップするIBuffer実装の単独ヘッダー版。
///          VulkanDevice.hpp内のVulkanBufferと同一のスタブだが、
///          バッファのみを使いたい場合に軽量なインクルードを提供する。
///          MITIRU_HAS_VULKANが定義されている場合のみコンパイルされる。

#ifdef MITIRU_HAS_VULKAN

#include <cstdint>

#include <mitiru/gfx/IBuffer.hpp>

namespace mitiru::gfx
{

/// @note VulkanBufferクラスはVulkanDevice.hppで定義済み。
///       このヘッダーは、VulkanDevice.hppをインクルードせずに
///       バッファ型のみを参照したい場合の利便性のために存在する。
///       重複定義を避けるため、VulkanDevice.hppで定義されたクラスを使用すること。

} // namespace mitiru::gfx

#endif // MITIRU_HAS_VULKAN
