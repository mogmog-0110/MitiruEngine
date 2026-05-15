#pragma once

/// @file PBRMaterial.hpp
/// @brief 物理ベースレンダリング（PBR）マテリアルシステム
/// @details Cook-Torrance BRDFによるPBRマテリアル。
///          GGX NDF、Schlick Fresnel、Smith GGX Geometry関数を使用する。
///          テクスチャマップが未設定の場合はPhongシェーディングにフォールバックする。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/math/Vec4.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/debug/Log.hpp>

#ifdef _WIN32
#include <d3d11.h>
#include <wrl/client.h>
#endif

#include <mitiru/render/PBRShaders.hpp>

namespace mitiru::render
{

/// @brief PBRマテリアル定義
/// @details メタリック-ラフネスワークフローのPBRパラメータ。
///
/// @code
/// mitiru::render::PBRMaterial mat;
/// mat.albedoColor = {1.0f, 0.2f, 0.2f, 1.0f};
/// mat.metallic = 0.8f;
/// mat.roughness = 0.3f;
/// @endcode
struct PBRMaterial
{
	sgc::Colorf albedoColor{0.8f, 0.8f, 0.8f, 1.0f}; ///< アルベド色
	float metallic = 0.0f;                              ///< メタリック [0,1]
	float roughness = 0.5f;                             ///< ラフネス [0,1]
	float ao = 1.0f;                                    ///< アンビエントオクルージョン [0,1]
	sgc::Colorf emissive{0.0f, 0.0f, 0.0f, 1.0f};     ///< 発光色

	std::string albedoMapKey;               ///< アルベドテクスチャキー（空=なし）
	std::string normalMapKey;               ///< 法線マップキー（空=なし）
	std::string metallicRoughnessMapKey;    ///< メタリック-ラフネスマップキー（空=なし）
	std::string aoMapKey;                   ///< AOマップキー（空=なし）
	std::string emissiveMapKey;             ///< 発光マップキー（空=なし）

	/// @brief テクスチャマップが1つでも設定されているかを返す
	[[nodiscard]] bool hasAnyMap() const noexcept
	{
		return !albedoMapKey.empty()
			|| !normalMapKey.empty()
			|| !metallicRoughnessMapKey.empty()
			|| !aoMapKey.empty()
			|| !emissiveMapKey.empty();
	}

	/// @brief デフォルトPBRマテリアルを作成する
	[[nodiscard]] static PBRMaterial defaultMaterial() noexcept
	{
		return PBRMaterial{};
	}

	/// @brief 金属マテリアルのプリセット
	[[nodiscard]] static PBRMaterial metal(const sgc::Colorf& color,
	                                       float rough = 0.3f) noexcept
	{
		PBRMaterial mat;
		mat.albedoColor = color;
		mat.metallic = 1.0f;
		mat.roughness = rough;
		return mat;
	}

	/// @brief 非金属マテリアルのプリセット
	[[nodiscard]] static PBRMaterial dielectric(const sgc::Colorf& color,
	                                            float rough = 0.5f) noexcept
	{
		PBRMaterial mat;
		mat.albedoColor = color;
		mat.metallic = 0.0f;
		mat.roughness = rough;
		return mat;
	}
};

/// @brief PBR環境マップ設定
/// @details IBL（Image-Based Lighting）に必要なキューブマップとBRDF LUTの参照。
struct PBREnvironment
{
	std::string irradianceMapKey;    ///< イラディアンスマップキー
	std::string prefilteredMapKey;   ///< プリフィルタード環境マップキー
	std::string brdfLutKey;          ///< BRDF LUTテクスチャキー
	float ambientIntensity = 1.0f;   ///< 環境光強度

	/// @brief IBLが有効かどうかを返す
	[[nodiscard]] bool isValid() const noexcept
	{
		return !irradianceMapKey.empty()
			&& !prefilteredMapKey.empty()
			&& !brdfLutKey.empty();
	}
};

/// @brief PBRシェーダー定数バッファ構造体
/// @details HLSL cbuffer にマッピングされるパック構造体。
///          16バイトアライメントに従う。
struct alignas(16) PBRShaderConstants
{
	/// 変換行列（各64バイト）
	float world[16]{};        ///< ワールド行列
	float view[16]{};         ///< ビュー行列
	float proj[16]{};         ///< プロジェクション行列

	/// カメラ・ライト（各16バイト）
	float cameraPos[4]{};     ///< カメラ位置 (x, y, z, padding)
	float lightDir[4]{};      ///< ライト方向 (x, y, z, padding)
	float lightColor[4]{};    ///< ライト色 (r, g, b, intensity)

	/// マテリアルパラメータ（16バイト）
	float albedo[4]{};        ///< アルベド色 (r, g, b, a)
	float metallicRoughness[4]{}; ///< (metallic, roughness, ao, padding)
	float emissive[4]{};      ///< 発光色 (r, g, b, padding)

	/// フラグ（16バイト）
	int hasAlbedoMap = 0;     ///< アルベドマップ有効フラグ
	int hasNormalMap = 0;     ///< 法線マップ有効フラグ
	int hasMetallicRoughnessMap = 0; ///< メタリック-ラフネスマップ有効フラグ
	int hasAoMap = 0;         ///< AOマップ有効フラグ

	/// @brief PBRMaterialからシェーダー定数を設定する
	void fromMaterial(const PBRMaterial& mat) noexcept
	{
		albedo[0] = mat.albedoColor.r;
		albedo[1] = mat.albedoColor.g;
		albedo[2] = mat.albedoColor.b;
		albedo[3] = mat.albedoColor.a;

		metallicRoughness[0] = mat.metallic;
		metallicRoughness[1] = mat.roughness;
		metallicRoughness[2] = mat.ao;
		metallicRoughness[3] = 0.0f;

		emissive[0] = mat.emissive.r;
		emissive[1] = mat.emissive.g;
		emissive[2] = mat.emissive.b;
		emissive[3] = 0.0f;

		hasAlbedoMap = mat.albedoMapKey.empty() ? 0 : 1;
		hasNormalMap = mat.normalMapKey.empty() ? 0 : 1;
		hasMetallicRoughnessMap = mat.metallicRoughnessMapKey.empty() ? 0 : 1;
		hasAoMap = mat.aoMapKey.empty() ? 0 : 1;
	}

	/// @brief 変換行列を設定する
	void setTransforms(const sgc::Mat4f& w,
	                   const sgc::Mat4f& v,
	                   const sgc::Mat4f& p) noexcept
	{
		std::copy(w.data(), w.data() + 16, world);
		std::copy(v.data(), v.data() + 16, view);
		std::copy(p.data(), p.data() + 16, proj);
	}

	/// @brief カメラ位置を設定する
	void setCameraPos(const sgc::Vec3f& pos) noexcept
	{
		cameraPos[0] = pos.x;
		cameraPos[1] = pos.y;
		cameraPos[2] = pos.z;
		cameraPos[3] = 0.0f;
	}

	/// @brief ライト方向と色を設定する
	void setLight(const sgc::Vec3f& dir, const sgc::Colorf& color,
	              float intensity) noexcept
	{
		lightDir[0] = dir.x;
		lightDir[1] = dir.y;
		lightDir[2] = dir.z;
		lightDir[3] = 0.0f;

		lightColor[0] = color.r;
		lightColor[1] = color.g;
		lightColor[2] = color.b;
		lightColor[3] = intensity;
	}
};

/// @brief 埋め込みHLSL: PBRピクセルシェーダー（Cook-Torrance BRDF）
/// @deprecated PBRShaders.hpp の kPBR_PS を使用すること。
///             この定数は後方互換性のために残されている。
/// @see kPBR_PS, kPBR_IBL_PS, kPBR_SHADOW_PS
inline constexpr const char* kPBRPixelShaderHLSL = nullptr;

/// @brief 埋め込みHLSL: Phongフォールバックピクセルシェーダー
/// @deprecated PBRShaders.hpp の kPBR_PS を使用すること。
///             PBRパイプラインではPhongフォールバックは不要。
/// @see kPBR_PS
inline constexpr const char* kPhongFallbackPixelShaderHLSL = nullptr;

/// @brief PBRレンダラー
/// @details DX11デバイスを使用してPBRマテリアルでメッシュを描画する。
///          テクスチャマップが未設定の場合はPhongシェーディングにフォールバックする。
///
/// @code
/// mitiru::render::PBRRenderer pbr;
/// pbr.init(dx11Device);
/// pbr.setMaterial(goldMaterial);
/// pbr.setEnvironment(skyEnv);
/// pbr.drawMesh(mesh, worldMatrix, camera, light);
/// @endcode
class PBRRenderer
{
public:
	/// @brief デフォルトコンストラクタ
	PBRRenderer() noexcept = default;

#ifdef _WIN32
	/// @brief DX11デバイスで初期化する
	/// @param device D3D11デバイスポインタ
	/// @return 初期化成功ならtrue
	bool init(ID3D11Device* device)
	{
		if (!device)
		{
			MITIRU_LOG_ERROR("PBRRenderer", "init: null device");
			return false;
		}
		m_device = device;
		m_device->GetImmediateContext(&m_context);

		if (!createConstantBuffer())
		{
			return false;
		}

		if (!initShaders(device))
		{
			MITIRU_LOG_ERROR("PBRRenderer",
				"shader compilation failed, renderer will not draw");
		}

		m_initialized = true;
		MITIRU_LOG_INFO("PBRRenderer", "initialized");
		return true;
	}

	/// @brief PBRシェーダーセットをコンパイルする
	/// @param device D3D11デバイスポインタ
	/// @return コンパイル成功ならtrue
	bool initShaders(ID3D11Device* device)
	{
		m_shaders = CompilePBRShaders(device);
		return m_shaders.valid;
	}

	/// @brief コンパイル済みシェーダーセットを取得する
	[[nodiscard]] const PBRShaderSet& shaders() const noexcept
	{
		return m_shaders;
	}
#endif

	/// @brief 初期化済みかどうかを返す
	[[nodiscard]] bool isInitialized() const noexcept
	{
		return m_initialized;
	}

	/// @brief 現在のマテリアルを設定する
	/// @param material PBRマテリアル
	void setMaterial(const PBRMaterial& material) noexcept
	{
		m_currentMaterial = material;
	}

	/// @brief 現在のマテリアルを取得する
	[[nodiscard]] const PBRMaterial& material() const noexcept
	{
		return m_currentMaterial;
	}

	/// @brief 環境マップを設定する
	/// @param env PBR環境設定
	void setEnvironment(const PBREnvironment& env) noexcept
	{
		m_environment = env;
	}

	/// @brief 環境マップを取得する
	[[nodiscard]] const PBREnvironment& environment() const noexcept
	{
		return m_environment;
	}

	/// @brief マテリアルがPBRパスを使用するかPhongフォールバックかを判定する
	/// @param mat マテリアル
	/// @return PBRパスを使用すべきならtrue
	[[nodiscard]] static bool shouldUsePBR(const PBRMaterial& mat) noexcept
	{
		return mat.hasAnyMap()
			|| mat.metallic > 0.01f
			|| mat.roughness < 0.99f;
	}

#ifdef _WIN32
	/// @brief メッシュを描画する
	/// @param worldMatrix ワールド行列
	/// @param viewMatrix ビュー行列
	/// @param projMatrix プロジェクション行列
	/// @param cameraPos カメラ位置
	/// @param lightDir ライト方向
	/// @param lightColor ライト色
	/// @param lightIntensity ライト強度
	void drawMesh(const sgc::Mat4f& worldMatrix,
	              const sgc::Mat4f& viewMatrix,
	              const sgc::Mat4f& projMatrix,
	              const sgc::Vec3f& cameraPos,
	              const sgc::Vec3f& lightDir,
	              const sgc::Colorf& lightColor,
	              float lightIntensity = 1.0f)
	{
		if (!m_initialized || !m_context)
		{
			return;
		}

		/// 定数バッファを更新する
		PBRShaderConstants constants;
		constants.setTransforms(worldMatrix, viewMatrix, projMatrix);
		constants.setCameraPos(cameraPos);
		constants.setLight(lightDir, lightColor, lightIntensity);
		constants.fromMaterial(m_currentMaterial);

		updateConstantBuffer(constants);

		/// 定数バッファをピクセルシェーダーにバインドする
		ID3D11Buffer* cbuf = m_constantBuffer.Get();
		m_context->PSSetConstantBuffers(0, 1, &cbuf);
	}
#endif

	/// @brief CPU上でPBRライティングを計算する（ソフトウェアレンダリング用）
	/// @param worldPos ワールド空間位置
	/// @param normal 法線（正規化済み）
	/// @param cameraPos カメラ位置
	/// @param lightDir ライト方向
	/// @param lightColor ライト色
	/// @param lightIntensity ライト強度
	/// @return 計算されたピクセル色
	[[nodiscard]] sgc::Colorf computeLighting(
		const sgc::Vec3f& worldPos,
		const sgc::Vec3f& normal,
		const sgc::Vec3f& cameraPos,
		const sgc::Vec3f& lightDir,
		const sgc::Colorf& lightColor,
		float lightIntensity = 1.0f) const noexcept
	{
		if (!shouldUsePBR(m_currentMaterial))
		{
			return computePhongFallback(
				worldPos, normal, cameraPos, lightDir,
				lightColor, lightIntensity);
		}

		const sgc::Vec3f N = normal;
		const sgc::Vec3f V = (cameraPos - worldPos).normalized();
		const sgc::Vec3f L = (lightDir * -1.0f).normalized();
		const sgc::Vec3f H = (V + L).normalized();

		const float NdotL = std::max(0.0f, N.dot(L));
		const float NdotV = std::max(0.0f, N.dot(V));
		const float NdotH = std::max(0.0f, N.dot(H));
		const float HdotV = std::max(0.0f, H.dot(V));

		const float met = m_currentMaterial.metallic;
		const float rough = m_currentMaterial.roughness;

		/// F0: 非金属は0.04、金属はアルベド色
		const float f0r = 0.04f * (1.0f - met) + m_currentMaterial.albedoColor.r * met;
		const float f0g = 0.04f * (1.0f - met) + m_currentMaterial.albedoColor.g * met;
		const float f0b = 0.04f * (1.0f - met) + m_currentMaterial.albedoColor.b * met;

		/// GGX NDF
		const float D = distributionGGX(NdotH, rough);

		/// Smith GGX Geometry
		const float G = geometrySmith(NdotV, NdotL, rough);

		/// Schlick Fresnel
		const float fresnelFactor = fresnelSchlickFactor(HdotV);
		const float Fr = f0r + (1.0f - f0r) * fresnelFactor;
		const float Fg = f0g + (1.0f - f0g) * fresnelFactor;
		const float Fb = f0b + (1.0f - f0b) * fresnelFactor;

		/// Cook-Torrance specular
		const float denom = 4.0f * NdotV * NdotL + 0.0001f;
		const float specR = D * G * Fr / denom;
		const float specG = D * G * Fg / denom;
		const float specB = D * G * Fb / denom;

		/// エネルギー保存
		const float kDr = (1.0f - Fr) * (1.0f - met);
		const float kDg = (1.0f - Fg) * (1.0f - met);
		const float kDb = (1.0f - Fb) * (1.0f - met);

		static constexpr float kInvPi = 1.0f / 3.14159265359f;

		const float lr = lightColor.r * lightIntensity;
		const float lg = lightColor.g * lightIntensity;
		const float lb = lightColor.b * lightIntensity;

		/// 直接光成分
		float outR = (kDr * m_currentMaterial.albedoColor.r * kInvPi + specR) * lr * NdotL;
		float outG = (kDg * m_currentMaterial.albedoColor.g * kInvPi + specG) * lg * NdotL;
		float outB = (kDb * m_currentMaterial.albedoColor.b * kInvPi + specB) * lb * NdotL;

		/// アンビエント
		const float aoVal = m_currentMaterial.ao;
		outR += 0.03f * m_currentMaterial.albedoColor.r * aoVal;
		outG += 0.03f * m_currentMaterial.albedoColor.g * aoVal;
		outB += 0.03f * m_currentMaterial.albedoColor.b * aoVal;

		/// 発光
		outR += m_currentMaterial.emissive.r;
		outG += m_currentMaterial.emissive.g;
		outB += m_currentMaterial.emissive.b;

		/// Reinhardトーンマッピング
		outR = outR / (outR + 1.0f);
		outG = outG / (outG + 1.0f);
		outB = outB / (outB + 1.0f);

		/// ガンマ補正
		outR = std::pow(outR, 1.0f / 2.2f);
		outG = std::pow(outG, 1.0f / 2.2f);
		outB = std::pow(outB, 1.0f / 2.2f);

		return sgc::Colorf{
			std::min(1.0f, outR),
			std::min(1.0f, outG),
			std::min(1.0f, outB),
			m_currentMaterial.albedoColor.a
		};
	}

private:
	/// @brief GGX Normal Distribution Function
	[[nodiscard]] static float distributionGGX(float NdotH, float roughness) noexcept
	{
		const float a = roughness * roughness;
		const float a2 = a * a;
		const float NdotH2 = NdotH * NdotH;
		const float denom = NdotH2 * (a2 - 1.0f) + 1.0f;
		static constexpr float kPi = 3.14159265359f;
		return a2 / std::max(kPi * denom * denom, 0.0000001f);
	}

	/// @brief Schlick-GGX Geometry Function (片方向)
	[[nodiscard]] static float geometrySchlickGGX(float NdotV, float roughness) noexcept
	{
		const float r = roughness + 1.0f;
		const float k = (r * r) / 8.0f;
		return NdotV / (NdotV * (1.0f - k) + k);
	}

	/// @brief Smith's Geometry Function (両方向合成)
	[[nodiscard]] static float geometrySmith(float NdotV, float NdotL,
	                                         float roughness) noexcept
	{
		return geometrySchlickGGX(NdotV, roughness)
			 * geometrySchlickGGX(NdotL, roughness);
	}

	/// @brief Schlick Fresnel の (1-cosTheta)^5 部分
	[[nodiscard]] static float fresnelSchlickFactor(float cosTheta) noexcept
	{
		const float t = std::max(0.0f, std::min(1.0f, 1.0f - cosTheta));
		const float t2 = t * t;
		return t2 * t2 * t; // t^5
	}

	/// @brief Phongフォールバックライティング計算
	[[nodiscard]] sgc::Colorf computePhongFallback(
		const sgc::Vec3f& worldPos,
		const sgc::Vec3f& normal,
		const sgc::Vec3f& cameraPos,
		const sgc::Vec3f& lightDir,
		const sgc::Colorf& lightColor,
		float lightIntensity) const noexcept
	{
		const sgc::Vec3f N = normal;
		const sgc::Vec3f L = (lightDir * -1.0f).normalized();
		const sgc::Vec3f V = (cameraPos - worldPos).normalized();

		/// 反射ベクトル: R = 2(N dot L)N - L
		const float nDotL = std::max(0.0f, N.dot(L));
		const sgc::Vec3f R = N * (2.0f * nDotL) - L;

		const float diff = nDotL;
		const float spec = std::pow(std::max(0.0f, R.dot(V)), 32.0f);

		const float lr = lightColor.r * lightIntensity;
		const float lg = lightColor.g * lightIntensity;
		const float lb = lightColor.b * lightIntensity;

		const float ar = m_currentMaterial.albedoColor.r;
		const float ag = m_currentMaterial.albedoColor.g;
		const float ab = m_currentMaterial.albedoColor.b;

		return sgc::Colorf{
			std::min(1.0f, 0.15f * ar + diff * ar * lr + spec * lr * 0.5f),
			std::min(1.0f, 0.15f * ag + diff * ag * lg + spec * lg * 0.5f),
			std::min(1.0f, 0.15f * ab + diff * ab * lb + spec * lb * 0.5f),
			m_currentMaterial.albedoColor.a
		};
	}

#ifdef _WIN32
	/// @brief 定数バッファを作成する
	bool createConstantBuffer()
	{
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = sizeof(PBRShaderConstants);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		const HRESULT hr = m_device->CreateBuffer(
			&desc, nullptr, m_constantBuffer.GetAddressOf());

		if (FAILED(hr))
		{
			MITIRU_LOG_ERROR("PBRRenderer", "failed to create constant buffer");
			return false;
		}
		return true;
	}

	/// @brief 定数バッファを更新する
	void updateConstantBuffer(const PBRShaderConstants& data)
	{
		D3D11_MAPPED_SUBRESOURCE mapped{};
		const HRESULT hr = m_context->Map(
			m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (SUCCEEDED(hr))
		{
			std::memcpy(mapped.pData, &data, sizeof(data));
			m_context->Unmap(m_constantBuffer.Get(), 0);
		}
	}

	Microsoft::WRL::ComPtr<ID3D11Device> m_device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
	PBRShaderSet m_shaders;
#endif

	PBRMaterial m_currentMaterial;
	PBREnvironment m_environment;
	bool m_initialized = false;
};

} // namespace mitiru::render
