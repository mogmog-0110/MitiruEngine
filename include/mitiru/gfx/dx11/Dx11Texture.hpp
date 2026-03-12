#pragma once

/// @file Dx11Texture.hpp
/// @brief DirectX 11テクスチャラッパー
/// @details ID3D11Texture2DとID3D11ShaderResourceViewをComPtrで管理する。
///          ピクセルデータからの生成およびステージングテクスチャの生成をサポートする。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

#include <mitiru/gfx/GfxTypes.hpp>
#include <mitiru/gfx/ITexture.hpp>

namespace mitiru::gfx
{

/// @brief DirectX 11テクスチャラッパー
/// @details ID3D11Texture2DおよびID3D11ShaderResourceViewをRAIIで管理する。
class Dx11Texture final : public ITexture
{
public:
	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief ピクセルデータからテクスチャを生成する
	/// @param device D3D11デバイス
	/// @param width テクスチャ幅
	/// @param height テクスチャ高さ
	/// @param data RGBA8形式のピクセルデータ
	/// @return 生成されたテクスチャ
	[[nodiscard]] static Dx11Texture createFromData(
		ID3D11Device* device,
		int width, int height,
		std::span<const std::uint8_t> data)
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = static_cast<UINT>(width);
		desc.Height = static_cast<UINT>(height);
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem = data.data();
		initData.SysMemPitch = static_cast<UINT>(width) * 4;

		Dx11Texture texture;
		texture.m_width = width;
		texture.m_height = height;
		texture.m_format = PixelFormat::RGBA8;

		HRESULT hr = device->CreateTexture2D(
			&desc, &initData, texture.m_texture.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error("Dx11Texture: CreateTexture2D failed");
		}

		/// シェーダーリソースビューの生成
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		hr = device->CreateShaderResourceView(
			texture.m_texture.Get(), &srvDesc,
			texture.m_srv.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx11Texture: CreateShaderResourceView failed");
		}

		return texture;
	}

	/// @brief ステージングテクスチャを生成する（readback用）
	/// @param device D3D11デバイス
	/// @param width テクスチャ幅
	/// @param height テクスチャ高さ
	/// @return ステージングテクスチャ
	[[nodiscard]] static Dx11Texture createStaging(
		ID3D11Device* device, int width, int height)
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = static_cast<UINT>(width);
		desc.Height = static_cast<UINT>(height);
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

		Dx11Texture texture;
		texture.m_width = width;
		texture.m_height = height;
		texture.m_format = PixelFormat::RGBA8;

		HRESULT hr = device->CreateTexture2D(
			&desc, nullptr, texture.m_texture.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx11Texture: CreateTexture2D (staging) failed");
		}

		return texture;
	}

	/// @brief テクスチャ幅を取得する
	[[nodiscard]] int width() const noexcept override
	{
		return m_width;
	}

	/// @brief テクスチャ高さを取得する
	[[nodiscard]] int height() const noexcept override
	{
		return m_height;
	}

	/// @brief ピクセルフォーマットを取得する
	[[nodiscard]] PixelFormat format() const noexcept override
	{
		return m_format;
	}

	/// @brief 内部のID3D11Texture2Dを取得する
	/// @return テクスチャリソースへのポインタ
	[[nodiscard]] ID3D11Texture2D* getTexture() const noexcept
	{
		return m_texture.Get();
	}

	/// @brief シェーダーリソースビューを取得する
	/// @return SRVへのポインタ（ステージングテクスチャの場合はnullptr）
	[[nodiscard]] ID3D11ShaderResourceView* getSRV() const noexcept
	{
		return m_srv.Get();
	}

private:
	ComPtr<ID3D11Texture2D> m_texture;              ///< テクスチャリソース
	ComPtr<ID3D11ShaderResourceView> m_srv;          ///< シェーダーリソースビュー
	int m_width = 0;                                 ///< テクスチャ幅
	int m_height = 0;                                ///< テクスチャ高さ
	PixelFormat m_format = PixelFormat::RGBA8;       ///< ピクセルフォーマット
};

} // namespace mitiru::gfx

#endif // _WIN32
