#pragma once

/// @file TextLayout.hpp
/// @brief 縦書き・横書きの組版。字形の位置だけを決め、画面や GPU には触れない。
/// @details 描画は ScreenFont::drawLayout が行うため、画面なしで位置を検査できる。
///
/// 縦書き (UAX #50 と JLReq の最小限):
/// - 立てる字 (U) は縦組みの字形で 1 字ずつ下へ進める。Tu と Tr は書体の vert 字形を使い、無ければ Tu は立て、Tr は寝かせる
/// - 寝かせる字 (R、欧文の語) は時計回りに 90 度回し、語ごとに 1 つの塊で進める
/// - 縦中横: ASCII 数字 2 桁と !! !? ?! ?? は 1 字分の中に横に並べる。幅が足りなければ全体を縮める。1 桁の数字は立てる
/// - 禁則: 行頭禁止の字は前の字と一緒に次の行へ送り、行末禁止の字は次の行へ送る。、。だけは行の外へぶら下げる
/// - ルビ: 親文字の右 (縦) か上 (横) に小さく置く。親文字とルビの長さが違うときは、短い側を均等に割り付ける
///
/// 基準は em (全角 1 字の字送り) で、fontSize (ascender から descender まで) ではない。

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <mitiru/text/FontFace.hpp>

namespace mitiru::text
{

enum class Writing : std::uint8_t
{
	Horizontal = 0, ///< 左から右へ、行は上から下へ
	VerticalRl = 1, ///< 上から下へ、行は右から左へ
	VerticalLr = 2, ///< 上から下へ、行は左から右へ
};

struct LayoutOptions
{
	Writing writing = Writing::VerticalRl;
	float fontSize = 16.0f;     ///< FontFace の pixelSize と同じ意味
	float extent = 0.0f;        ///< 1 行の長さ (縦書きは高さ、横書きは幅)。0 以下なら折り返さない
	float crossExtent = 0.0f;   ///< 行を並べる方向の長さ (縦書きは幅)。入りきらない行は捨てる。0 以下なら制限なし
	bool tateChuYoko = true;
	bool rotateSideways = true; ///< false なら欧文も 1 字ずつ立てる。回転を描けない描画先のため
	float rubyScale = 0.5f;     ///< ルビの大きさ (親文字に対する比)
	float lineGap = 0.5f;       ///< 行間 (em に対する比)
};

struct PlacedGlyph
{
	std::uint32_t glyphId = 0;
	float x = 0.0f; ///< 字形の原点 (ベースラインの左端)。y は下向き
	float y = 0.0f;
};

/// @brief 同じ大きさ・同じ向きの字形の塊。
struct GlyphRun
{
	float pixelSize = 0.0f;
	bool rotated = false;  ///< true なら (originX, originY) を原点に、時計回りに 90 度回して描く。字形の位置はその原点からの相対
	float originX = 0.0f;
	float originY = 0.0f;
	std::uint32_t first = 0;
	std::uint32_t count = 0;
};

/// @brief 字や語 1 つ分の領域。位置の検査と、領域を描いた確認画像に使う。
struct LayoutCell
{
	enum class Kind : std::uint8_t { Upright, Sideways, Combined, Ruby };
	Kind kind = Kind::Upright;
	float x = 0.0f;
	float y = 0.0f;
	float width = 0.0f;
	float height = 0.0f;
};

struct TextLayout
{
	std::vector<PlacedGlyph> glyphs;
	std::vector<GlyphRun> runs;
	std::vector<LayoutCell> cells;
	float width = 0.0f;       ///< 内容の幅
	float height = 0.0f;      ///< 内容の高さ
	float em = 0.0f;
	int lineCount = 0;        ///< 置いた行数
	bool truncated = false;   ///< crossExtent に入りきらず、行を捨てた

	void clear() noexcept
	{
		glyphs.clear();
		runs.clear();
		cells.clear();
		width = height = em = 0.0f;
		lineCount = 0;
		truncated = false;
	}
};

/// @brief text (ルビ記法を含む) を組む。out の中身は置き換える。
void layoutText(FontFace& face, std::string_view text, const LayoutOptions& options, TextLayout& out);

} // namespace mitiru::text
