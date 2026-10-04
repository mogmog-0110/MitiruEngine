#pragma once

/// @file Dx12ShaderDiskCache.hpp
/// @brief コンパイル済みシェーダーをディスクに残し、次の起動では HLSL をコンパイルし直さない
/// @details 鍵はソース・入口・ターゲット・フラグ・コンパイラの種類から作る XXH3 の 128 bit。
///          ソースが 1 文字でも変われば別の鍵になるので、古い結果を読むことはない。
///          置き場は MITIRU_SHADER_CACHE (0 で無効)、無ければ %LOCALAPPDATA%\MitiruEngine\shader_cache。
///          配布物は exe の隣の shader_cache を読むだけの置き場として持てる (`mitiru dist` が `mitiru_host --bake-caches` で作る)。
///          書く置き場に無いものだけをそこから読む。
///          読めない・壊れたファイルは無いものとして扱い、コンパイルし直して書き直す。
///          書くときは一時ファイルから rename するので、同時に走る別のプロセスが半端な中身を読まない。

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <mitiru/core/Env.hpp>
#include <mitiru/util/Hash.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <process.h>
#else
#include <unistd.h>
#endif

namespace mitiru::gfx
{

/// @brief 1 つのシェーダーの鍵
struct ShaderCacheKey
{
	std::uint64_t low = 0;
	std::uint64_t high = 0;

	[[nodiscard]] bool operator==(const ShaderCacheKey&) const noexcept = default;
};

/// @brief compiler はコンパイラの種類 (結果の形式が違うものを混ぜないため)
[[nodiscard]] inline ShaderCacheKey makeShaderCacheKey(std::string_view source, std::string_view entry,
                                                       std::string_view target, std::uint32_t flags,
                                                       std::uint32_t compiler) noexcept
{
	std::string meta;
	meta.reserve(entry.size() + target.size() + 24);
	meta.append(entry).push_back('\0');
	meta.append(target).push_back('\0');
	meta.append(std::to_string(flags)).push_back('\0');
	meta.append(std::to_string(compiler));
	const std::uint64_t seed = XXH3_64bits(meta.data(), meta.size());
	const XXH128_hash_t h = XXH3_128bits_withSeed(source.data(), source.size(), seed);
	return {h.low64, h.high64};
}

class ShaderDiskCache
{
public:
	/// @brief dir が空なら何もしない (読めず、書かない)。shipped は読むだけの置き場で、空なら見ない
	explicit ShaderDiskCache(std::filesystem::path dir, std::filesystem::path shipped = {})
		: m_dir(std::move(dir)), m_shipped(std::move(shipped))
	{
	}

	/// @brief 環境変数と既定の置き場から作る、プロセスに 1 つの置き場
	[[nodiscard]] static ShaderDiskCache& process()
	{
		static ShaderDiskCache cache(defaultDirectory(), shippedDirectoryNextToExe());
		return cache;
	}

	[[nodiscard]] bool enabled() const noexcept { return !m_dir.empty(); }
	[[nodiscard]] const std::filesystem::path& directory() const noexcept { return m_dir; }

	/// @brief 残っていれば out に入れて true。書く置き場に無ければ、読むだけの置き場を見る
	[[nodiscard]] bool load(const ShaderCacheKey& key, std::vector<std::uint8_t>& out) const
	{
		if (!enabled()) { return false; }
		const std::filesystem::path own = pathFor(key);
		return loadFrom(own, key, out) || (!m_shipped.empty() && loadFrom(m_shipped / own.filename(), key, out));
	}

	[[nodiscard]] const std::filesystem::path& shippedDirectory() const noexcept { return m_shipped; }

	/// @brief 残す。失敗しても描画は続けられるので、黙って諦める
	void store(const ShaderCacheKey& key, const void* data, std::size_t size) const
	{
		if (!enabled() || data == nullptr || size == 0 || size > kMaxBytes) { return; }
		std::error_code ec;
		std::filesystem::create_directories(m_dir, ec);
		const std::filesystem::path dst = pathFor(key);
		const std::filesystem::path tmp = dst.string() + "." + uniqueSuffix() + ".tmp";
		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out) { return; }
			Header h{};
			std::memcpy(h.magic, kMagic, sizeof h.magic);
			h.key = key;
			h.size = static_cast<std::uint64_t>(size);
			h.check = XXH3_64bits(data, size);
			out.write(reinterpret_cast<const char*>(&h), sizeof h);
			out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
			if (!out) { out.close(); std::filesystem::remove(tmp, ec); return; }
		}
		std::filesystem::rename(tmp, dst, ec);
		if (ec) { std::filesystem::remove(tmp, ec); }   // 同じ鍵を別のプロセスが先に書いた
	}

	[[nodiscard]] std::filesystem::path pathFor(const ShaderCacheKey& key) const
	{
		char name[48];
		std::snprintf(name, sizeof name, "%016llx%016llx.dxbc", static_cast<unsigned long long>(key.high),
		              static_cast<unsigned long long>(key.low));
		return m_dir / name;
	}

private:
	[[nodiscard]] static bool loadFrom(const std::filesystem::path& file, const ShaderCacheKey& key,
	                                   std::vector<std::uint8_t>& out)
	{
		std::ifstream in(file, std::ios::binary);
		Header h{};
		if (!in || !in.read(reinterpret_cast<char*>(&h), sizeof h)) { return false; }
		if (std::memcmp(h.magic, kMagic, sizeof h.magic) != 0 || !(h.key == key) || h.size == 0 || h.size > kMaxBytes)
		{
			return false;
		}
		out.resize(h.size);
		if (!in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(h.size))) { return false; }
		return XXH3_64bits(out.data(), out.size()) == h.check;
	}

	static constexpr char kMagic[8] = {'M', 'T', 'S', 'H', 'D', 'R', '0', '1'};
	static constexpr std::uint64_t kMaxBytes = 64ull * 1024 * 1024;

	struct Header
	{
		char magic[8];
		ShaderCacheKey key;
		std::uint64_t size;
		std::uint64_t check;
	};

	[[nodiscard]] static std::filesystem::path defaultDirectory()
	{
		if (const std::string dir = env::value("MITIRU_SHADER_CACHE"); !dir.empty())
		{
			return dir == "0" ? std::filesystem::path{} : std::filesystem::path(dir);
		}
#ifdef _WIN32
		if (const std::string local = env::value("LOCALAPPDATA"); !local.empty())
		{
			return std::filesystem::path(local) / "MitiruEngine" / "shader_cache";
		}
#endif
		std::error_code ec;
		const auto tmp = std::filesystem::temp_directory_path(ec);
		return ec ? std::filesystem::path{} : tmp / "mitiru_shader_cache";
	}

	/// @brief 配布物が exe の隣に置く読むだけの置き場。無ければ空
	[[nodiscard]] static std::filesystem::path shippedDirectoryNextToExe()
	{
#ifdef _WIN32
		wchar_t exe[MAX_PATH] = {};
		const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
		if (n == 0 || n >= MAX_PATH) { return {}; }
		std::error_code ec;
		const auto dir = std::filesystem::path(exe).parent_path() / "shader_cache";
		return std::filesystem::is_directory(dir, ec) ? dir : std::filesystem::path{};
#else
		return {};
#endif
	}

	[[nodiscard]] static std::string uniqueSuffix()
	{
#ifdef _WIN32
		const int pid = _getpid();
#else
		const int pid = static_cast<int>(::getpid());
#endif
		return std::to_string(pid) + "_" + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
	}

	std::filesystem::path m_dir;
	std::filesystem::path m_shipped;
};

} // namespace mitiru::gfx
