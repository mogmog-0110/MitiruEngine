// mitiru::Engine の detail header — 直接 include 禁止。core/Engine.hpp 経由で include される
#pragma once

#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/resource/AssetPath.hpp>

#include <fstream>

// ── font 読み込み (TTF + SDF atlas) の class 外定義 ──────────────

MITIRU_INLINE void mitiru::Engine::initFont(const std::string& userPath)
{
	// フォント検索パスリスト（日本語対応フォントを優先）
	std::vector<std::string> searchPaths;
	if (!userPath.empty()) { searchPaths.push_back(userPath); }
	// 同梱の日本語フォントを最優先で探す。既定は普通フォント (M+ Rounded 1c)、
	// その後にレトロ (PixelMplus) を fallback として見る。CMake が host exe の隣へ
	// 配るので exe 相対を先に、その後リポジトリ相対も見る。これでネイティブ描画
	// (drawTextInRect 等) が日本語可になる — CascadiaMono 等の日本語非対応 mono
	// より先に拾わせるのが肝。host は --font-face で fontPath を明示指定する。
	const std::string exeFontDir = mitiru::resource::AssetPath::executableDir();
	searchPaths.push_back(exeFontDir + "/assets/fonts/MPLUSRounded1c-Regular.ttf");
	searchPaths.push_back(exeFontDir + "/assets/fonts/PixelMplus12-Regular.ttf");
	searchPaths.insert(searchPaths.end(), {
		"assets/fonts/MPLUSRounded1c-Regular.ttf",
		"../assets/fonts/MPLUSRounded1c-Regular.ttf",
		"../../assets/fonts/MPLUSRounded1c-Regular.ttf",
		"assets/fonts/PixelMplus12-Regular.ttf",
		"../assets/fonts/PixelMplus12-Regular.ttf",
		"../../assets/fonts/PixelMplus12-Regular.ttf",
		"assets/fonts/default.ttf",
		"../assets/fonts/default.ttf",
		"../../assets/fonts/default.ttf",
	});
#ifdef _WIN32
	// Windows system fonts — Workbench に揃えた monospace を先に、その後 JP
	// fallback。Mono font (Cascadia / Consolas) は小サイズでも SDF rasterise
	// に強く、Yu Gothic の細い variable stroke (inspector の 16px で smudgy /
	// 判読不能に rendering されていた) より遥かに良い。
	const char* winFonts = std::getenv("WINDIR");
	if (winFonts)
	{
		const std::string fontsDir = std::string(winFonts) + "\\Fonts\\";
		// ── Monospace 系 (Workbench の主フォント) ────────────────────
		searchPaths.push_back(fontsDir + "CascadiaMono.ttf");      // Cascadia Mono (Win 11 既定)
		searchPaths.push_back(fontsDir + "CascadiaCode.ttf");      // Cascadia Code (Win 10+ Terminal)
		searchPaths.push_back(fontsDir + "consola.ttf");           // Consolas (Vista+ で遍在)
		// ── 日本語 fallback (CJK glyph をカバー) ─────────────────
		searchPaths.push_back(fontsDir + "YuGothM.ttc");           // Yu Gothic Medium
		searchPaths.push_back(fontsDir + "YuGothR.ttc");           // Yu Gothic Regular
		searchPaths.push_back(fontsDir + "meiryo.ttc");            // Meiryo
		searchPaths.push_back(fontsDir + "msgothic.ttc");          // MS Gothic
		// ── Sans-serif の最終 fallback ────────────────────────────
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

	// SDF アトラスは文字エフェクト用。
	initSdfFont(std::move(sdfFontData));

	// 既定の文字描画と計測を GlyphAtlasRenderer に任せる。
	m_screen->setTrueTypeFont(m_ttfFont.get(),
		[](void* f, Screen& s, float x, float y,
		   std::string_view text, float fontSize, const sgc::Colorf& color) {
			auto* font = static_cast<vn::TrueTypeFont*>(f);
			auto* engine = font->userData<Engine>();
			if (engine != nullptr)
			{
				engine->m_glyphRenderer.drawString(s, *font, text, x, y, fontSize, color);
			}
		},
		[](void* f, std::string_view text, float fontSize) -> sgc::Vec2f {
			return render::GlyphAtlasRenderer::measure(
				*static_cast<vn::TrueTypeFont*>(f), text, fontSize);
		});

	// 描画コールバックから Engine を辿れるようにする。
	m_ttfFont->setUserData(this);
}

MITIRU_INLINE void mitiru::Engine::initSdfFont(std::vector<std::uint8_t> fontData)
{
	try
	{
		// SDF padding は 12。表示を縮めても、細い縦線 (l / i / 数字) の輪郭を保てる幅。
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
