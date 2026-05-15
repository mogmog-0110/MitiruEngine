#pragma once

/// @file NormalMappingIntegration.hpp
/// @brief 法線マッピングGPU統合
/// @details タンジェント計算・TBN行列生成・法線マップ付きPhongシェーディングを
///          DX11パイプラインに統合する。Renderer3Dとの連携APIを提供する。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <sgc/math/Vec2.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/math/Vec4.hpp>
#include <sgc/types/Color.hpp>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")

#include <mitiru/render/Vertex3D.hpp>
#include <mitiru/render/ImageLoader.hpp>

namespace mitiru::render
{

// ============================================================================
// 拡張頂点構造体
// ============================================================================

/// @brief タンジェント付き3D頂点
/// @details 法線マッピングに必要なタンジェントベクトルを含む頂点型。
///          tangent.w にはハンドネス（+1 or -1）を格納し、
///          ビタンジェントはシェーダー内で cross(normal, tangent.xyz) * tangent.w で計算する。
struct VertexWithTangent
{
	sgc::Vec3f position{};                       ///< ワールド空間位置
	sgc::Vec3f normal{};                         ///< 法線ベクトル
	sgc::Vec2f texCoord{};                       ///< テクスチャ座標 [0,1]
	sgc::Colorf color{1.0f, 1.0f, 1.0f, 1.0f};  ///< 頂点色
	sgc::Vec4f tangent{};                        ///< タンジェント (xyz=方向, w=ハンドネス)

	/// @brief デフォルトコンストラクタ
	constexpr VertexWithTangent() noexcept = default;

	/// @brief 全フィールド指定コンストラクタ
	constexpr VertexWithTangent(
		const sgc::Vec3f& position,
		const sgc::Vec3f& normal,
		const sgc::Vec2f& texCoord,
		const sgc::Colorf& color,
		const sgc::Vec4f& tangent) noexcept
		: position(position)
		, normal(normal)
		, texCoord(texCoord)
		, color(color)
		, tangent(tangent)
	{
	}

	/// @brief Vertex3Dからの変換コンストラクタ
	explicit constexpr VertexWithTangent(const Vertex3D& v) noexcept
		: position(v.position)
		, normal(v.normal)
		, texCoord(v.texCoord)
		, color(v.color)
	{
	}
};

static_assert(std::is_standard_layout_v<VertexWithTangent>, "VertexWithTangent must be standard layout for offsetof");

// ============================================================================
// 法線マップテクスチャ
// ============================================================================

/// @brief 法線マップテクスチャリソース
/// @details テクスチャとSRVをまとめて保持する。
struct NormalMapTexture
{
	Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;         ///< テクスチャリソース
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;    ///< SRV
	std::uint32_t width = 0;     ///< テクスチャ幅
	std::uint32_t height = 0;    ///< テクスチャ高さ

	/// @brief 有効な法線マップかどうかを取得する
	[[nodiscard]] bool valid() const noexcept
	{
		return srv != nullptr;
	}
};

// ============================================================================
// HLSL定数 — 法線マッピング付きPhongシェーダー
// ============================================================================

/// @brief 法線マッピング対応 頂点シェーダー
/// @details タンジェント入力を受け取り、TBN行列の各ベクトルを
///          ピクセルシェーダーに渡す。
constexpr std::string_view NORMAL_MAP_VS = R"hlsl(
cbuffer CbTransform : register(b0)
{
	float4x4 World;
	float4x4 View;
	float4x4 Projection;
};

struct VSInput
{
	float3 Position : POSITION;
	float3 Normal   : NORMAL;
	float2 TexCoord : TEXCOORD0;
	float4 Color    : COLOR0;
	float4 Tangent  : TANGENT0;
};

struct VSOutput
{
	float4 Position   : SV_POSITION;
	float3 WorldPos   : TEXCOORD0;
	float3 WorldNorm  : TEXCOORD1;
	float2 TexCoord   : TEXCOORD2;
	float4 Color      : COLOR0;
	float3 WorldTan   : TEXCOORD3;
	float3 WorldBitan : TEXCOORD4;
};

VSOutput VSMain(VSInput input)
{
	VSOutput output;

	float4 worldPos = mul(float4(input.Position, 1.0), World);
	output.WorldPos = worldPos.xyz;

	float3 N = normalize(mul(input.Normal, (float3x3)World));
	float3 T = normalize(mul(input.Tangent.xyz, (float3x3)World));

	// グラム・シュミット直交化
	T = normalize(T - dot(T, N) * N);
	float3 B = cross(N, T) * input.Tangent.w;

	output.WorldNorm = N;
	output.WorldTan = T;
	output.WorldBitan = B;

	float4 viewPos = mul(worldPos, View);
	output.Position = mul(viewPos, Projection);

	output.TexCoord = input.TexCoord;
	output.Color = input.Color;

	return output;
}
)hlsl";

/// @brief 法線マッピング対応 ピクセルシェーダー
/// @details TBN行列を使って法線マップからワールド空間法線を再構成し、
///          Phongシェーディングを行う。法線マップがバインドされていない場合は
///          頂点法線をそのまま使用する。
constexpr std::string_view NORMAL_MAP_PS = R"hlsl(
Texture2D DiffuseTexture : register(t0);
Texture2D NormalTexture  : register(t1);
SamplerState samp        : register(s0);

cbuffer CbLighting : register(b1)
{
	float3 LightDir;
	float  _pad0;
	float3 LightColor;
	float  _pad1;
	float3 AmbientColor;
	float  _pad2;
	float3 CameraPos;
	float  _pad3;
	float4 MaterialDiffuse;
	float4 MaterialSpecular;
	float  MaterialShininess;
	float3 _pad4;
};

cbuffer CbNormalMap : register(b2)
{
	int hasNormalMap;
	float normalMapStrength;
	float2 _nmPad;
};

struct PSInput
{
	float4 Position   : SV_POSITION;
	float3 WorldPos   : TEXCOORD0;
	float3 WorldNorm  : TEXCOORD1;
	float2 TexCoord   : TEXCOORD2;
	float4 Color      : COLOR0;
	float3 WorldTan   : TEXCOORD3;
	float3 WorldBitan : TEXCOORD4;
};

float4 PSMain(PSInput input) : SV_TARGET
{
	float3 N = normalize(input.WorldNorm);

	if (hasNormalMap)
	{
		// 法線マップからタンジェント空間の法線を読み取る
		float3 normalMap = NormalTexture.Sample(samp, input.TexCoord).xyz;
		normalMap = normalMap * 2.0 - 1.0;

		// 法線マップの強度を適用する
		normalMap.xy *= normalMapStrength;
		normalMap = normalize(normalMap);

		// TBN行列でワールド空間に変換する
		float3 T = normalize(input.WorldTan);
		float3 B = normalize(input.WorldBitan);
		float3x3 TBN = float3x3(T, B, N);

		N = normalize(mul(normalMap, TBN));
	}

	// Phongシェーディング
	float3 L = normalize(-LightDir);
	float3 V = normalize(CameraPos - input.WorldPos);

	float3 ambient = AmbientColor * MaterialDiffuse.rgb;

	float NdotL = max(dot(N, L), 0.0);
	float3 diffuse = LightColor * MaterialDiffuse.rgb * NdotL;

	float3 H = normalize(L + V);
	float NdotH = max(dot(N, H), 0.0);
	float specFactor = pow(NdotH, MaterialShininess);
	float3 specular = LightColor * MaterialSpecular.rgb * specFactor;

	float3 finalColor = ambient + diffuse + specular;

	// テクスチャをサンプルする
	float4 texColor = DiffuseTexture.Sample(samp, input.TexCoord);

	float alpha = MaterialDiffuse.a * input.Color.a * texColor.a;
	return float4(finalColor * input.Color.rgb * texColor.rgb, alpha);
}
)hlsl";

// ============================================================================
// NormalMapHelperクラス
// ============================================================================

/// @brief 法線マッピングヘルパー
/// @details 法線マップのロード、タンジェント計算、入力レイアウト生成、
///          Renderer3Dとの統合を担当する。
///
/// @code
/// NormalMapHelper normalHelper;
/// normalHelper.init(device);
///
/// // 法線マップをロードする
/// auto normalMap = normalHelper.loadNormalMap(device, "assets/brick_normal.png");
///
/// // タンジェントを計算する
/// std::vector<VertexWithTangent> tangentVerts;
/// normalHelper.computeTangents(vertices, indices, tangentVerts);
///
/// // メッシュに法線マップを設定する
/// normalHelper.setNormalMap(meshId, normalMap);
/// @endcode
class NormalMapHelper
{
public:
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief デフォルトコンストラクタ
	NormalMapHelper() noexcept = default;

	/// @brief ヘルパーを初期化する
	/// @param device D3D11デバイス
	void init(ID3D11Device* device)
	{
		if (!device)
		{
			throw std::runtime_error("NormalMapHelper: device is null");
		}

		m_device = device;
		compileShaders();
		createInputLayout();
		createNormalMapConstantBuffer();
		createSampler();

		m_initialized = true;
	}

	/// @brief 初期化済みかどうかを取得する
	[[nodiscard]] bool isInitialized() const noexcept
	{
		return m_initialized;
	}

	/// @brief 法線マップをメモリから生成する
	/// @param device D3D11デバイス
	/// @param pixels ピクセルデータ（RGBA8）
	/// @param width テクスチャ幅
	/// @param height テクスチャ高さ
	/// @return 生成されたNormalMapTexture
	[[nodiscard]] NormalMapTexture createNormalMapFromMemory(
		ID3D11Device* device,
		const std::uint8_t* pixels,
		int width, int height)
	{
		if (!device || !pixels || width <= 0 || height <= 0)
		{
			throw std::runtime_error(
				"NormalMapHelper: invalid parameters for createNormalMapFromMemory");
		}

		NormalMapTexture result;
		result.width = static_cast<std::uint32_t>(width);
		result.height = static_cast<std::uint32_t>(height);

		// テクスチャを作成する
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = result.width;
		desc.Height = result.height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem = pixels;
		initData.SysMemPitch = result.width * 4;

		HRESULT hr = device->CreateTexture2D(
			&desc, &initData, result.texture.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"NormalMapHelper: CreateTexture2D failed");
		}

		// SRVを作成する
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		hr = device->CreateShaderResourceView(
			result.texture.Get(), &srvDesc, result.srv.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"NormalMapHelper: CreateShaderResourceView failed");
		}

		return result;
	}

	/// @brief タンジェントとビタンジェントを計算する
	/// @param vertices 入力頂点配列
	/// @param indices インデックス配列
	/// @param outVertices 出力タンジェント付き頂点配列
	/// @details MikkTSpace互換のアルゴリズムで三角形ごとにタンジェントを計算し、
	///          頂点に蓄積・正規化する。ハンドネスはw成分に格納する。
	static void computeTangents(
		const std::vector<Vertex3D>& vertices,
		const std::vector<std::uint32_t>& indices,
		std::vector<VertexWithTangent>& outVertices)
	{
		outVertices.resize(vertices.size());

		// Vertex3Dをコピーする
		for (std::size_t i = 0; i < vertices.size(); ++i)
		{
			outVertices[i] = VertexWithTangent(vertices[i]);
		}

		if (indices.size() < 3)
		{
			return;
		}

		// タンジェント・ビタンジェント蓄積用バッファ
		std::vector<sgc::Vec3f> tan1(vertices.size());
		std::vector<sgc::Vec3f> tan2(vertices.size());

		// 三角形ごとにタンジェントを計算する
		for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
		{
			const std::uint32_t i0 = indices[i];
			const std::uint32_t i1 = indices[i + 1];
			const std::uint32_t i2 = indices[i + 2];

			if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
			{
				continue;
			}

			const sgc::Vec3f& p0 = vertices[i0].position;
			const sgc::Vec3f& p1 = vertices[i1].position;
			const sgc::Vec3f& p2 = vertices[i2].position;

			const sgc::Vec2f& uv0 = vertices[i0].texCoord;
			const sgc::Vec2f& uv1 = vertices[i1].texCoord;
			const sgc::Vec2f& uv2 = vertices[i2].texCoord;

			const sgc::Vec3f edge1 = p1 - p0;
			const sgc::Vec3f edge2 = p2 - p0;

			const float deltaU1 = uv1.x - uv0.x;
			const float deltaV1 = uv1.y - uv0.y;
			const float deltaU2 = uv2.x - uv0.x;
			const float deltaV2 = uv2.y - uv0.y;

			const float denom = deltaU1 * deltaV2 - deltaU2 * deltaV1;
			if (std::abs(denom) < 1e-8f)
			{
				continue;
			}

			const float r = 1.0f / denom;

			const sgc::Vec3f tangent{
				(deltaV2 * edge1.x - deltaV1 * edge2.x) * r,
				(deltaV2 * edge1.y - deltaV1 * edge2.y) * r,
				(deltaV2 * edge1.z - deltaV1 * edge2.z) * r
			};

			const sgc::Vec3f bitangent{
				(deltaU1 * edge2.x - deltaU2 * edge1.x) * r,
				(deltaU1 * edge2.y - deltaU2 * edge1.y) * r,
				(deltaU1 * edge2.z - deltaU2 * edge1.z) * r
			};

			// 各頂点に蓄積する
			tan1[i0] = tan1[i0] + tangent;
			tan1[i1] = tan1[i1] + tangent;
			tan1[i2] = tan1[i2] + tangent;

			tan2[i0] = tan2[i0] + bitangent;
			tan2[i1] = tan2[i1] + bitangent;
			tan2[i2] = tan2[i2] + bitangent;
		}

		// 各頂点のタンジェントを正規化し、ハンドネスを計算する
		for (std::size_t i = 0; i < vertices.size(); ++i)
		{
			const sgc::Vec3f& n = vertices[i].normal;
			const sgc::Vec3f& t = tan1[i];

			// グラム・シュミット直交化（ゼロベクトル対策付き）
			const sgc::Vec3f ortho = t - n * n.dot(t);
			const float orthoLen = std::sqrt(
				ortho.x * ortho.x + ortho.y * ortho.y + ortho.z * ortho.z);
			const sgc::Vec3f tangent = (orthoLen > 1e-6f)
				? sgc::Vec3f{ortho.x / orthoLen, ortho.y / orthoLen, ortho.z / orthoLen}
				: sgc::Vec3f{1.0f, 0.0f, 0.0f};

			// ハンドネスを計算する（cross(n, t) . tan2 の符号）
			const sgc::Vec3f crossVec{
				n.y * t.z - n.z * t.y,
				n.z * t.x - n.x * t.z,
				n.x * t.y - n.y * t.x
			};
			const float handedness = crossVec.dot(tan2[i]) < 0.0f ? -1.0f : 1.0f;

			outVertices[i].tangent = sgc::Vec4f{
				tangent.x, tangent.y, tangent.z, handedness
			};
		}
	}

	/// @brief メッシュに法線マップを関連付ける
	/// @param meshId メッシュ識別子
	/// @param normalMap 法線マップテクスチャ
	void setNormalMap(std::uint32_t meshId, const NormalMapTexture& normalMap)
	{
		m_normalMaps[meshId] = normalMap;
	}

	/// @brief メッシュの法線マップを解除する
	/// @param meshId メッシュ識別子
	void removeNormalMap(std::uint32_t meshId)
	{
		m_normalMaps.erase(meshId);
	}

	/// @brief メッシュにタンジェントデータがあるかどうかを取得する
	/// @param meshId メッシュ識別子
	[[nodiscard]] bool hasNormalMap(std::uint32_t meshId) const noexcept
	{
		return m_normalMaps.count(meshId) > 0;
	}

	/// @brief 法線マッピングシェーダーを有効にする
	/// @param context D3D11デバイスコンテキスト
	/// @param meshId 描画中のメッシュID
	/// @param normalMapStrength 法線マップの適用強度 [0, 1]
	void bind(ID3D11DeviceContext* context,
	          std::uint32_t meshId,
	          float normalMapStrength = 1.0f)
	{
		if (!m_initialized || !context)
		{
			return;
		}

		// シェーダーを設定する
		context->VSSetShader(m_normalMapVS.Get(), nullptr, 0);
		context->PSSetShader(m_normalMapPS.Get(), nullptr, 0);
		context->IASetInputLayout(m_inputLayout.Get());

		// 法線マップ定数バッファを更新する
		auto it = m_normalMaps.find(meshId);
		const bool hasMap = (it != m_normalMaps.end());
		updateNormalMapCB(context, hasMap, normalMapStrength);

		// 法線マップがあればバインドする
		if (hasMap)
		{
			auto* srv = it->second.srv.Get();
			context->PSSetShaderResources(1, 1, &srv);
		}

		context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
		context->PSSetConstantBuffers(2, 1, m_cbNormalMap.GetAddressOf());
	}

	/// @brief 法線マッピングシェーダーを解除する
	/// @param context D3D11デバイスコンテキスト
	void unbind(ID3D11DeviceContext* context)
	{
		if (!context)
		{
			return;
		}

		ID3D11ShaderResourceView* nullSRV = nullptr;
		context->PSSetShaderResources(1, 1, &nullSRV);
	}

	/// @brief タンジェント付き頂点のDX11入力レイアウトを取得する
	/// @return 入力レイアウトのComPtr
	[[nodiscard]] const ComPtr<ID3D11InputLayout>& inputLayout() const noexcept
	{
		return m_inputLayout;
	}

	/// @brief 頂点シェーダーを取得する
	[[nodiscard]] const ComPtr<ID3D11VertexShader>& vertexShader() const noexcept
	{
		return m_normalMapVS;
	}

	/// @brief ピクセルシェーダーを取得する
	[[nodiscard]] const ComPtr<ID3D11PixelShader>& pixelShader() const noexcept
	{
		return m_normalMapPS;
	}

private:
	/// @brief 法線マップ定数バッファ
	struct alignas(16) CbNormalMapParams
	{
		int hasNormalMap;
		float normalMapStrength;
		float _pad[2];
	};

	/// @brief シェーダーをコンパイルする
	void compileShaders()
	{
		// 頂点シェーダー
		{
			ComPtr<ID3DBlob> blob;
			ComPtr<ID3DBlob> errorBlob;
			const std::string vsSource(NORMAL_MAP_VS);
			HRESULT hr = D3DCompile(
				vsSource.data(), vsSource.size(),
				"NormalMapVS", nullptr, nullptr,
				"VSMain", "vs_5_0", 0, 0,
				blob.GetAddressOf(), errorBlob.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"NormalMapHelper: VS compile failed");
			}
			hr = m_device->CreateVertexShader(
				blob->GetBufferPointer(), blob->GetBufferSize(),
				nullptr, m_normalMapVS.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"NormalMapHelper: CreateVertexShader failed");
			}
			m_vsBytecode = blob;
		}

		// ピクセルシェーダー
		{
			ComPtr<ID3DBlob> blob;
			ComPtr<ID3DBlob> errorBlob;
			const std::string psSource(NORMAL_MAP_PS);
			HRESULT hr = D3DCompile(
				psSource.data(), psSource.size(),
				"NormalMapPS", nullptr, nullptr,
				"PSMain", "ps_5_0", 0, 0,
				blob.GetAddressOf(), errorBlob.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"NormalMapHelper: PS compile failed");
			}
			hr = m_device->CreatePixelShader(
				blob->GetBufferPointer(), blob->GetBufferSize(),
				nullptr, m_normalMapPS.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"NormalMapHelper: CreatePixelShader failed");
			}
		}
	}

	/// @brief タンジェント付き頂点の入力レイアウトを作成する
	void createInputLayout()
	{
		D3D11_INPUT_ELEMENT_DESC layout[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
			  offsetof(VertexWithTangent, position),
			  D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
			  offsetof(VertexWithTangent, normal),
			  D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
			  offsetof(VertexWithTangent, texCoord),
			  D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
			  offsetof(VertexWithTangent, color),
			  D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
			  offsetof(VertexWithTangent, tangent),
			  D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

		HRESULT hr = m_device->CreateInputLayout(
			layout, 5,
			m_vsBytecode->GetBufferPointer(),
			m_vsBytecode->GetBufferSize(),
			m_inputLayout.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"NormalMapHelper: CreateInputLayout failed");
		}
	}

	/// @brief 法線マップ定数バッファを作成する
	void createNormalMapConstantBuffer()
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(CbNormalMapParams);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		HRESULT hr = m_device->CreateBuffer(
			&desc, nullptr, m_cbNormalMap.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"NormalMapHelper: CreateBuffer (CB) failed");
		}
	}

	/// @brief サンプラーを作成する
	void createSampler()
	{
		D3D11_SAMPLER_DESC desc = {};
		desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		desc.MaxAnisotropy = 1;
		desc.MaxLOD = D3D11_FLOAT32_MAX;

		HRESULT hr = m_device->CreateSamplerState(
			&desc, m_sampler.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"NormalMapHelper: CreateSamplerState failed");
		}
	}

	/// @brief 法線マップ定数バッファを更新する
	void updateNormalMapCB(ID3D11DeviceContext* context,
	                       bool hasMap,
	                       float strength)
	{
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		HRESULT hr = context->Map(
			m_cbNormalMap.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (FAILED(hr))
		{
			return;
		}

		CbNormalMapParams cb = {};
		cb.hasNormalMap = hasMap ? 1 : 0;
		cb.normalMapStrength = strength;

		std::memcpy(mapped.pData, &cb, sizeof(cb));
		context->Unmap(m_cbNormalMap.Get(), 0);
	}

	ComPtr<ID3D11Device> m_device;
	bool m_initialized = false;

	ComPtr<ID3DBlob> m_vsBytecode;
	ComPtr<ID3D11VertexShader> m_normalMapVS;
	ComPtr<ID3D11PixelShader> m_normalMapPS;
	ComPtr<ID3D11InputLayout> m_inputLayout;
	ComPtr<ID3D11Buffer> m_cbNormalMap;
	ComPtr<ID3D11SamplerState> m_sampler;

	/// @brief メッシュIDと法線マップの対応テーブル
	std::unordered_map<std::uint32_t, NormalMapTexture> m_normalMaps;
};

} // namespace mitiru::render

#endif // _WIN32
