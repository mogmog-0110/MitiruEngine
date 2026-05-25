#pragma once

/// @file StyledRectBatch.hpp
/// @brief SDF角丸矩形のバッチレンダラー
/// @details Style2Dのスタイル情報をGPU定数バッファ用のStyleConstantsに変換し、
///          SDF_RECT_VS/PS シェーダーで描画するための頂点・インデックスを蓄積する。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/math/Vec4.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Style2D.hpp>
#include <mitiru/render/Transform2D.hpp>

namespace mitiru::render
{

/// @brief SDF矩形シェーダー用の頂点データ
/// @details 位置・ローカルUV・色・シェイプ矩形を持つ。
///          SDF_RECT_VS の入力レイアウトと一致する。
struct StyledVertex2D
{
    sgc::Vec2f position{};   ///< スクリーン座標位置
    sgc::Vec2f localUV{};    ///< シェイプ内の正規化座標 [0,1]
    sgc::Colorf color{1.0f, 1.0f, 1.0f, 1.0f}; ///< 頂点色
    sgc::Vec4f shapeRect{};  ///< 元の矩形 (x, y, w, h)

    constexpr StyledVertex2D() noexcept = default;

    constexpr StyledVertex2D(const sgc::Vec2f& pos,
                             const sgc::Vec2f& uv,
                             const sgc::Colorf& col,
                             const sgc::Vec4f& rect) noexcept
        : position(pos), localUV(uv), color(col), shapeRect(rect)
    {
    }
};

/// @brief SDF矩形ピクセルシェーダーの定数バッファ構造体
/// @details HLSL cbuffer StyleConstants (register b1) と完全に一致する。
///          16バイトアライメントを遵守する。最大8ストップのグラデーション対応。
struct StyleConstants
{
    float cornerRadii[4]{};        ///< 角丸半径 (tl, tr, br, bl)
    float gradientStops[8][4]{};   ///< グラデーション停止色 (最大8, RGBA each)
    float gradientOffsets[8]{};    ///< グラデーション停止位置 (0.0-1.0)
    float gradientParams[4]{};    ///< type(0=solid,1=linear,2=radial), cos(angle), sin(angle), stopCount
    float strokeColor[4]{};       ///< ストローク色 (RGBA)
    float strokeWidth{};          ///< ストローク幅
    float _pad1[3]{};             ///< 16バイトアライメント用パディング
    float shadowColor[4]{};       ///< シャドウ色 (RGBA)
    float shadowBlur{};           ///< シャドウぼかし半径
    float shadowOffsetX{};        ///< シャドウX方向オフセット
    float shadowOffsetY{};        ///< シャドウY方向オフセット
    float opacity{1.0f};          ///< 全体の不透明度
};

/// @brief SDF角丸矩形のバッチレンダラー
/// @details begin() で蓄積開始、addRect() でスタイル付き矩形を蓄積し、
///          end() で蓄積を終了する。蓄積データは RenderPipeline が
///          SDF_RECT_VS/PS パイプラインで描画する。
///
/// @code
/// mitiru::render::StyledRectBatch batch;
/// batch.begin();
/// batch.addRect({10, 10, 200, 100}, style);
/// batch.end();
/// @endcode
class StyledRectBatch
{
public:
    /// @brief バッチ蓄積を開始する
    /// @note 前回のバッチデータはクリアされる
    void begin() noexcept
    {
        m_vertices.clear();
        m_indices.clear();
        m_style = StyleConstants{};
        m_recording = true;
    }

    /// @brief スタイル付き矩形を蓄積する
    /// @param rect 描画先矩形（スクリーン座標）
    /// @param style 描画スタイル
    /// @param worldXform 呼び出し側のワールド変換（オプション, 既定=恒等）
    /// @details シャドウの描画領域を確保するためクワッドを拡張する。
    ///          Style を StyleConstants に変換して保持する。
    ///          worldXform が非恒等なら、4頂点位置に変換を適用する。
    ///          SDF計算に使う shapeRect は元座標のまま保持される（ローカルUVで補間）。
    void addRect(const sgc::Rectf& rect, const Style& style,
                 const Transform2D& worldXform = Transform2D::identity())
    {
        if (!m_recording)
        {
            return;
        }

        convertStyleToConstants(style);

        // シャドウ拡張量を計算
        const float expandX = style.shadow.blur + std::abs(style.shadow.x);
        const float expandY = style.shadow.blur + std::abs(style.shadow.y);
        const float expand  = std::max(expandX, expandY);

        // 拡張されたクワッド座標（ローカル）
        const float x0 = rect.x() - expand;
        const float y0 = rect.y() - expand;
        const float x1 = rect.x() + rect.width() + expand;
        const float y1 = rect.y() + rect.height() + expand;

        // 4頂点をワールド変換
        const auto v0 = worldXform.apply(x0, y0);
        const auto v1 = worldXform.apply(x1, y0);
        const auto v2 = worldXform.apply(x1, y1);
        const auto v3 = worldXform.apply(x0, y1);

        // 元の矩形情報（シェーダーがSDF計算に使用）— 変換前のローカル座標のまま
        const sgc::Vec4f shapeRect{rect.x(), rect.y(), rect.width(), rect.height()};

        // 4頂点: localUV は拡張クワッド全体に [0,1] でマッピング
        const auto baseIndex = static_cast<std::uint32_t>(m_vertices.size());
        const sgc::Colorf white{1.0f, 1.0f, 1.0f, 1.0f};

        m_vertices.emplace_back(v0, sgc::Vec2f{0.0f, 0.0f}, white, shapeRect);
        m_vertices.emplace_back(v1, sgc::Vec2f{1.0f, 0.0f}, white, shapeRect);
        m_vertices.emplace_back(v2, sgc::Vec2f{1.0f, 1.0f}, white, shapeRect);
        m_vertices.emplace_back(v3, sgc::Vec2f{0.0f, 1.0f}, white, shapeRect);

        // 2三角形 = 6インデックス
        m_indices.push_back(baseIndex);
        m_indices.push_back(baseIndex + 1);
        m_indices.push_back(baseIndex + 2);
        m_indices.push_back(baseIndex);
        m_indices.push_back(baseIndex + 2);
        m_indices.push_back(baseIndex + 3);
    }

    /// @brief バッチ蓄積を終了する
    void end() noexcept
    {
        m_recording = false;
    }

    /// @brief 蓄積された頂点データを取得する
    [[nodiscard]] const std::vector<StyledVertex2D>& vertices() const noexcept
    {
        return m_vertices;
    }

    /// @brief 蓄積されたインデックスデータを取得する
    [[nodiscard]] const std::vector<std::uint32_t>& indices() const noexcept
    {
        return m_indices;
    }

    /// @brief 現在のスタイル定数を取得する
    [[nodiscard]] const StyleConstants& currentStyle() const noexcept
    {
        return m_style;
    }

    /// @brief 蓄積データがあるか
    [[nodiscard]] bool hasData() const noexcept
    {
        return !m_vertices.empty();
    }

private:
    /// @brief Style を StyleConstants に変換する
    /// @param style 入力スタイル
    void convertStyleToConstants(const Style& style)
    {
        m_style = StyleConstants{};

        // 角丸半径
        m_style.cornerRadii[0] = style.corners.tl;
        m_style.cornerRadii[1] = style.corners.tr;
        m_style.cornerRadii[2] = style.corners.br;
        m_style.cornerRadii[3] = style.corners.bl;

        // 塗り色とグラデーションパラメータ
        convertFill(style.fill);

        // ストローク
        convertStrokeColor(style.stroke.fill);
        m_style.strokeWidth = style.stroke.width;

        // シャドウ
        m_style.shadowColor[0] = style.shadow.color.r;
        m_style.shadowColor[1] = style.shadow.color.g;
        m_style.shadowColor[2] = style.shadow.color.b;
        m_style.shadowColor[3] = style.shadow.color.a;
        m_style.shadowBlur     = style.shadow.blur;
        m_style.shadowOffsetX  = style.shadow.x;
        m_style.shadowOffsetY  = style.shadow.y;

        // 不透明度
        m_style.opacity = style.opacity;
    }

    /// @brief Fill(Gradient) を定数バッファ値に変換する
    void convertFill(const Fill& fill)
    {
        if (fill.type == Gradient::Type::Solid)
        {
            // ソリッド: stops[0]に単色、stopCount=1
            setSolidStop(fill.solidColor);
            m_style.gradientParams[0] = 0.0f; // Solid
            m_style.gradientParams[1] = 0.0f;
            m_style.gradientParams[2] = 0.0f;
            m_style.gradientParams[3] = 1.0f; // stopCount
        }
        else if (fill.type == Gradient::Type::Linear)
        {
            populateGradientStops(fill);
            const float angleRad = fill.angle * 3.14159265358979f / 180.0f;
            m_style.gradientParams[0] = 1.0f; // Linear
            m_style.gradientParams[1] = std::cos(angleRad);
            m_style.gradientParams[2] = std::sin(angleRad);
            m_style.gradientParams[3] = static_cast<float>(
                std::max(static_cast<uint8_t>(1), fill.stopCount));
        }
        else if (fill.type == Gradient::Type::Radial)
        {
            populateGradientStops(fill);
            m_style.gradientParams[0] = 2.0f; // Radial
            m_style.gradientParams[1] = 0.0f;
            m_style.gradientParams[2] = 0.0f;
            m_style.gradientParams[3] = static_cast<float>(
                std::max(static_cast<uint8_t>(1), fill.stopCount));
        }
    }

    /// @brief ソリッドカラーを stops[0] に設定する
    void setSolidStop(const sgc::Colorf& c)
    {
        m_style.gradientStops[0][0] = c.r;
        m_style.gradientStops[0][1] = c.g;
        m_style.gradientStops[0][2] = c.b;
        m_style.gradientStops[0][3] = c.a;
        m_style.gradientOffsets[0]  = 0.0f;
    }

    /// @brief グラデーション停止点を全て定数バッファにコピーする
    void populateGradientStops(const Fill& fill)
    {
        if (fill.stopCount == 0)
        {
            // フォールバック: solidColor を単一ストップとして使用
            setSolidStop(fill.solidColor);
            return;
        }

        const int count = std::min(static_cast<int>(fill.stopCount), 8);
        for (int i = 0; i < count; ++i)
        {
            const auto& stop = fill.stops[static_cast<size_t>(i)];
            m_style.gradientStops[i][0] = stop.color.r;
            m_style.gradientStops[i][1] = stop.color.g;
            m_style.gradientStops[i][2] = stop.color.b;
            m_style.gradientStops[i][3] = stop.color.a;
            m_style.gradientOffsets[i]  = stop.offset;
        }
    }

    /// @brief Stroke の Fill からストローク色を抽出する
    void convertStrokeColor(const Fill& fill)
    {
        // ストロークはソリッドカラーのみサポート（グラデーションストロークは非対応）
        const auto& c = (fill.type == Gradient::Type::Solid)
            ? fill.solidColor
            : (fill.stopCount > 0 ? fill.stops[0].color : fill.solidColor);
        m_style.strokeColor[0] = c.r;
        m_style.strokeColor[1] = c.g;
        m_style.strokeColor[2] = c.b;
        m_style.strokeColor[3] = c.a;
    }

    std::vector<StyledVertex2D> m_vertices;     ///< 蓄積された頂点データ
    std::vector<std::uint32_t> m_indices;       ///< 蓄積されたインデックスデータ
    StyleConstants m_style{};                   ///< 現在のスタイル定数
    bool m_recording = false;                   ///< 蓄積中フラグ
};

/// @brief SDF円/楕円のバッチレンダラー
/// @details begin() で蓄積開始、addCircle()/addEllipse() でスタイル付き円/楕円を蓄積し、
///          end() で蓄積を終了する。蓄積データは RenderPipeline が
///          SDF_CIRCLE_VS/PS パイプラインで描画する。
///
/// @code
/// mitiru::render::StyledCircleBatch batch;
/// batch.begin();
/// batch.addCircle({100, 100}, 50.0f, style);
/// batch.end();
/// @endcode
class StyledCircleBatch
{
public:
    /// @brief バッチ蓄積を開始する
    /// @note 前回のバッチデータはクリアされる
    void begin() noexcept
    {
        m_vertices.clear();
        m_indices.clear();
        m_style = StyleConstants{};
        m_recording = true;
    }

    /// @brief スタイル付き円を蓄積する
    /// @param center 円の中心座標（スクリーン座標）
    /// @param radius 円の半径
    /// @param style 描画スタイル
    /// @param worldXform 呼び出し側のワールド変換（オプション）
    void addCircle(const sgc::Vec2f& center, float radius, const Style& style,
                   const Transform2D& worldXform = Transform2D::identity())
    {
        addEllipse(center, radius, radius, style, worldXform);
    }

    /// @brief スタイル付き楕円を蓄積する
    /// @param center 楕円の中心座標（スクリーン座標）
    /// @param rx X方向の半径
    /// @param ry Y方向の半径
    /// @param style 描画スタイル
    /// @param worldXform 呼び出し側のワールド変換（オプション, 既定=恒等）
    /// @details シャドウの描画領域を確保するためクワッドを拡張する。
    ///          worldXform が非恒等なら4頂点位置に変換を適用する。
    ///          shapeRect はローカル座標のまま保持される（SDFはローカル空間で計算される）。
    void addEllipse(const sgc::Vec2f& center, float rx, float ry, const Style& style,
                    const Transform2D& worldXform = Transform2D::identity())
    {
        if (!m_recording)
        {
            return;
        }

        convertStyleToConstants(style);

        // シャドウ拡張量を計算
        const float expandX = style.shadow.blur + std::abs(style.shadow.x);
        const float expandY = style.shadow.blur + std::abs(style.shadow.y);
        const float expand  = std::max(expandX, expandY);

        // 拡張されたクワッド座標（中心 ± 半径 ± シャドウ拡張, ローカル）
        const float x0 = center.x - rx - expand;
        const float y0 = center.y - ry - expand;
        const float x1 = center.x + rx + expand;
        const float y1 = center.y + ry + expand;

        // 4頂点をワールド変換
        const auto v0 = worldXform.apply(x0, y0);
        const auto v1 = worldXform.apply(x1, y0);
        const auto v2 = worldXform.apply(x1, y1);
        const auto v3 = worldXform.apply(x0, y1);

        // シェイプ情報（シェーダーがSDF計算に使用）— ローカル座標のまま
        const sgc::Vec4f shapeRect{center.x, center.y, rx, ry};

        // 4頂点: localUV は拡張クワッド全体に [0,1] でマッピング
        const auto baseIndex = static_cast<std::uint32_t>(m_vertices.size());
        const sgc::Colorf white{1.0f, 1.0f, 1.0f, 1.0f};

        m_vertices.emplace_back(v0, sgc::Vec2f{0.0f, 0.0f}, white, shapeRect);
        m_vertices.emplace_back(v1, sgc::Vec2f{1.0f, 0.0f}, white, shapeRect);
        m_vertices.emplace_back(v2, sgc::Vec2f{1.0f, 1.0f}, white, shapeRect);
        m_vertices.emplace_back(v3, sgc::Vec2f{0.0f, 1.0f}, white, shapeRect);

        // 2三角形 = 6インデックス
        m_indices.push_back(baseIndex);
        m_indices.push_back(baseIndex + 1);
        m_indices.push_back(baseIndex + 2);
        m_indices.push_back(baseIndex);
        m_indices.push_back(baseIndex + 2);
        m_indices.push_back(baseIndex + 3);
    }

    /// @brief バッチ蓄積を終了する
    void end() noexcept
    {
        m_recording = false;
    }

    /// @brief 蓄積された頂点データを取得する
    [[nodiscard]] const std::vector<StyledVertex2D>& vertices() const noexcept
    {
        return m_vertices;
    }

    /// @brief 蓄積されたインデックスデータを取得する
    [[nodiscard]] const std::vector<std::uint32_t>& indices() const noexcept
    {
        return m_indices;
    }

    /// @brief 現在のスタイル定数を取得する
    [[nodiscard]] const StyleConstants& currentStyle() const noexcept
    {
        return m_style;
    }

    /// @brief 蓄積データがあるか
    [[nodiscard]] bool hasData() const noexcept
    {
        return !m_vertices.empty();
    }

private:
    /// @brief Style を StyleConstants に変換する
    /// @param style 入力スタイル
    /// @details StyledRectBatch と同じ変換ロジックを使用する。
    ///          cornerRadii は 0 のまま（円/楕円には角丸不要）。
    void convertStyleToConstants(const Style& style)
    {
        m_style = StyleConstants{};

        // cornerRadii は未使用（デフォルト0のまま）

        // 塗り色とグラデーションパラメータ
        convertFill(style.fill);

        // ストローク
        convertStrokeColor(style.stroke.fill);
        m_style.strokeWidth = style.stroke.width;

        // シャドウ
        m_style.shadowColor[0] = style.shadow.color.r;
        m_style.shadowColor[1] = style.shadow.color.g;
        m_style.shadowColor[2] = style.shadow.color.b;
        m_style.shadowColor[3] = style.shadow.color.a;
        m_style.shadowBlur     = style.shadow.blur;
        m_style.shadowOffsetX  = style.shadow.x;
        m_style.shadowOffsetY  = style.shadow.y;

        // 不透明度
        m_style.opacity = style.opacity;
    }

    /// @brief Fill(Gradient) を定数バッファ値に変換する
    void convertFill(const Fill& fill)
    {
        if (fill.type == Gradient::Type::Solid)
        {
            setSolidStop(fill.solidColor);
            m_style.gradientParams[0] = 0.0f; // Solid
            m_style.gradientParams[1] = 0.0f;
            m_style.gradientParams[2] = 0.0f;
            m_style.gradientParams[3] = 1.0f; // stopCount
        }
        else if (fill.type == Gradient::Type::Linear)
        {
            populateGradientStops(fill);
            const float angleRad = fill.angle * 3.14159265358979f / 180.0f;
            m_style.gradientParams[0] = 1.0f; // Linear
            m_style.gradientParams[1] = std::cos(angleRad);
            m_style.gradientParams[2] = std::sin(angleRad);
            m_style.gradientParams[3] = static_cast<float>(
                std::max(static_cast<uint8_t>(1), fill.stopCount));
        }
        else if (fill.type == Gradient::Type::Radial)
        {
            populateGradientStops(fill);
            m_style.gradientParams[0] = 2.0f; // Radial
            m_style.gradientParams[1] = 0.0f;
            m_style.gradientParams[2] = 0.0f;
            m_style.gradientParams[3] = static_cast<float>(
                std::max(static_cast<uint8_t>(1), fill.stopCount));
        }
    }

    /// @brief ソリッドカラーを stops[0] に設定する
    void setSolidStop(const sgc::Colorf& c)
    {
        m_style.gradientStops[0][0] = c.r;
        m_style.gradientStops[0][1] = c.g;
        m_style.gradientStops[0][2] = c.b;
        m_style.gradientStops[0][3] = c.a;
        m_style.gradientOffsets[0]  = 0.0f;
    }

    /// @brief グラデーション停止点を全て定数バッファにコピーする
    void populateGradientStops(const Fill& fill)
    {
        if (fill.stopCount == 0)
        {
            setSolidStop(fill.solidColor);
            return;
        }

        const int count = std::min(static_cast<int>(fill.stopCount), 8);
        for (int i = 0; i < count; ++i)
        {
            const auto& stop = fill.stops[static_cast<size_t>(i)];
            m_style.gradientStops[i][0] = stop.color.r;
            m_style.gradientStops[i][1] = stop.color.g;
            m_style.gradientStops[i][2] = stop.color.b;
            m_style.gradientStops[i][3] = stop.color.a;
            m_style.gradientOffsets[i]  = stop.offset;
        }
    }

    /// @brief Stroke の Fill からストローク色を抽出する
    void convertStrokeColor(const Fill& fill)
    {
        const auto& c = (fill.type == Gradient::Type::Solid)
            ? fill.solidColor
            : (fill.stopCount > 0 ? fill.stops[0].color : fill.solidColor);
        m_style.strokeColor[0] = c.r;
        m_style.strokeColor[1] = c.g;
        m_style.strokeColor[2] = c.b;
        m_style.strokeColor[3] = c.a;
    }

    std::vector<StyledVertex2D> m_vertices;     ///< 蓄積された頂点データ
    std::vector<std::uint32_t> m_indices;       ///< 蓄積されたインデックスデータ
    StyleConstants m_style{};                   ///< 現在のスタイル定数
    bool m_recording = false;                   ///< 蓄積中フラグ
};

} // namespace mitiru::render
