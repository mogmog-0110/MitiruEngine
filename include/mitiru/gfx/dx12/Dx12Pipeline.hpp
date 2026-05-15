#pragma once

/// @file Dx12Pipeline.hpp
/// @brief DirectX 12パイプラインステート実装
/// @details ID3D12PipelineStateとID3D12RootSignatureを管理するIPipeline実装。
///          ルートシグネチャ・PSO・入力レイアウトを統合的に管理する。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <stdexcept>

#include <d3d12.h>
#include <wrl/client.h>

#include <mitiru/gfx/GfxTypes.hpp>
#include <mitiru/gfx/IPipeline.hpp>
#include <mitiru/gfx/dx12/Dx12Shader.hpp>

namespace mitiru::gfx
{

/// @brief D3D12パイプライン記述子
/// @details パイプライン生成に必要なパラメータを集約する。
struct Dx12PipelineDesc
{
	Dx12Shader* vertexShader = nullptr;             ///< 頂点シェーダー
	Dx12Shader* pixelShader = nullptr;              ///< ピクセルシェーダー
	BlendMode blendMode = BlendMode::Alpha;         ///< ブレンドモード
	bool wireframe = false;                         ///< ワイヤーフレーム表示
	DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;  ///< レンダーターゲットフォーマット
	uint32_t numRenderTargets = 1;                  ///< レンダーターゲット数
};

/// @brief DirectX 12パイプラインステート実装
/// @details ルートシグネチャ + PSO を束ねて管理する。
///
/// @code
/// Dx12PipelineDesc desc;
/// desc.vertexShader = &vs;
/// desc.pixelShader = &ps;
/// auto pipeline = std::make_unique<Dx12Pipeline>(device, desc);
/// @endcode
class Dx12Pipeline final : public IPipeline
{
public:
	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief コンストラクタ
	/// @param device D3D12デバイス
	/// @param desc パイプライン記述子
	explicit Dx12Pipeline(ID3D12Device* device, const Dx12PipelineDesc& desc)
	{
		if (!device || !desc.vertexShader || !desc.pixelShader)
		{
			throw std::runtime_error(
				"Dx12Pipeline: null device or shaders");
		}

		createRootSignature(device);
		createPipelineState(device, desc);
		m_valid = true;
	}

	/// @brief パイプラインが有効かどうかを判定する
	[[nodiscard]] bool isValid() const noexcept override
	{
		return m_valid;
	}

	/// @brief ルートシグネチャのネイティブポインタを取得する
	/// @return ID3D12RootSignatureへのvoidポインタ
	[[nodiscard]] void* rootSignature() const override
	{
		return m_rootSignature.Get();
	}

	/// @brief ID3D12PipelineStateを取得する
	/// @return PSOへのポインタ
	[[nodiscard]] ID3D12PipelineState* nativePSO() const noexcept
	{
		return m_pso.Get();
	}

	/// @brief ID3D12RootSignatureを取得する
	/// @return ルートシグネチャへのポインタ
	[[nodiscard]] ID3D12RootSignature* nativeRootSignature() const noexcept
	{
		return m_rootSignature.Get();
	}

private:
	/// @brief デフォルトのルートシグネチャを生成する
	/// @param device D3D12デバイス
	/// @details b0: CBV（定数バッファ）、t0: SRV（テクスチャ）のルートパラメータを定義する。
	void createRootSignature(ID3D12Device* device)
	{
		/// ルートパラメータ: CBV(b0) + DescriptorTable(t0)
		D3D12_ROOT_PARAMETER rootParams[2] = {};

		/// パラメータ0: ルートCBV（b0）
		rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParams[0].Descriptor.ShaderRegister = 0;
		rootParams[0].Descriptor.RegisterSpace = 0;
		rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

		/// パラメータ1: SRVデスクリプタテーブル（t0）
		D3D12_DESCRIPTOR_RANGE srvRange = {};
		srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		srvRange.NumDescriptors = 1;
		srvRange.BaseShaderRegister = 0;
		srvRange.RegisterSpace = 0;
		srvRange.OffsetInDescriptorsFromTableStart =
			D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
		rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;
		rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		/// スタティックサンプラー
		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.ShaderRegister = 0;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
		rsDesc.NumParameters = 2;
		rsDesc.pParameters = rootParams;
		rsDesc.NumStaticSamplers = 1;
		rsDesc.pStaticSamplers = &sampler;
		rsDesc.Flags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		HRESULT hr = D3D12SerializeRootSignature(
			&rsDesc,
			D3D_ROOT_SIGNATURE_VERSION_1,
			signature.GetAddressOf(),
			error.GetAddressOf());
		if (FAILED(hr))
		{
			std::string errorMsg = "Dx12Pipeline: SerializeRootSignature failed";
			if (error)
			{
				errorMsg += ": ";
				errorMsg += static_cast<const char*>(
					error->GetBufferPointer());
			}
			throw std::runtime_error(errorMsg);
		}

		hr = device->CreateRootSignature(
			0,
			signature->GetBufferPointer(),
			signature->GetBufferSize(),
			IID_PPV_ARGS(m_rootSignature.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx12Pipeline: CreateRootSignature failed");
		}
	}

	/// @brief パイプラインステートオブジェクト(PSO)を生成する
	/// @param device D3D12デバイス
	/// @param desc パイプライン記述子
	void createPipelineState(ID3D12Device* device,
	                         const Dx12PipelineDesc& desc)
	{
		/// 入力レイアウト: position(float2) + texCoord(float2) + color(float4)
		const D3D12_INPUT_ELEMENT_DESC inputLayout[] =
		{
			{
				"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,
				0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
			},
			{
				"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
				0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
			},
			{
				"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,
				0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
			},
		};

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = {
			inputLayout,
			static_cast<UINT>(std::size(inputLayout))
		};
		psoDesc.pRootSignature = m_rootSignature.Get();
		psoDesc.VS = desc.vertexShader->shaderBytecode();
		psoDesc.PS = desc.pixelShader->shaderBytecode();

		/// ラスタライザステート
		psoDesc.RasterizerState.FillMode = desc.wireframe
			? D3D12_FILL_MODE_WIREFRAME
			: D3D12_FILL_MODE_SOLID;
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
		psoDesc.RasterizerState.DepthClipEnable = TRUE;
		psoDesc.RasterizerState.MultisampleEnable = FALSE;
		psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
		psoDesc.RasterizerState.ForcedSampleCount = 0;
		psoDesc.RasterizerState.ConservativeRaster =
			D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

		/// ブレンドステート
		setupBlendState(psoDesc.BlendState, desc.blendMode);

		/// 深度ステンシルステート（無効）
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.StencilEnable = FALSE;

		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType =
			D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = desc.numRenderTargets;
		psoDesc.RTVFormats[0] = desc.rtvFormat;
		psoDesc.SampleDesc.Count = 1;

		HRESULT hr = device->CreateGraphicsPipelineState(
			&psoDesc, IID_PPV_ARGS(m_pso.GetAddressOf()));
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx12Pipeline: CreateGraphicsPipelineState failed");
		}
	}

	/// @brief ブレンドステートを設定する
	/// @param blendDesc 設定先のブレンドステート記述子
	/// @param mode ブレンドモード
	static void setupBlendState(D3D12_BLEND_DESC& blendDesc, BlendMode mode)
	{
		blendDesc.AlphaToCoverageEnable = FALSE;
		blendDesc.IndependentBlendEnable = FALSE;

		auto& rt = blendDesc.RenderTarget[0];
		rt.LogicOpEnable = FALSE;
		rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

		switch (mode)
		{
		case BlendMode::None:
			rt.BlendEnable = FALSE;
			break;
		case BlendMode::Alpha:
			rt.BlendEnable = TRUE;
			rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
			rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			rt.BlendOp = D3D12_BLEND_OP_ADD;
			rt.SrcBlendAlpha = D3D12_BLEND_ONE;
			rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
			rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			break;
		case BlendMode::Additive:
			rt.BlendEnable = TRUE;
			rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
			rt.DestBlend = D3D12_BLEND_ONE;
			rt.BlendOp = D3D12_BLEND_OP_ADD;
			rt.SrcBlendAlpha = D3D12_BLEND_ONE;
			rt.DestBlendAlpha = D3D12_BLEND_ONE;
			rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			break;
		case BlendMode::Multiplicative:
			rt.BlendEnable = TRUE;
			rt.SrcBlend = D3D12_BLEND_DEST_COLOR;
			rt.DestBlend = D3D12_BLEND_ZERO;
			rt.BlendOp = D3D12_BLEND_OP_ADD;
			rt.SrcBlendAlpha = D3D12_BLEND_DEST_ALPHA;
			rt.DestBlendAlpha = D3D12_BLEND_ZERO;
			rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			break;
		}
	}

	ComPtr<ID3D12PipelineState> m_pso;             ///< パイプラインステート
	ComPtr<ID3D12RootSignature> m_rootSignature;   ///< ルートシグネチャ
	bool m_valid = false;                          ///< 有効フラグ
};

} // namespace mitiru::gfx

#endif // _WIN32
