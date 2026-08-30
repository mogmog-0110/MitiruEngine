#pragma once
/// @file ShadowPass3D.hpp
/// @brief GPU シャドウマップパス（DX11実装付き）
///
/// @details ライトのビュー射影行列計算と、DX11デプステクスチャ/DSV/SRVの
///          ライフサイクル管理を提供する。デプスオンリーレンダーパスは
///          `beginShadowRender` / `endShadowRender` で制御する。
///
/// @code
/// ShadowPass3D pass;
/// pass.setConfig({1024, 15.0f, 0.1f, 50.0f});
/// if (!pass.init(device)) { /* error */ }
///
/// // 毎フレーム:
/// auto vp = pass.computeLightVP(light);
/// pass.setLightViewProjection(vp);
/// pass.beginShadowRender(ctx);
/// // --- ここでシャドウキャスターを描画 ---
/// pass.endShadowRender(ctx);
///
/// // メインパスで shadow SRV を使用:
/// auto* srv = pass.getShadowSRV();
/// @endcode

#include <mitiru/render/Light.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/Camera3D.hpp>
#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <string>

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
#include <wrl/client.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

#endif // MITIRU_HAS_DX11

namespace mitiru::render {

/// @brief シャドウマップ設定
struct ShadowMapConfig3D {
    int   resolution = 1024;
    float orthoSize  = 15.0f;
    float nearPlane  = 0.1f;
    float farPlane   = 50.0f;
    float bias       = 0.005f;
};

// ── デプスオンリー頂点シェーダー ──────────────────────────────

/// @brief シャドウパス用デプスオンリー頂点シェーダー（HLSL SM5.0）
constexpr const char* SHADOW_DEPTH_VS = R"hlsl(
cbuffer LightVP : register(b0)
{
    float4x4 lightViewProj;
};

struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_POSITION;
};

VSOutput main(VSInput input)
{
    VSOutput o;
    // 行列は転置済みで upload されるため HLSL では mul(mat, col_vec) = sgc * vec
    o.position = mul(lightViewProj, float4(input.position, 1.0));
    return o;
}
)hlsl";

/// @brief GPU シャドウマップ
class ShadowPass3D {
public:
    ShadowPass3D() = default;

    /// dtor。DX11リソースを解放する
    ~ShadowPass3D()
    {
#ifdef MITIRU_HAS_DX11
        shutdown();
#endif
    }

    // コピー禁止（ComPtr所有権の明確化）
    ShadowPass3D(const ShadowPass3D&)            = delete;
    ShadowPass3D& operator=(const ShadowPass3D&) = delete;
    ShadowPass3D(ShadowPass3D&&)                 = default;
    ShadowPass3D& operator=(ShadowPass3D&&)      = default;

    void setConfig(const ShadowMapConfig3D& config) { m_config = config; }
    const ShadowMapConfig3D& config() const noexcept { return m_config; }

    /// @brief ライトのビュー射影行列を計算する (D3D 規約: Z → [0,1])
    /// @details ライト方向が真上下 (worldUp と平行) の退化ケースを避けるため、
    ///          up ベクトルを動的に選ぶ。sgc の orthographic は OpenGL 規約
    ///          (Z → [-1,1]) なので、D3D 用に Z 範囲を再マップする行列を後乗せする。
    [[nodiscard]] sgc::Mat4f computeLightVP(const Light& light) const noexcept
    {
        const sgc::Vec3f lightPos = light.direction * (-20.0f);

        // light.direction と平行な up を回避
        sgc::Vec3f up = {0, 1, 0};
        if (std::abs(light.direction.y) > 0.99f) { up = {0, 0, 1}; }

        const auto view = sgc::Mat4f::lookAt(lightPos, {0, 0, 0}, up);
        const auto projGL = sgc::Mat4f::orthographic(
            -m_config.orthoSize, m_config.orthoSize,
            -m_config.orthoSize, m_config.orthoSize,
            m_config.nearPlane,  m_config.farPlane);

        // OpenGL [-1,1] → D3D [0,1] への Z 再マップ:
        // z_d3d = 0.5 * z_gl + 0.5
        sgc::Mat4f remap = sgc::Mat4f::identity();
        remap.m[2][2] = 0.5f;
        remap.m[2][3] = 0.5f;

        return remap * projGL * view;
    }

    [[nodiscard]] const sgc::Mat4f& lightViewProjection() const noexcept { return m_lightVP; }
    void setLightViewProjection(const sgc::Mat4f& vp) noexcept { m_lightVP = vp; }

    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

#ifdef MITIRU_HAS_DX11

    using ComPtr = Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>;

    /// @brief DX11リソースを初期化する
    /// @param device D3D11デバイス（ShadowPass3Dより長生きする必要がある）
    /// @return 成功時true、二重初期化時はfalse
    [[nodiscard]] bool init(ID3D11Device* device)
    {
        if (m_initialized) { return false; }
        if (!device) { return false; }

        m_device = device;
        if (!createDepthResources())   { return false; }
        if (!createDepthShader())      { shutdown(); return false; }
        if (!createShadowRasterizer()) { shutdown(); return false; }

        m_initialized = true;
        return true;
    }

    /// @brief DX11リソースをすべて解放する
    void shutdown()
    {
        m_depthTexture.Reset();
        m_dsv.Reset();
        m_srv.Reset();
        m_depthVS.Reset();
        m_inputLayout.Reset();
        m_lightVPCB.Reset();
        m_shadowRS.Reset();
        m_device     = nullptr;
        m_initialized = false;
    }

    /// @brief デプスオンリーレンダーパスを開始する
    /// @details 前回のRTV/DSV/ビューポート/ラスタライザをキャッシュし、
    ///          シャドウマップへ切り替える。シャドウパスは CULL_NONE を強制し、
    ///          メッシュ winding に依存せず両面ともデプスを書き込む。
    void beginShadowRender(ID3D11DeviceContext* ctx)
    {
        if (!m_initialized || !ctx) { return; }

        // 現在のRTV/DSV/ビューポート/ラスタライザをキャッシュ
        ctx->OMGetRenderTargets(1,
            m_savedRTV.GetAddressOf(),
            m_savedDSV.GetAddressOf());
        UINT vpCount = 1;
        ctx->RSGetViewports(&vpCount, &m_savedVP);
        ctx->RSGetState(m_savedRS.GetAddressOf());

        // シャドウ用 DSV のみバインド（RTV なし）
        ID3D11RenderTargetView* nullRTV = nullptr;
        ctx->OMSetRenderTargets(1, &nullRTV, m_dsv.Get());

        D3D11_VIEWPORT vp = {};
        vp.Width    = static_cast<float>(m_config.resolution);
        vp.Height   = static_cast<float>(m_config.resolution);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &vp);

        // CULL_NONE: メッシュ winding に依存せず両面とも depth を書き込む
        ctx->RSSetState(m_shadowRS.Get());

        ctx->ClearDepthStencilView(m_dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

        // シェーダーを設定し定数バッファを更新する
        ctx->VSSetShader(m_depthVS.Get(), nullptr, 0);
        ctx->PSSetShader(nullptr, nullptr, 0);
        ctx->IASetInputLayout(m_inputLayout.Get());

        updateLightVPBuffer(ctx);
        ID3D11Buffer* cbs[] = { m_lightVPCB.Get() };
        ctx->VSSetConstantBuffers(0, 1, cbs);
    }

    /// @brief デプスオンリーレンダーパスを終了し、前の状態を復元する
    void endShadowRender(ID3D11DeviceContext* ctx)
    {
        if (!m_initialized || !ctx) { return; }

        // 保存した RTV/DSV/ラスタライザを復元する
        ID3D11RenderTargetView* savedRTVRaw = m_savedRTV.Get();
        ctx->OMSetRenderTargets(1, &savedRTVRaw, m_savedDSV.Get());
        ctx->RSSetViewports(1, &m_savedVP);
        ctx->RSSetState(m_savedRS.Get());

        m_savedRTV.Reset();
        m_savedDSV.Reset();
        m_savedRS.Reset();
    }

    /// @brief シャドウマップ SRV を返す（未初期化時は nullptr）
    [[nodiscard]] ID3D11ShaderResourceView* getShadowSRV() const noexcept
    {
        return m_srv.Get();
    }

private:
    /// @brief デプステクスチャ・DSV・SRVを生成する
    [[nodiscard]] bool createDepthResources()
    {
        const UINT res = static_cast<UINT>(m_config.resolution);

        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width          = res;
        texDesc.Height         = res;
        texDesc.MipLevels      = 1;
        texDesc.ArraySize      = 1;
        texDesc.Format         = DXGI_FORMAT_R32_TYPELESS;
        texDesc.SampleDesc     = {1, 0};
        texDesc.Usage          = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags      = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, m_depthTexture.GetAddressOf());
        if (FAILED(hr)) { return false; }

        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format        = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

        hr = m_device->CreateDepthStencilView(m_depthTexture.Get(), &dsvDesc, m_dsv.GetAddressOf());
        if (FAILED(hr)) { return false; }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = DXGI_FORMAT_R32_FLOAT;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels       = 1;
        srvDesc.Texture2D.MostDetailedMip = 0;

        hr = m_device->CreateShaderResourceView(m_depthTexture.Get(), &srvDesc, m_srv.GetAddressOf());
        return SUCCEEDED(hr);
    }

    /// @brief デプスオンリーVSとインプットレイアウトを生成する
    [[nodiscard]] bool createDepthShader()
    {
        Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

        UINT flags = 0;
#ifdef _DEBUG
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

        HRESULT hr = D3DCompile(
            SHADOW_DEPTH_VS, strlen(SHADOW_DEPTH_VS),
            nullptr, nullptr, nullptr,
            "main", "vs_5_0", flags, 0,
            vsBlob.GetAddressOf(), errorBlob.GetAddressOf());
        if (FAILED(hr)) { return false; }

        hr = m_device->CreateVertexShader(
            vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
            nullptr, m_depthVS.GetAddressOf());
        if (FAILED(hr)) { return false; }

        D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
             D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        hr = m_device->CreateInputLayout(
            layout, 1,
            vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
            m_inputLayout.GetAddressOf());
        if (FAILED(hr)) { return false; }

        return createLightVPBuffer();
    }

    /// @brief ライトVP定数バッファを生成する
    [[nodiscard]] bool createLightVPBuffer()
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth      = (sizeof(float) * 16 + 15) & ~15u; // 64 bytes
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = m_device->CreateBuffer(&desc, nullptr, m_lightVPCB.GetAddressOf());
        return SUCCEEDED(hr);
    }

    /// @brief シャドウパス用ラスタライザ (CULL_NONE) を生成する
    [[nodiscard]] bool createShadowRasterizer()
    {
        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode        = D3D11_FILL_SOLID;
        rd.CullMode        = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        return SUCCEEDED(m_device->CreateRasterizerState(
            &rd, m_shadowRS.GetAddressOf()));
    }

    /// @brief ライトVP定数バッファを更新する
    void updateLightVPBuffer(ID3D11DeviceContext* ctx)
    {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = ctx->Map(m_lightVPCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) { return; }
        // sgc::Mat4f は row-major、HLSL の mul(vec, mat) は column-major 期待。
        // 転置してから upload。
        const auto vpT = m_lightVP.transposed();
        std::memcpy(mapped.pData, &vpT.m[0][0], sizeof(float) * 16);
        ctx->Unmap(m_lightVPCB.Get(), 0);
    }

    // DX11リソース
    ID3D11Device*                                    m_device      = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>          m_depthTexture;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView>   m_dsv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;
    Microsoft::WRL::ComPtr<ID3D11VertexShader>       m_depthVS;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>        m_inputLayout;
    Microsoft::WRL::ComPtr<ID3D11Buffer>             m_lightVPCB;

    // シャドウパス用ラスタライザ (CULL_NONE)
    Microsoft::WRL::ComPtr<ID3D11RasterizerState>  m_shadowRS;

    // キャッシュ（beginShadowRender / endShadowRender 間で使用）
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_savedRTV;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_savedDSV;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState>  m_savedRS;
    D3D11_VIEWPORT                                 m_savedVP = {};

#else // !MITIRU_HAS_DX11
    // DX11 なし: getShadowSRV() は nullptr を返す（型はvoid*）
public:
    [[nodiscard]] void* getShadowSRV() const noexcept { return nullptr; }
private:
#endif // MITIRU_HAS_DX11

    ShadowMapConfig3D m_config;
    sgc::Mat4f        m_lightVP;
    bool              m_initialized = false;
};

} // namespace mitiru::render
