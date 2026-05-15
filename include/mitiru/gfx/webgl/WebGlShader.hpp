#pragma once

/// @file WebGlShader.hpp
/// @brief WebGL2 GLSL ES 3.0シェーダー実装
/// @details GLSL ES 3.0シェーダーのコンパイル・リンクをRAIIで管理する。
///          頂点シェーダーとフラグメントシェーダーをリンクしたプログラムを保持する。

#ifdef __EMSCRIPTEN__

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <GLES3/gl3.h>

#include <mitiru/gfx/IShader.hpp>

namespace mitiru::gfx
{

/// @brief WebGL2用シェーダー実装
/// @details GLSL ES 3.0ソースをコンパイルし、シェーダープログラムをRAIIで管理する。
///          デストラクタでglDeleteProgram / glDeleteShaderを呼び出す。
///
/// @code
/// auto shader = WebGLShader::createProgram(vertexSrc, fragmentSrc);
/// glUseProgram(shader.program());
/// @endcode
class WebGLShader final : public IShader
{
public:
	/// @brief 頂点＋フラグメントシェーダーからプログラムを生成するファクトリ
	/// @param vertexSource GLSL ES 3.0頂点シェーダーソース
	/// @param fragmentSource GLSL ES 3.0フラグメントシェーダーソース
	/// @return 生成されたWebGLShader
	[[nodiscard]] static WebGLShader createProgram(
		std::string_view vertexSource,
		std::string_view fragmentSource)
	{
		WebGLShader shader;
		shader.m_type = ShaderType::Vertex;

		shader.m_vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
		shader.m_fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
		shader.m_program = linkProgram(shader.m_vertexShader, shader.m_fragmentShader);

		return shader;
	}

	/// @brief 頂点シェーダーのみを生成するファクトリ
	/// @param source GLSL ES 3.0頂点シェーダーソース
	/// @return 生成されたWebGLShader
	[[nodiscard]] static WebGLShader createVertexShader(std::string_view source)
	{
		WebGLShader shader;
		shader.m_type = ShaderType::Vertex;
		shader.m_vertexShader = compileShader(GL_VERTEX_SHADER, source);
		return shader;
	}

	/// @brief フラグメントシェーダーのみを生成するファクトリ
	/// @param source GLSL ES 3.0フラグメントシェーダーソース
	/// @return 生成されたWebGLShader
	[[nodiscard]] static WebGLShader createFragmentShader(std::string_view source)
	{
		WebGLShader shader;
		shader.m_type = ShaderType::Pixel;
		shader.m_fragmentShader = compileShader(GL_FRAGMENT_SHADER, source);
		return shader;
	}

	~WebGLShader() override
	{
		if (m_program != 0)
		{
			glDeleteProgram(m_program);
		}
		if (m_vertexShader != 0)
		{
			glDeleteShader(m_vertexShader);
		}
		if (m_fragmentShader != 0)
		{
			glDeleteShader(m_fragmentShader);
		}
	}

	WebGLShader(const WebGLShader&) = delete;
	WebGLShader& operator=(const WebGLShader&) = delete;

	WebGLShader(WebGLShader&& other) noexcept
		: m_program(other.m_program)
		, m_vertexShader(other.m_vertexShader)
		, m_fragmentShader(other.m_fragmentShader)
		, m_type(other.m_type)
	{
		other.m_program = 0;
		other.m_vertexShader = 0;
		other.m_fragmentShader = 0;
	}

	WebGLShader& operator=(WebGLShader&& other) noexcept
	{
		if (this != &other)
		{
			if (m_program != 0) { glDeleteProgram(m_program); }
			if (m_vertexShader != 0) { glDeleteShader(m_vertexShader); }
			if (m_fragmentShader != 0) { glDeleteShader(m_fragmentShader); }

			m_program = other.m_program;
			m_vertexShader = other.m_vertexShader;
			m_fragmentShader = other.m_fragmentShader;
			m_type = other.m_type;

			other.m_program = 0;
			other.m_vertexShader = 0;
			other.m_fragmentShader = 0;
		}
		return *this;
	}

	/// @brief シェーダー種別を取得する
	[[nodiscard]] ShaderType type() const noexcept override { return m_type; }

	/// @brief リンク済みプログラムIDを取得する
	/// @return GLプログラムID（未リンクの場合は0）
	[[nodiscard]] GLuint program() const noexcept { return m_program; }

	/// @brief 頂点シェーダーIDを取得する
	[[nodiscard]] GLuint vertexShader() const noexcept { return m_vertexShader; }

	/// @brief フラグメントシェーダーIDを取得する
	[[nodiscard]] GLuint fragmentShader() const noexcept { return m_fragmentShader; }

	/// @brief プログラムが有効かどうかを判定する
	[[nodiscard]] bool isValid() const noexcept { return m_program != 0; }

	/// @brief ユニフォームのロケーションを取得する
	/// @param name ユニフォーム名
	/// @return ロケーション値（見つからない場合は-1）
	[[nodiscard]] GLint getUniformLocation(const char* name) const noexcept
	{
		if (m_program == 0)
		{
			return -1;
		}
		return glGetUniformLocation(m_program, name);
	}

	/// @brief アトリビュートのロケーションを取得する
	/// @param name アトリビュート名
	/// @return ロケーション値（見つからない場合は-1）
	[[nodiscard]] GLint getAttribLocation(const char* name) const noexcept
	{
		if (m_program == 0)
		{
			return -1;
		}
		return glGetAttribLocation(m_program, name);
	}

	/// @brief プログラムをアクティブにする
	void use() const noexcept
	{
		if (m_program != 0)
		{
			glUseProgram(m_program);
		}
	}

private:
	/// @brief デフォルトコンストラクタ（ファクトリからのみ使用）
	WebGLShader() = default;

	/// @brief GLSLシェーダーをコンパイルする
	/// @param shaderType GL_VERTEX_SHADER または GL_FRAGMENT_SHADER
	/// @param source GLSL ES 3.0ソース文字列
	/// @return コンパイル済みシェーダーID
	[[nodiscard]] static GLuint compileShader(GLenum shaderType, std::string_view source)
	{
		const GLuint shader = glCreateShader(shaderType);
		if (shader == 0)
		{
			throw std::runtime_error("WebGLShader: glCreateShader failed");
		}

		const char* srcPtr = source.data();
		const GLint srcLen = static_cast<GLint>(source.size());
		glShaderSource(shader, 1, &srcPtr, &srcLen);
		glCompileShader(shader);

		GLint compiled = 0;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
		if (compiled == 0)
		{
			GLint logLen = 0;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);

			std::string infoLog(static_cast<std::size_t>(logLen), '\0');
			glGetShaderInfoLog(shader, logLen, nullptr, infoLog.data());

			glDeleteShader(shader);

			const char* typeName = (shaderType == GL_VERTEX_SHADER)
				? "vertex" : "fragment";
			throw std::runtime_error(
				std::string("WebGLShader: ") + typeName +
				" shader compile failed: " + infoLog);
		}

		return shader;
	}

	/// @brief 頂点＋フラグメントシェーダーをリンクする
	/// @param vs コンパイル済み頂点シェーダーID
	/// @param fs コンパイル済みフラグメントシェーダーID
	/// @return リンク済みプログラムID
	[[nodiscard]] static GLuint linkProgram(GLuint vs, GLuint fs)
	{
		const GLuint program = glCreateProgram();
		if (program == 0)
		{
			throw std::runtime_error("WebGLShader: glCreateProgram failed");
		}

		glAttachShader(program, vs);
		glAttachShader(program, fs);
		glLinkProgram(program);

		GLint linked = 0;
		glGetProgramiv(program, GL_LINK_STATUS, &linked);
		if (linked == 0)
		{
			GLint logLen = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);

			std::string infoLog(static_cast<std::size_t>(logLen), '\0');
			glGetProgramInfoLog(program, logLen, nullptr, infoLog.data());

			glDeleteProgram(program);

			throw std::runtime_error(
				std::string("WebGLShader: program link failed: ") + infoLog);
		}

		return program;
	}

	GLuint m_program = 0;         ///< リンク済みプログラムID
	GLuint m_vertexShader = 0;    ///< 頂点シェーダーID
	GLuint m_fragmentShader = 0;  ///< フラグメントシェーダーID
	ShaderType m_type = ShaderType::Vertex;  ///< シェーダー種別
};

/// @brief 2D頂点シェーダー（GLSL ES 3.0）
/// @details 正射影変換を適用し、頂点色・テクスチャ座標をフラグメントシェーダーに渡す。
constexpr const char* WEBGL_VERTEX_SHADER_2D = R"glsl(#version 300 es
precision highp float;

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

/// @brief 2Dフラグメントシェーダー（GLSL ES 3.0）
/// @details 頂点色をそのまま出力する。テクスチャ使用時はuUseTextureで切り替え可能。
constexpr const char* WEBGL_FRAGMENT_SHADER_2D = R"glsl(#version 300 es
precision highp float;

in vec4 vColor;
in vec2 vTexCoord;

uniform sampler2D uTexture;
uniform int uUseTexture;

out vec4 fragColor;

void main()
{
    if (uUseTexture != 0)
    {
        fragColor = texture(uTexture, vTexCoord) * vColor;
    }
    else
    {
        fragColor = vColor;
    }
}
)glsl";

} // namespace mitiru::gfx

#endif // __EMSCRIPTEN__
