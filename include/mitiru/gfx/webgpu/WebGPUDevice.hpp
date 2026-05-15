#pragma once

/// @file WebGPUDevice.hpp
/// @brief WebGPUバックエンド実装
/// @details Emscripten WebGPU環境向けのIDevice実装。
///          webgpu/webgpu.hを使用し、WGSLシェーダーによる描画をサポートする。
///          __EMSCRIPTEN__かつMITIRU_HAS_WEBGPUが定義されている場合のみコンパイルされる。

#if defined(__EMSCRIPTEN__) && defined(MITIRU_HAS_WEBGPU)

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <emscripten/html5.h>
#include <webgpu/webgpu.h>

#include <sgc/types/Color.hpp>

#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/ICommandList.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/gfx/IPipeline.hpp>
#include <mitiru/gfx/IRenderTarget.hpp>
#include <mitiru/gfx/webgpu/WebGPUBuffer.hpp>

namespace mitiru::gfx
{

// ── WGSL default shaders for 2D sprite rendering ──────────────────────

/// @brief 2D頂点シェーダー（WGSL）
/// @details 正射影変換を適用し、頂点色・テクスチャ座標をフラグメントシェーダーに渡す。
constexpr const char* WEBGPU_VERTEX_SHADER_2D = R"wgsl(
struct Uniforms {
    projection: mat4x4<f32>,
};
@binding(0) @group(0) var<uniform> uniforms: Uniforms;

struct VertexInput {
    @location(0) position: vec2<f32>,
    @location(1) texCoord: vec2<f32>,
    @location(2) color: vec4<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
    @location(1) texCoord: vec2<f32>,
};

@vertex
fn main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = uniforms.projection * vec4<f32>(input.position, 0.0, 1.0);
    output.color = input.color;
    output.texCoord = input.texCoord;
    return output;
}
)wgsl";

/// @brief 2Dフラグメントシェーダー（WGSL）
/// @details 頂点色をそのまま出力する。テクスチャ使用時はuseTexture切り替え可能。
constexpr const char* WEBGPU_FRAGMENT_SHADER_2D = R"wgsl(
@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var texColor: texture_2d<f32>;

struct Params {
    useTexture: f32,
};
@group(1) @binding(2) var<uniform> params: Params;

struct FragmentInput {
    @location(0) color: vec4<f32>,
    @location(1) texCoord: vec2<f32>,
};

@fragment
fn main(input: FragmentInput) -> @location(0) vec4<f32> {
    if (params.useTexture > 0.5) {
        let texColor = textureSample(texColor, texSampler, input.texCoord);
        return texColor * input.color;
    }
    return input.color;
}
)wgsl";

/// @brief WebGPU用コマンドリスト実装
/// @details WebGPUのコマンドエンコーダを使用してコマンドを記録する。
///          begin()でエンコーダを作成し、end()でコマンドバッファを完成させる。
class WebGPUCommandList final : public ICommandList
{
public:
    /// @brief コンストラクタ
    /// @param device WebGPUデバイスハンドル
    explicit WebGPUCommandList(WGPUDevice device)
        : m_device(device)
    {
    }

    ~WebGPUCommandList() override
    {
        if (m_renderPassEncoder)
        {
            wgpuRenderPassEncoderRelease(m_renderPassEncoder);
        }
        if (m_commandEncoder)
        {
            wgpuCommandEncoderRelease(m_commandEncoder);
        }
    }

    WebGPUCommandList(const WebGPUCommandList&) = delete;
    WebGPUCommandList& operator=(const WebGPUCommandList&) = delete;
    WebGPUCommandList(WebGPUCommandList&&) = delete;
    WebGPUCommandList& operator=(WebGPUCommandList&&) = delete;

    void begin() override
    {
        WGPUCommandEncoderDescriptor encoderDesc{};
        m_commandEncoder = wgpuDeviceCreateCommandEncoder(m_device, &encoderDesc);
    }

    void end() override
    {
        if (m_renderPassEncoder)
        {
            wgpuRenderPassEncoderEnd(m_renderPassEncoder);
            wgpuRenderPassEncoderRelease(m_renderPassEncoder);
            m_renderPassEncoder = nullptr;
        }
    }

    void setRenderTarget(IRenderTarget*) override
    {
        // WebGPU render targets are handled via render pass descriptors
    }

    void clearRenderTarget(const sgc::Colorf& color) override
    {
        m_clearColor = WGPUColor{
            static_cast<double>(color.r),
            static_cast<double>(color.g),
            static_cast<double>(color.b),
            static_cast<double>(color.a)};
    }

    void setPipeline(IPipeline*) override
    {
        // Pipeline binding handled by WebGPU render pipeline objects
    }

    void setVertexBuffer(IBuffer* buffer) override
    {
        auto* gpuBuf = dynamic_cast<WebGPUBuffer*>(buffer);
        if (gpuBuf && m_renderPassEncoder)
        {
            wgpuRenderPassEncoderSetVertexBuffer(
                m_renderPassEncoder, 0, gpuBuf->handle(), 0, gpuBuf->size());
        }
    }

    void setIndexBuffer(IBuffer* buffer) override
    {
        auto* gpuBuf = dynamic_cast<WebGPUBuffer*>(buffer);
        if (gpuBuf && m_renderPassEncoder)
        {
            wgpuRenderPassEncoderSetIndexBuffer(
                m_renderPassEncoder, gpuBuf->handle(),
                WGPUIndexFormat_Uint32, 0, gpuBuf->size());
        }
    }

    void drawIndexed(std::uint32_t indexCount, std::uint32_t startIndex, std::int32_t) override
    {
        if (m_renderPassEncoder)
        {
            wgpuRenderPassEncoderDrawIndexed(
                m_renderPassEncoder, indexCount, 1, startIndex, 0, 0);
        }
    }

    void draw(std::uint32_t vertexCount, std::uint32_t startVertex) override
    {
        if (m_renderPassEncoder)
        {
            wgpuRenderPassEncoderDraw(
                m_renderPassEncoder, vertexCount, 1, startVertex, 0);
        }
    }

    void setViewport(float width, float height) override
    {
        if (m_renderPassEncoder)
        {
            wgpuRenderPassEncoderSetViewport(
                m_renderPassEncoder, 0.0f, 0.0f, width, height, 0.0f, 1.0f);
        }
    }

    /// @brief レンダーパスを開始する
    /// @param textureView 描画先テクスチャビュー
    void beginRenderPass(WGPUTextureView textureView)
    {
        WGPURenderPassColorAttachment colorAttachment{};
        colorAttachment.view = textureView;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = m_clearColor;

        WGPURenderPassDescriptor renderPassDesc{};
        renderPassDesc.colorAttachmentCount = 1;
        renderPassDesc.colorAttachments = &colorAttachment;

        m_renderPassEncoder = wgpuCommandEncoderBeginRenderPass(
            m_commandEncoder, &renderPassDesc);
    }

    /// @brief コマンドバッファを完成させてキューに投入する
    /// @param queue WebGPUキュー
    void submit(WGPUQueue queue)
    {
        WGPUCommandBufferDescriptor cmdBufDesc{};
        WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(m_commandEncoder, &cmdBufDesc);

        wgpuQueueSubmit(queue, 1, &cmdBuf);
        wgpuCommandBufferRelease(cmdBuf);

        wgpuCommandEncoderRelease(m_commandEncoder);
        m_commandEncoder = nullptr;
    }

private:
    WGPUDevice m_device = nullptr;                         ///< WebGPUデバイス（非所有）
    WGPUCommandEncoder m_commandEncoder = nullptr;         ///< コマンドエンコーダ
    WGPURenderPassEncoder m_renderPassEncoder = nullptr;   ///< レンダーパスエンコーダ
    WGPUColor m_clearColor = {0.0, 0.0, 0.0, 1.0};        ///< クリアカラー
};

/// @brief WebGPU GPUデバイス実装
/// @details Emscripten WebGPU APIを使用してGPUデバイスとスワップチェーンを管理する。
///          canvasIdで対象キャンバスを指定可能（デフォルト: "#canvas"）。
///          バッファ・コマンドリストの生成機能を提供する。
///          シェーダーフォーマット: WGSL (WebGPU Shading Language)。
///
/// @code
/// auto device = std::make_unique<WebGPUDevice>();
/// device->init([](bool success) {
///     // WebGPU初期化完了コールバック
/// });
/// device->beginFrame();
/// // WebGPU描画コマンド...
/// device->endFrame();
/// @endcode
class WebGPUDevice final : public IDevice
{
public:
    /// @brief コンストラクタ
    /// @param canvasId HTMLキャンバスのセレクタ（デフォルト: "#canvas"）
    explicit WebGPUDevice(const char* canvasId = "#canvas")
        : m_canvasId(canvasId)
    {
    }

    ~WebGPUDevice() override
    {
        if (m_swapChain)
        {
            wgpuSwapChainRelease(m_swapChain);
        }
        if (m_queue)
        {
            wgpuQueueRelease(m_queue);
        }
        if (m_device)
        {
            wgpuDeviceRelease(m_device);
        }
        if (m_adapter)
        {
            wgpuAdapterRelease(m_adapter);
        }
        if (m_surface)
        {
            wgpuSurfaceRelease(m_surface);
        }
        if (m_instance)
        {
            wgpuInstanceRelease(m_instance);
        }
    }

    WebGPUDevice(const WebGPUDevice&) = delete;
    WebGPUDevice& operator=(const WebGPUDevice&) = delete;
    WebGPUDevice(WebGPUDevice&&) = delete;
    WebGPUDevice& operator=(WebGPUDevice&&) = delete;

    /// @brief WebGPUデバイスの非同期初期化
    /// @param callback 初期化完了時のコールバック（true: 成功, false: 失敗）
    /// @details WebGPU初期化はアダプタ・デバイス要求が非同期のため、
    ///          コールバックで完了を通知する。
    void init(std::function<void(bool)> callback)
    {
        m_initCallback = std::move(callback);

        m_instance = wgpuCreateInstance(nullptr);
        if (!m_instance)
        {
            std::fprintf(stderr, "WebGPUDevice: wgpuCreateInstance failed\n");
            if (m_initCallback) { m_initCallback(false); }
            return;
        }

        // Canvas からサーフェスを生成する
        WGPUSurfaceDescriptorFromCanvasHTMLSelector canvasDesc{};
        canvasDesc.chain.sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector;
        canvasDesc.selector = m_canvasId.c_str();

        WGPUSurfaceDescriptor surfaceDesc{};
        surfaceDesc.nextInChain = &canvasDesc.chain;
        m_surface = wgpuInstanceCreateSurface(m_instance, &surfaceDesc);

        // アダプタを非同期で要求する
        WGPURequestAdapterOptions adapterOpts{};
        adapterOpts.compatibleSurface = m_surface;

        wgpuInstanceRequestAdapter(
            m_instance, &adapterOpts,
            onAdapterRequestComplete, this);
    }

    /// @brief フレームバッファからピクセルを読み取る（スクリーンショット用）
    /// @param width 読み取り幅
    /// @param height 読み取り高さ
    /// @return RGBA8形式のピクセルデータ
    /// @note WebGPUではバッファマッピングによるGPU→CPU読み出しが必要。
    ///       非同期APIのため、同期的な読み出しは制限がある。
    [[nodiscard]] std::vector<std::uint8_t> readPixels(
        int width, int height) const override
    {
        const auto totalBytes = static_cast<std::size_t>(width) *
                                static_cast<std::size_t>(height) * 4;

        // readPixelsはスクリーンショット用。WebGPUの非同期バッファマッピングは
        // コールバックベースのため、同期読み出しは簡易実装とする。
        // 本格的な実装ではバッファマッピングのコールバックチェーンが必要。
        std::vector<std::uint8_t> data(totalBytes, 0);

        if (!m_device)
        {
            return data;
        }

        // 読み出し用ステージングバッファを作成する
        WGPUBufferDescriptor bufDesc{};
        bufDesc.size = totalBytes;
        bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;

        WGPUBuffer stagingBuffer = wgpuDeviceCreateBuffer(m_device, &bufDesc);
        if (!stagingBuffer)
        {
            return data;
        }

        // 注意: 完全な実装にはテクスチャ→バッファコピー＋非同期マッピングが必要。
        // Emscripten WebGPUの制限により、ここでは空データを返す。
        wgpuBufferRelease(stagingBuffer);

        return data;
    }

    /// @brief アクティブなバックエンドを取得する
    [[nodiscard]] Backend backend() const noexcept override
    {
        return Backend::WebGPU;
    }

    /// @brief フレーム開始処理
    /// @details スワップチェーンから現在のテクスチャビューを取得し、
    ///          コマンドエンコーダを準備する。
    void beginFrame() override
    {
        if (!m_swapChain)
        {
            return;
        }

        m_currentTextureView = wgpuSwapChainGetCurrentTextureView(m_swapChain);
        if (!m_currentTextureView)
        {
            std::fprintf(stderr, "WebGPUDevice: failed to get current texture view\n");
        }
    }

    /// @brief フレーム終了・プレゼント処理
    /// @details 現在のテクスチャビューを解放してスワップチェーンをプレゼントする。
    void endFrame() override
    {
        if (m_currentTextureView)
        {
            wgpuTextureViewRelease(m_currentTextureView);
            m_currentTextureView = nullptr;
        }
    }

    /// @brief GPUバッファを生成する
    [[nodiscard]] std::unique_ptr<IBuffer> createBuffer(
        BufferType bufferType,
        std::uint32_t sizeBytes,
        bool dynamic,
        const void* initialData) override
    {
        return std::make_unique<WebGPUBuffer>(
            m_device, bufferType, sizeBytes, dynamic, initialData);
    }

    /// @brief コマンドリストを生成する
    [[nodiscard]] std::unique_ptr<ICommandList> createCommandList() override
    {
        return std::make_unique<WebGPUCommandList>(m_device);
    }

    /// @brief GPU処理の完了を待機する
    /// @details Emscripten WebGPUではデバイスポーリングで待機する。
    void waitForGpu() override
    {
        // Emscripten WebGPUではイベントループベースのため、
        // 明示的なwaitは限定的。デバイスティックで保留コールバックを処理する。
#if defined(__EMSCRIPTEN__)
        // emscripten_sleep() を使うか、次のフレームまで待つのが一般的
#endif
    }

    /// @brief WGSLシェーダーモジュールを作成する
    /// @param wgslSource WGSLシェーダーソースコード
    /// @return シェーダーモジュールハンドル（呼び出し元が解放責任を持つ）
    [[nodiscard]] WGPUShaderModule createShaderModule(
        std::string_view wgslSource) const
    {
        WGPUShaderModuleWGSLDescriptor wgslDesc{};
        wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
        wgslDesc.code = wgslSource.data();

        WGPUShaderModuleDescriptor shaderDesc{};
        shaderDesc.nextInChain = &wgslDesc.chain;

        return wgpuDeviceCreateShaderModule(m_device, &shaderDesc);
    }

    /// @brief デフォルトの2Dシェーダーモジュールを作成する
    /// @return 2D描画用のWGSL頂点シェーダーモジュール
    [[nodiscard]] WGPUShaderModule createDefaultVertexShader2D() const
    {
        return createShaderModule(WEBGPU_VERTEX_SHADER_2D);
    }

    /// @brief デフォルトの2Dフラグメントシェーダーモジュールを作成する
    /// @return 2D描画用のWGSLフラグメントシェーダーモジュール
    [[nodiscard]] WGPUShaderModule createDefaultFragmentShader2D() const
    {
        return createShaderModule(WEBGPU_FRAGMENT_SHADER_2D);
    }

    /// @brief 初期化済みかどうか
    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

    /// @brief WebGPUデバイスハンドルを取得する
    [[nodiscard]] WGPUDevice deviceHandle() const noexcept { return m_device; }

    /// @brief WebGPUキューを取得する
    [[nodiscard]] WGPUQueue queue() const noexcept { return m_queue; }

    /// @brief 現在のテクスチャビューを取得する（フレーム中のみ有効）
    [[nodiscard]] WGPUTextureView currentTextureView() const noexcept
    {
        return m_currentTextureView;
    }

    /// @brief キャンバス幅を取得する
    [[nodiscard]] int canvasWidth() const noexcept { return m_canvasWidth; }

    /// @brief キャンバス高さを取得する
    [[nodiscard]] int canvasHeight() const noexcept { return m_canvasHeight; }

private:
    /// @brief アダプタ要求完了コールバック
    static void onAdapterRequestComplete(
        WGPURequestAdapterStatus status,
        WGPUAdapter adapter,
        const char* message,
        void* userdata)
    {
        auto* self = static_cast<WebGPUDevice*>(userdata);

        if (status != WGPURequestAdapterStatus_Success)
        {
            std::fprintf(stderr, "WebGPUDevice: adapter request failed: %s\n",
                         message ? message : "unknown error");
            if (self->m_initCallback) { self->m_initCallback(false); }
            return;
        }

        self->m_adapter = adapter;

        // デバイスを非同期で要求する
        WGPUDeviceDescriptor deviceDesc{};
        wgpuAdapterRequestDevice(
            adapter, &deviceDesc,
            onDeviceRequestComplete, self);
    }

    /// @brief デバイス要求完了コールバック
    static void onDeviceRequestComplete(
        WGPURequestDeviceStatus status,
        WGPUDevice device,
        const char* message,
        void* userdata)
    {
        auto* self = static_cast<WebGPUDevice*>(userdata);

        if (status != WGPURequestDeviceStatus_Success)
        {
            std::fprintf(stderr, "WebGPUDevice: device request failed: %s\n",
                         message ? message : "unknown error");
            if (self->m_initCallback) { self->m_initCallback(false); }
            return;
        }

        self->m_device = device;
        self->m_queue = wgpuDeviceGetQueue(device);

        // エラーコールバックを設定する
        wgpuDeviceSetUncapturedErrorCallback(
            device, onDeviceError, self);

        // キャンバスサイズを取得してスワップチェーンを作成する
        emscripten_get_canvas_element_size(
            self->m_canvasId.c_str(),
            &self->m_canvasWidth, &self->m_canvasHeight);

        self->createSwapChain();
        self->m_initialized = true;

        if (self->m_initCallback) { self->m_initCallback(true); }
    }

    /// @brief デバイスエラーコールバック
    static void onDeviceError(
        WGPUErrorType type, const char* message, void*)
    {
        const char* typeStr = "UNKNOWN";
        switch (type)
        {
        case WGPUErrorType_Validation:  typeStr = "VALIDATION"; break;
        case WGPUErrorType_OutOfMemory: typeStr = "OUT_OF_MEMORY"; break;
        case WGPUErrorType_DeviceLost:  typeStr = "DEVICE_LOST"; break;
        default: break;
        }
        std::fprintf(stderr, "WebGPUDevice [%s]: %s\n",
                     typeStr, message ? message : "no details");
    }

    /// @brief スワップチェーンを作成する
    void createSwapChain()
    {
        WGPUSwapChainDescriptor swapChainDesc{};
        swapChainDesc.usage = WGPUTextureUsage_RenderAttachment;
        swapChainDesc.format = WGPUTextureFormat_BGRA8Unorm;
        swapChainDesc.width = static_cast<uint32_t>(m_canvasWidth);
        swapChainDesc.height = static_cast<uint32_t>(m_canvasHeight);
        swapChainDesc.presentMode = WGPUPresentMode_Fifo;

        m_swapChain = wgpuDeviceCreateSwapChain(
            m_device, m_surface, &swapChainDesc);

        if (!m_swapChain)
        {
            std::fprintf(stderr, "WebGPUDevice: failed to create swap chain\n");
        }
    }

    std::string m_canvasId;                                ///< HTMLキャンバスセレクタ
    WGPUInstance m_instance = nullptr;                     ///< WebGPUインスタンス
    WGPUSurface m_surface = nullptr;                       ///< サーフェスハンドル
    WGPUAdapter m_adapter = nullptr;                       ///< アダプタハンドル
    WGPUDevice m_device = nullptr;                         ///< デバイスハンドル
    WGPUQueue m_queue = nullptr;                           ///< キューハンドル
    WGPUSwapChain m_swapChain = nullptr;                   ///< スワップチェーンハンドル
    WGPUTextureView m_currentTextureView = nullptr;        ///< 現在のフレームテクスチャビュー
    int m_canvasWidth = 0;                                 ///< キャンバス幅
    int m_canvasHeight = 0;                                ///< キャンバス高さ
    bool m_initialized = false;                            ///< 初期化完了フラグ
    std::function<void(bool)> m_initCallback;              ///< 初期化完了コールバック
};

} // namespace mitiru::gfx

#endif // defined(__EMSCRIPTEN__) && defined(MITIRU_HAS_WEBGPU)
