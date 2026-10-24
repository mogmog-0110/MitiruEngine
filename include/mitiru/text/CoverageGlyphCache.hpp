#pragma once

/// @file CoverageGlyphCache.hpp
/// @brief 表示サイズで塗った字形の濃度を白 + アルファで持つアトラス。
/// @details ヒンティング込みで画素に合わせる小さい文字と、CPU で画面を塗る描画先で使う。サイズごとに字形を分けて棚詰めし、満杯になったら全部捨てる。

#include <algorithm>
#include <cstdint>
#include <vector>

#include <mitiru/render/Texture.hpp>
#include <mitiru/text/DirtyRows.hpp>
#include <mitiru/text/FontFace.hpp>

namespace mitiru::text
{

struct CoverageEntry
{
	int x = 0;
	int y = 0;
	int width = 0;  ///< 0 なら描くものが無い (空白)
	int height = 0;
	int left = 0;   ///< ペン位置から画素枠左上まで
	int top = 0;
};

class CoverageGlyphCache
{
public:
	static constexpr int kWidth = 1024;
	static constexpr int kHeight = 1024;

	CoverageGlyphCache() = default;

	/// 横方向のサブピクセル位置を 1/3 画素刻みで塗り分ける
	static constexpr int kSubpixelSteps = 3;

	/// @brief 字形を引く。無ければ塗って詰める。
	/// @param subpixel 0 以上 kSubpixelSteps 未満。subpixel / kSubpixelSteps 画素だけ右へずらす
	[[nodiscard]] CoverageEntry get(FontFace& font, std::uint32_t glyphId, int pixelSize, int subpixel = 0)
	{
		const std::uint64_t key = (static_cast<std::uint64_t>(font.id() & 0xFFFF) << 48)
		                        | (static_cast<std::uint64_t>(pixelSize & 0xFFF) << 36)
		                        | (static_cast<std::uint64_t>(subpixel & 0xF) << 32) | glyphId;
		if (const CoverageEntry* hit = m_entries.find(key)) { return *hit; }

		CoverageEntry entry;
		const float shift = static_cast<float>(subpixel) / static_cast<float>(kSubpixelSteps);
		if (font.coverage(glyphId, static_cast<float>(pixelSize), shift, m_scratch) &&
		    reserve(m_scratch.width, m_scratch.height))
		{
			entry = place(m_scratch);
		}
		m_entries.insert(key, entry);
		return entry;
	}

	/// @brief テクスチャ描画で GPU にまだ送っていない行。
	[[nodiscard]] DirtyRows& dirty() noexcept { return m_dirty; }

	/// 画素の置き場は最初に字形を塗る (または絵を GPU へ送る) ときに作る。文字を描かない実行は 4 MiB を使わない
	[[nodiscard]] const render::Texture& texture() const
	{
		ensureTexture();
		return m_texture;
	}
	[[nodiscard]] std::uint64_t resets() const noexcept { return m_resets; }
	[[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }

private:
	static constexpr int kPad = 1;

	/// @brief 棚に (w,h) の場所を取る。入らなければ全部捨てて先頭から詰める。
	[[nodiscard]] bool reserve(int w, int h)
	{
		if (w + 2 * kPad > kWidth || h + 2 * kPad > kHeight) { return false; }
		if (m_shelfX + w + kPad > kWidth)
		{
			m_shelfX = kPad;
			m_shelfY += m_shelfH + kPad;
			m_shelfH = 0;
		}
		if (m_shelfY + h + kPad > kHeight)
		{
			m_entries.clear();
			m_shelfX = kPad;
			m_shelfY = kPad;
			m_shelfH = 0;
			++m_resets;
		}
		return true;
	}

	CoverageEntry place(const CoverageGlyph& g)
	{
		const CoverageEntry e{m_shelfX, m_shelfY, g.width, g.height, g.left, g.top};
		m_rgba.resize(static_cast<std::size_t>(g.width) * g.height * 4);
		for (std::size_t i = 0; i < g.alpha.size(); ++i)
		{
			m_rgba[i * 4 + 0] = 255;
			m_rgba[i * 4 + 1] = 255;
			m_rgba[i * 4 + 2] = 255;
			m_rgba[i * 4 + 3] = g.alpha[i];
		}
		ensureTexture();
		m_texture.writeRegion(e.x, e.y, g.width, g.height, m_rgba.data());
		m_dirty.mark(e.y, g.height);
		m_shelfX += g.width + kPad;
		m_shelfH = std::max(m_shelfH, g.height);
		return e;
	}

	void ensureTexture() const
	{
		if (m_textureReady) { return; }
		m_texture = render::Texture(kWidth, kHeight, std::vector<std::uint8_t>(static_cast<std::size_t>(kWidth) * kHeight * 4, 0));
		m_textureReady = true;
	}

	/// 文字は 1 字ごとにここを引く。std::unordered_map は 1 回 20 ナノ秒前後かかるので、開番地法の表に替えた。
	/// 使用中は slot ごとの印で持つ (鍵の全ビットは書体の番号と大きさに使われていて、どの値も鍵になり得るので、鍵の値を空きの印にしない)。
	/// 半分を超えたら倍に広げる
	class EntryTable
	{
	public:
		EntryTable() { m_slots.resize(1024); }

		[[nodiscard]] const CoverageEntry* find(std::uint64_t key) const noexcept
		{
			for (std::size_t i = index(key);; i = (i + 1) & (m_slots.size() - 1))
			{
				const Slot& s = m_slots[i];
				if (!s.used) { return nullptr; }
				if (s.key == key) { return &s.entry; }
			}
		}

		void insert(std::uint64_t key, const CoverageEntry& entry)
		{
			if ((m_count + 1) * 2 > m_slots.size()) { grow(); }
			put(key, entry);
		}

		void clear() noexcept
		{
			std::fill(m_slots.begin(), m_slots.end(), Slot{});
			m_count = 0;
		}

		[[nodiscard]] std::size_t size() const noexcept { return m_count; }

	private:
		struct Slot
		{
			std::uint64_t key = 0;
			CoverageEntry entry;
			bool used = false;
		};

		[[nodiscard]] std::size_t index(std::uint64_t key) const noexcept
		{
			return static_cast<std::size_t>((key * 0x9E3779B97F4A7C15ull) >> 20) & (m_slots.size() - 1);
		}

		void put(std::uint64_t key, const CoverageEntry& entry)
		{
			std::size_t i = index(key);
			while (m_slots[i].used && m_slots[i].key != key) { i = (i + 1) & (m_slots.size() - 1); }
			if (!m_slots[i].used) { ++m_count; }
			m_slots[i] = Slot{key, entry, true};
		}

		void grow()
		{
			std::vector<Slot> old;
			old.swap(m_slots);
			m_slots.assign(old.size() * 2, Slot{});
			m_count = 0;
			for (const Slot& s : old) { if (s.used) { put(s.key, s.entry); } }
		}

		std::vector<Slot> m_slots;
		std::size_t m_count = 0;
	};

	mutable render::Texture m_texture;
	mutable bool m_textureReady = false;
	EntryTable m_entries;
	CoverageGlyph m_scratch;
	std::vector<std::uint8_t> m_rgba;
	int m_shelfX = kPad;
	int m_shelfY = kPad;
	int m_shelfH = 0;
	std::uint64_t m_resets = 0;
	DirtyRows m_dirty;
};

} // namespace mitiru::text
