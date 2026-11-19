#pragma once

/// @file ColorReplace.hpp
/// @brief 描画時の色の差し替え (ABI v64、ADR 0164)。host の drain が持つ表と、差し替えた画像の置き場。
/// @details パレットは持たない。色は RGBA のまま、比べるのは 8 bit に丸めた RGB の完全一致。アルファは比べず、差し替え後のアルファは
///          元のアルファに差し替え先のアルファを掛ける。連鎖はしない (a→b と b→c があっても a は b)。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <mitiru/module/DrawCommands.hpp>
#include <mitiru/render/Texture.hpp>
#include <mitiru/util/Hash.hpp>

namespace mitiru::module::detail
{

/// 色の差し替えの表。drain の局所の値で、ゲームの描画が終わると消える (host の重ね描きと次のフレームには効かない)
class ColorReplaceTable
{
public:
	/// @brief 1 組を足す。同じ from は後の組が勝つ。64 組を越える分は捨てて false
	bool add(const float (&from)[4], const float (&to)[4]) noexcept
	{
		const Pair p{quantize(from[0]), quantize(from[1]), quantize(from[2]), {to[0], to[1], to[2], to[3]}};
		for (std::size_t i = 0; i < m_count; ++i)
		{
			if (m_pairs[i].r == p.r && m_pairs[i].g == p.g && m_pairs[i].b == p.b) { m_pairs[i] = p; m_hash = computeHash(); return true; }
		}
		if (m_count >= kMaxColorReplacements) { return false; }
		m_pairs[m_count++] = p;
		m_hash = computeHash();
		return true;
	}

	void clear() noexcept { m_count = 0; m_hash = 0; }
	[[nodiscard]] bool empty() const noexcept { return m_count == 0; }
	[[nodiscard]] std::size_t size() const noexcept { return m_count; }
	/// 表の中身から決まる値。同じ組なら同じ (差し替えた画像を覚える鍵)
	[[nodiscard]] std::uint64_t hash() const noexcept { return m_hash; }

	/// @brief 図形と文字の色 (r, g, b, a) を差し替える。当たりがなければそのまま
	[[nodiscard]] sgc::Colorf apply(const sgc::Colorf& c) const noexcept
	{
		if (const Pair* p = find(quantize(c.r), quantize(c.g), quantize(c.b))) { return {p->to[0], p->to[1], p->to[2], c.a * p->to[3]}; }
		return c;
	}

	/// @brief 画素 (0xAABBGGRR) を差し替える。当たりがなければそのまま
	[[nodiscard]] std::uint32_t applyPixel(std::uint32_t px) const noexcept
	{
		const std::uint8_t r = static_cast<std::uint8_t>(px & 0xFFu);
		const std::uint8_t g = static_cast<std::uint8_t>((px >> 8) & 0xFFu);
		const std::uint8_t b = static_cast<std::uint8_t>((px >> 16) & 0xFFu);
		const Pair* p = find(r, g, b);
		if (p == nullptr) { return px; }
		const float a = static_cast<float>((px >> 24) & 0xFFu) / 255.0f * p->to[3];
		return (static_cast<std::uint32_t>(quantize(a)) << 24) | (static_cast<std::uint32_t>(quantize(p->to[2])) << 16)
		     | (static_cast<std::uint32_t>(quantize(p->to[1])) << 8) | static_cast<std::uint32_t>(quantize(p->to[0]));
	}

	[[nodiscard]] static std::uint8_t quantize(float v) noexcept
	{
		if (!(v > 0.0f)) { return 0; }   // NaN も 0
		if (v >= 1.0f) { return 255; }
		return static_cast<std::uint8_t>(std::lround(v * 255.0f));
	}

private:
	struct Pair
	{
		std::uint8_t r = 0, g = 0, b = 0;
		float        to[4] = {};
	};

	[[nodiscard]] const Pair* find(std::uint8_t r, std::uint8_t g, std::uint8_t b) const noexcept
	{
		for (std::size_t i = 0; i < m_count; ++i)
		{
			if (m_pairs[i].r == r && m_pairs[i].g == g && m_pairs[i].b == b) { return &m_pairs[i]; }
		}
		return nullptr;
	}

	[[nodiscard]] std::uint64_t computeHash() const noexcept
	{
		std::uint8_t bytes[kMaxColorReplacements * 7];
		std::size_t n = 0;
		for (std::size_t i = 0; i < m_count; ++i)
		{
			bytes[n++] = m_pairs[i].r; bytes[n++] = m_pairs[i].g; bytes[n++] = m_pairs[i].b;
			for (int k = 0; k < 4; ++k) { bytes[n++] = quantize(m_pairs[i].to[k]); }
		}
		return util::Hash::xxhash(bytes, n) ^ (static_cast<std::uint64_t>(m_count) << 56);
	}

	std::array<Pair, kMaxColorReplacements> m_pairs{};
	std::size_t   m_count = 0;
	std::uint64_t m_hash = 0;
};

/// 差し替えた画像の置き場。Texture のアドレスは GPU 側が鍵にするので、一度作った置き場は壊さず、足りなくなったら使われていない置き場の
/// 中身を作り直す (新しい画素の配列になるので、GPU 側は PNG の読み直しと同じく、写しを作り直す)。
/// 元の画像の中身が変わったとき (--watch-assets での読み直し) は、元の画素のハッシュが変わるので作り直す。ハッシュは 1 フレームに 1 度だけ求める
class ReplacedTextureCache
{
public:
	static constexpr std::size_t kMaxSlots = 128;

	/// @brief フレーム (drain 1 回) の頭に呼ぶ
	void beginFrame() noexcept { ++m_epoch; }

	/// @brief src を表で差し替えた画像を返す。表が空なら src のまま
	[[nodiscard]] const render::Texture* get(const render::Texture& src, const ColorReplaceTable& table)
	{
		if (table.empty() || !src.valid()) { return &src; }
		Slot* slot = findSlot(&src, table.hash());
		const std::uint64_t srcHash = (slot != nullptr && slot->hashedEpoch == m_epoch) ? slot->srcHash : hashOf(src);
		if (slot == nullptr) { slot = acquireSlot(); slot->source = &src; slot->tableHash = table.hash(); slot->srcHash = 0; slot->built = false; }
		if (!slot->built || slot->srcHash != srcHash || slot->tex.width() != src.width() || slot->tex.height() != src.height())
		{
			std::vector<std::uint8_t> px = src.pixels();
			for (std::size_t i = 0; i + 3 < px.size(); i += 4)
			{
				std::uint32_t v;
				std::memcpy(&v, &px[i], 4);
				v = table.applyPixel(v);
				std::memcpy(&px[i], &v, 4);
			}
			slot->tex = render::Texture(src.width(), src.height(), std::move(px));
			slot->srcHash = srcHash;
			slot->built = true;
		}
		slot->hashedEpoch = m_epoch;
		slot->usedEpoch = m_epoch;
		return &slot->tex;
	}

	[[nodiscard]] std::size_t slotCount() const noexcept { return m_slots.size(); }

private:
	struct Slot
	{
		const render::Texture* source = nullptr;
		std::uint64_t tableHash = 0;
		std::uint64_t srcHash = 0;
		std::uint64_t hashedEpoch = 0;   ///< srcHash を求めたフレーム
		std::uint64_t usedEpoch = 0;
		bool          built = false;
		render::Texture tex;
	};

	[[nodiscard]] static std::uint64_t hashOf(const render::Texture& t) noexcept
	{
		return t.pixels().empty() ? 0 : util::Hash::xxhash(t.pixels().data(), t.pixels().size());
	}

	[[nodiscard]] Slot* findSlot(const render::Texture* src, std::uint64_t tableHash) noexcept
	{
		for (auto& s : m_slots) { if (s->built && s->source == src && s->tableHash == tableHash) { return s.get(); } }
		return nullptr;
	}

	[[nodiscard]] Slot* acquireSlot()
	{
		if (m_slots.size() < kMaxSlots) { m_slots.push_back(std::make_unique<Slot>()); return m_slots.back().get(); }
		Slot* oldest = m_slots.front().get();
		for (auto& s : m_slots) { if (s->usedEpoch < oldest->usedEpoch) { oldest = s.get(); } }
		return oldest;
	}

	std::vector<std::unique_ptr<Slot>> m_slots;
	std::uint64_t m_epoch = 0;
};

}  // namespace mitiru::module::detail
