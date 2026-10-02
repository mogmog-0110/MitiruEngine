#pragma once

/// @file ShaderLoader.hpp
/// @brief constexpr fallback 付きの外部 shader ファイル loader
/// @details shader を 2 つのモードで読み込む。開発時は外部ファイル (hot-reload)、
///          配布時は constexpr fallback。shader ディレクトリ下に外部 shader
///          ファイルがあれば runtime で読み込む。無ければヘッダに埋め込まれた
///          constexpr 文字列を使う。

#include <string>
#include <string_view>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <optional>
#include <unordered_map>

namespace mitiru::render
{

/// @brief constexpr fallback 付きで外部ファイルから shader source を読み込む
/// @details 使い方:
/// @code
///   // shader ディレクトリを設定する (起動時に 1 回)
///   ShaderLoader::setShaderDirectory("assets/shaders");
///
///   // 埋め込み文字列への fallback 付きで読み込む
///   std::string vs = ShaderLoader::load("hlsl/default_2d_vs.hlsl", DEFAULT_VS_2D);
///
///   // または外部ファイルだけを試す
///   auto external = ShaderLoader::loadFromFile("hlsl/phong_ps.hlsl");
///   if (external) { /* hot-reload した shader を使う */ }
///
///   // 同一フレーム内で繰り返し読む場合はキャッシュ付きで読み込む
///   const auto& cached = ShaderLoader::loadCached("hlsl/toon_ps.hlsl", TOON_PS_3D);
///
///   // hot-reload 時はキャッシュをクリアする
///   ShaderLoader::clearCache();
/// @endcode
class ShaderLoader
{
public:
    /// @brief shader ファイルの基準ディレクトリを設定する
    /// @param dir 基準ディレクトリパス (例: "assets/shaders")
    static void setShaderDirectory(const std::string& dir)
    {
        shaderDir() = dir;
    }

    /// @brief 現在の shader ディレクトリを取得する
    /// @return 現在の shader ディレクトリパス
    [[nodiscard]] static const std::string& getShaderDirectory()
    {
        return shaderDir();
    }

    /// @brief 外部ファイルから shader の読み込みを試みる
    /// @param relativePath shader ディレクトリからの相対パス (例: "hlsl/default_2d_vs.hlsl")
    /// @return shader source 文字列。ファイルが見つからなければ nullopt
    [[nodiscard]] static std::optional<std::string> loadFromFile(
        const std::string& relativePath)
    {
        const auto fullPath = shaderDir() + "/" + relativePath;

        if (!std::filesystem::exists(fullPath))
        {
            return std::nullopt;
        }

        std::ifstream file(fullPath);
        if (!file.is_open())
        {
            return std::nullopt;
        }

        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    /// @brief shader を読み込む。まず外部ファイルを試し、無ければ埋め込み constexpr 文字列へ fallback する
    /// @param relativePath shader ディレクトリからの相対パス
    /// @param fallback ファイルが見つからない場合に使う埋め込み constexpr shader 文字列
    /// @return shader source 文字列 (ファイル由来または fallback)
    [[nodiscard]] static std::string load(
        const std::string& relativePath,
        std::string_view fallback)
    {
        auto external = loadFromFile(relativePath);
        return external.has_value()
            ? std::move(*external)
            : std::string(fallback);
    }

    /// @brief キャッシュ済み shader をクリアする (hot-reload の前に呼ぶ)
    static void clearCache()
    {
        cache().clear();
    }

    /// @brief キャッシュ付きで読み込む (同一フレーム内で繰り返し読む用)
    /// @param relativePath shader ディレクトリからの相対パス
    /// @param fallback ファイルが見つからない場合に使う埋め込み constexpr shader 文字列
    /// @return キャッシュされた shader source 文字列への参照
    [[nodiscard]] static const std::string& loadCached(
        const std::string& relativePath,
        std::string_view fallback)
    {
        auto& c = cache();
        auto it = c.find(relativePath);
        if (it != c.end())
        {
            return it->second;
        }

        auto [inserted, success] = c.emplace(
            relativePath,
            load(relativePath, fallback));
        return inserted->second;
    }

private:
    static std::string& shaderDir()
    {
        static std::string dir = "assets/shaders";
        return dir;
    }

    static std::unordered_map<std::string, std::string>& cache()
    {
        static std::unordered_map<std::string, std::string> c;
        return c;
    }
};

} // namespace mitiru::render
