#pragma once

/// @file SaveStore.hpp
/// @brief セーブ/ロードの共通インフラ — atomic 書き込み + FNV-1a hash 破損検知。
/// @details game が任意の bytes blob を slot 名で保存し、後で同じ slot から読み戻す。
///          途中 crash でも壊れたファイルが残らないよう temp → rename の atomic 書き込み。

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace mitiru::save
{

namespace detail
{
	/// @brief FNV-1a 32-bit hash (破損検知用、暗号強度なし)。
	[[nodiscard]] inline std::uint32_t fnv1a32(const std::uint8_t* data, std::size_t n) noexcept
	{
		std::uint32_t h = 0x811C9DC5u;
		for (std::size_t i = 0; i < n; ++i)
		{
			h ^= data[i];
			h *= 0x01000193u;
		}
		return h;
	}

	/// @brief slot 名を安全なファイル名に正規化する (英数 / _ / - のみ許可)。
	[[nodiscard]] inline std::string sanitize(std::string_view slot)
	{
		std::string out; out.reserve(slot.size());
		for (char c : slot)
		{
			if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			    || c == '_' || c == '-')
			{
				out.push_back(c);
			}
			else { out.push_back('_'); }
		}
		if (out.empty()) { out = "_"; }
		return out;
	}

	// ファイル format: magic(4) + version(1) + length(4 LE) + hash(4 LE) + data(length)
	static constexpr char     kMagic[4] = {'M','I','S','A'};
	static constexpr std::uint8_t kVersion = 1;
}

class SaveStore
{
public:
	/// @brief セーブ先 dir を指定して構築する (例: %APPDATA%/mitiru/<project>/saves)。
	explicit SaveStore(std::filesystem::path dir) : m_dir(std::move(dir)) {}

	/// @brief slot に data を書き込む (atomic、新内容は完全に書かれてから rename)。
	[[nodiscard]] bool write(std::string_view slot, const std::uint8_t* data, std::size_t size)
	{
		std::error_code ec;
		std::filesystem::create_directories(m_dir, ec);  // 既存なら no-op
		const auto finalPath = m_dir / (detail::sanitize(slot) + ".sav");
		const auto tmpPath   = m_dir / (detail::sanitize(slot) + ".sav.tmp");

		{
			std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
			if (!out) { return false; }
			out.write(detail::kMagic, 4);
			out.put(static_cast<char>(detail::kVersion));
			const std::uint32_t len = static_cast<std::uint32_t>(size);
			out.write(reinterpret_cast<const char*>(&len), 4);
			const std::uint32_t h = (data && size > 0) ? detail::fnv1a32(data, size) : 0u;
			out.write(reinterpret_cast<const char*>(&h), 4);
			if (size > 0 && data) { out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size)); }
			out.flush();
			if (!out) { std::filesystem::remove(tmpPath, ec); return false; }
		}

		// atomic replace: 既存 final があれば上書きする (rename)。
		std::filesystem::rename(tmpPath, finalPath, ec);
		if (ec)
		{
			// 一部 OS では target 存在時 rename が失敗するので、remove して再試行。
			std::filesystem::remove(finalPath, ec);
			std::filesystem::rename(tmpPath, finalPath, ec);
		}
		return !ec;
	}

	/// @brief slot から読み戻す。format/hash が一致しない or 不在なら nullopt。
	[[nodiscard]] std::optional<std::vector<std::uint8_t>> read(std::string_view slot) const
	{
		const auto path = m_dir / (detail::sanitize(slot) + ".sav");
		std::ifstream in(path, std::ios::binary);
		if (!in) { return std::nullopt; }
		char magic[4];
		in.read(magic, 4);
		if (!in || std::memcmp(magic, detail::kMagic, 4) != 0) { return std::nullopt; }
		std::uint8_t version = 0;
		in.read(reinterpret_cast<char*>(&version), 1);
		if (!in || version != detail::kVersion) { return std::nullopt; }
		std::uint32_t len = 0, expectedHash = 0;
		in.read(reinterpret_cast<char*>(&len), 4);
		in.read(reinterpret_cast<char*>(&expectedHash), 4);
		if (!in) { return std::nullopt; }
		std::vector<std::uint8_t> data(len);
		if (len > 0)
		{
			in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(len));
			if (static_cast<std::uint32_t>(in.gcount()) != len) { return std::nullopt; }
			const std::uint32_t actual = detail::fnv1a32(data.data(), data.size());
			if (actual != expectedHash) { return std::nullopt; }  // 破損検知
		}
		else if (expectedHash != 0u) { return std::nullopt; }
		return data;
	}

	[[nodiscard]] bool exists(std::string_view slot) const
	{
		std::error_code ec;
		return std::filesystem::exists(m_dir / (detail::sanitize(slot) + ".sav"), ec);
	}

	[[nodiscard]] bool remove(std::string_view slot)
	{
		std::error_code ec;
		return std::filesystem::remove(m_dir / (detail::sanitize(slot) + ".sav"), ec);
	}

private:
	std::filesystem::path m_dir;
};

}  // namespace mitiru::save
