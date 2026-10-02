#pragma once

/// @file AssetPack_Mmap.hpp
/// @brief pack ファイル全体を読み取り専用でメモリに写像する薄いラッパー。
///
/// Win32 では CreateFileMapping/MapViewOfFile、POSIX では mmap を使う。どちらも
/// 使えない環境では、ファイル全体を std::vector へコピーして同じインタフェースを保つ
/// (機能上は動作するが、常駐コピーが増える fallback)。AssetPack::view() で、
/// scramble されていない v1 pack を直接参照するスパンとして使う。

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace mitiru::vfs::detail
{

class FileMap
{
public:
	FileMap() = default;
	~FileMap() { close(); }
	FileMap(const FileMap&)            = delete;
	FileMap& operator=(const FileMap&) = delete;
	FileMap(FileMap&& other) noexcept { *this = std::move(other); }
	FileMap& operator=(FileMap&& other) noexcept
	{
		if (this != &other)
		{
			close();
			m_data     = other.m_data;
			m_size     = other.m_size;
			m_fallback = std::move(other.m_fallback);
#if defined(_WIN32)
			m_file        = other.m_file;
			m_mapping     = other.m_mapping;
			other.m_file    = nullptr;
			other.m_mapping = nullptr;
#elif defined(__unix__) || defined(__APPLE__)
			m_fd       = other.m_fd;
			other.m_fd = -1;
#endif
			other.m_data = nullptr;
			other.m_size = 0;
		}
		return *this;
	}

	/// 成功時は true。空ファイルも map せず、size()==0 の成功として扱う。
	[[nodiscard]] bool open(const std::filesystem::path& file);
	void               close();

	[[nodiscard]] const uint8_t* data() const noexcept { return m_data; }
	[[nodiscard]] std::size_t    size() const noexcept { return m_size; }
	[[nodiscard]] bool           valid() const noexcept { return m_data != nullptr || m_size == 0; }

private:
	const uint8_t*        m_data = nullptr;
	std::size_t           m_size = 0;
	std::vector<uint8_t>  m_fallback;  // fallback 経路のみ実体を保持する
#if defined(_WIN32)
	void* m_file    = nullptr;
	void* m_mapping = nullptr;
#elif defined(__unix__) || defined(__APPLE__)
	int m_fd = -1;
#endif
};

inline bool FileMap::open(const std::filesystem::path& file)
{
	close();
#if defined(_WIN32)
	HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
	                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) { return false; }
	LARGE_INTEGER sz{};
	if (!GetFileSizeEx(h, &sz) || sz.QuadPart == 0)
	{
		const bool empty = (sz.QuadPart == 0);
		CloseHandle(h);
		return empty;
	}
	HANDLE mapping = CreateFileMappingW(h, nullptr, PAGE_READONLY, 0, 0, nullptr);
	if (mapping == nullptr)
	{
		CloseHandle(h);
		return false;
	}
	void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
	if (view == nullptr)
	{
		CloseHandle(mapping);
		CloseHandle(h);
		return false;
	}
	m_file    = h;
	m_mapping = mapping;
	m_data    = static_cast<const uint8_t*>(view);
	m_size    = static_cast<std::size_t>(sz.QuadPart);
	return true;
#elif defined(__unix__) || defined(__APPLE__)
	int fd = ::open(file.c_str(), O_RDONLY);
	if (fd < 0) { return false; }
	struct stat st{};
	if (fstat(fd, &st) != 0) { ::close(fd); return false; }
	if (st.st_size == 0) { ::close(fd); return true; }
	void* addr = mmap(nullptr, static_cast<std::size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
	if (addr == MAP_FAILED) { ::close(fd); return false; }
	m_fd   = fd;
	m_data = static_cast<const uint8_t*>(addr);
	m_size = static_cast<std::size_t>(st.st_size);
	return true;
#else
	std::ifstream f(file, std::ios::binary | std::ios::ate);
	if (!f) { return false; }
	const auto end = f.tellg();
	f.seekg(0);
	m_fallback.resize(end > 0 ? static_cast<std::size_t>(end) : 0);
	if (!m_fallback.empty()) { f.read(reinterpret_cast<char*>(m_fallback.data()), static_cast<std::streamsize>(m_fallback.size())); }
	m_data = m_fallback.data();
	m_size = m_fallback.size();
	return true;
#endif
}

inline void FileMap::close()
{
#if defined(_WIN32)
	if (m_data != nullptr) { UnmapViewOfFile(m_data); }
	if (m_mapping != nullptr) { CloseHandle(static_cast<HANDLE>(m_mapping)); }
	if (m_file != nullptr) { CloseHandle(static_cast<HANDLE>(m_file)); }
	m_file    = nullptr;
	m_mapping = nullptr;
#elif defined(__unix__) || defined(__APPLE__)
	if (m_data != nullptr && m_fd >= 0) { munmap(const_cast<uint8_t*>(m_data), m_size); }
	if (m_fd >= 0) { ::close(m_fd); }
	m_fd = -1;
#endif
	m_data = nullptr;
	m_size = 0;
	m_fallback.clear();
}

}  // namespace mitiru::vfs::detail
