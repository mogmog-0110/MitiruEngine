#pragma once

/// @file ModuleHost.hpp
/// @brief Game DLL を host process に load する RAII wrapper (v0.2.0 step 2)
/// @details
/// Windows: LoadLibrary / GetProcAddress / FreeLibrary を OS handle と共に
/// 寿命管理する。それ以外の OS では未実装 (load() が常に false を返す)。
///
/// **Reload-safe copy strategy**:
/// 直接 `LoadLibrary("game.dll")` すると Windows は元 .dll を file lock し、
/// rebuild できなくなる (リンカが .dll を上書きできない)。これを避けるため、
/// load() は受け取った source path を **%TEMP%/mitiru_module_<pid>_<seq>.dll**
/// に copy してから、その copy を LoadLibrary する。元 file は free のまま。
///
/// 同じ source path の reload で seq を bump するのは、Windows が完全に同じ
/// path に LoadLibrary すると refcount を増やすだけで新しい code を load しない
/// ため。temp file の名前を変えれば確実に新しい code が load される。

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

#if defined(_WIN32)
// HMODULE 等の型のために windows.h が必要だが、ヘッダー汚染を最小化するため
// このファイルに include。ModuleHost.hpp 自体は Engine.hpp から forward-decl
// 経由でしか参照されないため、windows.h の伝搬は ModuleHost.hpp 利用者
// (= Engine_Module.hpp + テスト) に限定される。
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::module
{

/// @brief Game DLL を一つ host する。move-only。
///
/// Lifecycle:
///   1. `load(source)`  — copy source to temp, LoadLibrary, resolve symbols
///   2. (caller invokes loadFn() to populate ModuleApi + memory)
///   3. `unload()`      — FreeLibrary + delete temp file
///   4. (or destructor — same as unload)
///
/// Reload is "unload() → load(source)" with a fresh temp filename — host code
/// holding the ModuleApi callbacks MUST drop them between unload and the
/// subsequent load (the function pointers belong to the old DLL).
class ModuleHost
{
public:
	ModuleHost() = default;

	~ModuleHost()
	{
		// Best-effort cleanup. Errors here can't be reported (we're in a
		// destructor), but the temp file is in %TEMP% so the OS reaps it
		// eventually anyway.
		unload();
	}

	ModuleHost(const ModuleHost&) = delete;
	ModuleHost& operator=(const ModuleHost&) = delete;

	ModuleHost(ModuleHost&& other) noexcept
		: m_sourcePath(std::move(other.m_sourcePath))
		, m_runtimePath(std::move(other.m_runtimePath))
		, m_lastError(std::move(other.m_lastError))
#if defined(_WIN32)
		, m_handle(other.m_handle)
#endif
	{
#if defined(_WIN32)
		other.m_handle = nullptr;
#endif
	}

	ModuleHost& operator=(ModuleHost&& other) noexcept
	{
		if (this != &other)
		{
			unload();
			m_sourcePath  = std::move(other.m_sourcePath);
			m_runtimePath = std::move(other.m_runtimePath);
			m_lastError   = std::move(other.m_lastError);
#if defined(_WIN32)
			m_handle      = other.m_handle;
			other.m_handle = nullptr;
#endif
		}
		return *this;
	}

	/// @brief Load the DLL.
	/// @param source 元 DLL の path (rebuild される側)
	/// @return 成功なら true、失敗なら false (詳細は lastError() を参照)
	/// @details
	///   - source を %TEMP% に copy してから LoadLibrary
	///   - `kLoadSymbol` (= "mitiru_module_load") が見つからないと失敗扱い
	///   - 既に load() 済みなら false (先に unload() を呼ぶこと)
	bool load(std::filesystem::path source)
	{
		if (isLoaded())
		{
			m_lastError = "module already loaded; call unload() first";
			return false;
		}

#if !defined(_WIN32)
		(void)source;
		m_lastError = "ModuleHost is not implemented on this platform "
		              "(only Windows is supported in v0.2.0)";
		return false;
#else
		std::error_code ec;
		if (!std::filesystem::exists(source, ec) || ec)
		{
			m_lastError = "source DLL not found: " + source.string();
			return false;
		}

		auto runtimePath = makeUniqueTempPath();
		std::filesystem::copy_file(
			source, runtimePath,
			std::filesystem::copy_options::overwrite_existing, ec);
		if (ec)
		{
			m_lastError = "failed to copy DLL to temp: " + ec.message();
			return false;
		}

		// `LoadLibraryW` for a Unicode-safe path. The temp filename is ASCII
		// but %TEMP% may contain non-ASCII characters.
		HMODULE handle = ::LoadLibraryW(runtimePath.wstring().c_str());
		if (handle == nullptr)
		{
			const DWORD err = ::GetLastError();
			std::error_code rmEc;
			std::filesystem::remove(runtimePath, rmEc);
			m_lastError = "LoadLibrary failed (GetLastError=" +
			              std::to_string(err) + ")";
			return false;
		}

		// Verify the entry symbol exists before we declare success — saves the
		// caller a separate "did GetProcAddress return null?" check.
		auto* loadFnPtr = ::GetProcAddress(handle, kLoadSymbol);
		if (loadFnPtr == nullptr)
		{
			::FreeLibrary(handle);
			std::error_code rmEc;
			std::filesystem::remove(runtimePath, rmEc);
			m_lastError = "DLL is missing required export: " +
			              std::string{kLoadSymbol};
			return false;
		}

		m_sourcePath  = std::move(source);
		m_runtimePath = std::move(runtimePath);
		m_handle      = handle;
		m_lastError.clear();
		return true;
#endif
	}

	/// @brief Unload the DLL. Safe to call multiple times.
	/// @details FreeLibrary + delete temp file. Errors are swallowed (best-effort).
	void unload() noexcept
	{
#if defined(_WIN32)
		if (m_handle != nullptr)
		{
			::FreeLibrary(m_handle);
			m_handle = nullptr;
		}
#endif
		if (!m_runtimePath.empty())
		{
			std::error_code ec;
			std::filesystem::remove(m_runtimePath, ec);
			m_runtimePath.clear();
		}
		m_sourcePath.clear();
	}

	[[nodiscard]] bool isLoaded() const noexcept
	{
#if defined(_WIN32)
		return m_handle != nullptr;
#else
		return false;
#endif
	}

	/// @brief Resolve the load entry symbol. nullptr if not loaded.
	[[nodiscard]] ModuleLoadFn loadFn() const noexcept
	{
#if defined(_WIN32)
		if (m_handle == nullptr) { return nullptr; }
		auto* p = ::GetProcAddress(m_handle, kLoadSymbol);
		return reinterpret_cast<ModuleLoadFn>(p);
#else
		return nullptr;
#endif
	}

	/// @brief Resolve the unload entry symbol. nullptr if not loaded or absent.
	[[nodiscard]] ModuleUnloadFn unloadFn() const noexcept
	{
#if defined(_WIN32)
		if (m_handle == nullptr) { return nullptr; }
		auto* p = ::GetProcAddress(m_handle, kUnloadSymbol);
		return reinterpret_cast<ModuleUnloadFn>(p);
#else
		return nullptr;
#endif
	}

	/// @brief 元 DLL の path (load() に渡された値)。未 load なら空。
	[[nodiscard]] const std::filesystem::path& sourcePath() const noexcept
	{
		return m_sourcePath;
	}

	/// @brief 実際に LoadLibrary した temp copy の path。未 load なら空。
	[[nodiscard]] const std::filesystem::path& runtimePath() const noexcept
	{
		return m_runtimePath;
	}

	/// @brief 最後の load() 失敗の原因。成功 / 未呼び出し時は空。
	[[nodiscard]] const std::string& lastError() const noexcept
	{
		return m_lastError;
	}

private:
	/// @brief Build a unique temp path: %TEMP%/mitiru_module_<pid>_<seq>.dll
	static std::filesystem::path makeUniqueTempPath()
	{
		static std::uint64_t s_seq = 0;
		++s_seq;
#if defined(_WIN32)
		const auto pid = static_cast<std::uint64_t>(::GetCurrentProcessId());
#else
		const std::uint64_t pid = 0;
#endif
		std::string filename = "mitiru_module_" + std::to_string(pid) +
		                       "_" + std::to_string(s_seq) + ".dll";
		std::error_code ec;
		auto tmp = std::filesystem::temp_directory_path(ec);
		if (ec)
		{
			// Fall back to source-relative if %TEMP% is unavailable.
			tmp = ".";
		}
		return tmp / filename;
	}

	std::filesystem::path m_sourcePath;
	std::filesystem::path m_runtimePath;
	std::string           m_lastError;
#if defined(_WIN32)
	HMODULE               m_handle{nullptr};
#endif
};

}  // namespace mitiru::module
