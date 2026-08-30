#pragma once

/// @file RenderPipeline2D_impl.hpp
/// @brief RenderPipeline2D の実装本体（RenderPipeline2D.hpp から機械的分割）

#include <mitiru/render/RenderPipeline2D.hpp>

namespace mitiru::render
{

/// @brief 頂点・インデックスデータをGPUに送信して描画する
inline void RenderPipeline2D::submitBatch(const std::vector<Vertex2D>& vertices,
                                          const std::vector<std::uint32_t>& indices)
{
	if (!m_valid || vertices.empty() || indices.empty())
	{
		return;
	}

#ifdef _WIN32
	if (m_useDx12Path)
	{
		submitBatchDx12(vertices, indices);
		return;
	}
#endif

	if (m_useGenericPath)
	{
		submitBatchGeneric(vertices, indices);
		return;
	}

#ifdef _WIN32
	submitBatchDx11(vertices, indices);
#endif
}

/// @brief SDF矩形バッチをGPUに送信して描画する
inline void RenderPipeline2D::submitStyledRectBatch(
	const std::vector<StyledVertex2D>& vertices,
	const std::vector<std::uint32_t>& indices,
	const StyleConstants& style)
{
	if (!m_valid || vertices.empty() || indices.empty())
	{
		return;
	}

#ifdef _WIN32
	if (m_useDx12Path)
	{
		submitStyledBatchDx12(
			vertices, indices, style,
			SDF_RECT_VS, SDF_RECT_PS,
			m_dx12SdfRectPso);
		return;
	}
#endif

#ifdef _WIN32
	if (!m_useGenericPath && !m_useDx12Path)
	{
		submitStyledBatchDx11(
			vertices, indices, style,
			SDF_RECT_VS, SDF_RECT_PS,
			m_sdfRectVS, m_sdfRectPS, m_sdfRectPipeline);
	}
#endif
}

/// @brief SDF円/楕円バッチをGPUに送信して描画する
inline void RenderPipeline2D::submitStyledCircleBatch(
	const std::vector<StyledVertex2D>& vertices,
	const std::vector<std::uint32_t>& indices,
	const StyleConstants& style)
{
	if (!m_valid || vertices.empty() || indices.empty())
	{
		return;
	}

#ifdef _WIN32
	if (m_useDx12Path)
	{
		submitStyledBatchDx12(
			vertices, indices, style,
			SDF_CIRCLE_VS, SDF_CIRCLE_PS,
			m_dx12SdfCirclePso);
		return;
	}
#endif

#ifdef _WIN32
	if (!m_useGenericPath && !m_useDx12Path)
	{
		submitStyledBatchDx11(
			vertices, indices, style,
			SDF_CIRCLE_VS, SDF_CIRCLE_PS,
			m_sdfCircleVS, m_sdfCirclePS, m_sdfCirclePipeline);
	}
#endif
}

/// @brief スクリーンサイズ変更時に正射影行列を更新する
inline void RenderPipeline2D::resize(float width, float height)
{
	m_screenWidth = width;
	m_screenHeight = height;

	if (m_useGenericPath && m_genConstBuffer)
	{
		const auto ortho = OrthoMatrix::create(width, height);
		m_genConstBuffer->update(ortho.m, sizeof(ortho.m));
	}
#ifdef _WIN32
	if (!m_useGenericPath && !m_useDx12Path && m_dx11Context && m_constantBuffer)
	{
		const auto ortho = OrthoMatrix::create(width, height);
		m_constantBuffer->update(
			m_dx11Context, ortho.m, sizeof(ortho.m));
	}

	if (m_useDx12Path && m_dx12VsCb)
	{
		// 共有 projection CB を書く前に全 in-flight を drain する
		// (in-flight submit が旧 CB を読んでいる最中の上書きを防ぐ)。
		waitDx12Fence();
		// CRITICAL: 実 runtime の VS constant buffer は m_dx12VsCb。
		// m_dx12ConstantBuffer は "エイリアス用" コメントの dead pointer
		// (init で populate されない)。そっちを update してた古い resize
		// は ortho 更新が runtime に届かず、resize 後に anisotropic
		// stretch が発生する。
		const auto ortho = OrthoMatrix::create(width, height);
		updateCbDx12(m_dx12VsCb.Get(), ortho.m, sizeof(ortho.m));
	}
#endif
}

/// @brief 抽象IDeviceから2Dパイプラインを構築する
inline RenderPipeline2D RenderPipeline2D::createFromDevice(
	gfx::IDevice* device,
	float screenWidth,
	float screenHeight)
{
	if (!device)
	{
		return {};
	}

	RenderPipeline2D pipeline;
	pipeline.m_screenWidth = screenWidth;
	pipeline.m_screenHeight = screenHeight;
	pipeline.m_useGenericPath = true;
	pipeline.m_genDevice = device;

	/// 定数バッファ（正射影行列）を生成する
	const auto ortho = OrthoMatrix::create(screenWidth, screenHeight);
	pipeline.m_genConstBuffer = device->createBuffer(
		gfx::BufferType::Constant,
		sizeof(ortho.m),
		true,
		ortho.m);

	/// 動的頂点バッファを生成する（初期サイズ64KB）
	constexpr std::uint32_t INITIAL_VB_SIZE = 65536;
	pipeline.m_genVertexBuffer = device->createBuffer(
		gfx::BufferType::Vertex,
		INITIAL_VB_SIZE,
		true);
	pipeline.m_genVbCapacity = INITIAL_VB_SIZE;

	/// 動的インデックスバッファを生成する（初期サイズ32KB）
	constexpr std::uint32_t INITIAL_IB_SIZE = 32768;
	pipeline.m_genIndexBuffer = device->createBuffer(
		gfx::BufferType::Index,
		INITIAL_IB_SIZE,
		true);
	pipeline.m_genIbCapacity = INITIAL_IB_SIZE;

	/// コマンドリストを生成する
	pipeline.m_genCommandList = device->createCommandList();

#ifdef __EMSCRIPTEN__
	/// WebGL: 2Dシェーダープログラムを作成する
	pipeline.m_glShader = std::make_unique<gfx::WebGLShader>(
		gfx::WebGLShader::createProgram(
			gfx::WEBGL_VERTEX_SHADER_2D,
			gfx::WEBGL_FRAGMENT_SHADER_2D));
	pipeline.m_glProgram = pipeline.m_glShader->program();
	pipeline.m_glProjLoc = glGetUniformLocation(pipeline.m_glProgram, "uProjection");
	pipeline.m_glUseTexLoc = glGetUniformLocation(pipeline.m_glProgram, "uUseTexture");

	/// VAOを作成し頂点アトリビュートを設定する
	glGenVertexArrays(1, &pipeline.m_glVAO);
	glBindVertexArray(pipeline.m_glVAO);

	auto* vb = dynamic_cast<gfx::WebGLBuffer*>(pipeline.m_genVertexBuffer.get());
	auto* ib = dynamic_cast<gfx::WebGLBuffer*>(pipeline.m_genIndexBuffer.get());
	if (vb) glBindBuffer(GL_ARRAY_BUFFER, vb->handle());
	if (ib) glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->handle());

	/// Vertex2D: pos(vec2) + texCoord(vec2) + color(vec4) = 32 bytes
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
		reinterpret_cast<void*>(offsetof(Vertex2D, position)));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
		reinterpret_cast<void*>(offsetof(Vertex2D, texCoord)));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
		reinterpret_cast<void*>(offsetof(Vertex2D, color)));

	glBindVertexArray(0);

	/// アルファブレンドを有効化する
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#endif

	pipeline.m_valid = true;
	return pipeline;
}

/// @brief 抽象インターフェース経由でバッチ描画を実行する
inline void RenderPipeline2D::submitBatchGeneric(
	const std::vector<Vertex2D>& vertices,
	const std::vector<std::uint32_t>& indices)
{
	if (!m_genDevice || !m_genCommandList)
	{
		return;
	}

	const auto vbSize = static_cast<std::uint32_t>(
		vertices.size() * sizeof(Vertex2D));
	const auto ibSize = static_cast<std::uint32_t>(
		indices.size() * sizeof(std::uint32_t));

	/// バッファサイズが不足していたら再生成する
	if (vbSize > m_genVbCapacity)
	{
		const auto newCapacity = std::max(
			vbSize, m_genVbCapacity * 2);
		m_genVertexBuffer = m_genDevice->createBuffer(
			gfx::BufferType::Vertex, newCapacity, true);
		m_genVbCapacity = newCapacity;
	}

	if (ibSize > m_genIbCapacity)
	{
		const auto newCapacity = std::max(
			ibSize, m_genIbCapacity * 2);
		m_genIndexBuffer = m_genDevice->createBuffer(
			gfx::BufferType::Index, newCapacity, true);
		m_genIbCapacity = newCapacity;
	}

	/// バッファを更新する
	m_genVertexBuffer->update(vertices.data(), vbSize);
	m_genIndexBuffer->update(indices.data(), ibSize);

	/// 描画コマンドを発行する
#ifdef __EMSCRIPTEN__
	/// WebGL: 2D描画では深度テストを無効化し、ビューポートを設定する
	glDisable(GL_DEPTH_TEST);
	glViewport(0, 0,
		static_cast<GLsizei>(m_screenWidth),
		static_cast<GLsizei>(m_screenHeight));
	glUseProgram(m_glProgram);
	if (m_glProjLoc >= 0 && m_genConstBuffer)
	{
		const auto ortho = OrthoMatrix::create(m_screenWidth, m_screenHeight);
		/// OrthoMatrix は column-major で構築済み → GL_FALSE（転置不要）
		glUniformMatrix4fv(m_glProjLoc, 1, GL_FALSE, &ortho.m[0][0]);
	}
	if (m_glUseTexLoc >= 0)
	{
		glUniform1i(m_glUseTexLoc, 0); // テクスチャ未使用
	}

	// 2D は毎回この状態で描く。生成時に一度だけ設定していると、同じフレームの
	// 前半で走る 3D パスが glDisable(GL_BLEND) した状態を引き継ぎ、
	// アルファが効かなくなる (足元の楕円の影が真っ黒な塊になった)。
	applyGlBlendMode();
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	glBindVertexArray(m_glVAO);

	/// VB/IBを再バインド（動的再生成されている可能性があるため）
	auto* vb = dynamic_cast<gfx::WebGLBuffer*>(m_genVertexBuffer.get());
	auto* ib = dynamic_cast<gfx::WebGLBuffer*>(m_genIndexBuffer.get());
	if (vb) glBindBuffer(GL_ARRAY_BUFFER, vb->handle());
	if (ib) glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->handle());

	/// 頂点アトリビュートを再設定（バッファ再生成時に必要）
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
		reinterpret_cast<void*>(offsetof(Vertex2D, position)));
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
		reinterpret_cast<void*>(offsetof(Vertex2D, texCoord)));
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
		reinterpret_cast<void*>(offsetof(Vertex2D, color)));

	glDrawElements(GL_TRIANGLES,
		static_cast<GLsizei>(indices.size()),
		GL_UNSIGNED_INT, nullptr);

	glBindVertexArray(0);
	glUseProgram(0);
	/// 深度テストを復元する（3D描画に必要）
	glEnable(GL_DEPTH_TEST);
#else
	m_genCommandList->begin();
	m_genCommandList->setViewport(viewportWidth(), viewportHeight());
	m_genCommandList->setVertexBuffer(m_genVertexBuffer.get());
	m_genCommandList->setIndexBuffer(m_genIndexBuffer.get());
	m_genCommandList->drawIndexed(
		static_cast<std::uint32_t>(indices.size()), 0, 0);
	m_genCommandList->end();
#endif
}

#ifdef __EMSCRIPTEN__
/// @brief 覚えてある合成モードを GL の状態へ反映する。
/// @details 描画のたびに呼ぶ。生成時に 1 回だけ設定すると、同じフレームの前半で
///          走る 3D パスが glDisable(GL_BLEND) した状態を引き継いでしまう。
inline void RenderPipeline2D::applyGlBlendMode() const noexcept
{
	if (m_glBlendMode == gfx::BlendMode::None)
	{
		glDisable(GL_BLEND);
		return;
	}
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	switch (m_glBlendMode)
	{
	case gfx::BlendMode::Additive:
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
		break;
	case gfx::BlendMode::Multiplicative:
		glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_DST_ALPHA, GL_ZERO);
		break;
	case gfx::BlendMode::Screen:
		glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		break;
	case gfx::BlendMode::Overlay:
		glBlendFuncSeparate(GL_ONE, GL_SRC_COLOR, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		break;
	case gfx::BlendMode::ColorDodge:
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
		break;
	default:  // Alpha。Dx11Pipeline の同名と同じ係数。
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		break;
	}
}
#endif

/// @brief ブレンドモードを変更する
inline void RenderPipeline2D::setBlendMode([[maybe_unused]] gfx::BlendMode mode)
{
#ifdef __EMSCRIPTEN__
	// WebGL は PSO を持たないので、次の描画で使う状態として覚えておくだけ。
	// ここが no-op のままだと、ゲームが指定した合成が黙って無視される。
	m_glBlendMode = mode;
#endif
#ifdef _WIN32
	if (!m_valid || m_useGenericPath || !m_dx11Device) return;

	gfx::Dx11PipelineDesc pipeDesc;
	pipeDesc.vertexShader = m_vertexShader.get();
	pipeDesc.pixelShader = m_pixelShader.get();
	pipeDesc.blendMode = mode;
	pipeDesc.scissorEnable = hasScissor();
	m_pipeline = std::make_unique<gfx::Dx11Pipeline>(m_dx11Device, pipeDesc);
#endif
}

/// @brief シザー矩形をプッシュする（クリッピング開始）
inline void RenderPipeline2D::pushScissorRect(const sgc::Rectf& rect)
{
	m_scissorStack.push(rect);
	applyScissorRect(rect);
}

/// @brief シザー矩形をポップする（前のクリップに戻す）
inline void RenderPipeline2D::popScissorRect()
{
	if (m_scissorStack.empty()) return;
	m_scissorStack.pop();
	if (!m_scissorStack.empty())
	{
		applyScissorRect(m_scissorStack.top());
	}
	else
	{
		resetScissorRect();
	}
}

/// @brief シザー矩形を適用する
inline void RenderPipeline2D::applyScissorRect(const sgc::Rectf& rect)
{
#ifdef _WIN32
	if (m_commandList)
	{
		m_commandList->setScissorRect(
			static_cast<int>(rect.x()),
			static_cast<int>(rect.y()),
			static_cast<int>(rect.width()),
			static_cast<int>(rect.height()));
	}
#endif
}

/// @brief シザー矩形をリセットする
inline void RenderPipeline2D::resetScissorRect()
{
#ifdef _WIN32
	if (m_commandList)
	{
		m_commandList->resetScissorRect(
			static_cast<int>(m_screenWidth),
			static_cast<int>(m_screenHeight));
	}
#endif
}

} // namespace mitiru::render
