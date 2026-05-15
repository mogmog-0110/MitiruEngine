#pragma once

/// @file GamePlugin.hpp
/// @brief ゲームDLLプラグインローダー
/// @details ゲームクラスをDLLとしてビルドし、MitiruHubから動的にロードするための
///          プラグインシステム。ホットリロード検出機能を含む。
///
/// DLLは以下の関数をエクスポートする必要がある:
/// - extern "C" Game* createGame();
/// - extern "C" void destroyGame(Game* game);
/// - extern "C" const char* getGameName();
///
/// @code
/// mitiru::GamePlugin plugin;
/// if (plugin.load("MyGame.dll")) {
///     auto* game = plugin.createGame();
///     engine.run(*game);
///     plugin.destroyGame(game);
/// }
/// @endcode

#include <cstdint>
#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <sys/stat.h>

#include <mitiru/core/Game.hpp>

namespace mitiru
{

/// @brief ゲームDLLプラグインローダー
/// @details DLLからゲームインスタンスを生成・破棄する。
///          ファイル変更検出によるホットリロードをサポートする。
class GamePlugin
{
public:
	/// @brief デフォルトコンストラクタ
	GamePlugin() = default;

	/// @brief デストラクタ（ロード済みDLLをアンロードする）
	~GamePlugin()
	{
		unload();
	}

	/// @brief コピー禁止
	GamePlugin(const GamePlugin&) = delete;
	GamePlugin& operator=(const GamePlugin&) = delete;

	/// @brief ムーブコンストラクタ
	GamePlugin(GamePlugin&& other) noexcept
		: m_handle{other.m_handle}
		, m_dllPath{std::move(other.m_dllPath)}
		, m_loadedCopyPath{std::move(other.m_loadedCopyPath)}
		, m_loadTime{other.m_loadTime}
		, m_createFn{other.m_createFn}
		, m_destroyFn{other.m_destroyFn}
		, m_nameFn{other.m_nameFn}
	{
		other.m_handle = nullptr;
		other.m_createFn = nullptr;
		other.m_destroyFn = nullptr;
		other.m_nameFn = nullptr;
		other.m_loadTime = 0;
	}

	/// @brief ムーブ代入演算子
	GamePlugin& operator=(GamePlugin&& other) noexcept
	{
		if (this != &other)
		{
			unload();
			m_handle = other.m_handle;
			m_dllPath = std::move(other.m_dllPath);
			m_loadedCopyPath = std::move(other.m_loadedCopyPath);
			m_loadTime = other.m_loadTime;
			m_createFn = other.m_createFn;
			m_destroyFn = other.m_destroyFn;
			m_nameFn = other.m_nameFn;
			other.m_handle = nullptr;
			other.m_createFn = nullptr;
			other.m_destroyFn = nullptr;
			other.m_nameFn = nullptr;
			other.m_loadTime = 0;
		}
		return *this;
	}

	/// @brief ゲームDLLをロードする
	/// @param path DLLファイルパス
	/// @return ロード成功時 true
	/// @note Win32ではDLLをコピーしてからロードし、元ファイルのビルドロックを回避する
	bool load(const std::string& path)
	{
		unload();

		m_dllPath = path;
		m_loadTime = queryFileTime(path);

#ifdef _WIN32
		// DLLをコピーしてからロード（ビルド中のファイルロック回避）
		namespace fs = std::filesystem;
		const fs::path srcPath(path);
		m_loadedCopyPath = (srcPath.parent_path()
			/ (srcPath.stem().string() + "_loaded.dll")).string();

		std::error_code ec;
		fs::copy_file(path, m_loadedCopyPath,
			fs::copy_options::overwrite_existing, ec);
		if (ec)
		{
			m_loadedCopyPath.clear();
			return false;
		}

		m_handle = static_cast<void*>(
			::LoadLibraryA(m_loadedCopyPath.c_str()));
#else
		m_handle = ::dlopen(path.c_str(), RTLD_NOW);
#endif

		if (!m_handle)
		{
			return false;
		}

		m_createFn = reinterpret_cast<CreateGameFn>(getSymbol("createGame"));
		m_destroyFn = reinterpret_cast<DestroyGameFn>(getSymbol("destroyGame"));
		m_nameFn = reinterpret_cast<GetGameNameFn>(getSymbol("getGameName"));

		if (!m_createFn || !m_destroyFn || !m_nameFn)
		{
			unload();
			return false;
		}

		return true;
	}

	/// @brief ロード済みDLLをアンロードする
	void unload()
	{
		if (m_handle)
		{
#ifdef _WIN32
			::FreeLibrary(static_cast<HMODULE>(m_handle));
#else
			::dlclose(m_handle);
#endif
			m_handle = nullptr;
		}

		m_createFn = nullptr;
		m_destroyFn = nullptr;
		m_nameFn = nullptr;
		m_loadTime = 0;

#ifdef _WIN32
		// コピーしたDLLファイルを削除
		if (!m_loadedCopyPath.empty())
		{
			std::error_code ec;
			std::filesystem::remove(m_loadedCopyPath, ec);
			m_loadedCopyPath.clear();
		}
#endif
	}

	/// @brief DLLをリロードする（アンロード→ロード）
	/// @return リロード成功時 true
	/// @note リロード前にすべてのゲームインスタンスを破棄すること
	bool reload()
	{
		const auto path = m_dllPath;
		unload();
		return load(path);
	}

	/// @brief DLLからゲームインスタンスを生成する
	/// @return ゲームインスタンスへのポインタ（失敗時 nullptr）
	/// @note 返されたポインタは destroyGame() で破棄すること
	[[nodiscard]] Game* createGame()
	{
		if (!m_createFn)
		{
			return nullptr;
		}
		return m_createFn();
	}

	/// @brief DLLのデストロイ関数でゲームインスタンスを破棄する
	/// @param game createGame() で取得したポインタ
	void destroyGame(Game* game)
	{
		if (m_destroyFn && game)
		{
			m_destroyFn(game);
		}
	}

	/// @brief DLLからゲーム名を取得する
	/// @return ゲーム名（未ロード時は空文字列）
	[[nodiscard]] std::string gameName() const
	{
		if (!m_nameFn)
		{
			return {};
		}
		return std::string(m_nameFn());
	}

	/// @brief DLLがロード済みか
	[[nodiscard]] bool isLoaded() const noexcept
	{
		return m_handle != nullptr;
	}

	/// @brief DLLファイルパスを取得する
	[[nodiscard]] const std::string& dllPath() const noexcept
	{
		return m_dllPath;
	}

	/// @brief DLLファイルの最終更新時刻を取得する
	/// @return エポックからの秒数（取得失敗時は0）
	[[nodiscard]] std::uint64_t lastModified() const
	{
		return queryFileTime(m_dllPath);
	}

	/// @brief DLLがロード後に変更されたか（ホットリロード検出用）
	/// @return ファイルが変更されている場合 true
	[[nodiscard]] bool isModified() const
	{
		if (m_loadTime == 0)
		{
			return false;
		}
		return lastModified() != m_loadTime;
	}

private:
	/// @brief DLLファクトリ関数型
	using CreateGameFn = Game* (*)();

	/// @brief DLLデストロイ関数型
	using DestroyGameFn = void (*)(Game*);

	/// @brief DLLゲーム名取得関数型
	using GetGameNameFn = const char* (*)();

	/// @brief DLLからシンボルを取得する
	/// @param name シンボル名
	/// @return 関数ポインタ（失敗時 nullptr）
	void* getSymbol(const char* name) const
	{
		if (!m_handle)
		{
			return nullptr;
		}

#ifdef _WIN32
		return reinterpret_cast<void*>(
			::GetProcAddress(static_cast<HMODULE>(m_handle), name));
#else
		return ::dlsym(m_handle, name);
#endif
	}

	/// @brief ファイルの最終更新時刻をクエリする
	/// @param path ファイルパス
	/// @return エポックからの秒数（取得失敗時は0）
	static std::uint64_t queryFileTime(const std::string& path)
	{
		if (path.empty())
		{
			return 0;
		}

		struct stat fileStat{};
		if (::stat(path.c_str(), &fileStat) != 0)
		{
			return 0;
		}
		return static_cast<std::uint64_t>(fileStat.st_mtime);
	}

	void* m_handle = nullptr;           ///< DLLハンドル (HMODULE / void*)
	std::string m_dllPath;              ///< DLLファイルパス
	std::string m_loadedCopyPath;       ///< Win32: ロック回避用コピーパス
	std::uint64_t m_loadTime = 0;       ///< ロード時のファイル更新時刻
	CreateGameFn m_createFn = nullptr;  ///< ゲーム生成関数ポインタ
	DestroyGameFn m_destroyFn = nullptr;///< ゲーム破棄関数ポインタ
	GetGameNameFn m_nameFn = nullptr;   ///< ゲーム名取得関数ポインタ
};

} // namespace mitiru
