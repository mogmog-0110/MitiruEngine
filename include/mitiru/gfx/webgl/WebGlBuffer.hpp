#pragma once

/// @file WebGlBuffer.hpp
/// @brief WebGL2 GPUバッファ実装
/// @details GLuintバッファオブジェクトをRAIIで管理する。
///          頂点バッファ・インデックスバッファ・ユニフォームバッファに対応。
///          動的バッファはglBufferSubDataで毎フレーム更新可能。

#ifdef __EMSCRIPTEN__

#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <GLES3/gl3.h>

#include <mitiru/gfx/IBuffer.hpp>

namespace mitiru::gfx
{

/// @brief WebGL2用GPUバッファ実装
/// @details GLuintバッファオブジェクトをRAIIで管理する。
///          デストラクタでglDeleteBuffersを呼び出す。
///
/// @code
/// WebGLBuffer vb(BufferType::Vertex, sizeof(vertices), false, vertices);
/// glBindBuffer(GL_ARRAY_BUFFER, vb.handle());
/// @endcode
class WebGLBuffer final : public IBuffer
{
public:
	/// @brief コンストラクタ
	/// @param bufferType バッファ種別（Vertex / Index / Constant）
	/// @param sizeBytes バッファサイズ（バイト）
	/// @param dynamic 動的更新が必要か
	/// @param initialData 初期データ（nullptrで初期化なし）
	WebGLBuffer(
		BufferType bufferType,
		std::uint32_t sizeBytes,
		bool dynamic,
		const void* initialData)
		: m_type(bufferType)
		, m_size(sizeBytes)
		, m_dynamic(dynamic)
	{
		glGenBuffers(1, &m_buffer);
		if (m_buffer == 0)
		{
			throw std::runtime_error("WebGLBuffer: glGenBuffers failed");
		}

		const GLenum target = bufferTarget();
		glBindBuffer(target, m_buffer);

		const GLenum usage = dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
		glBufferData(target, sizeBytes, initialData, usage);

		glBindBuffer(target, 0);
	}

	~WebGLBuffer() override
	{
		if (m_buffer != 0)
		{
			glDeleteBuffers(1, &m_buffer);
		}
	}

	WebGLBuffer(const WebGLBuffer&) = delete;
	WebGLBuffer& operator=(const WebGLBuffer&) = delete;
	WebGLBuffer(WebGLBuffer&&) = delete;
	WebGLBuffer& operator=(WebGLBuffer&&) = delete;

	/// @brief バッファデータを更新する（動的バッファ専用）
	/// @param data 書き込むデータへのポインタ
	/// @param sizeBytes 書き込みサイズ（バイト）
	void update(const void* data, std::uint32_t sizeBytes) override
	{
		if (!m_dynamic || !data || sizeBytes > m_size)
		{
			return;
		}

		const GLenum target = bufferTarget();
		glBindBuffer(target, m_buffer);
		glBufferSubData(target, 0, sizeBytes, data);
		glBindBuffer(target, 0);
	}

	/// @brief バッファサイズを取得する
	[[nodiscard]] std::uint32_t size() const noexcept override { return m_size; }

	/// @brief バッファ種別を取得する
	[[nodiscard]] BufferType type() const noexcept override { return m_type; }

	/// @brief 動的バッファかどうかを判定する
	[[nodiscard]] bool isDynamic() const noexcept { return m_dynamic; }

	/// @brief GLバッファハンドルを取得する
	[[nodiscard]] GLuint handle() const noexcept { return m_buffer; }

	/// @brief バッファターゲット（GL_ARRAY_BUFFER等）を取得する
	[[nodiscard]] GLenum bufferTarget() const noexcept
	{
		switch (m_type)
		{
		case BufferType::Vertex:   return GL_ARRAY_BUFFER;
		case BufferType::Index:    return GL_ELEMENT_ARRAY_BUFFER;
		case BufferType::Constant: return GL_UNIFORM_BUFFER;
		}
		return GL_ARRAY_BUFFER;
	}

private:
	GLuint m_buffer = 0;          ///< GLバッファオブジェクト
	BufferType m_type;            ///< バッファ種別
	std::uint32_t m_size;         ///< サイズ（バイト）
	bool m_dynamic;               ///< 動的更新フラグ
};

} // namespace mitiru::gfx

#endif // __EMSCRIPTEN__
