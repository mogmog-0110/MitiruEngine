#pragma once

/// @file GlShaders.hpp
/// @brief GLSL 330 Coreシェーダー定義
/// @details 2D描画用の頂点シェーダーとフラグメントシェーダーをconstexpr文字列リテラルで定義する。

#ifdef MITIRU_HAS_OPENGL

namespace mitiru::gfx
{

/// @brief 2D頂点シェーダー（GLSL 330 core）
/// @details 正射影変換を適用し、頂点色・テクスチャ座標をフラグメントシェーダーに渡す。
constexpr const char* GL_VERTEX_SHADER_2D = R"glsl(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;

uniform mat4 uProjection;

out vec4 vColor;
out vec2 vTexCoord;

void main()
{
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
    vColor = aColor;
    vTexCoord = aTexCoord;
}
)glsl";

/// @brief 2Dフラグメントシェーダー（GLSL 330 core）
/// @details 頂点色をそのまま出力する。
constexpr const char* GL_FRAGMENT_SHADER_2D = R"glsl(
#version 330 core
in vec4 vColor;
in vec2 vTexCoord;

out vec4 fragColor;

void main()
{
    fragColor = vColor;
}
)glsl";

} // namespace mitiru::gfx

#endif // MITIRU_HAS_OPENGL
