#pragma once
/// @file SpriteFont.hpp
/// @brief BMFont (AngelCode .fnt テキスト形式) のスプライト/ビットマップフォント。
/// @details 画像グリフ（プロポーショナル字幅）の見出しフォントを描くためのデータ層。
///          美咲の等幅ドット（PixelFont/PixelText）の兄弟で、こちらは .fnt + PNG ページを
///          読んで字幅可変・縁取り・色付きの「スタイル付き見出し」を出す。Screen に依存しない
///          純データ（描画は SpriteText.hpp）。本文の大量漢字は引き続き美咲 PixelText を使う。
///
/// @code
/// mitiru::render::pixel::SpriteFont font;
/// if (font.loadFile("assets/fonts/title.fnt")) { /* font.glyph(cp), font.page(0) */ }
/// @endcode

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <mitiru/render/Texture.hpp>
#include <mitiru/render/pixel/PixelFont.hpp> // decodeUtf8 共有

namespace mitiru::render::pixel
{

/// @brief 1 グリフのメトリクス（page テクスチャ内の矩形 + 配置）
struct SpriteGlyph
{
	int x = 0, y = 0, w = 0, h = 0;       ///< page テクスチャ内のソース矩形（ピクセル）
	int xoffset = 0, yoffset = 0;         ///< ペン位置からの描画オフセット
	int xadvance = 0;                     ///< 次グリフへの送り幅
	int page = 0;                         ///< 参照する PNG ページ番号
};

/// @brief BMFont スプライトフォント（テキスト .fnt 形式・複数ページ対応）
class SpriteFont
{
public:
	/// @brief .fnt ファイルを読み、PNG ページを同ディレクトリ基準で読み込む。
	[[nodiscard]] bool loadFile(const std::string& fntPath)
	{
		std::ifstream ifs(fntPath, std::ios::binary);
		if (!ifs) { m_error = "cannot open " + fntPath; return false; }
		std::stringstream ss; ss << ifs.rdbuf();
		const std::string baseDir = std::filesystem::path(fntPath).parent_path().string();
		return loadFromString(ss.str(), baseDir);
	}

	/// @brief .fnt テキストを解析し、PNG ページを baseDir 基準で読み込む。
	[[nodiscard]] bool loadFromString(const std::string& text, const std::string& baseDir)
	{
		reset();
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line))
		{
			const auto toks = tokenize(line);
			if (toks.empty()) continue;
			const std::string& tag = toks[0];
			if (tag == "common")
			{
				m_lineHeight = intOf(toks, "lineHeight", 0);
				m_base = intOf(toks, "base", 0);
			}
			else if (tag == "page")
			{
				const int id = intOf(toks, "id", 0);
				const std::string file = strOf(toks, "file");
				if (!file.empty()) loadPage(id, baseDir, file);
			}
			else if (tag == "char")
			{
				SpriteGlyph g;
				const auto cp = static_cast<std::uint32_t>(intOf(toks, "id", 0));
				g.x = intOf(toks, "x", 0); g.y = intOf(toks, "y", 0);
				g.w = intOf(toks, "width", 0); g.h = intOf(toks, "height", 0);
				g.xoffset = intOf(toks, "xoffset", 0); g.yoffset = intOf(toks, "yoffset", 0);
				g.xadvance = intOf(toks, "xadvance", 0); g.page = intOf(toks, "page", 0);
				m_glyphs[cp] = g;
			}
			else if (tag == "kerning")
			{
				const auto a = static_cast<std::uint32_t>(intOf(toks, "first", 0));
				const auto b = static_cast<std::uint32_t>(intOf(toks, "second", 0));
				m_kerning[kernKey(a, b)] = intOf(toks, "amount", 0);
			}
		}
		if (m_glyphs.empty()) { m_error = "no glyphs parsed"; return false; }
		return true;
	}

	[[nodiscard]] bool valid() const noexcept { return !m_glyphs.empty(); }
	[[nodiscard]] int glyphCount() const noexcept { return static_cast<int>(m_glyphs.size()); }
	[[nodiscard]] int lineHeight() const noexcept { return m_lineHeight > 0 ? m_lineHeight : 16; }
	[[nodiscard]] int base() const noexcept { return m_base; }
	[[nodiscard]] const std::string& error() const noexcept { return m_error; }

	/// @brief コードポイントのグリフを引く（無ければ nullptr）。
	[[nodiscard]] const SpriteGlyph* glyph(std::uint32_t cp) const
	{
		const auto it = m_glyphs.find(cp);
		return it == m_glyphs.end() ? nullptr : &it->second;
	}

	/// @brief first→second のカーニング量（無ければ 0）。
	[[nodiscard]] int kerning(std::uint32_t first, std::uint32_t second) const
	{
		const auto it = m_kerning.find(kernKey(first, second));
		return it == m_kerning.end() ? 0 : it->second;
	}

	/// @brief page 番号のテクスチャ（未ロード/範囲外は nullptr）。
	[[nodiscard]] const render::Texture* page(int idx) const
	{
		if (idx < 0 || idx >= static_cast<int>(m_pages.size())) return nullptr;
		return m_pages[static_cast<std::size_t>(idx)] ? &*m_pages[static_cast<std::size_t>(idx)] : nullptr;
	}

private:
	static std::uint64_t kernKey(std::uint32_t a, std::uint32_t b) noexcept
	{ return (static_cast<std::uint64_t>(a) << 32) | b; }

	void reset()
	{
		m_glyphs.clear(); m_kerning.clear(); m_pages.clear();
		m_lineHeight = 0; m_base = 0; m_error.clear();
	}

	void loadPage(int id, const std::string& baseDir, const std::string& file)
	{
		const std::filesystem::path full =
			baseDir.empty() ? std::filesystem::path(file) : std::filesystem::path(baseDir) / file;
		if (id >= static_cast<int>(m_pages.size())) m_pages.resize(static_cast<std::size_t>(id) + 1);
		m_pages[static_cast<std::size_t>(id)] = render::Texture::fromFile(full.string());
	}

	/// @brief BMFont の 1 行を「tag」と「key=value」トークンに分割（file="..." の引用に対応）。
	static std::vector<std::string> tokenize(const std::string& line)
	{
		std::vector<std::string> out;
		std::string cur; bool inQuote = false;
		for (char c : line)
		{
			if (c == '"') { inQuote = !inQuote; cur.push_back(c); }
			else if ((c == ' ' || c == '\t' || c == '\r') && !inQuote)
			{ if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
			else cur.push_back(c);
		}
		if (!cur.empty()) out.push_back(cur);
		return out;
	}

	static std::string rawValue(const std::vector<std::string>& toks, std::string_view key)
	{
		const std::string pref = std::string(key) + "=";
		for (std::size_t i = 1; i < toks.size(); ++i)
			if (toks[i].rfind(pref, 0) == 0) return toks[i].substr(pref.size());
		return {};
	}
	static int intOf(const std::vector<std::string>& toks, std::string_view key, int def)
	{
		const std::string v = rawValue(toks, key);
		if (v.empty()) return def;
		try { return std::stoi(v); } catch (...) { return def; }
	}
	static std::string strOf(const std::vector<std::string>& toks, std::string_view key)
	{
		std::string v = rawValue(toks, key);
		if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
		return v;
	}

	std::unordered_map<std::uint32_t, SpriteGlyph> m_glyphs;
	std::unordered_map<std::uint64_t, int> m_kerning;
	std::vector<std::optional<render::Texture>> m_pages;
	int m_lineHeight = 0, m_base = 0;
	std::string m_error;
};

} // namespace mitiru::render::pixel
