#pragma once

/// @file GpuParticleDx12.hpp
/// @brief DirectX 12 GPUパーティクルシステム実装
/// @details UAV付き構造化バッファ + コンピュートパイプラインによるGPUシミュレーションと、
///          Indirect Drawによるパーティクル描画を行う。
///          コンピュートとレンダリング間のリソースバリアを適切に管理する。
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
#include <mitiru/render/Camera3D.hpp>

#include <mitiru/debug/TracyZones.hpp>

namespace mitiru::effects
{

// ── HLSL シェーダーソース（DX12版） ────────────────────────

/// @brief パーティクルシミュレーション用コンピュートシェーダー (CS 5.0)
static constexpr std::string_view DX12_PARTICLE_COMPUTE_HLSL = R"(
struct Particle
{
    float3 position;
    float3 velocity;
    float3 acceleration;
    float4 color;
    float size;
    float lifetime;
    float age;
};

struct DrawArgs
{
    uint vertexCountPerInstance;
    uint instanceCount;
    uint startVertexLocation;
    uint startInstanceLocation;
};

cbuffer SimConstants : register(b0)
{
    float deltaTime;
    float3 gravity;
    float drag;
    uint maxParticles;
    uint activeParticles;
    float padding;
};

StructuredBuffer<Particle> particlesIn : register(t0);
RWStructuredBuffer<Particle> particlesOut : register(u0);
RWStructuredBuffer<DrawArgs> drawArgs : register(u1);

[numthreads(256, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    // 最初のスレッドがドローアーギュメントを初期化する
    if (dtid.x == 0)
    {
        DrawArgs args;
        args.vertexCountPerInstance = 6;
        args.instanceCount = 0;
        args.startVertexLocation = 0;
        args.startInstanceLocation = 0;
        drawArgs[0] = args;
    }

    GroupMemoryBarrierWithGroupSync();

    uint idx = dtid.x;
    if (idx >= activeParticles)
    {
        return;
    }

    Particle p = particlesIn[idx];

    // 寿命チェック
    p.age += deltaTime;
    if (p.age >= p.lifetime)
    {
        p.size = 0;
        p.color.a = 0;
        particlesOut[idx] = p;
        return;
    }

    // 物理シミュレーション
    float3 accel = gravity + p.acceleration;
    p.velocity += accel * deltaTime;
    p.velocity *= (1.0 - drag * deltaTime);
    p.position += p.velocity * deltaTime;

    // ライフタイム比率に基づくフェード
    float normalizedAge = p.age / p.lifetime;
    p.color.a *= (1.0 - normalizedAge * normalizedAge);

    particlesOut[idx] = p;

    // 生存パーティクルのインスタンスカウントをインクリメントする
    if (p.size > 0 && p.color.a > 0.001)
    {
        uint dummy;
        InterlockedAdd(drawArgs[0].instanceCount, 1, dummy);
    }
}
)";

/// @brief パーティクル描画用頂点シェーダー（DX12版）
static constexpr std::string_view DX12_PARTICLE_VS_HLSL = R"(
struct Particle
{
    float3 position;
    float3 velocity;
    float3 acceleration;
    float4 color;
    float size;
    float lifetime;
    float age;
};

cbuffer RenderConstants : register(b0)
{
    float4x4 viewProjection;
    float3 cameraRight;
    float pad0;
    float3 cameraUp;
    float pad1;
};

StructuredBuffer<Particle> particles : register(t0);

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR;
    float2 texCoord : TEXCOORD;
};

VSOutput VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    VSOutput output;

    Particle p = particles[instanceId];

    static const float2 quadVerts[6] = {
        float2(-1, -1), float2(-1, 1), float2(1, -1),
        float2(1, -1), float2(-1, 1), float2(1, 1)
    };

    float2 corner = quadVerts[vertexId];
    float halfSize = p.size * 0.5;

    float3 worldPos = p.position
        + cameraRight * corner.x * halfSize
        + cameraUp * corner.y * halfSize;

    output.position = mul(viewProjection, float4(worldPos, 1.0));
    output.color = p.color;
    output.texCoord = corner * 0.5 + 0.5;

    return output;
}
)";

/// @brief パーティクル描画用ピクセルシェーダー（DX12版）
static constexpr std::string_view DX12_PARTICLE_PS_HLSL = R"(
struct PSInput
{
    float4 position : SV_Position;
    float4 color : COLOR;
    float2 texCoord : TEXCOORD;
};

float4 PSMain(PSInput input) : SV_Target
{
    float2 center = input.texCoord - 0.5;
    float dist = length(center) * 2.0;
    float alpha = saturate(1.0 - dist * dist);

    float4 color = input.color;
    color.a *= alpha;

    if (color.a < 0.001)
    {
        discard;
    }

    return color;
}
)";

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
		renderCB.viewProjection = camera.viewProjectionMatrix();
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
	/// @brief コマンドアロケータとコマンドリストを生成する
	void createCommandResources()
	{
		HRESULT hr = m_device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(m_commandAllocator.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuParticleDx12: CreateCommandAllocator failed");
		}

		hr = m_device->CreateCommandList(
			0, D3D12_COMMAND_LIST_TYPE_DIRECT,
			m_commandAllocator.Get(), nullptr,
			IID_PPV_ARGS(m_commandList.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuParticleDx12: CreateCommandList failed");
		}

		m_commandList->Close();
	}

	/// @brief 構造化バッファとアップロードバッファを生成する
	void createBuffers()
	{
		const auto particleBufferSize = static_cast<UINT64>(
			m_maxParticles * sizeof(GpuParticle));

		/// パーティクル構造化バッファ（ピンポン、UAV対応）
		for (int i = 0; i < 2; ++i)
		{
			D3D12_HEAP_PROPERTIES heapProps = {};
			heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

			D3D12_RESOURCE_DESC resDesc = {};
			resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			resDesc.Width = particleBufferSize;
			resDesc.Height = 1;
			resDesc.DepthOrArraySize = 1;
			resDesc.MipLevels = 1;
			resDesc.Format = DXGI_FORMAT_UNKNOWN;
			resDesc.SampleDesc.Count = 1;
			resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

			HRESULT hr = m_device->CreateCommittedResource(
				&heapProps,
				D3D12_HEAP_FLAG_NONE,
				&resDesc,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				nullptr,
				IID_PPV_ARGS(m_particleBuffer[i].GetAddressOf()));
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"GpuParticleDx12: CreateCommittedResource (particle) failed");
			}
		}

		/// アップロードバッファ（CPU→GPU転送用）
		{
			D3D12_HEAP_PROPERTIES heapProps = {};
			heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

			D3D12_RESOURCE_DESC resDesc = {};
			resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			resDesc.Width = particleBufferSize;
			resDesc.Height = 1;
			resDesc.DepthOrArraySize = 1;
			resDesc.MipLevels = 1;
			resDesc.Format = DXGI_FORMAT_UNKNOWN;
			resDesc.SampleDesc.Count = 1;
			resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

			HRESULT hr = m_device->CreateCommittedResource(
				&heapProps,
				D3D12_HEAP_FLAG_NONE,
				&resDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(m_uploadBuffer.GetAddressOf()));
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"GpuParticleDx12: CreateCommittedResource (upload) failed");
			}
		}

		/// Indirect Argsバッファ（DrawInstanced引数）
		{
			D3D12_HEAP_PROPERTIES heapProps = {};
			heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

			D3D12_RESOURCE_DESC resDesc = {};
			resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			resDesc.Width = sizeof(D3D12_DRAW_ARGUMENTS);
			resDesc.Height = 1;
			resDesc.DepthOrArraySize = 1;
			resDesc.MipLevels = 1;
			resDesc.Format = DXGI_FORMAT_UNKNOWN;
			resDesc.SampleDesc.Count = 1;
			resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

			HRESULT hr = m_device->CreateCommittedResource(
				&heapProps,
				D3D12_HEAP_FLAG_NONE,
				&resDesc,
				D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
				nullptr,
				IID_PPV_ARGS(m_indirectArgsBuffer.GetAddressOf()));
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"GpuParticleDx12: CreateCommittedResource (indirect) failed");
			}
		}

		/// 定数バッファ（アップロードヒープ）
		{
			const UINT64 cbSize = 256; ///< 256バイトアライメント

			D3D12_HEAP_PROPERTIES heapProps = {};
			heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

			D3D12_RESOURCE_DESC resDesc = {};
			resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			resDesc.Width = cbSize;
			resDesc.Height = 1;
			resDesc.DepthOrArraySize = 1;
			resDesc.MipLevels = 1;
			resDesc.Format = DXGI_FORMAT_UNKNOWN;
			resDesc.SampleDesc.Count = 1;
			resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

			HRESULT hr = m_device->CreateCommittedResource(
				&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
				IID_PPV_ARGS(m_simConstantUpload.GetAddressOf()));
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"GpuParticleDx12: CreateCommittedResource (sim CB) failed");
			}

			hr = m_device->CreateCommittedResource(
				&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
				IID_PPV_ARGS(m_renderConstantUpload.GetAddressOf()));
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"GpuParticleDx12: CreateCommittedResource (render CB) failed");
			}
		}

		/// リードバックバッファ（コンパクション用）
		{
			D3D12_HEAP_PROPERTIES heapProps = {};
			heapProps.Type = D3D12_HEAP_TYPE_READBACK;

			D3D12_RESOURCE_DESC resDesc = {};
			resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			resDesc.Width = particleBufferSize;
			resDesc.Height = 1;
			resDesc.DepthOrArraySize = 1;
			resDesc.MipLevels = 1;
			resDesc.Format = DXGI_FORMAT_UNKNOWN;
			resDesc.SampleDesc.Count = 1;
			resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

			HRESULT hr = m_device->CreateCommittedResource(
				&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
				D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
				IID_PPV_ARGS(m_readbackBuffer.GetAddressOf()));
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"GpuParticleDx12: CreateCommittedResource (readback) failed");
			}
		}

		/// SRV/UAVデスクリプタヒープを生成する
		createDescriptorHeap();
	}

	/// @brief SRV/UAVデスクリプタヒープを生成する
	/// @details レイアウト:
	///   0: SRV パーティクルバッファ[0]
	///   1: SRV パーティクルバッファ[1]
	///   2: UAV パーティクルバッファ[0]
	///   3: UAV パーティクルバッファ[1]
	///   4: UAV IndirectArgsバッファ
	void createDescriptorHeap()
	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.NumDescriptors = 5;
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		HRESULT hr = m_device->CreateDescriptorHeap(
			&heapDesc,
			IID_PPV_ARGS(m_srvUavHeap.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuParticleDx12: CreateDescriptorHeap failed");
		}

		m_descriptorSize = m_device->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		auto cpuHandle = m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();

		/// SRV[0], SRV[1]
		for (int i = 0; i < 2; ++i)
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = DXGI_FORMAT_UNKNOWN;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			srvDesc.Shader4ComponentMapping =
				D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Buffer.FirstElement = 0;
			srvDesc.Buffer.NumElements = m_maxParticles;
			srvDesc.Buffer.StructureByteStride = sizeof(GpuParticle);

			D3D12_CPU_DESCRIPTOR_HANDLE handle = cpuHandle;
			handle.ptr += static_cast<SIZE_T>(i) * m_descriptorSize;
			m_device->CreateShaderResourceView(
				m_particleBuffer[i].Get(), &srvDesc, handle);
		}

		/// UAV[0], UAV[1]
		for (int i = 0; i < 2; ++i)
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = DXGI_FORMAT_UNKNOWN;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
			uavDesc.Buffer.FirstElement = 0;
			uavDesc.Buffer.NumElements = m_maxParticles;
			uavDesc.Buffer.StructureByteStride = sizeof(GpuParticle);

			D3D12_CPU_DESCRIPTOR_HANDLE handle = cpuHandle;
			handle.ptr += static_cast<SIZE_T>(2 + i) * m_descriptorSize;
			m_device->CreateUnorderedAccessView(
				m_particleBuffer[i].Get(), nullptr, &uavDesc, handle);
		}

		/// UAV IndirectArgs
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = DXGI_FORMAT_UNKNOWN;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
			uavDesc.Buffer.FirstElement = 0;
			uavDesc.Buffer.NumElements = 1;
			uavDesc.Buffer.StructureByteStride = sizeof(D3D12_DRAW_ARGUMENTS);

			D3D12_CPU_DESCRIPTOR_HANDLE handle = cpuHandle;
			handle.ptr += 4 * m_descriptorSize;
			m_device->CreateUnorderedAccessView(
				m_indirectArgsBuffer.Get(), nullptr, &uavDesc, handle);
		}
	}

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
	void createPipelines()
	{
		createComputePipeline();
		createRenderPipeline();
		createCommandSignature();
	}

	/// @brief コンピュートパイプラインを生成する
	void createComputePipeline()
	{
		/// ルートシグネチャ: b0(CBV) + t0(SRV table) + u0(UAV table) + u1(UAV table)
		D3D12_ROOT_PARAMETER rootParams[4] = {};

		/// パラメータ0: CBV (b0)
		rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParams[0].Descriptor.ShaderRegister = 0;
		rootParams[0].Descriptor.RegisterSpace = 0;
		rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		/// パラメータ1: SRV テーブル (t0)
		D3D12_DESCRIPTOR_RANGE srvRange = {};
		srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		srvRange.NumDescriptors = 1;
		srvRange.BaseShaderRegister = 0;
		rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
		rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;
		rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		/// パラメータ2: UAV テーブル (u0)
		D3D12_DESCRIPTOR_RANGE uavRange0 = {};
		uavRange0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		uavRange0.NumDescriptors = 1;
		uavRange0.BaseShaderRegister = 0;
		rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
		rootParams[2].DescriptorTable.pDescriptorRanges = &uavRange0;
		rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		/// パラメータ3: UAV テーブル (u1)
		D3D12_DESCRIPTOR_RANGE uavRange1 = {};
		uavRange1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		uavRange1.NumDescriptors = 1;
		uavRange1.BaseShaderRegister = 1;
		rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
		rootParams[3].DescriptorTable.pDescriptorRanges = &uavRange1;
		rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
		rsDesc.NumParameters = 4;
		rsDesc.pParameters = rootParams;
		rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		HRESULT hr = D3D12SerializeRootSignature(
			&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
			signature.GetAddressOf(), error.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuParticleDx12: SerializeRootSignature (compute) failed");
		}

		hr = m_device->CreateRootSignature(
			0, signature->GetBufferPointer(), signature->GetBufferSize(),
			IID_PPV_ARGS(m_computeRootSignature.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuParticleDx12: CreateRootSignature (compute) failed");
		}

		/// コンピュートシェーダーをコンパイルする
		auto csBlob = compileHLSL(
			DX12_PARTICLE_COMPUTE_HLSL, "CSMain", "cs_5_0");

		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = m_computeRootSignature.Get();
		psoDesc.CS = {csBlob->GetBufferPointer(), csBlob->GetBufferSize()};

		hr = m_device->CreateComputePipelineState(
			&psoDesc,
			IID_PPV_ARGS(m_computePSO.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuParticleDx12: CreateComputePipelineState failed");
		}
	}

	/// @brief レンダリングパイプラインを生成する
	void createRenderPipeline()
	{
		/// ルートシグネチャ: b0(CBV) + t0(SRV table)
		D3D12_ROOT_PARAMETER rootParams[2] = {};

		rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParams[0].Descriptor.ShaderRegister = 0;
		rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

		D3D12_DESCRIPTOR_RANGE srvRange = {};
		srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		srvRange.NumDescriptors = 1;
		srvRange.BaseShaderRegister = 0;
		rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
		rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;
		rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

		D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
		rsDesc.NumParameters = 2;
		rsDesc.pParameters = rootParams;
		rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		HRESULT hr = D3D12SerializeRootSignature(
			&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
			signature.GetAddressOf(), error.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuParticleDx12: SerializeRootSignature (render) failed");
		}

		hr = m_device->CreateRootSignature(
			0, signature->GetBufferPointer(), signature->GetBufferSize(),
			IID_PPV_ARGS(m_renderRootSignature.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuParticleDx12: CreateRootSignature (render) failed");
		}

		/// シェーダーをコンパイルする
		auto vsBlob = compileHLSL(
			DX12_PARTICLE_VS_HLSL, "VSMain", "vs_5_0");
		auto psBlob = compileHLSL(
			DX12_PARTICLE_PS_HLSL, "PSMain", "ps_5_0");

		/// グラフィックスPSOを生成する
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = {nullptr, 0};
		psoDesc.pRootSignature = m_renderRootSignature.Get();
		psoDesc.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
		psoDesc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};

		/// ラスタライザステート（カリングなし）
		psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
		psoDesc.RasterizerState.DepthClipEnable = TRUE;

		/// 加算ブレンドステート
		psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
		psoDesc.BlendState.IndependentBlendEnable = FALSE;
		auto& rt = psoDesc.BlendState.RenderTarget[0];
		rt.BlendEnable = TRUE;
		rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		rt.DestBlend = D3D12_BLEND_ONE;
		rt.BlendOp = D3D12_BLEND_OP_ADD;
		rt.SrcBlendAlpha = D3D12_BLEND_ONE;
		rt.DestBlendAlpha = D3D12_BLEND_ONE;
		rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

		/// 深度ステンシル（深度読み取りのみ、書き込みなし）
		psoDesc.DepthStencilState.DepthEnable = TRUE;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		psoDesc.DepthStencilState.StencilEnable = FALSE;

		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;

		hr = m_device->CreateGraphicsPipelineState(
			&psoDesc,
			IID_PPV_ARGS(m_renderPSO.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuParticleDx12: CreateGraphicsPipelineState failed");
		}
	}

	/// @brief ExecuteIndirect用のコマンドシグネチャを生成する
	void createCommandSignature()
	{
		D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
		argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

		D3D12_COMMAND_SIGNATURE_DESC sigDesc = {};
		sigDesc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
		sigDesc.NumArgumentDescs = 1;
		sigDesc.pArgumentDescs = &argDesc;

		HRESULT hr = m_device->CreateCommandSignature(
			&sigDesc, nullptr,
			IID_PPV_ARGS(m_commandSignature.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuParticleDx12: CreateCommandSignature failed");
		}
	}

	/// @brief フェンスを生成する
	void createFence()
	{
		HRESULT hr = m_device->CreateFence(
			0, D3D12_FENCE_FLAG_NONE,
			IID_PPV_ARGS(m_fence.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuParticleDx12: CreateFence failed");
		}

		m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (!m_fenceEvent)
		{
			throw std::runtime_error(
				"GpuParticleDx12: CreateEvent failed");
		}
	}

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

#endif // _WIN32
