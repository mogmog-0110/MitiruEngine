#pragma once

/// @file GpuSpriteBatch_impl.hpp
/// @brief GpuSpriteBatch の GPU リソース構築・描画の実装本体（GpuSpriteBatch.hpp から機械的分割）

#include <mitiru/render/GpuSpriteBatch.hpp>

#ifdef _WIN32

namespace mitiru::render
{

// ─── シェーダーコンパイル ───────────────────────────────

/// @brief HLSL文字列をコンパイルする
inline GpuSpriteBatch::ComPtr<ID3DBlob> GpuSpriteBatch::compileHLSL(
	std::string_view source,
	const char* entryPoint,
	const char* target) const
{
	ComPtr<ID3DBlob> shaderBlob;
	ComPtr<ID3DBlob> errorBlob;

	UINT flags = 0;
#ifdef _DEBUG
	flags |= D3DCOMPILE_DEBUG;
	flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	HRESULT hr = D3DCompile(
		source.data(), source.size(),
		nullptr, nullptr, nullptr,
		entryPoint, target,
		flags, 0,
		shaderBlob.GetAddressOf(),
		errorBlob.GetAddressOf());

	if (FAILED(hr))
	{
		std::string msg = "GpuSpriteBatch: shader compile failed";
		if (errorBlob)
		{
			msg += ": ";
			msg += static_cast<const char*>(
				errorBlob->GetBufferPointer());
		}
		throw std::runtime_error(msg);
	}

	return shaderBlob;
}

/// @brief 頂点・ピクセルシェーダーをコンパイルする
inline void GpuSpriteBatch::compileShaders()
{
	m_vsByteCode = compileHLSL(GPU_SPRITE_VS, "VSMain", "vs_5_0");

	HRESULT hr = m_device->CreateVertexShader(
		m_vsByteCode->GetBufferPointer(),
		m_vsByteCode->GetBufferSize(),
		nullptr,
		m_vertexShader.GetAddressOf());
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"GpuSpriteBatch: CreateVertexShader failed");
	}

	auto psBlob = compileHLSL(GPU_SPRITE_PS, "PSMain", "ps_5_0");

	hr = m_device->CreatePixelShader(
		psBlob->GetBufferPointer(),
		psBlob->GetBufferSize(),
		nullptr,
		m_pixelShader.GetAddressOf());
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"GpuSpriteBatch: CreatePixelShader failed");
	}
}

// ─── リソース生成 ─────────────────────────────────────

/// @brief GpuSpriteBatchVertex用の入力レイアウトを生成する
inline void GpuSpriteBatch::createInputLayout()
{
	const D3D11_INPUT_ELEMENT_DESC layout[] =
	{
		{
			"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,
			0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0
		},
		{
			"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
			0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0
		},
		{
			"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,
			0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0
		},
	};

	HRESULT hr = m_device->CreateInputLayout(
		layout,
		static_cast<UINT>(std::size(layout)),
		m_vsByteCode->GetBufferPointer(),
		m_vsByteCode->GetBufferSize(),
		m_inputLayout.GetAddressOf());
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"GpuSpriteBatch: CreateInputLayout failed");
	}
}

/// @brief 動的頂点バッファを生成する
inline void GpuSpriteBatch::createVertexBuffer()
{
	const auto vertexCount =
		static_cast<std::uint32_t>(m_maxSprites) * 4;
	const auto sizeBytes =
		vertexCount * static_cast<std::uint32_t>(sizeof(GpuSpriteBatchVertex));

	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = sizeBytes;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	HRESULT hr = m_device->CreateBuffer(
		&desc, nullptr, m_vertexBuffer.GetAddressOf());
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"GpuSpriteBatch: CreateBuffer (VB) failed");
	}
}

/// @brief 静的インデックスバッファを生成する
/// @details クワッド描画用のインデックスパターンを事前に生成する。
///          各クワッドは {0,1,2, 0,2,3} パターン。
inline void GpuSpriteBatch::createIndexBuffer()
{
	const auto spriteCount =
		static_cast<std::uint32_t>(m_maxSprites);
	std::vector<std::uint32_t> indices(
		static_cast<std::size_t>(spriteCount) * 6);

	for (std::uint32_t i = 0; i < spriteCount; ++i)
	{
		const auto vi = i * 4;
		const auto ii = static_cast<std::size_t>(i) * 6;
		indices[ii + 0] = vi + 0;
		indices[ii + 1] = vi + 1;
		indices[ii + 2] = vi + 2;
		indices[ii + 3] = vi + 0;
		indices[ii + 4] = vi + 2;
		indices[ii + 5] = vi + 3;
	}

	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = static_cast<UINT>(
		indices.size() * sizeof(std::uint32_t));
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

	D3D11_SUBRESOURCE_DATA initData = {};
	initData.pSysMem = indices.data();

	HRESULT hr = m_device->CreateBuffer(
		&desc, &initData, m_indexBuffer.GetAddressOf());
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"GpuSpriteBatch: CreateBuffer (IB) failed");
	}
}

/// @brief 定数バッファ（正射影行列）を生成する
inline void GpuSpriteBatch::createConstantBuffer()
{
	D3D11_BUFFER_DESC desc = {};
	/// float4x4 = 64バイト、16バイトアラインメント
	desc.ByteWidth = 64;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	HRESULT hr = m_device->CreateBuffer(
		&desc, nullptr, m_constantBuffer.GetAddressOf());
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"GpuSpriteBatch: CreateBuffer (CB) failed");
	}
}

/// @brief 各ブレンドモード用のブレンドステートを生成する
inline void GpuSpriteBatch::createBlendStates()
{
	/// AlphaBlend
	{
		D3D11_BLEND_DESC desc = {};
		desc.AlphaToCoverageEnable = FALSE;
		desc.IndependentBlendEnable = FALSE;
		auto& rt = desc.RenderTarget[0];
		rt.BlendEnable = TRUE;
		rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
		rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		rt.BlendOp = D3D11_BLEND_OP_ADD;
		rt.SrcBlendAlpha = D3D11_BLEND_ONE;
		rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		m_device->CreateBlendState(
			&desc, m_blendStates[0].GetAddressOf());
	}

	/// Additive
	{
		D3D11_BLEND_DESC desc = {};
		desc.AlphaToCoverageEnable = FALSE;
		desc.IndependentBlendEnable = FALSE;
		auto& rt = desc.RenderTarget[0];
		rt.BlendEnable = TRUE;
		rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
		rt.DestBlend = D3D11_BLEND_ONE;
		rt.BlendOp = D3D11_BLEND_OP_ADD;
		rt.SrcBlendAlpha = D3D11_BLEND_ONE;
		rt.DestBlendAlpha = D3D11_BLEND_ONE;
		rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		m_device->CreateBlendState(
			&desc, m_blendStates[1].GetAddressOf());
	}

	/// Multiply
	{
		D3D11_BLEND_DESC desc = {};
		desc.AlphaToCoverageEnable = FALSE;
		desc.IndependentBlendEnable = FALSE;
		auto& rt = desc.RenderTarget[0];
		rt.BlendEnable = TRUE;
		rt.SrcBlend = D3D11_BLEND_DEST_COLOR;
		rt.DestBlend = D3D11_BLEND_ZERO;
		rt.BlendOp = D3D11_BLEND_OP_ADD;
		rt.SrcBlendAlpha = D3D11_BLEND_DEST_ALPHA;
		rt.DestBlendAlpha = D3D11_BLEND_ZERO;
		rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		m_device->CreateBlendState(
			&desc, m_blendStates[2].GetAddressOf());
	}

	/// None
	{
		D3D11_BLEND_DESC desc = {};
		desc.AlphaToCoverageEnable = FALSE;
		desc.IndependentBlendEnable = FALSE;
		auto& rt = desc.RenderTarget[0];
		rt.BlendEnable = FALSE;
		rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		m_device->CreateBlendState(
			&desc, m_blendStates[3].GetAddressOf());
	}
}

/// @brief 各サンプリングモード用のサンプラーステートを生成する
inline void GpuSpriteBatch::createSamplerStates()
{
	/// Bilinear
	{
		D3D11_SAMPLER_DESC desc = {};
		desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.MaxAnisotropy = 1;
		desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		desc.MaxLOD = D3D11_FLOAT32_MAX;

		m_device->CreateSamplerState(
			&desc, m_samplerStates[0].GetAddressOf());
	}

	/// Point
	{
		D3D11_SAMPLER_DESC desc = {};
		desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.MaxAnisotropy = 1;
		desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		desc.MaxLOD = D3D11_FLOAT32_MAX;

		m_device->CreateSamplerState(
			&desc, m_samplerStates[1].GetAddressOf());
	}

	/// Anisotropic
	{
		D3D11_SAMPLER_DESC desc = {};
		desc.Filter = D3D11_FILTER_ANISOTROPIC;
		desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.MaxAnisotropy = 16;
		desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		desc.MaxLOD = D3D11_FLOAT32_MAX;

		m_device->CreateSamplerState(
			&desc, m_samplerStates[2].GetAddressOf());
	}
}

/// @brief 2D描画用ラスタライザステートを生成する
inline void GpuSpriteBatch::createRasterizerState()
{
	D3D11_RASTERIZER_DESC desc = {};
	desc.FillMode = D3D11_FILL_SOLID;
	desc.CullMode = D3D11_CULL_NONE;
	desc.FrontCounterClockwise = FALSE;
	desc.DepthClipEnable = TRUE;
	desc.ScissorEnable = FALSE;

	m_device->CreateRasterizerState(
		&desc, m_rasterizerState.GetAddressOf());
}

// ─── 描画ヘルパー ─────────────────────────────────────

/// @brief 正射影行列を定数バッファに書き込む
inline void GpuSpriteBatch::updateProjection(float width, float height)
{
	if (!m_context || !m_constantBuffer)
	{
		return;
	}

	/// left=0, right=w, top=0, bottom=h, near=0, far=1
	/// 行優先float4x4
	float projection[4][4] = {};
	projection[0][0] = 2.0f / width;
	projection[1][1] = -2.0f / height;
	projection[2][2] = 1.0f;
	projection[3][0] = -1.0f;
	projection[3][1] = 1.0f;
	projection[3][2] = 0.0f;
	projection[3][3] = 1.0f;

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	HRESULT hr = m_context->Map(
		m_constantBuffer.Get(), 0,
		D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (SUCCEEDED(hr))
	{
		std::memcpy(mapped.pData, projection, sizeof(projection));
		m_context->Unmap(m_constantBuffer.Get(), 0);
	}
}

/// @brief 軸整列クワッドの4頂点を蓄積する
inline void GpuSpriteBatch::pushQuad(
	float dstX, float dstY, float dstW, float dstH,
	float u0, float v0, float u1, float v1,
	float r, float g, float b, float a)
{
	const auto x0 = dstX;
	const auto y0 = dstY;
	const auto x1 = dstX + dstW;
	const auto y1 = dstY + dstH;

	m_vertices.push_back({x0, y0, u0, v0, r, g, b, a});
	m_vertices.push_back({x1, y0, u1, v0, r, g, b, a});
	m_vertices.push_back({x1, y1, u1, v1, r, g, b, a});
	m_vertices.push_back({x0, y1, u0, v1, r, g, b, a});
}

/// @brief 回転済みクワッドの4頂点を蓄積する
inline void GpuSpriteBatch::pushQuadRotated(
	float dstX, float dstY,
	float dstW, float dstH,
	float u0, float v0,
	float u1, float v1,
	float r, float g, float b, float a,
	float rotation)
{
	const auto cx = dstX + dstW * 0.5f;
	const auto cy = dstY + dstH * 0.5f;
	const auto hw = dstW * 0.5f;
	const auto hh = dstH * 0.5f;
	const auto cosR = std::cos(rotation);
	const auto sinR = std::sin(rotation);

	/// 左上
	const auto x0 = cx + (-hw * cosR - (-hh) * sinR);
	const auto y0 = cy + (-hw * sinR + (-hh) * cosR);
	/// 右上
	const auto x1 = cx + (hw * cosR - (-hh) * sinR);
	const auto y1 = cy + (hw * sinR + (-hh) * cosR);
	/// 右下
	const auto x2 = cx + (hw * cosR - hh * sinR);
	const auto y2 = cy + (hw * sinR + hh * cosR);
	/// 左下
	const auto x3 = cx + (-hw * cosR - hh * sinR);
	const auto y3 = cy + (-hw * sinR + hh * cosR);

	m_vertices.push_back({x0, y0, u0, v0, r, g, b, a});
	m_vertices.push_back({x1, y1, u1, v0, r, g, b, a});
	m_vertices.push_back({x2, y2, u1, v1, r, g, b, a});
	m_vertices.push_back({x3, y3, u0, v1, r, g, b, a});
}

/// @brief 蓄積された頂点をGPUに送信して描画する
inline void GpuSpriteBatch::flush()
{
	if (m_vertices.empty() || !m_context)
	{
		return;
	}

	/// 頂点バッファを更新する（MAP_WRITE_DISCARD）
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	HRESULT hr = m_context->Map(
		m_vertexBuffer.Get(), 0,
		D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (FAILED(hr))
	{
		m_vertices.clear();
		return;
	}

	const auto dataSize = m_vertices.size() *
		sizeof(GpuSpriteBatchVertex);
	std::memcpy(mapped.pData, m_vertices.data(), dataSize);
	m_context->Unmap(m_vertexBuffer.Get(), 0);

	/// パイプラインステートを設定する
	m_context->IASetInputLayout(m_inputLayout.Get());
	m_context->IASetPrimitiveTopology(
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	/// 頂点バッファをバインドする
	const UINT stride = sizeof(GpuSpriteBatchVertex);
	const UINT offset = 0;
	auto* vb = m_vertexBuffer.Get();
	m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);

	/// インデックスバッファをバインドする
	m_context->IASetIndexBuffer(
		m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

	/// シェーダーを設定する
	m_context->VSSetShader(
		m_vertexShader.Get(), nullptr, 0);
	m_context->PSSetShader(
		m_pixelShader.Get(), nullptr, 0);

	/// 定数バッファを設定する
	auto* cb = m_constantBuffer.Get();
	m_context->VSSetConstantBuffers(0, 1, &cb);

	/// テクスチャとサンプラーを設定する
	if (m_currentTextureSrv)
	{
		m_context->PSSetShaderResources(
			0, 1, &m_currentTextureSrv);
	}

	const auto samplerIdx =
		static_cast<std::size_t>(m_currentSamplerMode);
	auto* sampler = m_samplerStates[samplerIdx].Get();
	m_context->PSSetSamplers(0, 1, &sampler);

	/// ブレンドステートを設定する
	const auto blendIdx =
		static_cast<std::size_t>(m_currentBlendMode);
	const float blendFactor[4] = {0, 0, 0, 0};
	m_context->OMSetBlendState(
		m_blendStates[blendIdx].Get(),
		blendFactor, 0xFFFFFFFF);

	/// ラスタライザステートを設定する
	m_context->RSSetState(m_rasterizerState.Get());

	/// ビューポートを設定する
	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = m_screenWidth;
	viewport.Height = m_screenHeight;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	m_context->RSSetViewports(1, &viewport);

	/// インデックス描画を実行する
	const auto spriteCount =
		static_cast<UINT>(m_vertices.size() / 4);
	m_context->DrawIndexed(spriteCount * 6, 0, 0);

	/// バッチをクリアする
	m_vertices.clear();
	m_spriteCount = 0;
	++m_flushCount;
}

} // namespace mitiru::render

#endif // _WIN32
