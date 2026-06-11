#pragma once

/// @file TemporalContourPass.hpp
/// @brief 輪郭線時間安定化（`TemporalContourStabilizer`）の GPU シェーダ移植（#11）
/// @details CPU 実装（`TemporalContour.hpp`）と等価な処理を fullscreen ピクセルシェーダで行う。
///          入力 SRV: velocity(RG16F, UV単位) / rawContour(R16F) / prevStabilized(R16F, ping-pong) /
///          curObjId(R32_UINT) / prevObjId(R32_UINT)。出力: stabilized contour RT(R16F)。
///          アルゴリズム = reproject(uv - velocity) + objectId gate + EMA + raw 近傍ゲート床
///          （CPU 版と同一。研究 §3）。`MotionVectorPass` / TAA と同じ GPU RT 群に接続して
///          実ゲーム表示経路で使う。
///
/// 入力 SRV は外部設定（所有しない）。prevStabilized は前フレームの本パス出力を渡す（呼び出し側 ping-pong）。
/// @note GPU バックエンド有り環境での smoke 前提（headless では shader 文字列 + API の compile-check のみ）。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <stdexcept>
#include <string_view>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace mitiru::render
{

/// @brief 安定化シェーダ（CPU `TemporalContourStabilizer::stabilize` と等価）
constexpr std::string_view TEMPORAL_CONTOUR_PS = R"hlsl(
Texture2D<float2> Velocity   : register(t0);   // 画面内移動量(UV)
Texture2D<float>  RawContour : register(t1);   // 現フレーム生輪郭
Texture2D<float>  PrevStable : register(t2);   // 前フレーム安定化輪郭(ping-pong)
Texture2D<uint>   CurObjId   : register(t3);
Texture2D<uint>   PrevObjId  : register(t4);
SamplerState LinearClamp : register(s0);
SamplerState PointClamp  : register(s1);

cbuffer TCParams : register(b0)
{
    float Alpha;            // EMA 履歴重み
    float TauOn;            // ヒステリシス床下限
    float FloorMotionFalloff;
    float RawGateThreshold;
    float2 TexelSize;       // 1/幅, 1/高さ
    float2 ScreenSize;      // 幅, 高さ
    uint  UseObjId;
    uint  UseHysteresis;
    uint  FloorRawGate;
    uint  FirstFrame;       // 1 = 履歴なし → raw を返す
    float AlphaMotionFalloff; // EMA 履歴重みの velocity 減衰（CPU 版と同一）
    float3 _pad;
};

struct PSInput { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

float PSMain(PSInput i) : SV_TARGET
{
    float raw = RawContour.SampleLevel(PointClamp, i.uv, 0);
    if (FirstFrame != 0) { return raw; }

    float2 vel   = Velocity.SampleLevel(PointClamp, i.uv, 0);
    float2 srcUV = i.uv - vel;                         // 前フレーム対応位置
    if (srcUV.x < 0.0 || srcUV.x > 1.0 || srcUV.y < 0.0 || srcUV.y > 1.0) { return raw; }

    if (UseObjId != 0)
    {
        int2 curP = int2(i.uv  * ScreenSize);
        int2 srcP = int2(srcUV * ScreenSize);
        if (CurObjId.Load(int3(curP, 0)) != PrevObjId.Load(int3(srcP, 0))) { return raw; }
    }

    float hist = PrevStable.SampleLevel(LinearClamp, srcUV, 0);
    float speed = length(vel * ScreenSize);            // pixel 速度
    float alphaEff = Alpha;
    if (AlphaMotionFalloff > 0.0)                      // 移動エッジは履歴を弱めてぼやけ抑制
    {
        alphaEff *= saturate(1.0 - AlphaMotionFalloff * speed);
    }
    float s = raw * (1.0 - alphaEff) + hist * alphaEff; // EMA

    if (UseHysteresis != 0 && hist >= TauOn)
    {
        bool allowed = true;
        if (FloorRawGate != 0)
        {
            float m = 0.0;
            [unroll] for (int dy = -1; dy <= 1; ++dy)
            [unroll] for (int dx = -1; dx <= 1; ++dx)
                m = max(m, RawContour.SampleLevel(PointClamp,
                          i.uv + float2(dx, dy) * TexelSize, 0));
            allowed = m >= RawGateThreshold;
        }
        if (allowed)
        {
            float cap = TauOn;
            if (FloorMotionFalloff > 0.0)
            {
                cap *= saturate(1.0 - FloorMotionFalloff * speed);
            }
            s = max(s, min(hist, cap));
        }
    }
    return s;
}
)hlsl";

constexpr std::string_view TEMPORAL_CONTOUR_VS = R"hlsl(
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    o.uv  = float2((id << 1) & 2, id & 2);             // フルスクリーン三角形
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
)hlsl";

/// @brief パラメータ（CPU `TemporalContourParams` と対応）
struct TemporalContourPassParams
{
    float alpha = 0.8f;
    float alphaMotionFalloff = 0.0f; // EMA 履歴重みの velocity 減衰（0 = 従来挙動）
    float tauOn = 0.5f;
    float floorMotionFalloff = 0.0f;
    float rawGateThreshold = 0.1f;
    bool  useObjId = true;
    bool  useHysteresis = true;
    bool  floorRawGate = false;
};

class TemporalContourPass
{
public:
    template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    void init(ID3D11Device* device, std::uint32_t width, std::uint32_t height)
    {
        if (device == nullptr) { throw std::runtime_error("TemporalContourPass: null device"); }
        m_device = device; m_width = width; m_height = height;
        m_vs = compileVS(device, TEMPORAL_CONTOUR_VS);
        m_ps = compilePS(device, TEMPORAL_CONTOUR_PS);
        m_cb = createCB(device, sizeof(CbTC));
        m_outRT = createRT(device, width, height, DXGI_FORMAT_R16_FLOAT);
        m_linear = createSampler(device, D3D11_FILTER_MIN_MAG_MIP_LINEAR);
        m_point  = createSampler(device, D3D11_FILTER_MIN_MAG_MIP_POINT);
    }

    void setInputs(ID3D11ShaderResourceView* velocity, ID3D11ShaderResourceView* rawContour,
                   ID3D11ShaderResourceView* prevStable, ID3D11ShaderResourceView* curObjId,
                   ID3D11ShaderResourceView* prevObjId) noexcept
    {
        m_velocity = velocity; m_raw = rawContour; m_prevStable = prevStable;
        m_curObjId = curObjId; m_prevObjId = prevObjId;
    }

    void setParams(const TemporalContourPassParams& p) noexcept { m_params = p; }

    /// @brief 安定化を実行し、結果を出力 RT（`stabilizedSRV()`）へ書く。
    /// @param firstFrame 履歴が無い初回は true（raw をそのまま出す）。
    void apply(ID3D11DeviceContext* ctx, bool firstFrame)
    {
        CbTC cb{};
        cb.alpha = m_params.alpha; cb.tauOn = m_params.tauOn;
        cb.floorMotionFalloff = m_params.floorMotionFalloff;
        cb.rawGateThreshold = m_params.rawGateThreshold;
        cb.texelX = 1.0f / static_cast<float>(m_width);
        cb.texelY = 1.0f / static_cast<float>(m_height);
        cb.screenX = static_cast<float>(m_width);
        cb.screenY = static_cast<float>(m_height);
        cb.useObjId = m_params.useObjId ? 1u : 0u;
        cb.useHysteresis = m_params.useHysteresis ? 1u : 0u;
        cb.floorRawGate = m_params.floorRawGate ? 1u : 0u;
        cb.firstFrame = firstFrame ? 1u : 0u;
        cb.alphaMotionFalloff = m_params.alphaMotionFalloff;

        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(ctx->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
        {
            std::memcpy(m.pData, &cb, sizeof(cb));
            ctx->Unmap(m_cb.Get(), 0);
        }

        ID3D11RenderTargetView* rtv = m_outRT.rtv.Get();
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        D3D11_VIEWPORT vp{}; vp.Width = static_cast<float>(m_width);
        vp.Height = static_cast<float>(m_height); vp.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &vp);

        ID3D11ShaderResourceView* srvs[5] = {m_velocity, m_raw, m_prevStable, m_curObjId, m_prevObjId};
        ctx->PSSetShaderResources(0, 5, srvs);
        ID3D11SamplerState* samps[2] = {m_linear.Get(), m_point.Get()};
        ctx->PSSetSamplers(0, 2, samps);
        ID3D11Buffer* cbuf = m_cb.Get();
        ctx->PSSetConstantBuffers(0, 1, &cbuf);

        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(m_vs.Get(), nullptr, 0);
        ctx->PSSetShader(m_ps.Get(), nullptr, 0);
        ctx->Draw(3, 0);

        ID3D11ShaderResourceView* nul[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
        ctx->PSSetShaderResources(0, 5, nul);
        ID3D11RenderTargetView* nrt = nullptr;
        ctx->OMSetRenderTargets(1, &nrt, nullptr);
    }

    [[nodiscard]] ID3D11ShaderResourceView* stabilizedSRV() const noexcept { return m_outRT.srv.Get(); }
    [[nodiscard]] ID3D11Texture2D* stabilizedTexture() const noexcept { return m_outRT.texture.Get(); }

private:
    struct alignas(16) CbTC
    {
        float alpha, tauOn, floorMotionFalloff, rawGateThreshold;
        float texelX, texelY, screenX, screenY;
        std::uint32_t useObjId, useHysteresis, floorRawGate, firstFrame;
        float alphaMotionFalloff, pad0, pad1, pad2;
    };
    struct RTData { ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11ShaderResourceView> srv; ComPtr<ID3D11RenderTargetView> rtv; };

    [[nodiscard]] static ComPtr<ID3D11VertexShader> compileVS(ID3D11Device* d, std::string_view src)
    {
        ComPtr<ID3DBlob> b, e;
        if (FAILED(D3DCompile(src.data(), src.size(), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, b.GetAddressOf(), e.GetAddressOf())))
            throw std::runtime_error("TemporalContourPass: VS compile failed");
        ComPtr<ID3D11VertexShader> vs;
        if (FAILED(d->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, vs.GetAddressOf())))
            throw std::runtime_error("TemporalContourPass: CreateVS failed");
        return vs;
    }
    [[nodiscard]] static ComPtr<ID3D11PixelShader> compilePS(ID3D11Device* d, std::string_view src)
    {
        ComPtr<ID3DBlob> b, e;
        if (FAILED(D3DCompile(src.data(), src.size(), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, b.GetAddressOf(), e.GetAddressOf())))
            throw std::runtime_error("TemporalContourPass: PS compile failed");
        ComPtr<ID3D11PixelShader> ps;
        if (FAILED(d->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, ps.GetAddressOf())))
            throw std::runtime_error("TemporalContourPass: CreatePS failed");
        return ps;
    }
    [[nodiscard]] static ComPtr<ID3D11Buffer> createCB(ID3D11Device* d, std::uint32_t bytes)
    {
        D3D11_BUFFER_DESC desc{}; desc.ByteWidth = bytes; desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER; desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ComPtr<ID3D11Buffer> cb;
        if (FAILED(d->CreateBuffer(&desc, nullptr, cb.GetAddressOf())))
            throw std::runtime_error("TemporalContourPass: CreateBuffer failed");
        return cb;
    }
    [[nodiscard]] static RTData createRT(ID3D11Device* d, std::uint32_t w, std::uint32_t h, DXGI_FORMAT fmt)
    {
        RTData rt;
        D3D11_TEXTURE2D_DESC desc{}; desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = fmt; desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        if (FAILED(d->CreateTexture2D(&desc, nullptr, rt.texture.GetAddressOf())))
            throw std::runtime_error("TemporalContourPass: CreateTexture2D failed");
        if (FAILED(d->CreateShaderResourceView(rt.texture.Get(), nullptr, rt.srv.GetAddressOf())))
            throw std::runtime_error("TemporalContourPass: CreateSRV failed");
        if (FAILED(d->CreateRenderTargetView(rt.texture.Get(), nullptr, rt.rtv.GetAddressOf())))
            throw std::runtime_error("TemporalContourPass: CreateRTV failed");
        return rt;
    }
    [[nodiscard]] static ComPtr<ID3D11SamplerState> createSampler(ID3D11Device* d, D3D11_FILTER f)
    {
        D3D11_SAMPLER_DESC s{}; s.Filter = f;
        s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        ComPtr<ID3D11SamplerState> samp;
        if (FAILED(d->CreateSamplerState(&s, samp.GetAddressOf())))
            throw std::runtime_error("TemporalContourPass: CreateSampler failed");
        return samp;
    }

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader> m_ps;
    ComPtr<ID3D11Buffer> m_cb;
    ComPtr<ID3D11SamplerState> m_linear, m_point;
    RTData m_outRT;
    ID3D11ShaderResourceView* m_velocity = nullptr;
    ID3D11ShaderResourceView* m_raw = nullptr;
    ID3D11ShaderResourceView* m_prevStable = nullptr;
    ID3D11ShaderResourceView* m_curObjId = nullptr;
    ID3D11ShaderResourceView* m_prevObjId = nullptr;
    TemporalContourPassParams m_params;
    std::uint32_t m_width = 0, m_height = 0;
};

} // namespace mitiru::render

#endif // _WIN32
