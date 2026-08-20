#pragma once

/// @file Dx12ComputePipeline.hpp
/// @brief DirectX 12 コンピュートパイプライン実装
/// @details ID3D12PipelineState (compute) と ID3D12RootSignature を管理する IPipeline 実装。
///
///          graphics 側の Dx12Pipeline がルートシグネチャを内部で固定しているのに対し、
///          こちらは呼び出し側が ComputePipelineDesc で宣言する。コンピュートは用途ごとに
///          要求するリソースが違うので、固定した瞬間に二つ目の用途で作り直しになるため。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <stdexcept>
#include <string>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include <mitiru/gfx/GfxTypes.hpp>
#include <mitiru/gfx/IPipeline.hpp>
#include <mitiru/gfx/dx12/Dx12Shader.hpp>

namespace mitiru::gfx
{

/// @brief DirectX 12 コンピュートパイプライン実装
///
/// @code
/// const RootParam params[] = {
///     { RootParamType::ConstantBuffer,  0 },   // b0
///     { RootParamType::ShaderResource,  0 },   // t0
///     { RootParamType::UnorderedAccess, 0 },   // u0
/// };
/// ComputePipelineDesc desc;
/// desc.computeShader  = &cs;
/// desc.rootParams     = params;
/// desc.rootParamCount = 3;
/// auto pipeline = std::make_unique<Dx12ComputePipeline>(device, desc);
/// @endcode
class Dx12ComputePipeline final : public IPipeline
{
public:
	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief コンストラクタ
	/// @param device D3D12デバイス
	/// @param desc コンピュートパイプライン記述子
	/// @throws std::runtime_error device/シェーダーが null、または生成に失敗した場合
	Dx12ComputePipeline(ID3D12Device* device, const ComputePipelineDesc& desc)
	{
		if (!device)
		{
			throw std::runtime_error("Dx12ComputePipeline: device is null");
		}
		auto* cs = static_cast<Dx12Shader*>(desc.computeShader);
		if (!cs)
		{
			throw std::runtime_error(
				"Dx12ComputePipeline: computeShader is null");
		}

		createRootSignature(device, desc);
		createPipelineState(device, cs);
		m_valid = true;
	}

	/// @brief パイプラインが有効かどうかを判定する
	[[nodiscard]] bool isValid() const noexcept override { return m_valid; }

	/// @brief コンピュートパイプラインである
	[[nodiscard]] bool isCompute() const noexcept override { return true; }

	/// @brief ルートシグネチャのネイティブポインタを取得する
	[[nodiscard]] void* rootSignature() const override
	{
		return m_rootSignature.Get();
	}

	/// @brief ID3D12PipelineStateを取得する
	[[nodiscard]] ID3D12PipelineState* nativePSO() const noexcept
	{
		return m_pso.Get();
	}

private:
	/// @brief 記述子からルートシグネチャを組み立てる
	void createRootSignature(ID3D12Device* device, const ComputePipelineDesc& desc)
	{
		std::vector<D3D12_ROOT_PARAMETER> params(desc.rootParamCount);
		/// レンジは params が参照し続けるので、生存期間をこのスコープに揃える。
		/// vector の再確保でポインタが無効化されないよう、先に確保しておく。
		std::vector<D3D12_DESCRIPTOR_RANGE> ranges(desc.rootParamCount);

		for (uint32_t i = 0; i < desc.rootParamCount; ++i)
		{
			const RootParam& src = desc.rootParams[i];
			D3D12_ROOT_PARAMETER& dst = params[i];
			/// コンピュートのルート引数は全ステージ可視で構わない。
			/// D3D12 では compute に個別の可視性指定は無く、ALL が唯一の正解。
			dst.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

			switch (src.type)
			{
			case RootParamType::ConstantBuffer:
				dst.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
				dst.Descriptor.ShaderRegister = src.shaderRegister;
				dst.Descriptor.RegisterSpace = 0;
				break;
			case RootParamType::ShaderResource:
				dst.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
				dst.Descriptor.ShaderRegister = src.shaderRegister;
				dst.Descriptor.RegisterSpace = 0;
				break;
			case RootParamType::UnorderedAccess:
				dst.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
				dst.Descriptor.ShaderRegister = src.shaderRegister;
				dst.Descriptor.RegisterSpace = 0;
				break;
			case RootParamType::DescriptorTable:
			default:
				ranges[i].RangeType = toRangeType(src.tableRangeType);
				ranges[i].NumDescriptors = src.descriptorCount;
				ranges[i].BaseShaderRegister = src.shaderRegister;
				ranges[i].RegisterSpace = 0;
				ranges[i].OffsetInDescriptorsFromTableStart =
					D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

				dst.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
				dst.DescriptorTable.NumDescriptorRanges = 1;
				dst.DescriptorTable.pDescriptorRanges = &ranges[i];
				break;
			}
		}

		D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
		rsDesc.NumParameters = desc.rootParamCount;
		rsDesc.pParameters = params.empty() ? nullptr : params.data();
		/// 入力アセンブラは compute に存在しないので、その許可フラグは立てない。
		rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		const HRESULT hr = D3D12SerializeRootSignature(
			&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
		if (FAILED(hr))
		{
			/// シリアライザは失敗理由を error blob に書く。捨てると
			/// 「どのルートパラメータが悪いのか」が永久に分からなくなる。
			const char* detail = error
				? static_cast<const char*>(error->GetBufferPointer())
				: "(詳細なし)";
			throw std::runtime_error(
				std::string("Dx12ComputePipeline: ルートシグネチャの直列化に失敗: ")
				+ detail);
		}

		if (FAILED(device->CreateRootSignature(
				0, signature->GetBufferPointer(), signature->GetBufferSize(),
				IID_PPV_ARGS(&m_rootSignature))))
		{
			throw std::runtime_error(
				"Dx12ComputePipeline: CreateRootSignature に失敗");
		}
	}

	/// @brief コンピュート PSO を生成する
	void createPipelineState(ID3D12Device* device, Dx12Shader* cs)
	{
		if (cs->type() != ShaderType::Compute)
		{
			throw std::runtime_error(
				"Dx12ComputePipeline: 渡されたシェーダーが Compute ではない。"
				"cs_* プロファイルでコンパイルし、ShaderType::Compute で作ること");
		}

		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = m_rootSignature.Get();
		psoDesc.CS = cs->shaderBytecode();

		if (FAILED(device->CreateComputePipelineState(
				&psoDesc, IID_PPV_ARGS(&m_pso))))
		{
			throw std::runtime_error(
				"Dx12ComputePipeline: CreateComputePipelineState に失敗。"
				"シェーダーが cs_* プロファイルでコンパイルされているか確認すること");
		}
	}

	/// @brief RootParamType をデスクリプタレンジ種別へ写す
	[[nodiscard]] static D3D12_DESCRIPTOR_RANGE_TYPE toRangeType(RootParamType t) noexcept
	{
		switch (t)
		{
		case RootParamType::ConstantBuffer:  return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		case RootParamType::UnorderedAccess: return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		case RootParamType::ShaderResource:
		case RootParamType::DescriptorTable:
		default:                             return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		}
	}

	ComPtr<ID3D12RootSignature> m_rootSignature;  ///< ルートシグネチャ
	ComPtr<ID3D12PipelineState> m_pso;            ///< コンピュート PSO
	bool m_valid = false;                         ///< 構築成功フラグ
};

} // namespace mitiru::gfx

#endif // _WIN32
