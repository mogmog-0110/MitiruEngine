#pragma once

/// @file MotionVectorPass.hpp
/// @brief GPU モーションベクタ（velocity）パス — 画面内移動量を RT に焼く
/// @details 各メッシュを現フレーム / 前フレームの MVP で射影し、screen-space の移動量
///          `velocity = curUV - prevUV`（UV 単位）を RG16F の RT に書き出す。`TAAEffect::setVelocitySRV`
///          に `velocitySRV()` を渡すと、TAA が `historyUV = uv - velocity` で reproject して
///          動く物体のゴースト/にじみを抑える（NPR inbox #3 の「GPU が velocity RT を出す側」）。
///
/// CPU software deferred (#7) は GBuffer.velocity を書くが、GPU ランタイム（`Renderer3D`）には
/// motion-vector パスが無かった。本パスがその欠けていた半分を埋める。行列規約は `Renderer3D` と
/// 同一（`GlmBridge` の `toGlm`/`toHLSL` で row-major 化、VS は `mul(vector, matrix)`）。
///
/// @code
/// mitiru::render::MotionVectorPass mv;
/// mv.init(device, w, h);
/// mv.setCamera(curView, curProj, prevView, prevProj);
/// mv.begin(ctx);
/// for (each mesh) mv.drawMesh(ctx, vb, ib, indexCount, curWorld, prevWorld);
/// mv.end(ctx);
/// taa.setVelocitySRV(mv.velocitySRV());   // TAA が消費
/// @endcode
///
/// @note GPU バックエンド有り環境での実機 smoke が前提（headless CI では shader 文字列 +
///       C++ API の compile-check のみ）。

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

#include <sgc/math/Mat4.hpp>

#include <mitiru/render/GlmBridge.hpp>
#include <mitiru/render/Vertex3D.hpp>

#pragma comment(lib, "d3dcompiler.lib")

namespace mitiru::render
{

/// @brief モーションベクタ VS/PS（HLSL SM5.0、行列規約は Renderer3D と同一）
constexpr std::string_view MOTION_VECTOR_VS = R"hlsl(
cbuffer MVTransform : register(b0)
{
	float4x4 CurWorld;  float4x4 CurView;  float4x4 CurProj;
	float4x4 PrevWorld; float4x4 PrevView; float4x4 PrevProj;
};
struct VSIn  { float3 Position : POSITION; };
struct VSOut { float4 Position : SV_POSITION; float4 CurClip : TEXCOORD0; float4 PrevClip : TEXCOORD1; };
VSOut VSMain(VSIn i)
{
	VSOut o;
	float4 wc = mul(float4(i.Position, 1.0), CurWorld);
	float4 vc = mul(wc, CurView);
	o.CurClip = mul(vc, CurProj);
	o.Position = o.CurClip;
	float4 wp = mul(float4(i.Position, 1.0), PrevWorld);
	float4 vp = mul(wp, PrevView);
	o.PrevClip = mul(vp, PrevProj);
	return o;
}
)hlsl";

/// @brief 変形メッシュ用 VS（prev 位置を別ストリーム slot1 から読む。#21a / GPU 版 #18）
/// @details cur 位置(slot0) と prev 位置(slot1) は同トポロジの別頂点バッファ。剛体変換だけでは
///          出せない「world 不変でも頂点が動いた分」の velocity を出す（クロス/スキニング/モーフ）。
constexpr std::string_view MOTION_VECTOR_DEFORM_VS = R"hlsl(
cbuffer MVTransform : register(b0)
{
	float4x4 CurWorld;  float4x4 CurView;  float4x4 CurProj;
	float4x4 PrevWorld; float4x4 PrevView; float4x4 PrevProj;
};
struct VSIn  { float3 Position : POSITION; float3 PrevPosition : PREVPOSITION; };
struct VSOut { float4 Position : SV_POSITION; float4 CurClip : TEXCOORD0; float4 PrevClip : TEXCOORD1; };
VSOut VSMain(VSIn i)
{
	VSOut o;
	float4 wc = mul(float4(i.Position, 1.0), CurWorld);
	float4 vc = mul(wc, CurView);
	o.CurClip = mul(vc, CurProj);
	o.Position = o.CurClip;
	// prev は別ストリームの前フレーム頂点位置を使う（剛体なら Position と同値を渡せば velocity 0）。
	float4 wp = mul(float4(i.PrevPosition, 1.0), PrevWorld);
	float4 vp = mul(wp, PrevView);
	o.PrevClip = mul(vp, PrevProj);
	return o;
}
)hlsl";

constexpr std::string_view MOTION_VECTOR_PS = R"hlsl(
struct VSOut { float4 Position : SV_POSITION; float4 CurClip : TEXCOORD0; float4 PrevClip : TEXCOORD1; };
float2 PSMain(VSOut i) : SV_TARGET
{
	float2 curNDC  = i.CurClip.xy  / i.CurClip.w;
	float2 prevNDC = i.PrevClip.xy / i.PrevClip.w;
	// NDC -> UV（DX の y 反転）。
	float2 curUV  = float2(curNDC.x  * 0.5 + 0.5, curNDC.y  * -0.5 + 0.5);
	float2 prevUV = float2(prevNDC.x * 0.5 + 0.5, prevNDC.y * -0.5 + 0.5);
	// velocity（UV 単位）。TAA は historyUV = uv - velocity で前フレーム位置 prevUV を引く。
	return curUV - prevUV;
}
)hlsl";

class MotionVectorPass
{
public:
	template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief パスを初期化する（velocity RT + 深度 + シェーダ）。
	void init(ID3D11Device* device, std::uint32_t width, std::uint32_t height)
	{
		if (device == nullptr) { throw std::runtime_error("MotionVectorPass: null device"); }
		m_device = device;
		m_width = width;
		m_height = height;

		m_vs = compileVS(device, MOTION_VECTOR_VS, m_inputLayout);
		m_vsDeform = compileDeformVS(device, MOTION_VECTOR_DEFORM_VS, m_inputLayoutDeform);
		m_ps = compilePS(device, MOTION_VECTOR_PS);
		m_cb = createCB(device, sizeof(CbMV));
		m_velocityRT = createRT(device, width, height, DXGI_FORMAT_R16G16_FLOAT);
		m_depth = createDepth(device, width, height);

		// 明示的なパイプライン状態（呼び出し元の残留状態に依存しない）。
		// 両面描画（velocity は表裏問わず必要）+ 深度テスト LESS で最近接を残す。
		D3D11_RASTERIZER_DESC rs = {};
		rs.FillMode = D3D11_FILL_SOLID;
		rs.CullMode = D3D11_CULL_NONE;
		rs.DepthClipEnable = TRUE;
		if (FAILED(device->CreateRasterizerState(&rs, m_rasterState.GetAddressOf())))
		{
			throw std::runtime_error("MotionVectorPass: CreateRasterizerState failed");
		}
		D3D11_DEPTH_STENCIL_DESC ds = {};
		ds.DepthEnable = TRUE;
		ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		ds.DepthFunc = D3D11_COMPARISON_LESS;
		if (FAILED(device->CreateDepthStencilState(&ds, m_depthState.GetAddressOf())))
		{
			throw std::runtime_error("MotionVectorPass: CreateDepthStencilState failed");
		}
	}

	/// @brief カメラ行列を設定する（cur / prev）。drawMesh 前に毎フレーム呼ぶ。
	void setCamera(const sgc::Mat4f& curView, const sgc::Mat4f& curProj,
	               const sgc::Mat4f& prevView, const sgc::Mat4f& prevProj) noexcept
	{
		m_curView = curView; m_curProj = curProj;
		m_prevView = prevView; m_prevProj = prevProj;
	}

	/// @brief velocity RT をバインドして 0 クリアし、パイプラインを MV 用に設定する。
	void begin(ID3D11DeviceContext* ctx)
	{
		const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		ctx->ClearRenderTargetView(m_velocityRT.rtv.Get(), zero);
		ctx->ClearDepthStencilView(m_depth.dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
		ID3D11RenderTargetView* rtv = m_velocityRT.rtv.Get();
		ctx->OMSetRenderTargets(1, &rtv, m_depth.dsv.Get());

		D3D11_VIEWPORT vp = {};
		vp.Width = static_cast<float>(m_width);
		vp.Height = static_cast<float>(m_height);
		vp.MaxDepth = 1.0f;
		ctx->RSSetViewports(1, &vp);

		ctx->RSSetState(m_rasterState.Get());
		ctx->OMSetDepthStencilState(m_depthState.Get(), 0);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->PSSetShader(m_ps.Get(), nullptr, 0);
		// VS / input layout は draw 側で設定する（剛体 / 変形を 1 begin 内で混在できるように）。
	}

	/// @brief 1 メッシュを velocity RT へ描く。
	/// @param vertexBuffer Vertex3D レイアウトの頂点バッファ
	/// @param indexBuffer  R32_UINT インデックスバッファ
	/// @param indexCount   インデックス数
	/// @param curWorld     現フレームのワールド行列
	/// @param prevWorld    前フレームのワールド行列（無ければ curWorld を渡す → velocity 0）
	void drawMesh(ID3D11DeviceContext* ctx,
	              ID3D11Buffer* vertexBuffer, ID3D11Buffer* indexBuffer,
	              std::uint32_t indexCount,
	              const sgc::Mat4f& curWorld, const sgc::Mat4f& prevWorld)
	{
		CbMV cb;
		toHLSL(cb.curWorld, toGlm(curWorld));
		toHLSL(cb.curView, toGlm(m_curView));
		toHLSL(cb.curProj, toGlm(m_curProj));
		toHLSL(cb.prevWorld, toGlm(prevWorld));
		toHLSL(cb.prevView, toGlm(m_prevView));
		toHLSL(cb.prevProj, toGlm(m_prevProj));

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (SUCCEEDED(ctx->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			std::memcpy(mapped.pData, &cb, sizeof(cb));
			ctx->Unmap(m_cb.Get(), 0);
		}
		ID3D11Buffer* cbuf = m_cb.Get();
		ctx->VSSetConstantBuffers(0, 1, &cbuf);

		// 剛体 VS / 1 ストリーム layout（変形 draw と混在しても安全なよう毎回設定）。
		ctx->IASetInputLayout(m_inputLayout.Get());
		ctx->VSSetShader(m_vs.Get(), nullptr, 0);

		const UINT stride = sizeof(Vertex3D), offset = 0;
		ctx->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
		ctx->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);
		ctx->DrawIndexed(indexCount, 0, 0);
	}

	/// @brief 変形メッシュを velocity RT へ描く（#21a / GPU 版 #18）。
	/// @details cur / prev は同トポロジの別頂点バッファ（どちらも Vertex3D レイアウト）。VS が
	///          slot1 から前フレーム頂点位置を読み、world 不変でも頂点が動いた分の velocity を出す。
	///          CPU `DeferredPipeline::prevMesh` と同じ意味論。
	/// @param curVertexBuffer  現フレームの変形済み頂点（Vertex3D）
	/// @param prevVertexBuffer 前フレームの同トポロジ頂点（Vertex3D。無ければ curVertexBuffer を渡す → velocity 0）
	/// @param indexBuffer R32_UINT インデックス（cur / prev で共有）
	void drawMeshDeforming(ID3D11DeviceContext* ctx,
	                       ID3D11Buffer* curVertexBuffer, ID3D11Buffer* prevVertexBuffer,
	                       ID3D11Buffer* indexBuffer, std::uint32_t indexCount,
	                       const sgc::Mat4f& curWorld, const sgc::Mat4f& prevWorld)
	{
		CbMV cb;
		toHLSL(cb.curWorld, toGlm(curWorld));
		toHLSL(cb.curView, toGlm(m_curView));
		toHLSL(cb.curProj, toGlm(m_curProj));
		toHLSL(cb.prevWorld, toGlm(prevWorld));
		toHLSL(cb.prevView, toGlm(m_prevView));
		toHLSL(cb.prevProj, toGlm(m_prevProj));

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (SUCCEEDED(ctx->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			std::memcpy(mapped.pData, &cb, sizeof(cb));
			ctx->Unmap(m_cb.Get(), 0);
		}
		ID3D11Buffer* cbuf = m_cb.Get();
		ctx->VSSetConstantBuffers(0, 1, &cbuf);

		// 変形 VS / 2 ストリーム layout（slot0=現位置, slot1=前位置）。
		ctx->IASetInputLayout(m_inputLayoutDeform.Get());
		ctx->VSSetShader(m_vsDeform.Get(), nullptr, 0);

		ID3D11Buffer* vbs[2] = {curVertexBuffer, prevVertexBuffer};
		const UINT strides[2] = {sizeof(Vertex3D), sizeof(Vertex3D)};
		const UINT offsets[2] = {0, 0};
		ctx->IASetVertexBuffers(0, 2, vbs, strides, offsets);
		ctx->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);
		ctx->DrawIndexed(indexCount, 0, 0);
	}

	/// @brief パス終了（RT を解除）。
	void end(ID3D11DeviceContext* ctx)
	{
		ID3D11RenderTargetView* nullRtv = nullptr;
		ctx->OMSetRenderTargets(1, &nullRtv, nullptr);
	}

	/// @brief velocity RT の SRV（TAAEffect::setVelocitySRV に渡す）。
	[[nodiscard]] ID3D11ShaderResourceView* velocitySRV() const noexcept
	{
		return m_velocityRT.srv.Get();
	}

private:
	struct alignas(16) CbMV
	{
		float curWorld[4][4]{};  float curView[4][4]{};  float curProj[4][4]{};
		float prevWorld[4][4]{}; float prevView[4][4]{}; float prevProj[4][4]{};
	};

	struct RTData
	{
		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11RenderTargetView> rtv;
	};
	struct DepthData
	{
		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11DepthStencilView> dsv;
	};

	[[nodiscard]] static ComPtr<ID3D11VertexShader> compileVS(
		ID3D11Device* device, std::string_view src, ComPtr<ID3D11InputLayout>& layoutOut)
	{
		ComPtr<ID3DBlob> blob, err;
		if (FAILED(D3DCompile(src.data(), src.size(), nullptr, nullptr, nullptr,
		                      "VSMain", "vs_5_0", 0, 0, blob.GetAddressOf(), err.GetAddressOf())))
		{
			throw std::runtime_error("MotionVectorPass: VS compile failed");
		}
		ComPtr<ID3D11VertexShader> vs;
		if (FAILED(device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(),
		                                      nullptr, vs.GetAddressOf())))
		{
			throw std::runtime_error("MotionVectorPass: CreateVertexShader failed");
		}
		// POSITION のみ読む（頂点バッファは Vertex3D stride）。
		const D3D11_INPUT_ELEMENT_DESC layout[] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}};
		if (FAILED(device->CreateInputLayout(layout, 1, blob->GetBufferPointer(),
		                                     blob->GetBufferSize(), layoutOut.GetAddressOf())))
		{
			throw std::runtime_error("MotionVectorPass: CreateInputLayout failed");
		}
		return vs;
	}

	/// @brief 変形 VS をコンパイルし 2 ストリーム input layout を作る（slot0=POSITION, slot1=PREVPOSITION）。
	[[nodiscard]] static ComPtr<ID3D11VertexShader> compileDeformVS(
		ID3D11Device* device, std::string_view src, ComPtr<ID3D11InputLayout>& layoutOut)
	{
		ComPtr<ID3DBlob> blob, err;
		if (FAILED(D3DCompile(src.data(), src.size(), nullptr, nullptr, nullptr,
		                      "VSMain", "vs_5_0", 0, 0, blob.GetAddressOf(), err.GetAddressOf())))
		{
			throw std::runtime_error("MotionVectorPass: deform VS compile failed");
		}
		ComPtr<ID3D11VertexShader> vs;
		if (FAILED(device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(),
		                                      nullptr, vs.GetAddressOf())))
		{
			throw std::runtime_error("MotionVectorPass: deform CreateVertexShader failed");
		}
		// slot0: 現位置（Vertex3D の POSITION）/ slot1: 前位置（別 Vertex3D バッファの POSITION を PREVPOSITION として読む）。
		const D3D11_INPUT_ELEMENT_DESC layout[] = {
			{"POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"PREVPOSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}};
		if (FAILED(device->CreateInputLayout(layout, 2, blob->GetBufferPointer(),
		                                     blob->GetBufferSize(), layoutOut.GetAddressOf())))
		{
			throw std::runtime_error("MotionVectorPass: deform CreateInputLayout failed");
		}
		return vs;
	}

	[[nodiscard]] static ComPtr<ID3D11PixelShader> compilePS(
		ID3D11Device* device, std::string_view src)
	{
		ComPtr<ID3DBlob> blob, err;
		if (FAILED(D3DCompile(src.data(), src.size(), nullptr, nullptr, nullptr,
		                      "PSMain", "ps_5_0", 0, 0, blob.GetAddressOf(), err.GetAddressOf())))
		{
			throw std::runtime_error("MotionVectorPass: PS compile failed");
		}
		ComPtr<ID3D11PixelShader> ps;
		if (FAILED(device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(),
		                                     nullptr, ps.GetAddressOf())))
		{
			throw std::runtime_error("MotionVectorPass: CreatePixelShader failed");
		}
		return ps;
	}

	[[nodiscard]] static ComPtr<ID3D11Buffer> createCB(ID3D11Device* device, std::uint32_t bytes)
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = bytes;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		ComPtr<ID3D11Buffer> cb;
		if (FAILED(device->CreateBuffer(&desc, nullptr, cb.GetAddressOf())))
		{
			throw std::runtime_error("MotionVectorPass: CreateBuffer(CB) failed");
		}
		return cb;
	}

	[[nodiscard]] static RTData createRT(ID3D11Device* device, std::uint32_t w,
	                                     std::uint32_t h, DXGI_FORMAT format)
	{
		RTData rt;
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
		desc.Format = format; desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		if (FAILED(device->CreateTexture2D(&desc, nullptr, rt.texture.GetAddressOf())))
		{
			throw std::runtime_error("MotionVectorPass: CreateTexture2D failed");
		}
		if (FAILED(device->CreateShaderResourceView(rt.texture.Get(), nullptr, rt.srv.GetAddressOf())))
		{
			throw std::runtime_error("MotionVectorPass: CreateSRV failed");
		}
		if (FAILED(device->CreateRenderTargetView(rt.texture.Get(), nullptr, rt.rtv.GetAddressOf())))
		{
			throw std::runtime_error("MotionVectorPass: CreateRTV failed");
		}
		return rt;
	}

	[[nodiscard]] static DepthData createDepth(ID3D11Device* device, std::uint32_t w, std::uint32_t h)
	{
		DepthData d;
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_D32_FLOAT; desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		if (FAILED(device->CreateTexture2D(&desc, nullptr, d.texture.GetAddressOf())))
		{
			throw std::runtime_error("MotionVectorPass: CreateDepthTexture failed");
		}
		if (FAILED(device->CreateDepthStencilView(d.texture.Get(), nullptr, d.dsv.GetAddressOf())))
		{
			throw std::runtime_error("MotionVectorPass: CreateDSV failed");
		}
		return d;
	}

	ComPtr<ID3D11Device> m_device;
	ComPtr<ID3D11VertexShader> m_vs;
	ComPtr<ID3D11VertexShader> m_vsDeform;      // #21a: 変形メッシュ用（prevPosition 別ストリーム）
	ComPtr<ID3D11PixelShader> m_ps;
	ComPtr<ID3D11InputLayout> m_inputLayout;
	ComPtr<ID3D11InputLayout> m_inputLayoutDeform;
	ComPtr<ID3D11Buffer> m_cb;
	RTData m_velocityRT;
	DepthData m_depth;
	ComPtr<ID3D11RasterizerState> m_rasterState;
	ComPtr<ID3D11DepthStencilState> m_depthState;
	std::uint32_t m_width = 0, m_height = 0;
	sgc::Mat4f m_curView, m_curProj, m_prevView, m_prevProj;
};

} // namespace mitiru::render

#endif // _WIN32
