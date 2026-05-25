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

        // base color → diffuse
        mat.diffuse = {gltf.baseColor.r, gltf.baseColor.g, gltf.baseColor.b, gltf.baseColor.a};

        // ambient = base color を暗くしたもの
        const float ambientFactor = 0.3f;
        mat.ambient = {
            gltf.baseColor.r * ambientFactor,
            gltf.baseColor.g * ambientFactor,
            gltf.baseColor.b * ambientFactor,
            1.0f
        };

        // metallic → specular color
        // metallic 面は base color を反射、非 metallic は白を反射する
        const float m = std::clamp(gltf.metallic, 0.0f, 1.0f);
        mat.specular = {
            lerp(0.04f, gltf.baseColor.r, m),
            lerp(0.04f, gltf.baseColor.g, m),
            lerp(0.04f, gltf.baseColor.b, m),
            1.0f
        };

        // roughness → shininess (逆マッピング)
        // roughness 0 = 鏡面 (shininess 大), roughness 1 = マット (shininess 小)
        const float r = std::clamp(gltf.roughness, 0.01f, 1.0f);
        mat.shininess = std::pow(2.0f / (r * r) - 2.0f, 0.25f) * 10.0f;
        mat.shininess = std::clamp(mat.shininess, 1.0f, 256.0f);

        // テクスチャパス
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

        // カートゥーン調にするため彩度を上げる
        float grey = mat.diffuse.r * 0.299f + mat.diffuse.g * 0.587f + mat.diffuse.b * 0.114f;
        const float satBoost = 1.3f;
        mat.diffuse.r = std::clamp(grey + (mat.diffuse.r - grey) * satBoost, 0.0f, 1.0f);
        mat.diffuse.g = std::clamp(grey + (mat.diffuse.g - grey) * satBoost, 0.0f, 1.0f);
        mat.diffuse.b = std::clamp(grey + (mat.diffuse.b - grey) * satBoost, 0.0f, 1.0f);

        // カートゥーン用に specular を抑える (cel-shading は鋭い specular を使わない)
        mat.specular = {0.15f, 0.15f, 0.15f, 1.0f};
        mat.shininess = 8.0f;

        // カートゥーン用に ambient を明るめにする
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
