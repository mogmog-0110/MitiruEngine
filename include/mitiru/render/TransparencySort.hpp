#pragma once
/// @file TransparencySort.hpp
/// @brief 透過オブジェクトの深度ソート

#include <mitiru/render/Scene3D.hpp>
#include <mitiru/render/Camera3D.hpp>
#include <algorithm>
#include <vector>
#include <cmath>

namespace mitiru::render {

struct SortedDrawCommand {
    const Scene3D::RenderObject* object;
    float distanceToCamera;
};

/// @brief 透過オブジェクト深度ソーター
class TransparencySort {
public:
    /// @brief シーンオブジェクトを不透明と半透明に分離し、半透明を遠い順にソート
    static void sortScene(const Scene3D& scene, const Camera3D& camera,
                          std::vector<const Scene3D::RenderObject*>& opaqueOut,
                          std::vector<const Scene3D::RenderObject*>& transparentOut)
    {
        opaqueOut.clear();
        transparentOut.clear();

        std::vector<SortedDrawCommand> transparentCommands;

        for (const auto& obj : scene.objects()) {
            if (obj.material.diffuse.a < 0.99f) {
                // Transparent
                float dist = distanceSquared(obj.position, camera.position());
                transparentCommands.push_back({&obj, dist});
            } else {
                opaqueOut.push_back(&obj);
            }
        }

        // Sort transparent by distance (far first for correct blending)
        std::sort(transparentCommands.begin(), transparentCommands.end(),
            [](const SortedDrawCommand& a, const SortedDrawCommand& b) {
                return a.distanceToCamera > b.distanceToCamera;
            });

        transparentOut.reserve(transparentCommands.size());
        for (const auto& cmd : transparentCommands) {
            transparentOut.push_back(cmd.object);
        }
    }

private:
    static float distanceSquared(const sgc::Vec3f& a, const sgc::Vec3f& b) {
        float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return dx*dx + dy*dy + dz*dz;
    }
};

} // namespace mitiru::render
