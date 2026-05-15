#pragma once

/// @file ShaderLoader.hpp
/// @brief External shader file loader with constexpr fallback
/// @details Dual-mode shader loading: external files for development (hot-reload),
///          constexpr fallback for distribution. When external shader files exist
///          under the shader directory, they are loaded at runtime. Otherwise,
///          the embedded constexpr strings from the header files are used.

#include <string>
#include <string_view>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <optional>
#include <unordered_map>

namespace mitiru::render
{

/// @brief Loads shader source from external files with constexpr fallback
/// @details Usage:
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
    /// @brief Set the base directory for shader files
    /// @param dir Base directory path (e.g., "assets/shaders")
    static void setShaderDirectory(const std::string& dir)
    {
        shaderDir() = dir;
    }

    /// @brief Get the current shader directory
    /// @return Current shader directory path
    [[nodiscard]] static const std::string& getShaderDirectory()
    {
        return shaderDir();
    }

    /// @brief Try to load a shader from an external file
    /// @param relativePath Path relative to shader directory (e.g., "hlsl/default_2d_vs.hlsl")
    /// @return Shader source string, or nullopt if file not found
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

    /// @brief Load shader: try external file first, fall back to embedded constexpr string
    /// @param relativePath Path relative to shader directory
    /// @param fallback Embedded constexpr shader string to use if file not found
    /// @return Shader source string (from file or fallback)
    [[nodiscard]] static std::string load(
        const std::string& relativePath,
        std::string_view fallback)
    {
        auto external = loadFromFile(relativePath);
        return external.has_value()
            ? std::move(*external)
            : std::string(fallback);
    }

    /// @brief Clear cached shaders (call before hot-reload)
    static void clearCache()
    {
        cache().clear();
    }

    /// @brief Load with caching (for repeated loads in the same frame)
    /// @param relativePath Path relative to shader directory
    /// @param fallback Embedded constexpr shader string to use if file not found
    /// @return Reference to cached shader source string
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
