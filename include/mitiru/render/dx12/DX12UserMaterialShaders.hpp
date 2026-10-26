#pragma once

/// @file DX12UserMaterialShaders.hpp
/// @brief 自作の材質 (ADR 0100) の DX12 PS を、既定の陰影の PS から組み立てる
/// @details 既定の PS の文字列 (DX12LitShaders.hpp) は変更しない。材質付きの PS は、同じ文字列の決まった箇所を patchOnce で置き換えて作る。置き換える箇所が見つからないと例外になり、tests/mitiru/TestUserMaterial.cpp が失敗する。
/// material() は、基本色、法線マップ、金属と粗さを読んだ直後、デカールと照明の前に呼ばれる。
/// 法線 RT の a に入れる描画マーカー (packOutput) は元のまま使い、利用者の法線は輪郭線の下地へ出さない。

#include <stdexcept>
#include <string>
#include <string_view>

#include <mitiru/render/UserMaterial.hpp>
#include <mitiru/render/dx12/DX12LitShaders.hpp>

namespace mitiru::render
{

namespace dx12_user_material
{

/// anchor が text にちょうど 1 回あるときだけ replacement に置き換える。無い、または複数あれば例外
inline void patchOnce(std::string& text, std::string_view anchor, std::string_view replacement)
{
	const std::size_t first = text.find(anchor);
	if (first == std::string::npos || text.find(anchor, first + anchor.size()) != std::string::npos)
	{
		throw std::runtime_error("DX12 の材質: 既定の PS の置き換え箇所が 1 つに決まりません: " + std::string(anchor.substr(0, 60)));
	}
	text.replace(first, anchor.size(), replacement);
}

/// anchor を全部 open + anchor + ")" にする。1 つも無ければ例外
inline void wrapAll(std::string& text, std::string_view anchor, std::string_view open)
{
	std::size_t pos = 0;
	int count = 0;
	while ((pos = text.find(anchor, pos)) != std::string::npos)
	{
		const std::string wrapped = std::string(open) + std::string(anchor) + ")";
		text.replace(pos, anchor.size(), wrapped);
		pos += wrapped.size();
		++count;
	}
	if (count == 0) { throw std::runtime_error("DX12 の材質: 既定の PS に alpha の式がありません: " + std::string(anchor)); }
}

/// CbDrawEx の末尾に材質の値を足す。cbuffer は 512 byte 確保済みのため、元の読み位置は動かない
inline constexpr const char* kCommonAnchor = "    float4 LayerSpare;\n};";
inline constexpr const char* kCommonReplacement = R"hlsl(    float4 LayerSpare;
    float4 UserParams[4];   // 材質の float。param_<name> が指す
    float4 UserInfo;        // x=秒
};

static float  g_userAlpha = -1.0;
static float3 g_userEmissive = float3(0.0, 0.0, 0.0);
float userAlpha(float a) { return g_userAlpha >= 0.0 ? g_userAlpha : a; }
float4 mitiruUserVec(int i) { return UserParams[i]; }
)hlsl";

inline constexpr const char* kGlueCore = R"hlsl(
void runUserMaterial(PSInput input, float3 Ng, float3 V, inout SurfaceOut so)
{
    SurfaceIn si;
    si.uv = input.TexCoord;
    si.worldPos = input.WorldPos;
    si.normal = Ng;
    si.view = V;
    si.vertexColor = input.Color;
    si.time = UserInfo.x;
    si.pixel = input.Position.xy;
    material(si, so);
    if (so.discardPixel) { discard; }
    g_userAlpha = so.alpha;
    g_userEmissive = so.emissive;
}
)hlsl";

/// トゥーン、Phong、照明なし用。albedo、N、alpha、mr (metallic、roughness、ao) は、呼ぶ前の値が入り、呼んだ後の値に置き換わる。
/// PBR の層は 0 で渡し、戻りは読まない
inline constexpr const char* kGlue = R"hlsl(
void applyUserSurface(PSInput input, float3 Ng, float3 V, inout float3 albedo, inout float3 N, inout float alpha, inout float3 mr)
{
    SurfaceOut so;
    so.baseColor = albedo;
    so.alpha = alpha;
    so.normal = N;
    so.emissive = float3(0.0, 0.0, 0.0);
    so.roughness = mr.y;
    so.metallic = mr.x;
    so.ao = mr.z;
    so.discardPixel = false;
    so.clearCoat = 0.0;
    so.clearCoatRoughness = 0.0;
    so.anisotropy = 0.0;
    so.anisotropyRotation = 0.0;
    so.sheenColor = float3(0.0, 0.0, 0.0);
    so.sheenRoughness = 0.0;
    runUserMaterial(input, Ng, V, so);
    albedo = so.baseColor;
    if (any(so.normal != N)) { N = normalize(so.normal); }   // 変えなければ値を保つ。再正規化の丸めで段の境がずれない
    alpha = so.alpha;
    mr = float3(so.metallic, so.roughness, so.ao);
}
)hlsl";

inline constexpr const char* kGluePbr = R"hlsl(
void applyUserSurfacePbr(PSInput input, float3 Ng, float3 V, inout Surface s, inout float ao, inout float alpha)
{
    SurfaceOut so;
    so.baseColor = s.albedo;
    so.alpha = alpha;
    so.normal = s.N;
    so.emissive = float3(0.0, 0.0, 0.0);
    so.roughness = s.roughness;
    so.metallic = s.metallic;
    so.ao = ao;
    so.discardPixel = false;
    so.clearCoat = s.coat;
    so.clearCoatRoughness = s.coatRoughness;
    so.anisotropy = s.anisotropy;
    so.anisotropyRotation = s.anisotropyRotation;
    so.sheenColor = s.sheenColor;
    so.sheenRoughness = s.sheenRoughness;
    runUserMaterial(input, Ng, V, so);
    s.albedo = so.baseColor;
    if (any(so.normal != s.N)) { s.N = normalize(so.normal); }
    s.roughness = clamp(so.roughness, 0.04, 1.0);
    s.metallic = saturate(so.metallic);
    ao = so.ao;
    alpha = so.alpha;
    s.coat = saturate(so.clearCoat);
    s.coatRoughness = saturate(so.clearCoatRoughness);
    s.anisotropy = saturate(so.anisotropy);
    s.anisotropyRotation = so.anisotropyRotation;
    s.sheenColor = saturate(so.sheenColor);
    s.sheenRoughness = saturate(so.sheenRoughness);
}
)hlsl";

inline constexpr const char* kUnlitMain = R"hlsl(
PSOutput PSMain(PSInput input)
{
    chooseDrawColors(input);
    clipOccluded(input.Position, input.WorldPos, input.WorldNorm);
    float3 Ng = normalize(input.WorldNorm);
    float3 N = perturbNormal(Ng, input.WorldPos, input.TexCoord);
    float3 V = normalize(CameraPos - input.WorldPos);
    float4 texSample = sampleAlbedo(input.TexCoord);
    float3 albedo = MaterialDiffuse.rgb * input.Color.rgb * texSample.rgb;
    float3 mr = sampleMetalRough(input.TexCoord);
    float alpha = MaterialDiffuse.a * input.Color.a * texSample.a;
    applyUserSurface(input, Ng, V, albedo, N, alpha, mr);
    float3 color = albedo + sampleEmissive(input.TexCoord);
    color = applyFog(color, input.WorldPos);
    return packOutput(color, userAlpha(alpha), Ng, V);
}
)hlsl";

[[nodiscard]] inline std::string patchedCommon()
{
	std::string src = dx12LitCommonHlsl();
	patchOnce(src, kCommonAnchor, kCommonReplacement);
	patchOnce(src, "return e + TintAdd.rgb;", "return e + TintAdd.rgb + g_userEmissive;");
	return src;
}

[[nodiscard]] inline std::string patchedToonMain()
{
	std::string main = DX12_LIT_TOON_MAIN_HLSL;
	patchOnce(main, "    applyDecals(input.Position, input.WorldPos, Ng, albedo, N, mr.y);",
		"    { float ua = MaterialDiffuse.a * input.Color.a * texSample.a; applyUserSurface(input, Ng, V, albedo, N, ua, mr); }\n"
		"    applyDecals(input.Position, input.WorldPos, Ng, albedo, N, mr.y);");
	wrapAll(main, "MaterialDiffuse.a * input.Color.a * texAlpha", "userAlpha(");
	wrapAll(main, "MaterialDiffuse.a * input.Color.a * texSample.a", "userAlpha(");
	return main;
}

[[nodiscard]] inline std::string patchedPhongMain()
{
	std::string main = DX12_LIT_PHONG_MAIN_HLSL;
	patchOnce(main, "    applyDecals(input.Position, input.WorldPos, normalize(input.WorldNorm), albedo, N, mr.y);",
		"    { float ua = MaterialDiffuse.a * input.Color.a * texSample.a; applyUserSurface(input, normalize(input.WorldNorm), V, albedo, N, ua, mr); }\n"
		"    applyDecals(input.Position, input.WorldPos, normalize(input.WorldNorm), albedo, N, mr.y);");
	return main;
}

[[nodiscard]] inline std::string patchedPhongOpaque()
{
	std::string main = DX12_LIT_PHONG_OPAQUE_HLSL;
	wrapAll(main, "MaterialDiffuse.a * input.Color.a * texSample.a", "userAlpha(");
	return main;
}

[[nodiscard]] inline std::string patchedPbrMain()
{
	std::string main = DX12_LIT_PBR_MAIN_HLSL;
	patchOnce(main, "    applyDecals(input.Position, input.WorldPos, Ng, s.albedo, s.N, s.roughness);",
		"    {\n"
		"        float ua = BaseColor.a * input.Color.a * texSample.a;\n"
		"        s.F0 = float3(0.04, 0.04, 0.04);\n"
		"        loadPbrLayers(s, Ng);\n"
		"        applyUserSurfacePbr(input, Ng, V, s, mr.z, ua);\n"
		"    }\n"
		"    applyDecals(input.Position, input.WorldPos, Ng, s.albedo, s.N, s.roughness);");
	patchOnce(main, "    setupPbrLayers(s, Ng, input.WorldPos, input.TexCoord);",
		"    finishPbrLayers(s, input.WorldPos, input.TexCoord);");
	wrapAll(main, "BaseColor.a * input.Color.a * texSample.a", "userAlpha(");
	return main;
}

} // namespace dx12_user_material

/// 材質 src を、陰影 shade (Toon、Phong、Pbr) または照明なし (unlit) の PS に組み込んだ HLSL を返す。
/// 作れない組み合わせ (半透明の PhongOit) は例外
[[nodiscard]] inline std::string dx12UserMaterialPixelShader(LitShade shade, const UserMaterialSource& material)
{
	using namespace dx12_user_material;
	std::string src = patchedCommon();
	// 照明なしの材質は影、デカール、間接光を使わないため、共通部だけで足りる
	if (material.desc.unlit)
	{
		src += assembleUserMaterialCode(material);
		src += kGlueCore;
		src += kGlue;
		src += kUnlitMain;
		return src;
	}
	src += DX12_SUN_SHADOW_HLSL;
	src += DX12_DECAL_APPLY_HLSL;
	src += DX12_LIGHTING_PROBES_HLSL;
	switch (shade)
	{
	case LitShade::Toon:
		src += DX12_TOON_RAMP_HLSL;
		src += DX12_LIT_TOON_SHADE_HLSL;
		src += DX12_LIT_LOCAL_LOOP_HLSL;
		src += assembleUserMaterialCode(material);
		src += kGlueCore;
		src += kGlue;
		src += patchedToonMain();
		break;
	case LitShade::Phong:
		src += DX12_LIT_PHONG_SHADE_HLSL;
		src += DX12_LIT_LOCAL_LOOP_HLSL;
		src += assembleUserMaterialCode(material);
		src += kGlueCore;
		src += kGlue;
		src += patchedPhongMain();
		src += patchedPhongOpaque();
		break;
	case LitShade::Pbr:
		src += DX12_LIT_PBR_SHADE_HLSL;
		src += DX12_LIT_LOCAL_LOOP_HLSL;
		src += assembleUserMaterialCode(material);
		src += kGlueCore;
		src += kGluePbr;
		src += patchedPbrMain();
		break;
	case LitShade::PhongOit:
		throw std::runtime_error("DX12 の材質: 半透明の描画には材質を差せません");
	}
	return src;
}

} // namespace mitiru::render
