#pragma once

/// @file FontFace.hpp
/// @brief 書体の読み込み、HarfBuzz による字形選択、FreeType / msdfgen による輪郭のラスタ化。
/// @details 実装は mitiru_text の src/text/FontFace.cpp に閉じ、外部ライブラリのヘッダをこのファイルへ漏らさない。
///
/// サイズはピクセル高さで指定し、ascender と descender の差が pixelSize になる。em 基準ではないため、同じ値でも書体ごとに em の大きさが変わる。

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru::text
{

struct FontMetrics
{
	float ascent = 0.0f;     ///< ベースラインから上 (正)
	float descent = 0.0f;    ///< ベースラインから下 (通常は負)
	float lineGap = 0.0f;
	float lineHeight = 0.0f; ///< ascent - descent + lineGap
	float pixelSize = 0.0f;
};

/// @brief 字形選択後の 1 グリフ。座標は y 下向きで、単位は shape() に渡した pixelSize のピクセル。
struct ShapedGlyph
{
	std::uint32_t glyphId = 0;
	std::uint32_t cluster = 0; ///< 元の UTF-8 文字列内のバイト位置
	float advanceX = 0.0f;
	float advanceY = 0.0f;
	float offsetX = 0.0f;
	float offsetY = 0.0f;
};

enum class TextDirection : std::uint8_t
{
	LeftToRight,
	TopToBottom, ///< 縦書き。'vert' 置換が効き、句読点や括弧は縦組み用の字形になる
};

/// @brief 1 グリフの MTSDF。RGB は多チャンネル距離、A は真の距離で、rgba は上の行から並ぶ。
/// @details left、top、right、bottom は、ペン位置を基準とする pixelSize = 1 の画素枠。表示サイズを掛けると画面上の矩形になる。
struct DistanceFieldGlyph
{
	int width = 0;
	int height = 0;
	float left = 0.0f;
	float top = 0.0f;
	float right = 0.0f;
	float bottom = 0.0f;
	std::vector<std::uint8_t> rgba;
};

/// @brief 指定サイズで塗った濃さを 0 から 255 で表す。left と top は、ペン位置から画素枠の左上までの距離。
struct CoverageGlyph
{
	int width = 0;
	int height = 0;
	int left = 0;
	int top = 0;
	std::vector<std::uint8_t> alpha;
};

class FontFace
{
public:
	/// @brief TTF、OTF、TTC の先頭の書体を読む。失敗時は nullptr を返し、error に理由を入れる。
	[[nodiscard]] static std::unique_ptr<FontFace> load(std::vector<std::uint8_t> data,
	                                                    std::string& error);
	[[nodiscard]] static std::unique_ptr<FontFace> loadFile(const std::string& path,
	                                                        std::string& error);

	~FontFace();
	FontFace(const FontFace&) = delete;
	FontFace& operator=(const FontFace&) = delete;

	/// @brief キャッシュのキーに使う、プロセス内で一意な番号。
	[[nodiscard]] std::uint32_t id() const noexcept;

	[[nodiscard]] FontMetrics metrics(float pixelSize) const noexcept;

	/// @brief UTF-8 の字形を選ぶ。返す範囲は次の shape 系呼び出しまで有効。
	/// @details 文字種ごとに区切るため、和欧混在でも欧文のカーニングと合字が適用される。言語は日本語として扱う。
	std::span<const ShapedGlyph> shape(std::string_view utf8, float pixelSize,
	                                   TextDirection direction = TextDirection::LeftToRight);

	[[nodiscard]] float measureText(std::string_view utf8, float pixelSize);
	[[nodiscard]] float advanceWidth(std::uint32_t codepoint, float pixelSize);
	/// @brief GPOS または kern による 2 文字間の字送りの増減。
	[[nodiscard]] float kerning(std::uint32_t first, std::uint32_t second, float pixelSize);
	/// @brief 書体にない文字は 0 の .notdef を返す。
	[[nodiscard]] std::uint32_t glyphIndex(std::uint32_t codepoint) const noexcept;

	/// @brief MTSDF をまとめて作る。輪郭のない字形は width = 0 で返す。
	/// @details 輪郭は順に読み込み、距離は字形と行の帯に分けて並列計算する。結果は分け方やスレッド数に左右されない。
	/// @param atlasPixelSize 生成時のピクセル高さ
	/// @param pxRange アトラス上のピクセル単位で表す距離場の幅
	/// @param maxCell 画素枠の上限。大きな字形は倍率を下げて収める
	void distanceFields(std::span<const std::uint32_t> glyphIds, float atlasPixelSize, float pxRange,
	                    int maxCell, std::vector<DistanceFieldGlyph>& out);

	/// @brief 1 字形の MTSDF を作る。輪郭のない字形は false を返す。
	bool distanceField(std::uint32_t glyphId, float atlasPixelSize, float pxRange, int maxCell,
	                   DistanceFieldGlyph& out);

	/// @brief 指定サイズで濃さを塗る。輪郭のない字形は false を返す。
	/// @param subpixelX 輪郭を右へずらす量。0 以上 1 未満の画素とする。ペン位置の端数を字形に持たせ、画素へ丸めた字送りによる字間のばらつきを防ぐ。
	bool coverage(std::uint32_t glyphId, float pixelSize, float subpixelX, CoverageGlyph& out);

private:
	struct Impl;
	explicit FontFace(std::unique_ptr<Impl> impl) noexcept;
	std::unique_ptr<Impl> m_impl;
};

} // namespace mitiru::text
