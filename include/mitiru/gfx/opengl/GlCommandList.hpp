#pragma once

/// @file GlCommandList.hpp
/// @brief OpenGLコマンドリスト実装
/// @details OpenGLは即時モードのため、コマンドは直接実行される。

#ifdef MITIRU_HAS_OPENGL

#include <cstdint>

#ifdef MITIRU_HAS_SDL2
#include <SDL2/SDL_opengl.h>
#elif defined(MITIRU_HAS_GLFW)
#include <GL/glew.h>
#endif

#include <sgc/types/Color.hpp>

#include <mitiru/gfx/ICommandList.hpp>
#include <mitiru/gfx/IRenderTarget.hpp>
#include <mitiru/gfx/opengl/GlBuffer.hpp>
#include <mitiru/gfx/opengl/GlFunctions.hpp>
#include <mitiru/render/Vertex2D.hpp>
#include <mitiru/render/RenderPipeline2D.hpp>

namespace mitiru::gfx
{

/// @brief OpenGLコマンドリスト実装
/// @details 即時モードで描画コマンドを実行する。
///          begin()でシェーダーとプロジェクション行列をバインドする。
class GlCommandList final : public ICommandList
{
public:
	/// @brief コンストラクタ
	/// @param gl GL関数ポインタ群
	/// @param program シェーダープログラムハンドル
	/// @param projectionLoc プロジェクションユニフォームのロケーション
	/// @param vao 頂点配列オブジェクトハンドル
	/// @param screenWidth スクリーン幅へのポインタ
	/// @param screenHeight スクリーン高さへのポインタ
	GlCommandList(GlFunctions* gl,
	              GLuint program,
	              GLint projectionLoc,
	              GLuint vao,
	              const float* screenWidth,
	              const float* screenHeight)
		: m_gl(gl)
		, m_program(program)
		, m_projectionLoc(projectionLoc)
		, m_vao(vao)
		, m_screenWidth(screenWidth)
		, m_screenHeight(screenHeight)
	{
	}

	/// @brief コマンド記録を開始する
	/// @details シェーダープログラムをバインドし、正射影行列を設定する。
	void begin() override
	{
		m_recording = true;

		/// シェーダーをバインドする
		m_gl->useProgram(m_program);

		/// 正射影行列を設定する
		if (m_screenWidth && m_screenHeight)
		{
			const auto ortho = render::OrthoMatrix::create(
				*m_screenWidth, *m_screenHeight);
			m_gl->uniformMatrix4fv(
				m_projectionLoc, 1, GL_FALSE,
				&ortho.m[0][0]);
		}

		/// アルファブレンドを有効化する
		m_gl->enable(GL_BLEND);
		m_gl->blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	/// @brief コマンド記録を終了する
	void end() override
	{
		m_recording = false;
		m_gl->bindVertexArray(0);
		m_gl->useProgram(0);
	}

	/// @brief レンダーターゲットを設定する（OpenGLではデフォルトFBOを使用）
	void setRenderTarget(IRenderTarget*) override
	{
		/// OpenGLではデフォルトフレームバッファを使用する
	}

	/// @brief レンダーターゲットをクリアする
	void clearRenderTarget(const sgc::Colorf& color) override
	{
		if (!m_recording) return;
		glClearColor(color.r, color.g, color.b, color.a);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	/// @brief パイプライン状態を設定する（OpenGLでは何もしない）
	void setPipeline(IPipeline*) override
	{
		/// OpenGLではbegin()でシェーダーをバインド済み
	}

	/// @brief 頂点バッファを設定する
	/// @param buffer 頂点バッファ
	void setVertexBuffer(IBuffer* buffer) override
	{
		if (!m_recording || !buffer) return;

		auto* glBuf = dynamic_cast<GlBuffer*>(buffer);
		if (!glBuf) return;

		/// VAOをバインドし、VBOをアタッチして頂点アトリビュートを設定する
		m_gl->bindVertexArray(m_vao);
		m_gl->bindBuffer(GL_ARRAY_BUFFER, glBuf->glBuffer());

		/// Vertex2Dレイアウト: position(vec2) + texCoord(vec2) + color(vec4) = 32 bytes
		constexpr GLsizei stride = sizeof(render::Vertex2D);

		/// location 0: aPos (vec2, offset 0)
		m_gl->vertexAttribPointer(
			0, 2, GL_FLOAT, GL_FALSE, stride,
			reinterpret_cast<const void*>(0));
		m_gl->enableVertexAttribArray(0);

		/// location 1: aTexCoord (vec2, offset 8)
		m_gl->vertexAttribPointer(
			1, 2, GL_FLOAT, GL_FALSE, stride,
			reinterpret_cast<const void*>(8));
		m_gl->enableVertexAttribArray(1);

		/// location 2: aColor (vec4, offset 16)
		m_gl->vertexAttribPointer(
			2, 4, GL_FLOAT, GL_FALSE, stride,
			reinterpret_cast<const void*>(16));
		m_gl->enableVertexAttribArray(2);
	}

	/// @brief インデックスバッファを設定する
	/// @param buffer インデックスバッファ
	void setIndexBuffer(IBuffer* buffer) override
	{
		if (!m_recording || !buffer) return;

		auto* glBuf = dynamic_cast<GlBuffer*>(buffer);
		if (!glBuf) return;

		m_gl->bindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuf->glBuffer());
	}

	/// @brief インデックス付き描画を実行する
	void drawIndexed(std::uint32_t indexCount,
	                 std::uint32_t startIndex,
	                 std::int32_t baseVertex) override
	{
		if (!m_recording) return;

		static_cast<void>(baseVertex);

		const auto offset = static_cast<uintptr_t>(startIndex) * sizeof(std::uint32_t);
		m_gl->drawElements(
			GL_TRIANGLES,
			static_cast<GLsizei>(indexCount),
			GL_UNSIGNED_INT,
			reinterpret_cast<const void*>(offset));
	}

	/// @brief 頂点のみで描画を実行する
	void draw(std::uint32_t vertexCount,
	          std::uint32_t startVertex) override
	{
		if (!m_recording) return;
		glDrawArrays(GL_TRIANGLES,
			static_cast<GLint>(startVertex),
			static_cast<GLsizei>(vertexCount));
	}

	/// @brief ビューポートを設定する
	void setViewport(float width, float height) override
	{
		if (!m_recording) return;
		glViewport(0, 0,
			static_cast<GLsizei>(width),
			static_cast<GLsizei>(height));
	}

private:
	GlFunctions* m_gl = nullptr;          ///< GL関数ポインタ群（非所有）
	GLuint m_program = 0;                  ///< シェーダープログラム
	GLint m_projectionLoc = -1;            ///< uProjectionロケーション
	GLuint m_vao = 0;                      ///< 頂点配列オブジェクト
	const float* m_screenWidth = nullptr;  ///< スクリーン幅（非所有）
	const float* m_screenHeight = nullptr; ///< スクリーン高さ（非所有）
	bool m_recording = false;              ///< 記録中フラグ
};

} // namespace mitiru::gfx

#endif // MITIRU_HAS_OPENGL
