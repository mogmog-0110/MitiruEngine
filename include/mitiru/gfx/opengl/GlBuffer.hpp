#pragma once

/// @file GlBuffer.hpp
/// @brief OpenGL GPUバッファ実装
/// @details GLuintバッファオブジェクトをラップし、IBufferインターフェースを実装する。

#ifdef MITIRU_HAS_OPENGL

#include <cstdint>
#include <cstring>

#ifdef MITIRU_HAS_SDL2
#include <SDL2/SDL_opengl.h>
#elif defined(MITIRU_HAS_GLFW)
#include <GL/glew.h>
#endif

#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/opengl/GlFunctions.hpp>

namespace mitiru::gfx
{

/// @brief OpenGL GPUバッファ実装
/// @details 頂点バッファ・インデックスバッファ・定数バッファをGL bufferでラップする。
class GlBuffer final : public IBuffer
{
public:
	/// @brief コンストラクタ
	/// @param gl GL関数ポインタ群
	/// @param bufferType バッファ種別
	/// @param sizeBytes バッファサイズ（バイト）
	/// @param dynamic 動的更新が必要か
	/// @param initialData 初期データ（nullptrで初期化なし）
	GlBuffer(GlFunctions* gl,
	         BufferType bufferType,
	         std::uint32_t sizeBytes,
	         bool dynamic,
	         const void* initialData = nullptr)
		: m_gl(gl)
		, m_type(bufferType)
		, m_sizeBytes(sizeBytes)
		, m_dynamic(dynamic)
	{
		m_glTarget = toGlTarget(bufferType);

		m_gl->genBuffers(1, &m_buffer);
		m_gl->bindBuffer(m_glTarget, m_buffer);
		m_gl->bufferData(
			m_glTarget,
			static_cast<GLsizeiptr>(sizeBytes),
			initialData,
			dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
		m_gl->bindBuffer(m_glTarget, 0);
	}

	/// @brief デストラクタ
	~GlBuffer() override
	{
		if (m_buffer != 0 && m_gl && m_gl->deleteBuffers)
		{
			m_gl->deleteBuffers(1, &m_buffer);
			m_buffer = 0;
		}
	}

	/// コピー禁止
	GlBuffer(const GlBuffer&) = delete;
	GlBuffer& operator=(const GlBuffer&) = delete;

	/// ムーブ
	GlBuffer(GlBuffer&& other) noexcept
		: m_gl(other.m_gl)
		, m_buffer(other.m_buffer)
		, m_glTarget(other.m_glTarget)
		, m_type(other.m_type)
		, m_sizeBytes(other.m_sizeBytes)
		, m_dynamic(other.m_dynamic)
	{
		other.m_buffer = 0;
	}

	GlBuffer& operator=(GlBuffer&& other) noexcept
	{
		if (this != &other)
		{
			if (m_buffer != 0 && m_gl && m_gl->deleteBuffers)
			{
				m_gl->deleteBuffers(1, &m_buffer);
			}
			m_gl = other.m_gl;
			m_buffer = other.m_buffer;
			m_glTarget = other.m_glTarget;
			m_type = other.m_type;
			m_sizeBytes = other.m_sizeBytes;
			m_dynamic = other.m_dynamic;
			other.m_buffer = 0;
		}
		return *this;
	}

	/// @brief バッファデータを更新する
	/// @param data 書き込むデータへのポインタ
	/// @param sizeBytes 書き込みサイズ（バイト）
	void update(const void* data, std::uint32_t sizeBytes) override
	{
		if (!m_dynamic || !data || sizeBytes > m_sizeBytes || m_buffer == 0)
		{
			return;
		}

		m_gl->bindBuffer(m_glTarget, m_buffer);
		m_gl->bufferSubData(
			m_glTarget,
			0,
			static_cast<GLsizeiptr>(sizeBytes),
			data);
		m_gl->bindBuffer(m_glTarget, 0);
	}

	/// @brief バッファサイズを取得する
	[[nodiscard]] std::uint32_t size() const noexcept override
	{
		return m_sizeBytes;
	}

	/// @brief バッファ種別を取得する
	[[nodiscard]] BufferType type() const noexcept override
	{
		return m_type;
	}

	/// @brief GLバッファハンドルを取得する
	[[nodiscard]] GLuint glBuffer() const noexcept
	{
		return m_buffer;
	}

	/// @brief GLバッファターゲットを取得する
	[[nodiscard]] GLenum glTarget() const noexcept
	{
		return m_glTarget;
	}

private:
	/// @brief BufferTypeからGLバッファターゲットに変換する
	static GLenum toGlTarget(BufferType bt) noexcept
	{
		switch (bt)
		{
		case BufferType::Vertex:   return GL_ARRAY_BUFFER;
		case BufferType::Index:    return GL_ELEMENT_ARRAY_BUFFER;
		case BufferType::Constant: return GL_UNIFORM_BUFFER;
		}
		return GL_ARRAY_BUFFER;
	}

	GlFunctions* m_gl = nullptr;   ///< GL関数ポインタ群（非所有）
	GLuint m_buffer = 0;           ///< GLバッファハンドル
	GLenum m_glTarget = 0;         ///< GLターゲット
	BufferType m_type;             ///< バッファ種別
	std::uint32_t m_sizeBytes;     ///< サイズ（バイト）
	bool m_dynamic;                ///< 動的更新フラグ
};

} // namespace mitiru::gfx

#endif // MITIRU_HAS_OPENGL
