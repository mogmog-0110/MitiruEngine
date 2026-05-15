#pragma once

/// @file Sprite3DRenderer.hpp
/// @brief 2.5D スプライトレンダラー（WebGL2）
/// @details Sprite3Dインスタンスをバッチ描画する。
///          Camera3Dのビュー射影行列と組み合わせてMVPを計算し、
///          アルファクリッピングシェーダーで描画する。

#ifdef __EMSCRIPTEN__

#include <algorithm>
#include <cstdint>
#include <vector>

#include <string>

#include <GLES3/gl3.h>
#include <stb_image.h>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>

#include <mitiru/gfx/webgl/WebGlShader.hpp>
#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/Sprite3D.hpp>

namespace mitiru::render
{

/// @brief 2.5Dスプライトレンダラー
/// @details WebGL2上でSprite3Dインスタンスを描画する。
///          init()で初期化、submit()でインスタンスを追加、
///          flush()で一括描画する。
class Sprite3DRenderer
{
public:
	/// @brief 初期化（シェーダーコンパイル、バッファ生成）
	/// @return 成功時true
	bool init()
	{
		/// テクスチャ付きシェーダー
		m_shader = std::make_unique<gfx::WebGLShader>(
			gfx::WebGLShader::createProgram(
				SPRITE3D_VERTEX_SHADER,
				SPRITE3D_FRAGMENT_SHADER));

		/// 単色シェーダー（テクスチャなし）
		m_flatShader = std::make_unique<gfx::WebGLShader>(
			gfx::WebGLShader::createProgram(
				SPRITE3D_VERTEX_SHADER,
				SPRITE3D_FLAT_FRAGMENT_SHADER));

		/// Uniform locations
		m_locMVP = glGetUniformLocation(m_shader->program(), "uMVP");
		m_locTint = glGetUniformLocation(m_shader->program(), "uTint");
		m_locAlphaClip = glGetUniformLocation(m_shader->program(), "uAlphaClip");
		m_locTexture = glGetUniformLocation(m_shader->program(), "uTexture");

		m_flatLocMVP = glGetUniformLocation(m_flatShader->program(), "uMVP");
		m_flatLocTint = glGetUniformLocation(m_flatShader->program(), "uTint");
		m_flatLocAlphaClip = glGetUniformLocation(m_flatShader->program(), "uAlphaClip");

		/// Quad頂点バッファ（全スプライト共通、BottomCenterピボット）
		const auto quad = QuadMesh::create(1.0f, 1.0f, SpritePivot::BottomCenter);

		glGenVertexArrays(1, &m_vao);
		glBindVertexArray(m_vao);

		/// VBO
		glGenBuffers(1, &m_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
		glBufferData(GL_ARRAY_BUFFER,
			static_cast<GLsizeiptr>(quad.vertices.size() * sizeof(Vertex3DSprite)),
			quad.vertices.data(), GL_STATIC_DRAW);

		/// position (vec3) at location 0
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3DSprite),
			reinterpret_cast<void*>(offsetof(Vertex3DSprite, position)));

		/// texCoord (vec2) at location 1
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex3DSprite),
			reinterpret_cast<void*>(offsetof(Vertex3DSprite, texCoord)));

		/// IBO
		glGenBuffers(1, &m_ibo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER,
			static_cast<GLsizeiptr>(quad.indices.size() * sizeof(std::uint32_t)),
			quad.indices.data(), GL_STATIC_DRAW);

		glBindVertexArray(0);

		/// Centerピボット用のVAO
		const auto quadCenter = QuadMesh::create(1.0f, 1.0f, SpritePivot::Center);
		glGenVertexArrays(1, &m_vaoCentered);
		glBindVertexArray(m_vaoCentered);

		glGenBuffers(1, &m_vboCentered);
		glBindBuffer(GL_ARRAY_BUFFER, m_vboCentered);
		glBufferData(GL_ARRAY_BUFFER,
			static_cast<GLsizeiptr>(quadCenter.vertices.size() * sizeof(Vertex3DSprite)),
			quadCenter.vertices.data(), GL_STATIC_DRAW);

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3DSprite),
			reinterpret_cast<void*>(offsetof(Vertex3DSprite, position)));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex3DSprite),
			reinterpret_cast<void*>(offsetof(Vertex3DSprite, texCoord)));

		glGenBuffers(1, &m_iboCentered);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_iboCentered);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER,
			static_cast<GLsizeiptr>(quadCenter.indices.size() * sizeof(std::uint32_t)),
			quadCenter.indices.data(), GL_STATIC_DRAW);

		glBindVertexArray(0);

		m_initialized = true;
		return true;
	}

	/// @brief スプライトインスタンスを描画キューに追加する
	/// @param instance スプライトインスタンス
	/// @param textureId OpenGLテクスチャID（0=テクスチャなし/単色）
	void submit(const Sprite3DInstance& instance, GLuint textureId = 0)
	{
		m_queue.push_back({instance, textureId});
	}

	/// @brief キューに溜まったスプライトを一括描画する
	/// @param camera カメラ（ビュー射影行列の取得用）
	void flush(const Camera3D& camera)
	{
		if (!m_initialized || m_queue.empty())
		{
			return;
		}

		const auto vp = camera.viewProjectionMatrix();
		const auto cameraPos = camera.position();

		/// カメラの right/up ベクトル（完全ビルボード用）
		const auto camTarget = camera.target();
		const auto camUp = camera.up();
		auto fwd = sgc::Vec3f{
			camTarget.x - cameraPos.x,
			camTarget.y - cameraPos.y,
			camTarget.z - cameraPos.z};
		const float fwdLen = std::sqrt(fwd.x*fwd.x + fwd.y*fwd.y + fwd.z*fwd.z);
		if (fwdLen > 0.001f) { fwd.x /= fwdLen; fwd.y /= fwdLen; fwd.z /= fwdLen; }
		/// right = fwd × up
		sgc::Vec3f camRight{
			fwd.y * camUp.z - fwd.z * camUp.y,
			fwd.z * camUp.x - fwd.x * camUp.z,
			fwd.x * camUp.y - fwd.y * camUp.x};
		const float rLen = std::sqrt(camRight.x*camRight.x + camRight.y*camRight.y + camRight.z*camRight.z);
		if (rLen > 0.001f) { camRight.x /= rLen; camRight.y /= rLen; camRight.z /= rLen; }
		/// trueUp = right × fwd
		const sgc::Vec3f trueUp{
			camRight.y * fwd.z - camRight.z * fwd.y,
			camRight.z * fwd.x - camRight.x * fwd.z,
			camRight.x * fwd.y - camRight.y * fwd.x};

		/// Zバッファ有効（奥行き順を正しく処理）
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glDepthMask(GL_TRUE);

		/// 両面描画（板ポリゴンは裏からも見える）
		glDisable(GL_CULL_FACE);

		/// アルファブレンド有効
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		for (const auto& entry : m_queue)
		{
			const auto& inst = entry.instance;
			const bool hasTexture = (entry.textureId != 0);

			/// シェーダー選択
			const GLuint program = hasTexture
				? m_shader->program()
				: m_flatShader->program();
			glUseProgram(program);

			/// ワールド行列を計算する（ビルボード時は完全正対）
			const auto world = inst.worldMatrix(cameraPos, &camRight, &trueUp);

			/// スプライトのサイズをスケールとして適用
			const auto sizeScale = sgc::Mat4f::scaling(
				{inst.width, inst.height, 1.0f});
			const auto mvp = vp * world * sizeScale;

			/// MVP行列を送信する（row-major → GL_TRUE で転置送信）
			const GLint mvpLoc = hasTexture ? m_locMVP : m_flatLocMVP;
			glUniformMatrix4fv(mvpLoc, 1, GL_TRUE, &mvp.m[0][0]);

			/// ティント色
			const GLint tintLoc = hasTexture ? m_locTint : m_flatLocTint;
			glUniform4f(tintLoc,
				inst.tint.x, inst.tint.y, inst.tint.z, inst.tint.w);

			/// アルファクリッピング閾値
			const GLint clipLoc = hasTexture ? m_locAlphaClip : m_flatLocAlphaClip;
			glUniform1f(clipLoc, inst.alphaClip);

			/// テクスチャバインド
			if (hasTexture)
			{
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, entry.textureId);
				glUniform1i(m_locTexture, 0);
			}

			/// VAO選択（ピボットに応じて）
			const GLuint vao = (inst.pivot == SpritePivot::BottomCenter)
				? m_vao : m_vaoCentered;
			glBindVertexArray(vao);

			/// 描画
			glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
		}

		/// 後片付け
		glBindVertexArray(0);
		glUseProgram(0);

		m_queue.clear();
	}

	/// @brief PNGファイルからWebGLテクスチャを生成する
	/// @param path ファイルパス（Emscripten仮想FS上）
	/// @return OpenGLテクスチャID（失敗時0）
	[[nodiscard]] GLuint loadTexture(const std::string& path)
	{
		int w = 0, h = 0, ch = 0;
		stbi_set_flip_vertically_on_load(0); // QuadメッシュのUVが既にY反転済み
		auto* data = stbi_load(path.c_str(), &w, &h, &ch, 4); // RGBA強制
		if (!data)
		{
			return 0;
		}

		GLuint tex = 0;
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
			GL_RGBA, GL_UNSIGNED_BYTE, data);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glBindTexture(GL_TEXTURE_2D, 0);
		stbi_image_free(data);

		return tex;
	}

	/// @brief 初期化済みかどうか
	[[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

private:
	struct QueueEntry
	{
		Sprite3DInstance instance;
		GLuint textureId;
	};

	std::unique_ptr<gfx::WebGLShader> m_shader;
	std::unique_ptr<gfx::WebGLShader> m_flatShader;

	GLuint m_vao = 0;         ///< BottomCenterピボット用VAO
	GLuint m_vbo = 0;
	GLuint m_ibo = 0;
	GLuint m_vaoCentered = 0; ///< Centerピボット用VAO
	GLuint m_vboCentered = 0;
	GLuint m_iboCentered = 0;

	/// テクスチャ付きシェーダーのuniform locations
	GLint m_locMVP = -1;
	GLint m_locTint = -1;
	GLint m_locAlphaClip = -1;
	GLint m_locTexture = -1;

	/// 単色シェーダーのuniform locations
	GLint m_flatLocMVP = -1;
	GLint m_flatLocTint = -1;
	GLint m_flatLocAlphaClip = -1;

	bool m_initialized = false;
	std::vector<QueueEntry> m_queue;
};

} // namespace mitiru::render

#endif // __EMSCRIPTEN__
