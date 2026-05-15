#pragma once

/// @file DecalSystem.hpp
/// @brief ディファードデカルプロジェクションシステム
/// @details 深度バッファからワールド位置を再構成し、デカルのローカル空間に変換して
///          テクスチャを投影する。DX11デファードレンダリングとの統合を前提とする。

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
#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")

#include <mitiru/render/PostProcess.hpp>

namespace mitiru::render
{

// ============================================================================
// 設定構造体
// ============================================================================

/// @brief デカル設定
struct DecalConfig
{
	int maxDecals = 256;          ///< 同時描画可能なデカル最大数
	float fadeDuration = 3.0f;    ///< デフォルトフェード時間（秒）
	float defaultSize = 1.0f;     ///< デフォルトのデカルサイズ
};

/// @brief デカルID型
using DecalId = std::uint32_t;

/// @brief デカルデータ
/// @details 位置・回転・スケール・テクスチャキー・色・不透明度・フェード情報を保持する。
struct Decal
{
	float position[3]{0, 0, 0};      ///< ワールド空間位置
	float rotation[4]{0, 0, 0, 1};   ///< 回転クォータニオン (x, y, z, w)
	float scale[3]{1, 1, 1};         ///< スケール
	std::string textureKey;           ///< アルベドテクスチャのアセットキー
	std::string normalMapKey;         ///< ノーマルマップのアセットキー（空なら使用しない）
	float color[4]{1, 1, 1, 1};      ///< デカル色 (RGBA)
	float opacity = 1.0f;            ///< 不透明度 [0,1]
	float fadeTime = 0.0f;           ///< フェード開始までの残り時間（秒、0以下なら即フェード）
};

// ============================================================================
// HLSL定数
// ============================================================================

/// @brief デカル投影頂点シェーダー
/// @details デカルボックスの頂点をクリップ空間に変換する。
constexpr std::string_view DECAL_VS = R"hlsl(
cbuffer CbDecalTransform : register(b0)
{
	float4x4 gWorldViewProj;
	float4x4 gInvViewProj;
	float4x4 gDecalWorldInv;
	float4   gDecalColor;
	float    gDecalOpacity;
	float3   _pad0;
};

struct VSInput
{
	float3 Position : POSITION;
};

struct VSOutput
{
	float4 Position    : SV_POSITION;
	float4 ScreenPos   : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
	VSOutput output;
	output.Position = mul(float4(input.Position, 1.0), gWorldViewProj);
	output.ScreenPos = output.Position;
	return output;
}
)hlsl";

/// @brief デカル投影ピクセルシェーダー
/// @details 深度バッファからワールド位置を再構成し、デカルのローカル空間で
///          ボックスクリッピングとテクスチャサンプリングを行う。
constexpr std::string_view DECAL_PS = R"hlsl(
Texture2D depthTexture  : register(t0);
Texture2D normalTexture : register(t1);
Texture2D decalTexture  : register(t2);
Texture2D decalNormalMap : register(t3);
SamplerState linearSampler : register(s0);

cbuffer CbDecalTransform : register(b0)
{
	float4x4 gWorldViewProj;
	float4x4 gInvViewProj;
	float4x4 gDecalWorldInv;
	float4   gDecalColor;
	float    gDecalOpacity;
	float3   _pad0;
};

struct PSInput
{
	float4 Position    : SV_POSITION;
	float4 ScreenPos   : TEXCOORD0;
};

struct PSOutput
{
	float4 Albedo : SV_TARGET0;
	float4 Normal : SV_TARGET1;
};

PSOutput PSMain(PSInput input)
{
	PSOutput output;

	// スクリーン座標からUVを計算する
	float2 screenUV = input.ScreenPos.xy / input.ScreenPos.w;
	screenUV = screenUV * 0.5 + 0.5;
	screenUV.y = 1.0 - screenUV.y;

	// 深度バッファからワールド位置を再構成する
	float depth = depthTexture.Sample(linearSampler, screenUV).r;
	float4 clipPos = float4(
		screenUV.x * 2.0 - 1.0,
		-(screenUV.y * 2.0 - 1.0),
		depth,
		1.0
	);
	float4 worldPos = mul(clipPos, gInvViewProj);
	worldPos /= worldPos.w;

	// デカルのローカル空間に変換する
	float3 localPos = mul(float4(worldPos.xyz, 1.0), gDecalWorldInv).xyz;

	// ボックスクリッピング: [-0.5, 0.5]^3 の外ならdiscardする
	clip(0.5 - abs(localPos.x));
	clip(0.5 - abs(localPos.y));
	clip(0.5 - abs(localPos.z));

	// ローカルXZ座標をデカルテクスチャUVにマッピングする
	float2 decalUV = localPos.xz + 0.5;

	// デカルテクスチャをサンプリングする
	float4 decalColor = decalTexture.Sample(linearSampler, decalUV);
	decalColor *= gDecalColor;
	decalColor.a *= gDecalOpacity;

	// アルファが低すぎる場合はdiscardする
	clip(decalColor.a - 0.01);

	output.Albedo = decalColor;

	// シーンの法線を読み取り、デカルの法線マップがあればブレンドする
	float3 sceneNormal = normalTexture.Sample(linearSampler, screenUV).xyz;
	output.Normal = float4(sceneNormal, 1.0);

	return output;
}
)hlsl";

// ============================================================================
// デカルシステムクラス
// ============================================================================

/// @brief ディファードデカルプロジェクションシステム
/// @details デカルをワールドに投影してシーンジオメトリに貼り付ける。
///          深度バッファとデファードGバッファを参照し、デカルボックスの
///          内部にあるピクセルにのみテクスチャを適用する。
///
/// @code
/// DecalSystem decals;
/// decals.init(device, {});
///
/// Decal bulletHole;
/// bulletHole.position[0] = 5.0f;
/// bulletHole.position[1] = 1.5f;
/// bulletHole.position[2] = 3.0f;
/// bulletHole.textureKey = "decal_bullet";
/// bulletHole.fadeTime = 10.0f;
/// DecalId id = decals.addDecal(bulletHole);
///
/// // 毎フレーム
/// decals.update(deltaTime);
/// decals.render(context, depthSRV, normalSRV);
/// @endcode
class DecalSystem
{
public:
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief デフォルトコンストラクタ
	DecalSystem() noexcept = default;

	/// @brief デカルシステムを初期化する
	/// @param device D3D11デバイス
	/// @param config デカル設定
	void init(ID3D11Device* device, const DecalConfig& config = {})
	{
		if (!device)
		{
			throw std::runtime_error("DecalSystem: device is null");
		}

		m_device = device;
		m_config = config;

		compileShaders();
		createConstantBuffer();
		createSampler();
		createUnitCubeVertexBuffer();
		createBlendState();

		m_initialized = true;
	}

	/// @brief 初期化済みかどうかを取得する
	[[nodiscard]] bool isInitialized() const noexcept
	{
		return m_initialized;
	}

	/// @brief デカルを追加する
	/// @param decal デカルデータ
	/// @return 割り当てられたデカルID
	[[nodiscard]] DecalId addDecal(const Decal& decal)
	{
		if (static_cast<int>(m_decals.size()) >= m_config.maxDecals)
		{
			// 最も古いデカルを削除する
			m_decals.pop_front();
		}

		const DecalId id = m_nextId++;
		DecalInstance instance;
		instance.id = id;
		instance.data = decal;
		instance.lifetime = decal.fadeTime;
		instance.maxLifetime = decal.fadeTime;
		m_decals.push_back(std::move(instance));

		return id;
	}

	/// @brief デカルを削除する
	/// @param id 削除するデカルのID
	void removeDecal(DecalId id)
	{
		m_decals.erase(
			std::remove_if(m_decals.begin(), m_decals.end(),
				[id](const DecalInstance& inst) { return inst.id == id; }),
			m_decals.end());
	}

	/// @brief デカルの状態を更新する
	/// @param dt 前フレームからの経過時間（秒）
	void update(float dt)
	{
		for (auto& inst : m_decals)
		{
			if (inst.lifetime > 0.0f)
			{
				inst.lifetime -= dt;
			}
		}

		// 期限切れのデカルを削除する
		const float fadeDuration = m_config.fadeDuration;
		m_decals.erase(
			std::remove_if(m_decals.begin(), m_decals.end(),
				[fadeDuration](const DecalInstance& inst)
				{
					return inst.lifetime <= -fadeDuration;
				}),
			m_decals.end());
	}

	/// @brief 現在のデカル数を取得する
	[[nodiscard]] int decalCount() const noexcept
	{
		return static_cast<int>(m_decals.size());
	}

	/// @brief デカルを描画する
	/// @param context D3D11デバイスコンテキスト
	/// @param depthSRV 深度バッファのSRV
	/// @param normalSRV 法線バッファのSRV
	/// @param viewProj ビュー射影行列（16 float, row-major）
	/// @param invViewProj 逆ビュー射影行列（16 float, row-major）
	void render(ID3D11DeviceContext* context,
	            ID3D11ShaderResourceView* depthSRV,
	            ID3D11ShaderResourceView* normalSRV,
	            const float viewProj[16],
	            const float invViewProj[16])
	{
		if (!m_initialized || !context || !depthSRV || m_decals.empty())
		{
			return;
		}

		// シェーダーとリソースを設定する
		context->VSSetShader(m_decalVS.Get(), nullptr, 0);
		context->PSSetShader(m_decalPS.Get(), nullptr, 0);
		context->IASetInputLayout(m_inputLayout.Get());
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		const UINT stride = sizeof(float) * 3;
		const UINT offset = 0;
		context->IASetVertexBuffers(
			0, 1, m_cubeVB.GetAddressOf(), &stride, &offset);
		context->IASetIndexBuffer(m_cubeIB.Get(), DXGI_FORMAT_R16_UINT, 0);

		// テクスチャスロットを設定する
		ID3D11ShaderResourceView* srvs[] = {
			depthSRV, normalSRV, nullptr, nullptr
		};
		context->PSSetShaderResources(0, 4, srvs);
		context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());

		// ブレンドステートを有効にする
		const float blendFactor[4] = { 0, 0, 0, 0 };
		context->OMSetBlendState(
			m_blendState.Get(), blendFactor, 0xFFFFFFFF);

		// 各デカルを描画する
		for (const auto& inst : m_decals)
		{
			const float fadeFactor = computeFadeFactor(inst);
			if (fadeFactor <= 0.001f)
			{
				continue;
			}

			updateConstantBuffer(context, inst, viewProj, invViewProj, fadeFactor);
			context->VSSetConstantBuffers(0, 1, m_cbBuffer.GetAddressOf());
			context->PSSetConstantBuffers(0, 1, m_cbBuffer.GetAddressOf());

			context->DrawIndexed(36, 0, 0);
		}

		// リソースをアンバインドする
		ID3D11ShaderResourceView* nullSRVs[4] = {};
		context->PSSetShaderResources(0, 4, nullSRVs);
		context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
	}

private:
	/// @brief デカルインスタンス（内部管理用）
	struct DecalInstance
	{
		DecalId id = 0;
		Decal data;
		float lifetime = 0.0f;      ///< 残りライフタイム（秒）
		float maxLifetime = 0.0f;   ///< 初期ライフタイム
	};

	/// @brief デカルトランスフォーム定数バッファ
	struct alignas(16) CbDecalTransform
	{
		float worldViewProj[16];
		float invViewProj[16];
		float decalWorldInv[16];
		float decalColor[4];
		float decalOpacity;
		float _pad[3];
	};

	/// @brief フェードファクターを計算する
	/// @param inst デカルインスタンス
	/// @return フェードファクター [0, 1]
	[[nodiscard]] float computeFadeFactor(const DecalInstance& inst) const noexcept
	{
		if (inst.lifetime > 0.0f)
		{
			return inst.data.opacity;
		}

		// フェード中
		const float elapsed = -inst.lifetime;
		const float t = std::clamp(
			1.0f - elapsed / m_config.fadeDuration, 0.0f, 1.0f);
		return inst.data.opacity * t;
	}

	/// @brief クォータニオンから回転行列を構築する
	/// @param q クォータニオン (x, y, z, w)
	/// @param out 出力行列（4x4, row-major, 回転のみ）
	static void quaternionToMatrix(const float q[4], float out[16]) noexcept
	{
		const float x = q[0], y = q[1], z = q[2], w = q[3];
		const float x2 = x + x, y2 = y + y, z2 = z + z;
		const float xx = x * x2, xy = x * y2, xz = x * z2;
		const float yy = y * y2, yz = y * z2, zz = z * z2;
		const float wx = w * x2, wy = w * y2, wz = w * z2;

		out[0]  = 1.0f - (yy + zz);
		out[1]  = xy + wz;
		out[2]  = xz - wy;
		out[3]  = 0.0f;
		out[4]  = xy - wz;
		out[5]  = 1.0f - (xx + zz);
		out[6]  = yz + wx;
		out[7]  = 0.0f;
		out[8]  = xz + wy;
		out[9]  = yz - wx;
		out[10] = 1.0f - (xx + yy);
		out[11] = 0.0f;
		out[12] = 0.0f;
		out[13] = 0.0f;
		out[14] = 0.0f;
		out[15] = 1.0f;
	}

	/// @brief 4x4行列の積を計算する（row-major）
	static void matMul4x4(const float a[16], const float b[16], float out[16]) noexcept
	{
		for (int r = 0; r < 4; ++r)
		{
			for (int c = 0; c < 4; ++c)
			{
				out[r * 4 + c] =
					a[r * 4 + 0] * b[0 * 4 + c] +
					a[r * 4 + 1] * b[1 * 4 + c] +
					a[r * 4 + 2] * b[2 * 4 + c] +
					a[r * 4 + 3] * b[3 * 4 + c];
			}
		}
	}

	/// @brief 4x4アフィン行列の逆行列を計算する（非一様スケール対応）
	/// @details 各行のスケール成分で正規化してから転置することで、
	///          Scale * Rotation + Translation の逆行列を正しく求める。
	static void invertAffine(const float m[16], float out[16]) noexcept
	{
		// 各行のスケール二乗値を計算する
		const float sx2 = m[0]*m[0] + m[1]*m[1] + m[2]*m[2];
		const float sy2 = m[4]*m[4] + m[5]*m[5] + m[6]*m[6];
		const float sz2 = m[8]*m[8] + m[9]*m[9] + m[10]*m[10];

		// ゼロスケール対策
		const float isx2 = (sx2 > 1e-12f) ? (1.0f / sx2) : 0.0f;
		const float isy2 = (sy2 > 1e-12f) ? (1.0f / sy2) : 0.0f;
		const float isz2 = (sz2 > 1e-12f) ? (1.0f / sz2) : 0.0f;

		// (S*R)^-1 = R^T * S^-1 = transpose(row/scale^2)
		out[0]  = m[0]  * isx2; out[1]  = m[4]  * isy2; out[2]  = m[8]  * isz2; out[3]  = 0;
		out[4]  = m[1]  * isx2; out[5]  = m[5]  * isy2; out[6]  = m[9]  * isz2; out[7]  = 0;
		out[8]  = m[2]  * isx2; out[9]  = m[6]  * isy2; out[10] = m[10] * isz2; out[11] = 0;

		// 平行移動部分
		out[12] = -(out[0] * m[12] + out[4] * m[13] + out[8]  * m[14]);
		out[13] = -(out[1] * m[12] + out[5] * m[13] + out[9]  * m[14]);
		out[14] = -(out[2] * m[12] + out[6] * m[13] + out[10] * m[14]);
		out[15] = 1.0f;
	}

	/// @brief デカルのワールド行列を構築する
	/// @param inst デカルインスタンス
	/// @param worldMat 出力ワールド行列 (4x4 row-major)
	void buildDecalWorldMatrix(const DecalInstance& inst,
	                           float worldMat[16]) const noexcept
	{
		// 回転行列
		float rotMat[16];
		quaternionToMatrix(inst.data.rotation, rotMat);

		// スケール行列
		float scaleMat[16] = {};
		scaleMat[0]  = inst.data.scale[0];
		scaleMat[5]  = inst.data.scale[1];
		scaleMat[10] = inst.data.scale[2];
		scaleMat[15] = 1.0f;

		// スケール * 回転
		float sr[16];
		matMul4x4(scaleMat, rotMat, sr);

		// 平行移動を追加する
		std::memcpy(worldMat, sr, sizeof(float) * 16);
		worldMat[12] = inst.data.position[0];
		worldMat[13] = inst.data.position[1];
		worldMat[14] = inst.data.position[2];
	}

	/// @brief 行列乗算（row-major 4x4: C = A * B）
	static void mulMatrix4x4(const float a[16], const float b[16], float out[16])
	{
		for (int r = 0; r < 4; ++r)
		{
			for (int c = 0; c < 4; ++c)
			{
				float sum = 0.0f;
				for (int k = 0; k < 4; ++k)
				{
					sum += a[r * 4 + k] * b[k * 4 + c];
				}
				out[r * 4 + c] = sum;
			}
		}
	}

	/// @brief 定数バッファを更新する
	void updateConstantBuffer(ID3D11DeviceContext* context,
	                          const DecalInstance& inst,
	                          const float viewProj[16],
	                          const float invViewProj[16],
	                          float fadeFactor)
	{
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		HRESULT hr = context->Map(
			m_cbBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (FAILED(hr))
		{
			return;
		}

		float decalWorld[16];
		buildDecalWorldMatrix(inst, decalWorld);

		float decalWorldInv[16];
		invertAffine(decalWorld, decalWorldInv);

		/// WVP = decalWorld * viewProj
		float wvp[16];
		mulMatrix4x4(decalWorld, viewProj, wvp);

		CbDecalTransform cb = {};
		std::memcpy(cb.worldViewProj, wvp, sizeof(float) * 16);
		std::memcpy(cb.invViewProj, invViewProj, sizeof(float) * 16);
		std::memcpy(cb.decalWorldInv, decalWorldInv, sizeof(float) * 16);
		std::memcpy(cb.decalColor, inst.data.color, sizeof(float) * 4);
		cb.decalOpacity = fadeFactor;

		std::memcpy(mapped.pData, &cb, sizeof(cb));
		context->Unmap(m_cbBuffer.Get(), 0);
	}

	/// @brief シェーダーをコンパイルする
	void compileShaders()
	{
		// 頂点シェーダー
		ComPtr<ID3DBlob> vsBlob;
		{
			ComPtr<ID3DBlob> errorBlob;
			const std::string vsSource(DECAL_VS);
			HRESULT hr = D3DCompile(
				vsSource.data(), vsSource.size(),
				"DecalVS", nullptr, nullptr,
				"VSMain", "vs_5_0", 0, 0,
				vsBlob.GetAddressOf(), errorBlob.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error("DecalSystem: VS compile failed");
			}
			hr = m_device->CreateVertexShader(
				vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
				nullptr, m_decalVS.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error("DecalSystem: CreateVertexShader failed");
			}
		}

		// 入力レイアウト
		{
			D3D11_INPUT_ELEMENT_DESC layout[] = {
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
				  D3D11_INPUT_PER_VERTEX_DATA, 0 },
			};
			HRESULT hr = m_device->CreateInputLayout(
				layout, 1,
				vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
				m_inputLayout.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error("DecalSystem: CreateInputLayout failed");
			}
		}

		// ピクセルシェーダー
		{
			ComPtr<ID3DBlob> blob;
			ComPtr<ID3DBlob> errorBlob;
			const std::string psSource(DECAL_PS);
			HRESULT hr = D3DCompile(
				psSource.data(), psSource.size(),
				"DecalPS", nullptr, nullptr,
				"PSMain", "ps_5_0", 0, 0,
				blob.GetAddressOf(), errorBlob.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error("DecalSystem: PS compile failed");
			}
			hr = m_device->CreatePixelShader(
				blob->GetBufferPointer(), blob->GetBufferSize(),
				nullptr, m_decalPS.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error("DecalSystem: CreatePixelShader failed");
			}
		}
	}

	/// @brief 定数バッファを作成する
	void createConstantBuffer()
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(CbDecalTransform);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		HRESULT hr = m_device->CreateBuffer(
			&desc, nullptr, m_cbBuffer.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error("DecalSystem: CreateBuffer failed");
		}
	}

	/// @brief リニアクランプサンプラーを作成する
	void createSampler()
	{
		D3D11_SAMPLER_DESC desc = {};
		desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

		HRESULT hr = m_device->CreateSamplerState(
			&desc, m_sampler.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error("DecalSystem: CreateSamplerState failed");
		}
	}

	/// @brief ユニットキューブの頂点バッファとインデックスバッファを作成する
	void createUnitCubeVertexBuffer()
	{
		// [-0.5, 0.5]^3 のユニットキューブ
		constexpr float vertices[] = {
			-0.5f, -0.5f, -0.5f,
			 0.5f, -0.5f, -0.5f,
			 0.5f,  0.5f, -0.5f,
			-0.5f,  0.5f, -0.5f,
			-0.5f, -0.5f,  0.5f,
			 0.5f, -0.5f,  0.5f,
			 0.5f,  0.5f,  0.5f,
			-0.5f,  0.5f,  0.5f,
		};

		constexpr std::uint16_t indices[] = {
			// 前面
			0, 2, 1,  0, 3, 2,
			// 背面
			4, 5, 6,  4, 6, 7,
			// 左面
			0, 4, 7,  0, 7, 3,
			// 右面
			1, 2, 6,  1, 6, 5,
			// 上面
			3, 7, 6,  3, 6, 2,
			// 下面
			0, 1, 5,  0, 5, 4,
		};

		// 頂点バッファ
		{
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth = sizeof(vertices);
			desc.Usage = D3D11_USAGE_IMMUTABLE;
			desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

			D3D11_SUBRESOURCE_DATA initData = {};
			initData.pSysMem = vertices;

			HRESULT hr = m_device->CreateBuffer(
				&desc, &initData, m_cubeVB.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"DecalSystem: CreateBuffer (VB) failed");
			}
		}

		// インデックスバッファ
		{
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth = sizeof(indices);
			desc.Usage = D3D11_USAGE_IMMUTABLE;
			desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

			D3D11_SUBRESOURCE_DATA initData = {};
			initData.pSysMem = indices;

			HRESULT hr = m_device->CreateBuffer(
				&desc, &initData, m_cubeIB.GetAddressOf());
			if (FAILED(hr))
			{
				throw std::runtime_error(
					"DecalSystem: CreateBuffer (IB) failed");
			}
		}
	}

	/// @brief デカル用ブレンドステートを作成する
	void createBlendState()
	{
		D3D11_BLEND_DESC desc = {};
		desc.RenderTarget[0].BlendEnable = TRUE;
		desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		desc.RenderTarget[0].RenderTargetWriteMask =
			D3D11_COLOR_WRITE_ENABLE_ALL;

		HRESULT hr = m_device->CreateBlendState(
			&desc, m_blendState.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"DecalSystem: CreateBlendState failed");
		}
	}

	ComPtr<ID3D11Device> m_device;
	bool m_initialized = false;
	DecalConfig m_config;
	DecalId m_nextId = 1;

	std::deque<DecalInstance> m_decals;

	ComPtr<ID3D11VertexShader> m_decalVS;
	ComPtr<ID3D11PixelShader> m_decalPS;
	ComPtr<ID3D11InputLayout> m_inputLayout;
	ComPtr<ID3D11Buffer> m_cbBuffer;
	ComPtr<ID3D11Buffer> m_cubeVB;
	ComPtr<ID3D11Buffer> m_cubeIB;
	ComPtr<ID3D11SamplerState> m_sampler;
	ComPtr<ID3D11BlendState> m_blendState;
};

} // namespace mitiru::render

#endif // _WIN32
