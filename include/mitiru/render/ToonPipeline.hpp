#pragma once

/// @file ToonPipeline.hpp
/// @brief 自己完結型トゥーンレンダリングパイプライン（DX11）
/// @details N段階バンドライティング・スペキュラ・リムライト・背面膨張アウトライン・
///          スクリーンスペースアウトラインを1ファイルで提供する。
///          全パラメータは定数バッファ経由でランタイム変更可能。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d3d11.lib")

#include <sgc/types/Color.hpp>

namespace mitiru::render
{

// ============================================================================
// 設定構造体
// ============================================================================

/// @brief トゥーンライティング設定
struct ToonLightingConfig
{
	int bandCount = 3;                                            ///< バンド数 (1-8)
	float bandThresholds[8] = {0.5f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
	float bandBrightness[8] = {1.0f, 0.6f, 0.3f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f};
	sgc::Colorf shadowColor{0.2f, 0.1f, 0.2f, 1.0f};            ///< 影色ティント
	float specularSize      = 0.9f;                               ///< スペキュラ閾値
	float specularSmoothness = 0.05f;                             ///< スペキュラ遷移幅
	sgc::Colorf specularColor{1.0f, 1.0f, 1.0f, 1.0f};          ///< スペキュラ色
	bool rimEnabled         = true;                               ///< リムライト有効
	float rimWidth          = 0.3f;                               ///< フレネル幅
	float rimSmoothness     = 0.1f;                               ///< リム遷移幅
	sgc::Colorf rimColor{1.0f, 1.0f, 1.0f, 0.5f};               ///< リム色
	float ambientStrength   = 0.1f;                               ///< アンビエント強度
};

/// @brief アウトライン設定
struct ToonOutlineConfig
{
	/// アウトライン描画方式
	enum class Method : uint8_t
	{
		InvertedHull,          ///< 背面膨張法
		ScreenSpaceDepth,      ///< スクリーンスペース（深度）
		ScreenSpaceNormal,     ///< スクリーンスペース（法線）
		ScreenSpaceDepthNormal,///< スクリーンスペース（深度＋法線）
		None                   ///< アウトラインなし
	};

	Method method             = Method::InvertedHull;
	float width               = 0.02f;                            ///< 幅（膨張: ワールド単位, SS: ピクセル）
	sgc::Colorf color{0.0f, 0.0f, 0.0f, 1.0f};                  ///< アウトライン色
	float depthThreshold      = 0.1f;                             ///< SS深度閾値
	float normalThreshold     = 0.4f;                             ///< SS法線閾値
	bool scaleWithDistance    = true;                              ///< 距離に応じた幅スケーリング
};

/// @brief トゥーン設定（統合）
struct ToonConfig
{
	ToonLightingConfig lighting;
	ToonOutlineConfig outline;
};

// ============================================================================
// HLSL — トゥーン頂点シェーダー
// ============================================================================

constexpr const char* kTOON_VS = R"hlsl(
cbuffer CbToon : register(b0)
{
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
    float3   CameraPos;
    float    _pad0;
    float3   LightDir;
    float    _pad1;
    // lighting params (pixel shader only)
    float4   BandThresholds[2]; // 8 floats packed into 2 float4
    float4   BandBrightness[2];
    int      BandCount;
    float    AmbientStrength;
    float    SpecularSize;
    float    SpecularSmoothness;
    float4   SpecularColor;
    float4   ShadowColor;
    float    RimEnabled;
    float    RimWidth;
    float    RimSmoothness;
    float    _pad2;
    float4   RimColor;
};

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

struct VSOutput
{
    float4 Position  : SV_POSITION;
    float3 WorldPos  : TEXCOORD0;
    float3 WorldNorm : TEXCOORD1;
    float2 TexCoord  : TEXCOORD2;
    float4 Color     : COLOR0;
    float3 ViewDir   : TEXCOORD3;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;

    float4 worldPos = mul(float4(input.Position, 1.0), World);
    output.WorldPos  = worldPos.xyz;
    output.WorldNorm = normalize(mul(input.Normal, (float3x3)World));

    float4 viewPos   = mul(worldPos, View);
    output.Position  = mul(viewPos, Projection);

    output.TexCoord = input.TexCoord;
    output.Color    = input.Color;
    output.ViewDir  = normalize(CameraPos - worldPos.xyz);

    return output;
}
)hlsl";

// ============================================================================
// HLSL — トゥーンピクセルシェーダー
// ============================================================================

constexpr const char* kTOON_PS = R"hlsl(
cbuffer CbToon : register(b0)
{
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
    float3   CameraPos;
    float    _pad0;
    float3   LightDir;
    float    _pad1;
    float4   BandThresholds[2]; // [0].xyzw = bands 0-3, [1].xyzw = bands 4-7
    float4   BandBrightness[2];
    int      BandCount;
    float    AmbientStrength;
    float    SpecularSize;
    float    SpecularSmoothness;
    float4   SpecularColor;
    float4   ShadowColor;
    float    RimEnabled;
    float    RimWidth;
    float    RimSmoothness;
    float    _pad2;
    float4   RimColor;
};

Texture2D    albedoTex : register(t0);
SamplerState sampLinear : register(s0);

struct PSInput
{
    float4 Position  : SV_POSITION;
    float3 WorldPos  : TEXCOORD0;
    float3 WorldNorm : TEXCOORD1;
    float2 TexCoord  : TEXCOORD2;
    float4 Color     : COLOR0;
    float3 ViewDir   : TEXCOORD3;
};

float getBandThreshold(int idx)
{
    return (idx < 4) ? BandThresholds[0][idx] : BandThresholds[1][idx - 4];
}

float getBandBrightness(int idx)
{
    return (idx < 4) ? BandBrightness[0][idx] : BandBrightness[1][idx - 4];
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 N = normalize(input.WorldNorm);
    float3 L = normalize(-LightDir);
    float3 V = normalize(input.ViewDir);
    float3 H = normalize(L + V);

    float NdotL = dot(N, L);
    float NdotH = max(dot(N, H), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    // --- N-band quantized diffuse ---
    float brightness = getBandBrightness(BandCount - 1); // darkest band default
    for (int i = 0; i < BandCount; ++i)
    {
        float threshold = getBandThreshold(i);
        float edge = smoothstep(threshold - 0.02, threshold + 0.02, NdotL);
        brightness = lerp(brightness, getBandBrightness(i), edge);
    }

    // shadow color tinting: blend toward shadow color in dark areas
    float shadowFactor = 1.0 - brightness;
    float3 tintedLight = lerp(float3(1, 1, 1), ShadowColor.rgb, shadowFactor);

    // --- Specular (step-based) ---
    float specMask = smoothstep(SpecularSize - SpecularSmoothness,
                                SpecularSize + SpecularSmoothness, NdotH);
    // Only show specular on lit side
    float litSide = step(0.01, NdotL);
    float3 specContrib = SpecularColor.rgb * SpecularColor.a * specMask * litSide;

    // --- Rim light (fresnel) ---
    float3 rimContrib = float3(0, 0, 0);
    if (RimEnabled > 0.5)
    {
        float rim = 1.0 - NdotV;
        float rimMask = smoothstep(RimWidth - RimSmoothness,
                                   RimWidth + RimSmoothness, rim);
        // Rim visible on lit side only for artistic look
        float rimLit = smoothstep(-0.1, 0.3, NdotL);
        rimContrib = RimColor.rgb * RimColor.a * rimMask * rimLit;
    }

    // --- Combine ---
    float4 albedo = albedoTex.Sample(sampLinear, input.TexCoord) * input.Color;
    float3 ambient = albedo.rgb * AmbientStrength;
    float3 diffuse = albedo.rgb * brightness * tintedLight;
    float3 finalColor = ambient + diffuse + specContrib + rimContrib;

    return float4(finalColor, albedo.a);
}
)hlsl";

// ============================================================================
// HLSL — 背面膨張アウトライン頂点シェーダー
// ============================================================================

constexpr const char* kOUTLINE_HULL_VS = R"hlsl(
cbuffer CbOutline : register(b0)
{
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
    float3   CameraPos;
    float    OutlineWidth;
    float4   OutlineColor;
    float    ScaleWithDistance;
    float3   _pad;
};

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;

    float3 worldPos    = mul(float4(input.Position, 1.0), World).xyz;
    float3 worldNormal = normalize(mul(input.Normal, (float3x3)World));

    // Distance-based width scaling
    float dist = length(CameraPos - worldPos);
    float scale = (ScaleWithDistance > 0.5) ? saturate(dist * 0.1) : 1.0;

    // Expand along normal in world space
    float3 expanded = worldPos + worldNormal * OutlineWidth * scale;

    float4 viewPos  = mul(float4(expanded, 1.0), View);
    output.Position = mul(viewPos, Projection);

    return output;
}
)hlsl";

// ============================================================================
// HLSL — アウトラインピクセルシェーダー（膨張法用）
// ============================================================================

constexpr const char* kOUTLINE_PS = R"hlsl(
cbuffer CbOutline : register(b0)
{
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
    float3   CameraPos;
    float    OutlineWidth;
    float4   OutlineColor;
    float    ScaleWithDistance;
    float3   _pad;
};

struct PSInput
{
    float4 Position : SV_POSITION;
};

float4 PSMain(PSInput input) : SV_TARGET
{
    return OutlineColor;
}
)hlsl";

// ============================================================================
// HLSL — スクリーンスペースアウトラインピクセルシェーダー
// ============================================================================

constexpr const char* kOUTLINE_SCREEN_PS = R"hlsl(
Texture2D    sceneTexture  : register(t0);
Texture2D    depthTexture  : register(t1);
Texture2D    normalTexture : register(t2);
SamplerState sampPoint     : register(s0);

cbuffer CbScreenOutline : register(b0)
{
    float2 TexelSize;       // 1.0/screenW, 1.0/screenH
    float  DepthThreshold;
    float  NormalThreshold;
    float4 OutlineColor;
    float  LineWidth;
    int    Mode;            // 0=depth, 1=normal, 2=both
    float2 _pad;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

// Roberts Cross edge detection (sharper than Sobel for thin lines)
float robertsCrossDepth(float2 uv)
{
    float2 offset = TexelSize * LineWidth;
    float d00 = depthTexture.Sample(sampPoint, uv).r;
    float d11 = depthTexture.Sample(sampPoint, uv + offset).r;
    float d10 = depthTexture.Sample(sampPoint, uv + float2(offset.x, 0)).r;
    float d01 = depthTexture.Sample(sampPoint, uv + float2(0, offset.y)).r;

    float g1 = d00 - d11;
    float g2 = d10 - d01;
    return sqrt(g1 * g1 + g2 * g2);
}

float robertsCrossNormal(float2 uv)
{
    float2 offset = TexelSize * LineWidth;
    float3 n00 = normalTexture.Sample(sampPoint, uv).rgb;
    float3 n11 = normalTexture.Sample(sampPoint, uv + offset).rgb;
    float3 n10 = normalTexture.Sample(sampPoint, uv + float2(offset.x, 0)).rgb;
    float3 n01 = normalTexture.Sample(sampPoint, uv + float2(0, offset.y)).rgb;

    float3 g1 = n00 - n11;
    float3 g2 = n10 - n01;
    return sqrt(dot(g1, g1) + dot(g2, g2));
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 scene = sceneTexture.Sample(sampPoint, input.TexCoord);

    float edge = 0.0;

    if (Mode == 0 || Mode == 2) // depth
    {
        float depthEdge = robertsCrossDepth(input.TexCoord);
        edge = max(edge, step(DepthThreshold, depthEdge));
    }

    if (Mode == 1 || Mode == 2) // normal
    {
        float normalEdge = robertsCrossNormal(input.TexCoord);
        edge = max(edge, step(NormalThreshold, normalEdge));
    }

    float3 result = lerp(scene.rgb, OutlineColor.rgb, edge * OutlineColor.a);
    return float4(result, scene.a);
}
)hlsl";

// ============================================================================
// HLSL — フルスクリーン三角形頂点シェーダー（SS outline用）
// ============================================================================

constexpr const char* kFULLSCREEN_VS = R"hlsl(
struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;
    output.TexCoord = float2((vertexId << 1) & 2, vertexId & 2);
    output.Position = float4(
        output.TexCoord.x * 2.0 - 1.0,
        -(output.TexCoord.y * 2.0 - 1.0),
        0.0, 1.0);
    return output;
}
)hlsl";

// ============================================================================
// GPU定数バッファ構造体（CPU側）
// ============================================================================

/// @brief トゥーン描画用定数バッファ（CbToon: register(b0)）
struct alignas(16) CbToon
{
	float world[4][4]{};
	float view[4][4]{};
	float projection[4][4]{};
	float cameraPos[3]{};
	float _pad0 = 0.0f;
	float lightDir[3]{};
	float _pad1 = 0.0f;
	float bandThresholds[8]{};   // packed as 2x float4
	float bandBrightness[8]{};
	int   bandCount = 3;
	float ambientStrength = 0.1f;
	float specularSize = 0.9f;
	float specularSmoothness = 0.05f;
	float specularColor[4]{1.0f, 1.0f, 1.0f, 1.0f};
	float shadowColor[4]{0.2f, 0.1f, 0.2f, 1.0f};
	float rimEnabled = 1.0f;
	float rimWidth = 0.3f;
	float rimSmoothness = 0.1f;
	float _pad2 = 0.0f;
	float rimColor[4]{1.0f, 1.0f, 1.0f, 0.5f};
};

/// @brief アウトライン用定数バッファ（CbOutline: register(b0)）
struct alignas(16) CbOutline
{
	float world[4][4]{};
	float view[4][4]{};
	float projection[4][4]{};
	float cameraPos[3]{};
	float outlineWidth = 0.02f;
	float outlineColor[4]{0.0f, 0.0f, 0.0f, 1.0f};
	float scaleWithDistance = 1.0f;
	float _pad[3]{};
};

/// @brief スクリーンスペースアウトライン定数バッファ
struct alignas(16) CbScreenOutline
{
	float texelSize[2]{};
	float depthThreshold = 0.1f;
	float normalThreshold = 0.4f;
	float outlineColor[4]{0.0f, 0.0f, 0.0f, 1.0f};
	float lineWidth = 2.0f;
	int   mode = 0;          // 0=depth, 1=normal, 2=both
	float _pad[2]{};
};

// ============================================================================
// ToonPipeline
// ============================================================================

/// @brief 3D頂点のストライド: pos(3) + normal(3) + uv(2) + color(4) = 12 floats
static constexpr UINT kVertex3DStride = 48; // sizeof(float) * 12

/// @brief 自己完結型トゥーンレンダリングパイプライン
/// @details init()で全シェーダーをコンパイルし、描画パスを提供する。
class ToonPipeline
{
public:
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	ToonPipeline() noexcept = default;

	/// @brief 初期化（全シェーダーコンパイル＋GPUリソース生成）
	/// @param device DX11デバイス
	/// @return 成功時true
	bool init(ID3D11Device* device)
	{
		if (!device)
		{
			return false;
		}
		m_device = device;

		// シェーダーコンパイル
		auto toonVsBlob = compileShader(kTOON_VS, "VSMain", "vs_5_0");
		if (!toonVsBlob) return false;

		HRESULT hr = device->CreateVertexShader(
			toonVsBlob->GetBufferPointer(), toonVsBlob->GetBufferSize(),
			nullptr, m_toonVS.GetAddressOf());
		if (FAILED(hr)) return false;

		// 入力レイアウト（Vertex3D互換）
		const D3D11_INPUT_ELEMENT_DESC layout[] =
		{
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
		};
		hr = device->CreateInputLayout(
			layout, 4,
			toonVsBlob->GetBufferPointer(), toonVsBlob->GetBufferSize(),
			m_toonInputLayout.GetAddressOf());
		if (FAILED(hr)) return false;

		// トゥーンPS
		auto toonPsBlob = compileShader(kTOON_PS, "PSMain", "ps_5_0");
		if (!toonPsBlob) return false;
		hr = device->CreatePixelShader(
			toonPsBlob->GetBufferPointer(), toonPsBlob->GetBufferSize(),
			nullptr, m_toonPS.GetAddressOf());
		if (FAILED(hr)) return false;

		// アウトライン膨張VS
		auto outVsBlob = compileShader(kOUTLINE_HULL_VS, "VSMain", "vs_5_0");
		if (!outVsBlob) return false;
		hr = device->CreateVertexShader(
			outVsBlob->GetBufferPointer(), outVsBlob->GetBufferSize(),
			nullptr, m_outlineVS.GetAddressOf());
		if (FAILED(hr)) return false;

		// アウトライン入力レイアウト（同じVertex3Dフォーマット）
		hr = device->CreateInputLayout(
			layout, 4,
			outVsBlob->GetBufferPointer(), outVsBlob->GetBufferSize(),
			m_outlineInputLayout.GetAddressOf());
		if (FAILED(hr)) return false;

		// アウトラインPS
		auto outPsBlob = compileShader(kOUTLINE_PS, "PSMain", "ps_5_0");
		if (!outPsBlob) return false;
		hr = device->CreatePixelShader(
			outPsBlob->GetBufferPointer(), outPsBlob->GetBufferSize(),
			nullptr, m_outlinePS.GetAddressOf());
		if (FAILED(hr)) return false;

		// フルスクリーンVS（SS outline用）
		auto fsVsBlob = compileShader(kFULLSCREEN_VS, "VSMain", "vs_5_0");
		if (!fsVsBlob) return false;
		hr = device->CreateVertexShader(
			fsVsBlob->GetBufferPointer(), fsVsBlob->GetBufferSize(),
			nullptr, m_fullscreenVS.GetAddressOf());
		if (FAILED(hr)) return false;

		// SSアウトラインPS
		auto ssOutPsBlob = compileShader(kOUTLINE_SCREEN_PS, "PSMain", "ps_5_0");
		if (!ssOutPsBlob) return false;
		hr = device->CreatePixelShader(
			ssOutPsBlob->GetBufferPointer(), ssOutPsBlob->GetBufferSize(),
			nullptr, m_screenOutlinePS.GetAddressOf());
		if (FAILED(hr)) return false;

		// 定数バッファ
		m_cbToon = createCB(sizeof(CbToon));
		m_cbOutline = createCB(sizeof(CbOutline));
		m_cbScreenOutline = createCB(sizeof(CbScreenOutline));
		if (!m_cbToon || !m_cbOutline || !m_cbScreenOutline) return false;

		// ラスタライザステート: バックフェースカリング（トゥーン描画用）
		{
			D3D11_RASTERIZER_DESC rd = {};
			rd.FillMode = D3D11_FILL_SOLID;
			rd.CullMode = D3D11_CULL_BACK;
			rd.FrontCounterClockwise = FALSE;
			rd.DepthClipEnable = TRUE;
			hr = device->CreateRasterizerState(&rd, m_rsBackCull.GetAddressOf());
			if (FAILED(hr)) return false;
		}

		// ラスタライザステート: フロントフェースカリング（アウトライン用）
		{
			D3D11_RASTERIZER_DESC rd = {};
			rd.FillMode = D3D11_FILL_SOLID;
			rd.CullMode = D3D11_CULL_FRONT;
			rd.FrontCounterClockwise = FALSE;
			rd.DepthClipEnable = TRUE;
			hr = device->CreateRasterizerState(&rd, m_rsFrontCull.GetAddressOf());
			if (FAILED(hr)) return false;
		}

		// 深度ステンシル: 通常（トゥーン描画用）
		{
			D3D11_DEPTH_STENCIL_DESC dd = {};
			dd.DepthEnable = TRUE;
			dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			dd.DepthFunc = D3D11_COMPARISON_LESS;
			hr = device->CreateDepthStencilState(&dd, m_dssNormal.GetAddressOf());
			if (FAILED(hr)) return false;
		}

		// 深度ステンシル: 読み取り専用（アウトライン用）
		{
			D3D11_DEPTH_STENCIL_DESC dd = {};
			dd.DepthEnable = TRUE;
			dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
			hr = device->CreateDepthStencilState(&dd, m_dssOutline.GetAddressOf());
			if (FAILED(hr)) return false;
		}

		// ブレンドステート: アルファブレンド
		{
			D3D11_BLEND_DESC bd = {};
			bd.RenderTarget[0].BlendEnable = TRUE;
			bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
			bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
			bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
			bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			hr = device->CreateBlendState(&bd, m_blendAlpha.GetAddressOf());
			if (FAILED(hr)) return false;
		}

		// ポイントサンプラー（SS outline用）
		{
			D3D11_SAMPLER_DESC sd = {};
			sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
			sd.MaxLOD = D3D11_FLOAT32_MAX;
			hr = device->CreateSamplerState(&sd, m_samplerPoint.GetAddressOf());
			if (FAILED(hr)) return false;
		}

		// リニアサンプラー（トゥーンPS用）
		{
			D3D11_SAMPLER_DESC sd = {};
			sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
			sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
			sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
			sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
			sd.MaxLOD = D3D11_FLOAT32_MAX;
			hr = device->CreateSamplerState(&sd, m_samplerLinear.GetAddressOf());
			if (FAILED(hr)) return false;
		}

		// デフォルト白テクスチャ（アルベドなし時のフォールバック）
		createDefaultWhiteTexture();

		m_initialized = true;
		return true;
	}

	/// @brief リソース解放
	void shutdown()
	{
		m_toonVS.Reset(); m_toonPS.Reset(); m_toonInputLayout.Reset();
		m_outlineVS.Reset(); m_outlinePS.Reset(); m_outlineInputLayout.Reset();
		m_fullscreenVS.Reset(); m_screenOutlinePS.Reset();
		m_cbToon.Reset(); m_cbOutline.Reset(); m_cbScreenOutline.Reset();
		m_rsBackCull.Reset(); m_rsFrontCull.Reset();
		m_dssNormal.Reset(); m_dssOutline.Reset();
		m_blendAlpha.Reset();
		m_samplerPoint.Reset(); m_samplerLinear.Reset();
		m_defaultWhiteSRV.Reset();
		m_device = nullptr;
		m_initialized = false;
	}

	/// @brief 設定を適用する
	void setConfig(const ToonConfig& cfg) { m_config = cfg; }

	/// @brief 現在の設定を取得する
	[[nodiscard]] const ToonConfig& config() const noexcept { return m_config; }

	/// @brief 初期化済みか
	[[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

	// ─── トゥーン描画パス ─────────────────────────────────────

	/// @brief トゥーンパス開始
	void beginToonPass(ID3D11DeviceContext* ctx)
	{
		if (!m_initialized) return;
		ctx->VSSetShader(m_toonVS.Get(), nullptr, 0);
		ctx->PSSetShader(m_toonPS.Get(), nullptr, 0);
		ctx->IASetInputLayout(m_toonInputLayout.Get());
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->RSSetState(m_rsBackCull.Get());
		ctx->OMSetDepthStencilState(m_dssNormal.Get(), 0);

		const float bf[4] = {0, 0, 0, 0};
		ctx->OMSetBlendState(m_blendAlpha.Get(), bf, 0xFFFFFFFF);

		ID3D11SamplerState* sampler = m_samplerLinear.Get();
		ctx->PSSetSamplers(0, 1, &sampler);
	}

	/// @brief メッシュをトゥーン描画する
	/// @param ctx DX11コンテキスト
	/// @param vb 頂点バッファ
	/// @param ib インデックスバッファ（nullptrなら非インデックス描画）
	/// @param indexCount インデックス数（ibがnullptrならvertexCount）
	/// @param worldMatrix ワールド行列（float[16] row-major）
	/// @param viewMatrix ビュー行列
	/// @param projMatrix 射影行列
	/// @param lightDir ライト方向（float[3]）
	/// @param cameraPos カメラ位置（float[3]）
	void drawMesh(ID3D11DeviceContext* ctx,
	              ID3D11Buffer* vb, ID3D11Buffer* ib,
	              int indexCount,
	              const float* worldMatrix,
	              const float* viewMatrix,
	              const float* projMatrix,
	              const float* lightDir,
	              const float* cameraPos)
	{
		if (!m_initialized) return;

		// 定数バッファ更新
		CbToon cb = {};
		std::memcpy(cb.world, worldMatrix, 64);
		std::memcpy(cb.view, viewMatrix, 64);
		std::memcpy(cb.projection, projMatrix, 64);
		std::memcpy(cb.cameraPos, cameraPos, 12);
		std::memcpy(cb.lightDir, lightDir, 12);
		fillToonLightingCB(cb);
		updateBuffer(ctx, m_cbToon.Get(), &cb, sizeof(cb));

		ID3D11Buffer* cbs[] = {m_cbToon.Get()};
		ctx->VSSetConstantBuffers(0, 1, cbs);
		ctx->PSSetConstantBuffers(0, 1, cbs);

		// デフォルト白テクスチャをバインド（ユーザーが事前にセットしていない場合）
		ID3D11ShaderResourceView* srv = m_defaultWhiteSRV.Get();
		ctx->PSSetShaderResources(0, 1, &srv);

		// 頂点/インデックスバッファバインド
		const UINT stride = kVertex3DStride;
		const UINT offset = 0;
		ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);

		if (ib)
		{
			ctx->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
			ctx->DrawIndexed(static_cast<UINT>(indexCount), 0, 0);
		}
		else
		{
			ctx->Draw(static_cast<UINT>(indexCount), 0);
		}
	}

	/// @brief トゥーンパス終了
	void endToonPass(ID3D11DeviceContext* ctx)
	{
		(void)ctx;
		// 明示的なクリーンアップは不要（次パスが上書き）
	}

	// ─── 背面膨張アウトラインパス ─────────────────────────────

	/// @brief アウトラインパス開始
	void beginOutlinePass(ID3D11DeviceContext* ctx)
	{
		if (!m_initialized) return;
		ctx->VSSetShader(m_outlineVS.Get(), nullptr, 0);
		ctx->PSSetShader(m_outlinePS.Get(), nullptr, 0);
		ctx->IASetInputLayout(m_outlineInputLayout.Get());
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->RSSetState(m_rsFrontCull.Get());
		ctx->OMSetDepthStencilState(m_dssOutline.Get(), 0);
	}

	/// @brief アウトライン描画
	void drawOutline(ID3D11DeviceContext* ctx,
	                 ID3D11Buffer* vb, ID3D11Buffer* ib,
	                 int indexCount,
	                 const float* worldMatrix,
	                 const float* viewMatrix,
	                 const float* projMatrix,
	                 const float* cameraPos)
	{
		if (!m_initialized) return;

		CbOutline cb = {};
		std::memcpy(cb.world, worldMatrix, 64);
		std::memcpy(cb.view, viewMatrix, 64);
		std::memcpy(cb.projection, projMatrix, 64);
		std::memcpy(cb.cameraPos, cameraPos, 12);
		cb.outlineWidth = m_config.outline.width;
		cb.outlineColor[0] = m_config.outline.color.r;
		cb.outlineColor[1] = m_config.outline.color.g;
		cb.outlineColor[2] = m_config.outline.color.b;
		cb.outlineColor[3] = m_config.outline.color.a;
		cb.scaleWithDistance = m_config.outline.scaleWithDistance ? 1.0f : 0.0f;
		updateBuffer(ctx, m_cbOutline.Get(), &cb, sizeof(cb));

		ID3D11Buffer* cbs[] = {m_cbOutline.Get()};
		ctx->VSSetConstantBuffers(0, 1, cbs);
		ctx->PSSetConstantBuffers(0, 1, cbs);

		const UINT stride = kVertex3DStride;
		const UINT offset = 0;
		ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);

		if (ib)
		{
			ctx->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
			ctx->DrawIndexed(static_cast<UINT>(indexCount), 0, 0);
		}
		else
		{
			ctx->Draw(static_cast<UINT>(indexCount), 0);
		}
	}

	/// @brief アウトラインパス終了
	void endOutlinePass(ID3D11DeviceContext* ctx)
	{
		// バックフェースカリングに復元
		ctx->RSSetState(m_rsBackCull.Get());
		ctx->OMSetDepthStencilState(m_dssNormal.Get(), 0);
	}

	// ─── スクリーンスペースアウトライン（ポストプロセス）────────

	/// @brief スクリーンスペースアウトラインを適用する
	/// @param ctx DX11コンテキスト
	/// @param depthSRV 深度バッファSRV
	/// @param normalSRV 法線バッファSRV（nullptrならdepthのみ使用）
	/// @param outputRTV 出力先RTV
	/// @param sceneSRV シーンカラーSRV
	/// @param screenW スクリーン幅
	/// @param screenH スクリーン高さ
	void applyScreenSpaceOutline(ID3D11DeviceContext* ctx,
	                             ID3D11ShaderResourceView* sceneSRV,
	                             ID3D11ShaderResourceView* depthSRV,
	                             ID3D11ShaderResourceView* normalSRV,
	                             ID3D11RenderTargetView* outputRTV,
	                             int screenW, int screenH)
	{
		if (!m_initialized) return;

		// モード判定
		int mode = 0;
		if (normalSRV && !depthSRV) mode = 1;
		else if (normalSRV && depthSRV) mode = 2;

		CbScreenOutline cb = {};
		cb.texelSize[0] = 1.0f / static_cast<float>(screenW);
		cb.texelSize[1] = 1.0f / static_cast<float>(screenH);
		cb.depthThreshold = m_config.outline.depthThreshold;
		cb.normalThreshold = m_config.outline.normalThreshold;
		cb.outlineColor[0] = m_config.outline.color.r;
		cb.outlineColor[1] = m_config.outline.color.g;
		cb.outlineColor[2] = m_config.outline.color.b;
		cb.outlineColor[3] = m_config.outline.color.a;
		cb.lineWidth = m_config.outline.width;
		cb.mode = mode;
		updateBuffer(ctx, m_cbScreenOutline.Get(), &cb, sizeof(cb));

		// ビューポート
		D3D11_VIEWPORT vp = {};
		vp.Width = static_cast<float>(screenW);
		vp.Height = static_cast<float>(screenH);
		vp.MaxDepth = 1.0f;
		ctx->RSSetViewports(1, &vp);

		// レンダーターゲット
		ctx->OMSetRenderTargets(1, &outputRTV, nullptr);

		// シェーダー
		ctx->VSSetShader(m_fullscreenVS.Get(), nullptr, 0);
		ctx->PSSetShader(m_screenOutlinePS.Get(), nullptr, 0);
		ctx->IASetInputLayout(nullptr);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// SRVバインド
		ID3D11ShaderResourceView* srvs[3] = {sceneSRV, depthSRV, normalSRV};
		ctx->PSSetShaderResources(0, 3, srvs);

		// サンプラー
		ID3D11SamplerState* sampler = m_samplerPoint.Get();
		ctx->PSSetSamplers(0, 1, &sampler);

		// 定数バッファ
		ID3D11Buffer* cbs[] = {m_cbScreenOutline.Get()};
		ctx->PSSetConstantBuffers(0, 1, cbs);

		// フルスクリーン三角形描画
		ctx->Draw(3, 0);

		// SRVバインド解除
		ID3D11ShaderResourceView* nullSRVs[3] = {nullptr, nullptr, nullptr};
		ctx->PSSetShaderResources(0, 3, nullSRVs);
	}

	// ─── デモ（自己完結型テスト用）──────────────────────────

	/// @brief 回転キューブでトゥーン描画＋アウトラインの動作確認を行う
	/// @param ctx DX11コンテキスト
	/// @param rtv 出力先RTV
	/// @param dsv 深度バッファDSV
	/// @param screenW スクリーン幅
	/// @param screenH スクリーン高さ
	/// @param timeSeconds 経過時間（秒）
	/// @brief デモ用リソースを初期化する（1回だけ呼ぶ）
	/// @param device D3D11デバイス
	/// @return 成功した場合true
	bool initDemo(ID3D11Device* device)
	{
		if (m_demoInitialized)
		{
			return true;
		}

		if (!init(device))
		{
			return false;
		}

		struct DemoVertex
		{
			float px, py, pz;
			float nx, ny, nz;
			float u, v;
			float r, g, b, a;
		};

		const DemoVertex verts[] =
		{
			// 前面 (z = +0.5)
			{-0.5f, -0.5f,  0.5f,  0, 0, 1,  0,1,  0.9f,0.3f,0.3f,1},
			{ 0.5f, -0.5f,  0.5f,  0, 0, 1,  1,1,  0.9f,0.3f,0.3f,1},
			{ 0.5f,  0.5f,  0.5f,  0, 0, 1,  1,0,  0.9f,0.3f,0.3f,1},
			{-0.5f,  0.5f,  0.5f,  0, 0, 1,  0,0,  0.9f,0.3f,0.3f,1},
			// 背面 (z = -0.5)
			{ 0.5f, -0.5f, -0.5f,  0, 0,-1,  0,1,  0.3f,0.9f,0.3f,1},
			{-0.5f, -0.5f, -0.5f,  0, 0,-1,  1,1,  0.3f,0.9f,0.3f,1},
			{-0.5f,  0.5f, -0.5f,  0, 0,-1,  1,0,  0.3f,0.9f,0.3f,1},
			{ 0.5f,  0.5f, -0.5f,  0, 0,-1,  0,0,  0.3f,0.9f,0.3f,1},
			// 上面 (y = +0.5)
			{-0.5f,  0.5f,  0.5f,  0, 1, 0,  0,1,  0.3f,0.3f,0.9f,1},
			{ 0.5f,  0.5f,  0.5f,  0, 1, 0,  1,1,  0.3f,0.3f,0.9f,1},
			{ 0.5f,  0.5f, -0.5f,  0, 1, 0,  1,0,  0.3f,0.3f,0.9f,1},
			{-0.5f,  0.5f, -0.5f,  0, 1, 0,  0,0,  0.3f,0.3f,0.9f,1},
			// 下面 (y = -0.5)
			{-0.5f, -0.5f, -0.5f,  0,-1, 0,  0,1,  0.9f,0.9f,0.3f,1},
			{ 0.5f, -0.5f, -0.5f,  0,-1, 0,  1,1,  0.9f,0.9f,0.3f,1},
			{ 0.5f, -0.5f,  0.5f,  0,-1, 0,  1,0,  0.9f,0.9f,0.3f,1},
			{-0.5f, -0.5f,  0.5f,  0,-1, 0,  0,0,  0.9f,0.9f,0.3f,1},
			// 右面 (x = +0.5)
			{ 0.5f, -0.5f,  0.5f,  1, 0, 0,  0,1,  0.9f,0.3f,0.9f,1},
			{ 0.5f, -0.5f, -0.5f,  1, 0, 0,  1,1,  0.9f,0.3f,0.9f,1},
			{ 0.5f,  0.5f, -0.5f,  1, 0, 0,  1,0,  0.9f,0.3f,0.9f,1},
			{ 0.5f,  0.5f,  0.5f,  1, 0, 0,  0,0,  0.9f,0.3f,0.9f,1},
			// 左面 (x = -0.5)
			{-0.5f, -0.5f, -0.5f, -1, 0, 0,  0,1,  0.3f,0.9f,0.9f,1},
			{-0.5f, -0.5f,  0.5f, -1, 0, 0,  1,1,  0.3f,0.9f,0.9f,1},
			{-0.5f,  0.5f,  0.5f, -1, 0, 0,  1,0,  0.3f,0.9f,0.9f,1},
			{-0.5f,  0.5f, -0.5f, -1, 0, 0,  0,0,  0.3f,0.9f,0.9f,1},
		};

		const uint32_t indices[] =
		{
			 0, 1, 2,  0, 2, 3,   // 前面
			 4, 5, 6,  4, 6, 7,   // 背面
			 8, 9,10,  8,10,11,   // 上面
			12,13,14, 12,14,15,   // 下面
			16,17,18, 16,18,19,   // 右面
			20,21,22, 20,22,23,   // 左面
		};

		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = sizeof(verts);
		bd.Usage = D3D11_USAGE_IMMUTABLE;
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		D3D11_SUBRESOURCE_DATA sd = {};
		sd.pSysMem = verts;
		device->CreateBuffer(&bd, &sd, m_demoCubeVB.GetAddressOf());

		bd.ByteWidth = sizeof(indices);
		bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
		sd.pSysMem = indices;
		device->CreateBuffer(&bd, &sd, m_demoCubeIB.GetAddressOf());

		m_demoInitialized = true;
		return true;
	}

	/// @brief トゥーンシェーディングのデモを描画する
	/// @param ctx D3D11デバイスコンテキスト
	/// @param rtv 出力先RTV
	/// @param dsv 深度バッファDSV
	/// @param screenW スクリーン幅
	/// @param screenH スクリーン高さ
	/// @param timeSeconds 経過時間（秒）
	void renderDemo(ID3D11DeviceContext* ctx,
	                ID3D11RenderTargetView* rtv,
	                ID3D11DepthStencilView* dsv,
	                int screenW, int screenH,
	                float timeSeconds)
	{
		if (!m_demoInitialized)
		{
			return;
		}

		// ── 行列計算（簡易実装）──
		const float aspect = static_cast<float>(screenW) / static_cast<float>(screenH);
		const float fov = 0.785398f; // 45度
		const float nearZ = 0.1f;
		const float farZ = 100.0f;

		// 射影行列（透視投影）
		float projMatrix[16] = {};
		{
			float yScale = 1.0f / std::tan(fov * 0.5f);
			float xScale = yScale / aspect;
			projMatrix[0]  = xScale;
			projMatrix[5]  = yScale;
			projMatrix[10] = farZ / (farZ - nearZ);
			projMatrix[11] = 1.0f;
			projMatrix[14] = -nearZ * farZ / (farZ - nearZ);
		}

		// ビュー行列（カメラ: (0,1,-3) → 原点を見る）
		float viewMatrix[16] = {};
		{
			// 簡易lookAt: eye=(0, 1, -3), target=(0, 0, 0), up=(0, 1, 0)
			// 手計算でrow-major lookAt
			const float ex = 0.0f, ey = 1.0f, ez = -3.0f;
			float fx = -ex, fy = -ey, fz = -ez; // forward = target - eye
			float fl = std::sqrt(fx*fx + fy*fy + fz*fz);
			fx /= fl; fy /= fl; fz /= fl;
			// right = cross(up, forward)
			float rx = 1.0f * fz - 0.0f * fy;
			float ry = 0.0f * fx - 0.0f * fz;
			float rz = 0.0f * fy - 1.0f * fx;
			float rl = std::sqrt(rx*rx + ry*ry + rz*rz);
			rx /= rl; ry /= rl; rz /= rl;
			// up = cross(forward, right)
			float ux = fy*rz - fz*ry;
			float uy = fz*rx - fx*rz;
			float uz = fx*ry - fy*rx;

			viewMatrix[0] = rx;  viewMatrix[1] = ux;  viewMatrix[2] = fx;  viewMatrix[3] = 0;
			viewMatrix[4] = ry;  viewMatrix[5] = uy;  viewMatrix[6] = fy;  viewMatrix[7] = 0;
			viewMatrix[8] = rz;  viewMatrix[9] = uz;  viewMatrix[10]= fz;  viewMatrix[11]= 0;
			viewMatrix[12]= -(rx*ex + ry*ey + rz*ez);
			viewMatrix[13]= -(ux*ex + uy*ey + uz*ez);
			viewMatrix[14]= -(fx*ex + fy*ey + fz*ez);
			viewMatrix[15]= 1.0f;
		}

		// ワールド行列（Y軸回転）
		float worldMatrix[16] = {};
		{
			const float angle = timeSeconds * 0.8f;
			const float c = std::cos(angle);
			const float s = std::sin(angle);
			worldMatrix[0]  = c;
			worldMatrix[2]  = -s;
			worldMatrix[5]  = 1.0f;
			worldMatrix[8]  = s;
			worldMatrix[10] = c;
			worldMatrix[15] = 1.0f;
		}

		const float lightDir[3] = {0.3f, -0.7f, 0.5f};
		const float cameraPos[3] = {0.0f, 1.0f, -3.0f};

		// ── クリア ──
		const float clearColor[4] = {0.15f, 0.15f, 0.2f, 1.0f};
		ctx->ClearRenderTargetView(rtv, clearColor);
		if (dsv)
		{
			ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
		}
		ctx->OMSetRenderTargets(1, &rtv, dsv);

		D3D11_VIEWPORT vp = {};
		vp.Width = static_cast<float>(screenW);
		vp.Height = static_cast<float>(screenH);
		vp.MaxDepth = 1.0f;
		ctx->RSSetViewports(1, &vp);

		// ── アウトライン先行描画（背面膨張法）──
		beginOutlinePass(ctx);
		drawOutline(ctx, m_demoCubeVB.Get(), m_demoCubeIB.Get(), 36,
		            worldMatrix, viewMatrix, projMatrix, cameraPos);
		endOutlinePass(ctx);

		// ── トゥーン描画 ──
		beginToonPass(ctx);
		drawMesh(ctx, m_demoCubeVB.Get(), m_demoCubeIB.Get(), 36,
		         worldMatrix, viewMatrix, projMatrix, lightDir, cameraPos);
		endToonPass(ctx);
	}

private:
	/// @brief HLSLをコンパイルする
	[[nodiscard]] ComPtr<ID3DBlob> compileShader(
		const char* source,
		const char* entryPoint,
		const char* target)
	{
		ComPtr<ID3DBlob> shaderBlob;
		ComPtr<ID3DBlob> errorBlob;

		UINT flags = 0;
#ifdef _DEBUG
		flags |= D3DCOMPILE_DEBUG;
		flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

		HRESULT hr = D3DCompile(
			source, std::strlen(source),
			nullptr, nullptr, nullptr,
			entryPoint, target,
			flags, 0,
			shaderBlob.GetAddressOf(),
			errorBlob.GetAddressOf());

		if (FAILED(hr))
		{
			std::string msg = "ToonPipeline: shader compile failed";
			if (errorBlob)
			{
				msg += ": ";
				msg += static_cast<const char*>(errorBlob->GetBufferPointer());
			}
			throw std::runtime_error(msg);
		}

		return shaderBlob;
	}

	/// @brief 定数バッファ生成
	[[nodiscard]] ComPtr<ID3D11Buffer> createCB(uint32_t sizeBytes)
	{
		const auto aligned = (sizeBytes + 15u) & ~15u;
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = aligned;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		ComPtr<ID3D11Buffer> buffer;
		HRESULT hr = m_device->CreateBuffer(&desc, nullptr, buffer.GetAddressOf());
		if (FAILED(hr)) return nullptr;
		return buffer;
	}

	/// @brief 定数バッファ更新
	void updateBuffer(ID3D11DeviceContext* ctx, ID3D11Buffer* buffer,
	                  const void* data, uint32_t size)
	{
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		HRESULT hr = ctx->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (SUCCEEDED(hr))
		{
			std::memcpy(mapped.pData, data, size);
			ctx->Unmap(buffer, 0);
		}
	}

	/// @brief ToonLightingConfigからCbToonにパラメータを転送する
	void fillToonLightingCB(CbToon& cb) const
	{
		const auto& l = m_config.lighting;
		for (int i = 0; i < 8; ++i)
		{
			cb.bandThresholds[i] = l.bandThresholds[i];
			cb.bandBrightness[i] = l.bandBrightness[i];
		}
		cb.bandCount = l.bandCount;
		cb.ambientStrength = l.ambientStrength;
		cb.specularSize = l.specularSize;
		cb.specularSmoothness = l.specularSmoothness;
		cb.specularColor[0] = l.specularColor.r;
		cb.specularColor[1] = l.specularColor.g;
		cb.specularColor[2] = l.specularColor.b;
		cb.specularColor[3] = l.specularColor.a;
		cb.shadowColor[0] = l.shadowColor.r;
		cb.shadowColor[1] = l.shadowColor.g;
		cb.shadowColor[2] = l.shadowColor.b;
		cb.shadowColor[3] = l.shadowColor.a;
		cb.rimEnabled = l.rimEnabled ? 1.0f : 0.0f;
		cb.rimWidth = l.rimWidth;
		cb.rimSmoothness = l.rimSmoothness;
		cb.rimColor[0] = l.rimColor.r;
		cb.rimColor[1] = l.rimColor.g;
		cb.rimColor[2] = l.rimColor.b;
		cb.rimColor[3] = l.rimColor.a;
	}

	/// @brief 1x1白テクスチャ生成（アルベドマップ未指定時のフォールバック）
	void createDefaultWhiteTexture()
	{
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = 1;
		td.Height = 1;
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_IMMUTABLE;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		const uint32_t white = 0xFFFFFFFF;
		D3D11_SUBRESOURCE_DATA sd = {};
		sd.pSysMem = &white;
		sd.SysMemPitch = 4;

		ComPtr<ID3D11Texture2D> tex;
		HRESULT hr = m_device->CreateTexture2D(&td, &sd, tex.GetAddressOf());
		if (FAILED(hr)) return;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
		srvd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvd.Texture2D.MipLevels = 1;
		m_device->CreateShaderResourceView(tex.Get(), &srvd, m_defaultWhiteSRV.GetAddressOf());
	}

	// ─── メンバ ─────────────────────────────────────────────

	ComPtr<ID3D11Device> m_device;
	bool m_initialized = false;
	ToonConfig m_config;

	// シェーダー
	ComPtr<ID3D11VertexShader> m_toonVS;
	ComPtr<ID3D11PixelShader>  m_toonPS;
	ComPtr<ID3D11InputLayout>  m_toonInputLayout;

	ComPtr<ID3D11VertexShader> m_outlineVS;
	ComPtr<ID3D11PixelShader>  m_outlinePS;
	ComPtr<ID3D11InputLayout>  m_outlineInputLayout;

	ComPtr<ID3D11VertexShader> m_fullscreenVS;
	ComPtr<ID3D11PixelShader>  m_screenOutlinePS;

	// 定数バッファ
	ComPtr<ID3D11Buffer> m_cbToon;
	ComPtr<ID3D11Buffer> m_cbOutline;
	ComPtr<ID3D11Buffer> m_cbScreenOutline;

	// ステート
	ComPtr<ID3D11RasterizerState>   m_rsBackCull;
	ComPtr<ID3D11RasterizerState>   m_rsFrontCull;
	ComPtr<ID3D11DepthStencilState> m_dssNormal;
	ComPtr<ID3D11DepthStencilState> m_dssOutline;
	ComPtr<ID3D11BlendState>        m_blendAlpha;

	// サンプラー
	ComPtr<ID3D11SamplerState> m_samplerPoint;
	ComPtr<ID3D11SamplerState> m_samplerLinear;

	// デフォルトリソース
	ComPtr<ID3D11ShaderResourceView> m_defaultWhiteSRV;

	// デモ用リソース（initDemo()で初期化）
	ComPtr<ID3D11Buffer> m_demoCubeVB;
	ComPtr<ID3D11Buffer> m_demoCubeIB;
	bool m_demoInitialized = false;
};

} // namespace mitiru::render

#endif // _WIN32
