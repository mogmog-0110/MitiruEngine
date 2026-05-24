// Detail header for mitiru::Engine — do not include directly; included via core/Engine.hpp
#pragma once

#include <mitiru/core/InlineMacro.hpp>

#include <fstream>

// ── Font loading (TTF + SDF atlas) out-of-class definitions ──────────────

MITIRU_INLINE void mitiru::Engine::initFont(const std::string& userPath)
{
	// フォント検索パスリスト（日本語対応フォントを優先）
	std::vector<std::string> searchPaths;
	if (!userPath.empty()) { searchPaths.push_back(userPath); }
	searchPaths.insert(searchPaths.end(), {
		"assets/fonts/default.ttf",
		"../assets/fonts/default.ttf",
		"../../assets/fonts/default.ttf",
	});
#ifdef _WIN32
	// Windows system fonts — Workbench-aligned monospace first, then JP
	// fallbacks. Mono fonts (Cascadia / Consolas) survive SDF rasterisation
	// at small sizes much better than Yu Gothic's thin variable strokes,
	// which were rendering as smudgy/unreadable at 16px in the inspector.
	const char* winFonts = std::getenv("WINDIR");
	if (winFonts)
	{
		const std::string fontsDir = std::string(winFonts) + "\\Fonts\\";
		// ── Monospace family (Workbench primary) ────────────────────
		searchPaths.push_back(fontsDir + "CascadiaMono.ttf");      // Cascadia Mono (Win 11 default)
		searchPaths.push_back(fontsDir + "CascadiaCode.ttf");      // Cascadia Code (Win 10+ Terminal)
		searchPaths.push_back(fontsDir + "consola.ttf");           // Consolas (Vista+ ubiquitous)
		// ── Japanese fallbacks (CJK glyph coverage) ─────────────────
		searchPaths.push_back(fontsDir + "YuGothM.ttc");           // Yu Gothic Medium
		searchPaths.push_back(fontsDir + "YuGothR.ttc");           // Yu Gothic Regular
		searchPaths.push_back(fontsDir + "meiryo.ttc");            // Meiryo
		searchPaths.push_back(fontsDir + "msgothic.ttc");          // MS Gothic
		// ── Sans-serif ultimate fallback ────────────────────────────
		searchPaths.push_back(fontsDir + "segoeui.ttf");           // Segoe UI
	}
#endif
	searchPaths.insert(searchPaths.end(), {
		"external/nanovg/example/Roboto-Regular.ttf",
		"../external/nanovg/example/Roboto-Regular.ttf",
		"../../external/nanovg/example/Roboto-Regular.ttf",
	});

	// TTFファイルを読み込む
	std::vector<std::uint8_t> fontData;
	for (const auto& path : searchPaths)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file.is_open()) { continue; }
		const auto size = file.tellg();
		if (size <= 0) { continue; }
		file.seekg(0);
		fontData.resize(static_cast<std::size_t>(size));
		file.read(reinterpret_cast<char*>(fontData.data()), size);
		break;
	}

	if (fontData.empty() || !m_screen) { return; }

	// フォントデータをSDF用にコピー（TrueTypeFontがmoveで所有権を取るため）
	auto sdfFontData = fontData;

	// TrueTypeFontを構築（SDF非対応グリフのフォールバック用）
	try
	{
		m_ttfFont = std::make_unique<vn::TrueTypeFont>(std::move(fontData));
	}
	catch (const std::exception&)
	{
		m_ttfFont.reset();
		return;
	}
	if (!m_ttfFont || !m_ttfFont->valid()) { m_ttfFont.reset(); return; }

	// SDFフォントアトラスを構築
	initSdfFont(std::move(sdfFontData));

	if (m_sdfRenderer.ready())
	{
		// SDF描画 + TrueTypeFontフォールバックのハイブリッドコールバック
		m_screen->setTrueTypeFont(m_ttfFont.get(),
			[](void* f, Screen& s, float x, float y,
			   std::string_view text, float fontSize, const sgc::Colorf& color) {
				auto* font = static_cast<vn::TrueTypeFont*>(f);
				auto* engine = font->userData<Engine>();
				if (engine != nullptr && engine->m_sdfRenderer.ready()
					&& engine->sdfContainsAll(text))
				{
					engine->m_sdfRenderer.drawTextSoftware(s, text, x, y, fontSize, color);
				}
				else
				{
					render::TrueTypeScreenRenderer::drawText(*font, s, x, y, text, fontSize, color);
				}
			},
			[](void* f, std::string_view text, float fontSize) -> sgc::Vec2f {
				auto* font = static_cast<vn::TrueTypeFont*>(f);
				auto* engine = font->userData<Engine>();
				if (engine != nullptr && engine->m_sdfRenderer.ready()
					&& engine->sdfContainsAll(text))
				{
					const auto sz = engine->m_sdfRenderer.measureText(text, fontSize);
					return {sz.width, sz.height};
				}
				return {
					render::TrueTypeScreenRenderer::measureWidth(*font, text, fontSize),
					render::TrueTypeScreenRenderer::measureHeight(*font, fontSize)
				};
			});

		// TrueTypeFontにEngine*を保持させる（コールバックからアクセス用）
		m_ttfFont->setUserData(this);
	}
	else
	{
		// SDFが使えない場合はTrueTypeFont直接描画
		m_screen->setTrueTypeFont(m_ttfFont.get(),
			[](void* f, Screen& s, float x, float y,
			   std::string_view text, float fontSize, const sgc::Colorf& color) {
				render::TrueTypeScreenRenderer::drawText(
					*static_cast<vn::TrueTypeFont*>(f), s, x, y, text, fontSize, color);
			},
			[](void* f, std::string_view text, float fontSize) -> sgc::Vec2f {
				auto* font = static_cast<vn::TrueTypeFont*>(f);
				return {
					render::TrueTypeScreenRenderer::measureWidth(*font, text, fontSize),
					render::TrueTypeScreenRenderer::measureHeight(*font, fontSize)
				};
			});
	}
}

MITIRU_INLINE void mitiru::Engine::initSdfFont(std::vector<std::uint8_t> fontData)
{
	try
	{
		// SDF padding bumped to 12 (was 6). At 16px display scale (0.5x of
		// 32px atlas) padding shrinks to half — 6px padding becomes 3px,
		// which is too narrow for the smoothstep edge AA to render thin
		// vertical strokes (l / i / | / digits) cleanly. 12px padding gives
		// 6px effective at 0.5x — enough for crisp small-text rendering.
		// Atlas texture grows ~15% for ASCII-only; acceptable cost.
		m_sdfAtlas = std::make_unique<render::SdfFontAtlas>(
			std::move(fontData), 32.0f, 12);

		// config.fontAtlasRanges の bitmask に従って glyph 範囲を登録する。
		// Japanese (default) は 3000+ kanji を焼くため ~15s、
		// Latin は ASCII のみ (~1s 以下)。
		using FA = EngineConfig::FontAtlas;
		const auto ranges = m_config.fontAtlasRanges;
		if (hasFontAtlasRange(ranges, FA::Ascii))          m_sdfAtlas->addAsciiRange();
		if (hasFontAtlasRange(ranges, FA::Hiragana))       m_sdfAtlas->addHiraganaRange();
		if (hasFontAtlasRange(ranges, FA::Katakana))       m_sdfAtlas->addKatakanaRange();
		if (hasFontAtlasRange(ranges, FA::CjkPunctuation)) m_sdfAtlas->addCjkPunctuationRange();
		if (hasFontAtlasRange(ranges, FA::Fullwidth))      m_sdfAtlas->addFullwidthRange();
		if (hasFontAtlasRange(ranges, FA::CommonKanji))    m_sdfAtlas->addCommonKanjiRange();

		if (!m_sdfAtlas->buildAtlas())
		{
			m_sdfAtlas.reset();
			return;
		}

		m_sdfRenderer.setAtlas(*m_sdfAtlas);
	}
	catch (const std::exception&)
	{
		m_sdfAtlas.reset();
	}
}
