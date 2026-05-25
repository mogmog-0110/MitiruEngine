#pragma once

/// @file SdfFontAtlas.hpp
/// @brief SDFフォントアトラス生成（シェルフパック方式）

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <mitiru/render/Texture.hpp>
#include <mitiru/render/sdf/Utf8Utils.hpp>

#include <stb_truetype.h>

namespace mitiru::render
{

// ── SDF グリフ情報 ─────────────────────────────────────────────

/// @brief SDFアトラス上のグリフ情報
struct SdfGlyphInfo
{
	std::uint32_t codepoint = 0; ///< Unicodeコードポイント
	int x0 = 0;                  ///< アトラス上の左上X座標
	int y0 = 0;                  ///< アトラス上の左上Y座標
	int x1 = 0;                  ///< アトラス上の右下X座標
	int y1 = 0;                  ///< アトラス上の右下Y座標
	float xoff = 0.0f;           ///< ベースラインからのX方向オフセット
	float yoff = 0.0f;           ///< ベースラインからのY方向オフセット
	float xadvance = 0.0f;       ///< 次の文字までのX方向前進幅
	int sdfPadding = 0;          ///< SDFパディング（ピクセル数）

	/// @brief アトラス上のグリフ幅
	[[nodiscard]] int width() const noexcept { return x1 - x0; }

	/// @brief アトラス上のグリフ高さ
	[[nodiscard]] int height() const noexcept { return y1 - y0; }
};

// ── SDF フォントメトリクス ──────────────────────────────────────

/// @brief SDFフォントメトリクス
struct SdfFontMetrics
{
	float ascent = 0.0f;     ///< ベースラインからの上方向の高さ
	float descent = 0.0f;    ///< ベースラインからの下方向の深さ（通常負値）
	float lineGap = 0.0f;    ///< 行間の追加スペース
	float lineHeight = 0.0f; ///< 行の高さ（ascent - descent + lineGap）
	float sdfPixelSize = 0.0f; ///< SDF生成時のフォントサイズ
};

// ── SDF フォントアトラス ───────────────────────────────────────

/// @brief SDFフォントアトラス
/// @details TTFデータからSDF（Signed Distance Field）テクスチャアトラスを生成する。
///          距離情報はアルファチャネルに格納され、RGBは白（255,255,255）で統一される。
///          任意のフォントサイズへのスケーリングが可能で、アウトライン等のエフェクトも
///          ピクセルシェーダー（またはCPU）で適用できる。
///
/// @code
/// std::vector<uint8_t> ttfData = loadFile("font.ttf");
/// SdfFontAtlas atlas(std::move(ttfData), 32.0f);
/// atlas.addAsciiRange();
/// atlas.addHiraganaRange();
/// atlas.buildAtlas();
///
/// // 描画
/// SdfTextRenderer renderer(atlas);
/// renderer.drawText(screen, "Hello World!", 10, 10, 24.0f, sgc::Colorf::white());
/// @endcode
class SdfFontAtlas
{
	std::vector<std::uint8_t> m_fontData;  ///< TTFバイナリデータ
	stbtt_fontinfo m_fontInfo{};            ///< stb_truetype フォント情報
	bool m_initialized = false;             ///< フォント初期化済みフラグ

	float m_sdfPixelSize = 32.0f;           ///< SDF生成時のフォントサイズ
	int m_sdfPadding = 6;                   ///< SDFパディング半径（ピクセル）
	float m_sdfScale = 1.0f;               ///< フォントスケール係数

	/// @brief SDFエッジ値（0〜255）。128で文字の輪郭に一致
	static constexpr std::uint8_t SDF_ON_EDGE_VALUE = 128;

	/// @brief 距離スケール（padding内でのSDF減衰率）
	static constexpr float SDF_PIXEL_DIST_SCALE = 128.0f / 6.0f;

	std::vector<std::uint32_t> m_pendingCodepoints; ///< ビルド待ちコードポイント
	std::unordered_map<std::uint32_t, SdfGlyphInfo> m_glyphs; ///< コードポイント→グリフ情報

	Texture m_atlasTexture;        ///< アトラステクスチャ（RGBA8）
	int m_atlasWidth = 0;          ///< アトラス幅
	int m_atlasHeight = 0;         ///< アトラス高さ

	SdfFontMetrics m_metrics{};    ///< フォントメトリクス

public:
	/// @brief デフォルトコンストラクタ（無効なアトラス）
	SdfFontAtlas() = default;

	/// @brief TTFデータからSDFフォントアトラスを構築する
	/// @param fontData TTF/OTFファイルのバイナリデータ
	/// @param sdfPixelSize SDF生成時の基準フォントサイズ（デフォルト32）
	/// @param sdfPadding SDFパディング半径（デフォルト6）
	/// @throws std::runtime_error フォント初期化失敗時
	explicit SdfFontAtlas(std::vector<std::uint8_t> fontData,
		float sdfPixelSize = 32.0f, int sdfPadding = 6)
		: m_fontData(std::move(fontData))
		, m_sdfPixelSize(sdfPixelSize)
		, m_sdfPadding(sdfPadding)
	{
		if (m_fontData.empty())
		{
			throw std::runtime_error("SdfFontAtlas: empty font data");
		}

		const int offset = stbtt_GetFontOffsetForIndex(m_fontData.data(), 0);
		if (offset < 0)
		{
			throw std::runtime_error("SdfFontAtlas: invalid font offset");
		}

		if (!stbtt_InitFont(&m_fontInfo, m_fontData.data(), offset))
		{
			throw std::runtime_error("SdfFontAtlas: failed to initialize font");
		}

		m_initialized = true;
		m_sdfScale = stbtt_ScaleForPixelHeight(&m_fontInfo, m_sdfPixelSize);

		// メトリクスを取得
		int ascent = 0;
		int descent = 0;
		int lineGap = 0;
		stbtt_GetFontVMetrics(&m_fontInfo, &ascent, &descent, &lineGap);

		m_metrics.ascent = static_cast<float>(ascent) * m_sdfScale;
		m_metrics.descent = static_cast<float>(descent) * m_sdfScale;
		m_metrics.lineGap = static_cast<float>(lineGap) * m_sdfScale;
		m_metrics.lineHeight = m_metrics.ascent - m_metrics.descent + m_metrics.lineGap;
		m_metrics.sdfPixelSize = m_sdfPixelSize;
	}

	// ムーブセマンティクス
	SdfFontAtlas(SdfFontAtlas&& other) noexcept
		: m_fontData(std::move(other.m_fontData))
		, m_fontInfo(other.m_fontInfo)
		, m_initialized(other.m_initialized)
		, m_sdfPixelSize(other.m_sdfPixelSize)
		, m_sdfPadding(other.m_sdfPadding)
		, m_sdfScale(other.m_sdfScale)
		, m_pendingCodepoints(std::move(other.m_pendingCodepoints))
		, m_glyphs(std::move(other.m_glyphs))
		, m_atlasTexture(std::move(other.m_atlasTexture))
		, m_atlasWidth(other.m_atlasWidth)
		, m_atlasHeight(other.m_atlasHeight)
		, m_metrics(other.m_metrics)
	{
		if (m_initialized && !m_fontData.empty())
		{
			m_fontInfo.data = m_fontData.data();
		}
		other.m_initialized = false;
	}

	SdfFontAtlas& operator=(SdfFontAtlas&& other) noexcept
	{
		if (this != &other)
		{
			m_fontData = std::move(other.m_fontData);
			m_fontInfo = other.m_fontInfo;
			m_initialized = other.m_initialized;
			m_sdfPixelSize = other.m_sdfPixelSize;
			m_sdfPadding = other.m_sdfPadding;
			m_sdfScale = other.m_sdfScale;
			m_pendingCodepoints = std::move(other.m_pendingCodepoints);
			m_glyphs = std::move(other.m_glyphs);
			m_atlasTexture = std::move(other.m_atlasTexture);
			m_atlasWidth = other.m_atlasWidth;
			m_atlasHeight = other.m_atlasHeight;
			m_metrics = other.m_metrics;

			if (m_initialized && !m_fontData.empty())
			{
				m_fontInfo.data = m_fontData.data();
			}
			other.m_initialized = false;
		}
		return *this;
	}

	// コピー禁止
	SdfFontAtlas(const SdfFontAtlas&) = delete;
	SdfFontAtlas& operator=(const SdfFontAtlas&) = delete;

	// ── コードポイント範囲の追加 ────────────────────────────

	/// @brief コードポイント範囲を追加する
	/// @param first 開始コードポイント（含む）
	/// @param last 終了コードポイント（含む）
	void addCodepointRange(std::uint32_t first, std::uint32_t last)
	{
		for (std::uint32_t cp = first; cp <= last; ++cp)
		{
			m_pendingCodepoints.push_back(cp);
		}
	}

	/// @brief 個別のコードポイントを追加する
	/// @param codepoint 追加するコードポイント
	void addCodepoint(std::uint32_t codepoint)
	{
		m_pendingCodepoints.push_back(codepoint);
	}

	/// @brief ASCII印字可能文字（32-126）を追加する
	void addAsciiRange()
	{
		addCodepointRange(0x0020, 0x007E);
	}

	/// @brief ひらがな（U+3040-U+309F）を追加する
	void addHiraganaRange()
	{
		addCodepointRange(0x3040, 0x309F);
	}

	/// @brief カタカナ（U+30A0-U+30FF）を追加する
	void addKatakanaRange()
	{
		addCodepointRange(0x30A0, 0x30FF);
	}

	/// @brief 常用漢字の一部（CJK統合漢字の基本範囲、U+4E00-U+9FFF）を追加する
	/// @note 全20,992文字は巨大なアトラスが必要。必要な範囲だけ追加することを推奨
	void addCommonKanjiRange()
	{
		// 教育漢字1006字相当の範囲（実際にはフォントに存在するもののみ生成）
		addCodepointRange(0x4E00, 0x5FFF);
	}

	/// @brief UTF-8文字列に含まれるコードポイントを追加する
	/// @param text UTF-8文字列
	void addFromString(std::string_view text)
	{
		const auto cps = sdf_detail::toCodepoints(text);
		for (const auto cp : cps)
		{
			m_pendingCodepoints.push_back(cp);
		}
	}

	/// @brief CJK句読点（U+3000-U+303F）を追加する
	void addCjkPunctuationRange()
	{
		addCodepointRange(0x3000, 0x303F);
	}

	/// @brief 半角カナ・全角英数（U+FF00-U+FFEF）を追加する
	void addFullwidthRange()
	{
		addCodepointRange(0xFF00, 0xFFEF);
	}

	// ── アトラスビルド ──────────────────────────────────────

	/// @brief 登録済みコードポイントからSDFアトラスを生成する
	/// @param atlasPadding グリフ間の余白（ピクセル、デフォルト2）
	/// @return true: 成功、false: 失敗
	bool buildAtlas(int atlasPadding = 2)
	{
		if (!m_initialized || m_pendingCodepoints.empty())
		{
			return false;
		}

		// 重複排除とソート
		std::sort(m_pendingCodepoints.begin(), m_pendingCodepoints.end());
		m_pendingCodepoints.erase(
			std::unique(m_pendingCodepoints.begin(), m_pendingCodepoints.end()),
			m_pendingCodepoints.end());

		// 各コードポイントのSDFビットマップを生成
		struct RawGlyph
		{
			std::uint32_t codepoint = 0;
			int width = 0;
			int height = 0;
			int xoff = 0;
			int yoff = 0;
			float xadvance = 0.0f;
			std::vector<std::uint8_t> sdfBitmap;
		};

		const float pixDistScale = static_cast<float>(SDF_ON_EDGE_VALUE) /
			static_cast<float>(m_sdfPadding);

		std::vector<RawGlyph> rawGlyphs;
		rawGlyphs.reserve(m_pendingCodepoints.size());

		for (const auto cp : m_pendingCodepoints)
		{
			const int glyphIndex = stbtt_FindGlyphIndex(&m_fontInfo, static_cast<int>(cp));
			if (glyphIndex == 0 && cp != 0)
			{
				continue; // グリフが存在しないコードポイントをスキップ
			}

			int w = 0;
			int h = 0;
			int xoff = 0;
			int yoff = 0;
			unsigned char* sdfData = stbtt_GetCodepointSDF(
				&m_fontInfo, m_sdfScale, static_cast<int>(cp),
				m_sdfPadding, SDF_ON_EDGE_VALUE, pixDistScale,
				&w, &h, &xoff, &yoff);

			if (sdfData == nullptr || w <= 0 || h <= 0)
			{
				if (sdfData != nullptr)
				{
					stbtt_FreeSDF(sdfData, nullptr);
				}

				// スペース等、ビットマップを持たないグリフも前進幅は記録する
				int advance = 0;
				int lsb = 0;
				stbtt_GetCodepointHMetrics(&m_fontInfo, static_cast<int>(cp), &advance, &lsb);

				RawGlyph rg;
				rg.codepoint = cp;
				rg.xadvance = static_cast<float>(advance) * m_sdfScale;
				rawGlyphs.push_back(std::move(rg));
				continue;
			}

			int advance = 0;
			int lsb = 0;
			stbtt_GetCodepointHMetrics(&m_fontInfo, static_cast<int>(cp), &advance, &lsb);

			RawGlyph rg;
			rg.codepoint = cp;
			rg.width = w;
			rg.height = h;
			rg.xoff = xoff;
			rg.yoff = yoff;
			rg.xadvance = static_cast<float>(advance) * m_sdfScale;
			rg.sdfBitmap.assign(sdfData, sdfData + static_cast<std::size_t>(w) * h);

			stbtt_FreeSDF(sdfData, nullptr);
			rawGlyphs.push_back(std::move(rg));
		}

		if (rawGlyphs.empty())
		{
			return false;
		}

		// 高さ降順ソート（シェルフパッキング効率化）
		std::sort(rawGlyphs.begin(), rawGlyphs.end(),
			[](const RawGlyph& a, const RawGlyph& b)
			{
				return a.height > b.height;
			});

		// アトラスサイズ推定
		int totalArea = 0;
		for (const auto& rg : rawGlyphs)
		{
			totalArea += (rg.width + atlasPadding) * (rg.height + atlasPadding);
		}
		const int estimatedSide = sdf_detail::nextPow2(
			static_cast<int>(std::sqrt(static_cast<float>(totalArea) * 1.3f)));
		m_atlasWidth = estimatedSide;
		m_atlasHeight = estimatedSide;

		// シェルフパッキング（リトライ付き）
		std::vector<std::uint8_t> atlasPixels;
		bool packed = false;

		for (int attempt = 0; attempt < 4 && !packed; ++attempt)
		{
			atlasPixels.assign(
				static_cast<std::size_t>(m_atlasWidth) * m_atlasHeight * 4, 0);

			int shelfX = atlasPadding;
			int shelfY = atlasPadding;
			int shelfH = 0;
			packed = true;

			for (auto& rg : rawGlyphs)
			{
				if (rg.width == 0 || rg.height == 0)
				{
					continue;
				}

				if (shelfX + rg.width + atlasPadding > m_atlasWidth)
				{
					shelfX = atlasPadding;
					shelfY += shelfH + atlasPadding;
					shelfH = 0;
				}

				if (shelfY + rg.height + atlasPadding > m_atlasHeight)
				{
					m_atlasHeight *= 2;
					packed = false;
					break;
				}

				// グリフ情報を記録
				SdfGlyphInfo gi;
				gi.codepoint = rg.codepoint;
				gi.x0 = shelfX;
				gi.y0 = shelfY;
				gi.x1 = shelfX + rg.width;
				gi.y1 = shelfY + rg.height;
				gi.xoff = static_cast<float>(rg.xoff);
				gi.yoff = static_cast<float>(rg.yoff);
				gi.xadvance = rg.xadvance;
				gi.sdfPadding = m_sdfPadding;
				m_glyphs[rg.codepoint] = gi;

				shelfH = std::max(shelfH, rg.height);
				shelfX += rg.width + atlasPadding;
			}
		}

		if (!packed)
		{
			m_glyphs.clear();
			return false;
		}

		// SDFデータをアトラスにコピー（グレースケールSDF → RGBA、距離はAチャネル）
		for (const auto& rg : rawGlyphs)
		{
			const auto it = m_glyphs.find(rg.codepoint);
			if (it == m_glyphs.end() || rg.width == 0)
			{
				continue;
			}
			const auto& gi = it->second;

			for (int y = 0; y < rg.height; ++y)
			{
				for (int x = 0; x < rg.width; ++x)
				{
					const auto srcIdx = static_cast<std::size_t>(y * rg.width + x);
					const auto dstIdx = static_cast<std::size_t>(
						((gi.y0 + y) * m_atlasWidth + (gi.x0 + x)) * 4);

					if (srcIdx < rg.sdfBitmap.size() && dstIdx + 3 < atlasPixels.size())
					{
						atlasPixels[dstIdx + 0] = 255; // R
						atlasPixels[dstIdx + 1] = 255; // G
						atlasPixels[dstIdx + 2] = 255; // B
						atlasPixels[dstIdx + 3] = rg.sdfBitmap[srcIdx]; // A = SDF 距離
					}
				}
			}
		}

		// ビットマップを持たないグリフ（スペース等）のグリフ情報も追加
		for (const auto& rg : rawGlyphs)
		{
			if (rg.width == 0 && m_glyphs.find(rg.codepoint) == m_glyphs.end())
			{
				SdfGlyphInfo gi;
				gi.codepoint = rg.codepoint;
				gi.xadvance = rg.xadvance;
				gi.sdfPadding = m_sdfPadding;
				m_glyphs[rg.codepoint] = gi;
			}
		}

		m_atlasTexture = Texture(m_atlasWidth, m_atlasHeight, atlasPixels);
		m_pendingCodepoints.clear();
		return true;
	}

	// ── クエリ ─────────────────────────────────────────────

	/// @brief フォントが有効か
	[[nodiscard]] bool valid() const noexcept { return m_initialized && m_atlasTexture.valid(); }

	/// @brief アトラステクスチャを取得する
	[[nodiscard]] const Texture& texture() const noexcept { return m_atlasTexture; }

	/// @brief アトラス幅を取得する
	[[nodiscard]] int atlasWidth() const noexcept { return m_atlasWidth; }

	/// @brief アトラス高さを取得する
	[[nodiscard]] int atlasHeight() const noexcept { return m_atlasHeight; }

	/// @brief フォントメトリクスを取得する
	[[nodiscard]] const SdfFontMetrics& metrics() const noexcept { return m_metrics; }

	/// @brief SDF生成時のフォントサイズを取得する
	[[nodiscard]] float sdfPixelSize() const noexcept { return m_sdfPixelSize; }

	/// @brief SDFパディングを取得する
	[[nodiscard]] int sdfPadding() const noexcept { return m_sdfPadding; }

	/// @brief コードポイントからグリフ情報を検索する
	/// @param codepoint Unicodeコードポイント
	/// @return グリフ情報へのポインタ（見つからない場合はnullptr）
	[[nodiscard]] const SdfGlyphInfo* findGlyph(std::uint32_t codepoint) const noexcept
	{
		const auto it = m_glyphs.find(codepoint);
		if (it == m_glyphs.end())
		{
			return nullptr;
		}
		return &it->second;
	}

	/// @brief 2つのコードポイント間のカーニング値を取得する
	/// @param cp1 先行コードポイント
	/// @param cp2 後続コードポイント
	/// @param fontSize 表示フォントサイズ
	/// @return カーニング値（表示ピクセル単位）
	[[nodiscard]] float kerning(std::uint32_t cp1, std::uint32_t cp2, float fontSize) const noexcept
	{
		if (!m_initialized)
		{
			return 0.0f;
		}
		const float displayScale = fontSize / m_sdfPixelSize;
		const int kern = stbtt_GetCodepointKernAdvance(
			&m_fontInfo, static_cast<int>(cp1), static_cast<int>(cp2));
		return static_cast<float>(kern) * m_sdfScale * displayScale;
	}

	/// @brief 登録済みグリフ数を取得する
	[[nodiscard]] std::size_t glyphCount() const noexcept { return m_glyphs.size(); }

	/// @brief 全グリフ情報のマップを取得する
	[[nodiscard]] const std::unordered_map<std::uint32_t, SdfGlyphInfo>& glyphs() const noexcept
	{
		return m_glyphs;
	}
};

} // namespace mitiru::render
