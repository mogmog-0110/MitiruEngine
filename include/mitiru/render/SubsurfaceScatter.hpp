#pragma once

/// @file SubsurfaceScatter.hpp
/// @brief サブサーフェススキャタリング（SSS）ポストプロセスエフェクト
/// @details 分離可能ガウシアンブラーをSSSプロファイルで重み付けし、
///          肌や蝋などの半透明マテリアルの表面下散乱を近似する。
///          DX11ヘッダーオンリー実装。SSAOEffect.hppと同様のパターンに従う。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")

#include <mitiru/render/postprocess/PostProcessUtils.hpp>

namespace mitiru::render
{

// ============================================================================
// HLSL定数 — SSS分離可能ブラーピクセルシェーダー
// ============================================================================

/// @brief SSSブラーピクセルシェーダー
/// @details SSSプロファイルカーネルによる分離可能ガウシアンブラー。
///          水平パスと垂直パスを別々に実行する。
constexpr std::string_view SSS_BLUR_PS = R"hlsl(
Texture2D sceneTexture : register(t0);
Texture2D depthTexture : register(t1);
SamplerState linearClampSampler : register(s0);

cbuffer SSSParams : register(b0)
{
    float2 direction;      // (1,0) for horizontal, (0,1) for vertical
    float sssWidth;        // scatter width in screen space
    float correction;      // depth correction factor
    float4 kernel[17];     // SSS profile kernel weights (rgb + offset)
    float2 texelSize;      // 1.0 / textureSize
    float maxDepthDiff;    // max depth difference threshold
    float padding;
};

struct PSInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target
{
    float4 colorCenter = sceneTexture.Sample(linearClampSampler, input.uv);
    float depthCenter = depthTexture.Sample(linearClampSampler, input.uv).r;

    if (depthCenter >= 1.0)
        return colorCenter;

    // Standard separable Gaussian SSS:
    //   kernel[i].rgb = per-channel weight contribution
    //   kernel[i].a   = spatial offset along blur direction
    // Center (i=0) は offset=0、weight=rgb。各 tap は uv±offset の対称サンプリング。
    float3 totalColor  = kernel[0].rgb * colorCenter.rgb;
    float3 totalWeight = kernel[0].rgb;

    float2 finalStep = sssWidth * texelSize * direction;

    [unroll]
    for (int i = 1; i < 17; ++i)
    {
        float2 offset = kernel[i].a * finalStep;

        float2 uvPos = input.uv + offset;
        float2 uvNeg = input.uv - offset;

        float3 colorPos = sceneTexture.Sample(linearClampSampler, uvPos).rgb;
        float3 colorNeg = sceneTexture.Sample(linearClampSampler, uvNeg).rgb;

        float depthPos = depthTexture.Sample(linearClampSampler, uvPos).r;
        float depthNeg = depthTexture.Sample(linearClampSampler, uvNeg).r;

        float diffPos = abs(depthPos - depthCenter);
        float diffNeg = abs(depthNeg - depthCenter);

        float wPos = (diffPos < maxDepthDiff) ? 1.0 : 0.0;
        float wNeg = (diffNeg < maxDepthDiff) ? 1.0 : 0.0;

        totalColor  += kernel[i].rgb * (colorPos * wPos + colorNeg * wNeg);
        totalWeight += kernel[i].rgb * (wPos + wNeg);
    }

    return float4(totalColor / max(totalWeight, float3(0.001, 0.001, 0.001)),
                  colorCenter.a);
}
)hlsl";

// ============================================================================
// SSS設定
// ============================================================================

/// @brief SSSエフェクト設定
struct SSSConfig
{
	float sssWidth = 0.012f;        ///< スクリーンスペース散乱幅
	float maxDepthDiff = 0.01f;     ///< 深度差閾値
	float correction = 80.0f;       ///< 深度補正係数
	bool enabled = true;            ///< エフェクト有効フラグ
};

/// @brief SSSプロファイル種別
enum class SSSProfile : int
{
	Skin = 0,     ///< 肌（ピンク寄りの散乱）
	Marble,       ///< 大理石（白色散乱）
	Wax,          ///< 蝋（黄色寄りの散乱）
	Milk,         ///< 牛乳（強い白色散乱）
};

// ============================================================================
// Skinプロファイル — Jimenez 2009 6mmスキン拡散プロファイル相当
// ============================================================================

/// @brief Skinプロファイル 17タップカーネル（float4: RGB重み + オフセット）
// カーネルフォーマット (Jimenez 2009 SSS, 17-tap):
//   .rgb = チャンネルごとの重み寄与
//   .a   = ブラー方向に沿った空間オフセット (center = 0)
static constexpr std::array<std::array<float, 4>, 17> SKIN_KERNEL_17 = {{
	{{0.530605f, 0.613514f, 0.739601f,  0.0f}},
	{{0.000973f, 0.001138f, 0.001999f, -2.0f}},
	{{0.001000f, 0.001500f, 0.005000f, -1.7775f}},
	{{0.005000f, 0.011500f, 0.022500f, -1.5556f}},
	{{0.022500f, 0.025000f, 0.037500f, -1.3333f}},
	{{0.037500f, 0.057500f, 0.085000f, -1.1111f}},
	{{0.087500f, 0.135000f, 0.165000f, -0.8889f}},
	{{0.140000f, 0.210000f, 0.275000f, -0.6667f}},
	{{0.140000f, 0.225000f, 0.290000f, -0.4444f}},
	{{0.140000f, 0.225000f, 0.290000f,  0.4444f}},
	{{0.140000f, 0.210000f, 0.275000f,  0.6667f}},
	{{0.087500f, 0.135000f, 0.165000f,  0.8889f}},
	{{0.037500f, 0.057500f, 0.085000f,  1.1111f}},
	{{0.022500f, 0.025000f, 0.037500f,  1.3333f}},
	{{0.005000f, 0.011500f, 0.022500f,  1.5556f}},
	{{0.001000f, 0.001500f, 0.005000f,  1.7775f}},
	{{0.000973f, 0.001138f, 0.001999f,  2.0f}},
}};

// ============================================================================
// SSSエフェクトクラス
// ============================================================================

/// @brief サブサーフェススキャタリングポストプロセスエフェクト
/// @details DX11デバイスを使用してSSSブラーを適用する。
///          水平→垂直の2パス分離可能ブラーを実行する。
class SubsurfaceScatterEffect
{
public:
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief コンストラクタ
	SubsurfaceScatterEffect() = default;

	/// @brief デストラクタ
	~SubsurfaceScatterEffect() = default;

	/// コピー禁止
	SubsurfaceScatterEffect(const SubsurfaceScatterEffect&) = delete;
	SubsurfaceScatterEffect& operator=(const SubsurfaceScatterEffect&) = delete;

	/// ムーブ許可
	SubsurfaceScatterEffect(SubsurfaceScatterEffect&&) noexcept = default;
	SubsurfaceScatterEffect& operator=(SubsurfaceScatterEffect&&) noexcept = default;

	/// @brief SSSエフェクトを初期化する
	/// @param device D3D11デバイス
	/// @param width レンダーターゲット幅
	/// @param height レンダーターゲット高さ
	/// @return 初期化成功でtrue
	bool init(ID3D11Device* device, int width, int height)
	{
		if (!device || width <= 0 || height <= 0)
		{
			return false;
		}
		m_width = width;
		m_height = height;

		try
		{
			m_vs = compileFullscreenVS(device);
			m_ps = compileSSS_PS(device);
			m_cb = createConstantBuffer(device,
				static_cast<std::uint32_t>(sizeof(SSSCB)));
			m_pingRT = createRenderTarget(
				device,
				static_cast<std::uint32_t>(width),
				static_cast<std::uint32_t>(height),
				DXGI_FORMAT_R8G8B8A8_UNORM);
			m_sampler = createLinearClampSampler(device);
		}
		catch (...)
		{
			return false;
		}

		return m_vs && m_ps && m_cb && m_sampler &&
		       m_pingRT.texture && m_pingRT.srv && m_pingRT.rtv;
	}

	/// @brief SSSブラーを適用する
	/// @param context D3D11デバイスコンテキスト
	/// @param sceneSRV シーンカラーテクスチャSRV
	/// @param depthSRV 深度テクスチャSRV
	/// @param outputRTV 出力レンダーターゲットビュー
	void apply(ID3D11DeviceContext* context,
	           ID3D11ShaderResourceView* sceneSRV,
	           ID3D11ShaderResourceView* depthSRV,
	           ID3D11RenderTargetView* outputRTV) const
	{
		if (!m_config.enabled || !context || !sceneSRV ||
		    !depthSRV || !outputRTV)
		{
			return;
		}

		const auto& kernel = getKernel();
		const float texW = static_cast<float>(m_width);
		const float texH = static_cast<float>(m_height);

		// 水平パス: scene → ping RT
		runBlurPass(context, sceneSRV, depthSRV,
			m_pingRT.rtv.Get(), 1.0f, 0.0f,
			texW, texH, kernel);

		// 垂直パス: ping RT → outputRTV
		runBlurPass(context, m_pingRT.srv.Get(), depthSRV,
			outputRTV, 0.0f, 1.0f,
			texW, texH, kernel);
	}

	/// @brief SSSプロファイルを設定する
	/// @param profile SSSプロファイル種別
	void setProfile(SSSProfile profile) noexcept
	{
		m_profile = profile;
	}

	/// @brief 設定を取得する
	[[nodiscard]] SSSConfig& config() noexcept { return m_config; }

	/// @brief 設定を取得する（const版）
	[[nodiscard]] const SSSConfig& config() const noexcept { return m_config; }

	/// @brief 中間RTのSRVを取得する（テスト用）
	[[nodiscard]] ID3D11ShaderResourceView* pingRTSRV() const noexcept
	{
		return m_pingRT.srv.Get();
	}

private:
	// ── 定数バッファレイアウト ─────────────────────────

	/// @brief SSSシェーダー定数バッファ（16バイトアライン）
	struct SSSCB
	{
		float direction[2];      ///< ブラー方向 (1,0) or (0,1)
		float sssWidth;          ///< 散乱幅
		float correction;        ///< 深度補正係数
		float kernel[17][4];     ///< カーネルテーブル (float4 x 17)
		float texelSize[2];      ///< 1/textureSize
		float maxDepthDiff;      ///< 最大深度差閾値
		float padding;           ///< パディング
	};

	// ── シェーダーコンパイル ────────────────────────────

	/// @brief SSS_BLUR_PSをコンパイルする（エントリーポイント: main）
	[[nodiscard]] static ComPtr<ID3D11PixelShader>
	compileSSS_PS(ID3D11Device* device)
	{
		return compilePostProcessPS(device, SSS_BLUR_PS, "main");
	}

	// ── カーネル取得 ────────────────────────────────────

	/// @brief 現在のプロファイルのカーネルを返す
	[[nodiscard]] const std::array<std::array<float, 4>, 17>&
	getKernel() const noexcept
	{
		// 現状 Skin のみ実装。他プロファイルは 3-A.4 で追加予定。
		return SKIN_KERNEL_17;
	}

	// ── パス実行 ────────────────────────────────────────

	/// @brief ブラーパスを1回実行する
	void runBlurPass(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* srcSRV,
		ID3D11ShaderResourceView* depthSRV,
		ID3D11RenderTargetView* dstRTV,
		float dirX, float dirY,
		float texW, float texH,
		const std::array<std::array<float, 4>, 17>& kernel) const
	{
		SSSCB cbData = {};
		cbData.direction[0] = dirX;
		cbData.direction[1] = dirY;
		cbData.sssWidth     = m_config.sssWidth;
		cbData.correction   = m_config.correction;
		cbData.texelSize[0] = 1.0f / texW;
		cbData.texelSize[1] = 1.0f / texH;
		cbData.maxDepthDiff = m_config.maxDepthDiff;
		cbData.padding      = 0.0f;
		for (int i = 0; i < 17; ++i)
		{
			cbData.kernel[i][0] = kernel[i][0];
			cbData.kernel[i][1] = kernel[i][1];
			cbData.kernel[i][2] = kernel[i][2];
			cbData.kernel[i][3] = kernel[i][3];
		}
		updateConstantBuffer(context, m_cb.Get(),
			&cbData, static_cast<std::uint32_t>(sizeof(cbData)));

		D3D11_VIEWPORT vp = {};
		vp.Width    = texW;
		vp.Height   = texH;
		vp.MaxDepth = 1.0f;
		context->RSSetViewports(1, &vp);

		context->OMSetRenderTargets(1, &dstRTV, nullptr);
		context->VSSetShader(m_vs.Get(), nullptr, 0);
		context->PSSetShader(m_ps.Get(), nullptr, 0);
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		ID3D11ShaderResourceView* srvs[2] = {srcSRV, depthSRV};
		context->PSSetShaderResources(0, 2, srvs);

		auto* samplerPtr = m_sampler.Get();
		context->PSSetSamplers(0, 1, &samplerPtr);

		auto* cbPtr = m_cb.Get();
		context->PSSetConstantBuffers(0, 1, &cbPtr);

		context->Draw(3, 0);

		// SRVバインドをクリアする
		ID3D11ShaderResourceView* nullSRVs[2] = {nullptr, nullptr};
		context->PSSetShaderResources(0, 2, nullSRVs);
	}

	// ── メンバー変数 ────────────────────────────────────

	SSSConfig   m_config;
	SSSProfile  m_profile = SSSProfile::Skin;
	int         m_width   = 0;
	int         m_height  = 0;

	ComPtr<ID3D11VertexShader> m_vs;
	ComPtr<ID3D11PixelShader>  m_ps;
	ComPtr<ID3D11Buffer>       m_cb;
	ComPtr<ID3D11SamplerState> m_sampler;
	PostProcessRT              m_pingRT;
};

} // namespace mitiru::render

#endif // _WIN32
