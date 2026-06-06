#pragma once

/// @file Texture.hpp
/// @brief テクスチャ抽象化
/// @details RGBA8ピクセルバッファを保持するテクスチャクラス。
///          stb_image等の外部ライブラリなしで使用可能な
///          プロシージャルテクスチャ生成機能を提供する。

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <mitiru/asset/AssetPack.hpp> // vfs::readGlobal (pack 秘匿配布 / disk fallback, ADR 0016)
#include <stb_image.h> // 宣言のみ。実装は src/stb_impl.cpp (mitiru が INTERFACE-link 済み)。

namespace mitiru::render
{

/// @brief テクスチャデータ（RGBA8ピクセルバッファ）
/// @details 任意の画像ローダーからピクセルデータを受け取り保持する。
///          solid(), checker() 等のファクトリでプロシージャルテクスチャも生成可能。
class Texture
{
public:
	/// @brief デフォルトコンストラクタ（空テクスチャ）
	Texture() = default;

	/// @brief ピクセルデータからテクスチャを構築する
	/// @param width 幅（ピクセル）
	/// @param height 高さ（ピクセル）
	/// @param pixels RGBA8形式のピクセルバッファ
	Texture(int width, int height, const std::vector<std::uint8_t>& pixels)
		: m_width(width)
		, m_height(height)
		, m_pixels(pixels)
	{
	}

	/// @brief 画像ファイル (PNG/JPG/BMP/TGA 等) から Texture を読み込む。
	/// @details stb_image 経由で RGBA8 に decode する。**consumer DLL でもそのまま使える** —
	///          `mitiru` ターゲットが `stb_impl` を INTERFACE-link しているため、stbi_load の
	///          実装は既にリンク済み。**`STB_IMAGE_IMPLEMENTATION` を自前で定義しないこと**
	///          (二重シンボル LNK2005 になる)。描画は `Screen::drawSprite(tex, dstRect)` を使う
	///          (バッチ＆カメラ変換対応。`drawPixelGrid` は変換無視の即時 blit なので非推奨)。
	/// @param path 画像ファイルパス
	/// @return 成功時 Texture、失敗時 nullopt
	[[nodiscard]] static std::optional<Texture> fromFile(const std::string& path)
	{
		// pack が mount 済みなら pack から、未 mount (dev) なら disk から読む (ADR 0016)。
		const auto bytes = mitiru::vfs::readGlobal(path, path);
		if (!bytes) { return std::nullopt; }
		int w = 0, h = 0, channels = 0;
		unsigned char* data = stbi_load_from_memory(
			bytes->data(), static_cast<int>(bytes->size()), &w, &h, &channels, 4); // 強制 RGBA
		if (data == nullptr || w <= 0 || h <= 0)
		{
			if (data != nullptr) { stbi_image_free(data); }
			return std::nullopt;
		}
		std::vector<std::uint8_t> px(
			data, data + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
		stbi_image_free(data);
		return Texture(w, h, px);
	}

	/// @brief 単色テクスチャを生成する
	/// @param w 幅
	/// @param h 高さ
	/// @param r 赤（0-255）
	/// @param g 緑（0-255）
	/// @param b 青（0-255）
	/// @param a アルファ（0-255）
	/// @return 単色テクスチャ
	[[nodiscard]] static Texture solid(int w, int h,
		std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255)
	{
		std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
		for (int i = 0; i < w * h; ++i)
		{
			px[static_cast<std::size_t>(i) * 4 + 0] = r;
			px[static_cast<std::size_t>(i) * 4 + 1] = g;
			px[static_cast<std::size_t>(i) * 4 + 2] = b;
			px[static_cast<std::size_t>(i) * 4 + 3] = a;
		}
		return {w, h, px};
	}

	/// @brief チェッカーパターンテクスチャを生成する
	/// @param w 幅
	/// @param h 高さ
	/// @param tileSize タイルサイズ（ピクセル）
	/// @param r1 色1の赤
	/// @param g1 色1の緑
	/// @param b1 色1の青
	/// @param r2 色2の赤
	/// @param g2 色2の緑
	/// @param b2 色2の青
	/// @return チェッカーパターンテクスチャ
	[[nodiscard]] static Texture checker(int w, int h, int tileSize,
		std::uint8_t r1, std::uint8_t g1, std::uint8_t b1,
		std::uint8_t r2, std::uint8_t g2, std::uint8_t b2)
	{
		std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
		for (int y = 0; y < h; ++y)
		{
			for (int x = 0; x < w; ++x)
			{
				const bool dark = ((x / tileSize + y / tileSize) % 2) == 0;
				const auto i = static_cast<std::size_t>((y * w + x) * 4);
				px[i + 0] = dark ? r1 : r2;
				px[i + 1] = dark ? g1 : g2;
				px[i + 2] = dark ? b1 : b2;
				px[i + 3] = 255;
			}
		}
		return {w, h, px};
	}

	/// @brief テクスチャ幅を取得する
	[[nodiscard]] int width() const noexcept { return m_width; }

	/// @brief テクスチャ高さを取得する
	[[nodiscard]] int height() const noexcept { return m_height; }

	/// @brief ピクセルデータを取得する
	[[nodiscard]] const std::vector<std::uint8_t>& pixels() const noexcept { return m_pixels; }

	/// @brief 有効なテクスチャか
	[[nodiscard]] bool valid() const noexcept
	{
		return m_width > 0 && m_height > 0 && !m_pixels.empty();
	}

	/// @brief 指定座標のピクセル色を取得する（RGBA packed）
	/// @param x X座標
	/// @param y Y座標
	/// @return RGBA32値（範囲外は0）
	[[nodiscard]] std::uint32_t pixelAt(int x, int y) const
	{
		if (x < 0 || x >= m_width || y < 0 || y >= m_height) return 0;
		const auto i = static_cast<std::size_t>((y * m_width + x) * 4);
		return (static_cast<std::uint32_t>(m_pixels[i]) << 24) |
		       (static_cast<std::uint32_t>(m_pixels[i + 1]) << 16) |
		       (static_cast<std::uint32_t>(m_pixels[i + 2]) << 8) |
		       static_cast<std::uint32_t>(m_pixels[i + 3]);
	}

private:
	int m_width = 0;                    ///< テクスチャ幅
	int m_height = 0;                   ///< テクスチャ高さ
	std::vector<std::uint8_t> m_pixels; ///< RGBA8ピクセルバッファ
};

} // namespace mitiru::render
