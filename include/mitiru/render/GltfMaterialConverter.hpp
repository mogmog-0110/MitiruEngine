#pragma once

/// @file GltfMaterialConverter.hpp
/// @brief glTF PBRマテリアルからPhongマテリアルへの変換
/// @details PBR metallic-roughness → Phong (ambient/diffuse/specular/shininess) 近似変換。

#include <algorithm>

#include <mitiru/render/GltfTypes.hpp>
#include <mitiru/render/Material.hpp>

namespace mitiru::render
{

/// @brief glTF PBRマテリアルをPhongマテリアルに変換する
/// @param gltfMat glTFマテリアルデータ
/// @return Phongマテリアル
[[nodiscard]] inline Material convertGltfMaterial(const GltfMaterialData& gltfMat)
{
	Material mat;

	const auto& bc = gltfMat.baseColor;
	const float m = gltfMat.metallic;
	const float r = gltfMat.roughness;

	/// Phong近似: ambient = baseColor * 0.1
	mat.ambient = {bc.r * 0.1f, bc.g * 0.1f, bc.b * 0.1f, bc.a};

	/// diffuse = baseColor * (1 - metallic)
	const float diffFactor = 1.0f - m;
	mat.diffuse = {bc.r * diffFactor, bc.g * diffFactor, bc.b * diffFactor, bc.a};

	/// specular = lerp(0.04, baseColor, metallic)
	const float f0 = 0.04f;
	mat.specular = {
		f0 + (bc.r - f0) * m,
		f0 + (bc.g - f0) * m,
		f0 + (bc.b - f0) * m,
		1.0f
	};

	/// shininess = (1 - roughness) * 128
	mat.shininess = std::max(1.0f, (1.0f - r) * 128.0f);

	/// PBRフィールド
	mat.metallic = m;
	mat.roughness = r;
	mat.diffuseTexturePath = gltfMat.baseColorTexturePath;
	mat.normalTexturePath = gltfMat.normalTexturePath;

	/// 描画状態に効く指定はそのまま持ち越す (抜き・両面・最近傍)
	switch (gltfMat.alphaMode)
	{
	case GltfAlphaMode::Mask:  mat.alphaMode = Material::AlphaMode::Mask;  break;
	case GltfAlphaMode::Blend: mat.alphaMode = Material::AlphaMode::Blend; break;
	default:                   mat.alphaMode = Material::AlphaMode::Opaque; break;
	}
	mat.alphaCutoff   = gltfMat.alphaCutoff;
	mat.doubleSided   = gltfMat.doubleSided;
	mat.nearestFilter = gltfMat.nearestFilter;

	return mat;
}

} // namespace mitiru::render
