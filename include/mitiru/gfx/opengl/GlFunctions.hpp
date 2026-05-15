#pragma once

/// @file GlFunctions.hpp
/// @brief OpenGL 3.3 Core関数ポインタローダー
/// @details SDL_GL_GetProcAddressを使用してGL 3.3 Core関数をロードする。
///          MITIRU_HAS_OPENGLが定義されている場合のみコンパイルされる。

#ifdef MITIRU_HAS_OPENGL

#ifdef MITIRU_HAS_SDL2
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#elif defined(MITIRU_HAS_GLFW)
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#endif

#include <stdexcept>
#include <string>

/// @brief GL定数定義（OpenGL 3.3 Core）
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_INFO_LOG_LENGTH
#define GL_INFO_LOG_LENGTH 0x8B84
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_UNIFORM_BUFFER
#define GL_UNIFORM_BUFFER 0x8A11
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif

namespace mitiru::gfx
{

/// @brief OpenGL 3.3 Core関数ポインタ群
/// @details SDL_GL_GetProcAddressで動的にロードする。
struct GlFunctions
{
	/// シェーダー関連
	using PFN_glCreateShader = GLuint (*)(GLenum);
	using PFN_glShaderSource = void (*)(GLuint, GLsizei, const GLchar* const*, const GLint*);
	using PFN_glCompileShader = void (*)(GLuint);
	using PFN_glGetShaderiv = void (*)(GLuint, GLenum, GLint*);
	using PFN_glGetShaderInfoLog = void (*)(GLuint, GLsizei, GLsizei*, GLchar*);
	using PFN_glCreateProgram = GLuint (*)();
	using PFN_glAttachShader = void (*)(GLuint, GLuint);
	using PFN_glLinkProgram = void (*)(GLuint);
	using PFN_glGetProgramiv = void (*)(GLuint, GLenum, GLint*);
	using PFN_glUseProgram = void (*)(GLuint);
	using PFN_glDeleteShader = void (*)(GLuint);
	using PFN_glDeleteProgram = void (*)(GLuint);

	/// VAO関連
	using PFN_glGenVertexArrays = void (*)(GLsizei, GLuint*);
	using PFN_glDeleteVertexArrays = void (*)(GLsizei, const GLuint*);
	using PFN_glBindVertexArray = void (*)(GLuint);

	/// バッファ関連
	using PFN_glGenBuffers = void (*)(GLsizei, GLuint*);
	using PFN_glDeleteBuffers = void (*)(GLsizei, const GLuint*);
	using PFN_glBindBuffer = void (*)(GLenum, GLuint);
	using PFN_glBufferData = void (*)(GLenum, GLsizeiptr, const void*, GLenum);
	using PFN_glBufferSubData = void (*)(GLenum, GLintptr, GLsizeiptr, const void*);

	/// 頂点アトリビュート関連
	using PFN_glVertexAttribPointer = void (*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
	using PFN_glEnableVertexAttribArray = void (*)(GLuint);

	/// ユニフォーム関連
	using PFN_glGetUniformLocation = GLint (*)(GLuint, const GLchar*);
	using PFN_glUniformMatrix4fv = void (*)(GLint, GLsizei, GLboolean, const GLfloat*);

	/// 描画関連
	using PFN_glDrawElements = void (*)(GLenum, GLsizei, GLenum, const void*);

	/// ステート関連
	using PFN_glEnable = void (*)(GLenum);
	using PFN_glBlendFunc = void (*)(GLenum, GLenum);
	using PFN_glDisable = void (*)(GLenum);

	PFN_glCreateShader createShader = nullptr;
	PFN_glShaderSource shaderSource = nullptr;
	PFN_glCompileShader compileShader = nullptr;
	PFN_glGetShaderiv getShaderiv = nullptr;
	PFN_glGetShaderInfoLog getShaderInfoLog = nullptr;
	PFN_glCreateProgram createProgram = nullptr;
	PFN_glAttachShader attachShader = nullptr;
	PFN_glLinkProgram linkProgram = nullptr;
	PFN_glGetProgramiv getProgramiv = nullptr;
	PFN_glUseProgram useProgram = nullptr;
	PFN_glDeleteShader deleteShader = nullptr;
	PFN_glDeleteProgram deleteProgram = nullptr;
	PFN_glGenVertexArrays genVertexArrays = nullptr;
	PFN_glDeleteVertexArrays deleteVertexArrays = nullptr;
	PFN_glBindVertexArray bindVertexArray = nullptr;
	PFN_glGenBuffers genBuffers = nullptr;
	PFN_glDeleteBuffers deleteBuffers = nullptr;
	PFN_glBindBuffer bindBuffer = nullptr;
	PFN_glBufferData bufferData = nullptr;
	PFN_glBufferSubData bufferSubData = nullptr;
	PFN_glVertexAttribPointer vertexAttribPointer = nullptr;
	PFN_glEnableVertexAttribArray enableVertexAttribArray = nullptr;
	PFN_glGetUniformLocation getUniformLocation = nullptr;
	PFN_glUniformMatrix4fv uniformMatrix4fv = nullptr;
	PFN_glDrawElements drawElements = nullptr;
	PFN_glEnable enable = nullptr;
	PFN_glBlendFunc blendFunc = nullptr;
	PFN_glDisable disable = nullptr;

	/// @brief GL関数ポインタをロードする (SDL2またはGLFW経由)
	/// @throw std::runtime_error 必須関数のロードに失敗した場合
	void load()
	{
		auto loadFn = [](const char* name) -> void*
		{
#ifdef MITIRU_HAS_SDL2
			return SDL_GL_GetProcAddress(name);
#elif defined(MITIRU_HAS_GLFW)
			return reinterpret_cast<void*>(glfwGetProcAddress(name));
#else
			static_cast<void>(name);
			return nullptr;
#endif
		};

		createShader = reinterpret_cast<PFN_glCreateShader>(loadFn("glCreateShader"));
		shaderSource = reinterpret_cast<PFN_glShaderSource>(loadFn("glShaderSource"));
		compileShader = reinterpret_cast<PFN_glCompileShader>(loadFn("glCompileShader"));
		getShaderiv = reinterpret_cast<PFN_glGetShaderiv>(loadFn("glGetShaderiv"));
		getShaderInfoLog = reinterpret_cast<PFN_glGetShaderInfoLog>(loadFn("glGetShaderInfoLog"));
		createProgram = reinterpret_cast<PFN_glCreateProgram>(loadFn("glCreateProgram"));
		attachShader = reinterpret_cast<PFN_glAttachShader>(loadFn("glAttachShader"));
		linkProgram = reinterpret_cast<PFN_glLinkProgram>(loadFn("glLinkProgram"));
		getProgramiv = reinterpret_cast<PFN_glGetProgramiv>(loadFn("glGetProgramiv"));
		useProgram = reinterpret_cast<PFN_glUseProgram>(loadFn("glUseProgram"));
		deleteShader = reinterpret_cast<PFN_glDeleteShader>(loadFn("glDeleteShader"));
		deleteProgram = reinterpret_cast<PFN_glDeleteProgram>(loadFn("glDeleteProgram"));
		genVertexArrays = reinterpret_cast<PFN_glGenVertexArrays>(loadFn("glGenVertexArrays"));
		deleteVertexArrays = reinterpret_cast<PFN_glDeleteVertexArrays>(loadFn("glDeleteVertexArrays"));
		bindVertexArray = reinterpret_cast<PFN_glBindVertexArray>(loadFn("glBindVertexArray"));
		genBuffers = reinterpret_cast<PFN_glGenBuffers>(loadFn("glGenBuffers"));
		deleteBuffers = reinterpret_cast<PFN_glDeleteBuffers>(loadFn("glDeleteBuffers"));
		bindBuffer = reinterpret_cast<PFN_glBindBuffer>(loadFn("glBindBuffer"));
		bufferData = reinterpret_cast<PFN_glBufferData>(loadFn("glBufferData"));
		bufferSubData = reinterpret_cast<PFN_glBufferSubData>(loadFn("glBufferSubData"));
		vertexAttribPointer = reinterpret_cast<PFN_glVertexAttribPointer>(loadFn("glVertexAttribPointer"));
		enableVertexAttribArray = reinterpret_cast<PFN_glEnableVertexAttribArray>(loadFn("glEnableVertexAttribArray"));
		getUniformLocation = reinterpret_cast<PFN_glGetUniformLocation>(loadFn("glGetUniformLocation"));
		uniformMatrix4fv = reinterpret_cast<PFN_glUniformMatrix4fv>(loadFn("glUniformMatrix4fv"));
		drawElements = reinterpret_cast<PFN_glDrawElements>(loadFn("glDrawElements"));
		enable = reinterpret_cast<PFN_glEnable>(loadFn("glEnable"));
		blendFunc = reinterpret_cast<PFN_glBlendFunc>(loadFn("glBlendFunc"));
		disable = reinterpret_cast<PFN_glDisable>(loadFn("glDisable"));

		/// 必須関数の検証
		if (!createShader || !shaderSource || !compileShader ||
		    !createProgram || !attachShader || !linkProgram ||
		    !useProgram || !genVertexArrays || !bindVertexArray ||
		    !genBuffers || !bindBuffer || !bufferData ||
		    !vertexAttribPointer || !enableVertexAttribArray ||
		    !getUniformLocation || !uniformMatrix4fv ||
		    !drawElements || !enable || !blendFunc)
		{
			throw std::runtime_error(
				"GlFunctions: failed to load required GL 3.3 functions");
		}
	}
};

} // namespace mitiru::gfx

#endif // MITIRU_HAS_OPENGL
