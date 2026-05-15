#pragma once

/// @file TrueTypeFont.hpp
/// @brief stb_truetypeベースのTTFフォントレンダラー（後方互換ラッパー）
/// @details 実装は mitiru::vn::TrueTypeFont に統合されている。
///          このヘッダーは mitiru::render 名前空間での後方互換性を維持する。
///          新規コードでは mitiru::vn::TrueTypeFont を直接使用すること。

#include <mitiru/vn/TrueTypeFont.hpp>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace mitiru::render
{

/// @brief TTFフォントレンダラー（後方互換ラッパー）
/// @details 内部で mitiru::vn::TrueTypeFont に委譲する。
///          新規コードでは mitiru::vn::TrueTypeFont を直接使用すること。
///
/// @code
/// mitiru::render::TrueTypeFont font;
/// font.loadFromFile("assets/myfont.ttf");
/// auto tex = font.renderText("Hello World", 32.0f);
/// @endcode
class [[deprecated("Use mitiru::vn::TrueTypeFont instead")]] TrueTypeFont
{
public:
	/// @brief TTFファイルからフォントを読み込む
	/// @param path TTFファイルのパス
	/// @return 読み込み成功時 true
	bool loadFromFile(const std::string& path)
	{
		std::ifstream ifs(path, std::ios::binary | std::ios::ate);
		if (!ifs)
		{
			return false;
		}
		const auto size = ifs.tellg();
		ifs.seekg(0);
		std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
		ifs.read(reinterpret_cast<char*>(data.data()), size);

		try
		{
			m_impl = vn::TrueTypeFont(std::move(data));
		}
		catch (...)
		{
			return false;
		}

		return true;
	}

	/// @brief メモリバッファからフォントを読み込む
	/// @param data フォントデータのポインタ
	/// @param dataSize データサイズ（バイト）
	/// @return 読み込み成功時 true
	bool loadFromMemory(const std::uint8_t* data, int dataSize)
	{
		if (!data || dataSize < 16)
		{
			return false;
		}

		try
		{
			std::vector<std::uint8_t> fontData(data, data + dataSize);
			m_impl = vn::TrueTypeFont(std::move(fontData));
		}
		catch (...)
		{
			return false;
		}

		return true;
	}

	/// @brief テキストをTextureとしてラスタライズする
	/// @param text ASCII テキスト文字列
	/// @param fontSize フォントサイズ（ピクセル高）
	/// @return ラスタライズされたTexture（失敗時は空テクスチャ）
	[[nodiscard]] Texture renderText(const std::string& text, float fontSize) const
	{
		if (!m_impl.valid() || text.empty())
		{
			return {};
		}

		const auto metrics = m_impl.metrics(fontSize);
		const int lineH = static_cast<int>(metrics.ascent - metrics.descent) + 2;
		const int totalW = static_cast<int>(m_impl.measureText(text, fontSize));

		if (totalW <= 0)
		{
			return {};
		}

		// Rasterize using the vn::TrueTypeFont glyph API
		std::vector<std::uint8_t> pixels(
			static_cast<std::size_t>(totalW) * lineH * 4, 0);
		const int baseline = static_cast<int>(metrics.ascent);

		float xCursor = 0.0f;
		std::uint32_t prevCp = 0;
		vn::Utf8Iterator it(text.data(), text.size());

		// Need mutable access to getGlyph (cache), use const_cast
		// since the original render::TrueTypeFont also had mutable stbtt state
		auto& mutableImpl = const_cast<vn::TrueTypeFont&>(m_impl);

		while (it.hasNext())
		{
			const std::uint32_t cp = it.next();

			if (prevCp != 0)
			{
				xCursor += m_impl.kerning(prevCp, cp, fontSize);
			}

			const auto* glyph = mutableImpl.getGlyph(cp, fontSize);
			if (glyph && glyph->width > 0 && glyph->height > 0)
			{
				for (int y = 0; y < glyph->height; ++y)
				{
					for (int x = 0; x < glyph->width; ++x)
					{
						const int px = static_cast<int>(xCursor) + glyph->offsetX + x;
						const int py = baseline + glyph->offsetY + y;
						if (px >= 0 && px < totalW && py >= 0 && py < lineH)
						{
							const auto idx = static_cast<std::size_t>(
								(py * totalW + px) * 4);
							const std::uint8_t alpha =
								glyph->bitmap[static_cast<std::size_t>(y * glyph->width + x)];
							pixels[idx] = 255;
							pixels[idx + 1] = 255;
							pixels[idx + 2] = 255;
							pixels[idx + 3] = std::max(pixels[idx + 3], alpha);
						}
					}
				}
			}

			xCursor += m_impl.advanceWidth(cp, fontSize);
			prevCp = cp;
		}

		return Texture(totalW, lineH, pixels);
	}

	/// @brief フォントが読み込み済みか
	/// @return 読み込み済みなら true
	[[nodiscard]] bool loaded() const noexcept { return m_impl.valid(); }

	/// @brief 指定フォントサイズでの行の高さ（ピクセル）を返す
	/// @param fontSize フォントサイズ（ピクセル高）
	/// @return 行の高さ
	[[nodiscard]] int lineHeight(float fontSize) const
	{
		if (!m_impl.valid())
		{
			return 0;
		}
		const auto m = m_impl.metrics(fontSize);
		return static_cast<int>(m.ascent - m.descent) + 2;
	}

	/// @brief 指定フォントサイズでのテキスト幅（ピクセル）を返す
	/// @param text テキスト文字列
	/// @param fontSize フォントサイズ（ピクセル高）
	/// @return テキスト幅
	[[nodiscard]] int textWidth(const std::string& text, float fontSize) const
	{
		if (!m_impl.valid() || text.empty())
		{
			return 0;
		}
		return static_cast<int>(m_impl.measureText(text, fontSize));
	}

private:
	mutable vn::TrueTypeFont m_impl;  ///< 実装への委譲先
};

} // namespace mitiru::render
