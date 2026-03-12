#pragma once

/// @file Dx11Buffer.hpp
/// @brief DirectX 11 GPUバッファ実装
/// @details ID3D11Bufferをラップし、頂点・インデックス・定数バッファを統一的に管理する。
///          動的バッファはMAP_WRITE_DISCARDで毎フレーム更新可能。

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

#include <d3d11.h>
#include <wrl/client.h>

#include <mitiru/gfx/IBuffer.hpp>

namespace mitiru::gfx
{

/// @brief DirectX 11 GPUバッファ実装
/// @details 頂点バッファ・インデックスバッファ・定数バッファをラップする。
///          動的バッファではMAP_WRITE_DISCARDによる高速更新が可能。
class Dx11Buffer final : public IBuffer
{
public:
	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief コンストラクタ
	/// @param device D3D11デバイス
	/// @param bufferType バッファ種別
	/// @param sizeBytes バッファサイズ（バイト）
	/// @param dynamic 動的更新が必要か
	/// @param initialData 初期データ（nullptrで初期化なし）
	Dx11Buffer(ID3D11Device* device,
	           BufferType bufferType,
	           std::uint32_t sizeBytes,
	           bool dynamic,
	           const void* initialData = nullptr)
		: m_type(bufferType)
		, m_sizeBytes(sizeBytes)
		, m_dynamic(dynamic)
	{
		if (!device)
		{
			throw std::runtime_error(
				"Dx11Buffer: device is null");
		}

		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeBytes;
		desc.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
		desc.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;

		switch (bufferType)
		{
		case BufferType::Vertex:
			desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			break;
		case BufferType::Index:
			desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
			break;
		case BufferType::Constant:
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			break;
		}

		D3D11_SUBRESOURCE_DATA initData = {};
		const D3D11_SUBRESOURCE_DATA* pInitData = nullptr;
		if (initialData)
		{
			initData.pSysMem = initialData;
			pInitData = &initData;
		}

		HRESULT hr = device->CreateBuffer(
			&desc, pInitData, m_buffer.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx11Buffer: CreateBuffer failed");
		}
	}

	/// @brief バッファ内容を更新する（動的バッファ専用）
	/// @param context D3D11デバイスコンテキスト
	/// @param data 書き込むデータ
	/// @param dataSize データサイズ（バイト）
	/// @return 成功したらtrue
	bool update(ID3D11DeviceContext* context,
	            const void* data,
	            std::uint32_t dataSize)
	{
		if (!m_dynamic || !context || !data || dataSize > m_sizeBytes)
		{
			return false;
		}

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		HRESULT hr = context->Map(
			m_buffer.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (FAILED(hr))
		{
			return false;
		}

		std::memcpy(mapped.pData, data, dataSize);
		context->Unmap(m_buffer.Get(), 0);
		return true;
	}

	/// @brief バッファサイズを取得する
	[[nodiscard]] std::uint32_t size() const noexcept override
	{
		return m_sizeBytes;
	}

	/// @brief バッファ種別を取得する
	[[nodiscard]] BufferType type() const noexcept override
	{
		return m_type;
	}

	/// @brief 動的バッファかどうかを判定する
	[[nodiscard]] bool isDynamic() const noexcept
	{
		return m_dynamic;
	}

	/// @brief 内部のID3D11Bufferを取得する
	/// @return バッファへのポインタ
	[[nodiscard]] ID3D11Buffer* getD3DBuffer() const noexcept
	{
		return m_buffer.Get();
	}

private:
	ComPtr<ID3D11Buffer> m_buffer;  ///< D3D11バッファ
	BufferType m_type;              ///< バッファ種別
	std::uint32_t m_sizeBytes;      ///< サイズ（バイト）
	bool m_dynamic;                 ///< 動的更新フラグ
};

} // namespace mitiru::gfx

#endif // _WIN32
