#pragma once

/// @file VulkanBuffer.hpp
/// @brief Vulkan バッファ スタブ実装（単独ヘッダー）
/// @details VkBuffer をラップする IBuffer 実装の単独ヘッダー版。
///          VulkanDevice.hpp 内の VulkanBuffer と同一のスタブだが、
///          バッファのみを使いたい場合に軽量なインクルードを提供する。
///          MITIRU_HAS_VULKAN が定義されている場合のみコンパイルされる。

#ifdef MITIRU_HAS_VULKAN

#include <cstdint>

#include <mitiru/gfx/IBuffer.hpp>

namespace mitiru::gfx
{

/// @note VulkanBuffer クラスは VulkanDevice.hpp で定義済み。
///       このヘッダーは、VulkanDevice.hpp をインクルードせずに
///       バッファ型のみを参照したい場合に便利なように置いてある。
///       重複定義を避けるため、VulkanDevice.hpp で定義されたクラスを使用すること。

} // namespace mitiru::gfx

#endif // MITIRU_HAS_VULKAN
