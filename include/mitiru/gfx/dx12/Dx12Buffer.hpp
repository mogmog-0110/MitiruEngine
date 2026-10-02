#pragma once

/// @file Dx12Buffer.hpp
/// @brief DirectX 12 GPU バッファ実装
/// @details ID3D12Resource をラップし、頂点・インデックス・定数バッファを統一的に管理する。
///          動的バッファはアップロードヒープ、静的バッファはデフォルトヒープを使用する。

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

#include <d3d12.h>
#include <wrl/client.h>

#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/dx12/Dx12GpuMemory.hpp>

namespace mitiru::gfx
{

/// @brief DirectX 12 GPU バッファ実装
/// @details アップロードヒープ（動的）またはデフォルトヒープ（静的）の
///          ID3D12Resource をラップする。
///
/// @code
/// auto vb = device->createBuffer(BufferType::Vertex, sizeof(vertices), true, vertices);
/// auto gpuAddr = vb->gpuVirtualAddress();
/// @endcode
class Dx12Buffer final : public IBuffer
{
public:
	/// @brief ComPtr エイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief コンストラクタ
	/// @param device D3D12 デバイス
	/// @param bufferType バッファ種別
	/// @param sizeBytes バッファサイズ（バイト）
	/// @param dynamic 動的更新が必要か
	/// @param initialData 初期データ（nullptr で初期化なし）
	Dx12Buffer(ID3D12Device* device,
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
				"Dx12Buffer: device is null");
		}

		/// 定数バッファは 256 バイトアライメントが必要
		uint32_t alignedSize = sizeBytes;
		if (bufferType == BufferType::Constant)
		{
			alignedSize = (sizeBytes + 255) & ~255u;
		}
		m_alignedSize = alignedSize;

		if (dynamic)
		{
			createUploadBuffer(device, alignedSize, initialData);
		}
		else
		{
			createDefaultBuffer(device, alignedSize, initialData);
		}
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

	/// @brief GPU 仮想アドレスを取得する
	/// @return バッファの GPU 仮想アドレス
	[[nodiscard]] uint64_t gpuVirtualAddress() const override
	{
		if (m_resource)
		{
			return m_resource->GetGPUVirtualAddress();
		}
		return 0;
	}

	/// @brief 動的バッファかどうかを判定する
	[[nodiscard]] bool isDynamic() const noexcept
	{
		return m_dynamic;
	}

	/// @brief アライメント済みサイズを取得する
	/// @return アライメント済みのバッファサイズ（バイト）
	[[nodiscard]] uint32_t alignedSize() const noexcept
	{
		return m_alignedSize;
	}

	/// @brief バッファ内容を更新する（動的バッファ専用）
	/// @param data 書き込むデータ
	/// @param dataSize データサイズ（バイト）
	void update(const void* data, std::uint32_t dataSize) override
	{
		if (!m_dynamic || !data || dataSize > m_sizeBytes)
		{
			return;
		}

		void* mapped = nullptr;
		D3D12_RANGE readRange = {0, 0};
		HRESULT hr = m_resource->Map(0, &readRange, &mapped);
		if (FAILED(hr))
		{
			return;
		}

		std::memcpy(mapped, data, dataSize);

		D3D12_RANGE writeRange = {0, static_cast<SIZE_T>(dataSize)};
		m_resource->Unmap(0, &writeRange);
	}

	/// @brief 内部の ID3D12Resource を取得する
	/// @return リソースへのポインタ
	[[nodiscard]] ID3D12Resource* nativeResource() const noexcept
	{
		return m_resource.Get();
	}

private:
	/// @brief アップロードヒープ上にバッファを生成する（動的バッファ用）
	/// @param device D3D12 デバイス
	/// @param alignedSize アライメント済みサイズ
	/// @param initialData 初期データ
	void createUploadBuffer(ID3D12Device* device,
	                        uint32_t alignedSize,
	                        const void* initialData)
	{
		HRESULT hr = createGpuBuffer(device, D3D12_HEAP_TYPE_UPLOAD, alignedSize,
			D3D12_RESOURCE_STATE_GENERIC_READ, m_resource);
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx12Buffer: upload buffer allocation failed");
		}

		/// 初期データがあればコピーする
		if (initialData)
		{
			update(initialData, m_sizeBytes);
		}
	}

	/// @brief デフォルトヒープ上にバッファを生成する（静的バッファ用）
	/// @param device D3D12 デバイス
	/// @param alignedSize アライメント済みサイズ
	/// @param initialData 初期データ
	/// @note 初期データがある場合、アップロードヒープ経由でコピーする必要があるが、
	///       ここではアップロードヒープとして生成し、コマンドリスト経由のコピーを省略する。
	///       本格的な実装ではコピーキューを使用すべき。
	void createDefaultBuffer(ID3D12Device* device,
	                         uint32_t alignedSize,
	                         const void* initialData)
	{
		/// NOTE: 静的バッファでも初期データ転送の簡素化のため
		///       アップロードヒープを使用する。
		///       本番環境ではデフォルトヒープ + コピーキューが望ましい。
		createUploadBuffer(device, alignedSize, initialData);
	}

	GpuResource m_resource;             ///< D3D12リソース
	BufferType m_type;                  ///< バッファ種別
	std::uint32_t m_sizeBytes;          ///< 要求サイズ（バイト）
	std::uint32_t m_alignedSize = 0;    ///< アライメント済みサイズ
	bool m_dynamic;                     ///< 動的更新フラグ
};

} // namespace mitiru::gfx

#endif // _WIN32
