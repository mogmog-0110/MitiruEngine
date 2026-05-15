#pragma once

/// @file WebGLDevice.hpp
/// @brief WebGL2バックエンド実装
/// @details Emscripten/WebGL2環境向けのIDevice実装。
///          emscripten/html5.hとGLES3/gl3.hを使用した実描画をサポートする。
///          __EMSCRIPTEN__が定義されている場合のみコンパイルされる。

#ifdef __EMSCRIPTEN__

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <emscripten/html5.h>
#include <GLES3/gl3.h>

#include <sgc/types/Color.hpp>

#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/ICommandList.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/gfx/IPipeline.hpp>
#include <mitiru/gfx/IRenderTarget.hpp>
#include <mitiru/gfx/webgl/WebGlBuffer.hpp>
#include <mitiru/gfx/webgl/WebGlPipeline.hpp>
#include <mitiru/gfx/webgl/WebGlShader.hpp>
#include <mitiru/gfx/webgl/WebGlTexture.hpp>

namespace mitiru::gfx
{

/// @brief WebGL2用コマンドリスト実装
/// @details WebGL2はイミディエイトモードのため、コマンドは即座に実行される。
///          パイプラインバインド時にVAO・シェーダー・ブレンド設定を適用する。
class WebGLCommandList final : public ICommandList
{
public:
	void begin() override {}
	void end() override {}
	void setRenderTarget(IRenderTarget*) override {}

	void clearRenderTarget(const sgc::Colorf& color) override
	{
		glClearColor(color.r, color.g, color.b, color.a);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	void setPipeline(IPipeline* pipeline) override
	{
		auto* glPipeline = dynamic_cast<WebGLPipeline*>(pipeline);
		if (glPipeline)
		{
			glPipeline->bind();
			m_currentPipeline = glPipeline;
		}
	}

	void setVertexBuffer(IBuffer* buffer) override
	{
		auto* glBuf = dynamic_cast<WebGLBuffer*>(buffer);
		if (glBuf)
		{
			glBindBuffer(GL_ARRAY_BUFFER, glBuf->handle());
		}
	}

	void setIndexBuffer(IBuffer* buffer) override
	{
		auto* glBuf = dynamic_cast<WebGLBuffer*>(buffer);
		if (glBuf)
		{
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuf->handle());
		}
	}

	void drawIndexed(std::uint32_t indexCount, std::uint32_t startIndex, std::int32_t) override
	{
		glDrawElements(
			GL_TRIANGLES,
			static_cast<GLsizei>(indexCount),
			GL_UNSIGNED_INT,
			reinterpret_cast<const void*>(
				static_cast<std::uintptr_t>(startIndex) * sizeof(uint32_t)));
	}

	void draw(std::uint32_t vertexCount, std::uint32_t startVertex) override
	{
		glDrawArrays(GL_TRIANGLES,
			static_cast<GLint>(startVertex),
			static_cast<GLsizei>(vertexCount));
	}

	void setViewport(float width, float height) override
	{
		glViewport(0, 0,
			static_cast<GLsizei>(width),
			static_cast<GLsizei>(height));
	}

private:
	WebGLPipeline* m_currentPipeline = nullptr;  ///< 現在バインド中のパイプライン（非所有）
};

/// @brief WebGL2 GPUデバイス実装
/// @details Emscripten HTML5 APIを使用してWebGL2コンテキストを管理する。
///          canvasIdで対象キャンバスを指定可能（デフォルト: "#canvas"）。
///          バッファ・シェーダー・テクスチャ・パイプラインの生成機能を提供する。
///
/// @code
/// auto device = std::make_unique<WebGLDevice>();
/// auto shader = device->createShaderProgram(vsSrc, fsSrc);
/// auto vb = device->createBuffer(BufferType::Vertex, sizeof(vertices), false, vertices);
/// device->beginFrame();
/// // WebGL2描画コマンド...
/// device->endFrame();
/// @endcode
class WebGLDevice final : public IDevice
{
public:
	/// @brief コンストラクタ
	/// @param canvasId HTMLキャンバスのセレクタ（デフォルト: "#canvas"）
	explicit WebGLDevice(const char* canvasId = "#canvas")
	{
		EmscriptenWebGLContextAttributes attrs;
		emscripten_webgl_init_context_attributes(&attrs);
		attrs.majorVersion = 2;
		attrs.minorVersion = 0;
		attrs.alpha = EM_FALSE;
		attrs.depth = EM_TRUE;
		attrs.stencil = EM_FALSE;
		attrs.antialias = EM_TRUE;
		attrs.premultipliedAlpha = EM_FALSE;
		attrs.preserveDrawingBuffer = EM_TRUE;

		m_context = emscripten_webgl_create_context(canvasId, &attrs);
		if (m_context <= 0)
		{
			throw std::runtime_error("WebGLDevice: emscripten_webgl_create_context failed");
		}

		emscripten_webgl_make_context_current(m_context);

		/// キャンバスサイズを取得する
		emscripten_get_canvas_element_size(canvasId, &m_canvasWidth, &m_canvasHeight);

		/// WebGL2の初期設定
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glViewport(0, 0, m_canvasWidth, m_canvasHeight);
	}

	~WebGLDevice() override
	{
		if (m_context > 0)
		{
			emscripten_webgl_destroy_context(m_context);
		}
	}

	WebGLDevice(const WebGLDevice&) = delete;
	WebGLDevice& operator=(const WebGLDevice&) = delete;
	WebGLDevice(WebGLDevice&&) = delete;
	WebGLDevice& operator=(WebGLDevice&&) = delete;

	/// @brief フレームバッファからピクセルを読み取る（スクリーンショット用）
	/// @param width 読み取り幅
	/// @param height 読み取り高さ
	/// @return RGBA8形式のピクセルデータ（Y-flip済み）
	[[nodiscard]] std::vector<std::uint8_t> readPixels(
		int width, int height) const override
	{
		std::vector<std::uint8_t> data(
			static_cast<std::size_t>(width) *
			static_cast<std::size_t>(height) * 4);

		glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, data.data());

		/// OpenGL/WebGLはY軸が逆なのでY-flipする
		const auto rowSize = static_cast<std::size_t>(width) * 4;
		std::vector<std::uint8_t> rowTemp(rowSize);

		for (int y = 0; y < height / 2; ++y)
		{
			const int oppositeY = height - 1 - y;
			auto* rowA = data.data() + static_cast<std::size_t>(y) * rowSize;
			auto* rowB = data.data() + static_cast<std::size_t>(oppositeY) * rowSize;
			std::memcpy(rowTemp.data(), rowA, rowSize);
			std::memcpy(rowA, rowB, rowSize);
			std::memcpy(rowB, rowTemp.data(), rowSize);
		}

		return data;
	}

	/// @brief アクティブなバックエンドを取得する
	[[nodiscard]] Backend backend() const noexcept override
	{
		return Backend::WebGL;
	}

	/// @brief フレーム開始処理
	void beginFrame() override
	{
		glClearColor(m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3]);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		checkGLError("beginFrame");
	}

	/// @brief フレーム終了・プレゼント処理
	/// @details EmscriptenはrequestAnimationFrameで自動スワップするため、
	///          明示的なスワップは不要。glFlushでコマンドの完了を保証する。
	void endFrame() override
	{
		glFlush();
		checkGLError("endFrame");
	}

	/// @brief GLエラーをチェックしてstderrに出力する
	/// @param context エラー発生箇所の識別文字列
	void checkGLError(const char* context) const noexcept
	{
		GLenum err;
		while ((err = glGetError()) != GL_NO_ERROR)
		{
			const char* errStr = "UNKNOWN";
			switch (err)
			{
			case GL_INVALID_ENUM:                  errStr = "GL_INVALID_ENUM"; break;
			case GL_INVALID_VALUE:                 errStr = "GL_INVALID_VALUE"; break;
			case GL_INVALID_OPERATION:             errStr = "GL_INVALID_OPERATION"; break;
			case GL_INVALID_FRAMEBUFFER_OPERATION: errStr = "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
			case GL_OUT_OF_MEMORY:                 errStr = "GL_OUT_OF_MEMORY"; break;
			default: break;
			}
			std::fprintf(stderr, "WebGLDevice [%s]: GL error %s (0x%04X)\n",
				context, errStr, static_cast<unsigned>(err));
		}
	}

	/// @brief GPUバッファを生成する
	[[nodiscard]] std::unique_ptr<IBuffer> createBuffer(
		BufferType bufferType,
		std::uint32_t sizeBytes,
		bool dynamic,
		const void* initialData) override
	{
		return std::make_unique<WebGLBuffer>(bufferType, sizeBytes, dynamic, initialData);
	}

	/// @brief コマンドリストを生成する
	[[nodiscard]] std::unique_ptr<ICommandList> createCommandList() override
	{
		return std::make_unique<WebGLCommandList>();
	}

	/// @brief シェーダープログラムを生成する
	/// @param vertexSource GLSL ES 3.0頂点シェーダーソース
	/// @param fragmentSource GLSL ES 3.0フラグメントシェーダーソース
	/// @return 生成されたシェーダー
	[[nodiscard]] WebGLShader createShaderProgram(
		std::string_view vertexSource,
		std::string_view fragmentSource) const
	{
		return WebGLShader::createProgram(vertexSource, fragmentSource);
	}

	/// @brief デフォルトの2Dシェーダープログラムを生成する
	/// @return 2D描画用のリンク済みシェーダー
	[[nodiscard]] WebGLShader createDefaultShader2D() const
	{
		return WebGLShader::createProgram(
			WEBGL_VERTEX_SHADER_2D,
			WEBGL_FRAGMENT_SHADER_2D);
	}

	/// @brief テクスチャを生成する
	/// @param width テクスチャ幅
	/// @param height テクスチャ高さ
	/// @param pixelFormat ピクセルフォーマット
	/// @param data ピクセルデータ
	/// @return 生成されたテクスチャ
	[[nodiscard]] WebGLTexture createTexture(
		int width, int height,
		PixelFormat pixelFormat,
		std::span<const std::uint8_t> data) const
	{
		return WebGLTexture::createFromData(width, height, pixelFormat, data);
	}

	/// @brief 空テクスチャを生成する
	/// @param width テクスチャ幅
	/// @param height テクスチャ高さ
	/// @param pixelFormat ピクセルフォーマット
	/// @return 生成されたテクスチャ
	[[nodiscard]] WebGLTexture createEmptyTexture(
		int width, int height,
		PixelFormat pixelFormat = PixelFormat::RGBA8) const
	{
		return WebGLTexture::createEmpty(width, height, pixelFormat);
	}

	/// @brief レンダリングパイプラインを生成する
	/// @param desc パイプライン記述子
	/// @return 生成されたパイプライン
	[[nodiscard]] WebGLPipeline createPipeline(const WebGLPipelineDesc& desc) const
	{
		return WebGLPipeline(desc);
	}

	/// @brief キャンバス幅を取得する
	[[nodiscard]] int canvasWidth() const noexcept { return m_canvasWidth; }

	/// @brief キャンバス高さを取得する
	[[nodiscard]] int canvasHeight() const noexcept { return m_canvasHeight; }

	/// @brief ビューポートを設定する
	/// @param x ビューポート左下X座標
	/// @param y ビューポート左下Y座標
	/// @param width ビューポート幅
	/// @param height ビューポート高さ
	void setViewport(int x, int y, int width, int height) const noexcept
	{
		glViewport(x, y, width, height);
	}

private:
	EMSCRIPTEN_WEBGL_CONTEXT_HANDLE m_context = 0;  ///< WebGLコンテキストハンドル
	int m_canvasWidth = 0;                            ///< キャンバス幅
	int m_canvasHeight = 0;                           ///< キャンバス高さ
};

} // namespace mitiru::gfx

#endif // __EMSCRIPTEN__
