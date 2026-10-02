#pragma once

/// @file DistanceFieldCache.hpp
/// @brief 字形の MTSDF を必要な分だけ 1 枚のアトラスへ焼いて保持する。
/// @details 距離場は表示サイズに依らず、1 字形を 1 升に収めて全サイズで使い回す。
/// 升が尽きたら GlyphSlotAtlas 方式で最も長く使われていない字形から入れ替える。
/// 書き換えた行だけを GPU へ送る。

#include <algorithm>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include <mitiru/render/Texture.hpp>
#include <mitiru/text/DirtyRows.hpp>
#include <mitiru/text/FontFace.hpp>
#include <mitiru/text/GlyphSlotAtlas.hpp>

namespace mitiru::text
{

/// @brief アトラス上の 1 字形。四辺はペン位置からの距離で、pixelSize が 1 の単位。y 軸は下向き。
struct DistanceFieldEntry
{
	int slot = -1; ///< -1 は輪郭の無い字形 (空白)。描くものが無い
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
	float left = 0.0f;
	float top = 0.0f;
	float right = 0.0f;
	float bottom = 0.0f;
};

class DistanceFieldCache
{
public:
	/// アトラスの 1 辺の画素数
	static constexpr int kAtlasSize = 2048;
	/// 1 升の画素数。1 字形の上限。
	static constexpr int kCell = 64;
	/// 生成時のピクセル高さ。和文の em が約 40 画素になり、画数の多い漢字も線が分かれる。
	static constexpr float kGlyphPixelSize = 56.0f;
	/// 距離場の幅を表すアトラス上の画素数。シェーダーの距離から濃さへの換算にも使う。
	static constexpr float kPixelRange = 6.0f;

	DistanceFieldCache()
		: DistanceFieldCache(kAtlasSize)
	{
	}

	/// @brief 升の数を変えて作る (入れ替えの試験で小さなアトラスを使う)。
	explicit DistanceFieldCache(int atlasSize)
		: DistanceFieldCache(atlasSize, std::max(atlasSize, kCell))
	{
	}

	/// @brief 字形を取得する。無ければ生成して焼く。生成できない字形は空白 (width 0) として覚える
	[[nodiscard]] const DistanceFieldEntry* get(FontFace& font, std::uint32_t glyphId)
	{
		const std::uint64_t key = keyOf(font, glyphId);
		const auto it = m_entries.find(key);
		if (it != m_entries.end())
		{
			if (it->second.slot >= 0) { static_cast<void>(m_slots.find(key)); }
			return &it->second;
		}
		DistanceFieldGlyph generated;
		static_cast<void>(font.distanceField(glyphId, kGlyphPixelSize, kPixelRange, kCell, generated));
		return store(key, generated);
	}

	/// @brief まだ焼いていない字形をまとめて焼く。距離計算を並列に行うため、新しい字形が多い行では 1 字ずつ get するより速い。
	void prefetch(FontFace& font, std::span<const ShapedGlyph> glyphs)
	{
		// 既存の字形を先に使用済みにして、この行の字形どうしで升を取り上げないようにする。
		m_missing.clear();
		for (const ShapedGlyph& g : glyphs)
		{
			const std::uint64_t key = keyOf(font, g.glyphId);
			if (m_entries.find(key) != m_entries.end())
			{
				static_cast<void>(m_slots.find(key));
			}
			else if (std::find(m_missing.begin(), m_missing.end(), g.glyphId) == m_missing.end())
			{
				m_missing.push_back(g.glyphId);
			}
		}
		if (m_missing.empty()) { return; }
		font.distanceFields(m_missing, kGlyphPixelSize, kPixelRange, kCell, m_generated);
		for (std::size_t i = 0; i < m_missing.size(); ++i)
		{
			static_cast<void>(store(keyOf(font, m_missing[i]), m_generated[i]));
		}
	}

	/// @brief 次の描画単位へ進む。直前までに使った字形は入れ替えを後回しにする。
	void tick() noexcept { m_slots.tick(); }

	/// @brief GPU へまだ送っていない行。
	[[nodiscard]] DirtyRows& dirty() noexcept { return m_dirty; }

	[[nodiscard]] const render::Texture& texture() const noexcept { return m_texture; }
	[[nodiscard]] const GlyphSlotAtlas& slots() const noexcept { return m_slots; }

private:
	// 大きさが負や升より小さいときに巨大な確保や 0 升のアトラスを作らないよう、升 1 つ分に切り上げてから作る
	DistanceFieldCache(int, int size)
		: m_slots(size, size, kCell)
		, m_texture(size, size, std::vector<std::uint8_t>(static_cast<std::size_t>(size) * size * 4, 0))
	{
	}

	[[nodiscard]] static std::uint64_t keyOf(const FontFace& font, std::uint32_t glyphId) noexcept
	{
		return (static_cast<std::uint64_t>(font.id()) << 32) | glyphId;
	}

	/// @brief 距離場を升へ焼いて表に追加する。width が 0 の空白は升を使わない。
	const DistanceFieldEntry* store(std::uint64_t key, const DistanceFieldGlyph& field)
	{
		DistanceFieldEntry entry;
		if (field.width <= 0)
		{
			return &m_entries.insert_or_assign(key, entry).first->second;
		}
		const int slot = m_slots.allocate(key);
		if (slot < 0) { return nullptr; }
		evictOwnerOf(slot);

		entry.slot = slot;
		entry.x = m_slots.slotX(slot);
		entry.y = m_slots.slotY(slot);
		entry.width = field.width;
		entry.height = field.height;
		entry.left = field.left;
		entry.top = field.top;
		entry.right = field.right;
		entry.bottom = field.bottom;

		// 升の残りを輪郭から遠い外側を表す 0 で埋め、前の字形の画素を残さない。
		m_cellPixels.assign(static_cast<std::size_t>(kCell) * kCell * 4, 0);
		for (int row = 0; row < entry.height; ++row)
		{
			std::copy_n(&field.rgba[static_cast<std::size_t>(row) * entry.width * 4],
			            static_cast<std::size_t>(entry.width) * 4,
			            &m_cellPixels[static_cast<std::size_t>(row) * kCell * 4]);
		}
		m_texture.writeRegion(entry.x, entry.y, kCell, kCell, m_cellPixels.data());
		m_dirty.mark(entry.y, kCell);

		if (m_slotOwner.size() <= static_cast<std::size_t>(slot)) { m_slotOwner.resize(static_cast<std::size_t>(slot) + 1, GlyphSlotAtlas::kNoKey); }
		m_slotOwner[static_cast<std::size_t>(slot)] = key;
		return &m_entries.insert_or_assign(key, entry).first->second;
	}

	void evictOwnerOf(int slot)
	{
		if (static_cast<std::size_t>(slot) >= m_slotOwner.size()) { return; }
		const std::uint64_t previous = m_slotOwner[static_cast<std::size_t>(slot)];
		if (previous != GlyphSlotAtlas::kNoKey) { m_entries.erase(previous); }
	}

	GlyphSlotAtlas m_slots;
	render::Texture m_texture;
	std::unordered_map<std::uint64_t, DistanceFieldEntry> m_entries;
	std::vector<std::uint64_t> m_slotOwner;
	std::vector<std::uint32_t> m_missing;
	std::vector<DistanceFieldGlyph> m_generated;
	std::vector<std::uint8_t> m_cellPixels;
	DirtyRows m_dirty;
};

} // namespace mitiru::text
