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
/// ライフサイクル:
///   1. `load(source)`。source を temp に copy、LoadLibrary、symbol 解決
///   2. (caller が loadFn() を呼び ModuleApi + memory を埋める)
///   3. `unload()`。FreeLibrary + temp file 削除
///   4. (または destructor。unload と同じ)
///
/// reload は「別 ModuleHost で load(source) → move 代入で差し替え」(先ロード・
/// 後差し替え) が正規。temp filename が一意なので同一 source でも並走 load できる。
/// move 代入 / unload は FreeLibrary するだけ。ModuleApi callback を保持する
/// host code は、その時点で古い関数 pointer を破棄しなければならない。
class ModuleHost
{
public:
	ModuleHost() = default;

	~ModuleHost()
	{
		// best-effort な後始末。ここでのエラーは報告できない (destructor 内)
		// が、temp file は %TEMP% にあるので OS がいずれ回収する。
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

	/// @brief DLL を load する。
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

		// Unicode-safe な path のため `LoadLibraryW`。temp filename は ASCII
		// だが %TEMP% は非 ASCII 文字を含みうる。
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

		// 成功宣言の前に entry symbol の存在を確認。caller の「GetProcAddress
		// が null を返したか?」という別チェックを省ける。
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

	/// @brief DLL を unload する。複数回呼んでも安全。
	/// @details FreeLibrary + temp file 削除。エラーは握り潰す (best-effort)。
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

	/// @brief load entry symbol を解決する。未 load なら nullptr。
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

	/// @brief unload entry symbol を解決する。未 load または不在なら nullptr。
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

	/// @brief write-blame symbol を解決する (optional、`mitiru why` opt-in game のみ)。不在なら nullptr。
	[[nodiscard]] ModuleWhyBlameFn whyBlameAtFn() const noexcept
	{
#if defined(_WIN32)
		if (m_handle == nullptr) { return nullptr; }
		auto* p = ::GetProcAddress(m_handle, kWhyBlameSymbol);
		return reinterpret_cast<ModuleWhyBlameFn>(p);
#else
		return nullptr;
#endif
	}

	/// @brief 巻き戻しバッファ長 symbol を解決する (optional、MITIRU_REWIND_BUFFER 宣言時のみ)。不在なら nullptr。
	[[nodiscard]] ModuleRewindBufferFn rewindBufferFramesFn() const noexcept
	{
#if defined(_WIN32)
		if (m_handle == nullptr) { return nullptr; }
		auto* p = ::GetProcAddress(m_handle, kRewindBufferSymbol);
		return reinterpret_cast<ModuleRewindBufferFn>(p);
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

	/// @brief loader (Engine) 側で判定した失敗理由を記録する。
	/// @details reload の「先ロード・後差し替え」では一時 host で新 DLL を検証する。
	///          その load 失敗 / ABI version 拒否の理由を、caller が参照する正規の
	///          置き場 (= 現役 host の lastError) へ引き継ぐために使う。
	void setLastError(std::string message)
	{
		m_lastError = std::move(message);
	}

private:
	/// @brief 一意な temp path を作る: %TEMP%/mitiru_module_<pid>_<seq>.dll
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
			// %TEMP% が使えない場合は source 相対に fall back。
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
