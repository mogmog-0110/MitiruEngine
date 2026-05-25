#pragma once

/// @file Live2DRenderer.hpp
/// @brief Live2D Cubism Framework 向けの OpenGL renderer setup
/// @details CubismFramework の lifecycle、view/projection matrix、
///          stb_image 経由の texture 読み込みを管理する。

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

/// @brief ファイルを malloc した buffer に読み込む
/// @param path file path (絶対 or 相対)
/// @param outSize 出力: ファイルサイズ (byte)
/// @return buffer (呼び出し側が ReleaseBytes で解放)、失敗時は nullptr
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

/// @brief Cubism SDK 向けのファイル読み込みユーティリティ
/// @details CubismFramework の LoadFileFunction として使う。絶対 path
///          (model ファイル) と相対 path (framework shader ファイル) の両方を扱う。
///          "FrameworkShaders/..." path は SDK の shader directory を探索する。
inline Csm::csmByte* LoadFileAsBytes(const std::string filePath, Csm::csmSizeInt* outSize)
{
    // まずそのまま試す (絶対 path や正しい working directory では成功する)
    auto* buf = ReadFileBytes(filePath.c_str(), outSize);
    if (buf) return buf;

    // fallback: "FrameworkShaders/X" を SDK の Standard shader dir に解決する
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

/// @brief LoadFileAsBytes で確保した byte を解放する
inline void ReleaseBytes(Csm::csmByte* byteData)
{
    std::free(byteData);
}

/// @brief ファイルから texture を読み込み OpenGL texture を生成する
/// @param filePath 画像ファイルへの path
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

    // alpha を premultiply する
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

/// @brief CubismFramework の lifecycle と描画 matrix を管理する
/// @details 構築時に StartUp/Initialize、破棄時に Dispose/CleanUp を呼ぶ
///          RAII wrapper。
class Live2DRenderer
{
public:
    /// @brief allocator と option で CubismFramework を初期化する
    Live2DRenderer()
    {
        // option は生存させ続ける — CubismFramework は copy でなく pointer を保持する
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

    /// @brief CubismFramework を Dispose / clean up する
    ~Live2DRenderer()
    {
        Csm::CubismFramework::Dispose();
        Csm::CubismFramework::CleanUp();
    }

    // copy / move 禁止
    Live2DRenderer(const Live2DRenderer&) = delete;
    Live2DRenderer& operator=(const Live2DRenderer&) = delete;
    Live2DRenderer(Live2DRenderer&&) = delete;
    Live2DRenderer& operator=(Live2DRenderer&&) = delete;

    /// @brief view matrix を取得する
    [[nodiscard]] Csm::CubismViewMatrix& viewMatrix() noexcept
    {
        return m_viewMatrix;
    }

    /// @brief projection matrix を取得する
    [[nodiscard]] Csm::CubismMatrix44& projectionMatrix() noexcept
    {
        return m_projection;
    }

    /// @brief 与えられた window 寸法と model 幅で projection を設定する
    /// @param windowWidth window 幅 (pixel)
    /// @param windowHeight window 高さ (pixel)
    /// @param modelWidth scaling 用の model canvas 幅
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

    /// @brief 合成済みの view-projection matrix を取得する
    /// @return 合成 matrix (projection * view)
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
