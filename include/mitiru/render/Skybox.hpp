#pragma once

/// @file Skybox.hpp
/// @brief キューブマップ Skybox レンダラー
/// @details `Cubemap` から GPU リソース（TextureCube + SRV + sampler + cube mesh）
///          を構築し、現在バインドされた RTV/DSV に対して skybox を描画する。
///          v1 は DX11 専用。`#ifdef _WIN32` 外では `initialize / draw` は no-op。
///
///          描画手順（推奨パターン）:
///          1. `Renderer3D::beginFrame()` で RTV/DSV をバインド
///          2. `Skybox::drawDx11(camera)` で背景を描画
///          3. 通常の `Renderer3D::drawMesh()` 群
///          4. `Renderer3D::endFrame()`
///
///          ポイント:
///            - skybox は depth = 1（最遠）として描画されるので depth test は
///              LESS_EQUAL のときだけ可視。`Renderer3D` のクリアは 1.0 なので
///              そのまま動作する。
///            - 描画前後で RSState / DepthStencilState をスナップショットし、
///              戻る前に restore する。consumer 側の renderer state を壊さない。

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>

#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/Cubemap.hpp>
#include <mitiru/render/GlmBridge.hpp>
#include <mitiru/render/SkyboxShaders.hpp>

#ifdef _WIN32

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

#include <mitiru/gfx/dx11/Dx11Device.hpp>

#endif // _WIN32

namespace mitiru::render
{

/// @brief キューブマップ Skybox レンダラー
class Skybox
{
public:
	/// @brief デフォルトコンストラクタ（空 skybox）
	Skybox() = default;

	/// @brief CPU 側 cubemap を取り込む
	/// @param cubemap 6 面 RGBA8 キューブマップ
	explicit Skybox(Cubemap cubemap) noexcept
		: m_cubemap(std::move(cubemap))
	{
	}

	/// @brief CPU cubemap データを取得する
	[[nodiscard]] const Cubemap& cubemap() const noexcept { return m_cubemap; }

	/// @brief CPU cubemap を差し替える
	/// @details GPU 側は次の initializeDx11 で再構築される。
	void setCubemap(Cubemap cubemap) noexcept
	{
		m_cubemap = std::move(cubemap);
		m_initialized = false;
	}

	/// @brief CPU cubemap が valid か
	[[nodiscard]] bool hasValidCubemap() const noexcept
	{
		return m_cubemap.valid();
	}

	/// @brief GPU リソースを構築済みか
	[[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

#ifdef _WIN32

	/// @brief ComPtr エイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief DX11 GPU リソースを構築する（gfx::Dx11Device 経由）
	/// @param device Dx11 デバイス
	/// @return 成功 = true。cubemap が invalid なら false で何もしない。
	/// @details 冪等。既に初期化済みなら再構築しない（cubemap 差し替え後は
	///          setCubemap が m_initialized を落とすので、その後に再呼出可能）。
	bool initializeDx11(gfx::Dx11Device* device)
	{
		if (!device) return false;
		return initializeDx11(device->getD3DDevice());
	}

	/// @brief DX11 GPU リソースを構築する（raw ID3D11Device 経由）
	/// @details テストハーネスや独自 DX11 デバイス管理を行う consumer 向け。
	///          単に上記オーバーロードの内部実装でもある。
	bool initializeDx11(ID3D11Device* d3d)
	{
		if (m_initialized) return true;
		if (!d3d || !m_cubemap.valid()) return false;

		if (!createCubeTexture(d3d)) return false;
		if (!createSampler(d3d)) return false;
		if (!createShaders(d3d)) return false;
		if (!createMesh(d3d)) return false;
		if (!createConstantBuffer(d3d)) return false;
		if (!createRasterizerAndDepthStates(d3d)) return false;

		m_initialized = true;
		return true;
	}

	/// @brief Skybox を描画する（Camera3D 版）
	void drawDx11(ID3D11DeviceContext* ctx, const Camera3D& camera) const
	{
		drawDx11(ctx, camera.viewMatrix(), camera.projectionMatrix());
	}

	/// @brief Skybox を描画する（view+proj 直接版）
	/// @param ctx DX11 デバイスコンテキスト
	/// @param viewMatrix ビュー行列（translation 成分は内部で 0 にする）
	/// @param projMatrix 射影行列
	/// @details RTV/DSV のバインドは consumer 側で済んでいる前提。
	///          自前で VS/PS/InputLayout/PrimitiveTopology/rasterizer/
	///          depth-stencil state を一時的に変更し、描画後にすべて元に戻す。
	///          これにより呼び出し側 (Renderer3D::beginFrame で設定された
	///          shader / IL / topology) の状態が破壊されない。
	void drawDx11(ID3D11DeviceContext* ctx,
	              const sgc::Mat4f& viewMatrix,
	              const sgc::Mat4f& projMatrix) const
	{
		if (!m_initialized || !ctx) return;

		// ── 1. 既存のステートを退避（描画後復元するため） ───────────
		ComPtr<ID3D11VertexShader>       prevVS;
		ComPtr<ID3D11PixelShader>        prevPS;
		ComPtr<ID3D11InputLayout>        prevIL;
		ComPtr<ID3D11RasterizerState>    prevRaster;
		ComPtr<ID3D11DepthStencilState>  prevDepth;
		D3D11_PRIMITIVE_TOPOLOGY         prevTopo{};
		UINT prevStencilRef = 0;
		ctx->VSGetShader(prevVS.GetAddressOf(), nullptr, nullptr);
		ctx->PSGetShader(prevPS.GetAddressOf(), nullptr, nullptr);
		ctx->IAGetInputLayout(prevIL.GetAddressOf());
		ctx->IAGetPrimitiveTopology(&prevTopo);
		ctx->RSGetState(prevRaster.GetAddressOf());
		ctx->OMGetDepthStencilState(prevDepth.GetAddressOf(), &prevStencilRef);

		// ── 2. CbSkyTransform を更新する ─────────────────────────
		CbSkyTransform cb{};
		buildCb(cb, viewMatrix, projMatrix);

		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(ctx->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			return;
		}
		std::memcpy(mapped.pData, &cb, sizeof(cb));
		ctx->Unmap(m_cb.Get(), 0);

		// ── 3. シェーダー / バッファ / SRV / sampler を設定 ───────
		ctx->VSSetShader(m_vs.Get(), nullptr, 0);
		ctx->PSSetShader(m_ps.Get(), nullptr, 0);
		ctx->IASetInputLayout(m_inputLayout.Get());
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		const UINT stride = sizeof(float) * 3;
		const UINT offset = 0;
		ID3D11Buffer* vb = m_vb.Get();
		ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
		ctx->IASetIndexBuffer(m_ib.Get(), DXGI_FORMAT_R32_UINT, 0);

		ID3D11Buffer* cbPtr = m_cb.Get();
		ctx->VSSetConstantBuffers(0, 1, &cbPtr);

		ID3D11ShaderResourceView* srv = m_srv.Get();
		ctx->PSSetShaderResources(0, 1, &srv);

		ID3D11SamplerState* samp = m_sampler.Get();
		ctx->PSSetSamplers(0, 1, &samp);

		ctx->RSSetState(m_rasterizer.Get());
		ctx->OMSetDepthStencilState(m_depthState.Get(), 0);

		// ── 4. ドロー ─────────────────────────────────────────
		ctx->DrawIndexed(kIndexCount, 0, 0);

		// ── 5. ステートを元に戻す ─────────────────────────────
		ctx->VSSetShader(prevVS.Get(), nullptr, 0);
		ctx->PSSetShader(prevPS.Get(), nullptr, 0);
		ctx->IASetInputLayout(prevIL.Get());
		ctx->IASetPrimitiveTopology(prevTopo);
		ctx->RSSetState(prevRaster.Get());
		ctx->OMSetDepthStencilState(prevDepth.Get(), prevStencilRef);

		// SRV を解除（後段で同じ slot を使うときの混線を避ける）
		ID3D11ShaderResourceView* nullSrv = nullptr;
		ctx->PSSetShaderResources(0, 1, &nullSrv);
	}

#else // _WIN32

	bool initializeDx11(void* /*device*/) { return false; }
	void drawDx11(void* /*ctx*/, const Camera3D& /*camera*/) const {}

#endif // _WIN32

private:
	Cubemap m_cubemap;
	bool m_initialized = false;

	struct alignas(16) CbSkyTransform
	{
		float viewNoTranslation[4][4]{};
		float projection[4][4]{};
	};

	static constexpr UINT kVertexCount = 8;
	static constexpr UINT kIndexCount  = 36;

#ifdef _WIN32

	ComPtr<ID3D11Texture2D>          m_tex;
	ComPtr<ID3D11ShaderResourceView> m_srv;
	ComPtr<ID3D11SamplerState>       m_sampler;
	ComPtr<ID3D11VertexShader>       m_vs;
	ComPtr<ID3D11PixelShader>        m_ps;
	ComPtr<ID3D11InputLayout>        m_inputLayout;
	ComPtr<ID3D11Buffer>             m_vb;
	ComPtr<ID3D11Buffer>             m_ib;
	ComPtr<ID3D11Buffer>             m_cb;
	ComPtr<ID3D11RasterizerState>    m_rasterizer;
	ComPtr<ID3D11DepthStencilState>  m_depthState;

	/// @brief CbSkyTransform を埋める
	void buildCb(CbSkyTransform& cb,
	             const sgc::Mat4f& viewMatrix,
	             const sgc::Mat4f& projMatrix) const
	{
		glm::mat4 view = toGlm(viewMatrix);
		glm::mat4 proj = toGlm(projMatrix);

		// translation を消す。row-major で見ると view = [R | t; 0 0 0 1] の
		// 形だが、glm は column-major なので translation は最後の「列」、
		// すなわち view[3].xyz に格納されている。これを 0 にする。
		view[3][0] = 0.0f;
		view[3][1] = 0.0f;
		view[3][2] = 0.0f;
		// 残りの要素はそのまま。w 成分 view[3][3] = 1 のまま。

		toHLSL(cb.viewNoTranslation, view);
		toHLSL(cb.projection, proj);
	}

	bool createCubeTexture(ID3D11Device* d3d)
	{
		const int size = m_cubemap.faceSize();

		D3D11_TEXTURE2D_DESC td{};
		td.Width              = static_cast<UINT>(size);
		td.Height             = static_cast<UINT>(size);
		td.MipLevels          = 1;
		td.ArraySize          = kCubemapFaceCount;
		td.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count   = 1;
		td.Usage              = D3D11_USAGE_DEFAULT;
		td.BindFlags          = D3D11_BIND_SHADER_RESOURCE;
		td.MiscFlags          = D3D11_RESOURCE_MISC_TEXTURECUBE;

		std::array<D3D11_SUBRESOURCE_DATA, kCubemapFaceCount> srd{};
		for (int i = 0; i < kCubemapFaceCount; ++i)
		{
			const auto& face = m_cubemap.face(i);
			srd[i].pSysMem          = face.pixels().data();
			srd[i].SysMemPitch      = static_cast<UINT>(face.width() * 4);
			srd[i].SysMemSlicePitch = 0;
		}

		if (FAILED(d3d->CreateTexture2D(&td, srd.data(), m_tex.GetAddressOf())))
		{
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
		sd.Format                       = td.Format;
		sd.ViewDimension                = D3D11_SRV_DIMENSION_TEXTURECUBE;
		sd.TextureCube.MipLevels        = 1;
		sd.TextureCube.MostDetailedMip  = 0;

		if (FAILED(d3d->CreateShaderResourceView(
				m_tex.Get(), &sd, m_srv.GetAddressOf())))
		{
			return false;
		}
		return true;
	}

	bool createSampler(ID3D11Device* d3d)
	{
		D3D11_SAMPLER_DESC s{};
		s.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		s.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
		s.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
		s.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
		s.MaxAnisotropy  = 1;
		s.MinLOD         = 0;
		s.MaxLOD         = D3D11_FLOAT32_MAX;
		s.ComparisonFunc = D3D11_COMPARISON_NEVER;
		return SUCCEEDED(d3d->CreateSamplerState(&s, m_sampler.GetAddressOf()));
	}

	bool createShaders(ID3D11Device* d3d)
	{
		ComPtr<ID3DBlob> vsBlob;
		ComPtr<ID3DBlob> psBlob;
		ComPtr<ID3DBlob> err;

		if (FAILED(D3DCompile(
				SKYBOX_VS_HLSL, std::strlen(SKYBOX_VS_HLSL),
				nullptr, nullptr, nullptr,
				"VSMain", "vs_5_0",
				0, 0, vsBlob.GetAddressOf(), err.GetAddressOf())))
		{
			return false;
		}
		if (FAILED(D3DCompile(
				SKYBOX_PS_HLSL, std::strlen(SKYBOX_PS_HLSL),
				nullptr, nullptr, nullptr,
				"PSMain", "ps_5_0",
				0, 0, psBlob.GetAddressOf(), err.GetAddressOf())))
		{
			return false;
		}

		if (FAILED(d3d->CreateVertexShader(
				vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
				nullptr, m_vs.GetAddressOf())))
		{
			return false;
		}
		if (FAILED(d3d->CreatePixelShader(
				psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
				nullptr, m_ps.GetAddressOf())))
		{
			return false;
		}

		const D3D11_INPUT_ELEMENT_DESC layout[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
			  D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

		if (FAILED(d3d->CreateInputLayout(
				layout, 1,
				vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
				m_inputLayout.GetAddressOf())))
		{
			return false;
		}
		return true;
	}

	bool createMesh(ID3D11Device* d3d)
	{
		// 単位立方体（±1）の 8 頂点
		const float verts[8 * 3] = {
			-1.0f, -1.0f, -1.0f,  //  0  ---
			 1.0f, -1.0f, -1.0f,  //  1  +--
			 1.0f,  1.0f, -1.0f,  //  2  ++-
			-1.0f,  1.0f, -1.0f,  //  3  -+-
			-1.0f, -1.0f,  1.0f,  //  4  --+
			 1.0f, -1.0f,  1.0f,  //  5  +-+
			 1.0f,  1.0f,  1.0f,  //  6  +++
			-1.0f,  1.0f,  1.0f,  //  7  -++
		};

		// 内側から見る前提なので CCW を反転（CW で書く）。
		// 後で rasterizer を CULL_FRONT にして外側を捨てる手もあるが、
		// CULL_NONE で運用するため winding をここで揃える。
		const std::uint32_t idx[36] = {
			// -Z front (looking +Z from inside)
			0, 2, 1,  0, 3, 2,
			// +Z back
			4, 5, 6,  4, 6, 7,
			// -X left
			0, 4, 7,  0, 7, 3,
			// +X right
			1, 2, 6,  1, 6, 5,
			// +Y top
			3, 7, 6,  3, 6, 2,
			// -Y bottom
			0, 1, 5,  0, 5, 4,
		};

		D3D11_BUFFER_DESC vbd{};
		vbd.ByteWidth      = sizeof(verts);
		vbd.Usage          = D3D11_USAGE_IMMUTABLE;
		vbd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
		D3D11_SUBRESOURCE_DATA vbi{};
		vbi.pSysMem = verts;
		if (FAILED(d3d->CreateBuffer(&vbd, &vbi, m_vb.GetAddressOf())))
		{
			return false;
		}

		D3D11_BUFFER_DESC ibd{};
		ibd.ByteWidth      = sizeof(idx);
		ibd.Usage          = D3D11_USAGE_IMMUTABLE;
		ibd.BindFlags      = D3D11_BIND_INDEX_BUFFER;
		D3D11_SUBRESOURCE_DATA ibi{};
		ibi.pSysMem = idx;
		if (FAILED(d3d->CreateBuffer(&ibd, &ibi, m_ib.GetAddressOf())))
		{
			return false;
		}
		return true;
	}

	bool createConstantBuffer(ID3D11Device* d3d)
	{
		D3D11_BUFFER_DESC cbd{};
		cbd.ByteWidth      = sizeof(CbSkyTransform);
		cbd.Usage          = D3D11_USAGE_DYNAMIC;
		cbd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
		cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		return SUCCEEDED(d3d->CreateBuffer(&cbd, nullptr, m_cb.GetAddressOf()));
	}

	bool createRasterizerAndDepthStates(ID3D11Device* d3d)
	{
		D3D11_RASTERIZER_DESC rd{};
		rd.FillMode = D3D11_FILL_SOLID;
		// CULL_NONE: inside-out 巻きの index と整合的にどちらでも描画させる。
		// 性能を詰めるなら CULL_FRONT + 通常 CCW 巻きに切り替える余地あり。
		rd.CullMode = D3D11_CULL_NONE;
		rd.FrontCounterClockwise = FALSE;
		rd.DepthClipEnable = TRUE;
		if (FAILED(d3d->CreateRasterizerState(&rd, m_rasterizer.GetAddressOf())))
		{
			return false;
		}

		D3D11_DEPTH_STENCIL_DESC ds{};
		ds.DepthEnable      = TRUE;
		ds.DepthWriteMask   = D3D11_DEPTH_WRITE_MASK_ZERO; // depth 書き込まない
		ds.DepthFunc        = D3D11_COMPARISON_LESS_EQUAL; // depth=1 を許可
		ds.StencilEnable    = FALSE;
		return SUCCEEDED(d3d->CreateDepthStencilState(
			&ds, m_depthState.GetAddressOf()));
	}

#endif // _WIN32
};

} // namespace mitiru::render
