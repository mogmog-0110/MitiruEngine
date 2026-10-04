#pragma once

/// @file WebGPUDevice.hpp
/// @brief WebGPU バックエンド実装
/// @details Emscripten の WebGPU 環境向けの IDevice 実装。
///          webgpu/webgpu.h を使い、WGSL シェーダーで描画する。
///          __EMSCRIPTEN__ と MITIRU_HAS_WEBGPU の両方が定義されている場合だけコンパイルされる。
///          凍結中 (ADR 0047): 新機能は足さない。Web の本線は WebGL2 (gfx/webgl/)。

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

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <webgpu/webgpu.h>

#include <sgc/types/Color.hpp>

#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/ICommandList.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/gfx/IPipeline.hpp>
#include <mitiru/gfx/IRenderTarget.hpp>
#include <mitiru/gfx/webgpu/WebGPUBuffer.hpp>

namespace mitiru::gfx
{

// ── 2D スプライト描画用のデフォルト WGSL シェーダー ──────────────────────

/// @brief 2D 頂点シェーダー（WGSL）
/// @details 正射影変換をかけ、頂点色とテクスチャ座標をフラグメントシェーダーに渡す。
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

/// @brief 2D フラグメントシェーダー（WGSL）
/// @details 頂点色をそのまま出力する。テクスチャを使うときは useTexture で切り替えられる。
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

/// @brief WebGPU 用コマンドリスト実装
/// @details WebGPU のコマンドエンコーダでコマンドを記録する。
///          begin() でエンコーダを作り、end() でコマンドバッファを完成させる。
class WebGPUCommandList final : public ICommandList
{
public:
    /// @brief コンストラクタ
    /// @param device WebGPU デバイスハンドル
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
        // WebGPU のレンダーターゲットは render pass descriptor 経由で扱う
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
        // パイプラインのバインドは WebGPU の render pipeline オブジェクトが扱う
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
    /// @param queue WebGPU キュー
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

/// @brief WebGPU の GPU デバイス実装
/// @details Emscripten の WebGPU API で GPU デバイスとスワップチェーンを管理する。
/// canvasId で対象のキャンバスを指定できる（デフォルト: "#canvas"）。
/// バッファとコマンドリストを生成できる。
/// シェーダーフォーマット: WGSL (WebGPU Shading Language)。
/// @code
/// auto device = std::make_unique<WebGPUDevice>();
/// device->init([](bool success) {
/// // WebGPU 初期化完了コールバック
/// });
/// device->beginFrame();
/// // WebGPU 描画コマンド...
/// device->endFrame();
/// @endcode
class WebGPUDevice final : public IDevice
{
public:
    /// @brief コンストラクタ
    /// @param canvasId HTML キャンバスのセレクタ（デフォルト: "#canvas"）
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

    /// @brief WebGPU デバイスを非同期で初期化する
    /// @param callback 初期化が終わったときのコールバック（true: 成功, false: 失敗）
    /// @details WebGPU の初期化ではアダプタとデバイスの要求が非同期なので、
    ///          完了はコールバックで知らせる。
    void init(std::function<void(bool)> callback)
    {
        m_initCallback = std::move(callback);

        m_instance = wgpuCreateInstance(nullptr);
        if (!m_instance)
        {
            console::notice("WebGPU の初期化に失敗しました。WebGPU に対応したブラウザーで開いてください。");
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
    /// @return RGBA8 形式のピクセルデータ。撮れなかった場合は 0 埋め (呼び出し側は
    ///         「本物の黒画面」と区別できないが、フレーム外での呼び出しは呼び出し側の
    ///         誤用なので許容する)
    /// @note beginFrame() 〜 endFrame() の間 (m_currentTexture が生きている間) しか
    ///       撮れない。endFrame() は current texture を release するため、その外側で
    ///       呼ぶと未対応 (0 埋め) になる。
    [[nodiscard]] std::vector<std::uint8_t> readPixels(
        int width, int height) const override
    {
        const auto totalBytes = static_cast<std::size_t>(width) *
                                static_cast<std::size_t>(height) * 4;
        std::vector<std::uint8_t> data(totalBytes, 0);

        if (!m_device || !m_queue || !m_currentTexture || width <= 0 || height <= 0)
        {
            return data;
        }

        // copyTextureToBuffer の bytesPerRow は 256 の倍数でなければならない
        // (WebGPU 仕様の制約)。実データ幅とは別に padding 込みの行幅を確保する。
        constexpr std::uint32_t kRowAlign = 256;
        const auto unpaddedBytesPerRow = static_cast<std::uint32_t>(width) * 4;
        const auto paddedBytesPerRow =
            ((unpaddedBytesPerRow + kRowAlign - 1) / kRowAlign) * kRowAlign;
        const auto stagingSize =
            static_cast<std::uint64_t>(paddedBytesPerRow) * static_cast<std::uint64_t>(height);

        WGPUBufferDescriptor bufDesc{};
        bufDesc.size = stagingSize;
        bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        WGPUBuffer stagingBuffer = wgpuDeviceCreateBuffer(m_device, &bufDesc);
        if (!stagingBuffer)
        {
            return data;
        }

        WGPUCommandEncoderDescriptor encDesc{};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_device, &encDesc);

        WGPUImageCopyTexture src{};
        src.texture = m_currentTexture;
        src.mipLevel = 0;
        src.origin = WGPUOrigin3D{0, 0, 0};

        WGPUImageCopyBuffer dst{};
        dst.buffer = stagingBuffer;
        dst.layout.offset = 0;
        dst.layout.bytesPerRow = paddedBytesPerRow;
        dst.layout.rowsPerImage = static_cast<std::uint32_t>(height);

        WGPUExtent3D extent{
            static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);

        WGPUCommandBufferDescriptor cmdDesc{};
        WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(encoder, &cmdDesc);
        wgpuCommandEncoderRelease(encoder);
        wgpuQueueSubmit(m_queue, 1, &cmdBuf);
        wgpuCommandBufferRelease(cmdBuf);

        struct MapCtx { bool done = false; bool ok = false; };
        MapCtx ctx;
        wgpuBufferMapAsync(
            stagingBuffer, WGPUMapMode_Read, 0, stagingSize,
            [](WGPUBufferMapAsyncStatus status, void* userdata)
            {
                auto* c = static_cast<MapCtx*>(userdata);
                c->done = true;
                c->ok = (status == WGPUBufferMapAsyncStatus_Success);
            },
            &ctx);

        // ブラウザの WebGPU コールバックは JS のイベントループ経由でしか呼ばれない。
        // ASYNCIFY ビルドを前提に、emscripten_sleep で yield して完了を待つ
        // (待たないと map の完了前に関数を抜け、常に 0 埋めを返すことになる)。
        constexpr int kMaxWaitIters = 1000;
        for (int i = 0; i < kMaxWaitIters && !ctx.done; ++i)
        {
            emscripten_sleep(1);
        }

        if (ctx.done && ctx.ok)
        {
            const void* mapped = wgpuBufferGetConstMappedRange(stagingBuffer, 0, stagingSize);
            if (mapped)
            {
                const auto* src8 = static_cast<const std::uint8_t*>(mapped);
                for (int y = 0; y < height; ++y)
                {
                    std::memcpy(
                        data.data() + static_cast<std::size_t>(y) * unpaddedBytesPerRow,
                        src8 + static_cast<std::size_t>(y) * paddedBytesPerRow,
                        unpaddedBytesPerRow);
                }
            }
            wgpuBufferUnmap(stagingBuffer);
        }

        wgpuBufferRelease(stagingBuffer);
        return data;
    }

    /// @brief アクティブなバックエンドを取得する
    [[nodiscard]] Backend backend() const noexcept override
    {
        return Backend::WebGPU;
    }

    /// @brief フレームを開始する
    /// @details スワップチェーンから現在のテクスチャビューを取得し、
    ///          コマンドエンコーダを準備する。
    void beginFrame() override
    {
        if (!m_swapChain)
        {
            return;
        }

        // readPixels() がテクスチャからバッファへのコピーに使えるよう、view だけでなく
        // 元のテクスチャも保持する (WGPUTextureView からは逆引きできない)。
        m_currentTexture = wgpuSwapChainGetCurrentTexture(m_swapChain);
        m_currentTextureView = wgpuSwapChainGetCurrentTextureView(m_swapChain);
        if (!m_currentTextureView)
        {
            console::notice("WebGPU で描画先の取得に失敗しました。ページを読み込み直してください。");
        }
    }

    /// @brief フレームを終了してプレゼントする
    /// @details 現在のテクスチャビューを解放してスワップチェーンをプレゼントする。
    void endFrame() override
    {
        if (m_currentTextureView)
        {
            wgpuTextureViewRelease(m_currentTextureView);
            m_currentTextureView = nullptr;
        }
        if (m_currentTexture)
        {
            wgpuTextureRelease(m_currentTexture);
            m_currentTexture = nullptr;
        }
    }

    /// @brief GPU バッファを生成する
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

    /// @brief GPU 処理の完了を待つ
    /// @details Emscripten の WebGPU ではデバイスをポーリングして待つ。
    void waitForGpu() override
    {
        // Emscripten の WebGPU はイベントループで動くため、
        // 明示的な wait でできることは限られる。デバイスの tick で保留中のコールバックを処理する。
#if defined(__EMSCRIPTEN__)
        // emscripten_sleep() を使うか、次のフレームまで待つのが一般的
#endif
    }

    /// @brief WGSL シェーダーモジュールを作成する
    /// @param wgslSource WGSL シェーダーのソースコード
    /// @return シェーダーモジュールハンドル（解放は呼び出し元が行う）
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

    /// @brief デフォルトの 2D シェーダーモジュールを作成する
    /// @return 2D 描画用の WGSL 頂点シェーダーモジュール
    [[nodiscard]] WGPUShaderModule createDefaultVertexShader2D() const
    {
        return createShaderModule(WEBGPU_VERTEX_SHADER_2D);
    }

    /// @brief デフォルトの 2D フラグメントシェーダーモジュールを作成する
    /// @return 2D 描画用の WGSL フラグメントシェーダーモジュール
    [[nodiscard]] WGPUShaderModule createDefaultFragmentShader2D() const
    {
        return createShaderModule(WEBGPU_FRAGMENT_SHADER_2D);
    }

    /// @brief 初期化済みかどうか
    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

    /// @brief WebGPU デバイスハンドルを取得する
    [[nodiscard]] WGPUDevice deviceHandle() const noexcept { return m_device; }

    /// @brief WebGPU キューを取得する
    [[nodiscard]] WGPUQueue queue() const noexcept { return m_queue; }

    /// @brief 現在のテクスチャビューを取得する（フレーム中だけ有効）
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
            console::noticef("WebGPU で GPU の取得に失敗しました (%s)。WebGPU に対応したブラウザーで開いてください。",
                             message ? message : "理由は不明");
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
            console::noticef("WebGPU のデバイス作成に失敗しました (%s)。WebGPU に対応したブラウザーで開いてください。",
                             message ? message : "理由は不明");
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
        console::noticef("WebGPU で %s のエラーが出ました (%s)。ページを読み込み直してください。",
                         typeStr, message ? message : "詳細なし");
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
            console::notice("WebGPU で描画先の作成に失敗しました。ページを読み込み直してください。");
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
