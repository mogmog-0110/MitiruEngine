#pragma once

/// @file DxTextRenderer.hpp
/// @brief MitiruEngine用テキストレンダラー
///
/// フォント登録とテキスト計測・描画を提供する。
/// 現在はモノスペース推定による簡易実装。
///
/// @code
/// mitiru::render::DxTextRenderer textRenderer;
/// textRenderer.registerFont("default", 16.0f, {});
/// auto size = textRenderer.measureText("Hello", 16.0f);
/// @endcode

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>

#include <sgc/graphics/IRenderer2D.hpp>
#include <sgc/math/Vec2.hpp>

namespace mitiru::render
{

/// @brief グリフ情報
struct GlyphInfo
{
	char32_t codepoint = 0;  ///< Unicodeコードポイント
	float advance = 0.0f;   ///< 送り幅
	float bearingX = 0.0f;  ///< X方向ベアリング
	float bearingY = 0.0f;  ///< Y方向ベアリング
	float width = 0.0f;     ///< グリフ幅
	float height = 0.0f;    ///< グリフ高さ
};

/// @brief フォントデータ
struct FontData
{
	std::string name;                          ///< フォント名
	float size = 0.0f;                         ///< フォントサイズ
	std::map<char32_t, GlyphInfo> glyphs;      ///< グリフマップ
};

/// @brief テキストレンダラー
///
/// フォントの登録・管理と、テキストの計測・描画を行う。
/// 現在の実装はモノスペース推定（各文字 = fontSize * 0.6 幅）で計算する。
/// 将来的にはFreeType/DirectWriteベースの実装に置き換え可能。
class DxTextRenderer
{
public:
	/// @brief フォントを登録する
	/// @param name フォント名
	/// @param size フォントサイズ
	/// @param data フォントデータ
	void registerFont(const std::string& name, float size, const FontData& data)
	{
		FontEntry entry{name, size, data};
		m_fonts[name] = std::move(entry);
	}

	/// @brief フォントが登録済みかを確認する
	/// @param name フォント名
	/// @return 登録済みならtrue
	[[nodiscard]] bool hasFont(const std::string& name) const
	{
		return m_fonts.find(name) != m_fonts.end();
	}

	/// @brief テキストのサイズを計測する
	/// @param text テキスト文字列（UTF-8）
	/// @param fontSize フォントサイズ
	/// @return テキストの幅と高さ
	///
	/// 現在はモノスペース推定: 各文字の幅 = fontSize * 0.6
	[[nodiscard]] sgc::Vec2f measureText(const std::string& text, float fontSize) const
	{
		const float charWidth = fontSize * MONOSPACE_WIDTH_FACTOR;
		const float width = charWidth * static_cast<float>(text.size());
		return {width, fontSize};
	}

	/// @brief テキストを描画する
	/// @param renderer 描画先のIRenderer2D
	/// @param pos 描画位置（左上）
	/// @param text テキスト文字列（UTF-8）
	/// @param fontSize フォントサイズ
	/// @param color テキスト色
	void drawText(sgc::graphics::IRenderer2D& renderer,
	              sgc::Vec2f pos,
	              const std::string& text,
	              float fontSize,
	              sgc::graphics::Color color)
	{
		renderer.drawText(pos, text, fontSize, color);
	}

private:
	/// @brief モノスペース推定の文字幅係数
	static constexpr float MONOSPACE_WIDTH_FACTOR = 0.6f;

	/// @brief フォント登録エントリ
	struct FontEntry
	{
		std::string name;  ///< フォント名
		float size = 0.0f; ///< サイズ
		FontData data;     ///< フォントデータ
	};

	std::unordered_map<std::string, FontEntry> m_fonts;  ///< 登録フォント
};

} // namespace mitiru::render
