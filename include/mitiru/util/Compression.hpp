#pragma once

/// @file Compression.hpp
/// @brief Zstandard圧縮ラッパーおよびアセットバンドルシステム
///
/// MITIRU_HAS_ZSTDが定義されている場合はzstdライブラリを使用した実圧縮を行い、
/// 未定義の場合はno-op（無圧縮パススルー）として動作する。
///
/// @code
/// using mitiru::util::Compression;
///
/// // 文字列を圧縮・展開
/// auto compressed = Compression::compressString("Hello, World!");
/// auto original = Compression::decompressString(compressed.data(), compressed.size());
///
/// // アセットバンドル
/// using mitiru::util::CompressedAssetBundle;
/// std::map<std::string, std::vector<uint8_t>> files;
/// files["config.json"] = {'{', '}'};
/// auto bundle = CompressedAssetBundle::pack(files);
/// auto unpacked = CompressedAssetBundle::unpack(bundle);
/// @endcode

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef MITIRU_HAS_ZSTD
#include <zstd.h>
#endif

namespace mitiru::util
{

/// @brief Zstandard圧縮ラッパー（全static）
/// @details MITIRU_HAS_ZSTD未定義時はパススルー（無圧縮）で動作する
class Compression
{
public:
	/// @brief 圧縮レベルの最小値
	static constexpr int kMinLevel = 1;

	/// @brief 圧縮レベルの最大値
	static constexpr int kMaxLevel = 22;

	/// @brief zstdマジックバイト
	static constexpr uint32_t kMagic = 0xFD2FB528;

	/// @brief 高速圧縮プリセット
	[[nodiscard]] static constexpr int fast() noexcept { return 1; }

	/// @brief バランス圧縮プリセット
	[[nodiscard]] static constexpr int balanced() noexcept { return 3; }

	/// @brief 最高圧縮プリセット
	[[nodiscard]] static constexpr int best() noexcept { return 19; }

	/// @brief データを圧縮する
	/// @param data 入力データへのポインタ
	/// @param size 入力データのサイズ（バイト）
	/// @param level 圧縮レベル（1〜22、デフォルト3）
	/// @return 圧縮されたデータ
	[[nodiscard]] static std::vector<uint8_t> compress(
		const uint8_t* data, std::size_t size, int level = balanced())
	{
		if (data == nullptr || size == 0)
		{
			return {};
		}

		level = clampLevel(level);

#ifdef MITIRU_HAS_ZSTD
		const std::size_t bound = ZSTD_compressBound(size);
		std::vector<uint8_t> result(bound);

		const std::size_t compressedSize = ZSTD_compress(
			result.data(), bound, data, size, level);

		if (ZSTD_isError(compressedSize))
		{
			throw std::runtime_error(
				std::string("Compression::compress: ") + ZSTD_getErrorName(compressedSize));
		}

		result.resize(compressedSize);
		return result;
#else
		/// no-op: 元データをそのまま返す
		return std::vector<uint8_t>(data, data + size);
#endif
	}

	/// @brief vectorデータを圧縮する
	/// @param data 入力データ
	/// @param level 圧縮レベル
	/// @return 圧縮されたデータ
	[[nodiscard]] static std::vector<uint8_t> compress(
		const std::vector<uint8_t>& data, int level = balanced())
	{
		return compress(data.data(), data.size(), level);
	}

	/// @brief 圧縮データを展開する
	/// @param compressedData 圧縮データへのポインタ
	/// @param size 圧縮データのサイズ
	/// @param originalSize 展開後のデータサイズ
	/// @return 展開されたデータ
	/// @brief 展開サイズの上限（256 MB）
	static constexpr std::size_t kMaxDecompressSize = 256 * 1024 * 1024;

	[[nodiscard]] static std::vector<uint8_t> decompress(
		const uint8_t* compressedData, std::size_t size, std::size_t originalSize)
	{
		if (compressedData == nullptr || size == 0)
		{
			return {};
		}

		if (originalSize > kMaxDecompressSize)
		{
			throw std::runtime_error(
				"Compression::decompress: originalSize exceeds maximum allowed size");
		}

#ifdef MITIRU_HAS_ZSTD
		std::vector<uint8_t> result(originalSize);

		const std::size_t decompressedSize = ZSTD_decompress(
			result.data(), originalSize, compressedData, size);

		if (ZSTD_isError(decompressedSize))
		{
			throw std::runtime_error(
				std::string("Compression::decompress: ") + ZSTD_getErrorName(decompressedSize));
		}

		result.resize(decompressedSize);
		return result;
#else
		/// no-op: 入力データをそのまま返す（originalSizeは無圧縮時は入力サイズと同一）
		return std::vector<uint8_t>(compressedData, compressedData + size);
#endif
	}

	/// @brief 文字列を圧縮する
	/// @param str 入力文字列
	/// @param level 圧縮レベル
	/// @return 圧縮されたデータ
	[[nodiscard]] static std::vector<uint8_t> compressString(
		const std::string& str, int level = balanced())
	{
		return compress(
			reinterpret_cast<const uint8_t*>(str.data()), str.size(), level);
	}

	/// @brief 圧縮データを文字列として展開する
	/// @param data 圧縮データへのポインタ
	/// @param size 圧縮データのサイズ
	/// @return 展開された文字列
	[[nodiscard]] static std::string decompressString(
		const uint8_t* data, std::size_t size)
	{
		if (data == nullptr || size == 0)
		{
			return {};
		}

#ifdef MITIRU_HAS_ZSTD
		/// zstdフレームから元のサイズを取得
		const unsigned long long contentSize = ZSTD_getFrameContentSize(data, size);
		if (contentSize == ZSTD_CONTENTSIZE_UNKNOWN || contentSize == ZSTD_CONTENTSIZE_ERROR)
		{
			throw std::runtime_error("Compression::decompressString: unknown content size");
		}

		auto decompressed = decompress(data, size, static_cast<std::size_t>(contentSize));
		return std::string(
			reinterpret_cast<const char*>(decompressed.data()), decompressed.size());
#else
		/// no-op: 入力をそのまま文字列として返す
		return std::string(reinterpret_cast<const char*>(data), size);
#endif
	}

	/// @brief 圧縮後のサイズを推定する
	/// @param originalSize 元データのサイズ
	/// @return 圧縮後の推定最大サイズ
	[[nodiscard]] static std::size_t estimateCompressedSize(std::size_t originalSize) noexcept
	{
#ifdef MITIRU_HAS_ZSTD
		return ZSTD_compressBound(originalSize);
#else
		/// no-op: 圧縮しないので同じサイズ
		return originalSize;
#endif
	}

	/// @brief データがzstd圧縮されているかをマジックバイトで判定する
	/// @param data データへのポインタ
	/// @param size データのサイズ
	/// @return zstdマジックバイトを持つ場合true
	[[nodiscard]] static bool isCompressed(const uint8_t* data, std::size_t size) noexcept
	{
		if (data == nullptr || size < 4)
		{
			return false;
		}

		/// zstdマジックバイト: 0x28 0xB5 0x2F 0xFD（リトルエンディアン）
		uint32_t magic = 0;
		std::memcpy(&magic, data, sizeof(uint32_t));
		return magic == kMagic;
	}

private:
	/// @brief 圧縮レベルを有効範囲にクランプする
	[[nodiscard]] static int clampLevel(int level) noexcept
	{
		if (level < kMinLevel) return kMinLevel;
		if (level > kMaxLevel) return kMaxLevel;
		return level;
	}
};

/// @brief 圧縮アセットバンドル
/// @details 複数ファイルを個別圧縮してバンドルにまとめる。
///          各ファイルは個別に圧縮されるためランダムアクセスが可能。
///
/// バンドルフォーマット:
/// - ヘッダ: magic(4) + version(4) + fileCount(4)
/// - TOC: [nameLen(4) + name(N) + offset(8) + compressedSize(8) + originalSize(8)] * fileCount
/// - データ: 各ファイルの圧縮データ
class CompressedAssetBundle
{
public:
	/// @brief バンドルマジックバイト "MABF" (Mitiru Asset Bundle Format)
	static constexpr uint32_t kBundleMagic = 0x4642414D;

	/// @brief バンドルバージョン
	static constexpr uint32_t kBundleVersion = 1;

	/// @brief 複数ファイルを圧縮してバンドルにまとめる
	/// @param files ファイル名とデータのマップ
	/// @param level 圧縮レベル
	/// @return バンドルデータ
	[[nodiscard]] static std::vector<uint8_t> pack(
		const std::map<std::string, std::vector<uint8_t>>& files,
		int level = Compression::balanced())
	{
		/// 各ファイルを圧縮
		struct Entry
		{
			std::string name;
			std::vector<uint8_t> compressed;
			uint64_t originalSize;
		};

		std::vector<Entry> entries;
		entries.reserve(files.size());

		for (const auto& [name, data] : files)
		{
			Entry entry;
			entry.name = name;
			entry.originalSize = data.size();
			entry.compressed = Compression::compress(data, level);
			entries.push_back(std::move(entry));
		}

		/// バッファサイズを推定
		std::size_t totalSize = 12; // magic + version + fileCount の3フィールド
		for (const auto& e : entries)
		{
			totalSize += 4 + e.name.size() + 8 + 8 + 8; // TOCエントリ
			totalSize += e.compressed.size();              // データ本体
		}

		std::vector<uint8_t> bundle;
		bundle.reserve(totalSize);

		/// ヘッダ書き込み
		writeU32(bundle, kBundleMagic);
		writeU32(bundle, kBundleVersion);
		writeU32(bundle, static_cast<uint32_t>(entries.size()));

		/// TOCのオフセットを計算するため、TOC自体のサイズを先に計算
		std::size_t tocSize = 0;
		for (const auto& e : entries)
		{
			tocSize += 4 + e.name.size() + 8 + 8 + 8;
		}

		uint64_t dataOffset = 12 + tocSize;

		/// TOC書き込み
		for (const auto& e : entries)
		{
			writeU32(bundle, static_cast<uint32_t>(e.name.size()));
			for (char c : e.name)
			{
				bundle.push_back(static_cast<uint8_t>(c));
			}
			writeU64(bundle, dataOffset);
			writeU64(bundle, static_cast<uint64_t>(e.compressed.size()));
			writeU64(bundle, e.originalSize);
			dataOffset += e.compressed.size();
		}

		/// データ書き込み
		for (const auto& e : entries)
		{
			bundle.insert(bundle.end(), e.compressed.begin(), e.compressed.end());
		}

		return bundle;
	}

	/// @brief バンドルから全ファイルを展開する
	/// @param bundle バンドルデータ
	/// @return ファイル名とデータのマップ
	/// @throws std::runtime_error 不正なバンドルの場合
	[[nodiscard]] static std::map<std::string, std::vector<uint8_t>> unpack(
		const std::vector<uint8_t>& bundle)
	{
		validateHeader(bundle);

		const uint32_t fileCount = readU32(bundle, 8);

		if (static_cast<uint64_t>(fileCount) * 28 > bundle.size() - 12)
		{
			throw std::runtime_error("CompressedAssetBundle: fileCount exceeds bundle size");
		}

		std::map<std::string, std::vector<uint8_t>> result;

		std::size_t pos = 12;
		for (uint32_t i = 0; i < fileCount; ++i)
		{
			auto [name, offset, compressedSize, originalSize, nextPos] = readTocEntry(bundle, pos);
			pos = nextPos;

			if (compressedSize > bundle.size() || offset > bundle.size() - compressedSize)
			{
				throw std::runtime_error("CompressedAssetBundle::unpack: data out of bounds");
			}

			result[name] = Compression::decompress(
				bundle.data() + offset,
				static_cast<std::size_t>(compressedSize),
				static_cast<std::size_t>(originalSize));
		}

		return result;
	}

	/// @brief バンドルから単一ファイルを展開する
	/// @param bundle バンドルデータ
	/// @param filename 展開するファイル名
	/// @return 展開されたデータ
	/// @throws std::runtime_error ファイルが見つからない場合
	[[nodiscard]] static std::vector<uint8_t> extractFile(
		const std::vector<uint8_t>& bundle, const std::string& filename)
	{
		validateHeader(bundle);

		const uint32_t fileCount = readU32(bundle, 8);
		std::size_t pos = 12;

		for (uint32_t i = 0; i < fileCount; ++i)
		{
			auto [name, offset, compressedSize, originalSize, nextPos] = readTocEntry(bundle, pos);
			pos = nextPos;

			if (name == filename)
			{
				if (compressedSize > bundle.size() || offset > bundle.size() - compressedSize)
				{
					throw std::runtime_error(
						"CompressedAssetBundle::extractFile: data out of bounds");
				}

				return Compression::decompress(
					bundle.data() + offset,
					static_cast<std::size_t>(compressedSize),
					static_cast<std::size_t>(originalSize));
			}
		}

		throw std::runtime_error(
			"CompressedAssetBundle::extractFile: file not found: " + filename);
	}

private:
	/// @brief ヘッダを検証する
	static void validateHeader(const std::vector<uint8_t>& bundle)
	{
		if (bundle.size() < 12)
		{
			throw std::runtime_error("CompressedAssetBundle: bundle too small");
		}

		const uint32_t magic = readU32(bundle, 0);
		if (magic != kBundleMagic)
		{
			throw std::runtime_error("CompressedAssetBundle: invalid magic");
		}

		const uint32_t version = readU32(bundle, 4);
		if (version != kBundleVersion)
		{
			throw std::runtime_error("CompressedAssetBundle: unsupported version");
		}
	}

	/// @brief TOCエントリを読み取る
	struct TocEntry
	{
		std::string name;
		uint64_t offset;
		uint64_t compressedSize;
		uint64_t originalSize;
		std::size_t nextPos;
	};

	[[nodiscard]] static TocEntry readTocEntry(
		const std::vector<uint8_t>& bundle, std::size_t pos)
	{
		if (pos + 4 > bundle.size())
		{
			throw std::runtime_error("CompressedAssetBundle: truncated TOC");
		}

		const uint32_t nameLen = readU32(bundle, pos);
		pos += 4;

		if (pos + nameLen + 24 > bundle.size())
		{
			throw std::runtime_error("CompressedAssetBundle: truncated TOC entry");
		}

		std::string name(
			reinterpret_cast<const char*>(bundle.data() + pos), nameLen);
		pos += nameLen;

		const uint64_t offset = readU64(bundle, pos);
		pos += 8;
		const uint64_t compressedSize = readU64(bundle, pos);
		pos += 8;
		const uint64_t originalSize = readU64(bundle, pos);
		pos += 8;

		return {std::move(name), offset, compressedSize, originalSize, pos};
	}

	/// @brief リトルエンディアンで32ビット整数を書き込む
	static void writeU32(std::vector<uint8_t>& buf, uint32_t value)
	{
		buf.push_back(static_cast<uint8_t>(value & 0xFF));
		buf.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
		buf.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
		buf.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
	}

	/// @brief リトルエンディアンで64ビット整数を書き込む
	static void writeU64(std::vector<uint8_t>& buf, uint64_t value)
	{
		for (int i = 0; i < 8; ++i)
		{
			buf.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
		}
	}

	/// @brief リトルエンディアンで32ビット整数を読み取る
	[[nodiscard]] static uint32_t readU32(
		const std::vector<uint8_t>& buf, std::size_t pos)
	{
		return static_cast<uint32_t>(buf[pos])
			| (static_cast<uint32_t>(buf[pos + 1]) << 8)
			| (static_cast<uint32_t>(buf[pos + 2]) << 16)
			| (static_cast<uint32_t>(buf[pos + 3]) << 24);
	}

	/// @brief リトルエンディアンで64ビット整数を読み取る
	[[nodiscard]] static uint64_t readU64(
		const std::vector<uint8_t>& buf, std::size_t pos)
	{
		uint64_t value = 0;
		for (int i = 0; i < 8; ++i)
		{
			value |= static_cast<uint64_t>(buf[pos + i]) << (i * 8);
		}
		return value;
	}
};

} // namespace mitiru::util
