#pragma once

/// @file TextureAtlas.hpp
/// @brief 自動テクスチャアトラス（Ebiten風）
/// @details 小さなテクスチャを1枚の大きなアトラスに統合し、
///          描画バッチを最適化する。簡易行パッキングアルゴリズムを使用。
///
/// @code
/// mitiru::render::TextureAtlas atlas(1024);
/// auto region = atlas.add("player", playerTex);
/// // UV座標: region.u0(), region.v0(), region.u1(), region.v1()
/// @endcode

#include <mitiru/render/Texture.hpp>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace mitiru::render
{

/// @brief アトラス内のサブ領域
/// @details アトラス内での位置・サイズとUV座標の計算を提供する。
struct AtlasRegion
{
	int x = 0;            ///< アトラス内X位置
	int y = 0;            ///< アトラス内Y位置
	int width = 0;        ///< 領域の幅
	int height = 0;       ///< 領域の高さ
	int atlasWidth = 0;   ///< アトラス全体の幅
	int atlasHeight = 0;  ///< アトラス全体の高さ

	/// @brief 左上のU座標
	[[nodiscard]] float u0() const noexcept
	{
		return (atlasWidth > 0) ? static_cast<float>(x) / atlasWidth : 0;
	}

	/// @brief 左上のV座標
	[[nodiscard]] float v0() const noexcept
	{
		return (atlasHeight > 0) ? static_cast<float>(y) / atlasHeight : 0;
	}

	/// @brief 右下のU座標
	[[nodiscard]] float u1() const noexcept
	{
		return (atlasWidth > 0) ? static_cast<float>(x + width) / atlasWidth : 0;
	}

	/// @brief 右下のV座標
	[[nodiscard]] float v1() const noexcept
	{
		return (atlasHeight > 0) ? static_cast<float>(y + height) / atlasHeight : 0;
	}

	/// @brief 有効な領域か
	[[nodiscard]] bool valid() const noexcept
	{
		return width > 0 && height > 0;
	}
};

/// @brief 自動テクスチャアトラス
/// @details 簡易行パッキングで小テクスチャを大きな1枚に統合する。
///          GPU描画時のテクスチャ切り替えを削減しバッチ効率を向上させる。
class TextureAtlas
{
public:
	/// @brief コンストラクタ
	/// @param atlasSize アトラスの幅と高さ（正方形、デフォルト2048）
	explicit TextureAtlas(int atlasSize = 2048)
		: m_atlasSize(atlasSize)
		, m_cursorX(0)
		, m_cursorY(0)
		, m_rowHeight(0)
	{
		// 空のアトラステクスチャを初期化する
		std::vector<uint8_t> pixels(
			static_cast<std::size_t>(atlasSize) * atlasSize * 4, 0);
		m_atlas = Texture(atlasSize, atlasSize, pixels);
	}

	/// @brief テクスチャをアトラスに追加する（簡易行パッキング）
	/// @param name リソース名
	/// @param tex 追加するテクスチャ
	/// @return アトラス内の領域情報（追加失敗時はwidth=0）
	AtlasRegion add(const std::string& name, const Texture& tex)
	{
		if (!tex.valid()) return {};

		// 現在の行に収まらなければ改行する
		if (m_cursorX + tex.width() > m_atlasSize)
		{
			m_cursorX = 0;
			m_cursorY += m_rowHeight;
			m_rowHeight = 0;
		}

		// アトラスに収まらなければ失敗する
		if (m_cursorY + tex.height() > m_atlasSize)
		{
			return {};
		}

		// ピクセルをアトラスにコピーする
		auto& atlasPixels = const_cast<std::vector<uint8_t>&>(m_atlas.pixels());
		const auto& srcPixels = tex.pixels();

		for (int y = 0; y < tex.height(); ++y)
		{
			for (int x = 0; x < tex.width(); ++x)
			{
				const auto srcIdx = static_cast<std::size_t>(
					(y * tex.width() + x) * 4);
				const auto dstIdx = static_cast<std::size_t>(
					((m_cursorY + y) * m_atlasSize + (m_cursorX + x)) * 4);
				if (srcIdx + 3 < srcPixels.size() &&
				    dstIdx + 3 < atlasPixels.size())
				{
					atlasPixels[dstIdx]     = srcPixels[srcIdx];
					atlasPixels[dstIdx + 1] = srcPixels[srcIdx + 1];
					atlasPixels[dstIdx + 2] = srcPixels[srcIdx + 2];
					atlasPixels[dstIdx + 3] = srcPixels[srcIdx + 3];
				}
			}
		}

		AtlasRegion region{
			m_cursorX, m_cursorY,
			tex.width(), tex.height(),
			m_atlasSize, m_atlasSize
		};
		m_regions[name] = region;

		m_cursorX += tex.width();
		m_rowHeight = std::max(m_rowHeight, tex.height());

		return region;
	}

	/// @brief 名前でリージョンを取得する
	/// @param name リソース名
	/// @return 領域情報（未登録時はwidth=0）
	[[nodiscard]] AtlasRegion getRegion(const std::string& name) const
	{
		auto it = m_regions.find(name);
		return (it != m_regions.end()) ? it->second : AtlasRegion{};
	}

	/// @brief アトラステクスチャを取得する
	[[nodiscard]] const Texture& texture() const noexcept { return m_atlas; }

	/// @brief 登録済みリージョン数を取得する
	[[nodiscard]] int regionCount() const noexcept
	{
		return static_cast<int>(m_regions.size());
	}

	/// @brief アトラスサイズを取得する
	[[nodiscard]] int atlasSize() const noexcept { return m_atlasSize; }

private:
	Texture m_atlas;                                         ///< アトラステクスチャ
	int m_atlasSize;                                         ///< アトラスサイズ
	int m_cursorX;                                           ///< 現在のX書き込み位置
	int m_cursorY;                                           ///< 現在のY書き込み位置
	int m_rowHeight;                                         ///< 現在の行の高さ
	std::unordered_map<std::string, AtlasRegion> m_regions;  ///< 名前→領域マップ
};

} // namespace mitiru::render
