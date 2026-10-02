#pragma once

/// @file AtomicFile.hpp
/// @brief ファイルを「全部書けたものだけが本命の名前になる」形で置き換える。
/// @details 一時ファイルへ書いてディスクへ流し切ってから、本命の名前へ 1 回の rename で差し替える。
///          途中で落ちても本命は前の内容のまま残る。keepBackup を立てると、差し替える前の本命を
///          `<名前>.bak` に写しておく。本命の中身がディスク上で化けた時に、読む側が 1 つ前へ戻れる。

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <Windows.h>
#else
#  include <cstdio>
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace mitiru::save
{

[[nodiscard]] inline std::filesystem::path withSuffix(const std::filesystem::path& p, const char* suffix)
{
	std::filesystem::path out = p;
	out += suffix;
	return out;
}

namespace detail
{

#ifdef _WIN32
// FlushFileBuffers まで済ませる。std::ofstream は OS のキャッシュに渡すだけで、電源断では消える。
[[nodiscard]] inline bool writeDurable(const std::filesystem::path& path, std::span<const std::uint8_t> bytes,
                                       std::string& error)
{
	HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE)
	{
		error = "開けない (Win32 エラー " + std::to_string(::GetLastError()) + ")";
		return false;
	}
	std::size_t done = 0;
	bool ok = true;
	while (ok && done < bytes.size())
	{
		const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - done, 1u << 30));
		DWORD written = 0;
		ok = ::WriteFile(h, bytes.data() + done, chunk, &written, nullptr) != 0 && written == chunk;
		done += written;
	}
	if (ok) { ok = ::FlushFileBuffers(h) != 0; }
	if (!ok) { error = "書き込みに失敗した (Win32 エラー " + std::to_string(::GetLastError()) + ")"; }
	::CloseHandle(h);
	return ok;
}

[[nodiscard]] inline bool replaceFile(const std::filesystem::path& from, const std::filesystem::path& to,
                                      std::string& error)
{
	if (::MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0)
	{
		return true;
	}
	error = "差し替えに失敗した (Win32 エラー " + std::to_string(::GetLastError()) + ")";
	return false;
}
#else
[[nodiscard]] inline bool writeDurable(const std::filesystem::path& path, std::span<const std::uint8_t> bytes,
                                       std::string& error)
{
	const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
	{
		error = "開けない";
		return false;
	}
	std::size_t done = 0;
	bool ok = true;
	while (ok && done < bytes.size())
	{
		const auto n = ::write(fd, bytes.data() + done, bytes.size() - done);
		ok = n > 0;
		if (ok) { done += static_cast<std::size_t>(n); }
	}
	if (ok) { ok = ::fsync(fd) == 0; }
	if (!ok) { error = "書き込みに失敗した"; }
	::close(fd);
	return ok;
}

[[nodiscard]] inline bool replaceFile(const std::filesystem::path& from, const std::filesystem::path& to,
                                      std::string& error)
{
	if (std::rename(from.c_str(), to.c_str()) == 0) { return true; }
	error = "差し替えに失敗した";
	return false;
}
#endif

}  // namespace detail

/// @brief bytes を target へ置き換える。失敗したら false と error (一時ファイルは残さない)。
[[nodiscard]] inline bool writeFileAtomic(const std::filesystem::path& target, std::span<const std::uint8_t> bytes,
                                          bool keepBackup, std::string* error = nullptr)
{
	std::string why;
	std::error_code ec;
	if (target.has_parent_path()) { std::filesystem::create_directories(target.parent_path(), ec); }
	const auto tmp = withSuffix(target, ".tmp");
	bool ok = detail::writeDurable(tmp, bytes, why);
	if (ok && keepBackup && std::filesystem::exists(target, ec))
	{
		// 写しは前の本命の控え。途中で切れても読む側が checksum で弾くので、atomic でなくてよい。
		// 写せなくても新しい本命は書き切ってあるので保存は続け、半端な写しだけ消す
		const auto bak = withSuffix(target, ".bak");
		if (!std::filesystem::copy_file(target, bak, std::filesystem::copy_options::overwrite_existing, ec) || ec)
		{
			std::error_code ignore;
			std::filesystem::remove(bak, ignore);
		}
	}
	if (ok) { ok = detail::replaceFile(tmp, target, why); }
	if (!ok)
	{
		std::filesystem::remove(tmp, ec);
		if (error != nullptr) { *error = target.string() + ": " + why; }
	}
	return ok;
}

/// @brief ファイルを丸ごと読む。無い・読めない時は nullopt。
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>> readWholeFile(const std::filesystem::path& path)
{
	std::ifstream in(path, std::ios::binary | std::ios::ate);
	if (!in) { return std::nullopt; }
	const auto size = in.tellg();
	if (size < 0) { return std::nullopt; }
	std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
	in.seekg(0);
	if (!bytes.empty() && !in.read(reinterpret_cast<char*>(bytes.data()), size)) { return std::nullopt; }
	return bytes;
}

}  // namespace mitiru::save
