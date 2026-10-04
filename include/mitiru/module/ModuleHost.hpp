#pragma once

/// @file ModuleHost.hpp
/// @brief Game DLL を host process に load する RAII wrapper (v0.2.0 step 2)
/// @details
/// Windows: LoadLibrary / GetProcAddress / FreeLibrary を OS handle と共に
/// 寿命管理する。macOS/Linux: dlopen / dlsym / dlclose の POSIX 相当 (L1)。
/// Metal 版 MitiruEngine が実際に動く実績があるため hot reload だけ Windows 専用の
/// ままにする理由が無かった。両 platform とも reload-safe copy 戦略は共通 (下記)。
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
#include <fstream>
#include <iterator>
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
#elif defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#include <unistd.h>
#endif

#include <mitiru/module/Invariant.hpp>
#include <mitiru/module/detail/PdbPath.hpp>
#include <mitiru/module/ModuleApi.hpp>
#include <mitiru/module/ModuleReflection.hpp>

namespace mitiru::module
{

/// @brief 巻き戻しリング予算バイト数を返す関数名 (optional。`MITIRU_REWIND_BUDGET` で export)。
/// @details `kRewindBufferSymbol` (フレーム数) と対の byte 版。`ModuleApi.hpp` の ABI は
///          変えず別 export として host→DLL pull する (host が起動時に GetProcAddress で解決)。
///          不在なら `--rewind-mb` 明示指定 > 既定 512MB (`Engine_Module_Loader.hpp` 参照)。
constexpr const char* kRewindBudgetSymbol = "mitiru_module_rewind_budget_bytes";

/// @brief 巻き戻しリング予算バイト数を返す関数のシグネチャ (optional、`MITIRU_REWIND_BUDGET` 用)。
using ModuleRewindBudgetFn = std::uint64_t (*)();

/// @brief pause 中も dt を通す layer の mask を返す関数名 (optional。`MITIRU_PAUSE_ALWAYS_LAYERS`
///        で export、2-1)。他の rewind 系 export と同じく host が起動時に GetProcAddress で解決する。
///        不在なら mask=0 (既定: pause は全 layer 共通、`Engine_Module_Loader.hpp` 参照)。
constexpr const char* kPauseAlwaysLayersSymbol = "mitiru_module_pause_always_layers_mask";

/// @brief pause 中も dt を通す layer の mask を返す関数のシグネチャ (optional、`MITIRU_PAUSE_ALWAYS_LAYERS` 用)。
using ModulePauseAlwaysLayersFn = std::uint8_t (*)();

/// @brief pause の種類 (1..3) ごとの layer mask を返す関数名 (optional。`MITIRU_PAUSE_LAYERS_BY_KIND` で export)。
constexpr const char* kPauseLayersByKindSymbol = "mitiru_module_pause_layers_by_kind";
using ModulePauseLayersByKindFn = std::uint8_t (*)(std::uint8_t);

/// @brief 型の台帳 (`SpawnerTypeEntry` の配列) を書き出す関数名 (optional。`MITIRU_SPAWNER_TYPES_EXPORT()` で export)。
///        引数は (out, cap)、戻り値は書いた件数。型は Spawner.hpp 側 (ここでは void* で受けて Engine が読む)。
constexpr const char* kSpawnerTypesSymbol = "mitiru_module_spawner_types";
using ModuleSpawnerTypesFn = int (*)(void* out, int cap);

/// @brief 不変条件記述子の表を返す関数名 (optional。`MITIRU_INVARIANT` + `MITIRU_INVARIANTS_EXPORT`
///        で export、N1)。他の rewind 系 export と同じく host が起動時に GetProcAddress で解決する。
constexpr const char* kInvariantsSymbol = "mitiru_module_invariants";

/// @brief 不変条件記述子の表を返す関数のシグネチャ (optional、`MITIRU_INVARIANT` 用)。
using ModuleInvariantsFn = const InvariantDescriptor* (*)(std::int32_t*);

/// @brief MITIRU_REACHABLE の到達状態を全件リセットする関数名 (optional。
///        `MITIRU_INVARIANTS_EXPORT` が MITIRU_REACHABLE 宣言の有無に関わらず併せて export する)。
constexpr const char* kInvariantsResetSymbol = "mitiru_module_invariants_reset";

/// @brief 不変条件到達状態リセット関数のシグネチャ (optional)。
using ModuleInvariantsResetFn = void (*)();

/// @brief 配置 JSON を POD へ焼く関数名 (optional。`MITIRU_BAKE_ASSETS` (module/Bake.hpp) で
///        export、★4-1)。他の export と同じく host が起動時に GetProcAddress で解決する。
constexpr const char* kBakeAssetsSymbol = "mitiru_module_bake_assets";

/// @brief 配置 JSON (jsonPath) を .baked (outPath) へ焼く関数のシグネチャ (optional)。成功で true。
using ModuleBakeAssetsFn = bool (*)(const char*, const char*);

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
		, m_runtimePdbPath(std::move(other.m_runtimePdbPath))
		, m_lastError(std::move(other.m_lastError))
		, m_handle(other.m_handle)
		, m_lastLoadBusy(other.m_lastLoadBusy)
	{
		other.m_handle = nullptr;
	}

	ModuleHost& operator=(ModuleHost&& other) noexcept
	{
		if (this != &other)
		{
			unload();
			m_sourcePath     = std::move(other.m_sourcePath);
			m_runtimePath    = std::move(other.m_runtimePath);
			m_runtimePdbPath = std::move(other.m_runtimePdbPath);
			m_lastError      = std::move(other.m_lastError);
			m_handle         = other.m_handle;
			m_lastLoadBusy   = other.m_lastLoadBusy;
			other.m_handle   = nullptr;
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

#if !defined(_WIN32) && !defined(__unix__) && !defined(__APPLE__)
		(void)source;
		m_lastError = "ModuleHost is not implemented on this platform";
		return false;
#else
		m_lastLoadBusy = false;
		std::error_code ec;
		if (!std::filesystem::exists(source, ec) || ec)
		{
			m_lastError = "source DLL not found: " + source.string();
			return false;
		}

		// 写している間にリンカが書き始めないよう、書き込みを締め出して開いたまま写す。
		const WriterLock lock(source);
		if (lock.busy())
		{
			m_lastLoadBusy = true;
			m_lastError = "DLL はまだ書き込み中 (リンク中): " + source.string();
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
		std::filesystem::path runtimePdb = redirectPdb(runtimePath);

#if defined(_WIN32)
		// Unicode-safe な path のため `LoadLibraryW`。temp filename は ASCII
		// だが %TEMP% は非 ASCII 文字を含みうる。
		void* handle = ::LoadLibraryW(runtimePath.wstring().c_str());
		if (handle == nullptr)
		{
			const DWORD err = ::GetLastError();
			std::error_code rmEc;
			std::filesystem::remove(runtimePath, rmEc);
			if (!runtimePdb.empty()) { std::filesystem::remove(runtimePdb, rmEc); }
			m_lastError = "LoadLibrary failed (GetLastError=" +
			              std::to_string(err) + ")";
			return false;
		}
#else
		// dlopen は既定で symbol を process 全体に公開しない (RTLD_LOCAL) が、
		// Windows の LoadLibrary もモジュール単位の名前解決なので挙動を揃えられる。
		void* handle = ::dlopen(runtimePath.c_str(), RTLD_NOW | RTLD_LOCAL);
		if (handle == nullptr)
		{
			std::error_code rmEc;
			std::filesystem::remove(runtimePath, rmEc);
			const char* dlErr = ::dlerror();
			m_lastError = std::string("dlopen failed: ") + (dlErr ? dlErr : "unknown");
			return false;
		}
#endif

		// 成功宣言の前に entry symbol の存在を確認。caller の「symbol 解決が
		// null を返したか?」という別チェックを省ける。
		void* loadFnPtr = resolveSymbol(handle, kLoadSymbol);
		if (loadFnPtr == nullptr)
		{
			closeHandle(handle);
			std::error_code rmEc;
			std::filesystem::remove(runtimePath, rmEc);
			m_lastError = "module is missing required export: " +
			              std::string{kLoadSymbol};
			return false;
		}

		m_sourcePath     = std::move(source);
		m_runtimePath    = std::move(runtimePath);
		m_runtimePdbPath = std::move(runtimePdb);
		m_handle         = handle;
		m_lastError.clear();
		return true;
#endif
	}

	/// @brief DLL を unload する。複数回呼んでも安全。
	/// @details FreeLibrary + temp file 削除。エラーは無視する (best-effort)。
	void unload() noexcept
	{
		if (m_handle != nullptr)
		{
			closeHandle(m_handle);
			m_handle = nullptr;
		}
		if (!m_runtimePath.empty())
		{
			std::error_code ec;
			std::filesystem::remove(m_runtimePath, ec);
			m_runtimePath.clear();
		}
		if (!m_runtimePdbPath.empty())
		{
			// デバッガが開いたままなら消せない。%TEMP% に残るだけで次の load の邪魔はしない。
			std::error_code ec;
			std::filesystem::remove(m_runtimePdbPath, ec);
			m_runtimePdbPath.clear();
		}
		m_sourcePath.clear();
	}

	[[nodiscard]] bool isLoaded() const noexcept
	{
		return m_handle != nullptr;
	}

	/// @brief 読み込んだ module の先頭 (Windows では HMODULE と同じ値)。未 load なら nullptr。
	[[nodiscard]] const void* moduleBase() const noexcept { return m_handle; }

	/// @brief load entry symbol を解決する。未 load なら nullptr。
	[[nodiscard]] ModuleLoadFn loadFn() const noexcept
	{
		return reinterpret_cast<ModuleLoadFn>(resolveSymbol(m_handle, kLoadSymbol));
	}

	/// @brief unload entry symbol を解決する。未 load または不在なら nullptr。
	[[nodiscard]] ModuleUnloadFn unloadFn() const noexcept
	{
		return reinterpret_cast<ModuleUnloadFn>(resolveSymbol(m_handle, kUnloadSymbol));
	}

	/// @brief write-blame symbol を解決する (optional、`mitiru why` opt-in game のみ)。不在なら nullptr。
	[[nodiscard]] ModuleWhyBlameFn whyBlameAtFn() const noexcept
	{
		return reinterpret_cast<ModuleWhyBlameFn>(resolveSymbol(m_handle, kWhyBlameSymbol));
	}

	/// @brief everWrote symbol を解決する (optional、`mitiru_why_blame_at` に追加 opt-in する game のみ)。不在なら nullptr。
	[[nodiscard]] ModuleWhyEverWroteFn whyEverWroteAtFn() const noexcept
	{
		return reinterpret_cast<ModuleWhyEverWroteFn>(resolveSymbol(m_handle, kEverWroteSymbol));
	}

	/// @brief 巻き戻しバッファ長 symbol を解決する (optional、MITIRU_REWIND_BUFFER 宣言時のみ)。不在なら nullptr。
	[[nodiscard]] ModuleRewindBufferFn rewindBufferFramesFn() const noexcept
	{
		return reinterpret_cast<ModuleRewindBufferFn>(resolveSymbol(m_handle, kRewindBufferSymbol));
	}

	/// @brief 巻き戻しリング予算バイト数 symbol を解決する (optional、MITIRU_REWIND_BUDGET 宣言時のみ)。不在なら nullptr。
	[[nodiscard]] ModuleRewindBudgetFn rewindBudgetBytesFn() const noexcept
	{
		return reinterpret_cast<ModuleRewindBudgetFn>(resolveSymbol(m_handle, kRewindBudgetSymbol));
	}

	/// @brief pause 中も dt を通す layer mask symbol を解決する (optional、MITIRU_PAUSE_ALWAYS_LAYERS 宣言時のみ)。不在なら nullptr。
	/// @brief GameMemory の外に持つ状態の窓口の表 (ABI v47、`MITIRU_SIDE_STATE`)。不在なら nullptr
	[[nodiscard]] ModuleSideStatesFn sideStatesFn() const noexcept
	{
		return reinterpret_cast<ModuleSideStatesFn>(resolveSymbol(m_handle, kSideStatesSymbol));
	}

	/// @brief アクションの表 (ABI v48、`MITIRU_ACTIONS`)。不在なら nullptr
	[[nodiscard]] ModuleActionManifestFn actionManifestFn() const noexcept
	{
		return reinterpret_cast<ModuleActionManifestFn>(resolveSymbol(m_handle, kActionManifestSymbol));
	}

	/// @brief 形の変わったセーブを移す関数 (ABI v48、`MITIRU_MIGRATE`)。不在なら nullptr
	[[nodiscard]] ModuleMigrateFn migrateFn() const noexcept
	{
		return reinterpret_cast<ModuleMigrateFn>(resolveSymbol(m_handle, kMigrateSymbol));
	}

	/// @brief ツール窓に見せる資産の一覧 (ABI v49、`MITIRU_INSPECT_ASSETS`)。不在なら nullptr
	[[nodiscard]] ModuleInspectAssetsFn inspectAssetsFn() const noexcept
	{
		return reinterpret_cast<ModuleInspectAssetsFn>(resolveSymbol(m_handle, kInspectAssetsSymbol));
	}

	/// @brief host 権威の参加者が自分の分を先に進める関数 (ABI v51、`MITIRU_NET_PREDICT`)。不在なら nullptr
	[[nodiscard]] ModuleNetPredictFn netPredictFn() const noexcept
	{
		return reinterpret_cast<ModuleNetPredictFn>(resolveSymbol(m_handle, kNetPredictSymbol));
	}

	[[nodiscard]] ModulePauseAlwaysLayersFn pauseAlwaysLayersMaskFn() const noexcept
	{
		return reinterpret_cast<ModulePauseAlwaysLayersFn>(resolveSymbol(m_handle, kPauseAlwaysLayersSymbol));
	}

	/// @brief 型の台帳 symbol を解決する (optional、MITIRU_SPAWNER_TYPES_EXPORT 宣言時のみ)。不在なら nullptr。
	[[nodiscard]] ModuleSpawnerTypesFn spawnerTypesFn() const noexcept
	{
		return reinterpret_cast<ModuleSpawnerTypesFn>(resolveSymbol(m_handle, kSpawnerTypesSymbol));
	}

	/// @brief pause の種類別 layer mask symbol を解決する (optional、MITIRU_PAUSE_LAYERS_BY_KIND 宣言時のみ)。不在なら nullptr。
	[[nodiscard]] ModulePauseLayersByKindFn pauseLayersByKindFn() const noexcept
	{
		return reinterpret_cast<ModulePauseLayersByKindFn>(resolveSymbol(m_handle, kPauseLayersByKindSymbol));
	}

	/// @brief 不変条件記述子表 symbol を解決する (optional、MITIRU_INVARIANT 宣言時のみ)。不在なら nullptr。
	[[nodiscard]] ModuleInvariantsFn invariantsFn() const noexcept
	{
		return reinterpret_cast<ModuleInvariantsFn>(resolveSymbol(m_handle, kInvariantsSymbol));
	}

	/// @brief 不変条件到達状態リセット symbol を解決する (optional、MITIRU_REACHABLE 宣言時のみ)。不在なら nullptr。
	[[nodiscard]] ModuleInvariantsResetFn invariantsResetFn() const noexcept
	{
		return reinterpret_cast<ModuleInvariantsResetFn>(resolveSymbol(m_handle, kInvariantsResetSymbol));
	}

	/// @brief 配置 JSON を焼く symbol を解決する (optional、MITIRU_BAKE_ASSETS 宣言時のみ)。不在なら nullptr。
	[[nodiscard]] ModuleBakeAssetsFn bakeAssetsFn() const noexcept
	{
		return reinterpret_cast<ModuleBakeAssetsFn>(resolveSymbol(m_handle, kBakeAssetsSymbol));
	}

	/// @brief load 済み DLL の反射の export を引く。宣言していない game は全部 nullptr。
	[[nodiscard]] ReflectionExports reflectionExports() const noexcept
	{
		ReflectionExports e;
		e.layoutHash = reinterpret_cast<ModuleLayoutHashFn>(resolveSymbol(m_handle, kLayoutHashSymbol));
		e.fields     = reinterpret_cast<ModuleReflectFieldsFn>(resolveSymbol(m_handle, kReflectFieldsSymbol));
		e.schemas    = reinterpret_cast<ModuleReflectSchemasFn>(resolveSymbol(m_handle, kReflectSchemasSymbol));
		e.floatOffsets = reinterpret_cast<ModuleFloatOffsetsFn>(resolveSymbol(m_handle, kFloatOffsetsSymbol));
		e.paddingBytes = reinterpret_cast<ModulePaddingBytesFn>(resolveSymbol(m_handle, kPaddingBytesSymbol));
		return e;
	}

	/// @brief load 済み DLL から GameMemory の記述を集める (mitiru_module_load の後に呼ぶ)。
	[[nodiscard]] ModuleReflection captureReflection() const
	{
		return ModuleReflection::fromExports(reflectionExports());
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

	/// @brief 写した DLL が指す PDB の写し。PDB が無い / 書き換えられなかったなら空。
	[[nodiscard]] const std::filesystem::path& runtimePdbPath() const noexcept
	{
		return m_runtimePdbPath;
	}

	/// @brief 最後の load() が「元の DLL がまだ書き込み中」で断ったか。watch はこの間は待てばよい。
	[[nodiscard]] bool lastLoadBusy() const noexcept { return m_lastLoadBusy; }

	/// @brief 最後の load() 失敗の原因。成功 / 未呼び出し時は空。
	[[nodiscard]] const std::string& lastError() const noexcept
	{
		return m_lastError;
	}

	/// @brief loader (Engine) 側で判定した失敗理由を記録する。
	/// @details reload の「先ロード・後差し替え」では一時 host で新 DLL を検証する。
	///          その load 失敗 / ABI version 拒否の理由を、caller が参照する正規の
	///          置き場 (= 現役 host の lastError) へ引き継ぐために使う。
	void setLastError(std::string message, bool busy = false)
	{
		m_lastError    = std::move(message);
		m_lastLoadBusy = busy;
	}

private:
	/// @brief 共有ライブラリの symbol を解決する (GetProcAddress / dlsym の橋渡し)。
	[[nodiscard]] static void* resolveSymbol(void* handle, const char* name) noexcept
	{
		if (handle == nullptr) { return nullptr; }
#if defined(_WIN32)
		return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(handle), name));
#else
		return ::dlsym(handle, name);
#endif
	}

	/// @brief 共有ライブラリを閉じる (FreeLibrary / dlclose の橋渡し)。
	static void closeHandle(void* handle) noexcept
	{
#if defined(_WIN32)
		::FreeLibrary(static_cast<HMODULE>(handle));
#else
		::dlclose(handle);
#endif
	}

	/// @brief 一意な temp path を作る: %TEMP%/mitiru_module_<pid>_<seq><拡張子>
	/// @details 拡張子は Windows=.dll、macOS=.dylib、それ以外の Unix=.so。
	static std::filesystem::path makeUniqueTempPath()
	{
		static std::uint64_t s_seq = 0;
		++s_seq;
#if defined(_WIN32)
		const auto pid = static_cast<std::uint64_t>(::GetCurrentProcessId());
		constexpr const char* kExt = ".dll";
#elif defined(__APPLE__)
		const auto pid = static_cast<std::uint64_t>(::getpid());
		constexpr const char* kExt = ".dylib";
#else
		const auto pid = static_cast<std::uint64_t>(::getpid());
		constexpr const char* kExt = ".so";
#endif
		std::string filename = "mitiru_module_" + std::to_string(pid) +
		                       "_" + std::to_string(s_seq) + kExt;
		std::error_code ec;
		auto tmp = std::filesystem::temp_directory_path(ec);
		if (ec)
		{
			// %TEMP% が使えない場合は source 相対にフォールバックする。
			tmp = ".";
		}
		return tmp / filename;
	}

	/// @brief 元の DLL を「読み取りは許し、書き込みは締め出す」形で開いておく。リンカが書き込み用に
	///        開いていると開けない (busy)。POSIX には同じ締め出しが無いので常に通す。
	class WriterLock
	{
	public:
		explicit WriterLock(const std::filesystem::path& path)
		{
#if defined(_WIN32)
			m_handle = ::CreateFileW(path.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
			                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			m_busy = (m_handle == INVALID_HANDLE_VALUE) && (::GetLastError() == ERROR_SHARING_VIOLATION);
#else
			(void)path;
#endif
		}
		~WriterLock()
		{
#if defined(_WIN32)
			if (m_handle != INVALID_HANDLE_VALUE) { ::CloseHandle(m_handle); }
#endif
		}
		WriterLock(const WriterLock&) = delete;
		WriterLock& operator=(const WriterLock&) = delete;
		[[nodiscard]] bool busy() const noexcept { return m_busy; }

	private:
#if defined(_WIN32)
		HANDLE m_handle = INVALID_HANDLE_VALUE;
#endif
		bool m_busy = false;
	};

	/// @brief 写した DLL の PDB パスを、PDB の写し (DLL の写しと同じ名前の .pdb) へ向け直す。
	/// @return 写した PDB のパス。PDB が無い・パスが収まらない・PE でないなら空 (DLL はそのまま)。
	static std::filesystem::path redirectPdb(const std::filesystem::path& runtimeDll)
	{
		std::vector<std::uint8_t> image;
		{
			std::ifstream in(runtimeDll, std::ios::binary);
			if (!in) { return {}; }
			image.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		}
		const auto span = detail::findPdbPath(image);
		if (!span) { return {}; }
		const std::filesystem::path original = detail::readPdbPath(image, *span);
		std::error_code ec;
		if (original.empty() || !std::filesystem::exists(original, ec)) { return {}; }

		std::filesystem::path copy = runtimeDll;
		copy.replace_extension(".pdb");
		const std::string full = copy.string();
		const std::string name = copy.filename().string();
		// 収まらなければファイル名だけにする (デバッガは DLL と同じフォルダも探す)。
		if (!detail::writePdbPath(image, *span, full) && !detail::writePdbPath(image, *span, name)) { return {}; }
		std::filesystem::copy_file(original, copy, std::filesystem::copy_options::overwrite_existing, ec);
		if (ec) { return {}; }
		std::ofstream out(runtimeDll, std::ios::binary | std::ios::trunc);
		out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
		if (!out) { std::filesystem::remove(copy, ec); return {}; }
		return copy;
	}

	std::filesystem::path m_sourcePath;
	std::filesystem::path m_runtimePath;
	std::filesystem::path m_runtimePdbPath;
	std::string           m_lastError;
	void*                 m_handle{nullptr};
	bool                  m_lastLoadBusy{false};
};

}  // namespace mitiru::module
