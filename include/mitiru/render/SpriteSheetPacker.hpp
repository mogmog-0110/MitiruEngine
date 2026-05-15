#pragma once

/// @file SpriteSheetPacker.hpp
/// @brief テクスチャアトラスパッキングツール
/// @details 複数のテクスチャをShelfパッキングアルゴリズムで1枚のアトラスに結合する。
///          Aseprite互換のJSONメタデータ出力にも対応。
///
/// @code
/// mitiru::render::SpriteSheetPacker packer;
/// packer.addImage("player_idle", playerTex);
/// packer.addImage("player_run", runTex);
/// auto result = packer.pack(1024, 1024, 2);
/// auto atlas = result.atlas;
/// auto json = packer.exportJson();
/// @endcode

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <sgc/math/Rect.hpp>

#include <mitiru/render/Texture.hpp>

namespace mitiru::render
{

/// @brief パッキングされたスプライト情報
struct PackedSprite
{
	std::string name;        ///< スプライト名
	sgc::Rectf sourceRect;   ///< 元テクスチャの矩形（0,0,w,h）
	sgc::Rectf destRect;     ///< アトラス上の矩形
};

/// @brief パッキング結果
struct PackResult
{
	Texture atlas;                          ///< パッキング済みアトラステクスチャ
	std::vector<PackedSprite> sprites;      ///< スプライト情報リスト
	int atlasWidth = 0;                     ///< アトラス幅
	int atlasHeight = 0;                    ///< アトラス高さ
	bool success = false;                   ///< パッキング成功フラグ
};

/// @brief テクスチャアトラスパッカー（Shelfパッキングアルゴリズム）
/// @details addImage()で画像を追加し、pack()で1枚のアトラスに結合する。
///          Power-of-2サイズのアトラスを生成する。
class SpriteSheetPacker
{
public:
	/// @brief コンストラクタ
	SpriteSheetPacker() = default;

	/// @brief ソース画像を追加する
	/// @param name スプライト名
	/// @param texture テクスチャデータ
	void addImage(std::string_view name, const Texture& texture)
	{
		m_sources.push_back(SourceImage{std::string(name), texture});
	}

	/// @brief ディレクトリ内の全画像を追加する（プレースホルダ）
	/// @details 実際のファイルI/OはPlatformレイヤーが担当するため、
	///          このメソッドは名前とサイズのみ登録する。
	/// @param path ディレクトリパス
	/// @param extension ファイル拡張子（例: ".png"）
	void addDirectory(std::string_view path, std::string_view extension)
	{
		// ファイルI/Oは外部で行い、addImage()で個別に追加する
		static_cast<void>(path);
		static_cast<void>(extension);
	}

	/// @brief Shelfパッキングを実行する
	/// @param maxWidth 最大アトラス幅
	/// @param maxHeight 最大アトラス高さ
	/// @param padding スプライト間のパディング（ピクセル）
	/// @return パッキング結果
	[[nodiscard]] PackResult pack(int maxWidth = 2048, int maxHeight = 2048, int padding = 1)
	{
		if (m_sources.empty())
		{
			return PackResult{};
		}

		// 高さ降順でソートする（Shelfパッキングの効率化）
		std::vector<std::size_t> order(m_sources.size());
		for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;

		std::sort(order.begin(), order.end(),
			[this](std::size_t a, std::size_t b) {
				return m_sources[a].texture.height() > m_sources[b].texture.height();
			});

		// Shelfパッキング
		std::vector<Shelf> shelves;
		std::vector<Placement> placements(m_sources.size());

		for (std::size_t idx : order)
		{
			const auto& src = m_sources[idx];
			const int imgW = src.texture.width() + padding * 2;
			const int imgH = src.texture.height() + padding * 2;

			bool placed = false;

			// 既存のShelfに収まるか試す
			for (auto& shelf : shelves)
			{
				if (shelf.cursorX + imgW <= maxWidth && imgH <= shelf.height)
				{
					placements[idx] = {shelf.cursorX + padding, shelf.y + padding};
					shelf.cursorX += imgW;
					placed = true;
					break;
				}
			}

			// 新しいShelfを作る
			if (!placed)
			{
				const int newY = shelves.empty()
					? 0
					: shelves.back().y + shelves.back().height;

				if (newY + imgH > maxHeight)
				{
					// アトラスに収まらない
					return PackResult{};
				}

				shelves.push_back(Shelf{newY, imgH, imgW});
				placements[idx] = {padding, newY + padding};
			}
		}

		// アトラスサイズを計算する（Power-of-2に切り上げ）
		int usedW = 0;
		int usedH = 0;
		for (const auto& shelf : shelves)
		{
			usedW = std::max(usedW, shelf.cursorX);
			usedH = std::max(usedH, shelf.y + shelf.height);
		}

		const int atlasW = nextPow2(usedW);
		const int atlasH = nextPow2(usedH);

		if (atlasW > maxWidth || atlasH > maxHeight)
		{
			return PackResult{};
		}

		// アトラステクスチャを生成する
		std::vector<std::uint8_t> atlasPixels(
			static_cast<std::size_t>(atlasW) * atlasH * 4, 0);

		PackResult result;
		result.atlasWidth = atlasW;
		result.atlasHeight = atlasH;
		result.sprites.reserve(m_sources.size());

		for (std::size_t i = 0; i < m_sources.size(); ++i)
		{
			const auto& src = m_sources[i];
			const auto& pl = placements[i];

			// ピクセルをコピーする
			blitTexture(atlasPixels, atlasW, atlasH,
			            pl.x, pl.y, src.texture);

			// スプライト情報を記録する
			PackedSprite sprite;
			sprite.name = src.name;
			sprite.sourceRect = sgc::Rectf{
				0.0f, 0.0f,
				static_cast<float>(src.texture.width()),
				static_cast<float>(src.texture.height())};
			sprite.destRect = sgc::Rectf{
				static_cast<float>(pl.x),
				static_cast<float>(pl.y),
				static_cast<float>(src.texture.width()),
				static_cast<float>(src.texture.height())};
			result.sprites.push_back(std::move(sprite));
		}

		result.atlas = Texture{atlasW, atlasH, atlasPixels};
		result.success = true;

		m_lastResult = result;
		return result;
	}

	/// @brief Aseprite互換のJSONメタデータを出力する
	/// @return JSON文字列
	[[nodiscard]] std::string exportJson() const
	{
		std::ostringstream oss;
		oss << "{\n";
		oss << "  \"frames\": {\n";

		for (std::size_t i = 0; i < m_lastResult.sprites.size(); ++i)
		{
			const auto& s = m_lastResult.sprites[i];
			oss << "    \"" << s.name << "\": {\n";
			oss << "      \"frame\": { "
			    << "\"x\": " << static_cast<int>(s.destRect.x()) << ", "
			    << "\"y\": " << static_cast<int>(s.destRect.y()) << ", "
			    << "\"w\": " << static_cast<int>(s.destRect.width()) << ", "
			    << "\"h\": " << static_cast<int>(s.destRect.height())
			    << " },\n";
			oss << "      \"rotated\": false,\n";
			oss << "      \"trimmed\": false,\n";
			oss << "      \"spriteSourceSize\": { "
			    << "\"x\": 0, \"y\": 0, "
			    << "\"w\": " << static_cast<int>(s.sourceRect.width()) << ", "
			    << "\"h\": " << static_cast<int>(s.sourceRect.height())
			    << " },\n";
			oss << "      \"sourceSize\": { "
			    << "\"w\": " << static_cast<int>(s.sourceRect.width()) << ", "
			    << "\"h\": " << static_cast<int>(s.sourceRect.height())
			    << " }\n";
			oss << "    }";
			if (i + 1 < m_lastResult.sprites.size()) oss << ",";
			oss << "\n";
		}

		oss << "  },\n";
		oss << "  \"meta\": {\n";
		oss << "    \"app\": \"MitiruEngine SpriteSheetPacker\",\n";
		oss << "    \"version\": \"1.0\",\n";
		oss << "    \"format\": \"RGBA8888\",\n";
		oss << "    \"size\": { "
		    << "\"w\": " << m_lastResult.atlasWidth << ", "
		    << "\"h\": " << m_lastResult.atlasHeight
		    << " },\n";
		oss << "    \"scale\": \"1\"\n";
		oss << "  }\n";
		oss << "}\n";

		return oss.str();
	}

	/// @brief パッキング済みアトラステクスチャを取得する
	/// @return アトラステクスチャ（pack()未実行時は空テクスチャ）
	[[nodiscard]] Texture exportTexture() const
	{
		return m_lastResult.atlas;
	}

	/// @brief ソース画像をクリアする
	void clear()
	{
		m_sources.clear();
		m_lastResult = PackResult{};
	}

	/// @brief 登録済みソース画像数を取得する
	[[nodiscard]] std::size_t sourceCount() const noexcept { return m_sources.size(); }

private:
	/// @brief ソース画像
	struct SourceImage
	{
		std::string name;
		Texture texture;
	};

	/// @brief パッキングShelf
	struct Shelf
	{
		int y = 0;           ///< Shelf上端Y座標
		int height = 0;      ///< Shelf高さ
		int cursorX = 0;     ///< 現在のX書き込み位置
	};

	/// @brief 配置座標
	struct Placement
	{
		int x = 0;
		int y = 0;
	};

	std::vector<SourceImage> m_sources;
	PackResult m_lastResult;

	/// @brief 次のPower-of-2値を計算する
	[[nodiscard]] static int nextPow2(int v) noexcept
	{
		if (v <= 0) return 1;
		int p = 1;
		while (p < v) p <<= 1;
		return p;
	}

	/// @brief テクスチャをアトラスピクセルバッファにコピーする
	static void blitTexture(std::vector<std::uint8_t>& dest,
	                         int destW, int destH,
	                         int dx, int dy,
	                         const Texture& src)
	{
		const auto& srcPx = src.pixels();
		const int srcW = src.width();
		const int srcH = src.height();

		for (int y = 0; y < srcH; ++y)
		{
			const int destY = dy + y;
			if (destY < 0 || destY >= destH) continue;

			for (int x = 0; x < srcW; ++x)
			{
				const int destX = dx + x;
				if (destX < 0 || destX >= destW) continue;

				const auto srcIdx = static_cast<std::size_t>((y * srcW + x) * 4);
				const auto dstIdx = static_cast<std::size_t>((destY * destW + destX) * 4);

				if (srcIdx + 3 >= srcPx.size()) continue;
				if (dstIdx + 3 >= dest.size()) continue;

				dest[dstIdx + 0] = srcPx[srcIdx + 0];
				dest[dstIdx + 1] = srcPx[srcIdx + 1];
				dest[dstIdx + 2] = srcPx[srcIdx + 2];
				dest[dstIdx + 3] = srcPx[srcIdx + 3];
			}
		}
	}
};

} // namespace mitiru::render
