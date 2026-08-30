#pragma once

/// @file PostProcessUtils.hpp
/// @brief ポストプロセス共通ユーティリティ（レンダーターゲット・シェーダーコンパイル・定数バッファ）

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
#include <string>
#include <string_view>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")

#include <mitiru/render/PostProcessShaders.hpp>

namespace mitiru::render
{

/// @brief ComPtrエイリアス
template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

/// @brief ポストプロセス用レンダーターゲット
/// @details テクスチャ + SRV + RTV をまとめて保持する
struct PostProcessRT
{
	ComPtr<ID3D11Texture2D> texture;               ///< テクスチャリソース
	ComPtr<ID3D11ShaderResourceView> srv;           ///< シェーダーリソースビュー
	ComPtr<ID3D11RenderTargetView> rtv;             ///< レンダーターゲットビュー
	std::uint32_t width = 0;                        ///< テクスチャ幅
	std::uint32_t height = 0;                       ///< テクスチャ高さ
};

/// @brief ポストプロセス用レンダーターゲットを生成する
/// @param device D3D11デバイス
/// @param w 幅（ピクセル）
/// @param h 高さ（ピクセル）
/// @param format テクスチャフォーマット
/// @return 生成されたレンダーターゲット
[[nodiscard]] inline PostProcessRT createRenderTarget(
	ID3D11Device* device,
	std::uint32_t w,
	std::uint32_t h,
	DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT)
{
	PostProcessRT rt;
	rt.width = w;
	rt.height = h;

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = w;
	desc.Height = h;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

	HRESULT hr = device->CreateTexture2D(
		&desc, nullptr, rt.texture.GetAddressOf());
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"PostProcess: CreateTexture2D failed");
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	hr = device->CreateShaderResourceView(
		rt.texture.Get(), &srvDesc, rt.srv.GetAddressOf());
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"PostProcess: CreateShaderResourceView failed");
	}

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = format;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

	hr = device->CreateRenderTargetView(
		rt.texture.Get(), &rtvDesc, rt.rtv.GetAddressOf());
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"PostProcess: CreateRenderTargetView failed");
	}

	return rt;
}

// ============================================================================
// ヘルパー。シェーダーコンパイル
// ============================================================================

/// @brief HLSLピクセルシェーダーをコンパイルする
/// @param device D3D11デバイス
/// @param source HLSL文字列
/// @param entryPoint エントリーポイント名
/// @return コンパイル済みピクセルシェーダー
[[nodiscard]] inline ComPtr<ID3D11PixelShader> compilePostProcessPS(
	ID3D11Device* device,
	std::string_view source,
	const char* entryPoint = "PSMain")
{
	ComPtr<ID3DBlob> shaderBlob;
	ComPtr<ID3DBlob> errorBlob;

	UINT compileFlags = 0;
#ifdef _DEBUG
	compileFlags |= D3DCOMPILE_DEBUG;
	compileFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	HRESULT hr = D3DCompile(
		source.data(), source.size(),
		nullptr, nullptr, nullptr,
		entryPoint, "ps_5_0",
		compileFlags, 0,
		shaderBlob.GetAddressOf(),
		errorBlob.GetAddressOf());

	if (FAILED(hr))
	{
		std::string msg = "PostProcess: PS compile failed";
		if (errorBlob)
		{
			msg += ": ";
			msg += static_cast<const char*>(
				errorBlob->GetBufferPointer());
		}
		throw std::runtime_error(msg);
	}

	ComPtr<ID3D11PixelShader> ps;
	hr = device->CreatePixelShader(
		shaderBlob->GetBufferPointer(),
		shaderBlob->GetBufferSize(),
		nullptr, ps.GetAddressOf());
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"PostProcess: CreatePixelShader failed");
	}

	return ps;
}

/// @brief フルスクリーン頂点シェーダーをコンパイルする
/// @param device D3D11デバイス
/// @return コンパイル済み頂点シェーダー
[[nodiscard]] inline ComPtr<ID3D11VertexShader> compileFullscreenVS(
	ID3D11Device* device)
{
	ComPtr<ID3DBlob> shaderBlob;
	ComPtr<ID3DBlob> errorBlob;

	UINT compileFlags = 0;
#ifdef _DEBUG
	compileFlags |= D3DCOMPILE_DEBUG;
	compileFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	HRESULT hr = D3DCompile(
		PP_FULLSCREEN_VS.data(), PP_FULLSCREEN_VS.size(),
		nullptr, nullptr, nullptr,
		"VSMain", "vs_5_0",
		compileFlags, 0,
		shaderBlob.GetAddressOf(),
		errorBlob.GetAddressOf());

	if (FAILED(hr))
	{
		std::string msg = "PostProcess: VS compile failed";
		if (errorBlob)
		{
			msg += ": ";
			msg += static_cast<const char*>(
				errorBlob->GetBufferPointer());
		}
		throw std::runtime_error(msg);
	}

	ComPtr<ID3D11VertexShader> vs;
	hr = device->CreateVertexShader(
		shaderBlob->GetBufferPointer(),
		shaderBlob->GetBufferSize(),
		nullptr, vs.GetAddressOf());
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"PostProcess: CreateVertexShader failed");
	}

	return vs;
}

/// @brief 定数バッファを生成する
/// @param device D3D11デバイス
/// @param sizeBytes バッファサイズ（16バイトアライン済み）
/// @return 生成された定数バッファ
[[nodiscard]] inline ComPtr<ID3D11Buffer> createConstantBuffer(
	ID3D11Device* device,
	std::uint32_t sizeBytes)
{
	/// 16バイトアラインメントを保証する
	const auto aligned =
		(sizeBytes + 15u) & ~15u;

	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = aligned;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	ComPtr<ID3D11Buffer> buffer;
	HRESULT hr = device->CreateBuffer(
		&desc, nullptr, buffer.GetAddressOf());
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"PostProcess: CreateBuffer (constant) failed");
	}

	return buffer;
}

/// @brief 定数バッファを更新する
/// @param context D3D11コンテキスト
/// @param buffer 更新対象バッファ
/// @param data データポインタ
/// @param sizeBytes データサイズ
inline void updateConstantBuffer(
	ID3D11DeviceContext* context,
	ID3D11Buffer* buffer,
	const void* data,
	std::uint32_t sizeBytes)
{
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	HRESULT hr = context->Map(
		buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (SUCCEEDED(hr))
	{
		std::memcpy(mapped.pData, data, sizeBytes);
		context->Unmap(buffer, 0);
	}
#ifdef _DEBUG
	else
	{
		OutputDebugStringA("PostProcess: Map failed\n");
	}
#endif
}

/// @brief リニアサンプラーを生成する
/// @param device D3D11デバイス
/// @return 生成されたサンプラーステート
[[nodiscard]] inline ComPtr<ID3D11SamplerState> createLinearClampSampler(
	ID3D11Device* device)
{
	D3D11_SAMPLER_DESC desc = {};
	desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	desc.MinLOD = 0;
	desc.MaxLOD = D3D11_FLOAT32_MAX;

	ComPtr<ID3D11SamplerState> sampler;
	HRESULT hr = device->CreateSamplerState(
		&desc, sampler.GetAddressOf());
	if (FAILED(hr))
	{
		throw std::runtime_error(
			"PostProcess: CreateSamplerState failed");
	}

	return sampler;
}

} // namespace mitiru::render

#endif // _WIN32
