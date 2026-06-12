#pragma once

// ⚠ 800 行ルールの記録 (リファクタ P4): 本ファイルは 200+ の draw API 宣言 +
// 1-3 行の薄い inline ラッパのみで、実装本体は末尾 include の detail/Screen_*.hpp
// 8 ファイルに分割済み。単一クラスの公開 API 表面は宣言を分割できないため、
// 行数超過は構造的限界として許容 (in-class に 12 行超の実装体は存在しない)。
// ⚠ ABI: Screen* は gameDraw() で DLL 境界を渡る。メンバ追加は必ず class 末尾 +
// ModuleApi の kCurrentApiVersion を上げること (v14 事故の教訓)。

/// @file Screen.hpp
/// @brief 描画サーフェス
/// @details レンダラーへの描画コマンドを抽象化するサーフェスクラス。
///          RenderPipeline2Dが接続されている場合はGPU描画に委譲し、
///          未接続の場合はカウンターのみ増加する（ヘッドレス対応）。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stack>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <sgc/types/Color.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/math/Rect.hpp>

#include <mitiru/gfx/GfxTypes.hpp>
#include <mitiru/render/Texture.hpp>
#include <mitiru/render/SpriteBatch.hpp>
#include <mitiru/render/ShapeRenderer.hpp>
#include <mitiru/render/TextRenderer.hpp>
#include <mitiru/render/Style2D.hpp>
#include <mitiru/render/StyledRectBatch.hpp>
#include <mitiru/render/Transform2D.hpp>
#include <mitiru/render/StyledShapeRenderer.hpp>

#include <mitiru/ui/UINode.hpp>
#include <mitiru/ui/UITheme.hpp>
#include <mitiru/validate/DrawCallValidator.hpp>

namespace mitiru::render
{
class RenderPipeline2D;
enum class PixelArtFilter; ///< forward decl。完全な定義は RenderPipeline2D.hpp。
} // namespace mitiru::render


namespace mitiru
{

/// @brief AI 観測用 draw log の 1 エントリ (/api/ai/frame)
/// @details call は静的文字列リテラルを指す。text はテキスト描画の先頭バイトのみ保持。
struct DrawLogEntry
{
	float x = 0.0f;        ///< 描画矩形 左上 X
	float y = 0.0f;        ///< 描画矩形 左上 Y
	float w = 0.0f;        ///< 描画矩形 幅
	float h = 0.0f;        ///< 描画矩形 高さ
	const char* call = ""; ///< 描画 API 名 (例 "drawRect")
	char text[48] = {};    ///< テキスト描画の内容 (UTF-8、先頭 47 byte で切る)
};

/// @brief 描画サーフェス
/// @details ゲームの draw() に渡される描画インターフェース。
///          内部でSpriteBatch/ShapeRendererに委譲し、
///          RenderPipeline2D経由でGPUに送信する。
class Screen
{
public:
	/// @brief コンストラクタ
	/// @param width サーフェス幅（ピクセル）
	/// @param height サーフェス高さ（ピクセル）
	explicit Screen(int width, int height) noexcept
		: m_width(width)
		, m_height(height)
	{
	}

	/// @brief RenderPipeline2Dを接続する
	/// @param pipeline パイプライン（nullptrで解除）
	void setPipeline(render::RenderPipeline2D* pipeline) noexcept;

	/// @brief RenderPipeline2Dを取得する
	/// @return パイプラインへのポインタ（未接続時はnullptr）
	[[nodiscard]] render::RenderPipeline2D* pipeline() const noexcept
	{
		return m_pipeline;
	}

	/// @brief TrueTypeFont描画/計測コールバック型
	using TtDrawFunc = void(*)(void* font, Screen& scr, float x, float y,
	                           std::string_view text, float fontSize, const sgc::Colorf& color);
	using TtMeasureFunc = sgc::Vec2f(*)(void* font, std::string_view text, float fontSize);

	/// @brief TrueTypeFontを接続する（設定時はBitmapFontより優先）
	/// @details forward declaration のみで利用可能。呼び出し元で型を解決する。
	/// @code
	///   screen.setTrueTypeFont(&myFont,
	///       [](void* f, Screen& s, float x, float y, auto t, float fs, auto& c) {
	///           static_cast<vn::TrueTypeFont*>(f)->renderText(s, x, y, t, fs, c);
	///       },
	///       [](void* f, auto t, float fs) -> sgc::Vec2f {
	///           auto* font = static_cast<vn::TrueTypeFont*>(f);
	///           return { font->textWidth(t, fs), font->lineHeight(fs) };
	///       });
	/// @endcode
	void setTrueTypeFont(void* font, TtDrawFunc drawFn, TtMeasureFunc measureFn) noexcept;

	/// @brief TrueTypeFontを取得する（型消去）
	[[nodiscard]] void* trueTypeFont() const noexcept
	{
		return m_ttFont;
	}

	/// @brief SDFフォント描画/計測コールバック型
	/// @details TtDrawFunc/TtMeasureFunc と同じシグネチャ（型消去）。
	///          SDF フォントを設定するとSDFが TTF より優先される。
	using SdfDrawFunc = TtDrawFunc;
	using SdfMeasureFunc = TtMeasureFunc;

	/// @brief SDFフォントを接続する（設定時は TTF/BitmapFont より優先）
	/// @details 高品質な任意サイズレンダリング用。内部でGPU SDFシェーダまたは
	///          CPUソフトウェアラスタライズを使う（呼び出し側の実装に依存）。
	/// @code
	///   screen.setSdfFont(&sdfRenderer,
	///       [](void* r, Screen& s, float x, float y, auto t, float fs, auto& c) {
	///           static_cast<render::SdfTextRenderer*>(r)->drawTextSoftware(s, t, x, y, fs, c);
	///       },
	///       [](void* r, auto t, float fs) -> sgc::Vec2f {
	///           const auto sz = static_cast<render::SdfTextRenderer*>(r)->measureText(t, fs);
	///           return {sz.width, sz.height};
	///       });
	/// @endcode
	void setSdfFont(void* font, SdfDrawFunc drawFn, SdfMeasureFunc measureFn) noexcept;

	/// @brief SDFフォントを取得する（型消去）
	[[nodiscard]] void* sdfFont() const noexcept
	{
		return m_sdfFont;
	}

	/// @brief SDFフォントを解除する
	void clearSdfFont() noexcept;

	/// @brief フォントレジストリエントリ
	struct FontEntry
	{
		void* font = nullptr;
		TtDrawFunc drawFn = nullptr;
		TtMeasureFunc measureFn = nullptr;
	};

	/// @brief 名前付きフォントを登録する
	/// @param name フォント名（"title", "body" 等）
	/// @param font フォントポインタ（型消去）
	/// @param drawFn 描画コールバック
	/// @param measureFn 計測コールバック
	void registerFont(const std::string& name, void* font,
	                  TtDrawFunc drawFn, TtMeasureFunc measureFn);

	/// @brief アクティブフォントを名前で切り替える
	/// @param name 登録済みフォント名（空文字列でデフォルトに戻す）
	void setFont(const std::string& name);

	/// @brief DrawCallValidatorを接続する
	/// @param validator バリデーター（nullptrで解除）
	void setValidator(validate::DrawCallValidator* validator) noexcept;

	/// @brief DrawCallValidatorを取得する
	/// @return バリデーターへのポインタ（未接続時はnullptr）
	[[nodiscard]] validate::DrawCallValidator* validator() const noexcept
	{
		return m_validator;
	}

	/// @brief サーフェスをクリアする
	/// @param color クリア色
	void clear(const sgc::Colorf& color = sgc::Colorf{0.0f, 0.0f, 0.0f, 1.0f});

	/// @brief 全画面フルスクリーン tint を dur 秒間オーバーレイする (alpha は線形 fade out)。
	/// @details 被弾点滅・ボス登場フラッシュ・タイム停止のグレー化等の一発エフェクト。
	///          color.a が初期 alpha、時間と共に 0 に減衰。pushTint を呼び直すと最新で上書き。
	///          engine が毎フレーム update 末尾に `advanceTint(dt)` を、render 末尾に `renderTint()`
	///          を呼んで合成する。game コードは pushTint だけ気にすれば良い。
	void pushTint(const sgc::Colorf& color, float durSec) noexcept
	{
		m_tintColor    = color;
		m_tintDurSec   = (durSec > 0.0f) ? durSec : 0.0f;
		m_tintRemainSec = m_tintDurSec;
	}

	/// @brief tint 残量を dt 進める (engine が update 末尾で呼ぶ内部 API)。
	void advanceTint(float dt) noexcept
	{
		if (m_tintRemainSec > 0.0f)
		{
			m_tintRemainSec -= dt;
			if (m_tintRemainSec < 0.0f) { m_tintRemainSec = 0.0f; }
		}
	}

	/// @brief tint が残ってれば全画面 rect を描く (engine が render 末尾で呼ぶ内部 API)。
	void renderTint()
	{
		if (m_tintRemainSec <= 0.0f || m_tintDurSec <= 0.0f) { return; }
		const float k = m_tintRemainSec / m_tintDurSec; // 1 → 0
		sgc::Colorf c = m_tintColor;
		c.a *= k;
		drawRect(sgc::Rectf{0.0f, 0.0f,
		                    static_cast<float>(m_width),
		                    static_cast<float>(m_height)},
		         c);
	}

	// ── Styled Drawing API (CSS互換) ────────────────────

	/// @brief スタイル付き矩形を描画する (SDF GPU path)
	/// @details 各矩形は独立したスタイル定数を持つため、即座にGPU送信する。
	///          実装はRenderPipeline2D.hppインクルード後に配置。
	void drawStyledRect(const sgc::Rectf& rect, const render::Style& style);

	/// @brief スタイル付き円を描画する (SDF GPU path)
	/// @details 各円は独立したスタイル定数を持つため、即座にGPU送信する。
	///          実装はRenderPipeline2D.hppインクルード後に配置。
	void drawStyledCircle(const sgc::Vec2f& center, float radius, const render::Style& style);

	/// @brief スタイル付きシェイプを描画する（汎用）
	void draw(const render::ShapeData& shape, const render::Style& style)
	{
		std::visit([this, &style](const auto& s) { drawShape(s, style); }, shape);
	}

	/// @brief ギザギザエッジを描画する（レシート風）
	void drawZigzagEdge(const sgc::Rectf& rect, const render::ZigzagStyle& zs);

	/// @brief スキャンラインを描画する（CRT風テレビ画面）
	void drawScanlines(const sgc::Rectf& rect, float lineHeight, const sgc::Colorf& color);

	/// @brief 破線を描画する
	void drawDashedLine(const sgc::Vec2f& from, const sgc::Vec2f& to,
	                    float thickness, float dashLen, float gapLen,
	                    const sgc::Colorf& color);

	// ── Legacy Drawing API ──────────────────────────

	/// @brief 矩形を描画する
	/// @param rect 矩形領域
	/// @param color 描画色
	void drawRect(const sgc::Rectf& rect, const sgc::Colorf& color);

	// ── 初心者向け float オーバーロード ───────────────────────────
	// sgc::Rectf / sgc::Vec2f を組まずに座標を直接渡して描ける薄い糖衣。
	// 中身は sgc 版へ委譲するだけ。

	/// @brief (x,y) を左上に w×h の矩形を塗る。
	void drawRect(float x, float y, float w, float h, const sgc::Colorf& color)
	{
		drawRect(sgc::Rectf{x, y, w, h}, color);
	}
	/// @brief (cx,cy) を中心に w×h の矩形を塗る。
	void drawRectCentered(float cx, float cy, float w, float h, const sgc::Colorf& color)
	{
		drawRect(sgc::Rectf{cx - w * 0.5f, cy - h * 0.5f, w, h}, color);
	}
	/// @brief (cx,cy) を中心に半径 r の円を塗る。
	void fillCircle(float cx, float cy, float r, const sgc::Colorf& color)
	{
		drawCircle(sgc::Vec2f{cx, cy}, r, color);
	}
	/// @brief (x0,y0)-(x1,y1) を結ぶ線を引く。
	void line(float x0, float y0, float x1, float y1, const sgc::Colorf& color, float thickness = 2.0f)
	{
		drawLine(sgc::Vec2f{x0, y0}, sgc::Vec2f{x1, y1}, color, thickness);
	}
	/// @brief (x,y) を左上に 1 行テキストを描く。はみ出しはクリップされる安全版
	///        (生 drawText は禁止 → drawTextInRect に委譲する)。
	void text(std::string_view str, float x, float y,
	          const sgc::Colorf& color = sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f},
	          float fontSize = 18.0f)
	{
		drawTextInRect(sgc::Rectf{x, y, 100000.0f, fontSize * 1.6f}, str, color, fontSize,
		               TextAlignH::Left, TextAlignV::Top, 0.0f, 0.0f);
	}
	/// @brief 画面全体を 1 色で塗る (背景用)。draw() の最初に呼ぶ。
	/// @note `clear()` の色は host 設定 (EngineConfig::backgroundColor) に上書きされ
	///       game 窓に届かないことがある。背景は確実なこの fillScreen で塗る (全画面 rect)。
	///       画面端の隙間 (shake 等) も覆うよう少し大きめに描く。
	void fillScreen(const sgc::Colorf& color)
	{
		drawRect(sgc::Rectf{-128.0f, -128.0f,
		                    static_cast<float>(width()) + 256.0f,
		                    static_cast<float>(height()) + 256.0f}, color);
	}

	/// @brief 矩形の枠線を描画する
	/// @param rect 矩形領域
	/// @param color 枠線色
	/// @param thickness 線の太さ
	void drawRectFrame(const sgc::Rectf& rect, const sgc::Colorf& color, float thickness = 1.0f);

	/// @brief 角丸矩形を描画する
	/// @param rect 矩形領域
	/// @param color 描画色
	/// @param radius 角の半径
	void drawRoundedRect(const sgc::Rectf& rect, const sgc::Colorf& color, float radius = 8.0f);

	/// @brief 角丸矩形の枠線を描画する
	/// @param rect 矩形領域
	/// @param color 枠線色
	/// @param radius 角の半径
	/// @param thickness 線の太さ
	void drawRoundedRectFrame(const sgc::Rectf& rect, const sgc::Colorf& color,
	                          float radius = 8.0f, float thickness = 1.0f);

	/// @brief 扇形（パイ）を描画する
	/// @param center 中心座標
	/// @param radius 半径
	/// @param startAngle 開始角度（ラジアン、0=右）
	/// @param endAngle 終了角度（ラジアン）
	/// @param color 描画色
	void drawPie(const sgc::Vec2f& center, float radius,
	             float startAngle, float endAngle, const sgc::Colorf& color);

	/// @brief 円弧を描画する
	/// @param center 中心座標
	/// @param radius 半径
	/// @param startAngle 開始角度（ラジアン）
	/// @param endAngle 終了角度（ラジアン）
	/// @param color 描画色
	/// @param thickness 線の太さ
	void drawArc(const sgc::Vec2f& center, float radius,
	             float startAngle, float endAngle,
	             const sgc::Colorf& color, float thickness = 2.0f);

	/// @brief 塗りつぶし三角形を描画する
	/// @param p0 頂点0
	/// @param p1 頂点1
	/// @param p2 頂点2
	/// @param color 塗りつぶし色
	void drawTriangle(const sgc::Vec2f& p0, const sgc::Vec2f& p1,
	                  const sgc::Vec2f& p2, const sgc::Colorf& color);

	/// @brief 円を描画する
	/// @param center 中心座標
	/// @param radius 半径
	/// @param color 描画色
	void drawCircle(const sgc::Vec2f& center, float radius, const sgc::Colorf& color);

	/// @brief 円の枠線を描画する（drawArc の全周ラッパ）
	/// @param center 中心座標
	/// @param radius 半径
	/// @param color 枠線色
	/// @param thickness 線の太さ
	void drawCircleFrame(const sgc::Vec2f& center, float radius,
	                     const sgc::Colorf& color, float thickness = 2.0f);

	/// @brief 線分を描画する
	/// @param from 始点
	/// @param to 終点
	/// @param color 描画色
	/// @param thickness 線の太さ
	void drawLine(const sgc::Vec2f& from, const sgc::Vec2f& to,
	              const sgc::Colorf& color, float thickness = 1.0f);

	/// @brief テキストを描画する
	/// @param position 描画位置（左上）
	/// @param text テキスト内容
	/// @param color 描画色
	/// @param fontSize フォントサイズ（8の倍数でスケーリング）
	void drawText(const sgc::Vec2f& position, std::string_view text,
	              const sgc::Colorf& color, float fontSize = 16.0f);

	// ── テキストバウンド計測・クリッピング描画 ─────────────────

	/// @brief テキストの描画サイズを計測する（ピクセル単位）
	/// @param text テキスト内容
	/// @param fontSize フォントサイズ
	/// @return { width, height }
	[[nodiscard]] sgc::Vec2f measureText(std::string_view text, float fontSize = 16.0f) const
	{
		if (m_activeFont && m_activeFont->font && m_activeFont->measureFn)
		{
			return m_activeFont->measureFn(m_activeFont->font, text, fontSize);
		}
		if (m_sdfFont && m_sdfMeasureFunc)
		{
			return m_sdfMeasureFunc(m_sdfFont, text, fontSize);
		}
		if (m_ttFont && m_ttMeasureFunc)
		{
			return m_ttMeasureFunc(m_ttFont, text, fontSize);
		}
		// BitmapFont — floatスケール対応
		const float scale = fontSize / static_cast<float>(render::BitmapFont::GLYPH_HEIGHT);
		const float w = render::TextRenderer::measureWidthFloat(text, scale);
		const float h = render::TextRenderer::measureHeightFloat(scale);
		return {w, h};
	}

	/// @brief テキストが矩形内に収まるかチェックする
	/// @param rect 許容矩形
	/// @param position テキスト描画位置（左上）
	/// @param text テキスト内容
	/// @param fontSize フォントサイズ
	/// @return true: 収まる, false: はみ出す
	[[nodiscard]] bool textFitsInRect(const sgc::Rectf& rect,
	                                   const sgc::Vec2f& position,
	                                   std::string_view text,
	                                   float fontSize = 16.0f) const
	{
		const auto size = measureText(text, fontSize);
		return position.x >= rect.x()
		    && position.y >= rect.y()
		    && position.x + size.x <= rect.x() + rect.width()
		    && position.y + size.y <= rect.y() + rect.height();
	}

	/// @brief 矩形内にクリップしてテキストを描画する
	/// @details はみ出す場合は末尾を "..." に置換して収める。
	/// @param rect 描画許容矩形
	/// @param text テキスト内容
	/// @param color 描画色
	/// @param fontSize フォントサイズ
	/// @param padX 左右パディング
	/// @param padY 上下パディング
	void drawTextClipped(const sgc::Rectf& rect, std::string_view text,
	                      const sgc::Colorf& color, float fontSize = 16.0f,
	                      float padX = 0.0f, float padY = 0.0f);

	/// @brief テキスト水平アラインメント
	enum class TextAlignH { Left, Center, Right };
	/// @brief テキスト垂直アラインメント
	enum class TextAlignV { Top, Middle, Bottom };

	/// @brief 矩形内にアラインメント付きでテキストを描画する
	/// @details はみ出す場合は自動で省略記号に置換する。
	/// @param rect 描画領域
	/// @param text テキスト内容
	/// @param color 描画色
	/// @param fontSize フォントサイズ
	/// @param alignH 水平アラインメント
	/// @param alignV 垂直アラインメント
	/// @param padX 左右パディング
	/// @param padY 上下パディング
	void drawTextInRect(const sgc::Rectf& rect, std::string_view text,
	                     const sgc::Colorf& color, float fontSize = 16.0f,
	                     TextAlignH alignH = TextAlignH::Left,
	                     TextAlignV alignV = TextAlignV::Top,
	                     float padX = 4.0f, float padY = 2.0f);

	/// @brief 矩形内にワードラップしてテキストを描画する
	/// @details 単語境界（スペース）で折り返し、矩形高さを超える部分は描画しない。
	/// @param rect 描画領域
	/// @param text テキスト内容
	/// @param color 描画色
	/// @param fontSize フォントサイズ
	/// @param padX 左右パディング
	/// @param padY 上下パディング
	/// @param lineSpacing 行間倍率（デフォルト1.4）
	void drawTextWrapped(const sgc::Rectf& rect, std::string_view text,
	                      const sgc::Colorf& color, float fontSize = 16.0f,
	                      float padX = 4.0f, float padY = 2.0f,
	                      float lineSpacing = 1.4f);

	/// @brief 高品質テキスト描画（FontAtlasスケーリング使用）
	/// @details BitmapFontの8x8グリフをfontSizeに基づいてスケーリングし、
	///          各ピクセルをscale倍の矩形として描画する。
	///          通常のdrawText()より滑らかな拡大表示が可能。
	/// @param position 描画位置（左上）
	/// @param text テキスト内容
	/// @param color 描画色
	/// @param fontSize フォントサイズ（8の倍数でスケーリング）
	void drawTextHQ(const sgc::Vec2f& position, std::string_view text,
		const sgc::Colorf& color, float fontSize);

	/// @brief 凸多角形を描画する（三角形ファンで分割）
	/// @details 先頭頂点を基点とする三角形ファンで凸多角形を描画する。
	///          頂点数が3未満の場合は何も描画しない。
	/// @param points 頂点座標の配列
	/// @param color 塗りつぶし色
	void drawPolygon(const std::vector<sgc::Vec2f>& points, const sgc::Colorf& color);

	// ══════════════════════════════════════════════════════
	// Phase 1-2: 新規描画プリミティブ
	// ══════════════════════════════════════════════════════

	/// @brief 楕円を描画する
	/// @param center 中心座標
	/// @param radiusX X方向の半径
	/// @param radiusY Y方向の半径
	/// @param color 描画色
	void drawEllipse(const sgc::Vec2f& center, float radiusX, float radiusY,
	                 const sgc::Colorf& color);

	/// @brief リング（ドーナツ）を描画する
	/// @param center 中心座標
	/// @param outerRadius 外側の半径
	/// @param innerRadius 内側の半径
	/// @param color 描画色
	void drawRing(const sgc::Vec2f& center, float outerRadius, float innerRadius,
	              const sgc::Colorf& color);

	/// @brief 水平グラデーション矩形を描画する（左→右）
	/// @param rect 描画領域
	/// @param leftColor 左端の色
	/// @param rightColor 右端の色
	void drawGradientRectH(const sgc::Rectf& rect,
	                       const sgc::Colorf& leftColor, const sgc::Colorf& rightColor);

	/// @brief 4隅個別カラーのグラデーション矩形を描画する
	/// @param rect 描画領域
	/// @param topLeft 左上色
	/// @param topRight 右上色
	/// @param bottomRight 右下色
	/// @param bottomLeft 左下色
	void drawGradientRect4(const sgc::Rectf& rect,
	                       const sgc::Colorf& topLeft, const sgc::Colorf& topRight,
	                       const sgc::Colorf& bottomRight, const sgc::Colorf& bottomLeft);

	/// @brief 角ごとに異なる丸みの角丸矩形を描画する
	/// @param rect 矩形領域
	/// @param color 描画色
	/// @param tl 左上の角丸半径
	/// @param tr 右上の角丸半径
	/// @param br 右下の角丸半径
	/// @param bl 左下の角丸半径
	void drawRoundedRect4(const sgc::Rectf& rect, const sgc::Colorf& color,
	                      float tl, float tr, float br, float bl);

	/// @brief 回転した矩形を描画する
	/// @param rect 矩形領域
	/// @param color 描画色
	/// @param angleDeg 回転角度（度）
	void drawRectRotated(const sgc::Rectf& rect, const sgc::Colorf& color, float angleDeg);

	/// @brief テキストをシャドウ付きで描画する
	/// @param rect 描画領域
	/// @param text テキスト
	/// @param color テキスト色
	/// @param fontSize フォントサイズ
	/// @param shadowColor 影色
	/// @param shadowOffsetX 影のXオフセット
	/// @param shadowOffsetY 影のYオフセット
	/// @param alignH 水平アラインメント
	/// @param alignV 垂直アラインメント
	void drawTextWithShadow(const sgc::Rectf& rect, std::string_view text,
	                         const sgc::Colorf& color, float fontSize,
	                         const sgc::Colorf& shadowColor,
	                         float shadowOffsetX = 2.0f, float shadowOffsetY = 2.0f,
	                         TextAlignH alignH = TextAlignH::Center,
	                         TextAlignV alignV = TextAlignV::Middle);

	/// @brief テキストをアウトライン付きで描画する
	/// @param rect 描画領域
	/// @param text テキスト
	/// @param color テキスト色
	/// @param outlineColor アウトライン色
	/// @param outlineWidth アウトライン幅
	/// @param fontSize フォントサイズ
	/// @param alignH 水平アラインメント
	/// @param alignV 垂直アラインメント
	void drawTextOutlined(const sgc::Rectf& rect, std::string_view text,
	                       const sgc::Colorf& color, const sgc::Colorf& outlineColor,
	                       float outlineWidth = 1.0f, float fontSize = 16.0f,
	                       TextAlignH alignH = TextAlignH::Center,
	                       TextAlignV alignV = TextAlignV::Middle);

	/// @brief 取り消し線付きテキストを描画する
	/// @param rect 描画領域
	/// @param text テキスト
	/// @param color テキスト色
	/// @param fontSize フォントサイズ
	/// @param alignH 水平アラインメント
	/// @param alignV 垂直アラインメント
	void drawTextStrikethrough(const sgc::Rectf& rect, std::string_view text,
	                            const sgc::Colorf& color, float fontSize = 16.0f,
	                            TextAlignH alignH = TextAlignH::Left,
	                            TextAlignV alignV = TextAlignV::Middle);

	/// @brief 擬似ボールド（Faux Bold）でテキストを描画する
	/// @details 1pxオフセットで2回描画することで太字を模倣する
	/// @param rect 描画領域
	/// @param text テキスト
	/// @param color テキスト色
	/// @param fontSize フォントサイズ
	/// @param alignH 水平アラインメント
	/// @param alignV 垂直アラインメント
	void drawTextBold(const sgc::Rectf& rect, std::string_view text,
	                   const sgc::Colorf& color, float fontSize = 16.0f,
	                   TextAlignH alignH = TextAlignH::Left,
	                   TextAlignV alignV = TextAlignV::Middle);

	/// @brief レタースペーシング付きでテキストを描画する
	/// @param position 描画位置（左上）
	/// @param text テキスト
	/// @param color テキスト色
	/// @param fontSize フォントサイズ
	/// @param letterSpacing 文字間スペーシング（ピクセル）
	void drawTextSpaced(const sgc::Vec2f& position, std::string_view text,
	                     const sgc::Colorf& color, float fontSize = 16.0f,
	                     float letterSpacing = 2.0f);

	/// @brief グラデーション矩形を描画する（上→下）
	/// @details topColorからbottomColorへ線形補間した帯を積み重ねて
	///          疑似グラデーション矩形を描画する。
	/// @param rect 描画領域
	/// @param topColor 上端の色
	/// @param bottomColor 下端の色
	void drawGradientRect(const sgc::Rectf& rect,
		const sgc::Colorf& topColor, const sgc::Colorf& bottomColor);

	// ══════════════════════════════════════════════════════
	// Phase 6: 高度な描画プリミティブ
	// ══════════════════════════════════════════════════════

	/// @brief パターンタイプ
	enum class PatternType
	{
		Checkerboard, ///< チェッカーボード
		HStripes,     ///< 水平ストライプ
		VStripes,     ///< 垂直ストライプ
		DiagStripes,  ///< 斜めストライプ
		Dots,         ///< ドットパターン
	};

	/// @brief パターン塗り矩形を描画する
	/// @param rect 描画領域
	/// @param pattern パターンタイプ
	/// @param cellSize セルサイズ（ピクセル）
	/// @param color1 パターン色1
	/// @param color2 パターン色2
	void drawRectPattern(const sgc::Rectf& rect, PatternType pattern,
	                     float cellSize, const sgc::Colorf& color1,
	                     const sgc::Colorf& color2);

	/// @brief 内側シャドウを描画する
	/// @details 矩形の内側に半透明のグラデーション帯を描画して影を表現する
	/// @param rect 対象矩形
	/// @param shadowColor 影の色（通常は半透明の黒）
	/// @param blurSize 影のぼかし幅（ピクセル）
	/// @param offsetX 影のXオフセット
	/// @param offsetY 影のYオフセット
	void drawInnerShadow(const sgc::Rectf& rect,
	                     const sgc::Colorf& shadowColor,
	                     float blurSize = 8.0f,
	                     float offsetX = 0.0f, float offsetY = 0.0f);

	/// @brief フロスト効果（バックドロップブラーの近似）
	/// @details 真のブラーはポストプロセスが必要だが、
	///          半透明の重ね塗りで「すりガラス」風の効果を近似する
	/// @param rect 効果領域
	/// @param tintColor ティント色（通常は半透明の白）
	/// @param layers レイヤー数（多いほど不透明）
	void drawFrostedRect(const sgc::Rectf& rect,
	                     const sgc::Colorf& tintColor,
	                     int layers = 3);

	/// @brief パースペクティブ（台形）変換した矩形を描画する
	/// @details 矩形の上辺または下辺を狭めて奥行き感を出す
	/// @param rect 元の矩形
	/// @param color 描画色
	/// @param vanishTop trueなら上辺を狭める、falseなら下辺
	/// @param strength 変形の強さ（0.0=変形なし、1.0=三角形）
	void drawRectPerspective(const sgc::Rectf& rect, const sgc::Colorf& color,
	                         bool vanishTop = true, float strength = 0.2f);

	/// @brief パースペクティブ（台形）変換した矩形をグラデーション付きで描画する
	/// @param rect 元の矩形
	/// @param topColor 上端色
	/// @param bottomColor 下端色
	/// @param vanishTop trueなら上辺を狭める
	/// @param strength 変形の強さ
	void drawRectPerspectiveGradient(const sgc::Rectf& rect,
	                                 const sgc::Colorf& topColor,
	                                 const sgc::Colorf& bottomColor,
	                                 bool vanishTop = true, float strength = 0.2f);

	/// @brief テクスチャスプライトを描画する（ピクセル単位の矩形描画で近似）
	/// @param texture テクスチャ
	/// @param dstRect 描画先矩形
	void drawSprite(const render::Texture& texture, const sgc::Rectf& dstRect);

	/// @brief RGBA8ピクセルバッファを1つのテクスチャクワッドとしてブリットする
	/// @details 内部でピクセル幅・高さが前回と異なる場合のみGPUテクスチャを再確保する
	///          (ステディステートでは無アロケーション)。サンプリングはポイントフィルタ
	///          (バイリニアなし)。ピクセルバイト順はRGBA: byte[0]=R, byte[1]=G,
	///          byte[2]=B, byte[3]=A。リトルエンディアンメモリ上のuint32_tとしては
	///          0xAABBGGRR と読める。NullDevice/ヘッドレス時はno-op。
	/// @par Usage:
	///   std::array<std::uint32_t, 32 * 32> framebuffer;
	///   // ... fill framebuffer ...
	///   screen.drawPixelGrid({0, 0, 256, 256}, framebuffer.data(), 32, 32);
	/// @param dest 描画先矩形（スクリーン座標）
	/// @param pixels RGBA8ピクセルバッファ（pixelWidth * pixelHeight 要素）
	/// @param pixelWidth バッファ幅（ピクセル数）
	/// @param pixelHeight バッファ高さ（ピクセル数）
	void drawPixelGrid(const sgc::Rectf& dest,
	                   const std::uint32_t* pixels,
	                   int pixelWidth,
	                   int pixelHeight);

	/// @brief RGBA8ピクセルバッファをサンプリングフィルタ指定でブリットする
	/// @details 4-arg オーバーロードと同じ挙動だが、サンプリングフィルタを
	///          明示的に指定する。`PixelArtFilter::Point` を渡すとピクセルアート向け
	///          point sampling（バイリニア無し）、`PixelArtFilter::Linear` は既存挙動と
	///          同等の linear filtering。DX12 path のみ filter を尊重し、それ以外の
	///          backend では filter は無視される（baked-in sampler に従う）。
	///          NullDevice/ヘッドレス時はno-op。
	/// @param dest 描画先矩形（スクリーン座標）
	/// @param pixels RGBA8ピクセルバッファ（pixelWidth * pixelHeight 要素）
	/// @param pixelWidth バッファ幅（ピクセル数）
	/// @param pixelHeight バッファ高さ（ピクセル数）
	/// @param filter サンプリングフィルタ（Linear=デフォルト互換、Point=ピクセルアート）
	void drawPixelGrid(const sgc::Rectf& dest,
	                   const std::uint32_t* pixels,
	                   int pixelWidth,
	                   int pixelHeight,
	                   render::PixelArtFilter filter);

	/// @brief テクスチャスプライトをティント色付きで描画する
	/// @param texture テクスチャ
	/// @param dstRect 描画先矩形
	/// @param tintColor ティント色（テクスチャ色に乗算される）
	void drawSprite(const render::Texture& texture, const sgc::Rectf& dstRect,
	                const sgc::Colorf& tintColor);

	/// @brief スプライトシートの 1 コマ (srcRect) を ティント / 左右反転付きで描く
	/// @details テクスチャ全体ではなく srcRect (ピクセル単位の部分矩形) を dstRect に
	///          描画する。スプライトシートのアニメ 1 フレームを切り出す用途。バッチ＆
	///          カメラ変換に従う (drawPixelGrid と違い transform を尊重)。
	/// @param texture シート全体のテクスチャ
	/// @param dstRect 描画先矩形 (スクリーン座標)
	/// @param srcRect テクスチャ内のソース領域 (ピクセル単位)
	/// @param tintColor ティント色 (テクスチャ色に乗算。既定: 白)
	/// @param flipX 左右反転 (キャラの向き。既定: false)
	void drawSprite(const render::Texture& texture, const sgc::Rectf& dstRect,
	                const sgc::Rectf& srcRect,
	                const sgc::Colorf& tintColor = sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f},
	                bool flipX = false);

	/// @brief UIノードツリーを描画する
	/// @param root UIツリーのルートノード
	/// @param theme 描画に使用するテーマ
	void renderUI(const ui::UINode& root, const ui::UITheme& theme);

	/// @brief クリップ矩形をプッシュする（描画をこの矩形内に制限）
	/// @param rect クリップ矩形
	void pushClipRect(const sgc::Rectf& rect);

	/// @brief クリップ矩形をポップする（前のクリップ状態に戻す）
	void popClipRect();

	/// @brief ブレンドモードを変更する
	/// @details バッチをフラッシュしてからパイプラインのブレンドモードを切り替える。
	void setBlendMode(gfx::BlendMode mode);

	/// @brief 2D affine transform (mitiru::render::Transform2D の alias)。
	/// @details translate/scale/rotate/compose をサポートする 2x3 affine matrix。
	///          詳細は `include/mitiru/render/Transform2D.hpp`。
	using Transform2D = render::Transform2D;

	/// @brief 変換をプッシュする（現在の変換に乗算）
	/// @param tx X平行移動
	/// @param ty Y平行移動
	/// @param sx Xスケール
	/// @param sy Yスケール
	/// @details 後方互換の translate+scale 専用オーバーロード。
	void pushTransform(float tx = 0.0f, float ty = 0.0f,
	                   float sx = 1.0f, float sy = 1.0f);

	/// @brief 任意の2Dアフィン変換をプッシュする（現在の変換に乗算）
	void pushTransform(const Transform2D& t);

	/// @brief ピボット周りの回転をプッシュする便利関数
	/// @param rad 回転角度（ラジアン）
	/// @param pivotX ピボットX（入力座標系）
	/// @param pivotY ピボットY（入力座標系）
	void pushRotation(float rad, float pivotX = 0.0f, float pivotY = 0.0f);

	/// @brief 変換をポップする
	void popTransform();

	/// @brief 矩形に現在の変換を適用する
	/// @param rect 入力矩形
	/// @return 変換後の矩形（軸整列バウンディング）
	[[nodiscard]] sgc::Rectf applyTransform(const sgc::Rectf& rect) const
	{
		return currentTransform().applyBounds(rect);
	}

	/// @brief 座標に現在の変換を適用する
	/// @param pos 入力座標
	/// @return 変換後の座標
	[[nodiscard]] sgc::Vec2f applyTransform(const sgc::Vec2f& pos) const
	{
		return currentTransform().apply(pos);
	}

	/// @brief グループ描画（変換をまとめて適用する）
	/// @param position グループの平行移動オフセット
	/// @param rotationDeg 回転角度（度）— グループ原点（position）まわりで回転
	/// @param drawFn グループ内の描画コールバック
	/// @details translation と rotation を合成した変換をスタックにプッシュしてから
	///          drawFn を呼ぶ。drawFn 内の描画は position を原点としたローカル座標で行う。
	template <typename Fn>
	void drawGroup(const sgc::Vec2f& position, float rotationDeg,
	               Fn&& drawFn)
	{
		constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
		const float rad = rotationDeg * kDegToRad;
		// translate(position) * rotate(rad) — local 原点まわりで回転させ、
		// その後 group 全体を `position` へ平行移動する。
		pushTransform(Transform2D::translate(position.x, position.y) * Transform2D::rotate(rad));
		drawFn(*this);
		popTransform();
	}

	/// @brief フレーム描画を完了し、GPU送信する
	/// @details SpriteBatch/ShapeRendererの蓄積データをRenderPipeline2Dに送る。
	void present();

	/// @brief サーフェス幅を取得する
	[[nodiscard]] int width() const noexcept
	{
		return m_width;
	}

	/// @brief サーフェス高さを取得する
	[[nodiscard]] int height() const noexcept
	{
		return m_height;
	}

	/// @brief フレーム内の描画コール数を取得する
	[[nodiscard]] int drawCallCount() const noexcept
	{
		return m_drawCallCount;
	}

	/// @brief 描画コール数をリセットする
	void resetDrawCallCount() noexcept;

	/// @brief AI 観測用 draw log を有効化 / 無効化する (/api/ai/frame)
	/// @details 有効化時に容量を予約する。記録はフレームリセット以降に積まれる。
	void setDrawLogEnabled(bool enabled)
	{
		m_drawLogEnabled = enabled;
		if (enabled && m_drawLog.capacity() < kDrawLogCap) { m_drawLog.reserve(kDrawLogCap); }
	}

	/// @brief draw log が有効か
	[[nodiscard]] bool drawLogEnabled() const noexcept { return m_drawLogEnabled; }

	/// @brief 当フレームの draw log を取得する
	[[nodiscard]] const std::vector<DrawLogEntry>& drawLog() const noexcept { return m_drawLog; }

	/// @brief 最後に設定されたクリア色を取得する
	[[nodiscard]] const sgc::Colorf& clearColor() const noexcept
	{
		return m_clearColor;
	}

	/// @brief サーフェスサイズを変更する
	/// @param width 新しい幅
	/// @param height 新しい高さ
	void resize(int width, int height) noexcept;

	/// @brief SpriteBatchへの参照を取得する
	[[nodiscard]] render::SpriteBatch& spriteBatch() noexcept
	{
		return m_spriteBatch;
	}

	/// @brief SpriteBatchへのconst参照を取得する
	[[nodiscard]] const render::SpriteBatch& spriteBatch() const noexcept
	{
		return m_spriteBatch;
	}

	/// @brief ShapeRendererへの参照を取得する
	[[nodiscard]] render::ShapeRenderer& shapeRenderer() noexcept
	{
		return m_shapeRenderer;
	}

	/// @brief ShapeRendererへのconst参照を取得する
	[[nodiscard]] const render::ShapeRenderer& shapeRenderer() const noexcept
	{
		return m_shapeRenderer;
	}

private:
	int m_width;                  ///< サーフェス幅
	int m_height;                 ///< サーフェス高さ
	int m_drawCallCount = 0;      ///< 描画コール数
	// フルスクリーン tint オーバーレイ (#31): pushTint で残量セット、advanceTint で減衰、renderTint で重ねる。
	sgc::Colorf m_tintColor     = {0.0f, 0.0f, 0.0f, 0.0f};
	float       m_tintDurSec    = 0.0f;
	float       m_tintRemainSec = 0.0f;
	sgc::Colorf m_clearColor{0.0f, 0.0f, 0.0f, 1.0f};  ///< クリア色
	render::SpriteBatch m_spriteBatch;       ///< スプライトバッチ（現在開いている run のジオメトリ）
	std::uint32_t m_curTexHandle = 0;        ///< 現在の run のテクスチャハンドル（0=頂点カラー, ADR 0009）
	render::ShapeRenderer m_shapeRenderer;   ///< シェイプレンダラー
	render::StyledRectBatch m_styledRectBatch;         ///< SDF矩形バッチ
	render::StyledCircleBatch m_styledCircleBatch;     ///< SDF円/楕円バッチ
	render::StyledShapeRenderer m_styledShapeRenderer;  ///< スタイル付きシェイプ
	render::RenderPipeline2D* m_pipeline = nullptr;  ///< レンダリングパイプライン（非所有）
	validate::DrawCallValidator* m_validator = nullptr; ///< 描画バリデーター（非所有）
	void* m_ttFont = nullptr;                          ///< TrueTypeフォント（型消去、非所有）
	TtDrawFunc m_ttDrawFunc = nullptr;                 ///< TTF描画コールバック
	TtMeasureFunc m_ttMeasureFunc = nullptr;           ///< TTF計測コールバック
	void* m_sdfFont = nullptr;                         ///< SDFフォント（型消去、非所有）
	SdfDrawFunc m_sdfDrawFunc = nullptr;               ///< SDF描画コールバック
	SdfMeasureFunc m_sdfMeasureFunc = nullptr;         ///< SDF計測コールバック
	std::unordered_map<std::string, FontEntry> m_fontRegistry; ///< 名前付きフォントレジストリ
	const FontEntry* m_activeFont = nullptr;           ///< アクティブフォント（非所有）
	std::stack<Transform2D> m_transformStack;           ///< 2D変換スタック

	/// @brief 現在の変換を取得する（スタック空なら恒等変換）
	[[nodiscard]] Transform2D currentTransform() const
	{
		if (m_transformStack.empty()) return Transform2D::identity();
		return m_transformStack.top();
	}

	// ── textured sprite batch ヘルパー（ADR 0009）─────────
	// painter 順を保つため、頂点カラー run と textured run の切替で現バッチを
	// flush+submit する。実装は detail/Screen_Frame.hpp（RenderPipeline2D の
	// 完全型が要るため）。
	/// @brief 現在のバッチ（頂点カラー or textured）を submit し、空で開き直す
	void flushCurrentBatch();
	/// @brief texHandle の textured run を開く（切替時に現バッチを flush）
	void switchToTexture(std::uint32_t texHandle);
	/// @brief textured quad を現在の run に積む（必要なら run を切替）
	void drawSpriteTexturedQuad(std::uint32_t texHandle,
	                            const sgc::Vec2f corners[4],
	                            const sgc::Vec2f uvs[4],
	                            const sgc::Colorf& color);

	// ── transform 対応の内部 emit ヘルパー ─────────────
	// 描画メソッドはこれらを経由することで currentTransform を自動適用する。
	// rotation を持つ変換では、SpriteBatch のAABB矩形では表現不能なため
	// ShapeRenderer 経由で2三角形クワッドとして emit する。

	/// @brief 現在のtransformを適用して矩形をemitする
	void emitRect(const sgc::Rectf& rect, const sgc::Colorf& color)
	{
		// 頂点カラー描画: 開いている textured run があれば閉じて順序を保つ（ADR 0009）
		if (m_curTexHandle != 0) flushCurrentBatch();
		const auto t = currentTransform();
		if (t.isIdentity())
		{
			m_spriteBatch.drawRect(rect, color);
			return;
		}
		if (!t.hasRotation())
		{
			// translation + non-rotated scale のみ: 軸整列矩形のまま
			const auto p0 = t.apply(rect.x(), rect.y());
			const auto p1 = t.apply(rect.x() + rect.width(), rect.y() + rect.height());
			const float nx = std::min(p0.x, p1.x);
			const float ny = std::min(p0.y, p1.y);
			const float nw = std::abs(p1.x - p0.x);
			const float nh = std::abs(p1.y - p0.y);
			m_spriteBatch.drawRect(sgc::Rectf{nx, ny, nw, nh}, color);
			return;
		}
		// 回転を含む: 4隅を変換して2三角形クワッドとして emit
		const auto p0 = t.apply(rect.x(), rect.y());
		const auto p1 = t.apply(rect.x() + rect.width(), rect.y());
		const auto p2 = t.apply(rect.x() + rect.width(), rect.y() + rect.height());
		const auto p3 = t.apply(rect.x(), rect.y() + rect.height());
		m_shapeRenderer.drawTriangle(p0, p1, p2, color);
		m_shapeRenderer.drawTriangle(p0, p2, p3, color);
	}

	/// @brief 現在のtransformを適用して三角形をemitする
	void emitTriangle(const sgc::Vec2f& a, const sgc::Vec2f& b, const sgc::Vec2f& c,
	                  const sgc::Colorf& color)
	{
		const auto t = currentTransform();
		if (t.isIdentity())
		{
			m_shapeRenderer.drawTriangle(a, b, c, color);
			return;
		}
		m_shapeRenderer.drawTriangle(t.apply(a), t.apply(b), t.apply(c), color);
	}

	/// @brief 現在のtransformを適用して線分をemitする
	void emitLine(const sgc::Vec2f& from, const sgc::Vec2f& to,
	              const sgc::Colorf& color, float thickness)
	{
		const auto t = currentTransform();
		if (t.isIdentity())
		{
			m_shapeRenderer.drawLine(from, to, color, thickness);
			return;
		}
		const float th = thickness * t.avgScale();
		m_shapeRenderer.drawLine(t.apply(from), t.apply(to), color, th);
	}

	/// @brief 現在のtransformを適用して4頂点グラデーション矩形をemitする
	void emitGradientRect(const sgc::Rectf& rect,
	                      const sgc::Colorf& tl, const sgc::Colorf& tr,
	                      const sgc::Colorf& br, const sgc::Colorf& bl)
	{
		// 頂点カラー描画: 開いている textured run があれば閉じる（ADR 0009）
		if (m_curTexHandle != 0) flushCurrentBatch();
		const auto t = currentTransform();
		if (t.isIdentity())
		{
			m_spriteBatch.drawRectGradient4(rect, tl, tr, br, bl);
			return;
		}
		const sgc::Vec2f corners[4] = {
			t.apply(rect.x(),                rect.y()),
			t.apply(rect.x() + rect.width(), rect.y()),
			t.apply(rect.x() + rect.width(), rect.y() + rect.height()),
			t.apply(rect.x(),                rect.y() + rect.height()),
		};
		m_spriteBatch.drawQuadGradient4(corners, tl, tr, br, bl);
	}

	/// @brief 現在のtransformを適用して点（座標）を返す
	[[nodiscard]] sgc::Vec2f emitPoint(const sgc::Vec2f& p) const
	{
		return currentTransform().apply(p);
	}

	/// @brief 現在のtransform を適用して値（スカラー長）を返す
	[[nodiscard]] float emitScale(float v) const
	{
		return v * currentTransform().avgScale();
	}

	bool m_softwareFb = false;               ///< ソフトウェアフレームバッファ有効フラグ
	std::vector<std::uint8_t> m_pixels;      ///< ソフトウェアフレームバッファ（RGBA8）

	// ── AI 観測用 draw log (/api/ai/frame) ──────────────────────────
	// ABI 注意: Screen* は DLL 境界を渡る (gameDraw)。既存メンバのオフセットを
	// 変えないため、新規メンバは必ずクラス末尾に追加すること (ABI v14)。
	bool m_drawLogEnabled = false;            ///< draw log の記録フラグ
	std::vector<DrawLogEntry> m_drawLog;      ///< 当フレームの draw log (有効時のみ、capacity 維持で再利用)
	bool m_drawLogSuppress = false;           ///< テキスト内部のグリフ矩形を記録から除外するフラグ
	static constexpr std::size_t kDrawLogCap = 1024; ///< draw log の 1 フレーム上限 (超過分は捨てる)

public:
	/// @brief ソフトウェアフレームバッファを有効化する
	/// @details headlessモードでのピクセル検証に使用する。
	///          present()時に三角形をソフトウェアラスタライズする。
	void enableSoftwareFramebuffer() noexcept
	{
		m_softwareFb = true;
		m_pixels.resize(
			static_cast<std::size_t>(m_width) *
			static_cast<std::size_t>(m_height) * 4, 0);
	}

	/// @brief ソフトウェアフレームバッファが有効か
	[[nodiscard]] bool hasSoftwareFramebuffer() const noexcept
	{
		return m_softwareFb;
	}

	/// @brief ソフトウェアフレームバッファのピクセルデータを取得する
	/// @return RGBA8形式のピクセルデータ（左上起点）
	[[nodiscard]] const std::vector<std::uint8_t>& pixels() const noexcept
	{
		return m_pixels;
	}

	/// @brief 指定座標のピクセル色を取得する
	/// @param x X座標
	/// @param y Y座標
	/// @return RGBA色（範囲外は黒）
	[[nodiscard]] sgc::Colorf pixelAt(int x, int y) const noexcept
	{
		if (x < 0 || x >= m_width || y < 0 || y >= m_height || !m_softwareFb)
		{
			return sgc::Colorf{0.0f, 0.0f, 0.0f, 1.0f};
		}
		const auto idx = static_cast<std::size_t>((y * m_width + x) * 4);
		return sgc::Colorf{
			m_pixels[idx + 0] / 255.0f,
			m_pixels[idx + 1] / 255.0f,
			m_pixels[idx + 2] / 255.0f,
			m_pixels[idx + 3] / 255.0f
		};
	}

private:
	// ── バリデーション・ヘルパー ─────────────────

	// ── Styled draw dispatch ─────────────────────────
	void drawShape(const render::ShapeRect& s, const render::Style& st)
	{
		drawStyledRect(s.rect, st);
	}
	void drawShape(const render::ShapeCircle& s, const render::Style& st)
	{
		drawStyledCircle(s.center, s.radius, st);
	}
	void drawShape(const render::ShapeEllipse& s, const render::Style& st);  // impl below
	void drawShape(const render::ShapeTriangle& s, const render::Style& st)
	{
		m_styledShapeRenderer.drawTriangle(s, st);
		++m_drawCallCount;
	}
	void drawShape(const render::ShapeLine& s, const render::Style& st)
	{
		m_styledShapeRenderer.drawLine(s, st);
		++m_drawCallCount;
	}
	void drawShape(const render::ShapeArc& s, const render::Style& st)
	{
		m_styledShapeRenderer.drawArc(s, st);
		++m_drawCallCount;
	}
	void drawShape(const render::ShapePie& s, const render::Style& st)
	{
		m_styledShapeRenderer.drawPie(s, st);
		++m_drawCallCount;
	}
	void drawShape(const render::ShapeRing& s, const render::Style& st)
	{
		m_styledShapeRenderer.drawRing(s, st);
		++m_drawCallCount;
	}
	void drawShape(const render::ShapePolygon& s, const render::Style& st)
	{
		m_styledShapeRenderer.drawPolygon(s, st);
		++m_drawCallCount;
	}
	void drawShape(const render::ShapePath& s, const render::Style& st)
	{
		m_styledShapeRenderer.drawPath(s, st);
		++m_drawCallCount;
	}

	/// @brief テキスト描画内部のグリフ矩形が draw log を汚さないようにする RAII ガード
	struct DrawLogSuppress
	{
		explicit DrawLogSuppress(Screen& s) noexcept
			: m_s(s), m_prev(s.m_drawLogSuppress) { s.m_drawLogSuppress = true; }
		~DrawLogSuppress() noexcept { m_s.m_drawLogSuppress = m_prev; }
		DrawLogSuppress(const DrawLogSuppress&) = delete;
		DrawLogSuppress& operator=(const DrawLogSuppress&) = delete;
	private:
		Screen& m_s;
		bool m_prev;
	};

	/// @brief draw log へ 1 エントリ記録する（有効時のみ。上限超過は黙って捨てる）
	void recordDrawLog(const sgc::Rectf& bounds, const char* callName, std::string_view text = {})
	{
		if (!m_drawLogEnabled || m_drawLogSuppress || m_drawLog.size() >= kDrawLogCap) { return; }
		DrawLogEntry e{bounds.x(), bounds.y(), bounds.width(), bounds.height(), callName, {}};
		const std::size_t n = std::min(text.size(), sizeof(e.text) - 1);
		if (n > 0) { std::memcpy(e.text, text.data(), n); }
		e.text[n] = '\0';
		m_drawLog.push_back(e);
	}

	/// @brief 描画領域のバリデーションを実行する（バリデーター接続時のみ）
	void validateDrawCall(const sgc::Rectf& bounds, const char* callName)
	{
		if (m_validator) { m_validator->onDrawCall(bounds, callName); }
		recordDrawLog(bounds, callName);
	}

	/// @brief 色値のバリデーションを実行する（バリデーター接続時のみ）
	void validateColor(const sgc::Colorf& color, const char* callName, const sgc::Rectf& bounds)
	{
		if (m_validator) { m_validator->onColor(color, callName, bounds); }
	}

	/// @brief テキスト描画のバリデーションを実行する（バリデーター接続時のみ）
	/// @details text を渡すと draw log にも内容が記録される (AI 観測用)。
	void validateTextDraw(const sgc::Vec2f& pos, float w, float h,
	                      const char* callName, const sgc::Colorf& color,
	                      std::string_view text = {})
	{
		if (m_validator)
		{
			m_validator->onTextDraw(pos, w, h, callName);
			m_validator->onColor(color, callName, sgc::Rectf{pos.x, pos.y, w, h});
		}
		recordDrawLog(sgc::Rectf{pos.x, pos.y, w, h}, callName, text);
	}

	/// @brief テキスト幅超過のバリデーションを実行する（バリデーター接続時のみ）
	void validateTextOverflow(const sgc::Rectf& rect, float availW, float textW, const char* callName)
	{
		if (m_validator)
		{
			m_validator->onDrawCall(rect, callName);
			if (textW > availW) { m_validator->onTextOverflow(availW, textW, callName, rect); }
		}
	}

	/// @brief ソフトウェアラスタライザ: 三角形リストを描画する
	void rasterizeTriangles(
		const std::vector<render::Vertex2D>& verts,
		const std::vector<std::uint32_t>& indices)
	{
		for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
		{
			const auto& v0 = verts[indices[i]];
			const auto& v1 = verts[indices[i + 1]];
			const auto& v2 = verts[indices[i + 2]];
			rasterizeTriangle(v0, v1, v2);
		}
	}

	/// @brief 単一三角形のラスタライズ（バウンディングボックス法）
	void rasterizeTriangle(
		const render::Vertex2D& v0,
		const render::Vertex2D& v1,
		const render::Vertex2D& v2)
	{
		/// バウンディングボックスを計算する
		const float minXf = std::min({v0.position.x, v1.position.x, v2.position.x});
		const float maxXf = std::max({v0.position.x, v1.position.x, v2.position.x});
		const float minYf = std::min({v0.position.y, v1.position.y, v2.position.y});
		const float maxYf = std::max({v0.position.y, v1.position.y, v2.position.y});

		const int minX = std::max(0, static_cast<int>(minXf));
		const int maxX = std::min(m_width - 1, static_cast<int>(maxXf));
		const int minY = std::max(0, static_cast<int>(minYf));
		const int maxY = std::min(m_height - 1, static_cast<int>(maxYf));

		/// エッジ関数による内外判定
		const float dx01 = v1.position.x - v0.position.x;
		const float dy01 = v1.position.y - v0.position.y;
		const float dx12 = v2.position.x - v1.position.x;
		const float dy12 = v2.position.y - v1.position.y;
		const float dx20 = v0.position.x - v2.position.x;
		const float dy20 = v0.position.y - v2.position.y;

		/// 三角形面積（2倍）で退化チェック
		const float area = dx01 * (v2.position.y - v0.position.y) -
		                   dy01 * (v2.position.x - v0.position.x);
		if (std::abs(area) < 0.001f) return;

		const float invArea = 1.0f / area;

		for (int y = minY; y <= maxY; ++y)
		{
			for (int x = minX; x <= maxX; ++x)
			{
				const float px = static_cast<float>(x) + 0.5f;
				const float py = static_cast<float>(y) + 0.5f;

				/// 重心座標を計算する
				const float e0 = (px - v1.position.x) * dy12 -
				                 (py - v1.position.y) * dx12;
				const float e1 = (px - v2.position.x) * dy20 -
				                 (py - v2.position.y) * dx20;
				const float e2 = (px - v0.position.x) * dy01 -
				                 (py - v0.position.y) * dx01;

				/// 三角形の内側判定（スクリーン座標系ではCW三角形の
				/// area>0でedge値が負になるため符号を反転して判定）
				const bool inside = (area > 0.0f)
					? (e0 <= 0.0f && e1 <= 0.0f && e2 <= 0.0f)
					: (e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f);

				if (!inside) continue;

				/// 頂点色の重心補間（edge値とareaの符号が逆なので反転）
				const float w0 = -e0 * invArea;
				const float w1 = -e1 * invArea;
				const float w2 = 1.0f - w0 - w1;

				const float r = w0 * v0.color.r + w1 * v1.color.r + w2 * v2.color.r;
				const float g = w0 * v0.color.g + w1 * v1.color.g + w2 * v2.color.g;
				const float b = w0 * v0.color.b + w1 * v1.color.b + w2 * v2.color.b;
				const float a = w0 * v0.color.a + w1 * v1.color.a + w2 * v2.color.a;

				/// アルファブレンド（SrcAlpha, OneMinusSrcAlpha）
				const auto idx = static_cast<std::size_t>((y * m_width + x) * 4);
				const float dstR = m_pixels[idx + 0] / 255.0f;
				const float dstG = m_pixels[idx + 1] / 255.0f;
				const float dstB = m_pixels[idx + 2] / 255.0f;

				const auto clamp = [](float v) -> std::uint8_t {
					return static_cast<std::uint8_t>(
						std::max(0.0f, std::min(255.0f, v * 255.0f)));
				};

				m_pixels[idx + 0] = clamp(r * a + dstR * (1.0f - a));
				m_pixels[idx + 1] = clamp(g * a + dstG * (1.0f - a));
				m_pixels[idx + 2] = clamp(b * a + dstB * (1.0f - a));
				m_pixels[idx + 3] = 255;
			}
		}
	}

	/// @brief UIノードを再帰的に描画する
	void renderUINode(const ui::UINode& node, const ui::UITheme& theme)
	{
		if (!node.visible()) return;

		const auto& bounds = node.bounds();
		const auto style = theme.styleFor(node.role());

		// 背景を描画
		if (style.background.a > 0.01f)
		{
			drawRect(bounds, style.background);
		}

		// 枠線を描画
		if (style.border.a > 0.01f)
		{
			const float bw = 1.0f;
			drawRect(sgc::Rectf{bounds.x(), bounds.y(), bounds.width(), bw}, style.border);
			drawRect(sgc::Rectf{bounds.x(), bounds.y() + bounds.height() - bw, bounds.width(), bw}, style.border);
			drawRect(sgc::Rectf{bounds.x(), bounds.y(), bw, bounds.height()}, style.border);
			drawRect(sgc::Rectf{bounds.x() + bounds.width() - bw, bounds.y(), bw, bounds.height()}, style.border);
		}

		// role 固有のコンテンツを描画
		switch (node.role())
		{
		case ui::UIRole::Label:
		case ui::UIRole::ScoreLabel:
			if (!node.text().empty())
			{
				drawText({bounds.x() + style.padding, bounds.y() + style.padding},
				         node.text(), style.foreground, style.fontSize);
			}
			break;
		case ui::UIRole::Button:
			if (!node.text().empty())
			{
				const float textW = static_cast<float>(node.text().size()) * style.fontSize;
				const float tx = bounds.x() + (bounds.width() - textW) * 0.5f;
				const float ty = bounds.y() + (bounds.height() - style.fontSize) * 0.5f;
				drawText({tx, ty}, node.text(), style.foreground, style.fontSize);
			}
			break;
		case ui::UIRole::ProgressBar:
		case ui::UIRole::HealthBar:
		{
			const float fill = (node.maxValue() > 0.0f) ? (node.value() / node.maxValue()) : 0.0f;
			const float barW = bounds.width() * std::clamp(fill, 0.0f, 1.0f);
			drawRect(sgc::Rectf{bounds.x(), bounds.y(), barW, bounds.height()}, style.foreground);
			break;
		}
		default:
			break;
		}

		// 子ノードを再帰描画
		for (const auto& child : node.children())
		{
			if (child) renderUINode(*child, theme);
		}
	}

	/// @brief フレームバッファをクリア色でクリアする
	void clearFramebuffer()
	{
		const auto clamp = [](float v) -> std::uint8_t {
			return static_cast<std::uint8_t>(
				std::max(0.0f, std::min(255.0f, v * 255.0f)));
		};
		const auto r = clamp(m_clearColor.r);
		const auto g = clamp(m_clearColor.g);
		const auto b = clamp(m_clearColor.b);
		const auto a = clamp(m_clearColor.a);

		const auto total = static_cast<std::size_t>(m_width) *
		                   static_cast<std::size_t>(m_height);
		for (std::size_t i = 0; i < total; ++i)
		{
			m_pixels[i * 4 + 0] = r;
			m_pixels[i * 4 + 1] = g;
			m_pixels[i * 4 + 2] = b;
			m_pixels[i * 4 + 3] = a;
		}
	}

public:
	/// @brief 今フレーム SW ラスタライズを行うかを設定する (#53 観測フレーム gating)
	/// @details engine が毎フレーム頭で駆動する。false の間は clear/flush/present/
	///          sprite blit が m_pixels に触れず、直前のラスタライズ結果が残る。
	///          SW-FB 無効時は無意味 (windowed には影響しない)。
	void setSoftwareFbActive(bool active) noexcept { m_swFbActive = active; }

	/// @brief 今フレーム SW ラスタライズが有効か
	[[nodiscard]] bool softwareFbActive() const noexcept
	{
		return m_softwareFb && m_swFbActive;
	}

	/// @brief 次フレームの SW ラスタライズを要求する (gating 中の on-demand 消費者用)
	void requestSwRasterizeNext() noexcept { m_swFbWantNext = true; }

	/// @brief on-demand 要求を取り出してクリアする (engine の frame 頭で消費)
	[[nodiscard]] bool consumeSwRasterizeRequest() noexcept
	{
		const bool want = m_swFbWantNext;
		m_swFbWantNext = false;
		return want;
	}

private:
	// ABI 注意: Screen* は DLL 境界を渡る。既存メンバのオフセット維持のため末尾追加 (ABI v15)。
	bool m_swFbActive = true;    ///< 今フレーム SW ラスタライズを行うか (#53)
	bool m_swFbWantNext = false; ///< capture() 等が次フレームのラスタライズを要求したか
};

} // namespace mitiru

/// @brief present()のインライン実装
/// @details RenderPipeline2D.hppをインクルードせずに宣言だけで済むよう、
///          ヘッダー下部で別途インクルードして実装する。
#include <mitiru/render/RenderPipeline2D.hpp>

#include <mitiru/core/detail/Screen_Sprites.hpp>
#include <mitiru/core/detail/Screen_Transform.hpp>
#include <mitiru/core/detail/Screen_Frame.hpp>
#include <mitiru/core/detail/Screen_Shapes.hpp>
#include <mitiru/core/detail/Screen_Effects.hpp>
#include <mitiru/core/detail/Screen_Text.hpp>
#include <mitiru/core/detail/Screen_Styled.hpp>
#include <mitiru/core/detail/Screen_PixelGrid.hpp>

// ── DrawCallValidator のメソッド実装（Screen 完全型が必要） ──────────

inline void mitiru::validate::DrawCallValidator::attach(Screen& screen)
{
	screen.setValidator(this);
	setScreenBounds(screen.width(), screen.height());
}

inline void mitiru::validate::DrawCallValidator::drawDebugOverlay(Screen& screen) const
{
	const sgc::Colorf red{1.0f, 0.0f, 0.0f, 0.5f};
	const sgc::Colorf yellow{1.0f, 1.0f, 0.0f, 0.5f};

	// バリデーターを一時的に無効化してオーバーレイ自体を検証しない
	auto* saved = screen.validator();
	screen.setValidator(nullptr);

	for (const auto& issue : m_issues)
	{
		const auto& color = (issue.severity == IssueSeverity::Error) ? red : yellow;
		const float sw = static_cast<float>(screen.width());
		const float sh = static_cast<float>(screen.height());

		// 画面内に収まる部分だけハイライト
		const float x1 = std::max(0.0f, issue.rect.x());
		const float y1 = std::max(0.0f, issue.rect.y());
		const float x2 = std::min(sw, issue.rect.x() + issue.rect.width());
		const float y2 = std::min(sh, issue.rect.y() + issue.rect.height());
		if (x2 > x1 && y2 > y1)
		{
			screen.drawRect(sgc::Rectf{x1, y1, x2 - x1, y2 - y1}, color);
		}
	}

	screen.setValidator(const_cast<DrawCallValidator*>(saved));
}
