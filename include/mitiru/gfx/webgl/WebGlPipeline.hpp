#pragma once

/// @file WebGlPipeline.hpp
/// @brief WebGL2レンダリングパイプライン状態実装
/// @details ブレンド・深度テスト・ラスタライザ・VAO（頂点属性）を統合管理する。
///          WebGL2ではPSOが存在しないため、bind()時にGL状態を直接設定する。

#ifdef __EMSCRIPTEN__

#include <cstdint>
#include <stdexcept>

#include <GLES3/gl3.h>

#include <mitiru/gfx/GfxTypes.hpp>
#include <mitiru/gfx/IPipeline.hpp>
#include <mitiru/gfx/webgl/WebGlShader.hpp>

namespace mitiru::gfx
{

/// @brief WebGL2パイプライン記述子
/// @details パイプライン生成に必要なパラメータを集約する。
struct WebGLPipelineDesc
{
	WebGLShader* shader = nullptr;            ///< リンク済みシェーダープログラム
	VertexFormat vertexFormat = VertexFormat::PositionTexCoord; ///< 頂点フォーマット
	BlendMode blendMode = BlendMode::Alpha;   ///< ブレンドモード
	bool depthTest = true;                    ///< 深度テスト有効化
	bool depthWrite = true;                   ///< 深度書き込み有効化
	bool cullFace = false;                    ///< 背面カリング有効化
	bool wireframe = false;                   ///< ワイヤーフレーム表示（WebGL2未対応のためヒントのみ）
};

/// @brief WebGL2用レンダリングパイプライン状態実装
/// @details シェーダープログラム・VAO・ブレンド設定・深度設定を束ねる。
///          bind()でOpenGL ES状態マシンに一括適用する。
///
/// @code
/// WebGLPipelineDesc desc;
/// desc.shader = &myShader;
/// desc.blendMode = BlendMode::Alpha;
/// auto pipeline = WebGLPipeline(desc);
/// pipeline.bind();
/// @endcode
class WebGLPipeline final : public IPipeline
{
public:
	/// @brief コンストラクタ
	/// @param desc パイプライン記述子
	explicit WebGLPipeline(const WebGLPipelineDesc& desc)
		: m_shader(desc.shader)
		, m_blendMode(desc.blendMode)
		, m_depthTest(desc.depthTest)
		, m_depthWrite(desc.depthWrite)
		, m_cullFace(desc.cullFace)
		, m_vertexFormat(desc.vertexFormat)
	{
		if (!m_shader || !m_shader->isValid())
		{
			throw std::runtime_error("WebGLPipeline: null or invalid shader");
		}

		createVAO();
		m_valid = true;
	}

	~WebGLPipeline() override
	{
		if (m_vao != 0)
		{
			glDeleteVertexArrays(1, &m_vao);
		}
	}

	WebGLPipeline(const WebGLPipeline&) = delete;
	WebGLPipeline& operator=(const WebGLPipeline&) = delete;

	WebGLPipeline(WebGLPipeline&& other) noexcept
		: m_shader(other.m_shader)
		, m_vao(other.m_vao)
		, m_blendMode(other.m_blendMode)
		, m_depthTest(other.m_depthTest)
		, m_depthWrite(other.m_depthWrite)
		, m_cullFace(other.m_cullFace)
		, m_vertexFormat(other.m_vertexFormat)
		, m_valid(other.m_valid)
	{
		other.m_vao = 0;
		other.m_valid = false;
	}

	WebGLPipeline& operator=(WebGLPipeline&& other) noexcept
	{
		if (this != &other)
		{
			if (m_vao != 0)
			{
				glDeleteVertexArrays(1, &m_vao);
			}

			m_shader = other.m_shader;
			m_vao = other.m_vao;
			m_blendMode = other.m_blendMode;
			m_depthTest = other.m_depthTest;
			m_depthWrite = other.m_depthWrite;
			m_cullFace = other.m_cullFace;
			m_vertexFormat = other.m_vertexFormat;
			m_valid = other.m_valid;

			other.m_vao = 0;
			other.m_valid = false;
		}
		return *this;
	}

	/// @brief パイプラインが有効かどうかを判定する
	[[nodiscard]] bool isValid() const noexcept override { return m_valid; }

	/// @brief シェーダープログラムを取得する
	[[nodiscard]] WebGLShader* shader() const noexcept { return m_shader; }

	/// @brief VAOハンドルを取得する
	[[nodiscard]] GLuint vao() const noexcept { return m_vao; }

	/// @brief パイプライン状態をWebGL2コンテキストにバインドする
	/// @details シェーダーの使用、VAOのバインド、ブレンド・深度・カリング設定を適用する。
	void bind() const noexcept
	{
		if (!m_valid)
		{
			return;
		}

		/// シェーダープログラムをアクティブにする
		m_shader->use();

		/// VAOをバインドする
		glBindVertexArray(m_vao);

		/// ブレンド設定を適用する
		applyBlendMode();

		/// 深度テスト設定を適用する
		applyDepthState();

		/// カリング設定を適用する
		applyCullState();
	}

	/// @brief パイプラインバインドを解除する
	static void unbind() noexcept
	{
		glBindVertexArray(0);
		glUseProgram(0);
	}

private:
	/// @brief 頂点フォーマットに対応するVAOを生成する
	void createVAO()
	{
		glGenVertexArrays(1, &m_vao);
		if (m_vao == 0)
		{
			throw std::runtime_error("WebGLPipeline: glGenVertexArrays failed");
		}

		glBindVertexArray(m_vao);

		/// 頂点フォーマットに応じた頂点属性を設定する
		/// 実際のバッファバインドはdraw時に行うが、属性レイアウトはVAOに記録する
		switch (m_vertexFormat)
		{
		case VertexFormat::Position2D:
			/// aPos: vec2 (location=0)
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
				2 * sizeof(float), nullptr);
			break;

		case VertexFormat::Position3D:
			/// aPos: vec3 (location=0)
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
				3 * sizeof(float), nullptr);
			break;

		case VertexFormat::PositionColor:
			/// aPos: vec2 (location=0) + aColor: vec4 (location=2)
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
				6 * sizeof(float), nullptr);
			glEnableVertexAttribArray(2);
			glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE,
				6 * sizeof(float),
				reinterpret_cast<const void*>(2 * sizeof(float)));
			break;

		case VertexFormat::PositionTexCoord:
			/// aPos: vec2 (location=0) + aTexCoord: vec2 (location=1) + aColor: vec4 (location=2)
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
				8 * sizeof(float), nullptr);
			glEnableVertexAttribArray(1);
			glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
				8 * sizeof(float),
				reinterpret_cast<const void*>(2 * sizeof(float)));
			glEnableVertexAttribArray(2);
			glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE,
				8 * sizeof(float),
				reinterpret_cast<const void*>(4 * sizeof(float)));
			break;
		}

		glBindVertexArray(0);
	}

	/// @brief ブレンドモードを適用する
	void applyBlendMode() const noexcept
	{
		switch (m_blendMode)
		{
		case BlendMode::None:
			glDisable(GL_BLEND);
			break;

		case BlendMode::Alpha:
			glEnable(GL_BLEND);
			glBlendFuncSeparate(
				GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
				GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			glBlendEquation(GL_FUNC_ADD);
			break;

		case BlendMode::Additive:
			glEnable(GL_BLEND);
			glBlendFuncSeparate(
				GL_SRC_ALPHA, GL_ONE,
				GL_ONE, GL_ONE);
			glBlendEquation(GL_FUNC_ADD);
			break;

		case BlendMode::Multiplicative:
			glEnable(GL_BLEND);
			glBlendFuncSeparate(
				GL_DST_COLOR, GL_ZERO,
				GL_DST_ALPHA, GL_ZERO);
			glBlendEquation(GL_FUNC_ADD);
			break;

		// 以下 3 つは Dx11Pipeline と同じ近似式にする。ここが抜けていると
		// glBlendFunc が前の描画のまま残り、同じ絵が backend で変わる。
		case BlendMode::Screen:
			// 1 - (1-src)*(1-dst) を src*1 + dst*(1-src) で近似する。
			glEnable(GL_BLEND);
			glBlendFuncSeparate(
				GL_ONE, GL_ONE_MINUS_SRC_COLOR,
				GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			glBlendEquation(GL_FUNC_ADD);
			break;

		case BlendMode::Overlay:
			glEnable(GL_BLEND);
			glBlendFuncSeparate(
				GL_ONE, GL_SRC_COLOR,
				GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			glBlendEquation(GL_FUNC_ADD);
			break;

		case BlendMode::ColorDodge:
			// dst / (1-src) を加算とアルファで近似する。
			glEnable(GL_BLEND);
			glBlendFuncSeparate(
				GL_SRC_ALPHA, GL_ONE,
				GL_ONE, GL_ONE);
			glBlendEquation(GL_FUNC_ADD);
			break;
		}
	}

	/// @brief 深度テスト状態を適用する
	void applyDepthState() const noexcept
	{
		if (m_depthTest)
		{
			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_LEQUAL);
		}
		else
		{
			glDisable(GL_DEPTH_TEST);
		}

		glDepthMask(m_depthWrite ? GL_TRUE : GL_FALSE);
	}

	/// @brief カリング状態を適用する
	void applyCullState() const noexcept
	{
		if (m_cullFace)
		{
			glEnable(GL_CULL_FACE);
			glCullFace(GL_BACK);
			glFrontFace(GL_CCW);
		}
		else
		{
			glDisable(GL_CULL_FACE);
		}
	}

	WebGLShader* m_shader = nullptr;    ///< シェーダープログラム（非所有）
	GLuint m_vao = 0;                   ///< 頂点配列オブジェクト
	BlendMode m_blendMode = BlendMode::Alpha;  ///< ブレンドモード
	bool m_depthTest = true;            ///< 深度テスト有効フラグ
	bool m_depthWrite = true;           ///< 深度書き込み有効フラグ
	bool m_cullFace = false;            ///< 背面カリング有効フラグ
	VertexFormat m_vertexFormat = VertexFormat::PositionTexCoord; ///< 頂点フォーマット
	bool m_valid = false;               ///< 有効フラグ
};

} // namespace mitiru::gfx

#endif // __EMSCRIPTEN__
