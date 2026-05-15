#pragma once

/// @file GpuParticleDx11.hpp
/// @brief DirectX 11 GPUパーティクルシステム実装
/// @details 構造化バッファ + コンピュートシェーダー(CS 5.0)によるGPUシミュレーションと、
///          インスタンス描画によるビルボードレンダリングを行う。
///          ピンポン方式のダブルバッファリングでパーティクルデータを管理する。
///
/// @code
/// auto device = std::make_unique<Dx11Device>(window);
/// auto particles = std::make_unique<GpuParticleDx11>(
///     device->getD3DDevice(), device->getD3DContext());
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

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

#include <mitiru/effects/GpuParticleBase.hpp>
#include <mitiru/render/Camera3D.hpp>

namespace mitiru::effects
{

// ── HLSL シェーダーソース ─────────────────────────────────

/// @brief パーティクルシミュレーション用コンピュートシェーダー (CS 5.0)
static constexpr std::string_view PARTICLE_COMPUTE_HLSL = R"(
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

[numthreads(256, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
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
        // 死亡パーティクルはサイズ0にして無効化
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
}
)";

/// @brief パーティクル描画用頂点シェーダー
/// @details インスタンス描画でビルボードクアッドを展開する。
static constexpr std::string_view PARTICLE_VS_HLSL = R"(
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

    // クアッド頂点（三角形ストリップ順）
    // 0: (-1,-1), 1: (1,-1), 2: (-1,1), 3: (1,1)
    // 三角形リスト: 0,2,1, 1,2,3
    static const float2 quadVerts[6] = {
        float2(-1, -1), float2(-1, 1), float2(1, -1),
        float2(1, -1), float2(-1, 1), float2(1, 1)
    };

    float2 corner = quadVerts[vertexId];
    float halfSize = p.size * 0.5;

    // ビルボード展開
    float3 worldPos = p.position
        + cameraRight * corner.x * halfSize
        + cameraUp * corner.y * halfSize;

    output.position = mul(viewProjection, float4(worldPos, 1.0));
    output.color = p.color;
    output.texCoord = corner * 0.5 + 0.5;

    return output;
}
)";

/// @brief パーティクル描画用ピクセルシェーダー
/// @details 円形のソフトパーティクルを描画する。
static constexpr std::string_view PARTICLE_PS_HLSL = R"(
struct PSInput
{
    float4 position : SV_Position;
    float4 color : COLOR;
    float2 texCoord : TEXCOORD;
};

float4 PSMain(PSInput input) : SV_Target
{
    // 円形マスク（ソフトエッジ）
    float2 center = input.texCoord - 0.5;
    float dist = length(center) * 2.0;
    float alpha = saturate(1.0 - dist * dist);

    float4 color = input.color;
    color.a *= alpha;

    // アルファが極小のフラグメントを破棄する
    if (color.a < 0.001)
    {
        discard;
    }

    return color;
}
)";

/// @brief DirectX 11 GPUパーティクルシステム実装
/// @details 構造化バッファのピンポンとCS 5.0コンピュートシェーダーによるシミュレーション、
///          SV_InstanceIDベースのインスタンス描画を行う。
class GpuParticleDx11 final : public GpuParticleBase
{
public:
	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief コンストラクタ
	/// @param device D3D11デバイス
	/// @param context D3D11デバイスコンテキスト
	/// @param maxParticles 最大パーティクル数
	explicit GpuParticleDx11(ID3D11Device* device,
	                         ID3D11DeviceContext* context,
	                         std::uint32_t maxParticles = MAX_GPU_PARTICLES)
		: GpuParticleBase(maxParticles)
		, m_device(device)
		, m_context(context)
	{
		if (!device || !context)
		{
			throw std::runtime_error(
				"GpuParticleDx11: device or context is null");
		}

		createBuffers();
		createShaders();
		createConstantBuffers();
		createStates();
		m_valid = true;
	}

	/// @brief GPUシミュレーションを実行する
	void update(float dt) override
	{
		if (!m_valid)
		{
			return;
		}

		/// ステージングパーティクルをGPUバッファにアップロードする
		uploadStagingParticles();

		if (m_activeCount == 0)
		{
			return;
		}

		/// シミュレーション定数を更新する
		ParticleSimConstants simCB;
		simCB.deltaTime = dt;
		simCB.gravityX = m_emitter.gravity.x;
		simCB.gravityY = m_emitter.gravity.y;
		simCB.gravityZ = m_emitter.gravity.z;
		simCB.drag = m_emitter.drag;
		simCB.maxParticles = m_maxParticles;
		simCB.activeParticles = m_activeCount;

		updateConstantBuffer(m_simConstantBuffer.Get(), &simCB, sizeof(simCB));

		/// コンピュートシェーダーをディスパッチする
		m_context->CSSetShader(m_computeShader.Get(), nullptr, 0);

		ID3D11Buffer* cbs[] = {m_simConstantBuffer.Get()};
		m_context->CSSetConstantBuffers(0, 1, cbs);

		/// 入力: 現在のバッファ (SRV)
		ID3D11ShaderResourceView* srvs[] = {m_particleSRV[m_currentBuffer].Get()};
		m_context->CSSetShaderResources(0, 1, srvs);

		/// 出力: 反対側のバッファ (UAV)
		const std::uint32_t otherBuffer = 1 - m_currentBuffer;
		ID3D11UnorderedAccessView* uavs[] = {m_particleUAV[otherBuffer].Get()};
		m_context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

		/// ディスパッチ
		const std::uint32_t groupCount =
			computeDispatchGroupCount(m_activeCount);
		m_context->Dispatch(groupCount, 1, 1);

		/// リソースバインドを解除する
		ID3D11ShaderResourceView* nullSRV[] = {nullptr};
		ID3D11UnorderedAccessView* nullUAV[] = {nullptr};
		m_context->CSSetShaderResources(0, 1, nullSRV);
		m_context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
		m_context->CSSetShader(nullptr, nullptr, 0);

		/// バッファをスワップする（ピンポン）
		m_currentBuffer = otherBuffer;

		/// 定期的なコンパクション
		if (shouldCompact())
		{
			compactDeadParticles();
		}
	}

	/// @brief パーティクルをインスタンス描画する
	void render(const mitiru::render::Camera3D& camera) override
	{
		if (!m_valid || m_activeCount == 0)
		{
			return;
		}

		/// 描画定数を更新する
		ParticleRenderConstants renderCB;
		renderCB.viewProjection = camera.viewProjectionMatrix();
		renderCB.cameraRight = camera.rightDirection();
		renderCB.cameraUp = camera.upDirection();

		updateConstantBuffer(m_renderConstantBuffer.Get(),
			&renderCB, sizeof(renderCB));

		/// シェーダーを設定する
		m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
		m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);

		/// 定数バッファを設定する
		ID3D11Buffer* vsCBs[] = {m_renderConstantBuffer.Get()};
		m_context->VSSetConstantBuffers(0, 1, vsCBs);

		/// パーティクルバッファをSRVとして頂点シェーダーにバインドする
		ID3D11ShaderResourceView* vsSRVs[] = {
			m_particleSRV[m_currentBuffer].Get()
		};
		m_context->VSSetShaderResources(0, 1, vsSRVs);

		/// 入力アセンブラ（頂点バッファなし、SV_VertexIDで駆動）
		m_context->IASetInputLayout(nullptr);
		m_context->IASetPrimitiveTopology(
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		/// ブレンドステートを設定する
		const float blendFactor[4] = {0, 0, 0, 0};
		m_context->OMSetBlendState(
			m_blendState.Get(), blendFactor, 0xFFFFFFFF);

		/// 深度ステンシルステートを設定する
		m_context->OMSetDepthStencilState(
			m_depthStencilState.Get(), 0);

		/// ラスタライザステートを設定する
		m_context->RSSetState(m_rasterizerState.Get());

		/// インスタンス描画（6頂点/クアッド × m_activeCountインスタンス）
		m_context->DrawInstanced(6, m_activeCount, 0, 0);

		/// バインドを解除する
		ID3D11ShaderResourceView* nullSRV[] = {nullptr};
		m_context->VSSetShaderResources(0, 1, nullSRV);
	}

private:
	/// @brief 構造化バッファ（ピンポン）を生成する
	void createBuffers()
	{
		const std::uint32_t bufferSize =
			m_maxParticles * static_cast<std::uint32_t>(sizeof(GpuParticle));

		for (int i = 0; i < 2; ++i)
		{
			/// 構造化バッファ（SRV + UAV）
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth = bufferSize;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE |
			                 D3D11_BIND_UNORDERED_ACCESS;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.StructureByteStride = sizeof(GpuParticle);

			HRESULT hr = m_device->CreateBuffer(
				&desc, nullptr,
				m_particleBuffer[i].GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"GpuParticleDx11: CreateBuffer (structured) failed");
			}

			/// SRVの生成
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = DXGI_FORMAT_UNKNOWN;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			srvDesc.Buffer.FirstElement = 0;
			srvDesc.Buffer.NumElements = m_maxParticles;

			hr = m_device->CreateShaderResourceView(
				m_particleBuffer[i].Get(), &srvDesc,
				m_particleSRV[i].GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"GpuParticleDx11: CreateShaderResourceView failed");
			}

			/// UAVの生成
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = DXGI_FORMAT_UNKNOWN;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			uavDesc.Buffer.FirstElement = 0;
			uavDesc.Buffer.NumElements = m_maxParticles;

			hr = m_device->CreateUnorderedAccessView(
				m_particleBuffer[i].Get(), &uavDesc,
				m_particleUAV[i].GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"GpuParticleDx11: CreateUnorderedAccessView failed");
			}
		}

		/// ステージングバッファ（CPU→GPU転送用）
		D3D11_BUFFER_DESC stagingDesc = {};
		stagingDesc.ByteWidth = bufferSize;
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

		HRESULT hr = m_device->CreateBuffer(
			&stagingDesc, nullptr,
			m_stagingBuffer.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"GpuParticleDx11: CreateBuffer (staging) failed");
		}
	}

	/// @brief シェーダーをコンパイル・生成する
	void createShaders()
	{
		/// コンピュートシェーダー
		{
			auto blob = compileHLSL(
				PARTICLE_COMPUTE_HLSL, "CSMain", "cs_5_0");
			HRESULT hr = m_device->CreateComputeShader(
				blob->GetBufferPointer(),
				blob->GetBufferSize(),
				nullptr,
				m_computeShader.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"GpuParticleDx11: CreateComputeShader failed");
			}
		}

		/// 頂点シェーダー
		{
			auto blob = compileHLSL(
				PARTICLE_VS_HLSL, "VSMain", "vs_5_0");
			HRESULT hr = m_device->CreateVertexShader(
				blob->GetBufferPointer(),
				blob->GetBufferSize(),
				nullptr,
				m_vertexShader.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"GpuParticleDx11: CreateVertexShader failed");
			}
		}

		/// ピクセルシェーダー
		{
			auto blob = compileHLSL(
				PARTICLE_PS_HLSL, "PSMain", "ps_5_0");
			HRESULT hr = m_device->CreatePixelShader(
				blob->GetBufferPointer(),
				blob->GetBufferSize(),
				nullptr,
				m_pixelShader.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"GpuParticleDx11: CreatePixelShader failed");
			}
		}
	}

	/// @brief 定数バッファを生成する
	void createConstantBuffers()
	{
		/// シミュレーション定数
		{
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth = (sizeof(ParticleSimConstants) + 15) & ~15u;
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

			HRESULT hr = m_device->CreateBuffer(
				&desc, nullptr,
				m_simConstantBuffer.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"GpuParticleDx11: CreateBuffer (sim CB) failed");
			}
		}

		/// 描画定数
		{
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth = (sizeof(ParticleRenderConstants) + 15) & ~15u;
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

			HRESULT hr = m_device->CreateBuffer(
				&desc, nullptr,
				m_renderConstantBuffer.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"GpuParticleDx11: CreateBuffer (render CB) failed");
			}
		}
	}

	/// @brief レンダリングステートを生成する
	void createStates()
	{
		/// 加算ブレンドステート
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

			HRESULT hr = m_device->CreateBlendState(
				&desc, m_blendState.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"GpuParticleDx11: CreateBlendState failed");
			}
		}

		/// 深度書き込み無効ステート
		{
			D3D11_DEPTH_STENCIL_DESC desc = {};
			desc.DepthEnable = TRUE;
			desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
			desc.StencilEnable = FALSE;

			HRESULT hr = m_device->CreateDepthStencilState(
				&desc, m_depthStencilState.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"GpuParticleDx11: CreateDepthStencilState failed");
			}
		}

		/// カリングなしラスタライザステート
		{
			D3D11_RASTERIZER_DESC desc = {};
			desc.FillMode = D3D11_FILL_SOLID;
			desc.CullMode = D3D11_CULL_NONE;
			desc.FrontCounterClockwise = FALSE;
			desc.DepthClipEnable = TRUE;

			HRESULT hr = m_device->CreateRasterizerState(
				&desc, m_rasterizerState.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"GpuParticleDx11: CreateRasterizerState failed");
			}
		}
	}

	/// @brief ステージングパーティクルをGPUバッファにアップロードする
	void uploadStagingParticles()
	{
		const std::uint32_t uploadCount = prepareStagingUpload();
		if (uploadCount == 0)
		{
			return;
		}

		/// 現在のバッファの末尾にコピーする
		D3D11_BOX destBox = {};
		destBox.left = m_activeCount * sizeof(GpuParticle);
		destBox.right = (m_activeCount + uploadCount) * sizeof(GpuParticle);
		destBox.top = 0;
		destBox.bottom = 1;
		destBox.front = 0;
		destBox.back = 1;

		m_context->UpdateSubresource(
			m_particleBuffer[m_currentBuffer].Get(),
			0,
			&destBox,
			m_stagingParticles.data(),
			0, 0);

		finalizeStagingUpload(uploadCount);
	}

	/// @brief 死亡パーティクルを除去するコンパクション処理
	/// @details GPUバッファをステージングにコピーし、生存パーティクルのみを
	///          再パックしてアップロードする。
	void compactDeadParticles()
	{
		if (m_activeCount == 0)
		{
			return;
		}

		/// GPUバッファをステージングバッファにコピーする
		m_context->CopyResource(
			m_stagingBuffer.Get(),
			m_particleBuffer[m_currentBuffer].Get());

		/// ステージングバッファをマップして読み取る
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		HRESULT hr = m_context->Map(
			m_stagingBuffer.Get(), 0,
			D3D11_MAP_READ, 0, &mapped);
		if (FAILED(hr))
		{
			return;
		}

		const auto* src = static_cast<const GpuParticle*>(mapped.pData);
		auto alive = filterAliveParticles(src, m_activeCount);

		m_context->Unmap(m_stagingBuffer.Get(), 0);

		/// 生存パーティクルを再アップロードする
		m_activeCount = static_cast<std::uint32_t>(alive.size());
		if (m_activeCount > 0)
		{
			D3D11_BOX destBox = {};
			destBox.left = 0;
			destBox.right = m_activeCount * sizeof(GpuParticle);
			destBox.top = 0;
			destBox.bottom = 1;
			destBox.front = 0;
			destBox.back = 1;

			m_context->UpdateSubresource(
				m_particleBuffer[m_currentBuffer].Get(),
				0,
				&destBox,
				alive.data(),
				0, 0);
		}
	}

	/// @brief 定数バッファを更新する
	/// @param buffer 更新対象のバッファ
	/// @param data 書き込むデータ
	/// @param dataSize データサイズ（バイト）
	void updateConstantBuffer(ID3D11Buffer* buffer,
	                          const void* data,
	                          std::uint32_t dataSize)
	{
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		HRESULT hr = m_context->Map(
			buffer, 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (SUCCEEDED(hr))
		{
			std::memcpy(mapped.pData, data, dataSize);
			m_context->Unmap(buffer, 0);
		}
	}

	/// @brief HLSL文字列をコンパイルする
	/// @param source HLSL文字列
	/// @param entryPoint エントリーポイント名
	/// @param target コンパイルターゲット
	/// @return コンパイル済みBlob
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
			source.data(),
			source.size(),
			nullptr,
			nullptr,
			nullptr,
			entryPoint,
			target,
			compileFlags,
			0,
			shaderBlob.GetAddressOf(),
			errorBlob.GetAddressOf());

		if (FAILED(hr))
		{
			std::string errorMsg = "GpuParticleDx11: D3DCompile failed";
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

	// ── DX11固有メンバ変数 ──────────────────────────────
	ID3D11Device* m_device = nullptr;                             ///< D3D11デバイス（非所有）
	ID3D11DeviceContext* m_context = nullptr;                     ///< D3D11コンテキスト（非所有）

	/// 構造化バッファ（ピンポン）
	ComPtr<ID3D11Buffer> m_particleBuffer[2];
	ComPtr<ID3D11ShaderResourceView> m_particleSRV[2];
	ComPtr<ID3D11UnorderedAccessView> m_particleUAV[2];

	ComPtr<ID3D11Buffer> m_stagingBuffer;                         ///< ステージングバッファ

	/// シェーダー
	ComPtr<ID3D11ComputeShader> m_computeShader;                  ///< コンピュートシェーダー
	ComPtr<ID3D11VertexShader> m_vertexShader;                    ///< 頂点シェーダー
	ComPtr<ID3D11PixelShader> m_pixelShader;                      ///< ピクセルシェーダー

	/// 定数バッファ
	ComPtr<ID3D11Buffer> m_simConstantBuffer;                     ///< シミュレーション定数
	ComPtr<ID3D11Buffer> m_renderConstantBuffer;                  ///< 描画定数

	/// レンダリングステート
	ComPtr<ID3D11BlendState> m_blendState;                        ///< ブレンドステート
	ComPtr<ID3D11DepthStencilState> m_depthStencilState;          ///< 深度ステンシルステート
	ComPtr<ID3D11RasterizerState> m_rasterizerState;              ///< ラスタライザステート
};

} // namespace mitiru::effects

#endif // _WIN32
