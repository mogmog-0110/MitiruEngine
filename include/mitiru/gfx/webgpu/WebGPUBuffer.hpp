#pragma once

/// @file WebGPUBuffer.hpp
/// @brief WebGPU GPUバッファ実装
/// @details wgpuDeviceCreateBufferによるバッファをRAIIで管理する。
///          頂点バッファ・インデックスバッファ・ユニフォームバッファに対応。

#if defined(__EMSCRIPTEN__) && defined(MITIRU_HAS_WEBGPU)

#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <webgpu/webgpu.h>

#include <mitiru/gfx/IBuffer.hpp>

namespace mitiru::gfx
{

/// @brief WebGPU用GPUバッファ実装
/// @details WGPUBufferをRAIIで管理する。
///          デストラクタでwgpuBufferReleaseを呼び出す。
class WebGPUBuffer final : public IBuffer
{
public:
    /// @brief コンストラクタ
    /// @param device WebGPUデバイスハンドル
    /// @param bufferType バッファ種別（Vertex / Index / Constant）
    /// @param sizeBytes バッファサイズ（バイト）
    /// @param dynamic 動的更新が必要か
    /// @param initialData 初期データ（nullptrで初期化なし）
    WebGPUBuffer(
        WGPUDevice device,
        BufferType bufferType,
        std::uint32_t sizeBytes,
        bool dynamic,
        const void* initialData)
        : m_device(device)
        , m_bufferType(bufferType)
        , m_sizeBytes(sizeBytes)
    {
        WGPUBufferUsageFlags usage = 0;
        switch (bufferType)
        {
        case BufferType::Vertex:
            usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            break;
        case BufferType::Index:
            usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
            break;
        case BufferType::Constant:
            usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            break;
        }

        if (dynamic)
        {
            usage |= WGPUBufferUsage_CopyDst;
        }

        WGPUBufferDescriptor desc{};
        desc.size = sizeBytes;
        desc.usage = usage;
        desc.mappedAtCreation = (initialData != nullptr);

        m_buffer = wgpuDeviceCreateBuffer(device, &desc);
        if (!m_buffer)
        {
            throw std::runtime_error("WebGPUBuffer: wgpuDeviceCreateBuffer failed");
        }

        if (initialData)
        {
            void* mapped = wgpuBufferGetMappedRange(m_buffer, 0, sizeBytes);
            if (mapped)
            {
                std::memcpy(mapped, initialData, sizeBytes);
            }
            wgpuBufferUnmap(m_buffer);
        }
    }

    ~WebGPUBuffer() override
    {
        if (m_buffer)
        {
            wgpuBufferRelease(m_buffer);
        }
    }

    WebGPUBuffer(const WebGPUBuffer&) = delete;
    WebGPUBuffer& operator=(const WebGPUBuffer&) = delete;

    WebGPUBuffer(WebGPUBuffer&& other) noexcept
        : m_device(other.m_device)
        , m_buffer(other.m_buffer)
        , m_bufferType(other.m_bufferType)
        , m_sizeBytes(other.m_sizeBytes)
    {
        other.m_buffer = nullptr;
    }

    WebGPUBuffer& operator=(WebGPUBuffer&& other) noexcept
    {
        if (this != &other)
        {
            if (m_buffer)
            {
                wgpuBufferRelease(m_buffer);
            }
            m_device = other.m_device;
            m_buffer = other.m_buffer;
            m_bufferType = other.m_bufferType;
            m_sizeBytes = other.m_sizeBytes;
            other.m_buffer = nullptr;
        }
        return *this;
    }

    /// @brief バッファサイズを取得する
    [[nodiscard]] std::uint32_t size() const noexcept override { return m_sizeBytes; }

    /// @brief バッファデータを更新する（動的バッファ用）
    /// @param queue WebGPUキュー
    /// @param data 書き込むデータ
    /// @param sizeBytes データサイズ（バイト）
    /// @param offset オフセット（バイト）
    void update(WGPUQueue queue, const void* data, std::uint32_t sizeBytes,
                std::uint32_t offset = 0) const
    {
        wgpuQueueWriteBuffer(queue, m_buffer, offset, data, sizeBytes);
    }

    /// @brief WGPUBufferハンドルを取得する
    [[nodiscard]] WGPUBuffer handle() const noexcept { return m_buffer; }

    /// @brief バッファ種別を取得する
    [[nodiscard]] BufferType bufferType() const noexcept { return m_bufferType; }

private:
    WGPUDevice m_device = nullptr;   ///< WebGPUデバイスハンドル（非所有）
    WGPUBuffer m_buffer = nullptr;   ///< WebGPUバッファハンドル
    BufferType m_bufferType{};       ///< バッファ種別
    std::uint32_t m_sizeBytes = 0;   ///< バッファサイズ
};

} // namespace mitiru::gfx

#endif // defined(__EMSCRIPTEN__) && defined(MITIRU_HAS_WEBGPU)
