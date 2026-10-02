#pragma once

/// @file DrawCommands.hpp
/// @brief Screen* を DLL 境界から撤去するための POD draw-command wire format (ADR 0025)。
/// `mitiru::Screen` は host 所有の STL コンテナ (SpriteBatch 等) を持つ header-only class
/// であり、DLL 側にインライン展開された Screen メソッド呼び出しは DLL の compiled code が
/// host のコンテナへ直接書き込むという de-facto ABI を生む (build fingerprint でしか
/// 守れない、ENGINE_AUDIT_2026_07_05 H-1/H-4)。本ファイルはその代替として、DLL 側が POD の
/// `DrawCommandBuffer` へコマンドを書くだけの経路 (`mitiru::Canvas`) を提供する。
///
/// v32 でカテゴリ 1 (図形/線/演出塗り)・3 (テキスト)・5 の一部 (sprite(id))・6 (blend/
/// transform/回転) を語彙に追加した (ADR 0025 実装状況参照)。未対応 API は `Canvas` に
/// メソッド自体が無いためコンパイルエラーになる (対応が終わっていない draw を Screen*
/// 経路のまま気付かず残すよりは、無いものは無いと分かる形にする)。

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/math/Vec2.hpp>

#include <mitiru/core/Color.hpp>
#include <mitiru/gfx/GfxTypes.hpp>

namespace mitiru::render { class Texture; }

namespace mitiru::module
{

/// @brief 1 フレームに乗るコマンド数の上限。量産シーン (neon_asteroids 級) はこの経路を
/// 想定していない (ADR 0025 のワーストケース試算 3,000〜5,000 を見る前段階の値)。
constexpr std::uint32_t kMaxDrawCommands = 4096;

/// @brief テキスト/文字列 id を積む pool の総バイト数。
constexpr std::uint32_t kTextPoolBytes = 8192;

/// @brief 文字列を 32bit へ畳む (FNV-1a)。`Canvas::beginObject` の name と host 側の
/// reflect フィールド名 (`FieldDescriptor::name`) を同じ関数で畳むことで、テキスト自体を
/// 境界越しに運ばずに `DrawCommand::sourceId` だけで対応付けられる (ADR 0035 O2)。
constexpr std::uint32_t fnv1a32(std::string_view s) noexcept
{
	std::uint32_t h = 2166136261u;
	for (const unsigned char c : s)
	{
		h ^= c;
		h *= 16777619u;
	}
	return h;
}

/// @brief drawPolygon の頂点を積む pool の総 float 数 (Vec2f 1 個 = 2 float)。
constexpr std::uint32_t kPointPoolFloats = 4096;

/// @brief このバッチで対応するコマンド種。値は wire 上に出るので既存値は変更しないこと
/// (追加は末尾のみ)。
enum class DrawCmdKind : std::uint16_t
{
	PushTransform    = 1,   ///< applyCamera / 手動 transform 開始
	PopTransform     = 2,   ///< endCamera
	PushRotation     = 3,   ///< pushRotation (ピボット回転)
	SetBlendMode     = 4,   ///< setBlendMode
	Rect             = 5,   ///< 単色矩形
	RectFrame        = 6,   ///< 矩形の枠線
	RoundedRect      = 7,   ///< 角丸矩形
	RoundedRectFrame = 8,   ///< 角丸矩形の枠線
	GradientRect     = 9,   ///< 上下グラデーション矩形
	GradientRectH    = 10,  ///< 左右グラデーション矩形
	Circle           = 11,  ///< 塗りつぶし円
	CircleFrame      = 12,  ///< 円の枠線
	Ellipse          = 13,  ///< 楕円
	Ring             = 14,  ///< リング（ドーナツ）
	Line             = 15,  ///< 線分
	DashedLine       = 16,  ///< 破線
	Triangle         = 17,  ///< 塗りつぶし三角形
	Polygon          = 18,  ///< 凸多角形 (pointPool 参照)
	GlowLine         = 19,  ///< 発光線分
	GlowRing         = 20,  ///< 発光リング
	TextInRect       = 21,  ///< 矩形内アラインメント付きテキスト
	TextClipped      = 22,  ///< 矩形内クリップテキスト
	TextWrapped      = 23,  ///< 矩形内ワードラップテキスト
	Sprite           = 24,  ///< テクスチャスプライト (src rect + tint + flipX)
	SpriteById       = 25,  ///< id 文字列によるスプライト (textPool 参照。sprite(id) 相当)
	SpriteRectById   = 26,  ///< id 文字列 + 明示 dst/src rect + tint + flipX (`registerTexture` 経由)
	SceneFieldMap    = 27,  ///< 非描画。`beginObject(name, fieldX, fieldY)` が乗せるドラッグ書き戻し先
	                        ///< フィールド名 (ADR 0035 O2/O3 追記)。textPool に "fieldX\0fieldY" を積む
};

/// @brief 1 描画コマンド (POD、固定長)。ポインタを含まないので、memcpy で境界を跨げる
/// (`textureHandle` だけは例外で、ポインタ由来の識別子を運ぶ。下記 note 参照)。
/// payload の読み替えは kind ごとに固定 (drainDrawCommands 参照)。
struct DrawCommand
{
	DrawCmdKind   kind{};
	std::uint16_t flags{};        ///< kind 別 (align 2bit×2 / flipX 1bit / BlendMode 値 / segments 等)
	std::uint32_t textOffset{};   ///< textPool 内オフセット (bytes)。文字列コマンド用
	std::uint32_t textLen{};      ///< textPool 参照長 (bytes)
	std::uint32_t pointOffset{};  ///< pointPool 内オフセット (float 単位)。Polygon 用
	std::uint32_t pointCount{};   ///< pointPool 参照点数 (Vec2f 単位)
	float         p[8]{};         ///< kind 別の汎用 payload (座標・半径・太さ等。下記コメント参照)
	float         colorA[4]{};    ///< 主色 (GradientRect は上端/左端、Sprite はチント)
	float         colorB[4]{};    ///< GradientRect/GradientRectH の下端/右端色のみ使用

	/// @brief `Canvas::beginObject(name)`〜`endObject()` の区間内で積まれたコマンドに乗る
	/// `fnv1a32(name)`。0 = 未タグ (既定)。分岐エディタのシーンビュー (ADR 0035 O2) が、
	/// どの描画がどの reflect フィールド由来かをピッキングするためだけに使う。textureHandle
	/// 直前の alignment padding (colorB 末尾から 8-byte 境界までの 4 byte、旧 v32 では未使用の
	/// 隙間) にちょうど収まるため `sizeof(DrawCommand)` は変わらない (下の static_assert 参照)。
	std::uint32_t sourceId{};

	/// @brief Sprite のみ: `&texture` のアドレスをそのまま識別子として運ぶ。
	/// @note host 事前登録の u32 handle (ADR 0025 の本来案) ではなくアドレス直運びなので、
	///       host 側 drain が Texture* へ読み戻せるかは host/DLL が同一ビルド構成であることに
	///       依存する (GameMemory の bytes 解釈と同じ前提。引き続き fingerprint で守る)。
	///       文字列 id 経由の SpriteById はこの制約を持たない (textPool 参照のみ)。
	std::uint64_t textureHandle{};
};
static_assert(sizeof(DrawCommand) == 96, "DrawCommand layout pinned (v32 wire format)");

/// @brief 1 フレーム分のコマンド列 + 可変長データ pool。host 所有・毎フレーム reset・
/// DLL は `Canvas` 経由で積むだけ・reader は `[0, count)` / `[0, *PoolUsed)` しか読まない
/// (FrameIntents と同じ規約)。
struct DrawCommandBuffer
{
	std::uint32_t count{};
	std::uint32_t droppedCount{};   ///< コマンド上限あふれで捨てた数 (観測可能にする)
	std::uint32_t textPoolUsed{};   ///< bytes
	std::uint32_t pointPoolUsed{};  ///< float 数
	DrawCommand   commands[kMaxDrawCommands]{};
	char          textPool[kTextPoolBytes]{};
	float         pointPool[kPointPoolFloats]{};
};

/// @brief `Canvas::applyCamera` が画面中心を計算するための read-only 情報。
/// InputSnapshot::logicalW/H (v21) の再供給。draw 時点で host が組み立てて渡す。
struct DrawContext
{
	std::uint16_t logicalW{};
	std::uint16_t logicalH{};
};

/// @brief バッファへ 1 コマンド追記する。あふれたら該当コマンドごと捨てて `droppedCount` を
/// 増やす。入力が同じなら捨てる位置も毎回同じなので決定論は保たれる。
inline void pushDrawCommand(DrawCommandBuffer& buf, const DrawCommand& cmd) noexcept
{
	if (buf.count >= kMaxDrawCommands) { ++buf.droppedCount; return; }
	buf.commands[buf.count++] = cmd;
}

/// @brief 文字列を textPool へコピーし (offset, len) を返す。あふれたら false を返す
/// (呼び出し元はコマンド自体を捨てる。文字の途中で切れて、気づかないうちに文字列がおかしくなるのを避けるため)。
[[nodiscard]] inline bool pushTextPool(DrawCommandBuffer& buf, std::string_view text,
                                       std::uint32_t& outOffset, std::uint32_t& outLen) noexcept
{
	if (buf.textPoolUsed + text.size() > kTextPoolBytes) { return false; }
	outOffset = buf.textPoolUsed;
	outLen    = static_cast<std::uint32_t>(text.size());
	std::memcpy(buf.textPool + outOffset, text.data(), text.size());
	buf.textPoolUsed += outLen;
	return true;
}

/// @brief Vec2f 頂点列を pointPool へコピーし (offset, count) を返す。あふれたら false。
[[nodiscard]] inline bool pushPointPool(DrawCommandBuffer& buf, const std::vector<sgc::Vec2f>& pts,
                                        std::uint32_t& outOffset, std::uint32_t& outCount) noexcept
{
	const std::uint32_t floats = static_cast<std::uint32_t>(pts.size()) * 2u;
	if (buf.pointPoolUsed + floats > kPointPoolFloats) { return false; }
	outOffset = buf.pointPoolUsed;
	outCount  = static_cast<std::uint32_t>(pts.size());
	for (std::size_t i = 0; i < pts.size(); ++i)
	{
		buf.pointPool[buf.pointPoolUsed + i * 2 + 0] = pts[i].x;
		buf.pointPool[buf.pointPoolUsed + i * 2 + 1] = pts[i].y;
	}
	buf.pointPoolUsed += floats;
	return true;
}

/// @brief テキストのアラインメント (`Screen::TextAlignH/V` と同じ意味・並び)。Canvas は
/// `Screen` を include しない (ADR 0025 のコンパイル時間短縮の狙い) ので独立に定義する。
/// `Canvas` が同名の nested alias を持つことで、テンプレート越しに `Surface::TextAlignH`
/// の形で共有コードから参照できる。
enum class TextAlignH : std::uint8_t { Left = 0, Center = 1, Right = 2 };
enum class TextAlignV : std::uint8_t { Top = 0, Middle = 1, Bottom = 2 };

namespace detail
{
/// @brief `Canvas::registerTexture` が書き込む DLL ローカルの id 表。key は Texture の
/// アドレス、値は sprite id (SpriteCache の `assets/sprites/<id>.png` 規約に乗る名前)。
/// **境界を跨がない** (DLL プロセス内の静的領域のみ。host には id 文字列だけが
/// `SpriteRectById` コマンド経由で渡る。ADR 0025 の textureHandle アドレス直運びを
/// 置き換える経路)。
inline std::unordered_map<const void*, std::string>& textureIdRegistry() noexcept
{
	static std::unordered_map<const void*, std::string> registry;
	return registry;
}
}  // namespace detail

}  // namespace mitiru::module

namespace mitiru
{

/// @brief `Screen&` の代わりに DLL 境界へ渡す POD writer (ADR 0025)。同名メソッドを持つのは
/// `Screen` と呼び出し側コードを共有するため (`examples/**` はテンプレート関数/メンバを
/// `Screen`/`Canvas` 両方へ実体化する)。対応語彙は `DrawCommands.hpp` の説明を参照。
/// per-frame の値渡しで構築し、DLL 側は保持しない。
class Canvas
{
public:
	using TextAlignH = module::TextAlignH;
	using TextAlignV = module::TextAlignV;

	Canvas(module::DrawCommandBuffer& buf, const module::DrawContext& ctx) noexcept
		: m_buf(&buf), m_ctx(ctx) {}

	// ── 分岐エディタ用ソースタグ (ADR 0035 O2) ───
	/// @brief 以後 `endObject()` までに積むコマンドへ `fnv1a32(name)` を乗せる。
	/// `name` は host 側 reflect フィールド名 (`/api/ai/state` のキーと同じ dotted 名) と
	/// 一致させる規約。ネスト不可 (呼び直しは前の tag を上書きする、スタックを持たない)。
	void beginObject(std::string_view name) noexcept { m_currentSourceId = module::fnv1a32(name); }
	/// @brief `name+".x"/".y"` 規約 (struct フィールド) を満たせない場合の明示版。`fieldX`/`fieldY`
	/// はドラッグが `PUT /api/ai/state` へ書く実際のキー名 (beko_run の px/py のような分離 scalar 用、
	/// ADR 0035 O2/O3 追記)。`DrawCommand` 自体のレイアウトは変えず、textPool 経由の非描画
	/// マーカーコマンド (`SceneFieldMap`) 1 個を積むだけ (ABI 変更なし)。textPool が尽きていれば
	/// 何も知らせずに諦める (通常のタグ付けは働くので枠自体は出る。ドラッグの書き戻し先だけが推測できなくなる)。
	void beginObject(std::string_view name, std::string_view fieldX, std::string_view fieldY) noexcept
	{
		beginObject(name);
		std::string joined;
		joined.reserve(fieldX.size() + fieldY.size() + 1);
		joined.append(fieldX);
		joined.push_back('\0');
		joined.append(fieldY);
		module::DrawCommand c{};
		c.kind = module::DrawCmdKind::SceneFieldMap;
		if (!module::pushTextPool(*m_buf, joined, c.textOffset, c.textLen)) { return; }
		push(c);
	}
	/// @brief タグ付けを終える (以後のコマンドは sourceId=0 = 未タグに戻る)。
	void endObject() noexcept { m_currentSourceId = 0; }

	// ── 変換・カメラ ────────────────────────────────────────────
	void pushTransform(float tx = 0.0f, float ty = 0.0f, float sx = 1.0f, float sy = 1.0f) noexcept
	{
		module::DrawCommand c{};
		c.kind = module::DrawCmdKind::PushTransform;
		c.p[0] = tx; c.p[1] = ty; c.p[2] = sx; c.p[3] = sy;
		push(c);
	}
	void popTransform() noexcept
	{
		module::DrawCommand c{};
		c.kind = module::DrawCmdKind::PopTransform;
		push(c);
	}
	/// @brief `Screen::pushRotation` と同じくピボット周りの回転をプッシュする (ラジアン)。
	void pushRotation(float rad, float pivotX = 0.0f, float pivotY = 0.0f) noexcept
	{
		module::DrawCommand c{};
		c.kind = module::DrawCmdKind::PushRotation;
		c.p[0] = rad; c.p[1] = pivotX; c.p[2] = pivotY;
		push(c);
	}
	/// @brief `Screen::applyCamera` と同じ計算 (注視点が画面中央に来る) を PushTransform
	/// コマンドとして積む。座標変換を実際に適用するのは host drain 側の `Screen::pushTransform`
	/// なので、Canvas はスタックを持たない。
	void applyCamera(float camX, float camY, float zoom = 1.0f) noexcept
	{
		const float cx = static_cast<float>(m_ctx.logicalW) * 0.5f;
		const float cy = static_cast<float>(m_ctx.logicalH) * 0.5f;
		pushTransform(cx - camX * zoom, cy - camY * zoom, zoom, zoom);
	}
	/// @brief applyCamera を外す (popTransform の別名。Screen 版と対で読めるように)。
	void endCamera() noexcept { popTransform(); }

	void setBlendMode(gfx::BlendMode mode) noexcept
	{
		module::DrawCommand c{};
		c.kind  = module::DrawCmdKind::SetBlendMode;
		c.flags = static_cast<std::uint16_t>(mode);
		push(c);
	}

	// ── 図形 ────────────────────────────────────────────────────
	void drawRect(const sgc::Rectf& rect, const sgc::Colorf& color) noexcept
	{
		module::DrawCommand c{};
		c.kind = module::DrawCmdKind::Rect;
		writeRect(c.p, rect);
		writeColor(c.colorA, color);
		push(c);
	}
	void drawRect(float x, float y, float w, float h, const sgc::Colorf& color) noexcept
	{
		drawRect(sgc::Rectf{x, y, w, h}, color);
	}
	void drawRectCentered(float cx, float cy, float w, float h, const sgc::Colorf& color) noexcept
	{
		drawRect(sgc::Rectf{cx - w * 0.5f, cy - h * 0.5f, w, h}, color);
	}
	/// @note `Screen::fillScreen` と同じく shake 等の余白を覆うよう少し大きめに描く。
	void fillScreen(const sgc::Colorf& color) noexcept
	{
		drawRect(sgc::Rectf{-128.0f, -128.0f,
		                    static_cast<float>(m_ctx.logicalW) + 256.0f,
		                    static_cast<float>(m_ctx.logicalH) + 256.0f}, color);
	}
	void drawRectFrame(const sgc::Rectf& rect, const sgc::Colorf& color, float thickness = 1.0f) noexcept
	{
		module::DrawCommand c{};
		c.kind = module::DrawCmdKind::RectFrame;
		writeRect(c.p, rect);
		c.p[4] = thickness;
		writeColor(c.colorA, color);
		push(c);
	}
	void drawRoundedRect(const sgc::Rectf& rect, const sgc::Colorf& color, float radius = 8.0f) noexcept
	{
		module::DrawCommand c{};
		c.kind = module::DrawCmdKind::RoundedRect;
		writeRect(c.p, rect);
		c.p[4] = radius;
		writeColor(c.colorA, color);
		push(c);
	}
	void drawRoundedRectFrame(const sgc::Rectf& rect, const sgc::Colorf& color,
	                          float radius = 8.0f, float thickness = 1.0f) noexcept
	{
		module::DrawCommand c{};
		c.kind = module::DrawCmdKind::RoundedRectFrame;
		writeRect(c.p, rect);
		c.p[4] = radius; c.p[5] = thickness;
		writeColor(c.colorA, color);
		push(c);
	}
	void drawGradientRect(const sgc::Rectf& rect,
	                      const sgc::Colorf& topColor, const sgc::Colorf& bottomColor) noexcept
	{
		module::DrawCommand c{};
		c.kind = module::DrawCmdKind::GradientRect;
		writeRect(c.p, rect);
		writeColor(c.colorA, topColor);
		writeColor(c.colorB, bottomColor);
		push(c);
	}
	void drawGradientRectH(const sgc::Rectf& rect,
	                       const sgc::Colorf& leftColor, const sgc::Colorf& rightColor) noexcept
	{
		module::DrawCommand c{};
		c.kind = module::DrawCmdKind::GradientRectH;
		writeRect(c.p, rect);
		writeColor(c.colorA, leftColor);
		writeColor(c.colorB, rightColor);
		push(c);
	}
	void drawCircle(const sgc::Vec2f& center, float radius, const sgc::Colorf& color) noexcept
	{
		module::DrawCommand c{};
		c.kind = module::DrawCmdKind::Circle;
		c.p[0] = center.x; c.p[1] = center.y; c.p[2] = radius;
		writeColor(c.colorA, color);
		push(c);
	}
	void fillCircle(float cx, float cy, float r, const sgc::Colorf& color) noexcept
	{
		drawCircle(sgc::Vec2f{cx, cy}, r, color);
	}
	void drawCircleFrame(const sgc::Vec2f& center, float radius,
	                     const sgc::Colorf& color, float thickness = 2.0f) noexcept
	{
		module::DrawCommand c{};
		c.kind = module::DrawCmdKind::CircleFrame;
		c.p[0] = center.x; c.p[1] = center.y; c.p[2] = radius; c.p[3] = thickness;
		writeColor(c.colorA, color);
		push(c);
	}
	void drawEllipse(const sgc::Vec2f& center, float radiusX, float radiusY,
	                 const sgc::Colorf& color) noexcept
	{
		module::DrawCommand c{};
		c.kind = module::DrawCmdKind::Ellipse;
		c.p[0] = center.x; c.p[1] = center.y; c.p[2] = radiusX; c.p[3] = radiusY;
		writeColor(c.colorA, color);
		push(c);
	}
	void drawRing(const sgc::Vec2f& center, float outerRadius, float innerRadius,
	              const sgc::Colorf& color) noexcept
	{
		module::DrawCommand c{};
		c.kind = module::DrawCmdKind::Ring;
		c.p[0] = center.x; c.p[1] = center.y; c.p[2] = outerRadius; c.p[3] = innerRadius;
		writeColor(c.colorA, color);
		push(c);
	}
	void drawLine(const sgc::Vec2f& from, const sgc::Vec2f& to,
	              const sgc::Colorf& color, float thickness = 1.0f) noexcept
	{
		module::DrawCommand c{};
		c.kind = module::DrawCmdKind::Line;
		c.p[0] = from.x; c.p[1] = from.y; c.p[2] = to.x; c.p[3] = to.y; c.p[4] = thickness;
		writeColor(c.colorA, color);
		push(c);
	}
	void line(float x0, float y0, float x1, float y1, const sgc::Colorf& color, float thickness = 2.0f) noexcept
	{
		drawLine(sgc::Vec2f{x0, y0}, sgc::Vec2f{x1, y1}, color, thickness);
	}
	void drawDashedLine(const sgc::Vec2f& from, const sgc::Vec2f& to,
	                    float thickness, float dashLen, float gapLen,
	                    const sgc::Colorf& color) noexcept
	{
		module::DrawCommand c{};
		c.kind = module::DrawCmdKind::DashedLine;
		c.p[0] = from.x; c.p[1] = from.y; c.p[2] = to.x; c.p[3] = to.y;
		c.p[4] = thickness; c.p[5] = dashLen; c.p[6] = gapLen;
		writeColor(c.colorA, color);
		push(c);
	}
	void dashedLine(float x1, float y1, float x2, float y2, const sgc::Colorf& color,
	                float width = 2.0f, float dashLen = 9.0f, float gapLen = 6.0f) noexcept
	{
		drawDashedLine(sgc::Vec2f{x1, y1}, sgc::Vec2f{x2, y2}, width, dashLen, gapLen, color);
	}
	void drawTriangle(const sgc::Vec2f& p0, const sgc::Vec2f& p1,
	                  const sgc::Vec2f& p2, const sgc::Colorf& color) noexcept
	{
		module::DrawCommand c{};
		c.kind = module::DrawCmdKind::Triangle;
		c.p[0] = p0.x; c.p[1] = p0.y; c.p[2] = p1.x; c.p[3] = p1.y; c.p[4] = p2.x; c.p[5] = p2.y;
		writeColor(c.colorA, color);
		push(c);
	}
	/// @brief 凸多角形を描く。頂点は pointPool へコピーする (あふれたらコマンドごと捨てる)。
	void drawPolygon(const std::vector<sgc::Vec2f>& points, const sgc::Colorf& color) noexcept
	{
		module::DrawCommand c{};
		c.kind = module::DrawCmdKind::Polygon;
		if (!module::pushPointPool(*m_buf, points, c.pointOffset, c.pointCount))
		{
			++m_buf->droppedCount;
			return;
		}
		writeColor(c.colorA, color);
		push(c);
	}
	void glowLine(float x1, float y1, float x2, float y2, const sgc::Colorf& color,
	              float coreWidth = 2.0f, float glowWidth = 4.4f) noexcept
	{
		module::DrawCommand c{};
		c.kind = module::DrawCmdKind::GlowLine;
		c.p[0] = x1; c.p[1] = y1; c.p[2] = x2; c.p[3] = y2; c.p[4] = coreWidth; c.p[5] = glowWidth;
		writeColor(c.colorA, color);
		push(c);
	}
	void glowRing(float cx, float cy, float r, const sgc::Colorf& color,
	              float coreWidth = 2.0f, float glowWidth = 4.4f, int segments = 20) noexcept
	{
		module::DrawCommand c{};
		c.kind  = module::DrawCmdKind::GlowRing;
		c.flags = static_cast<std::uint16_t>(segments < 0 ? 0 : segments);
		c.p[0] = cx; c.p[1] = cy; c.p[2] = r; c.p[3] = coreWidth; c.p[4] = glowWidth;
		writeColor(c.colorA, color);
		push(c);
	}

	// ── テキスト ────────────────────────────────────────────────
	// @note measureText / textFitsInRect (同期 query) は POD 一方向バッファでは提供できない
	// (ADR 0025 「measureText 問題」)。中央寄せ等のレイアウトは drawTextInRect の alignH/alignV
	// に任せること (Canvas はこの縮退を強制することで判断を分岐させない)。

	void text(std::string_view str, float x, float y,
	          const sgc::Colorf& color = sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f},
	          float fontSize = 18.0f) noexcept
	{
		drawTextInRect(sgc::Rectf{x, y, 100000.0f, fontSize * 1.6f}, str, color, fontSize,
		               TextAlignH::Left, TextAlignV::Top, 0.0f, 0.0f);
	}
	void drawTextInRect(const sgc::Rectf& rect, std::string_view text,
	                     const sgc::Colorf& color, float fontSize = 16.0f,
	                     TextAlignH alignH = TextAlignH::Left,
	                     TextAlignV alignV = TextAlignV::Top,
	                     float padX = 4.0f, float padY = 2.0f) noexcept
	{
		module::DrawCommand c{};
		c.kind = module::DrawCmdKind::TextInRect;
		if (!module::pushTextPool(*m_buf, text, c.textOffset, c.textLen)) { ++m_buf->droppedCount; return; }
		c.flags = static_cast<std::uint16_t>(static_cast<std::uint8_t>(alignH))
		        | static_cast<std::uint16_t>(static_cast<std::uint8_t>(alignV) << 2);
		writeRect(c.p, rect);
		c.p[4] = fontSize; c.p[5] = padX; c.p[6] = padY;
		writeColor(c.colorA, color);
		push(c);
	}
	void drawTextClipped(const sgc::Rectf& rect, std::string_view text,
	                      const sgc::Colorf& color, float fontSize = 16.0f,
	                      float padX = 0.0f, float padY = 0.0f) noexcept
	{
		module::DrawCommand c{};
		c.kind = module::DrawCmdKind::TextClipped;
		if (!module::pushTextPool(*m_buf, text, c.textOffset, c.textLen)) { ++m_buf->droppedCount; return; }
		writeRect(c.p, rect);
		c.p[4] = fontSize; c.p[5] = padX; c.p[6] = padY;
		writeColor(c.colorA, color);
		push(c);
	}
	void drawTextWrapped(const sgc::Rectf& rect, std::string_view text,
	                      const sgc::Colorf& color, float fontSize = 16.0f,
	                      float padX = 4.0f, float padY = 2.0f,
	                      float lineSpacing = 1.4f) noexcept
	{
		module::DrawCommand c{};
		c.kind = module::DrawCmdKind::TextWrapped;
		if (!module::pushTextPool(*m_buf, text, c.textOffset, c.textLen)) { ++m_buf->droppedCount; return; }
		writeRect(c.p, rect);
		c.p[4] = fontSize; c.p[5] = padX; c.p[6] = padY; c.p[7] = lineSpacing;
		writeColor(c.colorA, color);
		push(c);
	}

	// ── スプライト ──────────────────────────────────────────────
	/// @brief `texture` を過去に `registerTexture` した id で覚えていれば、その id で
	/// `SpriteRectById` を積む (アドレスは境界を跨がない)。未登録 (動的生成テクスチャ等) は
	/// 従来どおりアドレス直運びの `Sprite` へ後退する (host/DLL 同一 build 構成が前提。
	/// fingerprint で守る。ADR 0025 の既存 TODO のまま)。
	void drawSprite(const render::Texture& texture, const sgc::Rectf& dstRect,
	                const sgc::Rectf& srcRect,
	                const sgc::Colorf& tintColor = sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f},
	                bool flipX = false) noexcept
	{
		const auto& registry = module::detail::textureIdRegistry();
		if (const auto it = registry.find(static_cast<const void*>(&texture)); it != registry.end())
		{
			drawSprite(it->second.c_str(), dstRect, srcRect, tintColor, flipX);
			return;
		}
		module::DrawCommand c{};
		c.kind  = module::DrawCmdKind::Sprite;
		c.flags = flipX ? 1u : 0u;
		c.p[0] = dstRect.x(); c.p[1] = dstRect.y(); c.p[2] = dstRect.width(); c.p[3] = dstRect.height();
		c.p[4] = srcRect.x(); c.p[5] = srcRect.y(); c.p[6] = srcRect.width(); c.p[7] = srcRect.height();
		writeColor(c.colorA, tintColor);
		c.textureHandle = reinterpret_cast<std::uint64_t>(&texture);
		push(c);
	}
	/// @brief id 文字列 (SpriteCache 規約) + 明示 dst/src rect + tint + flipX。`sprite(id,...)`
	/// (等倍のみ) の全機能版で、`drawSprite(Texture&,...)` と違い id しか境界を跨がない。
	void drawSprite(const char* id, const sgc::Rectf& dstRect, const sgc::Rectf& srcRect,
	                const sgc::Colorf& tintColor = sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f},
	                bool flipX = false) noexcept
	{
		module::DrawCommand c{};
		c.kind = module::DrawCmdKind::SpriteRectById;
		if (!module::pushTextPool(*m_buf, std::string_view{id}, c.textOffset, c.textLen))
		{
			++m_buf->droppedCount;
			return;
		}
		c.flags = flipX ? 1u : 0u;
		c.p[0] = dstRect.x(); c.p[1] = dstRect.y(); c.p[2] = dstRect.width(); c.p[3] = dstRect.height();
		c.p[4] = srcRect.x(); c.p[5] = srcRect.y(); c.p[6] = srcRect.width(); c.p[7] = srcRect.height();
		writeColor(c.colorA, tintColor);
		push(c);
	}
	/// @brief `texture` の以後の `drawSprite(texture,...)` 呼び出しを id ベース
	/// (`SpriteRectById`) へ切り替える。`id` は SpriteCache 規約 (`assets/sprites/<id>.png`、
	/// DLL 隣接) に従うファイル名。DLL ローカルで完結する登録であり host には通知しない
	/// (host は draw コマンドに乗った id 文字列だけを見る)。
	void registerTexture(const render::Texture& texture, const char* id) noexcept
	{
		module::detail::textureIdRegistry()[static_cast<const void*>(&texture)] = id;
	}
	/// @brief `Screen::sprite(id, x, y[, scale])` と同じ id から Texture への解決を host に
	/// 委ねる版。アドレスを運ばないため beko_run の `Sprite` コマンドより制約が緩く、動的に
	/// 生成/破棄される Texture でも安全。ソース矩形指定やチントには対応しない (sprite() の
	/// sugar と同じ縮退)。
	void sprite(const char* id, float x, float y, float scale = 1.0f) noexcept
	{
		module::DrawCommand c{};
		c.kind = module::DrawCmdKind::SpriteById;
		if (!module::pushTextPool(*m_buf, std::string_view{id}, c.textOffset, c.textLen))
		{
			++m_buf->droppedCount;
			return;
		}
		c.p[0] = x; c.p[1] = y; c.p[2] = scale;
		push(c);
	}

private:
	/// @brief 全 push 経路の唯一の出口。現在の `beginObject` タグを毎コマンドに乗せてから push する (ADR 0035 O2)。
	void push(module::DrawCommand& c) noexcept
	{
		c.sourceId = m_currentSourceId;
		module::pushDrawCommand(*m_buf, c);
	}
	static void writeRect(float (&dst)[8], const sgc::Rectf& r) noexcept
	{
		dst[0] = r.x(); dst[1] = r.y(); dst[2] = r.width(); dst[3] = r.height();
	}
	static void writeColor(float (&dst)[4], const sgc::Colorf& c) noexcept
	{
		dst[0] = c.r; dst[1] = c.g; dst[2] = c.b; dst[3] = c.a;
	}

	module::DrawCommandBuffer* m_buf;
	module::DrawContext        m_ctx;
	std::uint32_t              m_currentSourceId{0};  ///< beginObject/endObject (ADR 0035 O2)
};

}  // namespace mitiru
