#pragma once

/// @file AssetPack.hpp
/// @brief アセットを単一ファイル (.mtpak) に詰める runtime VFS の中核。
///
/// 配布時に assets/ をまとめて秘匿するためのパック形式の read/write を提供する。
/// 純粋な C++ (GPU/CEF 非依存) で、テストとツールの双方から使える。
///
/// 形式:
///   magic "MTPAK\0" (6) | version u16 | flags u16 | count u32
///   [count] pathLen u16, path(UTF-8 '/'区切り), offset u64, size u64
///   blob region (flags の scramble bit が立っていれば XOR 難読化済み)
///
/// XOR は「暗号」ではなく、strings / hex での平文閲覧を防ぐ難読化である。

#include <algorithm>
#include <cstdint>
#include <cstdlib>  // std::getenv (MITIRU_ASSET_PACK 経由の境界越え mount)
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mitiru::vfs
{

inline constexpr char     kMagic[6]      = {'M', 'T', 'P', 'A', 'K', '\0'};
inline constexpr uint16_t kVersion       = 1;
inline constexpr uint16_t kFlagScrambled = 0x1;
// exe へ連結したときのフッタの印。パックは exe 本体の後ろに置くしかない (前に置くと
// 実行形式が壊れる) ので、位置はファイル末尾のフッタから逆引きする。
inline constexpr char     kAppendMagic[8] = {'M', 'T', 'P', 'A', 'K', 'E', 'X', 'E'};
inline constexpr uint8_t  kXorKey        = 0x5A;  // 難読化用の固定値 (暗号ではない)

/// パスを '/' 区切りに正規化し、先頭 "./" を落とす。
[[nodiscard]] inline std::string normalizePath(std::string_view p)
{
	std::string s{p};
	std::replace(s.begin(), s.end(), '\\', '/');
	if (s.rfind("./", 0) == 0) { s.erase(0, 2); }
	return s;
}

/// blob を絶対 offset 依存の XOR で可逆変換する (write/read で対称)。
inline void xorScramble(std::vector<uint8_t>& data, uint64_t offset)
{
	for (std::size_t i = 0; i < data.size(); ++i)
	{
		data[i] ^= static_cast<uint8_t>(kXorKey + ((offset + i) & 0xFF));
	}
}

struct PackEntry
{
	std::string path;
	uint64_t    offset = 0;
	uint64_t    size   = 0;
};

/// @brief .mtpak の読み取り (open/read) と書き出し (write) を提供する。
class AssetPack
{
public:
	/// (論理パス, バイト列) の列を .mtpak に書き出す。成功で true。
	static bool write(const std::filesystem::path&                                       outFile,
	                  const std::vector<std::pair<std::string, std::vector<uint8_t>>>&    entries,
	                  bool                                                                scramble = true);

	/// 既存の .mtpak を開いて index を読む。形式違いなら nullopt。
	/// ファイル先頭の .mtpak として開き、駄目なら exe 連結のフッタを探して開く。
	/// 呼ぶ側は「この exe に埋めたか、隣に置いたか」を気にしなくてよい。
	[[nodiscard]] static std::optional<AssetPack> open(const std::filesystem::path& file);

	/// exe の末尾へ .mtpak を連結する。配布物を 1 ファイルへ寄せるためのもの。
	/// すでに連結済みの exe には足さない (二重に埋めると、どちらを読んでいるのか
	/// 外から分からなくなる)。
	[[nodiscard]] static bool appendTo(const std::filesystem::path& exeFile,
	                                   const std::filesystem::path& packFile);

	[[nodiscard]] bool contains(std::string_view path) const
	{
		const std::string np = normalizePath(path);
		return std::any_of(m_entries.begin(), m_entries.end(),
		                   [&](const PackEntry& e) { return e.path == np; });
	}

	/// 論理パスの中身を取り出す (scramble 済みなら復元)。無ければ nullopt。
	[[nodiscard]] std::optional<std::vector<uint8_t>> read(std::string_view path) const;

	/// 目次にあるエントリの大きさ。無ければ 0。中身を読まずに指紋を作る用途のため。
	[[nodiscard]] uint64_t sizeOf(std::string_view path) const
	{
		const std::string np = normalizePath(path);
		for (const auto& e : m_entries)
		{
			if (e.path == np) { return e.size; }
		}
		return 0;
	}

	[[nodiscard]] std::vector<std::string> list() const
	{
		std::vector<std::string> out;
		out.reserve(m_entries.size());
		for (const auto& e : m_entries) { out.push_back(e.path); }
		return out;
	}

private:
	std::filesystem::path  m_file;
	uint64_t               m_baseOffset = 0;   ///< exe 連結時の pack 先頭位置
	std::vector<PackEntry> m_entries;
	bool                   m_scrambled = false;
};

// ── 実装 ──────────────────────────────────────────────────────

namespace detail
{
inline void wu16(std::ofstream& f, uint16_t v) { for (int i = 0; i < 2; ++i) f.put(char((v >> (8 * i)) & 0xFF)); }
inline void wu32(std::ofstream& f, uint32_t v) { for (int i = 0; i < 4; ++i) f.put(char((v >> (8 * i)) & 0xFF)); }
inline void wu64(std::ofstream& f, uint64_t v) { for (int i = 0; i < 8; ++i) f.put(char((v >> (8 * i)) & 0xFF)); }
inline uint16_t ru16(std::ifstream& f) { unsigned char b[2]; f.read(reinterpret_cast<char*>(b), 2); return uint16_t(b[0] | (b[1] << 8)); }
inline uint32_t ru32(std::ifstream& f) { unsigned char b[4]; f.read(reinterpret_cast<char*>(b), 4); uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= uint32_t(b[i]) << (8 * i); return v; }
inline uint64_t ru64(std::ifstream& f) { unsigned char b[8]; f.read(reinterpret_cast<char*>(b), 8); uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= uint64_t(b[i]) << (8 * i); return v; }
}  // namespace detail

inline bool AssetPack::write(const std::filesystem::path&                                    outFile,
                             const std::vector<std::pair<std::string, std::vector<uint8_t>>>& entries,
                             bool                                                             scramble)
{
	// index 部のサイズ = blob 開始 offset を先に算出する。
	uint64_t blobStart = 6 + 2 + 2 + 4;  // magic + version + flags + count
	for (const auto& [p, _] : entries) { blobStart += 2 + normalizePath(p).size() + 8 + 8; }

	std::ofstream f(outFile, std::ios::binary | std::ios::trunc);
	if (!f) { return false; }

	f.write(kMagic, 6);
	detail::wu16(f, kVersion);
	detail::wu16(f, scramble ? kFlagScrambled : 0);
	detail::wu32(f, static_cast<uint32_t>(entries.size()));

	std::vector<uint64_t> offsets;
	offsets.reserve(entries.size());
	uint64_t off = blobStart;
	for (const auto& [p, data] : entries)
	{
		const std::string np = normalizePath(p);
		detail::wu16(f, static_cast<uint16_t>(np.size()));
		f.write(np.data(), static_cast<std::streamsize>(np.size()));
		detail::wu64(f, off);
		detail::wu64(f, static_cast<uint64_t>(data.size()));
		offsets.push_back(off);
		off += data.size();
	}

	for (std::size_t i = 0; i < entries.size(); ++i)
	{
		std::vector<uint8_t> blob = entries[i].second;
		if (scramble) { xorScramble(blob, offsets[i]); }
		if (!blob.empty()) { f.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size())); }
	}
	return static_cast<bool>(f);
}

inline std::optional<AssetPack> AssetPack::open(const std::filesystem::path& file)
{
	std::ifstream f(file, std::ios::binary);
	if (!f) { return std::nullopt; }

	uint64_t base = 0;
	char magic[6] = {};
	f.read(magic, 6);
	if (f.gcount() != 6 || std::memcmp(magic, kMagic, 6) != 0)
	{
		// 先頭が .mtpak でなければ、exe 連結のフッタ (末尾 16 バイト) を探す。
		f.clear();
		f.seekg(0, std::ios::end);
		const auto fileSize = static_cast<uint64_t>(f.tellg());
		if (fileSize < 32) { return std::nullopt; }
		f.seekg(static_cast<std::streamoff>(fileSize - 16));
		char foot[16] = {};
		f.read(foot, 16);
		if (f.gcount() != 16 || std::memcmp(foot + 8, kAppendMagic, 8) != 0) { return std::nullopt; }
		std::memcpy(&base, foot, 8);
		if (base >= fileSize - 16) { return std::nullopt; }
		f.seekg(static_cast<std::streamoff>(base));
		f.read(magic, 6);
		if (f.gcount() != 6 || std::memcmp(magic, kMagic, 6) != 0) { return std::nullopt; }
	}

	(void)detail::ru16(f);  // version
	const uint16_t flags = detail::ru16(f);
	const uint32_t count = detail::ru32(f);

	AssetPack pack;
	pack.m_file       = file;
	pack.m_baseOffset = base;
	pack.m_scrambled  = (flags & kFlagScrambled) != 0;
	for (uint32_t i = 0; i < count; ++i)
	{
		const uint16_t len = detail::ru16(f);
		std::string    p(len, '\0');
		f.read(p.data(), len);
		const uint64_t off  = detail::ru64(f);
		const uint64_t size = detail::ru64(f);
		if (!f) { return std::nullopt; }
		pack.m_entries.push_back({std::move(p), off, size});
	}
	return pack;
}

inline std::optional<std::vector<uint8_t>> AssetPack::read(std::string_view path) const
{
	const std::string np = normalizePath(path);
	const PackEntry*   e  = nullptr;
	for (const auto& en : m_entries)
	{
		if (en.path == np) { e = &en; break; }
	}
	if (e == nullptr) { return std::nullopt; }

	std::ifstream f(m_file, std::ios::binary);
	if (!f) { return std::nullopt; }
	// scramble は pack 内の相対 offset で掛かっている。連結ぶんは seek にだけ足す。
	f.seekg(static_cast<std::streamoff>(m_baseOffset + e->offset));
	std::vector<uint8_t> data(e->size);
	if (e->size != 0) { f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(e->size)); }
	if (static_cast<uint64_t>(f.gcount()) != e->size) { return std::nullopt; }
	if (m_scrambled) { xorScramble(data, e->offset); }
	return data;
}

inline bool AssetPack::appendTo(const std::filesystem::path& exeFile,
                                const std::filesystem::path& packFile)
{
	{
		std::ifstream probe(exeFile, std::ios::binary);
		if (!probe) { return false; }
		probe.seekg(0, std::ios::end);
		const auto sz = static_cast<uint64_t>(probe.tellg());
		if (sz >= 16)
		{
			probe.seekg(static_cast<std::streamoff>(sz - 16));
			char foot[16] = {};
			probe.read(foot, 16);
			if (probe.gcount() == 16 && std::memcmp(foot + 8, kAppendMagic, 8) == 0) { return false; }
		}
	}
	std::ifstream in(packFile, std::ios::binary);
	if (!in) { return false; }
	std::ofstream out(exeFile, std::ios::binary | std::ios::app);
	if (!out) { return false; }
	out.seekp(0, std::ios::end);
	const auto base = static_cast<uint64_t>(out.tellp());
	out << in.rdbuf();
	char foot[16];
	std::memcpy(foot, &base, 8);
	std::memcpy(foot + 8, kAppendMagic, 8);
	out.write(foot, 16);
	return static_cast<bool>(out);
}

// ── グローバル mount (段階2) ──────────────────────────
//
// host が起動時に assets.mtpak を mountGlobal する。各 loader (画像/音/フォント/CEF)
// は readGlobal(logicalPath) を呼ぶ: pack が mount 済みなら pack を、未 mount (dev) なら
// disk を読む。これで「配布=パック秘匿 / 開発=バラ置き編集」が同じコードで両立する。

namespace detail
{
inline std::optional<AssetPack>& globalPack()
{
	static std::optional<AssetPack> pack;
	return pack;
}
inline bool& globalMountTried()
{
	static bool tried = false;
	return tried;
}

/// dev (未 mount) の相対パス解決の基準。host が MITIRU_ASSET_ROOT に game DLL の
/// 隣を入れる (cwd は exe 位置に固定されるため、cwd 相対だけだと game assets に届かない)。
/// pack と同じく env 経由。header-only の static は host / DLL / CEF helper で
/// 別インスタンスになるので、env が唯一の module 跨ぎ共有点。
inline const std::filesystem::path& globalDiskRoot()
{
	static const std::filesystem::path root = [] {
		const char* env = std::getenv("MITIRU_ASSET_ROOT");
		return (env != nullptr && env[0] != '\0') ? std::filesystem::path(env)
		                                          : std::filesystem::path{};
	}();
	return root;
}

/// disk から 1 ファイル読む (readGlobal の下請け)。開けない/読み損ねは nullopt。
[[nodiscard]] inline std::optional<std::vector<uint8_t>>
readDiskFile(const std::filesystem::path& p)
{
	std::ifstream f(p, std::ios::binary | std::ios::ate);
	if (!f) { return std::nullopt; }
	const auto end = f.tellg();
	f.seekg(0);
	std::vector<uint8_t> buf(end > 0 ? static_cast<std::size_t>(end) : 0);
	if (!buf.empty()) { f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size())); }
	if (f.bad()) { return std::nullopt; }
	return buf;
}

/// 未 mount なら、環境変数 MITIRU_ASSET_PACK が指す .mtpak を 1 度だけ開いて mount する。
/// header-only の static は host exe と game DLL / CEF helper で別インスタンスになるため、
/// host が mountGlobal しても DLL 内の loader には届かない。そこで host は env を set し、
/// 各プロセス/モジュールがこの lazy mount で同じ pack を開く (env は境界を越えて共有される)。
inline void ensureGlobalMount()
{
	if (globalPack().has_value() || globalMountTried()) { return; }
	globalMountTried() = true;
	const char* env = std::getenv("MITIRU_ASSET_PACK");
	if (env != nullptr && env[0] != '\0')
	{
		if (auto p = AssetPack::open(std::filesystem::path(env))) { globalPack() = std::move(*p); }
	}
}
}  // namespace detail

inline void mountGlobal(AssetPack pack)
{
	detail::globalPack()      = std::move(pack);
	detail::globalMountTried() = true;
}
inline void unmountGlobal()
{
	detail::globalPack().reset();
	detail::globalMountTried() = true;  // 明示 unmount は再 lazy-mount しない
}
[[nodiscard]] inline bool hasGlobalMount()
{
	detail::ensureGlobalMount();
	return detail::globalPack().has_value();
}

/// logicalPath を「mount 済み pack 優先 → 無ければ disk」で読む。
/// diskPath を別に指定したいとき (logical とファイル位置が違う) は第2引数で上書き。
[[nodiscard]] inline std::optional<std::vector<uint8_t>>
readGlobal(std::string_view logicalPath, const std::filesystem::path& diskPath = {})
{
	detail::ensureGlobalMount();
	if (auto& gp = detail::globalPack(); gp.has_value())
	{
		if (auto data = gp->read(logicalPath)) { return data; }
		// pack mount 中はそれが正本。pack に無いものは「無い」とする (秘匿配布で
		// disk を覗かせない)。
		return std::nullopt;
	}
	// dev (未 mount): disk から読む。相対 logical は MITIRU_ASSET_ROOT (host が game DLL の
	// 隣を指す) を先に見て、無ければ従来どおり cwd 相対 (examples の章 prefix 流儀)。
	if (diskPath.empty())
	{
		const std::filesystem::path rel{normalizePath(logicalPath)};
		if (const auto& root = detail::globalDiskRoot(); !root.empty() && rel.is_relative())
		{
			if (auto buf = detail::readDiskFile(root / rel)) { return buf; }
		}
		return detail::readDiskFile(rel);
	}
	return detail::readDiskFile(diskPath);
}

// ── ゲーム向け公開アセット読み込み API ───────────────
//
// ゲームは **生 std::ifstream で assets を読まず、これを使う**。pack 配布時はパックから、
// 開発時は disk から、同じ相対パスで読める。これにより `mitiru dist --pack` で
// JSON/レベル/マニフェスト等のデータファイルも秘匿でき、配布物にバラ置きが出ない。
// path は cwd 相対 (engine の他経路と同じ。例 "<game>/assets/levels/1.json")。

[[nodiscard]] inline std::optional<std::vector<uint8_t>> readAsset(std::string_view path)
{
	return readGlobal(path);
}

/// テキストアセット (JSON / マニフェスト等) を文字列で読む。
[[nodiscard]] inline std::optional<std::string> readAssetText(std::string_view path)
{
	auto bytes = readGlobal(path);
	if (!bytes) { return std::nullopt; }
	return std::string(bytes->begin(), bytes->end());
}

/// テキストアセットを行単位で読む (改行は CRLF / LF 両対応、末尾 CR は除去)。
[[nodiscard]] inline std::vector<std::string> readAssetLines(std::string_view path)
{
	std::vector<std::string> lines;
	auto text = readAssetText(path);
	if (!text) { return lines; }
	std::string cur;
	for (char c : *text)
	{
		if (c == '\n')
		{
			if (!cur.empty() && cur.back() == '\r') { cur.pop_back(); }
			lines.push_back(std::move(cur));
			cur.clear();
		}
		else { cur.push_back(c); }
	}
	if (!cur.empty())
	{
		if (cur.back() == '\r') { cur.pop_back(); }
		lines.push_back(std::move(cur));
	}
	return lines;
}

}  // namespace mitiru::vfs
