#pragma once

/// @file ParticleCollision.hpp
/// @brief GPUパーティクルコリジョン（深度バッファベース）
/// @details 深度バッファを利用してGPUパーティクルの衝突判定を行う。
///          パーティクルのワールド座標を深度バッファに投影し、
///          深度値との比較で衝突を検出・応答する。

#include <array>
#include <cstdint>
#include <vector>

#ifdef MITIRU_HAS_DX11

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <string_view>
#include <wrl/client.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

#endif // MITIRU_HAS_DX11

namespace mitiru::render
{

/// @brief パーティクルコリジョン設定
struct ParticleCollisionConfig
{
    float depthThreshold = 0.01f;     ///< 深度一致判定閾値
    float restitution = 0.3f;         ///< 反発係数（0.0-1.0）
    float friction = 0.5f;            ///< 摩擦係数（0.0-1.0）
    float stickyThreshold = 0.05f;    ///< 張り付き判定速度閾値
    bool enableCollision = true;      ///< 衝突判定の有効フラグ
    bool enableSticky = false;        ///< 張り付き挙動の有効フラグ
    int maxCollisionsPerFrame = 1024; ///< フレームあたり最大衝突処理数
};

/// @brief コリジョンイベント情報
struct ParticleCollisionEvent
{
    std::uint32_t particleIndex = 0;             ///< 衝突したパーティクルのインデックス
    std::array<float, 3> contactPoint = {};      ///< 衝突点（ワールド座標）
    std::array<float, 3> contactNormal = {};     ///< 衝突面法線（推定）
    float penetrationDepth = 0.0f;               ///< めり込み深さ
};

/// @brief パーティクルコリジョンの統計情報
struct ParticleCollisionStats
{
    int totalParticles = 0;        ///< 処理対象パーティクル総数
    int collisionsDetected = 0;    ///< 検出された衝突数
    int collisionsResolved = 0;    ///< 解決された衝突数
    float resolveTimeMs = 0.0f;   ///< 衝突解決処理時間（ミリ秒）
};

/// @brief コリジョン解決用パーティクルデータ（16バイト整列）
struct CollisionParticle
{
    float position[3]; ///< ワールド座標
    float pad0;        ///< パディング（16バイト境界）
    float velocity[3]; ///< 速度
    float life;        ///< 残存ライフ（0以下で無効）
};

#ifdef MITIRU_HAS_DX11

/// @brief GPU 側コリジョンイベント出力バッファ（AppendStructuredBuffer用）
struct CollisionEvent_GPU
{
    std::uint32_t particleIndex;
    float         contactX;
    float         contactY;
    float         contactZ;
    float         penetrationDepth;
    float         pad[3]; ///< 32バイト整列
};

// ── HLSL compute shader ──────────────────────────────────────────────────────

/// @brief パーティクルコリジョン解決コンピュートシェーダー（CS 5.0）
/// @details CPU側で viewProj を transposed() してアップロードし、
///          HLSL内で mul(viewProj, vec) を呼ぶことで sgc * vec を実現する。
///          （mul(vec, mat) パターンは sgc^T * vec になるため禁止）
static constexpr std::string_view PARTICLE_COLLISION_CS_HLSL = R"(
struct Particle
{
    float3 position;
    float  pad0;
    float3 velocity;
    float  life;
};

struct CollisionEvent
{
    uint  particleIndex;
    float contactX;
    float contactY;
    float contactZ;
    float penetrationDepth;
    float pad0;
    float pad1;
    float pad2;
};

cbuffer CollisionParams : register(b0)
{
    float4x4 viewProj;       // CPU側で transposed() してアップロード済み
    float    depthThreshold;
    float    restitution;
    float    friction;
    float    deltaTime;
    int      depthWidth;
    int      depthHeight;
    int      maxParticles;
    int      _pad;
};

RWStructuredBuffer<Particle>       particles   : register(u0);
AppendStructuredBuffer<CollisionEvent> events  : register(u1);
Texture2D<float>                   depthBuffer : register(t0);

[numthreads(64, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    uint id = dtid.x;
    if (id >= (uint)maxParticles) { return; }

    Particle p = particles[id];
    if (p.life <= 0.0) { return; }

    // mul(matrix, column_vec): CPU側 transposed() + HLSL左matrix → sgc * vec
    float4 clip = mul(viewProj, float4(p.position, 1.0));
    if (clip.w <= 0.0) { return; }

    float3 ndc = clip.xyz / clip.w;
    if (any(abs(ndc.xy) > 1.0)) { return; }
    if (ndc.z < 0.0 || ndc.z > 1.0) { return; }

    float2 uv = ndc.xy * float2(0.5, -0.5) + 0.5;
    int2 px = int2(uv * float2((float)depthWidth, (float)depthHeight));
    px = clamp(px, int2(0,0), int2(depthWidth-1, depthHeight-1));
    float scene_z = depthBuffer.Load(int3(px, 0));

    float diff = ndc.z - scene_z;
    if (diff > depthThreshold)
    {
        // 反射応答（Y軸法線を仮定）
        float3 normal = float3(0.0, 1.0, 0.0);
        float vDotN = dot(p.velocity, normal);
        if (vDotN < 0.0)
        {
            p.velocity = p.velocity - (1.0 + restitution) * vDotN * normal;
            p.velocity.xz *= (1.0 - friction);
        }
        particles[id] = p;

        CollisionEvent ev;
        ev.particleIndex   = id;
        ev.contactX        = p.position.x;
        ev.contactY        = p.position.y;
        ev.contactZ        = p.position.z;
        ev.penetrationDepth = diff;
        ev.pad0 = 0.0; ev.pad1 = 0.0; ev.pad2 = 0.0;
        events.Append(ev);
    }
}
)";

#endif // MITIRU_HAS_DX11

/// @brief GPUパーティクルコリジョンリゾルバ
/// @details 深度バッファを利用したパーティクル衝突検出・応答システム。
///          コンピュートシェーダーでGPU上の衝突判定を行う。
class ParticleCollisionResolver
{
public:
    /// @brief デフォルトコンストラクタ
    ParticleCollisionResolver() = default;

    /// @brief デストラクタ
    ~ParticleCollisionResolver()
    {
#ifdef MITIRU_HAS_DX11
        shutdown();
#endif
    }

    /// コピー禁止
    ParticleCollisionResolver(const ParticleCollisionResolver&) = delete;
    ParticleCollisionResolver& operator=(const ParticleCollisionResolver&) = delete;

    /// ムーブ許可
    ParticleCollisionResolver(ParticleCollisionResolver&&) noexcept = default;
    ParticleCollisionResolver& operator=(ParticleCollisionResolver&&) noexcept = default;

    /// @brief コリジョンシステムを初期化する（デバイスなし・no-op fallback）
    /// @param maxParticles 最大パーティクル数
    /// @param config コリジョン設定
    /// @return 初期化成功でtrue
    bool init(int maxParticles, const ParticleCollisionConfig& config = {})
    {
        if (maxParticles <= 0)
        {
            return false;
        }
        m_maxParticles = maxParticles;
        m_config = config;
        return true;
    }

    /// @brief 深度バッファとビュー・プロジェクション行列を設定する
    /// @param viewMatrix ビュー行列（float[16]、列優先）
    /// @param projMatrix プロジェクション行列（float[16]、列優先）
    void setCamera(const float* viewMatrix, const float* projMatrix)
    {
        if (viewMatrix)
        {
            for (int i = 0; i < 16; ++i)
            {
                m_viewMatrix[static_cast<std::size_t>(i)] = viewMatrix[i];
            }
        }
        if (projMatrix)
        {
            for (int i = 0; i < 16; ++i)
            {
                m_projMatrix[static_cast<std::size_t>(i)] = projMatrix[i];
            }
        }
    }

    /// @brief 衝突判定と応答を実行する（CPU fallback: 常に空を返す）
    /// @param deltaTime フレーム間隔（秒）
    /// @return 検出された衝突イベント一覧
    [[nodiscard]] std::vector<ParticleCollisionEvent> resolve(float deltaTime)
    {
        if (!m_config.enableCollision)
        {
            return {};
        }
        static_cast<void>(deltaTime);
#ifdef MITIRU_HAS_DX11
        if (m_dx11Valid)
        {
            return resolveDx11(deltaTime);
        }
#endif
        return {};
    }

    /// @brief 統計情報を取得する
    [[nodiscard]] ParticleCollisionStats stats() const noexcept
    {
        return m_stats;
    }

    /// @brief 設定を取得する
    [[nodiscard]] ParticleCollisionConfig& config() noexcept { return m_config; }

    /// @brief 設定を取得する（const版）
    [[nodiscard]] const ParticleCollisionConfig& config() const noexcept { return m_config; }

#ifdef MITIRU_HAS_DX11

    /// @brief DX11デバイスでコリジョンシステムを初期化する
    /// @param device D3D11デバイス
    /// @param maxParticles 最大パーティクル数
    /// @param cfg コリジョン設定
    /// @return 初期化成功でtrue
    bool init(ID3D11Device* device, int maxParticles,
              const ParticleCollisionConfig& cfg = {})
    {
        if (!device || maxParticles <= 0)
        {
            return false;
        }
        m_device = device;
        m_maxParticles = maxParticles;
        m_config = cfg;

        if (!compileShader()) { return false; }
        if (!createParticleBuffer()) { return false; }
        if (!createEventBuffer()) { return false; }
        if (!createDepthTexture(256, 256)) { return false; }
        if (!createConstantBuffer()) { return false; }

        m_dx11Valid = true;
        return true;
    }

    /// @brief DX11リソースを解放する
    void shutdown()
    {
        m_computeShader.Reset();
        m_particleBuffer.Reset();
        m_particleUAV.Reset();
        m_eventBuffer.Reset();
        m_eventUAV.Reset();
        m_eventCounterStaging.Reset();
        m_eventReadback.Reset();
        m_depthTexture.Reset();
        m_depthSRV.Reset();
        m_constantBuffer.Reset();
        m_dx11Valid = false;
        m_depthWidth = 0;
        m_depthHeight = 0;
    }

    /// @brief カメラのビュープロジェクション行列を設定する
    /// @param vpRowMajor 行優先4x4行列（sgc::Mat4f の m[0][0] 先頭アドレス）
    /// @note CPU側で transposed() せずに渡すこと。upload時に自動転置する。
    void setCameraVP(const float* vpRowMajor)
    {
        if (vpRowMajor)
        {
            for (int i = 0; i < 16; ++i)
            {
                m_vpMatrix[static_cast<std::size_t>(i)] = vpRowMajor[i];
            }
        }
    }

    /// @brief パーティクルデータをGPUにアップロードする
    /// @param data パーティクル配列
    /// @param count 有効パーティクル数（m_maxParticles以下）
    void uploadParticles(const CollisionParticle* data, int count)
    {
        if (!m_device || !m_particleBuffer || count <= 0) { return; }

        const int uploadCount = count < m_maxParticles ? count : m_maxParticles;

        ID3D11DeviceContext* ctx = nullptr;
        m_device->GetImmediateContext(&ctx);
        if (!ctx) { return; }

        // DEFAULT usage + UAV bind: Map() 不可なので UpdateSubresource を使う。
        // partial update のため D3D11_BOX で先頭 uploadCount 要素のみ書く。
        D3D11_BOX box = {};
        box.left   = 0;
        box.right  = static_cast<UINT>(uploadCount) * sizeof(CollisionParticle);
        box.top    = 0;
        box.bottom = 1;
        box.front  = 0;
        box.back   = 1;
        ctx->UpdateSubresource(m_particleBuffer.Get(), 0, &box, data, 0, 0);
        ctx->Release();
    }

    /// @brief 深度バッファデータをGPUにアップロードする
    /// @param data float深度値配列（width*height要素）
    /// @param width テクスチャ幅
    /// @param height テクスチャ高さ
    void uploadDepth(const float* data, int width, int height)
    {
        if (!m_device || !data || width <= 0 || height <= 0) { return; }

        if (width != m_depthWidth || height != m_depthHeight)
        {
            m_depthTexture.Reset();
            m_depthSRV.Reset();
            if (!createDepthTexture(width, height)) { return; }
        }

        ID3D11DeviceContext* ctx = nullptr;
        m_device->GetImmediateContext(&ctx);
        if (!ctx) { return; }

        const UINT rowPitch = static_cast<UINT>(width) * sizeof(float);
        ctx->UpdateSubresource(m_depthTexture.Get(), 0, nullptr, data,
                               rowPitch, 0);
        ctx->Release();
    }

#endif // MITIRU_HAS_DX11

private:
    ParticleCollisionConfig m_config;
    ParticleCollisionStats  m_stats;
    int                     m_maxParticles = 0;
    std::array<float, 16>   m_viewMatrix   = {};
    std::array<float, 16>   m_projMatrix   = {};

#ifdef MITIRU_HAS_DX11

    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    // cbuffer レイアウト（256バイト整列不要。16バイト倍数であれば可）
    struct alignas(16) CollisionCB
    {
        float viewProjT[16];   // transposed VP (CPU transposed, HLSL左行列)
        float depthThreshold;
        float restitution;
        float friction;
        float deltaTime;
        int   depthWidth;
        int   depthHeight;
        int   maxParticles;
        int   _pad;
    };

    ID3D11Device*             m_device       = nullptr;
    bool                      m_dx11Valid     = false;
    int                       m_depthWidth    = 0;
    int                       m_depthHeight   = 0;
    std::array<float, 16>     m_vpMatrix      = {};

    ComPtr<ID3D11ComputeShader>    m_computeShader;
    ComPtr<ID3D11Buffer>           m_particleBuffer;
    ComPtr<ID3D11Buffer>           m_particleUploadStaging; ///< CPU→GPU upload用
    ComPtr<ID3D11UnorderedAccessView> m_particleUAV;
    ComPtr<ID3D11Buffer>           m_eventBuffer;
    ComPtr<ID3D11UnorderedAccessView> m_eventUAV;
    ComPtr<ID3D11Buffer>           m_eventCounterStaging;
    ComPtr<ID3D11Buffer>           m_eventReadback;
    ComPtr<ID3D11Texture2D>        m_depthTexture;
    ComPtr<ID3D11ShaderResourceView> m_depthSRV;
    ComPtr<ID3D11Buffer>           m_constantBuffer;

    // ── Private DX11 helpers ─────────────────────────────────────────────

    bool compileShader()
    {
        const auto src = PARTICLE_COLLISION_CS_HLSL;
        ID3DBlob* blob  = nullptr;
        ID3DBlob* error = nullptr;
        UINT flags = 0;
#ifdef _DEBUG
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        HRESULT hr = D3DCompile(src.data(), src.size(), nullptr,
                                nullptr, nullptr, "CSMain", "cs_5_0",
                                flags, 0, &blob, &error);
        if (error) { error->Release(); }
        if (FAILED(hr) || !blob) { return false; }

        hr = m_device->CreateComputeShader(
            blob->GetBufferPointer(), blob->GetBufferSize(),
            nullptr, m_computeShader.GetAddressOf());
        blob->Release();
        return SUCCEEDED(hr);
    }

    bool createParticleBuffer()
    {
        const auto byteWidth = static_cast<UINT>(m_maxParticles)
                               * sizeof(CollisionParticle);

        // DX11 制約: UAV 用バッファは DYNAMIC 不可。DEFAULT + UpdateSubresource で扱う。
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth           = byteWidth;
        bd.Usage               = D3D11_USAGE_DEFAULT;
        bd.BindFlags           = D3D11_BIND_UNORDERED_ACCESS
                               | D3D11_BIND_SHADER_RESOURCE;
        bd.CPUAccessFlags      = 0;
        bd.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = sizeof(CollisionParticle);

        HRESULT hr = m_device->CreateBuffer(&bd, nullptr,
                                            m_particleBuffer.GetAddressOf());
        if (FAILED(hr)) { return false; }

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavd = {};
        uavd.Format              = DXGI_FORMAT_UNKNOWN;
        uavd.ViewDimension       = D3D11_UAV_DIMENSION_BUFFER;
        uavd.Buffer.NumElements  = static_cast<UINT>(m_maxParticles);

        hr = m_device->CreateUnorderedAccessView(
            m_particleBuffer.Get(), &uavd, m_particleUAV.GetAddressOf());
        return SUCCEEDED(hr);
    }

    bool createEventBuffer()
    {
        const int maxEvents = m_config.maxCollisionsPerFrame;
        const UINT byteWidth = static_cast<UINT>(maxEvents)
                               * sizeof(CollisionEvent_GPU);

        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth           = byteWidth;
        bd.Usage               = D3D11_USAGE_DEFAULT;
        bd.BindFlags           = D3D11_BIND_UNORDERED_ACCESS;
        bd.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = sizeof(CollisionEvent_GPU);

        HRESULT hr = m_device->CreateBuffer(&bd, nullptr,
                                            m_eventBuffer.GetAddressOf());
        if (FAILED(hr)) { return false; }

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavd = {};
        uavd.Format              = DXGI_FORMAT_UNKNOWN;
        uavd.ViewDimension       = D3D11_UAV_DIMENSION_BUFFER;
        uavd.Buffer.NumElements  = static_cast<UINT>(maxEvents);
        uavd.Buffer.Flags        = D3D11_BUFFER_UAV_FLAG_APPEND;

        hr = m_device->CreateUnorderedAccessView(
            m_eventBuffer.Get(), &uavd, m_eventUAV.GetAddressOf());
        if (FAILED(hr)) { return false; }

        // UAV カウンターを CPU に読み出す用のステージングバッファ（4バイト）
        D3D11_BUFFER_DESC cbd = {};
        cbd.ByteWidth      = sizeof(UINT);
        cbd.Usage          = D3D11_USAGE_STAGING;
        cbd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        hr = m_device->CreateBuffer(&cbd, nullptr,
                                    m_eventCounterStaging.GetAddressOf());
        if (FAILED(hr)) { return false; }

        // イベントデータ読み出し用ステージングバッファ
        D3D11_BUFFER_DESC rbd = {};
        rbd.ByteWidth      = byteWidth;
        rbd.Usage          = D3D11_USAGE_STAGING;
        rbd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        rbd.MiscFlags      = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        rbd.StructureByteStride = sizeof(CollisionEvent_GPU);

        hr = m_device->CreateBuffer(&rbd, nullptr,
                                    m_eventReadback.GetAddressOf());
        return SUCCEEDED(hr);
    }

    bool createDepthTexture(int width, int height)
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width          = static_cast<UINT>(width);
        td.Height         = static_cast<UINT>(height);
        td.MipLevels      = 1;
        td.ArraySize      = 1;
        td.Format         = DXGI_FORMAT_R32_FLOAT;
        td.SampleDesc     = {1, 0};
        td.Usage          = D3D11_USAGE_DEFAULT;
        td.BindFlags      = D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = m_device->CreateTexture2D(&td, nullptr,
                                               m_depthTexture.GetAddressOf());
        if (FAILED(hr)) { return false; }

        hr = m_device->CreateShaderResourceView(
            m_depthTexture.Get(), nullptr, m_depthSRV.GetAddressOf());
        if (FAILED(hr)) { return false; }

        m_depthWidth  = width;
        m_depthHeight = height;
        return true;
    }

    bool createConstantBuffer()
    {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth      = sizeof(CollisionCB);
        bd.Usage          = D3D11_USAGE_DEFAULT;
        bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;

        HRESULT hr = m_device->CreateBuffer(&bd, nullptr,
                                            m_constantBuffer.GetAddressOf());
        return SUCCEEDED(hr);
    }

    /// @brief sgc::Mat4f 行優先配列を HLSL 列優先 cbuffer 用に転置して返す
    static std::array<float, 16> transposeForHlsl(const float* rowMajor)
    {
        std::array<float, 16> out = {};
        for (int r = 0; r < 4; ++r)
        {
            for (int c = 0; c < 4; ++c)
            {
                out[static_cast<std::size_t>(c * 4 + r)] =
                    rowMajor[static_cast<std::size_t>(r * 4 + c)];
            }
        }
        return out;
    }

    [[nodiscard]] std::vector<ParticleCollisionEvent> resolveDx11(float deltaTime)
    {
        ID3D11DeviceContext* ctx = nullptr;
        m_device->GetImmediateContext(&ctx);
        if (!ctx) { return {}; }

        // cbuffer を更新する
        CollisionCB cb = {};
        const auto vpT = transposeForHlsl(m_vpMatrix.data());
        std::memcpy(cb.viewProjT, vpT.data(), sizeof(float) * 16);
        cb.depthThreshold = m_config.depthThreshold;
        cb.restitution    = m_config.restitution;
        cb.friction       = m_config.friction;
        cb.deltaTime      = deltaTime;
        cb.depthWidth     = m_depthWidth;
        cb.depthHeight    = m_depthHeight;
        cb.maxParticles   = m_maxParticles;

        ctx->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);

        // UAV をバインドする。
        // particles UAV: カウンターなし（0xFFFFFFFF = don't reset）
        // events UAV: AppendStructuredBuffer カウンターを 0 にリセット
        UINT initialCounts[] = {0xFFFFFFFF, 0};
        ID3D11UnorderedAccessView* uavs[] = {
            m_particleUAV.Get(), m_eventUAV.Get()
        };
        ctx->CSSetUnorderedAccessViews(0, 2, uavs, initialCounts);

        ID3D11Buffer* cbs[] = {m_constantBuffer.Get()};
        ctx->CSSetConstantBuffers(0, 1, cbs);

        ID3D11ShaderResourceView* srvs[] = {m_depthSRV.Get()};
        ctx->CSSetShaderResources(0, 1, srvs);

        ctx->CSSetShader(m_computeShader.Get(), nullptr, 0);

        const UINT groupCount =
            (static_cast<UINT>(m_maxParticles) + 63) / 64;
        ctx->Dispatch(groupCount, 1, 1);

        // バインド解除
        ID3D11UnorderedAccessView* nullUAVs[] = {nullptr, nullptr};
        UINT nullCount = 0xFFFFFFFF;
        ctx->CSSetUnorderedAccessViews(0, 2, nullUAVs, &nullCount);
        ID3D11ShaderResourceView* nullSRV[] = {nullptr};
        ctx->CSSetShaderResources(0, 1, nullSRV);
        ctx->CSSetShader(nullptr, nullptr, 0);

        // AppendStructuredBuffer の書き込み数を UAV カウンターから取得する
        ctx->CopyStructureCount(m_eventCounterStaging.Get(), 0,
                                m_eventUAV.Get());

        UINT appendedCount = 0;
        {
            D3D11_MAPPED_SUBRESOURCE cntMapped = {};
            if (SUCCEEDED(ctx->Map(m_eventCounterStaging.Get(), 0,
                                   D3D11_MAP_READ, 0, &cntMapped)))
            {
                appendedCount = *static_cast<const UINT*>(cntMapped.pData);
                ctx->Unmap(m_eventCounterStaging.Get(), 0);
            }
        }

        auto events = std::vector<ParticleCollisionEvent>{};

        if (appendedCount == 0) { ctx->Release(); return events; }

        const int maxEvents = m_config.maxCollisionsPerFrame;
        const UINT readCount =
            appendedCount < static_cast<UINT>(maxEvents)
            ? appendedCount
            : static_cast<UINT>(maxEvents);

        // イベントバッファを CPU に読み戻す
        ctx->CopyResource(m_eventReadback.Get(), m_eventBuffer.Get());

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(ctx->Map(m_eventReadback.Get(), 0,
                               D3D11_MAP_READ, 0, &mapped)))
        {
            const auto* src = static_cast<const CollisionEvent_GPU*>(
                mapped.pData);

            events.reserve(readCount);
            for (UINT i = 0; i < readCount; ++i)
            {
                ParticleCollisionEvent ev;
                ev.particleIndex     = src[i].particleIndex;
                ev.contactPoint[0]   = src[i].contactX;
                ev.contactPoint[1]   = src[i].contactY;
                ev.contactPoint[2]   = src[i].contactZ;
                ev.contactNormal[0]  = 0.0f;
                ev.contactNormal[1]  = 1.0f;
                ev.contactNormal[2]  = 0.0f;
                ev.penetrationDepth  = src[i].penetrationDepth;
                events.push_back(ev);
            }
            ctx->Unmap(m_eventReadback.Get(), 0);
        }

        m_stats.totalParticles    = m_maxParticles;
        m_stats.collisionsDetected = static_cast<int>(events.size());
        m_stats.collisionsResolved = static_cast<int>(events.size());

        ctx->Release();
        return events;
    }

#endif // MITIRU_HAS_DX11
};

} // namespace mitiru::render
