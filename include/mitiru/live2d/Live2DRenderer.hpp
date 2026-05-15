#pragma once

/// @file Live2DRenderer.hpp
/// @brief OpenGL renderer setup for Live2D Cubism Framework
/// @details Manages CubismFramework lifecycle, view/projection matrices,
///          and texture loading via stb_image.

#ifdef MITIRU_HAS_CUBISM

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <GL/glew.h>
#include <GL/gl.h>

#include <CubismFramework.hpp>
#include <Math/CubismMatrix44.hpp>
#include <Math/CubismViewMatrix.hpp>
#include <Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp>

#include <mitiru/live2d/Live2DAllocator.hpp>

#ifndef STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#endif
#include <stb_image.h>

namespace mitiru::live2d
{

/// @brief Read a file into a malloc'd buffer
/// @param path File path (absolute or relative)
/// @param outSize Output: file size in bytes
/// @return Buffer (caller frees via ReleaseBytes), or nullptr on failure
inline Csm::csmByte* ReadFileBytes(const char* path, Csm::csmSizeInt* outSize)
{
    std::FILE* file = std::fopen(path, "rb");
    if (!file) { *outSize = 0; return nullptr; }

    std::fseek(file, 0, SEEK_END);
    const auto size = static_cast<Csm::csmSizeInt>(std::ftell(file));
    std::fseek(file, 0, SEEK_SET);

    auto* buf = static_cast<Csm::csmByte*>(std::malloc(static_cast<std::size_t>(size)));
    if (!buf) { std::fclose(file); *outSize = 0; return nullptr; }

    std::fread(buf, 1, static_cast<std::size_t>(size), file);
    std::fclose(file);
    *outSize = size;
    return buf;
}

/// @brief File loading utility for Cubism SDK
/// @details Used as CubismFramework LoadFileFunction. Handles both absolute
///          paths (model files) and relative paths (framework shader files).
///          For "FrameworkShaders/..." paths, searches the SDK's shader directory.
inline Csm::csmByte* LoadFileAsBytes(const std::string filePath, Csm::csmSizeInt* outSize)
{
    // Try as-is first (works for absolute paths and correct working directory)
    auto* buf = ReadFileBytes(filePath.c_str(), outSize);
    if (buf) return buf;

    // Fallback: resolve "FrameworkShaders/X" to the SDK's Standard shader dir
    const std::string prefix = "FrameworkShaders/";
    if (filePath.compare(0, prefix.size(), prefix) == 0)
    {
        const std::string shaderName = filePath.substr(prefix.size());
#ifdef MITIRU_SOURCE_DIR
        const std::string sdkShaderDir = std::string(MITIRU_SOURCE_DIR)
            + "/external/cubism/CubismSdkForNative-5-r.4.1/Framework/src/Rendering/OpenGL/Shaders/Standard/";
        const std::string fullPath = sdkShaderDir + shaderName;
        buf = ReadFileBytes(fullPath.c_str(), outSize);
        if (buf) return buf;
#endif
    }

    *outSize = 0;
    return nullptr;
}

/// @brief Release bytes allocated by LoadFileAsBytes
inline void ReleaseBytes(Csm::csmByte* byteData)
{
    std::free(byteData);
}

/// @brief Load a texture from file and create an OpenGL texture
/// @param filePath Path to the image file
/// @return OpenGL texture ID
inline GLuint LoadTextureFromFile(const std::string& filePath)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(filePath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!data)
    {
        throw std::runtime_error("Failed to load texture: " + filePath);
    }

    GLuint textureId = 0;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Premultiply alpha
    const int pixelCount = width * height;
    for (int i = 0; i < pixelCount; ++i)
    {
        const int idx = i * 4;
        const float a = static_cast<float>(data[idx + 3]) / 255.0f;
        data[idx + 0] = static_cast<unsigned char>(static_cast<float>(data[idx + 0]) * a);
        data[idx + 1] = static_cast<unsigned char>(static_cast<float>(data[idx + 1]) * a);
        data[idx + 2] = static_cast<unsigned char>(static_cast<float>(data[idx + 2]) * a);
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
    return textureId;
}

/// @brief Manages CubismFramework lifecycle and rendering matrices
/// @details RAII wrapper that calls StartUp/Initialize on construction
///          and Dispose/CleanUp on destruction.
class Live2DRenderer
{
public:
    /// @brief Initialize CubismFramework with allocator and options
    Live2DRenderer()
    {
        // Option must persist — CubismFramework stores the pointer, not a copy
        m_option.LogFunction = nullptr;
        m_option.LoggingLevel = Csm::CubismFramework::Option::LogLevel_Off;
        m_option.LoadFileFunction = LoadFileAsBytes;
        m_option.ReleaseBytesFunction = ReleaseBytes;

        if (!Csm::CubismFramework::StartUp(&m_allocator, &m_option))
        {
            throw std::runtime_error("CubismFramework::StartUp failed");
        }
        Csm::CubismFramework::Initialize();

        m_viewMatrix.SetScreenRect(-1.0f, 1.0f, -1.0f, 1.0f);
        m_viewMatrix.SetMaxScale(2.0f);
        m_viewMatrix.SetMinScale(0.8f);
        m_viewMatrix.SetMaxScreenRect(-2.0f, 2.0f, -2.0f, 2.0f);
    }

    /// @brief Dispose and clean up CubismFramework
    ~Live2DRenderer()
    {
        Csm::CubismFramework::Dispose();
        Csm::CubismFramework::CleanUp();
    }

    // Non-copyable, non-movable
    Live2DRenderer(const Live2DRenderer&) = delete;
    Live2DRenderer& operator=(const Live2DRenderer&) = delete;
    Live2DRenderer(Live2DRenderer&&) = delete;
    Live2DRenderer& operator=(Live2DRenderer&&) = delete;

    /// @brief Get the view matrix
    [[nodiscard]] Csm::CubismViewMatrix& viewMatrix() noexcept
    {
        return m_viewMatrix;
    }

    /// @brief Get the projection matrix
    [[nodiscard]] Csm::CubismMatrix44& projectionMatrix() noexcept
    {
        return m_projection;
    }

    /// @brief Set projection for given window dimensions and model width
    /// @param windowWidth Window width in pixels
    /// @param windowHeight Window height in pixels
    /// @param modelWidth Model canvas width for scaling
    void setProjection(float windowWidth, float windowHeight, float modelWidth)
    {
        const float aspect = windowWidth / windowHeight;
        m_projection.LoadIdentity();
        if (modelWidth > 0.0f)
        {
            m_projection.Scale(1.0f, aspect);
        }
        else
        {
            m_projection.Scale(1.0f / aspect, 1.0f);
        }
    }

    /// @brief Get the combined view-projection matrix
    /// @return Combined matrix (projection * view)
    [[nodiscard]] Csm::CubismMatrix44 getViewProjectionMatrix()
    {
        Csm::CubismMatrix44 vp;
        vp.SetMatrix(m_projection.GetArray());
        vp.MultiplyByMatrix(&m_viewMatrix);
        return vp;
    }

private:
    Live2DAllocator m_allocator;
    Csm::CubismFramework::Option m_option;
    Csm::CubismViewMatrix m_viewMatrix;
    Csm::CubismMatrix44 m_projection;
};

} // namespace mitiru::live2d

#endif // MITIRU_HAS_CUBISM
