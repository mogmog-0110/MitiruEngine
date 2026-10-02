// mitiru::text::FontFace の実装。FreeType、HarfBuzz、msdfgen のヘッダはこの .cpp にだけ入れる。

#include <mitiru/text/FontFace.hpp>
#include <mitiru/i18n/Utf8Text.hpp>

#include <algorithm>
#include <atomic>
#include <execution>
#include <cmath>
#include <fstream>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_DRIVER_H
#include FT_MODULE_H
#include FT_OUTLINE_H

#include <hb.h>

#include <msdfgen.h>
#include <msdfgen-ext.h>

#include "GlyphGeometry.h"

namespace mitiru::text
{

namespace
{

std::atomic<std::uint32_t> g_nextFaceId{1};

hb_language_t japanese() noexcept
{
	static const hb_language_t lang = hb_language_from_string("ja", -1);
	return lang;
}

[[nodiscard]] bool isNeutralScript(hb_script_t s) noexcept
{
	return s == HB_SCRIPT_COMMON || s == HB_SCRIPT_INHERITED || s == HB_SCRIPT_UNKNOWN;
}

} // namespace

struct FontFace::Impl
{
	std::vector<std::uint8_t> data;
	FT_Library library = nullptr;
	FT_Face face = nullptr;
	hb_blob_t* blob = nullptr;
	hb_face_t* hbFace = nullptr;
	hb_font_t* hbFont = nullptr;
	hb_buffer_t* buffer = nullptr;
	msdfgen::FontHandle* msdfFont = nullptr;
	std::uint32_t id = 0;
	float heightUnits = 1.0f; ///< ascender - descender (フォント単位)
	std::vector<ShapedGlyph> shaped;

	Impl() = default;
	Impl(const Impl&) = delete;
	Impl& operator=(const Impl&) = delete;
	~Impl()
	{
		if (msdfFont) { msdfgen::destroyFont(msdfFont); }
		if (buffer) { hb_buffer_destroy(buffer); }
		if (hbFont) { hb_font_destroy(hbFont); }
		if (hbFace) { hb_face_destroy(hbFace); }
		if (blob) { hb_blob_destroy(blob); }
		if (face) { FT_Done_Face(face); }
		if (library) { FT_Done_FreeType(library); }
	}

	[[nodiscard]] float scale(float pixelSize) const noexcept { return pixelSize / heightUnits; }

	void shapeRun(std::string_view text, std::size_t begin, std::size_t end, hb_script_t script,
	              hb_direction_t direction, float unit)
	{
		if (end <= begin) { return; }
		hb_buffer_clear_contents(buffer);
		hb_buffer_add_utf8(buffer, text.data(), static_cast<int>(text.size()),
			static_cast<unsigned>(begin), static_cast<int>(end - begin));
		hb_buffer_set_direction(buffer, direction);
		hb_buffer_set_script(buffer, isNeutralScript(script) ? HB_SCRIPT_LATIN : script);
		hb_buffer_set_language(buffer, japanese());
		hb_shape(hbFont, buffer, nullptr, 0);

		unsigned count = 0;
		const hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buffer, &count);
		const hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buffer, &count);
		for (unsigned i = 0; i < count; ++i)
		{
			ShapedGlyph g;
			g.glyphId = info[i].codepoint;
			g.cluster = info[i].cluster;
			g.advanceX = static_cast<float>(pos[i].x_advance) * unit;
			g.advanceY = -static_cast<float>(pos[i].y_advance) * unit;
			g.offsetX = static_cast<float>(pos[i].x_offset) * unit;
			g.offsetY = -static_cast<float>(pos[i].y_offset) * unit;
			shaped.push_back(g);
		}
	}
};

FontFace::FontFace(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {}
FontFace::~FontFace() = default;

std::unique_ptr<FontFace> FontFace::load(std::vector<std::uint8_t> data, std::string& error)
{
	if (data.size() < 12)
	{
		error = "font data is empty or truncated";
		return nullptr;
	}
	auto impl = std::make_unique<Impl>();
	impl->data = std::move(data);
	if (FT_Init_FreeType(&impl->library) != 0)
	{
		error = "FreeType initialization failed";
		return nullptr;
	}
	// 小さい文字の細い線が薄い灰色にぼやけないよう、自動ヒンタの線を太くする。
	const FT_Bool noStemDarkening = 0;
	FT_Property_Set(impl->library, "autofitter", "no-stem-darkening", &noStemDarkening);
	const FT_Error ftErr = FT_New_Memory_Face(impl->library, impl->data.data(),
		static_cast<FT_Long>(impl->data.size()), 0, &impl->face);
	if (ftErr != 0 || impl->face == nullptr || !FT_IS_SCALABLE(impl->face))
	{
		error = "not a scalable TrueType/OpenType font (FreeType error " + std::to_string(ftErr) + ")";
		return nullptr;
	}
	const int heightUnits = impl->face->ascender - impl->face->descender;
	if (heightUnits <= 0)
	{
		error = "font has no vertical metrics (ascender - descender <= 0)";
		return nullptr;
	}
	impl->heightUnits = static_cast<float>(heightUnits);

	impl->blob = hb_blob_create(reinterpret_cast<const char*>(impl->data.data()),
		static_cast<unsigned>(impl->data.size()), HB_MEMORY_MODE_READONLY, nullptr, nullptr);
	impl->hbFace = hb_face_create(impl->blob, 0);
	impl->hbFont = hb_font_create(impl->hbFace);
	const int upem = static_cast<int>(hb_face_get_upem(impl->hbFace));
	hb_font_set_scale(impl->hbFont, upem, upem);
	impl->buffer = hb_buffer_create();
	if (!hb_buffer_allocation_successful(impl->buffer))
	{
		error = "HarfBuzz buffer allocation failed";
		return nullptr;
	}
	impl->msdfFont = msdfgen::adoptFreetypeFont(impl->face);
	impl->id = g_nextFaceId.fetch_add(1);
	return std::unique_ptr<FontFace>(new FontFace(std::move(impl)));
}

std::unique_ptr<FontFace> FontFace::loadFile(const std::string& path, std::string& error)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file)
	{
		error = "cannot open font file: " + path;
		return nullptr;
	}
	const auto size = file.tellg();
	std::vector<std::uint8_t> data(size > 0 ? static_cast<std::size_t>(size) : 0);
	file.seekg(0);
	file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
	return load(std::move(data), error);
}

std::uint32_t FontFace::id() const noexcept { return m_impl->id; }

FontMetrics FontFace::metrics(float pixelSize) const noexcept
{
	const float s = m_impl->scale(pixelSize);
	const FT_Face f = m_impl->face;
	FontMetrics m;
	m.ascent = static_cast<float>(f->ascender) * s;
	m.descent = static_cast<float>(f->descender) * s;
	m.lineGap = std::max(0.0f, static_cast<float>(f->height - (f->ascender - f->descender)) * s);
	m.lineHeight = m.ascent - m.descent + m.lineGap;
	m.pixelSize = pixelSize;
	return m;
}

std::span<const ShapedGlyph> FontFace::shape(std::string_view utf8, float pixelSize,
                                             TextDirection direction)
{
	Impl& im = *m_impl;
	im.shaped.clear();
	const hb_direction_t dir =
		direction == TextDirection::TopToBottom ? HB_DIRECTION_TTB : HB_DIRECTION_LTR;
	const float unit = im.scale(pixelSize);
	hb_unicode_funcs_t* ucd = hb_unicode_funcs_get_default();

	// 文字種の切れ目で区切る。空白や記号 (Common) は直前の文字種に含める。
	std::size_t runStart = 0;
	hb_script_t runScript = HB_SCRIPT_COMMON;
	for (std::size_t i = 0; i < utf8.size();)
	{
		int len = 1;
		const hb_script_t sc = hb_unicode_script(ucd, i18n::decodeAt(utf8, i, len));
		if (!isNeutralScript(sc))
		{
			if (!isNeutralScript(runScript) && sc != runScript)
			{
				im.shapeRun(utf8, runStart, i, runScript, dir, unit);
				runStart = i;
			}
			runScript = sc;
		}
		i += static_cast<std::size_t>(len);
	}
	im.shapeRun(utf8, runStart, utf8.size(), runScript, dir, unit);
	return im.shaped;
}

float FontFace::measureText(std::string_view utf8, float pixelSize)
{
	float width = 0.0f;
	for (const ShapedGlyph& g : shape(utf8, pixelSize)) { width += g.advanceX; }
	return width;
}

std::uint32_t FontFace::glyphIndex(std::uint32_t codepoint) const noexcept
{
	hb_codepoint_t glyph = 0;
	return hb_font_get_nominal_glyph(m_impl->hbFont, codepoint, &glyph) ? glyph : 0;
}

float FontFace::advanceWidth(std::uint32_t codepoint, float pixelSize)
{
	const hb_position_t adv = hb_font_get_glyph_h_advance(m_impl->hbFont, glyphIndex(codepoint));
	return static_cast<float>(adv) * m_impl->scale(pixelSize);
}

float FontFace::kerning(std::uint32_t first, std::uint32_t second, float pixelSize)
{
	char buf[8];
	int n = i18n::encodeUtf8(first, buf);
	n += i18n::encodeUtf8(second, buf + n);
	const float pair = measureText(std::string_view(buf, static_cast<std::size_t>(n)), pixelSize);
	return pair - advanceWidth(first, pixelSize) - advanceWidth(second, pixelSize);
}

namespace
{

/// 4 行ずつ並列に計算する。漢字 1 字 (約 45 行) は十数個の仕事に分かれる。
constexpr int kBandRows = 4;

struct PendingField
{
	msdf_atlas::GlyphGeometry geometry;
	msdfgen::Bitmap<float, 4> bitmap; ///< 輪郭と同じ y 上向き
	bool valid = false;
};

struct Band
{
	PendingField* field = nullptr;
	int y0 = 0;
	int y1 = 0;
};

/// @brief 輪郭を読み、色分けして、枠に収まる倍率を決める。FreeType を使うため並列にしない。
bool prepareField(msdfgen::FontHandle* font, double unitScale, std::uint32_t glyphId,
                  double atlasPixelSize, double pxRange, int maxCell, PendingField& p)
{
	if (!p.geometry.load(font, unitScale, msdfgen::GlyphIndex(glyphId), false) || p.geometry.isWhitespace())
	{
		return false;
	}
	p.geometry.edgeColoring(&msdfgen::edgeColoringInkTrap, 3.0, 0);
	// 大きすぎる字形は倍率を下げて枠に収める。距離場の幅はアトラス上の画素で一定に保つ。
	double scale = atlasPixelSize;
	int w = 0, h = 0;
	for (int attempt = 0; attempt < 4; ++attempt)
	{
		p.geometry.wrapBox(scale, pxRange / scale, 1.0, false);
		p.geometry.getBoxSize(w, h);
		if (w <= maxCell && h <= maxCell) { break; }
		scale *= 0.98 * static_cast<double>(maxCell) / static_cast<double>(std::max(w, h));
	}
	if (w <= 0 || h <= 0 || w > maxCell || h > maxCell) { return false; }
	p.bitmap = msdfgen::Bitmap<float, 4>(w, h, msdfgen::Y_UPWARD);
	return true;
}

/// @brief 行 [y0, y1) の距離を計算する。補正は全行の計算後に finishField で行う。
void generateBand(const Band& band)
{
	const msdf_atlas::GlyphGeometry& g = band.field->geometry;
	const msdfgen::Vector2 t = g.getBoxTranslate();
	const double s = g.getBoxScale();
	const msdfgen::Projection projection(msdfgen::Vector2(s), msdfgen::Vector2(t.x, t.y - band.y0 / s));
	msdfgen::MSDFGeneratorConfig config;
	config.errorCorrection.mode = msdfgen::ErrorCorrectionConfig::DISABLED;
	const msdfgen::BitmapSection<float, 4> whole(band.field->bitmap);
	msdfgen::generateMTSDF(whole.getSection(0, band.y0, whole.width, band.y1), g.getShape(), projection,
	                       g.getBoxRange(), config);
}

/// @brief 走査線で重なった輪郭の内外を正し、補間で生じる角の欠けを補正する。
void finishField(PendingField& p)
{
	const msdf_atlas::GlyphGeometry& g = p.geometry;
	const msdfgen::BitmapSection<float, 4> whole(p.bitmap);
	msdfgen::distanceSignCorrection(whole, g.getShape(), g.getBoxProjection(), 0.5f, msdfgen::FILL_NONZERO);
	msdfgen::MSDFGeneratorConfig config;
	config.errorCorrection.distanceCheckMode = msdfgen::ErrorCorrectionConfig::DO_NOT_CHECK_DISTANCE;
	msdfgen::msdfErrorCorrection(whole, g.getShape(), g.getBoxProjection(), g.getBoxRange(), config);
}

/// @brief 画素枠の四辺を y 上向きのシェイプ座標から、y 下向きでピクセル高さ 1 の単位へ直し、行を上から並べる。
void storeField(const PendingField& p, double unitScale, DistanceFieldGlyph& out)
{
	const msdfgen::Vector2 t = p.geometry.getBoxTranslate();
	const double s = p.geometry.getBoxScale();
	const int w = p.bitmap.width();
	const int h = p.bitmap.height();
	out.width = w;
	out.height = h;
	out.left = static_cast<float>(-t.x * unitScale);
	out.right = static_cast<float>((-t.x + w / s) * unitScale);
	out.bottom = static_cast<float>(t.y * unitScale);
	out.top = static_cast<float>((t.y - h / s) * unitScale);
	out.rgba.resize(static_cast<std::size_t>(w) * h * 4);
	for (int row = 0; row < h; ++row)
	{
		for (int x = 0; x < w; ++x)
		{
			const float* px = p.bitmap(x, h - 1 - row);
			std::uint8_t* d = &out.rgba[(static_cast<std::size_t>(row) * w + x) * 4];
			for (int c = 0; c < 4; ++c) { d[c] = msdfgen::pixelFloatToByte(px[c]); }
		}
	}
}

} // namespace

void FontFace::distanceFields(std::span<const std::uint32_t> glyphIds, float atlasPixelSize, float pxRange,
                              int maxCell, std::vector<DistanceFieldGlyph>& out)
{
	const double unitScale = 1.0 / static_cast<double>(m_impl->heightUnits);
	std::vector<PendingField> fields(glyphIds.size());
	std::vector<Band> bands;
	for (std::size_t i = 0; i < glyphIds.size(); ++i)
	{
		PendingField& p = fields[i];
		p.valid = prepareField(m_impl->msdfFont, unitScale, glyphIds[i], atlasPixelSize, pxRange, maxCell, p);
		for (int y = 0; p.valid && y < p.bitmap.height(); y += kBandRows)
		{
			bands.push_back(Band{&p, y, std::min(y + kBandRows, p.bitmap.height())});
		}
	}
	std::for_each(std::execution::par, bands.begin(), bands.end(), generateBand);
	std::for_each(std::execution::par, fields.begin(), fields.end(),
	              [](PendingField& p) { if (p.valid) { finishField(p); } });

	out.resize(glyphIds.size());
	for (std::size_t i = 0; i < fields.size(); ++i)
	{
		out[i] = DistanceFieldGlyph{};
		if (fields[i].valid) { storeField(fields[i], unitScale, out[i]); }
	}
}

bool FontFace::distanceField(std::uint32_t glyphId, float atlasPixelSize, float pxRange, int maxCell,
                             DistanceFieldGlyph& out)
{
	std::vector<DistanceFieldGlyph> one;
	distanceFields(std::span<const std::uint32_t>(&glyphId, 1), atlasPixelSize, pxRange, maxCell, one);
	out = std::move(one[0]);
	return out.width > 0;
}

bool FontFace::coverage(std::uint32_t glyphId, float pixelSize, float subpixelX, CoverageGlyph& out)
{
	const FT_Face f = m_impl->face;
	const double emPx = static_cast<double>(pixelSize) * f->units_per_EM / m_impl->heightUnits;
	if (FT_Set_Char_Size(f, 0, static_cast<FT_F26Dot6>(std::lround(emPx * 64.0)), 72, 72) != 0) { return false; }
	if (FT_Load_Glyph(f, glyphId, FT_LOAD_TARGET_LIGHT) != 0) { return false; }
	if (f->glyph->format == FT_GLYPH_FORMAT_OUTLINE && subpixelX != 0.0f)
	{
		FT_Outline_Translate(&f->glyph->outline, static_cast<FT_Pos>(std::lround(subpixelX * 64.0f)), 0);
	}
	if (FT_Render_Glyph(f->glyph, FT_RENDER_MODE_LIGHT) != 0) { return false; }

	const FT_Bitmap& bmp = f->glyph->bitmap;
	if (bmp.width == 0 || bmp.rows == 0) { return false; }
	out.width = static_cast<int>(bmp.width);
	out.height = static_cast<int>(bmp.rows);
	out.left = f->glyph->bitmap_left;
	out.top = -f->glyph->bitmap_top;
	out.alpha.assign(static_cast<std::size_t>(out.width) * out.height, 0);
	const bool mono = bmp.pixel_mode == FT_PIXEL_MODE_MONO;
	for (int y = 0; y < out.height; ++y)
	{
		const unsigned char* row = bmp.buffer + static_cast<std::ptrdiff_t>(y) * bmp.pitch;
		std::uint8_t* dst = &out.alpha[static_cast<std::size_t>(y) * out.width];
		for (int x = 0; x < out.width; ++x)
		{
			dst[x] = mono ? static_cast<std::uint8_t>((row[x >> 3] & (0x80 >> (x & 7))) ? 255 : 0) : row[x];
		}
	}
	return true;
}

} // namespace mitiru::text
