#pragma once
/// @file Billboard3D.hpp
/// @brief 3D空間内のビルボード（常にカメラを向くクワッド）

#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/Material.hpp>
#include <mitiru/render/Camera3D.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/math/Mat4.hpp>
#include <sgc/types/Color.hpp>
#include <vector>
#include <cmath>

namespace mitiru::render {

struct BillboardInstance {
    sgc::Vec3f position;
    float width = 1.0f;
    float height = 1.0f;
    sgc::Colorf color{1,1,1,1};
    float rotation = 0.0f;  // radians around view axis
};

/// @brief ビルボードレンダラー
/// @details 3D空間に常にカメラを向くクワッドを配置する。
///          湯気、値段タグ、エフェクト等に使用。
class Billboard3D {
public:
    Billboard3D() {
        m_quad = Mesh::createPlane(1.0f, 1.0f);
    }

    void add(const BillboardInstance& instance) {
        m_instances.push_back(instance);
    }

    void clear() { m_instances.clear(); }

    /// @brief ビルボードのワールド行列を計算する（カメラの向きを向く）
    [[nodiscard]] sgc::Mat4f computeWorldMatrix(const BillboardInstance& bb,
                                                 const Camera3D& camera) const noexcept {
        // Camera right and up vectors
        const auto forward = camera.forwardDirection();
        const auto right = camera.rightDirection();
        const auto up = camera.upDirection();

        // Billboard matrix: scale + orient to face camera + translate
        const auto scale = sgc::Mat4f::scaling({bb.width, bb.height, 1.0f});

        // Rotation around view axis
        const auto rot = sgc::Mat4f::rotationZ(bb.rotation);

        // Orientation matrix (columns = right, up, -forward)
        sgc::Mat4f orient = sgc::Mat4f::identity();
        orient.m[0][0] = right.x;  orient.m[0][1] = right.y;  orient.m[0][2] = right.z;
        orient.m[1][0] = up.x;     orient.m[1][1] = up.y;     orient.m[1][2] = up.z;
        orient.m[2][0] = -forward.x; orient.m[2][1] = -forward.y; orient.m[2][2] = -forward.z;

        const auto translate = sgc::Mat4f::translation(bb.position);

        return translate * orient * rot * scale;
    }

    [[nodiscard]] const Mesh& quadMesh() const noexcept { return m_quad; }
    [[nodiscard]] const std::vector<BillboardInstance>& instances() const noexcept { return m_instances; }
    [[nodiscard]] std::size_t instanceCount() const noexcept { return m_instances.size(); }

private:
    Mesh m_quad;
    std::vector<BillboardInstance> m_instances;
};

} // namespace mitiru::render
