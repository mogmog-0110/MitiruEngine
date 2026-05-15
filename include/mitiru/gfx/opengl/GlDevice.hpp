#pragma once

/// @file GlDevice.hpp
/// @brief OpenGL 3.3 Coreデバイス実装
/// @details SDL2またはGLFWウィンドウからOpenGLコンテキストを管理し、
///          フレーム制御・バッファ生成・シェーダー管理を提供するIDevice実装。

#ifdef MITIRU_HAS_OPENGL

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef MITIRU_HAS_SDL2
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <mitiru/platform/sdl2/Sdl2Window.hpp>
#endif

#ifdef MITIRU_HAS_GLFW
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <mitiru/platform/glfw/GlfwWindow.hpp>
#endif

#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/ICommandList.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/gfx/opengl/GlBuffer.hpp>
#include <mitiru/gfx/opengl/GlCommandList.hpp>
#include <mitiru/gfx/opengl/GlFunctions.hpp>
#include <mitiru/gfx/opengl/GlShaders.hpp>

namespace mitiru::gfx
{

/// @brief OpenGL 3.3 Coreデバイス実装
/// @details SDL2またはGLFWウィンドウからGLコンテキストを管理し、
///          シェーダーとVAOを管理する。
class GlDevice final : public IDevice
{
public:
#ifdef MITIRU_HAS_SDL2
	/// @brief SDL2ウィンドウからコンストラクト
	/// @param window SDL2ウィンドウ
	explicit GlDevice(mitiru::Sdl2Window* window)
		: m_screenWidth(static_cast<float>(window->width()))
		, m_screenHeight(static_cast<float>(window->height()))
	{
		if (!window) throw std::runtime_error("GlDevice: Sdl2Window is null");

		m_sdlWindow = window->nativeWindow();

		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

		m_glContext = SDL_GL_CreateContext(m_sdlWindow);
		if (!m_glContext)
		{
			throw std::runtime_error(
				std::string("GlDevice: SDL_GL_CreateContext failed: ") + SDL_GetError());
		}
		SDL_GL_SetSwapInterval(1);

		m_swapFunc = [this]() { SDL_GL_SwapWindow(m_sdlWindow); };
		initGL();
	}
#endif

#ifdef MITIRU_HAS_GLFW
	/// @brief GLFWウィンドウからコンストラクト (OpenGLモード)
	/// @param window GLFWウィンドウ (GlfwGraphicsMode::OpenGL で作成済み)
	explicit GlDevice(mitiru::GlfwWindow* window)
		: m_screenWidth(static_cast<float>(window->width()))
		, m_screenHeight(static_cast<float>(window->height()))
	{
		if (!window) throw std::runtime_error("GlDevice: GlfwWindow is null");
		m_glfwWindow = window->nativeWindow();

		// GLFWウィンドウが既にOpenGLコンテキストを持っている
		// (GlfwGraphicsMode::OpenGL で作成された場合)
		m_swapFunc = [this]() { glfwSwapBuffers(m_glfwWindow); };
		initGL();
	}
#endif

	/// @brief デストラクタ
	~GlDevice() override
	{
		if (m_vao != 0)
		{
			m_gl.deleteVertexArrays(1, &m_vao);
			m_vao = 0;
		}

		if (m_program != 0)
		{
			m_gl.deleteProgram(m_program);
			m_program = 0;
		}

#ifdef MITIRU_HAS_SDL2
		if (m_glContext)
		{
			SDL_GL_DeleteContext(m_glContext);
			m_glContext = nullptr;
		}
#endif
	}

	/// コピー禁止
	GlDevice(const GlDevice&) = delete;
	GlDevice& operator=(const GlDevice&) = delete;

	/// ムーブ禁止
	GlDevice(GlDevice&&) = delete;
	GlDevice& operator=(GlDevice&&) = delete;

	/// @brief フレームバッファからピクセルを読み取る
	/// @param width 読み取り幅
	/// @param height 読み取り高さ
	/// @return RGBA8形式のピクセルデータ
	[[nodiscard]] std::vector<std::uint8_t> readPixels(
		int width, int height) const override
	{
		const auto pixelCount = static_cast<std::size_t>(width) *
		                        static_cast<std::size_t>(height);
		std::vector<std::uint8_t> pixels(pixelCount * 4);

		glReadPixels(0, 0, width, height,
			GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

		/// OpenGLはY軸が反転しているため上下反転する
		const auto rowBytes = static_cast<std::size_t>(width) * 4;
		std::vector<std::uint8_t> row(rowBytes);
		for (int y = 0; y < height / 2; ++y)
		{
			const auto topIdx = static_cast<std::size_t>(y) * rowBytes;
			const auto botIdx = static_cast<std::size_t>(height - 1 - y) * rowBytes;
			std::memcpy(row.data(), pixels.data() + topIdx, rowBytes);
			std::memcpy(pixels.data() + topIdx, pixels.data() + botIdx, rowBytes);
			std::memcpy(pixels.data() + botIdx, row.data(), rowBytes);
		}

		return pixels;
	}

	/// @brief アクティブなバックエンドを取得する
	[[nodiscard]] Backend backend() const noexcept override
	{
		return Backend::OpenGL;
	}

	/// @brief フレーム開始処理
	/// @details コーンフラワーブルーでクリアする。
	void beginFrame() override
	{
		glClearColor(m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3]);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	/// @brief フレーム終了・プレゼント処理
	void endFrame() override
	{
		if (m_swapFunc) m_swapFunc();
	}

	/// @brief GPUバッファを生成する
	[[nodiscard]] std::unique_ptr<IBuffer> createBuffer(
		BufferType bufferType,
		std::uint32_t sizeBytes,
		bool dynamic,
		const void* initialData) override
	{
		return std::make_unique<GlBuffer>(
			&m_gl, bufferType, sizeBytes, dynamic, initialData);
	}

	/// @brief コマンドリストを生成する
	[[nodiscard]] std::unique_ptr<ICommandList> createCommandList() override
	{
		return std::make_unique<GlCommandList>(
			&m_gl, m_program, m_projectionLoc, m_vao,
			&m_screenWidth, &m_screenHeight);
	}

	/// @brief GL関数ポインタ群を取得する
	[[nodiscard]] GlFunctions& glFunctions() noexcept
	{
		return m_gl;
	}

	/// @brief シェーダープログラムを取得する
	[[nodiscard]] GLuint program() const noexcept
	{
		return m_program;
	}

	/// @brief VAOを取得する
	[[nodiscard]] GLuint vao() const noexcept
	{
		return m_vao;
	}

	/// @brief スクリーン幅を取得する
	[[nodiscard]] float screenWidth() const noexcept
	{
		return m_screenWidth;
	}

	/// @brief スクリーン高さを取得する
	[[nodiscard]] float screenHeight() const noexcept
	{
		return m_screenHeight;
	}

	/// @brief スクリーンサイズを更新する
	void updateScreenSize(float width, float height) noexcept
	{
		m_screenWidth = width;
		m_screenHeight = height;
	}

private:
	/// @brief シェーダーをコンパイル・リンクする
	void compileShaders()
	{
		/// 頂点シェーダーをコンパイルする
		GLuint vs = m_gl.createShader(GL_VERTEX_SHADER);
		m_gl.shaderSource(vs, 1, &GL_VERTEX_SHADER_2D, nullptr);
		m_gl.compileShader(vs);
		checkShaderCompile(vs, "vertex");

		/// フラグメントシェーダーをコンパイルする
		GLuint fs = m_gl.createShader(GL_FRAGMENT_SHADER);
		m_gl.shaderSource(fs, 1, &GL_FRAGMENT_SHADER_2D, nullptr);
		m_gl.compileShader(fs);
		checkShaderCompile(fs, "fragment");

		/// プログラムをリンクする
		m_program = m_gl.createProgram();
		m_gl.attachShader(m_program, vs);
		m_gl.attachShader(m_program, fs);
		m_gl.linkProgram(m_program);
		checkProgramLink(m_program);

		/// シェーダーオブジェクトを削除する（プログラムにリンク済み）
		m_gl.deleteShader(vs);
		m_gl.deleteShader(fs);

		/// ユニフォームロケーションを取得する
		m_projectionLoc = m_gl.getUniformLocation(
			m_program, "uProjection");
	}

	/// @brief シェーダーコンパイルエラーを検査する
	void checkShaderCompile(GLuint shader, const char* name)
	{
		GLint success = 0;
		m_gl.getShaderiv(shader, GL_COMPILE_STATUS, &success);
		if (!success)
		{
			GLint logLen = 0;
			m_gl.getShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
			std::string log(static_cast<std::size_t>(logLen), '\0');
			m_gl.getShaderInfoLog(shader, logLen, nullptr, log.data());
			throw std::runtime_error(
				std::string("GlDevice: ") + name +
				" shader compile failed: " + log);
		}
	}

	/// @brief プログラムリンクエラーを検査する
	void checkProgramLink(GLuint program)
	{
		GLint success = 0;
		m_gl.getProgramiv(program, GL_LINK_STATUS, &success);
		if (!success)
		{
			GLint logLen = 0;
			m_gl.getProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
			std::string log(static_cast<std::size_t>(logLen), '\0');
			/// getProgramInfoLogはGlFunctionsにないので直接使用しない
			/// 代わりにリンクエラーとして報告する
			throw std::runtime_error(
				"GlDevice: shader program link failed");
		}
	}

	/// @brief GL関数ロード + シェーダー + VAO初期化
	void initGL()
	{
		m_gl.load();
		compileShaders();
		m_gl.genVertexArrays(1, &m_vao);
	}

#ifdef MITIRU_HAS_SDL2
	SDL_Window* m_sdlWindow = nullptr;          ///< SDL_Windowハンドル（非所有）
	SDL_GLContext m_glContext = nullptr;         ///< OpenGLコンテキスト
#endif
#ifdef MITIRU_HAS_GLFW
	GLFWwindow* m_glfwWindow = nullptr;         ///< GLFWウィンドウハンドル（非所有）
#endif
	std::function<void()> m_swapFunc;           ///< バッファスワップ関数
	GlFunctions m_gl;                           ///< GL関数ポインタ群
	GLuint m_program = 0;                       ///< シェーダープログラム
	GLint m_projectionLoc = -1;                 ///< uProjectionユニフォームロケーション
	GLuint m_vao = 0;                           ///< 頂点配列オブジェクト
	float m_screenWidth = 0.0f;                 ///< スクリーン幅
	float m_screenHeight = 0.0f;                ///< スクリーン高さ
};

} // namespace mitiru::gfx

#endif // MITIRU_HAS_OPENGL
