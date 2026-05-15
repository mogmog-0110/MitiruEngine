#pragma once
/// @file GltfMaterialIntegration.hpp
/// @brief glTF PBRマテリアルをRenderer3D用Materialに変換する

#include <mitiru/render/Material.hpp>
#include <mitiru/render/GltfTypes.hpp>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace mitiru::render {

/// @brief glTFマテリアル変換ユーティリティ
class GltfMaterialConverter {
public:
    /// @brief glTF PBR metallic-roughness → Phong Material 近似変換
    [[nodiscard]] static Material convertPBR(const GltfMaterialData& gltf) {
        Material mat;

        // Base color → diffuse
        mat.diffuse = {gltf.baseColor.r, gltf.baseColor.g, gltf.baseColor.b, gltf.baseColor.a};

        // Ambient = darkened base color
        const float ambientFactor = 0.3f;
        mat.ambient = {
            gltf.baseColor.r * ambientFactor,
            gltf.baseColor.g * ambientFactor,
            gltf.baseColor.b * ambientFactor,
            1.0f
        };

        // Metallic → specular color
        // Metallic surfaces reflect the base color, non-metallic reflect white
        const float m = std::clamp(gltf.metallic, 0.0f, 1.0f);
        mat.specular = {
            lerp(0.04f, gltf.baseColor.r, m),
            lerp(0.04f, gltf.baseColor.g, m),
            lerp(0.04f, gltf.baseColor.b, m),
            1.0f
        };

        // Roughness → shininess (inverse mapping)
        // roughness 0 = mirror (high shininess), roughness 1 = matte (low shininess)
        const float r = std::clamp(gltf.roughness, 0.01f, 1.0f);
        mat.shininess = std::pow(2.0f / (r * r) - 2.0f, 0.25f) * 10.0f;
        mat.shininess = std::clamp(mat.shininess, 1.0f, 256.0f);

        // Texture paths
        mat.diffuseTexturePath = gltf.baseColorTexturePath;
        mat.normalTexturePath = gltf.normalTexturePath;
        mat.metallic = gltf.metallic;
        mat.roughness = gltf.roughness;

        return mat;
    }

    /// @brief glTFマテリアルをトゥーンシェーダー向けに調整する
    /// @details 彩度を上げ、スペキュラーを抑え、カートゥーン調にする
    [[nodiscard]] static Material convertPBRForToon(const GltfMaterialData& gltf) {
        auto mat = convertPBR(gltf);

        // Boost saturation for cartoon look
        float grey = mat.diffuse.r * 0.299f + mat.diffuse.g * 0.587f + mat.diffuse.b * 0.114f;
        const float satBoost = 1.3f;
        mat.diffuse.r = std::clamp(grey + (mat.diffuse.r - grey) * satBoost, 0.0f, 1.0f);
        mat.diffuse.g = std::clamp(grey + (mat.diffuse.g - grey) * satBoost, 0.0f, 1.0f);
        mat.diffuse.b = std::clamp(grey + (mat.diffuse.b - grey) * satBoost, 0.0f, 1.0f);

        // Reduce specular for cartoon (cel-shading doesn't use sharp specular)
        mat.specular = {0.15f, 0.15f, 0.15f, 1.0f};
        mat.shininess = 8.0f;

        // Brighter ambient for cartoon
        mat.ambient = {
            mat.diffuse.r * 0.5f,
            mat.diffuse.g * 0.5f,
            mat.diffuse.b * 0.5f,
            1.0f
        };

        return mat;
    }

    /// @brief 一括変換
    [[nodiscard]] static std::vector<Material> convertAll(
        const std::vector<GltfMaterialData>& gltfMaterials, bool toonMode = false) {
        std::vector<Material> result;
        result.reserve(gltfMaterials.size());
        for (const auto& gm : gltfMaterials) {
            result.push_back(toonMode ? convertPBRForToon(gm) : convertPBR(gm));
        }
        return result;
    }

private:
    static float lerp(float a, float b, float t) { return a + (b - a) * t; }
};

} // namespace mitiru::render
