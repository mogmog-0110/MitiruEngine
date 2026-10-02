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

    /// @brief カスケード数 (B13)。1 = 従来の単一シャドウマップと同じ挙動。2 = 近距離/遠距離、
    ///        3 = 近距離/中距離/遠距離を distance で切り替える (`cascadeForDistance` 参照)。上限 3
    int   cascadeCount          = 1;
    /// @brief カスケード切り替え距離 (ワールド単位、カメラ〜対象点)。cascadeCount >= 2 のとき有効
    float cascadeSplitDistance  = 15.0f;
    /// @brief 近距離カスケードの ortho half extent。orthoHalfExtent より小さくして
    ///        同じ解像度のシャドウマップでもテクセル密度を上げる (遠距離は orthoHalfExtent を使う)
    float cascadeNearHalfExtent = 8.0f;
    /// @brief 中距離 → 遠距離の切り替え距離。cascadeCount >= 3 のとき有効
    float cascadeSplitDistance2 = 40.0f;
    /// @brief 中距離カスケードの ortho half extent。cascadeCount >= 3 のとき有効
    float cascadeMidHalfExtent  = 14.0f;

    /// @brief true のとき cascadeSplitDistance / cascadeNearHalfExtent / orthoHalfExtent を手動値ではなく
    ///        カメラの視錐台から毎フレーム決め直す (`DirectionalShadow::fitCascadesToCamera`)
    bool  autoFitCascades       = false;
    /// @brief 自動フィット時に影を付ける最遠距離 (ワールド単位)。カメラの farClip は空まで含んで
    ///        遠すぎることが多いので、影の範囲はこちらで区切る
    float cascadeMaxDistance    = 60.0f;
    /// @brief 自動フィットの分割係数 λ。0 = 一様分割、1 = 対数分割 (Zhang 2006 の実用分割)
    float cascadeSplitLambda    = 0.7f;
};

/// @brief 視錐台の切片 [nearDist, farDist] を包む球。centerDist は視線上の中心までの距離
struct FrustumSliceSphere
{
    float centerDist = 0.0f;
    float radius     = 0.0f;
};

/// @brief 一様分割と対数分割を λ で混ぜた実用分割の index/count 段目の境界距離
[[nodiscard]] inline float practicalSplitDistance(float nearClip, float farClip, int index, int count, float lambda) noexcept
{
    if (count <= 0 || nearClip <= 0.0f) { return farClip; }
    const float t           = static_cast<float>(index) / static_cast<float>(count);
    const float uniform     = nearClip + (farClip - nearClip) * t;
    const float logarithmic = nearClip * std::pow(farClip / nearClip, t);
    return lambda * logarithmic + (1.0f - lambda) * uniform;
}

/// @brief 視錐台の切片を包む球を、視線上で近側 4 隅と遠側 4 隅が等距離になる中心で求める
///        (中心は遠側の面より奥へは出さない)。ライト方向に依らない半径なので、カメラが回っても
///        ortho の大きさが変わらず影の縁がちらつかない
[[nodiscard]] inline FrustumSliceSphere frustumSliceBoundingSphere(float fovY, float aspect, float nearDist, float farDist) noexcept
{
    const float tanHalf = std::tan(fovY * 0.5f);
    const float k       = tanHalf * tanHalf * (1.0f + aspect * aspect);
    float c = 0.5f * (nearDist + farDist) * (1.0f + k);
    if (c > farDist) { c = farDist; }
    const float dn    = c - nearDist;
    const float df    = farDist - c;
    const float rNear = std::sqrt(dn * dn + nearDist * nearDist * k);
    const float rFar  = std::sqrt(df * df + farDist * farDist * k);
    return { c, rNear > rFar ? rNear : rFar };
}

/// @brief ortho 投影の平行移動をシャドウマップのテクセル格子に揃える。カメラが平行移動すると
///        フォーカス点 (= ortho の中心) がサブテクセル量だけ動き、影の縁が毎フレーム別のテクセルに
///        当たってちらつく。世界原点の投影先を 2/mapSize の倍数へ丸めた分だけ clip 空間で
///        ずらすと、格子が世界に固定される (ortho は w=1 なので clip 平行移動がそのまま NDC の平行移動)
[[nodiscard]] inline sgc::Mat4f snapProjectionToTexels(const sgc::Mat4f& lightView, const sgc::Mat4f& lightProj, int mapSize) noexcept
{
    if (mapSize <= 0) { return lightProj; }
    const float texel = 2.0f / static_cast<float>(mapSize);
    const sgc::Vec3f origin = (lightProj * lightView).transformPoint(sgc::Vec3f{ 0.0f, 0.0f, 0.0f });
    const float dx = std::round(origin.x / texel) * texel - origin.x;
    const float dy = std::round(origin.y / texel) * texel - origin.y;
    sgc::Mat4f snapped = lightProj;
    snapped.m[0][3] += dx;
    snapped.m[1][3] += dy;
    return snapped;
}

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

    /// @brief ライト空間 view 行列を計算する (左手系 lookAt)
    /// @param sceneFocus シャドウのフォーカス点 (ワールド座標)
    /// @return ライト視点の view 行列
    /// @details
    ///   eye = sceneFocus - normalize(lightDir) * lightEyeDistance (既定 50、自動フィットが包含球に合わせて伸ばす)
    ///   target = sceneFocus
    ///   worldUp = (0, 1, 0)。ライト方向が Y 軸と平行なときは Z 軸にフォールバック。
    ///
    ///   左手系 (glm::lookAtLH と同規約) で組む。理由は lightProjectionMatrix 側にある。
    ///   あれは Z[0,1] かつ「view z が正」前提 (z'=(z-near)/(far-near)) なので、
    ///   view も左手系でなければ符号が合わない。
    ///   sgc::Mat4f::lookAt は右手系で visible geometry の view z が負になり、
    ///   投影後の深度が全て負 → viewport で 0 にクランプ → SampleCmp が常に
    ///   「影なし」を返してシャドウが一切出なくなる。
    ///
    ///   ここはライト空間だけの話で、メインカメラとは無関係である。
    ///   メインカメラは GlmBridge::lookAt = glm::lookAtRH、
    ///   GlmBridge::perspective = glm::perspectiveRH_ZO、つまり**右手系**。
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
            sceneFocus.x - normDir.x * m_lightEyeDistance,
            sceneFocus.y - normDir.y * m_lightEyeDistance,
            sceneFocus.z - normDir.z * m_lightEyeDistance
        };

        // Y 軸と平行なら Z 軸を worldUp として使う
        const float dotY = std::abs(normDir.y);
        const sgc::Vec3f worldUp = (dotY > 0.999f)
            ? sgc::Vec3f{ 0.0f, 0.0f, 1.0f }
            : sgc::Vec3f{ 0.0f, 1.0f, 0.0f };

        // 左手系 lookAt: forward はシーンへ向かう (target - eye)。
        const sgc::Vec3f forward = (sceneFocus - eye).normalized();
        const sgc::Vec3f right   = worldUp.cross(forward).normalized();
        const sgc::Vec3f up      = forward.cross(right);

        return sgc::Mat4f{
            right.x,   right.y,   right.z,   -right.dot(eye),
            up.x,      up.y,      up.z,      -up.dot(eye),
            forward.x, forward.y, forward.z, -forward.dot(eye),
            0.0f,      0.0f,      0.0f,       1.0f
        };
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

    /// @brief 指定カスケードの ortho half extent で projection 行列を計算する (B13)
    /// @param cascadeIndex 0 = 近距離 (cascadeNearHalfExtent) / 最後の段 = 遠距離 (orthoHalfExtent) /
    ///        3 カスケード時の 1 = 中距離 (cascadeMidHalfExtent)
    /// @details near/far クリップと式は `lightProjectionMatrix()` と同じで、half extent だけ
    ///          カスケードごとに差し替える。cascadeCount == 1 のときも呼んでよく、その場合は
    ///          cascadeIndex に関わらず orthoHalfExtent (= 単一カスケード相当) を返す。
    [[nodiscard]] sgc::Mat4f lightProjectionMatrixForCascade(int cascadeIndex) const noexcept
    {
        float h = m_config.orthoHalfExtent;
        if (m_config.cascadeCount > 1 && cascadeIndex <= 0) { h = m_config.cascadeNearHalfExtent; }
        else if (m_config.cascadeCount >= 3 && cascadeIndex == 1) { h = m_config.cascadeMidHalfExtent; }
        const float n   = m_config.nearClip;
        const float f   = m_config.farClip;
        const float rcp = 1.0f / (f - n);

        return sgc::Mat4f{
            1.0f / h, 0.0f,     0.0f,         0.0f,
            0.0f,     1.0f / h, 0.0f,         0.0f,
            0.0f,     0.0f,     rcp,          -n * rcp,
            0.0f,     0.0f,     0.0f,          1.0f
        };
    }

    /// @brief 描画に使うカスケードの projection。自動フィット中は `snapProjectionToTexels` で
    ///        テクセル格子に揃える (手動値のときは `lightProjectionMatrixForCascade` そのもの)
    [[nodiscard]] sgc::Mat4f cascadeProjection(int cascadeIndex, const sgc::Mat4f& lightView) const noexcept
    {
        const sgc::Mat4f proj = lightProjectionMatrixForCascade(cascadeIndex);
        if (!m_config.autoFitCascades || !m_fitApplied || m_config.cascadeCount <= 1) { return proj; }
        return snapProjectionToTexels(lightView, proj, m_config.mapSize);
    }

    /// @brief カメラ距離からどのカスケードを使うか決める (B13)
    /// @param distanceFromCamera 描画対象点とカメラの距離 (ワールド単位)
    /// @return cascadeCount <= 1 なら常に 0。それ以外は cascadeSplitDistance 未満で 0 (近距離)、
    ///         以上で 1。3 カスケード時は cascadeSplitDistance2 以上で 2 (遠距離)
    [[nodiscard]] int cascadeForDistance(float distanceFromCamera) const noexcept
    {
        if (m_config.cascadeCount <= 1) { return 0; }
        if (distanceFromCamera < m_config.cascadeSplitDistance) { return 0; }
        if (m_config.cascadeCount >= 3 && distanceFromCamera >= m_config.cascadeSplitDistance2) { return 2; }
        return 1;
    }

    /// @brief カスケード (2 か 3) をカメラの視錐台に合わせる (config().autoFitCascades の実体)。
    ///        [cameraNear, cascadeMaxDistance] を cascadeSplitLambda で段数ぶんに分け、各切片の包含球の
    ///        半径を各段の half extent に、球の中心を各段のフォーカス点にする。
    ///        フォーカス点は `cascadeFocus` で読む。cascadeCount <= 1 なら何もしない
    void fitCascadesToCamera(float fovY, float aspect, float cameraNear,
                             const sgc::Vec3f& cameraPos, const sgc::Vec3f& cameraForward) noexcept
    {
        if (m_config.cascadeCount <= 1) { return; }
        const float n = cameraNear > 1e-3f ? cameraNear : 1e-3f;
        const float f = m_config.cascadeMaxDistance > n + 1e-3f ? m_config.cascadeMaxDistance : n + 1e-3f;
        const int   count  = m_config.cascadeCount >= 3 ? 3 : 2;
        const float split  = practicalSplitDistance(n, f, 1, count, m_config.cascadeSplitLambda);
        const float split2 = count >= 3 ? practicalSplitDistance(n, f, 2, count, m_config.cascadeSplitLambda) : f;
        const FrustumSliceSphere nearSphere = frustumSliceBoundingSphere(fovY, aspect, n, split);
        const FrustumSliceSphere midSphere  = frustumSliceBoundingSphere(fovY, aspect, split, split2);
        const FrustumSliceSphere farSphere  = count >= 3 ? frustumSliceBoundingSphere(fovY, aspect, split2, f) : midSphere;

        m_config.cascadeSplitDistance  = split;
        m_config.cascadeNearHalfExtent = nearSphere.radius;
        m_config.orthoHalfExtent       = farSphere.radius;
        if (count >= 3)
        {
            m_config.cascadeSplitDistance2 = split2;
            m_config.cascadeMidHalfExtent  = midSphere.radius;
        }
        // 包含球が既定の視点距離 50 より大きいと、ライト側の caster が near で切れる。視点を球の外へ
        // 出し、far も球の反対側まで届かせる (R32 深度なので範囲を広げても精度の問題は無い)
        const float eyeDist = farSphere.radius + 1.0f;
        if (eyeDist > m_lightEyeDistance) { m_lightEyeDistance = eyeDist; }
        if (m_config.farClip < 2.0f * m_lightEyeDistance) { m_config.farClip = 2.0f * m_lightEyeDistance; }
        m_fittedFocus[0] = cameraPos + cameraForward * nearSphere.centerDist;
        m_fittedFocus[1] = cameraPos + cameraForward * midSphere.centerDist;
        m_fittedFocus[2] = cameraPos + cameraForward * farSphere.centerDist;
        m_fitApplied     = true;
    }

    /// @brief ライト視点をフォーカス点からどれだけ離すか (lightViewMatrix の eye)
    [[nodiscard]] float lightEyeDistance() const noexcept { return m_lightEyeDistance; }

    /// @brief カスケードのフォーカス点。自動フィットが有効で一度でも適用されていれば視錐台切片の
    ///        中心、そうでなければ fallback (従来の caster 重心) を返す
    [[nodiscard]] sgc::Vec3f cascadeFocus(int cascadeIndex, const sgc::Vec3f& fallback) const noexcept
    {
        if (!m_config.autoFitCascades || !m_fitApplied || m_config.cascadeCount <= 1) { return fallback; }
        const int last = m_config.cascadeCount >= 3 ? 2 : 1;
        return m_fittedFocus[cascadeIndex <= 0 ? 0 : (cascadeIndex >= last ? last : cascadeIndex)];
    }

private:
    DirectionalShadowConfig m_config{};
    /// @brief ライト方向ベクトル (正規化済みでなくてよい)
    sgc::Vec3f m_lightDir{ -1.0f, -2.0f, -1.0f };
    sgc::Vec3f m_fittedFocus[3]{};
    bool       m_fitApplied = false;
    float      m_lightEyeDistance = 50.0f;
};

} // namespace mitiru::render
