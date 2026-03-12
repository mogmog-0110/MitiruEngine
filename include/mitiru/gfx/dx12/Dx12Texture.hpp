#pragma once

/// @file Dx12Texture.hpp
/// @brief DirectX 12テクスチャ実装
/// @details ID3D12Resourceをラップし、テクスチャリソースの管理を行う。
///          SRVデスクリプタとの関連付けもサポートする。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <span>
#include <stdexcept>

#include <d3d12.h>
#include <wrl/client.h>

#include <mitiru/gfx/GfxTypes.hpp>
#include <mitiru/gfx/ITexture.hpp>

namespace mitiru::gfx
{

/// @brief DirectX 12テクスチャ実装
/// @details ID3D12Resourceとしてテクスチャリソースを管理する。
///          レンダーターゲットやシェーダーリソースとして使用可能。
class Dx12Texture final : public ITexture
{
public:
	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief デフォルトコンストラクタ
	Dx12Texture() = default;

	/// @brief 既存リソースからテクスチャを構築する
	/// @param resource 既存のID3D12Resource
	/// @param width テクスチャ幅
	/// @param height テクスチャ高さ
	/// @param format ピクセルフォーマット
	Dx12Texture(ComPtr<ID3D12Resource> resource,
	            int width, int height,
	            PixelFormat format = PixelFormat::RGBA8)
		: m_resource(std::move(resource))
		, m_width(width)
		, m_height(height)
		, m_format(format)
	{
	}

	/// @brief ピクセルデータから新規テクスチャを生成する
	/// @param device D3D12デバイス
	/// @param width テクスチャ幅
	/// @param height テクスチャ高さ
	/// @param data RGBA8形式のピクセルデータ
	/// @return 生成されたテクスチャ
	/// @note アップロードヒープ経由のデータ転送にはコマンドリストが必要。
	///       ここではリソースのみ生成し、データ転送は呼び出し側が行う。
	[[nodiscard]] static Dx12Texture createEmpty(
		ID3D12Device* device,
		int width, int height,
		PixelFormat format = PixelFormat::RGBA8)
	{
		if (!device)
		{
			throw std::runtime_error(
				"Dx12Texture: device is null");
		}

		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width = static_cast<UINT64>(width);
		desc.Height = static_cast<UINT>(height);
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = toDxgiFormat(format);
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;

		const D3D12_HEAP_PROPERTIES heapProps = {
			D3D12_HEAP_TYPE_DEFAULT,
			D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
			D3D12_MEMORY_POOL_UNKNOWN, 0, 0
		};

		Dx12Texture texture;
		texture.m_width = width;
		texture.m_height = height;
		texture.m_format = format;

		HRESULT hr = device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(texture.m_resource.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx12Texture: CreateCommittedResource failed");
		}

		return texture;
	}

	/// @brief レンダーターゲット用テクスチャを生成する
	/// @param device D3D12デバイス
	/// @param width テクスチャ幅
	/// @param height テクスチャ高さ
	/// @param format ピクセルフォーマット
	/// @return 生成されたテクスチャ
	[[nodiscard]] static Dx12Texture createRenderTarget(
		ID3D12Device* device,
		int width, int height,
		PixelFormat format = PixelFormat::RGBA8)
	{
		if (!device)
		{
			throw std::runtime_error(
				"Dx12Texture: device is null");
		}

		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width = static_cast<UINT64>(width);
		desc.Height = static_cast<UINT>(height);
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = toDxgiFormat(format);
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		const D3D12_HEAP_PROPERTIES heapProps = {
			D3D12_HEAP_TYPE_DEFAULT,
			D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
			D3D12_MEMORY_POOL_UNKNOWN, 0, 0
		};

		const float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
		D3D12_CLEAR_VALUE clearValue = {};
		clearValue.Format = desc.Format;
		clearValue.Color[0] = clearColor[0];
		clearValue.Color[1] = clearColor[1];
		clearValue.Color[2] = clearColor[2];
		clearValue.Color[3] = clearColor[3];

		Dx12Texture texture;
		texture.m_width = width;
		texture.m_height = height;
		texture.m_format = format;

		HRESULT hr = device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			&clearValue,
			IID_PPV_ARGS(texture.m_resource.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx12Texture: CreateCommittedResource (RT) failed");
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

	/// @brief 内部のID3D12Resourceを取得する
	/// @return リソースへのポインタ
	[[nodiscard]] ID3D12Resource* nativeResource() const noexcept
	{
		return m_resource.Get();
	}

	/// @brief SRVデスクリプタハンドルを設定する
	/// @param handle CPUデスクリプタハンドル
	void setSrvHandle(CpuDescriptorHandle handle) noexcept
	{
		m_srvHandle = handle;
	}

	/// @brief SRVデスクリプタハンドルを取得する
	/// @return CPUデスクリプタハンドル
	[[nodiscard]] CpuDescriptorHandle srvHandle() const noexcept
	{
		return m_srvHandle;
	}

private:
	/// @brief PixelFormatをDXGI_FORMATに変換する
	/// @param format 変換元のピクセルフォーマット
	/// @return DXGIフォーマット
	[[nodiscard]] static DXGI_FORMAT toDxgiFormat(PixelFormat format) noexcept
	{
		switch (format)
		{
		case PixelFormat::RGBA8:
			return DXGI_FORMAT_R8G8B8A8_UNORM;
		case PixelFormat::BGRA8:
			return DXGI_FORMAT_B8G8R8A8_UNORM;
		case PixelFormat::R8:
			return DXGI_FORMAT_R8_UNORM;
		case PixelFormat::Depth24Stencil8:
			return DXGI_FORMAT_D24_UNORM_S8_UINT;
		default:
			return DXGI_FORMAT_R8G8B8A8_UNORM;
		}
	}

	ComPtr<ID3D12Resource> m_resource;           ///< テクスチャリソース
	int m_width = 0;                              ///< テクスチャ幅
	int m_height = 0;                             ///< テクスチャ高さ
	PixelFormat m_format = PixelFormat::RGBA8;    ///< ピクセルフォーマット
	CpuDescriptorHandle m_srvHandle = {};         ///< SRVデスクリプタハンドル
};

} // namespace mitiru::gfx

#endif // _WIN32
