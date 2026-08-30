#pragma once

/// @file AssetPath.hpp
/// @brief アセットパス解決。実行ファイルの場所を基準にパスを解決する

#include <filesystem>
#include <string>

namespace mitiru::resource
{

/// @brief アセットパス解決ユーティリティ
/// @details 相対パスを実行ファイルまたはカレントディレクトリ基準で絶対パスに変換する。
///
/// @code
/// auto path = mitiru::resource::AssetPath::resolve("assets/models/strawberry.obj");
/// // 実行ファイルと同じディレクトリにあれば絶対パスが返る
///
/// mitiru::resource::AssetPath::setBasePath("C:/MyGame/assets");
/// auto tex = mitiru::resource::AssetPath::fromBase("textures/diffuse.png");
/// // -> "C:/MyGame/assets/textures/diffuse.png"
/// @endcode
class AssetPath
{
public:
	/// @brief 実行ファイルのディレクトリを取得する
	/// @return ディレクトリパス文字列（取得できない場合はカレントディレクトリ）
	[[nodiscard]] static std::string executableDir()
	{
#ifdef _WIN32
		// Windows: _pgmptr から実行ファイルのパスを取得する
		{
			char* pgm = nullptr;
			if (_get_pgmptr(&pgm) == 0 && pgm)
			{
				auto p = std::filesystem::path(pgm).parent_path();
				if (!p.empty()) return p.string();
			}
		}
#endif

		// フォールバック: カレントワーキングディレクトリ
		try
		{
			return std::filesystem::current_path().string();
		}
		catch (...)
		{
			return ".";
		}
	}

	/// @brief アセットパスを解決する（相対パスを絶対パスに変換）
	/// @param relativePath アセットの相対パス（例: "assets/models/strawberry.obj"）
	/// @return 解決された絶対パス（見つからない場合は元のパスを返す）
	[[nodiscard]] static std::string resolve(const std::string& relativePath)
	{
		// 絶対パスはそのまま返す
		if (std::filesystem::path(relativePath).is_absolute())
		{
			return relativePath;
		}

		// 実行ファイルディレクトリ基準で探す
		const auto exeDir = executableDir();
		auto exePath = std::filesystem::path(exeDir) / relativePath;
		if (std::filesystem::exists(exePath))
		{
			return exePath.string();
		}

		// カレントディレクトリ基準で探す
		try
		{
			auto cwdPath = std::filesystem::current_path() / relativePath;
			if (std::filesystem::exists(cwdPath))
			{
				return cwdPath.string();
			}
		}
		catch (...)
		{
			// current_path() が失敗した場合は無視
		}

		// 見つからなければ元のパスを返す（呼び出し元がエラー処理する）
		return relativePath;
	}

	/// @brief アセットディレクトリのベースパスを設定する
	/// @param path ベースディレクトリパス
	static void setBasePath(const std::string& path)
	{
		basePath() = path;
	}

	/// @brief 設定されたベースパスからアセットパスを解決する
	/// @param relativePath ベースパスからの相対パス
	/// @return 解決されたパス
	[[nodiscard]] static std::string fromBase(const std::string& relativePath)
	{
		if (basePath().empty())
		{
			return resolve(relativePath);
		}
		auto path = std::filesystem::path(basePath()) / relativePath;
		return path.string();
	}

private:
	[[nodiscard]] static std::string& basePath()
	{
		static std::string s_basePath;
		return s_basePath;
	}
};

} // namespace mitiru::resource
