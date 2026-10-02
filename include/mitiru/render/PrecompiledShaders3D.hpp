#pragma once
/// @file PrecompiledShaders3D.hpp
/// @brief プリコンパイル済み 3D シェーダーバイトコード
/// @details D3DCompiler 依存を除去するため、コンパイル済み HLSL バイトコードを埋め込む。
///          配布ビルドではこちらを使用し、D3DCompiler_47.dll を不要にする。

#include <cstdint>
#include <cstddef>

namespace mitiru::render
{

/// @brief プリコンパイル済みシェーダーの有無を示すフラグ
/// @details 配布ビルドでは true に設定し、D3DCompile 呼び出しをスキップする。
///          開発ビルドでは false のまま D3DCompile を使用する。
#ifdef MITIRU_USE_PRECOMPILED_SHADERS
inline constexpr bool kUsePrecompiledShaders = true;
#else
inline constexpr bool kUsePrecompiledShaders = false;
#endif

/// @brief シェーダーバイトコードを保持する構造体
struct ShaderBytecode
{
    const uint8_t* data = nullptr;
    std::size_t size = 0;

    [[nodiscard]] bool valid() const noexcept { return data != nullptr && size > 0; }
};

/// @brief プリコンパイル済み頂点シェーダーのバイトコード
/// @details fxc /T vs_5_0 /E VSMain でコンパイルしたバイトコードを
///          tools/compile_shaders.py で生成して埋め込む。
///          未コンパイル時は空を返す。
[[nodiscard]] inline ShaderBytecode precompiledVS3D() noexcept
{
#ifdef MITIRU_VS_3D_BYTECODE
    static const uint8_t data[] = { MITIRU_VS_3D_BYTECODE };
    return { data, sizeof(data) };
#else
    return {};
#endif
}

/// @brief プリコンパイル済みピクセルシェーダーのバイトコード
[[nodiscard]] inline ShaderBytecode precompiledPS3D() noexcept
{
#ifdef MITIRU_PS_3D_BYTECODE
    static const uint8_t data[] = { MITIRU_PS_3D_BYTECODE };
    return { data, sizeof(data) };
#else
    return {};
#endif
}

/// @brief プリコンパイル済み 2D 頂点シェーダーのバイトコード
[[nodiscard]] inline ShaderBytecode precompiledVS2D() noexcept
{
#ifdef MITIRU_VS_2D_BYTECODE
    static const uint8_t data[] = { MITIRU_VS_2D_BYTECODE };
    return { data, sizeof(data) };
#else
    return {};
#endif
}

/// @brief プリコンパイル済み 2D ピクセルシェーダーのバイトコード
[[nodiscard]] inline ShaderBytecode precompiledPS2D() noexcept
{
#ifdef MITIRU_PS_2D_BYTECODE
    static const uint8_t data[] = { MITIRU_PS_2D_BYTECODE };
    return { data, sizeof(data) };
#else
    return {};
#endif
}

} // namespace mitiru::render
