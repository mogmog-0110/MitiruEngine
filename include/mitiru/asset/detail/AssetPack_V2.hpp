#pragma once

/// @file AssetPack_V2.hpp
/// @brief AssetPack の v2 (chunk 分割) 形式: 書き出し / 読み込み / view / chunk キャッシュ。
///
/// AssetPack.hpp のクラス定義が終わった直後の namespace mitiru::vfs から include される
/// 前提 (AssetPack.hpp の続きとして読む分割ファイル)。単体で include されても
/// #include <mitiru/asset/AssetPack.hpp> が pragma once 越しに成立するので問題ない。
///
/// v2 レイアウトは先頭から順に: magic(6) と version=2(u16) と flags(u16) と count(u32) と
/// chunkSize(u32) と dependsCount(u16)。続けて依存 pack 名を dependsCount 個
/// (nameLen(u16) + name)、その後にエントリを count 個
/// (中身は pathLen(u16) + path + firstChunk(u32) + chunkCount(u32) + size(u64) の並び)、
/// 最後に chunk 表 (totalChunks(u32) と storedSize(u32) を totalChunks 個) と
/// blob 本体 (chunk を index 順に連結、各 chunk は圧縮してから難読化した状態)。
///
/// エントリは chunkSize (64KiB) 境界に揃えて分割される。view() はどのケースでも
/// パスごとに一度だけ復号して m_viewFallback (pack の生存期間だけ残る) に載せてから
/// span を張る。chunk キャッシュ (LRU、容量超過で evict) へ直接 span を張らないのは、
/// 後続の無関係な view()/read() が同じ chunk を追い出すと span が dangling になるため。

#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/util/Compression.hpp>

#include <algorithm>

namespace mitiru::vfs
{

inline const PackEntry* AssetPack::findEntry(const std::string& np) const
{
	for (const auto& e : m_entries)
	{
		if (e.path == np) { return &e; }
	}
	return nullptr;
}

inline const std::vector<uint8_t>* AssetPack::chunkData(uint32_t idx) const
{
	if (idx >= m_chunkOffset.size()) { return nullptr; }
	if (const auto* cached = m_chunkCache->find(idx)) { return cached; }

	std::ifstream f(m_file, std::ios::binary);
	if (!f) { return nullptr; }
	f.seekg(static_cast<std::streamoff>(m_baseOffset + m_chunkOffset[idx]));
	std::vector<uint8_t> raw(m_chunkStoredSize[idx]);
	if (!raw.empty()) { f.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size())); }
	if (static_cast<uint64_t>(f.gcount()) != raw.size()) { return nullptr; }
	// 書き込み順 (圧縮→難読化) の逆で戻す。
	if (m_scrambled) { xorScramble(raw, m_chunkOffset[idx]); }
	if (m_zstdChunks && !raw.empty())
	{
		raw = mitiru::util::Compression::decompress(raw.data(), raw.size(), m_chunkUncompressedSize[idx]);
	}
	return &m_chunkCache->insert(idx, std::move(raw));
}

inline std::optional<std::vector<uint8_t>> AssetPack::readChunked(const PackEntry& e) const
{
	std::vector<uint8_t> out;
	out.reserve(e.size);
	for (uint32_t c = 0; c < e.chunkCount; ++c)
	{
		const auto* chunk = chunkData(e.firstChunk + c);
		if (chunk == nullptr) { return std::nullopt; }
		const uint64_t already = static_cast<uint64_t>(c) * m_chunkSize;
		const uint64_t take    = std::min<uint64_t>(m_chunkSize, e.size - already);
		if (chunk->size() < take) { return std::nullopt; }
		out.insert(out.end(), chunk->begin(), chunk->begin() + static_cast<std::ptrdiff_t>(take));
	}
	return out;
}

inline bool AssetPack::ensureMmap() const
{
	if (m_fileMap->valid()) { return true; }
	return m_fileMap->open(m_file);
}

inline std::optional<std::span<const uint8_t>> AssetPack::viewViaFallback(const std::string& np, const PackEntry& e) const
{
	auto it = m_viewFallback->find(np);
	if (it == m_viewFallback->end())
	{
		auto data = (m_version >= kVersion2) ? readChunked(e) : read(np);
		if (!data) { return std::nullopt; }
		it = m_viewFallback->emplace(np, std::move(*data)).first;
	}
	return std::span<const uint8_t>(it->second.data(), it->second.size());
}

inline std::optional<std::span<const uint8_t>> AssetPack::view(std::string_view path) const
{
	const std::string np = normalizePath(path);
	const PackEntry*  e  = findEntry(np);
	if (e == nullptr) { return std::nullopt; }
	if (e->size == 0) { return std::span<const uint8_t>{}; }

	if (m_version == kVersion)
	{
		// 難読化されていなければ mmap した pack へ直接 span を張れる (コピー無し)。
		if (!m_scrambled && ensureMmap() && m_baseOffset + e->offset + e->size <= m_fileMap->size())
		{
			return std::span<const uint8_t>(m_fileMap->data() + m_baseOffset + e->offset, e->size);
		}
		return viewViaFallback(np, *e);
	}

	if (e->chunkCount == 1)
	{
		// m_chunkCache は容量 64 の LRU で、無関係な別 chunk の view() 連打により
		// ここで返した span の裏にある vector が evictOldest() で erase されうる
		// (unordered_map::erase は要素の破棄を伴い、既存の参照/ポインタを無効化する)。
		// そのため単一 chunk でも zero-copy にはせず、viewViaFallback (パスごとに
		// 永続する m_viewFallback) を経由して span の有効期間を pack の生存期間に揃える。
		auto it = m_viewFallback->find(np);
		if (it == m_viewFallback->end())
		{
			const auto* chunk = chunkData(e->firstChunk);
			if (chunk == nullptr || chunk->size() < e->size) { return viewViaFallback(np, *e); }
			it = m_viewFallback->emplace(np, std::vector<uint8_t>(chunk->begin(), chunk->begin() + e->size)).first;
		}
		return std::span<const uint8_t>(it->second.data(), it->second.size());
	}
	return viewViaFallback(np, *e);
}

inline bool AssetPack::writeV2(const std::filesystem::path&                                    outFile,
                               const std::vector<std::pair<std::string, std::vector<uint8_t>>>& entries,
                               const std::vector<std::string>&                                  dependsOn,
                               bool                                                              scramble,
                               bool                                                              zstdChunks)
{
	struct ChunkOut
	{
		std::vector<uint8_t> stored;
	};
	std::vector<ChunkOut> chunks;
	std::vector<uint32_t> firstChunkOf(entries.size());
	std::vector<uint32_t> chunkCountOf(entries.size());

	// 各エントリを chunkSize 単位に分割し、圧縮 (任意) だけ済ませた状態で並べる。
	// 難読化は全 chunk の書き込み offset が決まってから (offset を鍵に使うため) 一括で行う。
	for (std::size_t i = 0; i < entries.size(); ++i)
	{
		const auto&    data    = entries[i].second;
		const uint32_t cnt     = static_cast<uint32_t>((data.size() + kChunkSize - 1) / kChunkSize);
		const uint32_t safeCnt = cnt == 0 ? 1 : cnt;  // 空データも 1 個 (0 byte) の chunk を持つ
		firstChunkOf[i] = static_cast<uint32_t>(chunks.size());
		chunkCountOf[i] = safeCnt;
		for (uint32_t c = 0; c < safeCnt; ++c)
		{
			const uint64_t begin = static_cast<uint64_t>(c) * kChunkSize;
			const uint64_t take  = begin >= data.size() ? 0 : std::min<uint64_t>(kChunkSize, data.size() - begin);
			std::vector<uint8_t> raw(data.begin() + static_cast<std::ptrdiff_t>(begin),
			                         data.begin() + static_cast<std::ptrdiff_t>(begin + take));
			if (zstdChunks) { raw = mitiru::util::Compression::compress(raw); }
			chunks.push_back({std::move(raw)});
		}
	}

	// ヘッダ (magic〜依存名〜エントリ表〜chunk 表) の合計サイズ = blob 開始位置。
	uint64_t headerSize = 6 + 2 + 2 + 4 + 4 + 2;
	for (const auto& d : dependsOn) { headerSize += 2 + d.size(); }
	for (const auto& [p, unused] : entries) { (void)unused; headerSize += 2 + normalizePath(p).size() + 4 + 4 + 8; }
	headerSize += 4 + 4 * static_cast<uint64_t>(chunks.size());

	std::vector<uint64_t> chunkOffset(chunks.size());
	uint64_t              off = headerSize;
	for (std::size_t i = 0; i < chunks.size(); ++i)
	{
		chunkOffset[i] = off;
		off += chunks[i].stored.size();
	}
	if (scramble)
	{
		for (std::size_t i = 0; i < chunks.size(); ++i) { xorScramble(chunks[i].stored, chunkOffset[i]); }
	}

	std::ofstream f(outFile, std::ios::binary | std::ios::trunc);
	if (!f) { return false; }
	f.write(kMagic, 6);
	detail::wu16(f, kVersion2);
	uint16_t flags = kFlagChunked;
	if (scramble) { flags |= kFlagScrambled; }
	if (zstdChunks) { flags |= kFlagZstdChunks; }
	detail::wu16(f, flags);
	detail::wu32(f, static_cast<uint32_t>(entries.size()));
	detail::wu32(f, kChunkSize);
	detail::wu16(f, static_cast<uint16_t>(dependsOn.size()));
	for (const auto& d : dependsOn)
	{
		detail::wu16(f, static_cast<uint16_t>(d.size()));
		f.write(d.data(), static_cast<std::streamsize>(d.size()));
	}
	for (std::size_t i = 0; i < entries.size(); ++i)
	{
		const std::string np = normalizePath(entries[i].first);
		detail::wu16(f, static_cast<uint16_t>(np.size()));
		f.write(np.data(), static_cast<std::streamsize>(np.size()));
		detail::wu32(f, firstChunkOf[i]);
		detail::wu32(f, chunkCountOf[i]);
		detail::wu64(f, static_cast<uint64_t>(entries[i].second.size()));
	}
	detail::wu32(f, static_cast<uint32_t>(chunks.size()));
	for (const auto& c : chunks) { detail::wu32(f, static_cast<uint32_t>(c.stored.size())); }
	for (const auto& c : chunks)
	{
		if (!c.stored.empty()) { f.write(reinterpret_cast<const char*>(c.stored.data()), static_cast<std::streamsize>(c.stored.size())); }
	}
	return static_cast<bool>(f);
}

inline std::optional<AssetPack> AssetPack::openV2(std::ifstream& f, const std::filesystem::path& file, uint64_t base)
{
	const uint16_t flags        = detail::ru16(f);
	const uint32_t count        = detail::ru32(f);
	const uint32_t chunkSize    = detail::ru32(f);
	const uint16_t dependsCount = detail::ru16(f);

	AssetPack pack;
	pack.m_file         = file;
	pack.m_baseOffset   = base;
	pack.m_version      = kVersion2;
	pack.m_scrambled    = (flags & kFlagScrambled) != 0;
	pack.m_zstdChunks   = (flags & kFlagZstdChunks) != 0;
	pack.m_chunkSize    = chunkSize;
	pack.m_chunkCache   = std::make_shared<detail::ChunkCache>();
	pack.m_fileMap      = std::make_shared<detail::FileMap>();
	pack.m_viewFallback = std::make_shared<std::unordered_map<std::string, std::vector<uint8_t>>>();

	for (uint16_t i = 0; i < dependsCount; ++i)
	{
		const uint16_t len = detail::ru16(f);
		std::string    name(len, '\0');
		f.read(name.data(), len);
		if (!f) { return std::nullopt; }
		pack.m_dependsOn.push_back(std::move(name));
	}

	std::vector<uint32_t> firstChunkOf(count);
	std::vector<uint32_t> chunkCountOf(count);
	std::vector<uint64_t> entrySize(count);
	for (uint32_t i = 0; i < count; ++i)
	{
		const uint16_t len = detail::ru16(f);
		std::string    p(len, '\0');
		f.read(p.data(), len);
		firstChunkOf[i] = detail::ru32(f);
		chunkCountOf[i] = detail::ru32(f);
		entrySize[i]    = detail::ru64(f);
		if (!f) { return std::nullopt; }
		pack.m_entries.push_back({std::move(p), 0, entrySize[i], firstChunkOf[i], chunkCountOf[i]});
	}

	const uint32_t totalChunks = detail::ru32(f);
	pack.m_chunkStoredSize.resize(totalChunks);
	for (uint32_t i = 0; i < totalChunks; ++i) { pack.m_chunkStoredSize[i] = detail::ru32(f); }
	if (!f) { return std::nullopt; }

	// 各 chunk の展開後サイズは既定 chunkSize、ただし各エントリの最終 chunk だけ端数になる。
	pack.m_chunkUncompressedSize.assign(totalChunks, chunkSize);
	for (uint32_t i = 0; i < count; ++i)
	{
		const uint32_t cnt = chunkCountOf[i];
		if (cnt == 0) { continue; }
		const uint64_t rem = entrySize[i] - static_cast<uint64_t>(cnt - 1) * chunkSize;
		pack.m_chunkUncompressedSize[firstChunkOf[i] + cnt - 1] = static_cast<uint32_t>(rem);
	}

	const uint64_t blobStart = static_cast<uint64_t>(f.tellg()) - base;
	pack.m_chunkOffset.resize(totalChunks);
	uint64_t off = blobStart;
	for (uint32_t i = 0; i < totalChunks; ++i)
	{
		pack.m_chunkOffset[i] = off;
		off += pack.m_chunkStoredSize[i];
	}

	return pack;
}

}  // namespace mitiru::vfs
