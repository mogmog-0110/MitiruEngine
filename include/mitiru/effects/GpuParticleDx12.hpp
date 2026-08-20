#pragma once

/// @file GpuParticleDx12.hpp
/// @brief DirectX 12 GPUパーティクルシステム実装
/// @details UAV付き構造化バッファ + コンピュートパイプラインによるGPUシミュレーションと、
///          Indirect Drawによるパーティクル描画を行う。
///          コンピュートとレンダリング間のリソースバリアを適切に管理する。
///          シェーダーソースは GpuParticleDx12_shaders_tables.hpp、
///          リソース・パイプライン構築の実装本体は detail/GpuParticleDx12_impl.hpp。
///
/// @code
/// auto device = std::make_unique<Dx12Device>(window);
/// auto particles = std::make_unique<GpuParticleDx12>(
///     device->nativeDevice(), device->commandQueue());
/// particles->setEmitter(emitter);
/// // 毎フレーム:
/// particles->emitFromEmitter(dt);
/// particles->update(dt);
/// particles->render(camera);
/// @endcode

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <random>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

#include <mitiru/effects/GpuParticleBase.hpp>
#include <mitiru/effects/GpuParticleDx12_shaders_tables.hpp>
#include <mitiru/render/Camera3D.hpp>

#include <mitiru/debug/TracyZones.hpp>

namespace mitiru::effects
{

/// @brief DirectX 12 GPUパーティクルシステム実装
/// @details UAV付き構造化バッファとコンピュートパイプラインによるシミュレーション。
///          Indirect Drawで生存パーティクル数のみを描画する。
///          コンピュート→レンダリング間のリソースバリアを管理する。
class GpuParticleDx12 final : public GpuParticleBase
{
public:
	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief コンストラクタ
	/// @param device D3D12デバイス
	/// @param commandQueue コマンドキュー
	/// @param maxParticles 最大パーティクル数
	explicit GpuParticleDx12(ID3D12Device* device,
	                         ID3D12CommandQueue* commandQueue,
	                         std::uint32_t maxParticles = MAX_GPU_PARTICLES)
		: GpuParticleBase(maxParticles)
		, m_device(device)
		, m_commandQueue(commandQueue)
	{
		if (!device || !commandQueue)
		{
			throw std::runtime_error(
				"GpuParticleDx12: device or commandQueue is null");
		}

		createCommandResources();
		createBuffers();
		createPipelines();
		createFence();
		m_valid = true;
	}

	/// @brief デストラクタ
	~GpuParticleDx12() override
	{
		waitForGpu();
		if (m_fenceEvent)
		{
			CloseHandle(m_fenceEvent);
			m_fenceEvent = nullptr;
		}
	}

	/// @brief GPUシミュレーションを実行する
	void update(float dt) override
	{
		MITIRU_ZONE_NAMED("Particle::GpuUpdate");
		if (!m_valid)
		{
			return;
		}

		/// ステージングパーティクルをアップロードする
		uploadStagingParticles();

		if (m_activeCount == 0)
		{
			return;
		}

		/// コマンドリストの準備
		m_commandAllocator->Reset();
		m_commandList->Reset(m_commandAllocator.Get(), nullptr);

		/// シミュレーション定数を更新する
		ParticleSimConstants simCB;
		simCB.deltaTime = dt;
		simCB.gravityX = m_emitter.gravity.x;
		simCB.gravityY = m_emitter.gravity.y;
		simCB.gravityZ = m_emitter.gravity.z;
		simCB.drag = m_emitter.drag;
		simCB.maxParticles = m_maxParticles;
		simCB.activeParticles = m_activeCount;

		updateUploadBuffer(m_simConstantUpload.Get(),
			&simCB, sizeof(simCB));

		/// パーティクルバッファを UAV 状態にする
		const std::uint32_t otherBuffer = 1 - m_currentBuffer;
		transitionResource(m_commandList.Get(),
			m_particleBuffer[otherBuffer].Get(),
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		/// IndirectArgsバッファをUAV状態にする
		transitionResource(m_commandList.Get(),
			m_indirectArgsBuffer.Get(),
			D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		/// コンピュートパイプラインを設定する
		m_commandList->SetComputeRootSignature(m_computeRootSignature.Get());
		m_commandList->SetPipelineState(m_computePSO.Get());

		/// デスクリプタヒープを設定する
		ID3D12DescriptorHeap* heaps[] = {m_srvUavHeap.Get()};
		m_commandList->SetDescriptorHeaps(1, heaps);

		/// ルートパラメータを設定する
		m_commandList->SetComputeRootConstantBufferView(
			0, m_simConstantUpload->GetGPUVirtualAddress());
		m_commandList->SetComputeRootDescriptorTable(
			1, srvGpuHandle(m_currentBuffer));
		m_commandList->SetComputeRootDescriptorTable(
			2, uavGpuHandle(otherBuffer));
		m_commandList->SetComputeRootDescriptorTable(
			3, indirectArgsUavGpuHandle());

		/// ディスパッチ
		const std::uint32_t groupCount =
			computeDispatchGroupCount(m_activeCount);
		m_commandList->Dispatch(groupCount, 1, 1);

		/// バリア: UAV → SRV (コンピュート出力 → レンダリング入力)
		transitionResource(m_commandList.Get(),
			m_particleBuffer[otherBuffer].Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		/// IndirectArgs: UAV → INDIRECT_ARGUMENT
		transitionResource(m_commandList.Get(),
			m_indirectArgsBuffer.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

		/// コマンドリストを閉じて実行する
		m_commandList->Close();
		ID3D12CommandList* cmdLists[] = {m_commandList.Get()};
		m_commandQueue->ExecuteCommandLists(1, cmdLists);

		waitForGpu();

		/// バッファをスワップする（ピンポン）
		m_currentBuffer = otherBuffer;

		/// 定期的なコンパクション
		if (shouldCompact())
		{
			compactDeadParticles();
		}
	}

	/// @brief パーティクルをIndirect Drawで描画する
	void render(const mitiru::render::Camera3D& camera) override
	{
		MITIRU_ZONE_NAMED("Particle::GpuRender");
		if (!m_valid || m_activeCount == 0)
		{
			return;
		}

		/// 描画定数を更新する
		ParticleRenderConstants renderCB;
		// D3D 規約 [0,1]。[-1,1] 版を渡すとパーティクルが常にメッシュの手前に来る。
		renderCB.viewProjection = camera.viewProjectionMatrixZO();
		renderCB.cameraRight = camera.rightDirection();
		renderCB.cameraUp = camera.upDirection();

		updateUploadBuffer(m_renderConstantUpload.Get(),
			&renderCB, sizeof(renderCB));

		/// コマンドリストの準備
		m_commandAllocator->Reset();
		m_commandList->Reset(m_commandAllocator.Get(), m_renderPSO.Get());

		/// デスクリプタヒープを設定する
		ID3D12DescriptorHeap* heaps[] = {m_srvUavHeap.Get()};
		m_commandList->SetDescriptorHeaps(1, heaps);

		/// グラフィックスルートシグネチャを設定する
		m_commandList->SetGraphicsRootSignature(m_renderRootSignature.Get());

		/// 定数バッファを設定する
		m_commandList->SetGraphicsRootConstantBufferView(
			0, m_renderConstantUpload->GetGPUVirtualAddress());

		/// パーティクルSRVを設定する
		m_commandList->SetGraphicsRootDescriptorTable(
			1, srvGpuHandle(m_currentBuffer));

		/// 入力アセンブラ（頂点バッファなし）
		m_commandList->IASetPrimitiveTopology(
			D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		/// Indirect Drawを実行する
		m_commandList->ExecuteIndirect(
			m_commandSignature.Get(),
			1,
			m_indirectArgsBuffer.Get(),
			0,
			nullptr,
			0);

		m_commandList->Close();

		ID3D12CommandList* cmdLists[] = {m_commandList.Get()};
		m_commandQueue->ExecuteCommandLists(1, cmdLists);

		waitForGpu();
	}

private:
	// ===== リソース・パイプライン構築の実装本体は detail/GpuParticleDx12_impl.hpp =====

	/// @brief コマンドアロケータとコマンドリストを生成する
	void createCommandResources();

	/// @brief 構造化バッファとアップロードバッファを生成する
	void createBuffers();

	/// @brief SRV/UAVデスクリプタヒープを生成する
	void createDescriptorHeap();

	/// @brief GPUハンドルヘルパー: SRV
	[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle(
		std::uint32_t bufferIndex) const noexcept
	{
		auto handle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
		handle.ptr += static_cast<UINT64>(bufferIndex) * m_descriptorSize;
		return handle;
	}

	/// @brief GPUハンドルヘルパー: UAV
	[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE uavGpuHandle(
		std::uint32_t bufferIndex) const noexcept
	{
		auto handle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
		handle.ptr += static_cast<UINT64>(2 + bufferIndex) * m_descriptorSize;
		return handle;
	}

	/// @brief GPUハンドルヘルパー: IndirectArgs UAV
	[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE indirectArgsUavGpuHandle() const noexcept
	{
		auto handle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
		handle.ptr += 4 * static_cast<UINT64>(m_descriptorSize);
		return handle;
	}

	/// @brief コンピュートパイプラインとレンダリングパイプラインを生成する
	void createPipelines();

	/// @brief コンピュートパイプラインを生成する
	void createComputePipeline();

	/// @brief レンダリングパイプラインを生成する
	void createRenderPipeline();

	/// @brief ExecuteIndirect用のコマンドシグネチャを生成する
	void createCommandSignature();

	/// @brief フェンスを生成する
	void createFence();

	/// @brief GPU処理の完了を待機する
	void waitForGpu()
	{
		if (!m_fence || !m_commandQueue)
		{
			return;
		}

		++m_fenceValue;
		m_commandQueue->Signal(m_fence.Get(), m_fenceValue);

		if (m_fence->GetCompletedValue() < m_fenceValue)
		{
			m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
			WaitForSingleObject(m_fenceEvent, INFINITE);
		}
	}

	/// @brief ステージングパーティクルをGPUにアップロードする
	void uploadStagingParticles()
	{
		const std::uint32_t uploadCount = prepareStagingUpload();
		if (uploadCount == 0)
		{
			return;
		}

		const auto uploadSize = uploadCount * sizeof(GpuParticle);
		const auto offset = m_activeCount * sizeof(GpuParticle);

		/// アップロードバッファにデータを書き込む
		updateUploadBuffer(m_uploadBuffer.Get(),
			m_stagingParticles.data(),
			static_cast<std::uint32_t>(uploadSize));

		/// コマンドリストでコピーする
		m_commandAllocator->Reset();
		m_commandList->Reset(m_commandAllocator.Get(), nullptr);

		/// パーティクルバッファをCOPY_DEST状態にする
		transitionResource(m_commandList.Get(),
			m_particleBuffer[m_currentBuffer].Get(),
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_COPY_DEST);

		m_commandList->CopyBufferRegion(
			m_particleBuffer[m_currentBuffer].Get(),
			offset,
			m_uploadBuffer.Get(),
			0,
			uploadSize);

		/// SRV状態に戻す
		transitionResource(m_commandList.Get(),
			m_particleBuffer[m_currentBuffer].Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		m_commandList->Close();
		ID3D12CommandList* cmdLists[] = {m_commandList.Get()};
		m_commandQueue->ExecuteCommandLists(1, cmdLists);

		waitForGpu();

		finalizeStagingUpload(uploadCount);
	}

	/// @brief 死亡パーティクルを除去するコンパクション処理
	void compactDeadParticles()
	{
		if (m_activeCount == 0)
		{
			return;
		}

		/// パーティクルバッファをリードバックにコピーする
		m_commandAllocator->Reset();
		m_commandList->Reset(m_commandAllocator.Get(), nullptr);

		transitionResource(m_commandList.Get(),
			m_particleBuffer[m_currentBuffer].Get(),
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_COPY_SOURCE);

		m_commandList->CopyBufferRegion(
			m_readbackBuffer.Get(), 0,
			m_particleBuffer[m_currentBuffer].Get(), 0,
			static_cast<UINT64>(m_activeCount) * sizeof(GpuParticle));

		transitionResource(m_commandList.Get(),
			m_particleBuffer[m_currentBuffer].Get(),
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		m_commandList->Close();
		ID3D12CommandList* cmdLists[] = {m_commandList.Get()};
		m_commandQueue->ExecuteCommandLists(1, cmdLists);
		waitForGpu();

		/// リードバックバッファから生存パーティクルを読み取る
		void* mapped = nullptr;
		D3D12_RANGE readRange = {
			0,
			static_cast<SIZE_T>(m_activeCount) * sizeof(GpuParticle)
		};
		HRESULT hr = m_readbackBuffer->Map(0, &readRange, &mapped);
		if (FAILED(hr))
		{
			return;
		}

		const auto* src = static_cast<const GpuParticle*>(mapped);
		auto alive = filterAliveParticles(src, m_activeCount);

		D3D12_RANGE writeRange = {0, 0};
		m_readbackBuffer->Unmap(0, &writeRange);

		/// 生存パーティクルを再アップロードする
		m_activeCount = static_cast<std::uint32_t>(alive.size());
		if (m_activeCount > 0)
		{
			const auto uploadSize = m_activeCount * sizeof(GpuParticle);
			updateUploadBuffer(m_uploadBuffer.Get(),
				alive.data(),
				static_cast<std::uint32_t>(uploadSize));

			m_commandAllocator->Reset();
			m_commandList->Reset(m_commandAllocator.Get(), nullptr);

			transitionResource(m_commandList.Get(),
				m_particleBuffer[m_currentBuffer].Get(),
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_COPY_DEST);

			m_commandList->CopyBufferRegion(
				m_particleBuffer[m_currentBuffer].Get(), 0,
				m_uploadBuffer.Get(), 0,
				uploadSize);

			transitionResource(m_commandList.Get(),
				m_particleBuffer[m_currentBuffer].Get(),
				D3D12_RESOURCE_STATE_COPY_DEST,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

			m_commandList->Close();
			ID3D12CommandList* lists[] = {m_commandList.Get()};
			m_commandQueue->ExecuteCommandLists(1, lists);
			waitForGpu();
		}
	}

	/// @brief アップロードバッファにデータを書き込む
	static void updateUploadBuffer(ID3D12Resource* buffer,
	                               const void* data,
	                               std::uint32_t dataSize)
	{
		void* mapped = nullptr;
		D3D12_RANGE readRange = {0, 0};
		HRESULT hr = buffer->Map(0, &readRange, &mapped);
		if (SUCCEEDED(hr))
		{
			std::memcpy(mapped, data, dataSize);
			D3D12_RANGE writeRange = {0, static_cast<SIZE_T>(dataSize)};
			buffer->Unmap(0, &writeRange);
		}
	}

	/// @brief リソースバリアを挿入する
	static void transitionResource(ID3D12GraphicsCommandList* cmdList,
	                               ID3D12Resource* resource,
	                               D3D12_RESOURCE_STATES before,
	                               D3D12_RESOURCE_STATES after)
	{
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = resource;
		barrier.Transition.StateBefore = before;
		barrier.Transition.StateAfter = after;
		barrier.Transition.Subresource =
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		cmdList->ResourceBarrier(1, &barrier);
	}

	/// @brief HLSL文字列をコンパイルする
	[[nodiscard]] static ComPtr<ID3DBlob> compileHLSL(
		std::string_view source,
		const char* entryPoint,
		const char* target)
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
			entryPoint, target,
			compileFlags, 0,
			shaderBlob.GetAddressOf(),
			errorBlob.GetAddressOf());

		if (FAILED(hr))
		{
			std::string errorMsg = "GpuParticleDx12: D3DCompile failed";
			if (errorBlob)
			{
				errorMsg += ": ";
				errorMsg += static_cast<const char*>(
					errorBlob->GetBufferPointer());
			}
			throw std::runtime_error(errorMsg);
		}

		return shaderBlob;
	}

	// ── DX12固有メンバ変数 ──────────────────────────────
	ID3D12Device* m_device = nullptr;                              ///< D3D12デバイス（非所有）
	ID3D12CommandQueue* m_commandQueue = nullptr;                  ///< コマンドキュー（非所有）

	/// コマンドリソース
	ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	ComPtr<ID3D12GraphicsCommandList> m_commandList;

	/// パーティクルバッファ（ピンポン）
	ComPtr<ID3D12Resource> m_particleBuffer[2];
	ComPtr<ID3D12Resource> m_uploadBuffer;                         ///< アップロードバッファ
	ComPtr<ID3D12Resource> m_readbackBuffer;                       ///< リードバックバッファ
	ComPtr<ID3D12Resource> m_indirectArgsBuffer;                   ///< Indirect Argsバッファ

	/// 定数バッファ
	ComPtr<ID3D12Resource> m_simConstantUpload;                    ///< シミュレーション定数
	ComPtr<ID3D12Resource> m_renderConstantUpload;                 ///< 描画定数

	/// デスクリプタヒープ
	ComPtr<ID3D12DescriptorHeap> m_srvUavHeap;
	UINT m_descriptorSize = 0;

	/// パイプラインステート
	ComPtr<ID3D12RootSignature> m_computeRootSignature;
	ComPtr<ID3D12PipelineState> m_computePSO;
	ComPtr<ID3D12RootSignature> m_renderRootSignature;
	ComPtr<ID3D12PipelineState> m_renderPSO;
	ComPtr<ID3D12CommandSignature> m_commandSignature;

	/// フェンス
	ComPtr<ID3D12Fence> m_fence;
	HANDLE m_fenceEvent = nullptr;
	UINT64 m_fenceValue = 0;
};

} // namespace mitiru::effects

// 実装本体（render/Renderer3D.hpp と同じ末尾 detail include 流儀）
#include <mitiru/effects/detail/GpuParticleDx12_impl.hpp>

#endif // _WIN32
