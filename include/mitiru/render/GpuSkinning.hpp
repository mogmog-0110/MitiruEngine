#pragma once

/// @file GpuSkinning.hpp
/// @brief GPU加速スケルタルスキニング（DX11）
/// @details ボーン変換行列をGPUに転送し、スキニング頂点シェーダーで
///          頂点をリアルタイムに変形する。最大128ボーン、1頂点あたり4ボーン影響。

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/GlmBridge.hpp>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d3d11.lib")

#endif // _WIN32

namespace mitiru::render
{

/// @brief スキニング用の最大ボーン数
inline constexpr int kMaxBones = 128;

/// @brief スキニング頂点データ
/// @details 通常の3D頂点に加え、ボーンインデックスとウェイトを持つ。
struct SkinnedVertex
{
	sgc::Vec3f position{};                             ///< 位置
	sgc::Vec3f normal{};                               ///< 法線
	sgc::Vec2f texCoord{};                             ///< テクスチャ座標
	sgc::Colorf color{1.0f, 1.0f, 1.0f, 1.0f};       ///< 頂点色
	std::array<uint32_t, 4> boneIndices{0, 0, 0, 0};  ///< 影響ボーンインデックス（最大4）
	std::array<float, 4> boneWeights{0.0f, 0.0f, 0.0f, 0.0f}; ///< ボーンウェイト（合計1.0）

	constexpr SkinnedVertex() noexcept = default;

	constexpr SkinnedVertex(const sgc::Vec3f& pos,
	                        const sgc::Vec3f& nrm,
	                        const sgc::Vec2f& uv,
	                        const sgc::Colorf& col,
	                        const std::array<uint32_t, 4>& indices,
	                        const std::array<float, 4>& weights) noexcept
		: position(pos)
		, normal(nrm)
		, texCoord(uv)
		, color(col)
		, boneIndices(indices)
		, boneWeights(weights)
	{
	}
};

/// @brief スキニング用定数バッファ（CbSkinning: register(b3)）
/// @details ボーン変換行列の配列をGPUに転送する。
struct alignas(16) CbSkinning
{
	float boneMatrices[kMaxBones][4][4]{};  ///< ボーン変換行列（最大128）
};

/// @brief スキニング用頂点シェーダー（HLSL SM5.0）
/// @details 1頂点あたり最大4ボーンの影響を適用する。
///          CbTransform(b0) と CbSkinning(b3) を参照する。
constexpr const char* SKINNING_VS = R"hlsl(
cbuffer CbTransform : register(b0)
{
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
};

cbuffer CbSkinning : register(b3)
{
    float4x4 BoneMatrices[128];
};

struct VSInput
{
    float3 Position    : POSITION;
    float3 Normal      : NORMAL;
    float2 TexCoord    : TEXCOORD0;
    float4 Color       : COLOR0;
    uint4  BoneIndices : BLENDINDICES;
    float4 BoneWeights : BLENDWEIGHT;
};

struct VSOutput
{
    float4 Position  : SV_POSITION;
    float3 WorldPos  : TEXCOORD0;
    float3 WorldNorm : TEXCOORD1;
    float2 TexCoord  : TEXCOORD2;
    float4 Color     : COLOR0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;

    // Apply up to 4 bone influences per vertex
    float4 skinnedPos = float4(0, 0, 0, 0);
    float3 skinnedNormal = float3(0, 0, 0);
    for (int i = 0; i < 4; i++)
    {
        float w = input.BoneWeights[i];
        if (w > 0)
        {
            float4x4 bone = BoneMatrices[input.BoneIndices[i]];
            skinnedPos += mul(bone, float4(input.Position, 1)) * w;
            skinnedNormal += mul((float3x3)bone, input.Normal) * w;
        }
    }

    float4 worldPos = mul(skinnedPos, World);
    output.WorldPos = worldPos.xyz;
    output.WorldNorm = normalize(mul(skinnedNormal, (float3x3)World));

    float4 viewPos = mul(worldPos, View);
    output.Position = mul(viewPos, Projection);

    output.TexCoord = input.TexCoord;
    output.Color = input.Color;

    return output;
}
)hlsl";

#ifdef _WIN32

/// @brief GPU加速スケルタルスキナー
/// @details スキニング用シェーダーのコンパイル、ボーン定数バッファの管理、
///          入力レイアウトの構築を行い、Renderer3Dと連携してスキンドメッシュを描画する。
///
/// @code
/// mitiru::render::GpuSkinner skinner;
/// skinner.init(d3dDevice);
///
/// // 毎フレーム
/// auto matrices = skeleton.computeSkinningMatrices();
/// skinner.updateBones(d3dContext, matrices.data(), matrices.size());
/// skinner.bindSkinning(d3dContext);
/// // ... drawMesh() ...
/// skinner.unbindSkinning(d3dContext);
/// @endcode
class GpuSkinner
{
public:
	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	GpuSkinner() noexcept = default;

	/// @brief 初期化済みかどうか
	[[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

	/// @brief 初期化する
	/// @param device DX11デバイス
	void init(ID3D11Device* device)
	{
		if (!device)
		{
			return;
		}

		m_device = device;

		compileSkinningShader();
		createBoneConstantBuffer();
		createInputLayout();

		m_initialized = true;
	}

	/// @brief ボーン変換行列をGPUにアップロードする
	/// @param context DX11デバイスコンテキスト
	/// @param matrices ボーン変換行列の配列（sgc::Mat4f）
	/// @param count ボーン数
	void updateBones(ID3D11DeviceContext* context,
	                 const sgc::Mat4f* matrices,
	                 size_t count)
	{
		if (!context || !matrices || !m_cbSkinning)
		{
			return;
		}

		m_cbData = {};
		const size_t boneCount = (std::min)(count, static_cast<size_t>(kMaxBones));
		for (size_t i = 0; i < boneCount; ++i)
		{
			glm::mat4 m = toGlm(matrices[i]);
			toHLSL(m_cbData.boneMatrices[i], m);
		}

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		HRESULT hr = context->Map(
			m_cbSkinning.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (SUCCEEDED(hr))
		{
			std::memcpy(mapped.pData, &m_cbData, sizeof(m_cbData));
			context->Unmap(m_cbSkinning.Get(), 0);
		}
	}

	/// @brief スキニングシェーダーとボーン定数バッファをバインドする
	/// @param context DX11デバイスコンテキスト
	/// @details 既存のVSを保存し、スキニングVS + ボーンCB(b3) + 入力レイアウトを設定する。
	void bindSkinning(ID3D11DeviceContext* context)
	{
		if (!context || !m_initialized)
		{
			return;
		}

		/// 既存のVSと入力レイアウトを保存する
		context->VSGetShader(m_savedVS.GetAddressOf(), nullptr, nullptr);
		context->IAGetInputLayout(m_savedInputLayout.GetAddressOf());

		/// スキニングVSと入力レイアウトを設定する
		context->VSSetShader(m_skinningVS.Get(), nullptr, 0);
		context->IASetInputLayout(m_skinningInputLayout.Get());

		/// ボーン定数バッファをb3にバインドする
		ID3D11Buffer* buf = m_cbSkinning.Get();
		context->VSSetConstantBuffers(3, 1, &buf);
	}

	/// @brief スキニングを解除し、元のVSに復元する
	/// @param context DX11デバイスコンテキスト
	void unbindSkinning(ID3D11DeviceContext* context)
	{
		if (!context)
		{
			return;
		}

		/// 保存したVSと入力レイアウトに復元する
		context->VSSetShader(m_savedVS.Get(), nullptr, 0);
		context->IASetInputLayout(m_savedInputLayout.Get());

		m_savedVS.Reset();
		m_savedInputLayout.Reset();
	}

	/// @brief スキニング用入力レイアウトを取得する
	/// @return 入力レイアウトへのポインタ
	[[nodiscard]] ID3D11InputLayout* inputLayout() const noexcept
	{
		return m_skinningInputLayout.Get();
	}

private:
	/// @brief スキニング頂点シェーダーをコンパイルする
	void compileSkinningShader()
	{
		ComPtr<ID3DBlob> shaderBlob;
		ComPtr<ID3DBlob> errorBlob;

		UINT flags = 0;
#ifdef _DEBUG
		flags |= D3DCOMPILE_DEBUG;
		flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

		HRESULT hr = D3DCompile(
			SKINNING_VS,
			std::strlen(SKINNING_VS),
			nullptr, nullptr, nullptr,
			"VSMain", "vs_5_0",
			flags, 0,
			shaderBlob.GetAddressOf(),
			errorBlob.GetAddressOf());

		if (FAILED(hr))
		{
			std::string msg = "GpuSkinner: D3DCompile failed";
			if (errorBlob)
			{
				msg += ": ";
				msg += static_cast<const char*>(errorBlob->GetBufferPointer());
			}
			throw std::runtime_error(msg);
		}

		hr = m_device->CreateVertexShader(
			shaderBlob->GetBufferPointer(),
			shaderBlob->GetBufferSize(),
			nullptr,
			m_skinningVS.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error("GpuSkinner: CreateVertexShader failed");
		}

		/// バイトコードを保存する（InputLayout用）
		m_vsBytecode.resize(shaderBlob->GetBufferSize());
		std::memcpy(m_vsBytecode.data(),
		            shaderBlob->GetBufferPointer(),
		            shaderBlob->GetBufferSize());
	}

	/// @brief ボーン定数バッファを作成する
	void createBoneConstantBuffer()
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(CbSkinning);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		HRESULT hr = m_device->CreateBuffer(
			&desc, nullptr, m_cbSkinning.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error("GpuSkinner: CreateBuffer(CbSkinning) failed");
		}
	}

	/// @brief SkinnedVertex用の入力レイアウトを作成する
	void createInputLayout()
	{
		/// SkinnedVertex: position(float3) + normal(float3) + texCoord(float2)
		///              + color(float4) + boneIndices(uint4) + boneWeights(float4)
		const D3D11_INPUT_ELEMENT_DESC layout[] =
		{
			{
				"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
				0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0
			},
			{
				"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,
				0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0
			},
			{
				"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
				0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0
			},
			{
				"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,
				0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0
			},
			{
				"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT,
				0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0
			},
			{
				"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,
				0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0
			},
		};

		HRESULT hr = m_device->CreateInputLayout(
			layout,
			static_cast<UINT>(std::size(layout)),
			m_vsBytecode.data(),
			m_vsBytecode.size(),
			m_skinningInputLayout.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error("GpuSkinner: CreateInputLayout failed");
		}
	}

	ComPtr<ID3D11Device> m_device;
	bool m_initialized = false;

	/// @brief シェーダー・レイアウト
	ComPtr<ID3D11VertexShader> m_skinningVS;
	ComPtr<ID3D11InputLayout> m_skinningInputLayout;
	std::vector<uint8_t> m_vsBytecode;

	/// @brief 定数バッファ
	ComPtr<ID3D11Buffer> m_cbSkinning;

	/// @brief ボーンデータ（メンバに保持してスタック上の大量割り当てを回避）
	CbSkinning m_cbData{};

	/// @brief バインド前の保存用
	ComPtr<ID3D11VertexShader> m_savedVS;
	ComPtr<ID3D11InputLayout> m_savedInputLayout;
};

#endif // _WIN32

} // namespace mitiru::render
