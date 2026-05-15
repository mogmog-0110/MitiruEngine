#pragma once

/// @file Dx12CommandList.hpp
/// @brief DirectX 12コマンドリスト実装
/// @details ID3D12GraphicsCommandListをラップし、遅延コマンド記録を行う
///          ICommandList実装。D3D12のコマンドリストセマンティクスに従い、
///          reset/begin/end/closeのライフサイクルを管理する。

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

#include <d3d12.h>
#include <wrl/client.h>

#include <sgc/types/Color.hpp>

#include <mitiru/gfx/GfxTypes.hpp>
#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/ICommandList.hpp>
#include <mitiru/gfx/IDescriptorHeap.hpp>
#include <mitiru/gfx/IRenderTarget.hpp>
#include <mitiru/gfx/IPipeline.hpp>
#include <mitiru/gfx/dx12/Dx12Buffer.hpp>
#include <mitiru/gfx/dx12/Dx12DescriptorHeap.hpp>
#include <mitiru/gfx/dx12/Dx12Pipeline.hpp>
#include <mitiru/gfx/dx12/Dx12RenderTarget.hpp>
#include <mitiru/gfx/dx12/Dx12Texture.hpp>

namespace mitiru::gfx
{

/// @brief DirectX 12コマンドリスト実装
/// @details ID3D12GraphicsCommandListを使い、描画コマンドを遅延記録する。
///          記録完了後、デバイスのexecuteCommandListsで実行する。
///
/// @code
/// commandList->reset();
/// commandList->begin();
/// commandList->setRenderTarget(target);
/// commandList->clearRenderTarget(color);
/// commandList->setPipeline(pipeline);
/// commandList->end();
/// commandList->close();
/// @endcode
class Dx12CommandList final : public ICommandList
{
public:
	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief コンストラクタ
	/// @param device D3D12デバイス
	Dx12CommandList(ID3D12Device* device)
	{
		if (!device)
		{
			throw std::runtime_error(
				"Dx12CommandList: device is null");
		}

		/// コマンドアロケータを生成する
		HRESULT hr = device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(m_allocator.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx12CommandList: CreateCommandAllocator failed");
		}

		/// コマンドリストを生成する（クローズ状態で生成）
		hr = device->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			m_allocator.Get(),
			nullptr,
			IID_PPV_ARGS(m_commandList.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx12CommandList: CreateCommandList failed");
		}

		/// 初期状態はクローズにする
		m_commandList->Close();
		m_closed = true;
	}

	/// @brief コマンドリストをリセットする
	/// @details アロケータとコマンドリストをリセットし、記録可能な状態にする。
	void reset() override
	{
		HRESULT hr = m_allocator->Reset();
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx12CommandList: allocator Reset failed");
		}

		hr = m_commandList->Reset(m_allocator.Get(), nullptr);
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx12CommandList: commandList Reset failed");
		}

		m_recording = true;
		m_closed = false;
	}

	/// @brief コマンド記録を開始する
	/// @details reset()の後に呼ぶ。reset()がまだ呼ばれていない場合はreset()も行う。
	void begin() override
	{
		if (m_closed)
		{
			reset();
		}
		m_recording = true;
	}

	/// @brief コマンド記録を終了する
	void end() override
	{
		m_recording = false;
	}

	/// @brief コマンドリストを閉じる
	/// @details 実行前にクローズが必要。
	void close() override
	{
		if (!m_closed)
		{
			m_commandList->Close();
			m_closed = true;
			m_recording = false;
		}
	}

	/// @brief レンダーターゲットを設定する
	/// @param target 描画先のレンダーターゲット
	void setRenderTarget(IRenderTarget* target) override
	{
		if (!m_recording || !target)
		{
			return;
		}

		auto* dx12RT = dynamic_cast<Dx12RenderTarget*>(target);
		if (!dx12RT)
		{
			return;
		}

		auto rtvHandle = dx12RT->rtvHandle();
		m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
		m_currentRT = dx12RT;
	}

	/// @brief レンダーターゲットをクリアする
	/// @param color クリア色
	void clearRenderTarget(const sgc::Colorf& color) override
	{
		if (!m_recording || !m_currentRT)
		{
			return;
		}

		const float clearColor[4] = {
			color.r, color.g, color.b, color.a
		};
		auto rtvHandle = m_currentRT->rtvHandle();
		m_commandList->ClearRenderTargetView(
			rtvHandle, clearColor, 0, nullptr);
	}

	/// @brief パイプライン状態を設定する
	/// @param pipeline 使用するパイプライン
	void setPipeline(IPipeline* pipeline) override
	{
		if (!m_recording || !pipeline)
		{
			return;
		}

		auto* dx12Pipeline = dynamic_cast<Dx12Pipeline*>(pipeline);
		if (!dx12Pipeline)
		{
			return;
		}

		m_commandList->SetPipelineState(dx12Pipeline->nativePSO());
		m_commandList->SetGraphicsRootSignature(
			dx12Pipeline->nativeRootSignature());
		m_commandList->IASetPrimitiveTopology(
			D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}

	/// @brief 頂点バッファを設定する
	/// @param buffer 頂点バッファ
	void setVertexBuffer(IBuffer* buffer) override
	{
		if (!m_recording || !buffer)
		{
			return;
		}

		auto* dx12Buf = dynamic_cast<Dx12Buffer*>(buffer);
		if (!dx12Buf)
		{
			return;
		}

		/// Vertex2D: pos(2) + uv(2) + color(4) = float * 8
		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		vbv.BufferLocation = dx12Buf->nativeResource()->GetGPUVirtualAddress();
		vbv.SizeInBytes = dx12Buf->size();
		vbv.StrideInBytes = sizeof(float) * 8;

		m_commandList->IASetVertexBuffers(0, 1, &vbv);
	}

	/// @brief インデックスバッファを設定する
	/// @param buffer インデックスバッファ
	void setIndexBuffer(IBuffer* buffer) override
	{
		if (!m_recording || !buffer)
		{
			return;
		}

		auto* dx12Buf = dynamic_cast<Dx12Buffer*>(buffer);
		if (!dx12Buf)
		{
			return;
		}

		D3D12_INDEX_BUFFER_VIEW ibv = {};
		ibv.BufferLocation = dx12Buf->nativeResource()->GetGPUVirtualAddress();
		ibv.SizeInBytes = dx12Buf->size();
		ibv.Format = DXGI_FORMAT_R32_UINT;

		m_commandList->IASetIndexBuffer(&ibv);
	}

	/// @brief インデックス付き描画を実行する
	/// @param indexCount 描画するインデックス数
	/// @param startIndex 開始インデックスオフセット
	/// @param baseVertex ベース頂点オフセット
	void drawIndexed(std::uint32_t indexCount,
	                 std::uint32_t startIndex,
	                 std::int32_t baseVertex) override
	{
		if (!m_recording)
		{
			return;
		}

		m_commandList->DrawIndexedInstanced(
			indexCount, 1, startIndex, baseVertex, 0);
	}

	/// @brief 頂点のみで描画を実行する
	/// @param vertexCount 描画する頂点数
	/// @param startVertex 開始頂点オフセット
	void draw(std::uint32_t vertexCount,
	          std::uint32_t startVertex) override
	{
		if (!m_recording)
		{
			return;
		}

		m_commandList->DrawInstanced(
			vertexCount, 1, startVertex, 0);
	}

	/// @brief ビューポートを設定する
	/// @param width ビューポート幅
	/// @param height ビューポート高さ
	void setViewport(float width, float height) override
	{
		if (!m_recording)
		{
			return;
		}

		D3D12_VIEWPORT vp = {};
		vp.Width = width;
		vp.Height = height;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		vp.TopLeftX = 0.0f;
		vp.TopLeftY = 0.0f;
		m_commandList->RSSetViewports(1, &vp);

		D3D12_RECT scissor = {};
		scissor.right = static_cast<LONG>(width);
		scissor.bottom = static_cast<LONG>(height);
		m_commandList->RSSetScissorRects(1, &scissor);
	}

	/// @brief リソースバリアを発行する
	/// @param resource バリア対象のテクスチャリソース
	/// @param before 遷移前のリソース状態
	/// @param after 遷移後のリソース状態
	void resourceBarrier(
		ITexture* resource, ResourceState before, ResourceState after) override
	{
		if (!m_recording)
		{
			return;
		}

		/// Dx12Textureからリソースを取得する
		auto* dx12Tex = dynamic_cast<Dx12Texture*>(resource);
		ID3D12Resource* d3dResource = nullptr;
		if (dx12Tex)
		{
			d3dResource = dx12Tex->nativeResource();
		}

		if (!d3dResource)
		{
			return;
		}

		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = d3dResource;
		barrier.Transition.StateBefore = toD3D12State(before);
		barrier.Transition.StateAfter = toD3D12State(after);
		barrier.Transition.Subresource =
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		m_commandList->ResourceBarrier(1, &barrier);
	}

	/// @brief ID3D12Resourceに対してバリアを発行する（DX12固有）
	/// @param resource D3D12リソース
	/// @param before 遷移前の状態
	/// @param after 遷移後の状態
	void resourceBarrierDirect(
		ID3D12Resource* resource,
		D3D12_RESOURCE_STATES before,
		D3D12_RESOURCE_STATES after)
	{
		if (!m_recording || !resource)
		{
			return;
		}

		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = resource;
		barrier.Transition.StateBefore = before;
		barrier.Transition.StateAfter = after;
		barrier.Transition.Subresource =
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		m_commandList->ResourceBarrier(1, &barrier);
	}

	/// @brief ルートシグネチャを設定する
	/// @param rootSignature ルートシグネチャのネイティブポインタ
	void setRootSignature(void* rootSignature) override
	{
		if (!m_recording || !rootSignature)
		{
			return;
		}

		m_commandList->SetGraphicsRootSignature(
			static_cast<ID3D12RootSignature*>(rootSignature));
	}

	/// @brief デスクリプタヒープを設定する
	/// @param heaps デスクリプタヒープの配列
	/// @param count ヒープ数
	void setDescriptorHeaps(
		IDescriptorHeap* const* heaps, uint32_t count) override
	{
		if (!m_recording || !heaps || count == 0)
		{
			return;
		}

		/// ネイティブヒープポインタの配列を構築する
		ID3D12DescriptorHeap* nativeHeaps[8] = {};
		const uint32_t heapCount =
			count < 8 ? count : 8;

		for (uint32_t i = 0; i < heapCount; ++i)
		{
			auto* dx12Heap = dynamic_cast<Dx12DescriptorHeap*>(heaps[i]);
			if (dx12Heap)
			{
				nativeHeaps[i] = dx12Heap->nativeHeap();
			}
		}

		m_commandList->SetDescriptorHeaps(heapCount, nativeHeaps);
	}

	/// @brief ルートデスクリプタテーブルを設定する
	/// @param paramIndex ルートパラメータインデックス
	/// @param handle GPUデスクリプタハンドル
	void setGraphicsRootDescriptorTable(
		uint32_t paramIndex, GpuDescriptorHandle handle) override
	{
		if (!m_recording)
		{
			return;
		}

		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
		gpuHandle.ptr = handle.ptr;
		m_commandList->SetGraphicsRootDescriptorTable(
			paramIndex, gpuHandle);
	}

	/// @brief ルート定数バッファビューを設定する
	/// @param paramIndex ルートパラメータインデックス
	/// @param gpuVirtualAddress GPUバッファの仮想アドレス
	void setGraphicsRootCBV(
		uint32_t paramIndex, uint64_t gpuVirtualAddress) override
	{
		if (!m_recording)
		{
			return;
		}

		m_commandList->SetGraphicsRootConstantBufferView(
			paramIndex,
			static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(gpuVirtualAddress));
	}

	/// @brief 内部のID3D12GraphicsCommandListを取得する
	/// @return コマンドリストへのポインタ
	[[nodiscard]] ID3D12GraphicsCommandList* nativeCommandList() const noexcept
	{
		return m_commandList.Get();
	}

private:
	/// @brief ResourceStateをD3D12_RESOURCE_STATESに変換する
	/// @param state 変換元のリソース状態
	/// @return D3D12のリソース状態
	[[nodiscard]] static D3D12_RESOURCE_STATES toD3D12State(
		ResourceState state) noexcept
	{
		switch (state)
		{
		case ResourceState::Common:
			return D3D12_RESOURCE_STATE_COMMON;
		case ResourceState::RenderTarget:
			return D3D12_RESOURCE_STATE_RENDER_TARGET;
		case ResourceState::Present:
			return D3D12_RESOURCE_STATE_PRESENT;
		case ResourceState::ShaderResource:
			return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		case ResourceState::DepthWrite:
			return D3D12_RESOURCE_STATE_DEPTH_WRITE;
		case ResourceState::CopyDest:
			return D3D12_RESOURCE_STATE_COPY_DEST;
		case ResourceState::CopySrc:
			return D3D12_RESOURCE_STATE_COPY_SOURCE;
		case ResourceState::UnorderedAccess:
			return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		default:
			return D3D12_RESOURCE_STATE_COMMON;
		}
	}

	ComPtr<ID3D12CommandAllocator> m_allocator;          ///< コマンドアロケータ
	ComPtr<ID3D12GraphicsCommandList> m_commandList;     ///< グラフィックスコマンドリスト
	Dx12RenderTarget* m_currentRT = nullptr;              ///< 現在のレンダーターゲット
	bool m_recording = false;                             ///< 記録中フラグ
	bool m_closed = false;                                ///< クローズ済みフラグ
};

} // namespace mitiru::gfx

#endif // _WIN32
