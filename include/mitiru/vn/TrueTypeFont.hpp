#pragma once

/// @file TrueTypeFont.hpp
/// @brief stb_truetypeベースのTrueTypeフォントレンダラー
/// @details TTF/OTFファイルからグリフをラスタライズし、テクスチャアトラスを生成する。
///          LRUキャッシュによるグリフ管理、UTF-8デコード、カーニング、
///          日本語を含む全Unicode範囲をサポートする。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <mitiru/render/Texture.hpp>

// stb_truetype をインクルード（実装は1つの .cpp で定義済みと想定）
#include <stb_truetype.h>

namespace mitiru::vn
{

// ── UTF-8 デコーダ ──────────────────────────────────────────

/// @brief UTF-8文字列からUnicodeコードポイントを順次取得するイテレータ
class Utf8Iterator
{
	const char* m_ptr = nullptr;
	const char* m_end = nullptr;

public:
	/// @brief UTF-8文字列範囲を指定して構築する
	/// @param str 文字列先頭
	/// @param len バイト長
	Utf8Iterator(const char* str, std::size_t len) noexcept
		: m_ptr(str)
		, m_end(str + len)
	{
	}

	/// @brief UTF-8文字列ビューから構築する
	/// @param sv 文字列ビュー
	explicit Utf8Iterator(std::string_view sv) noexcept
		: m_ptr(sv.data())
		, m_end(sv.data() + sv.size())
	{
	}

	/// @brief まだコードポイントが残っているか
	[[nodiscard]] bool hasNext() const noexcept
	{
		return m_ptr < m_end;
	}

	/// @brief 次のコードポイントを取得して内部ポインタを進める
	/// @return Unicodeコードポイント（不正シーケンスの場合は0xFFFD）
	[[nodiscard]] std::uint32_t next() noexcept
	{
		if (m_ptr >= m_end)
		{
			return 0;
		}

		const auto byte0 = static_cast<std::uint8_t>(*m_ptr);

		// 1バイト (0xxxxxxx)
		if (byte0 < 0x80)
		{
			++m_ptr;
			return byte0;
		}

		// 2バイト (110xxxxx 10xxxxxx)
		if ((byte0 & 0xE0) == 0xC0)
		{
			if (m_ptr + 1 >= m_end)
			{
				m_ptr = m_end;
				return 0xFFFD;
			}
			const auto byte1 = static_cast<std::uint8_t>(m_ptr[1]);
			if ((byte1 & 0xC0) != 0x80)
			{
				++m_ptr;
				return 0xFFFD;
			}
			m_ptr += 2;
			const std::uint32_t cp = (static_cast<std::uint32_t>(byte0 & 0x1F) << 6)
				| static_cast<std::uint32_t>(byte1 & 0x3F);
			return (cp >= 0x80) ? cp : 0xFFFD;
		}

		// 3バイト (1110xxxx 10xxxxxx 10xxxxxx)
		if ((byte0 & 0xF0) == 0xE0)
		{
			if (m_ptr + 2 >= m_end)
			{
				m_ptr = m_end;
				return 0xFFFD;
			}
			const auto byte1 = static_cast<std::uint8_t>(m_ptr[1]);
			const auto byte2 = static_cast<std::uint8_t>(m_ptr[2]);
			if ((byte1 & 0xC0) != 0x80 || (byte2 & 0xC0) != 0x80)
			{
				++m_ptr;
				return 0xFFFD;
			}
			m_ptr += 3;
			const std::uint32_t cp = (static_cast<std::uint32_t>(byte0 & 0x0F) << 12)
				| (static_cast<std::uint32_t>(byte1 & 0x3F) << 6)
				| static_cast<std::uint32_t>(byte2 & 0x3F);
			return (cp >= 0x800) ? cp : 0xFFFD;
		}

		// 4バイト (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
		if ((byte0 & 0xF8) == 0xF0)
		{
			if (m_ptr + 3 >= m_end)
			{
				m_ptr = m_end;
				return 0xFFFD;
			}
			const auto byte1 = static_cast<std::uint8_t>(m_ptr[1]);
			const auto byte2 = static_cast<std::uint8_t>(m_ptr[2]);
			const auto byte3 = static_cast<std::uint8_t>(m_ptr[3]);
			if ((byte1 & 0xC0) != 0x80 || (byte2 & 0xC0) != 0x80 || (byte3 & 0xC0) != 0x80)
			{
				++m_ptr;
				return 0xFFFD;
			}
			m_ptr += 4;
			const std::uint32_t cp = (static_cast<std::uint32_t>(byte0 & 0x07) << 18)
				| (static_cast<std::uint32_t>(byte1 & 0x3F) << 12)
				| (static_cast<std::uint32_t>(byte2 & 0x3F) << 6)
				| static_cast<std::uint32_t>(byte3 & 0x3F);
			return (cp >= 0x10000 && cp <= 0x10FFFF) ? cp : 0xFFFD;
		}

		// 不正なバイト
		++m_ptr;
		return 0xFFFD;
	}
};

/// @brief UTF-8文字列のコードポイント数を数える
/// @param sv UTF-8文字列ビュー
/// @return コードポイント数
[[nodiscard]] inline std::size_t utf8Length(std::string_view sv) noexcept
{
	std::size_t count = 0;
	Utf8Iterator it(sv);
	while (it.hasNext())
	{
		static_cast<void>(it.next());
		++count;
	}
	return count;
}

/// @brief UTF-8文字列をコードポイント配列に変換する
/// @param sv UTF-8文字列ビュー
/// @return コードポイントのベクタ
[[nodiscard]] inline std::vector<std::uint32_t> utf8ToCodepoints(std::string_view sv)
{
	std::vector<std::uint32_t> result;
	result.reserve(sv.size()); // 最大バイト数分確保（実際はもっと少ない）
	Utf8Iterator it(sv);
	while (it.hasNext())
	{
		result.push_back(it.next());
	}
	return result;
}

// ── グリフ情報 ──────────────────────────────────────────────

/// @brief ラスタライズ済みグリフの情報
struct GlyphInfo
{
	std::uint32_t codepoint = 0;     ///< Unicodeコードポイント
	int width = 0;                    ///< ビットマップ幅（ピクセル）
	int height = 0;                   ///< ビットマップ高さ（ピクセル）
	int offsetX = 0;                  ///< ベースラインからのX方向オフセット
	int offsetY = 0;                  ///< ベースラインからのY方向オフセット
	float advanceX = 0.0f;           ///< 次の文字までのX方向前進幅
	int atlasX = 0;                   ///< アトラス上のX座標
	int atlasY = 0;                   ///< アトラス上のY座標
	std::vector<std::uint8_t> bitmap; ///< グレースケールビットマップ（width*height）
};

/// @brief フォントメトリクス
struct FontMetrics
{
	float ascent = 0.0f;    ///< ベースラインからの上方向の高さ
	float descent = 0.0f;   ///< ベースラインからの下方向の深さ（通常負値）
	float lineGap = 0.0f;   ///< 行間の追加スペース
	float lineHeight = 0.0f; ///< 行の高さ（ascent - descent + lineGap）
	float pixelSize = 0.0f;  ///< フォントサイズ（ピクセル）
};

// ── LRU グリフキャッシュ ─────────────────────────────────────

/// @brief LRUポリシーのグリフキャッシュ
/// @details 最大容量に達すると最も古いエントリを削除する。
class GlyphCache
{
	/// @brief キャッシュキー: コードポイント + サイズ
	struct CacheKey
	{
		std::uint32_t codepoint = 0;
		float pixelSize = 0.0f;

		[[nodiscard]] bool operator==(const CacheKey& other) const noexcept
		{
			return codepoint == other.codepoint
				&& std::abs(pixelSize - other.pixelSize) < 0.01f;
		}
	};

	struct CacheKeyHash
	{
		[[nodiscard]] std::size_t operator()(const CacheKey& key) const noexcept
		{
			const auto h1 = std::hash<std::uint32_t>{}(key.codepoint);
			const auto h2 = std::hash<int>{}(static_cast<int>(key.pixelSize * 100.0f));
			return h1 ^ (h2 << 16);
		}
	};

	using ListIterator = std::list<CacheKey>::iterator;

	std::size_t m_maxSize;
	std::list<CacheKey> m_lruList;
	std::unordered_map<CacheKey, std::pair<GlyphInfo, ListIterator>, CacheKeyHash> m_map;

public:
	/// @brief キャッシュを構築する
	/// @param maxSize 最大エントリ数
	explicit GlyphCache(std::size_t maxSize = 4096)
		: m_maxSize(maxSize)
	{
	}

	/// @brief キャッシュからグリフを検索する
	/// @param codepoint Unicodeコードポイント
	/// @param pixelSize フォントサイズ
	/// @return 見つかった場合はグリフ情報へのポインタ、なければnullptr
	[[nodiscard]] const GlyphInfo* find(std::uint32_t codepoint, float pixelSize)
	{
		const CacheKey key{codepoint, pixelSize};
		const auto it = m_map.find(key);
		if (it == m_map.end())
		{
			return nullptr;
		}
		// LRUリストの先頭に移動
		m_lruList.erase(it->second.second);
		m_lruList.push_front(key);
		it->second.second = m_lruList.begin();
		return &it->second.first;
	}

	/// @brief グリフをキャッシュに追加する
	/// @param glyph 追加するグリフ情報
	/// @param pixelSize フォントサイズ
	void insert(const GlyphInfo& glyph, float pixelSize)
	{
		const CacheKey key{glyph.codepoint, pixelSize};

		// 既存エントリがあれば更新
		const auto existing = m_map.find(key);
		if (existing != m_map.end())
		{
			m_lruList.erase(existing->second.second);
			m_lruList.push_front(key);
			existing->second = {glyph, m_lruList.begin()};
			return;
		}

		// 容量超過時は最古のエントリを削除
		if (m_map.size() >= m_maxSize)
		{
			const auto& oldest = m_lruList.back();
			m_map.erase(oldest);
			m_lruList.pop_back();
		}

		m_lruList.push_front(key);
		m_map.emplace(key, std::pair{glyph, m_lruList.begin()});
	}

	/// @brief キャッシュをクリアする
	void clear()
	{
		m_map.clear();
		m_lruList.clear();
	}

	/// @brief キャッシュ内のエントリ数
	[[nodiscard]] std::size_t size() const noexcept { return m_map.size(); }
};

// ── TrueTypeFont ─────────────────────────────────────────────

/// @brief stb_truetypeベースのTrueTypeフォントレンダラー
/// @details TTFバイナリデータを保持し、任意サイズでグリフをラスタライズする。
///          LRUキャッシュによる効率的なグリフ管理と、テクスチャアトラス生成を提供する。
///
/// @code
/// // TTFファイルを読み込んでフォントを作成
/// std::vector<std::uint8_t> ttfData = loadFile("font.ttf");
/// mitiru::vn::TrueTypeFont font(std::move(ttfData));
///
/// // グリフを取得
/// const auto* glyph = font.getGlyph(U'A', 24.0f);
///
/// // テクスチャアトラスを生成
/// auto atlas = font.generateAtlas(24.0f, U"ABCあいう");
/// @endcode
class TrueTypeFont
{
	std::vector<std::uint8_t> m_fontData;  ///< TTFバイナリデータ
	stbtt_fontinfo m_fontInfo{};            ///< stb_truetype フォント情報
	bool m_initialized = false;             ///< 初期化済みフラグ
	GlyphCache m_cache;                     ///< グリフキャッシュ
	void* m_userData = nullptr;             ///< ユーザーデータ（型消去）

public:
	/// @brief デフォルトコンストラクタ（無効なフォント）
	TrueTypeFont() = default;

	/// @brief TTFバイナリデータからフォントを構築する
	/// @param fontData TTF/OTFファイルのバイナリデータ（ムーブ）
	/// @param cacheSize LRUキャッシュの最大エントリ数
	/// @throws std::runtime_error フォントの初期化に失敗した場合
	explicit TrueTypeFont(std::vector<std::uint8_t> fontData, std::size_t cacheSize = 4096)
		: m_fontData(std::move(fontData))
		, m_cache(cacheSize)
	{
		if (m_fontData.empty())
		{
			throw std::runtime_error("TrueTypeFont: empty font data");
		}

		const int offset = stbtt_GetFontOffsetForIndex(m_fontData.data(), 0);
		if (offset < 0)
		{
			throw std::runtime_error("TrueTypeFont: invalid font offset");
		}

		if (!stbtt_InitFont(&m_fontInfo, m_fontData.data(), offset))
		{
			throw std::runtime_error("TrueTypeFont: failed to initialize font");
		}

		m_initialized = true;
	}

	/// @brief ムーブコンストラクタ
	TrueTypeFont(TrueTypeFont&& other) noexcept
		: m_fontData(std::move(other.m_fontData))
		, m_fontInfo(other.m_fontInfo)
		, m_initialized(other.m_initialized)
		, m_cache(std::move(other.m_cache))
	{
		// stbtt_fontinfo内のポインタを更新
		if (m_initialized && !m_fontData.empty())
		{
			m_fontInfo.data = m_fontData.data();
		}
		other.m_initialized = false;
	}

	/// @brief ムーブ代入演算子
	TrueTypeFont& operator=(TrueTypeFont&& other) noexcept
	{
		if (this != &other)
		{
			m_fontData = std::move(other.m_fontData);
			m_fontInfo = other.m_fontInfo;
			m_initialized = other.m_initialized;
			m_cache = std::move(other.m_cache);

			if (m_initialized && !m_fontData.empty())
			{
				m_fontInfo.data = m_fontData.data();
			}
			other.m_initialized = false;
		}
		return *this;
	}

	// コピー禁止（大きなフォントデータを持つため）
	TrueTypeFont(const TrueTypeFont&) = delete;
	TrueTypeFont& operator=(const TrueTypeFont&) = delete;

	/// @brief フォントが有効か
	[[nodiscard]] bool valid() const noexcept { return m_initialized; }

	/// @brief ユーザーデータを設定する
	void setUserData(void* data) noexcept { m_userData = data; }

	/// @brief ユーザーデータを取得する
	template <typename T>
	[[nodiscard]] T* userData() const noexcept { return static_cast<T*>(m_userData); }

	// ── メトリクス ─────────────────────────────────────────

	/// @brief 指定ピクセルサイズでのフォントメトリクスを取得する
	/// @param pixelSize フォントサイズ（ピクセル）
	/// @return フォントメトリクス
	[[nodiscard]] FontMetrics metrics(float pixelSize) const noexcept
	{
		if (!m_initialized)
		{
			return {};
		}

		const float scale = stbtt_ScaleForPixelHeight(&m_fontInfo, pixelSize);

		int ascent = 0;
		int descent = 0;
		int lineGap = 0;
		stbtt_GetFontVMetrics(&m_fontInfo, &ascent, &descent, &lineGap);

		FontMetrics result;
		result.ascent = static_cast<float>(ascent) * scale;
		result.descent = static_cast<float>(descent) * scale;
		result.lineGap = static_cast<float>(lineGap) * scale;
		result.lineHeight = result.ascent - result.descent + result.lineGap;
		result.pixelSize = pixelSize;
		return result;
	}

	/// @brief 指定コードポイントの前進幅を取得する
	/// @param codepoint Unicodeコードポイント
	/// @param pixelSize フォントサイズ（ピクセル）
	/// @return 前進幅（ピクセル）
	[[nodiscard]] float advanceWidth(std::uint32_t codepoint, float pixelSize) const noexcept
	{
		if (!m_initialized)
		{
			return 0.0f;
		}

		const float scale = stbtt_ScaleForPixelHeight(&m_fontInfo, pixelSize);
		int advance = 0;
		int lsb = 0;
		stbtt_GetCodepointHMetrics(&m_fontInfo, static_cast<int>(codepoint), &advance, &lsb);
		return static_cast<float>(advance) * scale;
	}

	/// @brief 2つのコードポイント間のカーニング値を取得する
	/// @param cp1 先行コードポイント
	/// @param cp2 後続コードポイント
	/// @param pixelSize フォントサイズ（ピクセル）
	/// @return カーニング値（ピクセル）
	[[nodiscard]] float kerning(std::uint32_t cp1, std::uint32_t cp2, float pixelSize) const noexcept
	{
		if (!m_initialized)
		{
			return 0.0f;
		}

		const float scale = stbtt_ScaleForPixelHeight(&m_fontInfo, pixelSize);
		const int kern = stbtt_GetCodepointKernAdvance(
			&m_fontInfo, static_cast<int>(cp1), static_cast<int>(cp2));
		return static_cast<float>(kern) * scale;
	}

	/// @brief UTF-8文字列の描画幅を計測する
	/// @param text UTF-8文字列
	/// @param pixelSize フォントサイズ（ピクセル）
	/// @return 描画幅（ピクセル）
	[[nodiscard]] float measureText(std::string_view text, float pixelSize) const noexcept
	{
		if (!m_initialized)
		{
			return 0.0f;
		}

		const float scale = stbtt_ScaleForPixelHeight(&m_fontInfo, pixelSize);
		float width = 0.0f;
		std::uint32_t prevCp = 0;

		Utf8Iterator it(text);
		while (it.hasNext())
		{
			const std::uint32_t cp = it.next();
			if (prevCp != 0)
			{
				width += kerning(prevCp, cp, pixelSize);
			}

			int advance = 0;
			int lsb = 0;
			stbtt_GetCodepointHMetrics(&m_fontInfo, static_cast<int>(cp), &advance, &lsb);
			width += static_cast<float>(advance) * scale;
			prevCp = cp;
		}

		return width;
	}

	// ── グリフラスタライズ ──────────────────────────────────

	/// @brief 指定コードポイントのグリフをラスタライズして取得する
	/// @param codepoint Unicodeコードポイント
	/// @param pixelSize フォントサイズ（ピクセル）
	/// @return グリフ情報へのポインタ（キャッシュが管理）
	[[nodiscard]] const GlyphInfo* getGlyph(std::uint32_t codepoint, float pixelSize)
	{
		if (!m_initialized)
		{
			return nullptr;
		}

		// キャッシュを検索
		const auto* cached = m_cache.find(codepoint, pixelSize);
		if (cached != nullptr)
		{
			return cached;
		}

		// ラスタライズ
		GlyphInfo glyph = rasterizeGlyph(codepoint, pixelSize);
		m_cache.insert(glyph, pixelSize);
		return m_cache.find(codepoint, pixelSize);
	}

	// ── テクスチャアトラス ──────────────────────────────────

	/// @brief テクスチャアトラスの生成結果
	struct AtlasResult
	{
		mitiru::render::Texture texture;                          ///< RGBA8テクスチャ
		std::unordered_map<std::uint32_t, GlyphInfo> glyphs;     ///< コードポイント→グリフ情報
		int width = 0;                                            ///< アトラス幅
		int height = 0;                                           ///< アトラス高さ
	};

	/// @brief 指定コードポイント群からテクスチャアトラスを生成する
	/// @param pixelSize フォントサイズ
	/// @param codepoints パックするコードポイントのリスト
	/// @param padding グリフ間のパディング（ピクセル）
	/// @return アトラス生成結果
	[[nodiscard]] AtlasResult generateAtlas(
		float pixelSize,
		const std::vector<std::uint32_t>& codepoints,
		int padding = 1)
	{
		if (!m_initialized || codepoints.empty())
		{
			return {};
		}

		// 全グリフをラスタライズ
		std::vector<GlyphInfo> glyphs;
		glyphs.reserve(codepoints.size());
		for (const auto cp : codepoints)
		{
			glyphs.push_back(rasterizeGlyph(cp, pixelSize));
		}

		// 高さ降順でソート（パッキング効率向上のため）
		std::sort(glyphs.begin(), glyphs.end(),
			[](const GlyphInfo& a, const GlyphInfo& b)
			{
				return a.height > b.height;
			});

		// アトラスサイズを推定（シンプルなシェルフパッキング）
		const int estimatedArea = estimateAtlasArea(glyphs, padding);
		int atlasWidth = nextPowerOf2(static_cast<int>(std::sqrt(static_cast<float>(estimatedArea))));
		int atlasHeight = atlasWidth;

		// シェルフパッキング
		std::vector<std::uint8_t> atlasPixels;
		bool packed = false;

		for (int attempt = 0; attempt < 4 && !packed; ++attempt)
		{
			atlasPixels.assign(
				static_cast<std::size_t>(atlasWidth) * atlasHeight * 4, 0);

			int shelfX = padding;
			int shelfY = padding;
			int shelfHeight = 0;
			packed = true;

			for (auto& g : glyphs)
			{
				if (g.width == 0 || g.height == 0)
				{
					g.atlasX = 0;
					g.atlasY = 0;
					continue;
				}

				// 現在のシェルフに収まらなければ次の行へ
				if (shelfX + g.width + padding > atlasWidth)
				{
					shelfX = padding;
					shelfY += shelfHeight + padding;
					shelfHeight = 0;
				}

				// アトラス高さを超える場合はリサイズ
				if (shelfY + g.height + padding > atlasHeight)
				{
					atlasHeight *= 2;
					packed = false;
					break;
				}

				g.atlasX = shelfX;
				g.atlasY = shelfY;
				shelfHeight = std::max(shelfHeight, g.height);
				shelfX += g.width + padding;
			}
		}

		if (!packed)
		{
			return {};
		}

		// グリフビットマップをアトラスにコピー（グレースケール→RGBA）
		for (const auto& g : glyphs)
		{
			for (int y = 0; y < g.height; ++y)
			{
				for (int x = 0; x < g.width; ++x)
				{
					const auto srcIdx = static_cast<std::size_t>(y * g.width + x);
					const auto dstIdx = static_cast<std::size_t>(
						((g.atlasY + y) * atlasWidth + (g.atlasX + x)) * 4);

					if (srcIdx < g.bitmap.size() && dstIdx + 3 < atlasPixels.size())
					{
						const auto alpha = g.bitmap[srcIdx];
						atlasPixels[dstIdx + 0] = 255;   // R
						atlasPixels[dstIdx + 1] = 255;   // G
						atlasPixels[dstIdx + 2] = 255;   // B
						atlasPixels[dstIdx + 3] = alpha;  // A
					}
				}
			}
		}

		// 結果を構築
		AtlasResult result;
		result.width = atlasWidth;
		result.height = atlasHeight;
		result.texture = mitiru::render::Texture(atlasWidth, atlasHeight, atlasPixels);

		for (const auto& g : glyphs)
		{
			result.glyphs.emplace(g.codepoint, g);
		}

		return result;
	}

	/// @brief UTF-8文字列に含まれるコードポイントからアトラスを生成する
	/// @param pixelSize フォントサイズ
	/// @param text UTF-8文字列
	/// @param padding グリフ間のパディング
	/// @return アトラス生成結果
	[[nodiscard]] AtlasResult generateAtlas(
		float pixelSize,
		std::string_view text,
		int padding = 1)
	{
		return generateAtlas(pixelSize, utf8ToCodepoints(text), padding);
	}

	/// @brief グリフキャッシュをクリアする
	void clearCache()
	{
		m_cache.clear();
	}

private:
	/// @brief 2xオーバーサンプリング倍率
	static constexpr int OVERSAMPLE = 2;

	/// @brief 単一グリフを2xオーバーサンプリングでラスタライズする
	/// @param codepoint Unicodeコードポイント
	/// @param pixelSize フォントサイズ
	/// @return ラスタライズ済みグリフ情報
	/// @details 2xオーバーサンプリングで高解像度ビットマップを生成し、
	///          ボックスフィルターで縮小することで、滑らかなアンチエイリアシングを得る。
	[[nodiscard]] GlyphInfo rasterizeGlyph(std::uint32_t codepoint, float pixelSize) const
	{
		GlyphInfo glyph;
		glyph.codepoint = codepoint;

		const float scale = stbtt_ScaleForPixelHeight(&m_fontInfo, pixelSize);

		// 前進幅を取得
		int advance = 0;
		int lsb = 0;
		stbtt_GetCodepointHMetrics(
			&m_fontInfo, static_cast<int>(codepoint), &advance, &lsb);
		glyph.advanceX = static_cast<float>(advance) * scale;

		// オーバーサンプリングされたスケールでバウンディングボックスを取得
		const float osScale = scale * static_cast<float>(OVERSAMPLE);
		int x0 = 0;
		int y0 = 0;
		int x1 = 0;
		int y1 = 0;
		stbtt_GetCodepointBitmapBoxSubpixel(
			&m_fontInfo, static_cast<int>(codepoint),
			osScale, osScale, 0.0f, 0.0f,
			&x0, &y0, &x1, &y1);

		const int osW = x1 - x0;
		const int osH = y1 - y0;

		if (osW <= 0 || osH <= 0)
		{
			return glyph;
		}

		// オーバーサンプリングされたビットマップをレンダリング
		std::vector<std::uint8_t> osBitmap(
			static_cast<std::size_t>(osW) * osH, 0);
		stbtt_MakeCodepointBitmapSubpixel(
			&m_fontInfo, osBitmap.data(),
			osW, osH, osW,
			osScale, osScale, 0.0f, 0.0f,
			static_cast<int>(codepoint));

		// ボックスフィルターで1/OVERSAMPLEに縮小
		const int finalW = (osW + OVERSAMPLE - 1) / OVERSAMPLE;
		const int finalH = (osH + OVERSAMPLE - 1) / OVERSAMPLE;

		glyph.width = finalW;
		glyph.height = finalH;
		glyph.offsetX = x0 / OVERSAMPLE;
		glyph.offsetY = y0 / OVERSAMPLE;
		glyph.bitmap.resize(static_cast<std::size_t>(finalW) * finalH);

		for (int dy = 0; dy < finalH; ++dy)
		{
			for (int dx = 0; dx < finalW; ++dx)
			{
				int sum = 0;
				int count = 0;
				for (int sy = dy * OVERSAMPLE;
					 sy < std::min((dy + 1) * OVERSAMPLE, osH); ++sy)
				{
					for (int sx = dx * OVERSAMPLE;
						 sx < std::min((dx + 1) * OVERSAMPLE, osW); ++sx)
					{
						sum += osBitmap[static_cast<std::size_t>(sy * osW + sx)];
						++count;
					}
				}
				const auto avg = (count > 0)
					? static_cast<std::uint8_t>(sum / count)
					: static_cast<std::uint8_t>(0);
				glyph.bitmap[static_cast<std::size_t>(dy * finalW + dx)] = avg;
			}
		}

		return glyph;
	}

	/// @brief アトラスに必要な面積を推定する
	[[nodiscard]] static int estimateAtlasArea(
		const std::vector<GlyphInfo>& glyphs, int padding) noexcept
	{
		int area = 0;
		for (const auto& g : glyphs)
		{
			area += (g.width + padding) * (g.height + padding);
		}
		// マージン込みで1.3倍の余裕
		return static_cast<int>(static_cast<float>(area) * 1.3f);
	}

	/// @brief 2のべき乗に切り上げる
	[[nodiscard]] static int nextPowerOf2(int v) noexcept
	{
		int result = 64; // 最小64
		while (result < v)
		{
			result *= 2;
		}
		return result;
	}
};

} // namespace mitiru::vn
