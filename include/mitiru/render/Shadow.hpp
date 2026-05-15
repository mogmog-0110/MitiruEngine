#pragma once

/// @file Shadow.hpp
/// @brief バックエンド非依存の指向性ライトシャドウマップ設定と行列計算
/// @details
///   DirectionalShadowConfig はシャドウマップの解像度・クリップ面・バイアス等を保持する
///   純データ型。DirectionalShadow はライト方向から view / projection 行列を生成する。
///   GPU リソースは持たず、DX12 / Vulkan いずれのバックエンドからも利用できる。
///
///   使用例:
///   @code
///   mitiru::render::DirectionalShadow shadow;
///   shadow.setLightDirection({-1.0f, -2.0f, -1.0f});
///   auto view = shadow.lightViewMatrix({0.0f, 0.0f, 0.0f});
///   auto proj = shadow.lightProjectionMatrix();
///   @endcode

#include <cmath>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>

namespace mitiru::render
{

/// @brief 指向性シャドウマップの設定パラメータ
/// @details 全フィールドはデフォルト値を持ち、メンバーアクセスで変更する。
struct DirectionalShadowConfig
{
    /// @brief シャドウマップの一辺解像度 (ピクセル)。デフォルト 1024x1024
    int   mapSize         = 1024;
    /// @brief ライト空間直交投影の半範囲 (ワールド単位)
    float orthoHalfExtent = 20.0f;
    /// @brief ニアクリップ面 (ワールド単位)
    float nearClip        = 0.1f;
    /// @brief ファークリップ面 (ワールド単位)
    float farClip         = 100.0f;
    /// @brief シャドウアクネ回避のための深度バイアス
    float depthBias       = 0.001f;
    /// @brief PCF フィルタのテクセル半径
    float pcfRadius       = 1.5f;
};

/// @brief 指向性ライトのシャドウ行列を計算するクラス
/// @details
///   GPU リソースを持たない純粋な行列計算クラス。
///   view 行列は右手系 lookAt、projection 行列は DX12 スタイル Z[0,1] 直交投影。
///
///   使用例:
///   @code
///   DirectionalShadow s;
///   s.setLightDirection({-0.5f, -1.0f, -0.5f});
///   const auto V = s.lightViewMatrix({0, 0, 0});
///   const auto P = s.lightProjectionMatrix();
///   // シェーダーへ渡す: P * V * model
///   @endcode
class DirectionalShadow
{
public:
    /// @brief 設定への参照を返す (非 const)
    [[nodiscard]] DirectionalShadowConfig& config() noexcept { return m_config; }

    /// @brief 設定への参照を返す (const)
    [[nodiscard]] const DirectionalShadowConfig& config() const noexcept { return m_config; }

    /// @brief ライト方向ベクトルを設定する (正規化不要)
    /// @param dir ライトが向かう方向 (ゼロベクトルは未定義)
    void setLightDirection(const sgc::Vec3f& dir) noexcept { m_lightDir = dir; }

    /// @brief 現在のライト方向ベクトルを返す
    [[nodiscard]] sgc::Vec3f lightDirection() const noexcept { return m_lightDir; }

    /// @brief ライト空間 view 行列を計算する (右手系 lookAt)
    /// @param sceneFocus シャドウのフォーカス点 (ワールド座標)
    /// @return ライト視点の view 行列
    /// @details
    ///   eye = sceneFocus - normalize(lightDir) * 50
    ///   target = sceneFocus
    ///   worldUp = (0, 1, 0)。ライト方向が Y 軸と平行なときは Z 軸にフォールバック。
    [[nodiscard]] sgc::Mat4f lightViewMatrix(const sgc::Vec3f& sceneFocus) const noexcept
    {
        const float len = std::sqrt(
            m_lightDir.x * m_lightDir.x +
            m_lightDir.y * m_lightDir.y +
            m_lightDir.z * m_lightDir.z);

        const sgc::Vec3f normDir = (len > 1e-6f)
            ? sgc::Vec3f{ m_lightDir.x / len, m_lightDir.y / len, m_lightDir.z / len }
            : sgc::Vec3f{ 0.0f, -1.0f, 0.0f };

        const sgc::Vec3f eye{
            sceneFocus.x - normDir.x * 50.0f,
            sceneFocus.y - normDir.y * 50.0f,
            sceneFocus.z - normDir.z * 50.0f
        };

        // Y 軸と平行なら Z 軸を worldUp として使う
        const float dotY = std::abs(normDir.y);
        const sgc::Vec3f worldUp = (dotY > 0.999f)
            ? sgc::Vec3f{ 0.0f, 0.0f, 1.0f }
            : sgc::Vec3f{ 0.0f, 1.0f, 0.0f };

        return sgc::Mat4f::lookAt(eye, sceneFocus, worldUp);
    }

    /// @brief ライト空間 projection 行列を計算する (DX12 スタイル Z[0,1])
    /// @return 直交投影行列
    /// @details
    ///   DX12 の NDC は Z が [0, 1]。sgc::Mat4f::orthographic は GL スタイル Z[-1,1] の
    ///   ため、ここでは手動で行列を構築する。
    ///
    ///   行列の導出:
    ///   @code
    ///   x' = x / halfExtent
    ///   y' = y / halfExtent
    ///   z' = (z - near) / (far - near)       // Z[0,1] mapping
    ///   @endcode
    [[nodiscard]] sgc::Mat4f lightProjectionMatrix() const noexcept
    {
        const float h   = m_config.orthoHalfExtent;
        const float n   = m_config.nearClip;
        const float f   = m_config.farClip;
        const float rcp = 1.0f / (f - n);

        // 行優先 (row-major) で構築
        // | 1/h   0     0      0    |
        // | 0     1/h   0      0    |
        // | 0     0     1/(f-n)  -n/(f-n) |
        // | 0     0     0      1    |
        return sgc::Mat4f{
            1.0f / h, 0.0f,     0.0f,         0.0f,
            0.0f,     1.0f / h, 0.0f,         0.0f,
            0.0f,     0.0f,     rcp,          -n * rcp,
            0.0f,     0.0f,     0.0f,          1.0f
        };
    }

private:
    DirectionalShadowConfig m_config{};
    /// @brief ライト方向ベクトル (正規化済みでなくてよい)
    sgc::Vec3f m_lightDir{ -1.0f, -2.0f, -1.0f };
};

} // namespace mitiru::render
