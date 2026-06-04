#pragma once

/// @file GeometryPass3D.hpp
/// @brief GPU ジオメトリパス — depth / world-normal / objectId を MRT に焼く（NPR 輪郭線の前段）
/// @details Scene3D の各メッシュを GPU で 1 パス描画し、(0) 深度 R32_FLOAT、(1) ワールド法線
///          RGBA16F、(2) objectId R32_UINT を MRT に出力する。これが GPU 版輪郭抽出
///          （`ContourDetect` 相当の fullscreen PS、別パス）の入力になる。CPU software deferred
///          (`DeferredPipeline`) が CPU GBuffer に書く depth/normal/objectId の GPU 対応物で、
///          NPR inbox #20 (a) で欠けていた「GPU 深度/法線/objectId ターゲット」を埋める。
///
/// 深度規約は CPU `DeferredPipeline` と同一 — NDC z を `ndcZ * 0.5 + 0.5` で [0,1] に写す
/// （`(ca.z/ca.w + 1) * 0.5` と等価）。これにより `ContourDetect` の depthThreshold が
/// CPU/GPU で同じ意味を持つ。行列規約は `MotionVectorPass`/`Renderer3D` と同一（row-major、
/// VS は `mul(vector, matrix)`）。
///
/// @code
/// mitiru::render::GeometryPass3D gp;
/// gp.init(device, w, h);
/// gp.setCamera(view, proj);
/// gp.begin(ctx);
/// for (each mesh) gp.drawMesh(ctx, vb, ib, indexCount, world, nodeId + 1);
/// gp.end(ctx);
/// // gp.depthSRV() / gp.normalSRV() / gp.objectIdSRV() を輪郭抽出パスへ渡す
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
#include <cstring>
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

/// @brief ジオメトリ VS/PS（HLSL SM5.0、行列規約は Renderer3D と同一）
constexpr std::string_view GEOMETRY_PASS_VS = R"hlsl(
cbuffer GeoTransform : register(b0)
{
	float4x4 World; float4x4 View; float4x4 Proj;
	uint ObjectId; float3 _pad;
};
struct VSIn  { float3 Position : POSITION; float3 Normal : NORMAL; };
struct VSOut { float4 Position : SV_POSITION; float3 WorldNormal : TEXCOORD0; };
VSOut VSMain(VSIn i)
{
	VSOut o;
	float4 wc = mul(float4(i.Position, 1.0), World);
	float4 vc = mul(wc, View);
	o.Position = mul(vc, Proj);
	// 法線は World の回転成分で変換（非一様スケール無し前提、Renderer3D と同様）。
	o.WorldNormal = normalize(mul(float4(i.Normal, 0.0), World).xyz);
	return o;
}
)hlsl";

constexpr std::string_view GEOMETRY_PASS_PS = R"hlsl(
cbuffer GeoTransform : register(b0)
{
	float4x4 World; float4x4 View; float4x4 Proj;
	uint ObjectId; float3 _pad;
};
struct VSOut { float4 Position : SV_POSITION; float3 WorldNormal : TEXCOORD0; };
struct PSOut
{
	float  Depth    : SV_TARGET0; // [0,1]（CPU DeferredPipeline と同一規約）
	float4 Normal   : SV_TARGET1; // ワールド法線（[-1,1] 生値）
	uint   ObjectId : SV_TARGET2; // 0 = 背景、それ以外 = nodeId+1 等
};
PSOut PSMain(VSOut i)
{
	PSOut o;
	// SV_POSITION.z は DX の [0,1] デバイス深度。CPU は (ndcZ+1)*0.5 = z だが、
	// CPU projection が GL 規約(z in [-1,1])のため明示的に再現する。
	o.Depth    = i.Position.z;
	o.Normal   = float4(normalize(i.WorldNormal), 1.0);
	o.ObjectId = ObjectId;
	return o;
}
)hlsl";

class GeometryPass3D
{
public:
	template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief パスを初期化する（depth/normal/objectId RT + 深度バッファ + シェーダ）。
	void init(ID3D11Device* device, std::uint32_t width, std::uint32_t height)
	{
		if (device == nullptr) { throw std::runtime_error("GeometryPass3D: null device"); }
		m_device = device;
		m_width = width;
		m_height = height;

		m_vs = compileVS(device, GEOMETRY_PASS_VS, m_inputLayout);
		m_ps = compilePS(device, GEOMETRY_PASS_PS);
		m_cb = createCB(device, sizeof(CbGeo));
		m_depthRT    = createRT(device, width, height, DXGI_FORMAT_R32_FLOAT);
		m_normalRT   = createRT(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
		m_objectIdRT = createRT(device, width, height, DXGI_FORMAT_R32_UINT);
		m_depth = createDepth(device, width, height);

		// 明示的なパイプライン状態（呼び出し元の残留状態に依存しない）。
		D3D11_RASTERIZER_DESC rs = {};
		rs.FillMode = D3D11_FILL_SOLID;
		rs.CullMode = D3D11_CULL_BACK;
		rs.FrontCounterClockwise = FALSE;
		rs.DepthClipEnable = TRUE;
		if (FAILED(device->CreateRasterizerState(&rs, m_rasterState.GetAddressOf())))
		{
			throw std::runtime_error("GeometryPass3D: CreateRasterizerState failed");
		}
		D3D11_DEPTH_STENCIL_DESC ds = {};
		ds.DepthEnable = TRUE;
		ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		ds.DepthFunc = D3D11_COMPARISON_LESS;
		if (FAILED(device->CreateDepthStencilState(&ds, m_depthState.GetAddressOf())))
		{
			throw std::runtime_error("GeometryPass3D: CreateDepthStencilState failed");
		}
	}

	/// @brief カメラ行列を設定する。drawMesh 前に毎フレーム呼ぶ。
	void setCamera(const sgc::Mat4f& view, const sgc::Mat4f& proj) noexcept
	{
		m_view = view; m_proj = proj;
	}

	/// @brief 全 RT をクリアしてバインドし、パイプラインを geometry 用に設定する。
	/// @details depth=1（最遠）/ normal=0 / objectId=0（背景）でクリアする。
	void begin(ID3D11DeviceContext* ctx)
	{
		const float far1[4]  = {1.0f, 1.0f, 1.0f, 1.0f};
		const float zero4[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		const UINT  zeroU[4] = {0u, 0u, 0u, 0u};
		ctx->ClearRenderTargetView(m_depthRT.rtv.Get(), far1);
		ctx->ClearRenderTargetView(m_normalRT.rtv.Get(), zero4);
		// R32_UINT への ClearRenderTargetView は float[4] のビット列を書き込む。
		// 全ゼロ float = 全ゼロビット = uint 0（背景）なので objectId クリアに使える。
		ctx->ClearRenderTargetView(m_objectIdRT.rtv.Get(), reinterpret_cast<const float*>(zeroU));
		ctx->ClearDepthStencilView(m_depth.dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

		ID3D11RenderTargetView* rtvs[3] = {
			m_depthRT.rtv.Get(), m_normalRT.rtv.Get(), m_objectIdRT.rtv.Get()};
		ctx->OMSetRenderTargets(3, rtvs, m_depth.dsv.Get());

		D3D11_VIEWPORT vp = {};
		vp.Width = static_cast<float>(m_width);
		vp.Height = static_cast<float>(m_height);
		vp.MaxDepth = 1.0f;
		ctx->RSSetViewports(1, &vp);

		ctx->RSSetState(m_rasterState.Get());
		ctx->OMSetDepthStencilState(m_depthState.Get(), 0);
		ctx->IASetInputLayout(m_inputLayout.Get());
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->VSSetShader(m_vs.Get(), nullptr, 0);
		ctx->PSSetShader(m_ps.Get(), nullptr, 0);
	}

	/// @brief 1 メッシュを MRT へ描く。
	/// @param vertexBuffer Vertex3D レイアウトの頂点バッファ
	/// @param indexBuffer  R32_UINT インデックスバッファ
	/// @param indexCount   インデックス数
	/// @param world        ワールド行列
	/// @param objectId     このメッシュの objectId（0 は背景予約、通常 nodeId+1）
	void drawMesh(ID3D11DeviceContext* ctx,
	              ID3D11Buffer* vertexBuffer, ID3D11Buffer* indexBuffer,
	              std::uint32_t indexCount,
	              const sgc::Mat4f& world, std::uint32_t objectId)
	{
		CbGeo cb;
		toHLSL(cb.world, toGlm(world));
		toHLSL(cb.view, toGlm(m_view));
		toHLSL(cb.proj, toGlm(m_proj));
		cb.objectId = objectId;

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (SUCCEEDED(ctx->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			std::memcpy(mapped.pData, &cb, sizeof(cb));
			ctx->Unmap(m_cb.Get(), 0);
		}
		ID3D11Buffer* cbuf = m_cb.Get();
		ctx->VSSetConstantBuffers(0, 1, &cbuf);
		ctx->PSSetConstantBuffers(0, 1, &cbuf);

		const UINT stride = sizeof(Vertex3D), offset = 0;
		ctx->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
		ctx->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);
		ctx->DrawIndexed(indexCount, 0, 0);
	}

	/// @brief パス終了（RT を解除）。
	void end(ID3D11DeviceContext* ctx)
	{
		ID3D11RenderTargetView* nullRtvs[3] = {nullptr, nullptr, nullptr};
		ctx->OMSetRenderTargets(3, nullRtvs, nullptr);
	}

	/// @brief 深度 RT の SRV（R32_FLOAT、[0,1]）。
	[[nodiscard]] ID3D11ShaderResourceView* depthSRV() const noexcept { return m_depthRT.srv.Get(); }
	/// @brief ワールド法線 RT の SRV（RGBA16F、生値）。
	[[nodiscard]] ID3D11ShaderResourceView* normalSRV() const noexcept { return m_normalRT.srv.Get(); }
	/// @brief objectId RT の SRV（R32_UINT）。
	[[nodiscard]] ID3D11ShaderResourceView* objectIdSRV() const noexcept { return m_objectIdRT.srv.Get(); }

	/// @brief readback テスト用の生テクスチャ。
	[[nodiscard]] ID3D11Texture2D* depthTexture() const noexcept { return m_depthRT.texture.Get(); }
	[[nodiscard]] ID3D11Texture2D* normalTexture() const noexcept { return m_normalRT.texture.Get(); }
	[[nodiscard]] ID3D11Texture2D* objectIdTexture() const noexcept { return m_objectIdRT.texture.Get(); }

private:
	struct alignas(16) CbGeo
	{
		float world[4][4]{}; float view[4][4]{}; float proj[4][4]{};
		std::uint32_t objectId = 0; float pad[3]{};
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
			throw std::runtime_error("GeometryPass3D: VS compile failed");
		}
		ComPtr<ID3D11VertexShader> vs;
		if (FAILED(device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(),
		                                      nullptr, vs.GetAddressOf())))
		{
			throw std::runtime_error("GeometryPass3D: CreateVertexShader failed");
		}
		// POSITION(offset 0) + NORMAL(offset 12)。頂点バッファは Vertex3D stride。
		const D3D11_INPUT_ELEMENT_DESC layout[] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}};
		if (FAILED(device->CreateInputLayout(layout, 2, blob->GetBufferPointer(),
		                                     blob->GetBufferSize(), layoutOut.GetAddressOf())))
		{
			throw std::runtime_error("GeometryPass3D: CreateInputLayout failed");
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
			throw std::runtime_error("GeometryPass3D: PS compile failed");
		}
		ComPtr<ID3D11PixelShader> ps;
		if (FAILED(device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(),
		                                     nullptr, ps.GetAddressOf())))
		{
			throw std::runtime_error("GeometryPass3D: CreatePixelShader failed");
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
			throw std::runtime_error("GeometryPass3D: CreateBuffer(CB) failed");
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
			throw std::runtime_error("GeometryPass3D: CreateTexture2D failed");
		}
		if (FAILED(device->CreateShaderResourceView(rt.texture.Get(), nullptr, rt.srv.GetAddressOf())))
		{
			throw std::runtime_error("GeometryPass3D: CreateSRV failed");
		}
		if (FAILED(device->CreateRenderTargetView(rt.texture.Get(), nullptr, rt.rtv.GetAddressOf())))
		{
			throw std::runtime_error("GeometryPass3D: CreateRTV failed");
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
			throw std::runtime_error("GeometryPass3D: CreateDepthTexture failed");
		}
		if (FAILED(device->CreateDepthStencilView(d.texture.Get(), nullptr, d.dsv.GetAddressOf())))
		{
			throw std::runtime_error("GeometryPass3D: CreateDSV failed");
		}
		return d;
	}

	ComPtr<ID3D11Device> m_device;
	ComPtr<ID3D11VertexShader> m_vs;
	ComPtr<ID3D11PixelShader> m_ps;
	ComPtr<ID3D11InputLayout> m_inputLayout;
	ComPtr<ID3D11Buffer> m_cb;
	RTData m_depthRT;
	RTData m_normalRT;
	RTData m_objectIdRT;
	DepthData m_depth;
	ComPtr<ID3D11RasterizerState> m_rasterState;
	ComPtr<ID3D11DepthStencilState> m_depthState;
	std::uint32_t m_width = 0, m_height = 0;
	sgc::Mat4f m_view, m_proj;
};

} // namespace mitiru::render

#endif // _WIN32
