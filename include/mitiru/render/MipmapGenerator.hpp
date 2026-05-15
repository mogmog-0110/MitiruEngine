#pragma once

/// @file MipmapGenerator.hpp
/// @brief GPU ミップマップ生成
/// @details DX11の組み込みミップマップ生成機能を使い、テクスチャの
///          フルミップチェーンをGPU側で自動生成する。
///          異方性フィルタリング用サンプラーの生成もサポートする。

#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <d3d11.h>
#include <wrl/client.h>

namespace mitiru::render
{

/// @brief ミップマップフィルタモード
enum class MipmapFilterMode : std::uint8_t
{
	Box,      ///< ボックスフィルタ（高速・標準品質）
	Lanczos   ///< Lanczosフィルタ（高品質・低速）
};

/// @brief ミップマップ生成設定
struct MipmapConfig
{
	MipmapFilterMode filterMode = MipmapFilterMode::Box;  ///< フィルタモード
	std::uint32_t maxLevel = 0;          ///< 最大ミップレベル（0 = フルチェーン）
	std::uint32_t anisotropicLevel = 1;  ///< 異方性フィルタリングレベル（1-16）
};

/// @brief GPUミップマップ生成器
/// @details DX11のGenerateMips()を使い、テクスチャのフルミップチェーンを
///          GPU側で自動生成する。テクスチャ生成・SRV作成・サンプラー作成を
///          ワンストップで提供する。
///
/// @code
/// mitiru::render::MipmapGenerator mipGen;
///
/// auto* texture = mipGen.createTextureWithMipmaps(
///     device, 256, 256, pixelData.data());
///
/// auto* srv = mipGen.createSRVWithMipmaps(device, texture);
///
/// mipGen.generateMipmaps(device, context, texture);
///
/// auto* sampler = mipGen.createAnisotropicSampler(device, 16);
/// @endcode
class MipmapGenerator
{
public:
	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief デフォルトコンストラクタ
	MipmapGenerator() noexcept = default;

	/// @brief 既存テクスチャに対してフルミップチェーンを生成する
	/// @param device D3D11デバイス
	/// @param context D3D11デバイスコンテキスト
	/// @param texture ミップ生成対象のテクスチャ
	/// @details テクスチャは D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE
	///          および D3D11_RESOURCE_MISC_GENERATE_MIPS フラグ付きで作成されている必要がある。
	void generateMipmaps(ID3D11Device* device,
	                     ID3D11DeviceContext* context,
	                     ID3D11Texture2D* texture)
	{
		if (!device || !context || !texture)
		{
			throw std::invalid_argument(
				"MipmapGenerator::generateMipmaps: null argument");
		}

		/// SRVを作成してGenerateMipsを呼び出す
		ComPtr<ID3D11ShaderResourceView> srv;
		D3D11_TEXTURE2D_DESC texDesc = {};
		texture->GetDesc(&texDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = texDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = static_cast<UINT>(-1);

		HRESULT hr = device->CreateShaderResourceView(
			texture, &srvDesc, srv.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"MipmapGenerator: CreateShaderResourceView failed");
		}

		context->GenerateMips(srv.Get());
	}

	/// @brief ミップマップ付きテクスチャを新規作成する
	/// @param device D3D11デバイス
	/// @param width テクスチャ幅（ピクセル）
	/// @param height テクスチャ高さ（ピクセル）
	/// @param pixels RGBA8ピクセルデータ（幅 * 高さ * 4バイト）
	/// @return 作成されたテクスチャ（ComPtr管理）
	[[nodiscard]] ComPtr<ID3D11Texture2D> createTextureWithMipmaps(
		ID3D11Device* device,
		std::uint32_t width,
		std::uint32_t height,
		const void* pixels)
	{
		if (!device)
		{
			throw std::invalid_argument(
				"MipmapGenerator::createTextureWithMipmaps: device is null");
		}
		if (width == 0 || height == 0)
		{
			throw std::invalid_argument(
				"MipmapGenerator::createTextureWithMipmaps: invalid dimensions");
		}

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 0;  ///< フルミップチェーンを自動計算
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET
		               | D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

		/// 最大ミップレベルを制限する場合
		if (m_config.maxLevel > 0)
		{
			const auto fullLevels = calculateMipLevels(width, height);
			desc.MipLevels = (std::min)(
				static_cast<UINT>(m_config.maxLevel),
				static_cast<UINT>(fullLevels));
		}

		ComPtr<ID3D11Texture2D> texture;
		HRESULT hr = device->CreateTexture2D(
			&desc, nullptr, texture.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"MipmapGenerator: CreateTexture2D failed");
		}

		/// ミップレベル0にピクセルデータを書き込む
		if (pixels)
		{
			ComPtr<ID3D11DeviceContext> context;
			device->GetImmediateContext(context.GetAddressOf());

			const UINT rowPitch = width * 4;
			context->UpdateSubresource(
				texture.Get(), 0, nullptr,
				pixels, rowPitch, 0);
		}

		return texture;
	}

	/// @brief ミップマップ付きSRVを作成する
	/// @param device D3D11デバイス
	/// @param texture 対象テクスチャ
	/// @return 作成されたSRV（ComPtr管理）
	[[nodiscard]] ComPtr<ID3D11ShaderResourceView> createSRVWithMipmaps(
		ID3D11Device* device,
		ID3D11Texture2D* texture)
	{
		if (!device || !texture)
		{
			throw std::invalid_argument(
				"MipmapGenerator::createSRVWithMipmaps: null argument");
		}

		D3D11_TEXTURE2D_DESC texDesc = {};
		texture->GetDesc(&texDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = texDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = static_cast<UINT>(-1);

		ComPtr<ID3D11ShaderResourceView> srv;
		HRESULT hr = device->CreateShaderResourceView(
			texture, &srvDesc, srv.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"MipmapGenerator: CreateShaderResourceView failed");
		}

		return srv;
	}

	/// @brief 異方性フィルタリングサンプラーを作成する
	/// @param device D3D11デバイス
	/// @param level 異方性フィルタリングレベル（1-16）
	/// @return 作成されたサンプラーステート（ComPtr管理）
	[[nodiscard]] ComPtr<ID3D11SamplerState> createAnisotropicSampler(
		ID3D11Device* device,
		std::uint32_t level)
	{
		if (!device)
		{
			throw std::invalid_argument(
				"MipmapGenerator::createAnisotropicSampler: device is null");
		}

		const auto clampedLevel = (std::max)(1u, (std::min)(level, 16u));

		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = (clampedLevel > 1)
			? D3D11_FILTER_ANISOTROPIC
			: D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.MipLODBias = 0.0f;
		samplerDesc.MaxAnisotropy = static_cast<UINT>(clampedLevel);
		samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		samplerDesc.MinLOD = 0.0f;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

		ComPtr<ID3D11SamplerState> sampler;
		HRESULT hr = device->CreateSamplerState(
			&samplerDesc, sampler.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"MipmapGenerator: CreateSamplerState failed");
		}

		return sampler;
	}

	/// @brief 設定を取得する
	[[nodiscard]] const MipmapConfig& config() const noexcept
	{
		return m_config;
	}

	/// @brief 設定を変更する
	/// @param cfg 新しい設定
	void setConfig(const MipmapConfig& cfg) noexcept
	{
		m_config = cfg;
	}

	/// @brief テクスチャサイズからフルミップレベル数を計算する
	/// @param width テクスチャ幅
	/// @param height テクスチャ高さ
	/// @return ミップレベル数
	[[nodiscard]] static std::uint32_t calculateMipLevels(
		std::uint32_t width, std::uint32_t height) noexcept
	{
		if (width == 0 || height == 0)
		{
			return 0;
		}
		const auto maxDim = (std::max)(width, height);
		return static_cast<std::uint32_t>(
			std::floor(std::log2(static_cast<double>(maxDim)))) + 1;
	}

private:
	MipmapConfig m_config;  ///< ミップマップ生成設定
};

} // namespace mitiru::render

#endif // _WIN32
