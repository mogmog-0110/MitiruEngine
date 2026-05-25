#pragma once

/// @file ShaderLoader.hpp
/// @brief constexpr fallback 付きの外部 shader ファイル loader
/// @details 二重モードの shader 読み込み: 開発時は外部ファイル (hot-reload)、
///          配布時は constexpr fallback。shader ディレクトリ下に外部 shader
///          ファイルが存在すれば runtime で読み込む。無ければヘッダに埋め込まれた
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
///   // Set shader directory (once at startup)
///   ShaderLoader::setShaderDirectory("assets/shaders");
///
///   // Load with fallback to embedded string
///   std::string vs = ShaderLoader::load("hlsl/default_2d_vs.hlsl", DEFAULT_VS_2D);
///
///   // Or try external only
///   auto external = ShaderLoader::loadFromFile("hlsl/phong_ps.hlsl");
///   if (external) { /* use hot-reloaded shader */ }
///
///   // Cached loading for repeated access in the same frame
///   const auto& cached = ShaderLoader::loadCached("hlsl/toon_ps.hlsl", TOON_PS_3D);
///
///   // Clear cache when hot-reloading
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

    /// @brief shader を読み込む: まず外部ファイル、無ければ埋め込み constexpr 文字列へ fallback
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
