#pragma once

/// @file WebGlTexture.hpp
/// @brief WebGL2テクスチャ実装
/// @details GLuintテクスチャオブジェクトをRAIIで管理する。
///          RGBA8形式のTexture2D生成・アップロード・サンプラー設定に対応。

#ifdef __EMSCRIPTEN__

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

#include <GLES3/gl3.h>

#include <mitiru/gfx/GfxTypes.hpp>
#include <mitiru/gfx/ITexture.hpp>

namespace mitiru::gfx
{

/// @brief テクスチャフィルタモード
enum class TextureFilter
{
	Nearest,  ///< ニアレストネイバー
	Linear    ///< バイリニアフィルタ
};

/// @brief テクスチャラップモード
enum class TextureWrap
{
	Repeat,       ///< リピート
	ClampToEdge,  ///< エッジクランプ
	MirroredRepeat ///< ミラーリピート
};

/// @brief WebGL2用テクスチャ実装
/// @details GLuintテクスチャオブジェクトをRAIIで管理する。
///          デストラクタでglDeleteTexturesを呼び出す。
///
/// @code
/// auto tex = WebGLTexture::createFromData(256, 256, PixelFormat::RGBA8, pixelData);
/// tex.bind(0);
/// @endcode
class WebGLTexture final : public ITexture
{
public:
	/// @brief ピクセルデータからテクスチャを生成するファクトリ
	/// @param width テクスチャ幅
	/// @param height テクスチャ高さ
	/// @param pixelFormat ピクセルフォーマット
	/// @param data ピクセルデータ
	/// @return 生成されたテクスチャ
	[[nodiscard]] static WebGLTexture createFromData(
		int width, int height,
		PixelFormat pixelFormat,
		std::span<const std::uint8_t> data)
	{
		WebGLTexture texture;
		texture.m_width = width;
		texture.m_height = height;
		texture.m_format = pixelFormat;

		glGenTextures(1, &texture.m_texture);
		if (texture.m_texture == 0)
		{
			throw std::runtime_error("WebGLTexture: glGenTextures failed");
		}

		glBindTexture(GL_TEXTURE_2D, texture.m_texture);

		const auto [internalFmt, fmt, type] = toGlFormat(pixelFormat);

		glTexImage2D(
			GL_TEXTURE_2D, 0,
			internalFmt,
			width, height, 0,
			fmt, type,
			data.data());

		/// デフォルトサンプラーパラメータを設定する
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glBindTexture(GL_TEXTURE_2D, 0);

		return texture;
	}

	/// @brief 空テクスチャを生成するファクトリ
	/// @param width テクスチャ幅
	/// @param height テクスチャ高さ
	/// @param pixelFormat ピクセルフォーマット
	/// @return 生成されたテクスチャ
	[[nodiscard]] static WebGLTexture createEmpty(
		int width, int height,
		PixelFormat pixelFormat = PixelFormat::RGBA8)
	{
		WebGLTexture texture;
		texture.m_width = width;
		texture.m_height = height;
		texture.m_format = pixelFormat;

		glGenTextures(1, &texture.m_texture);
		if (texture.m_texture == 0)
		{
			throw std::runtime_error("WebGLTexture: glGenTextures failed");
		}

		glBindTexture(GL_TEXTURE_2D, texture.m_texture);

		const auto [internalFmt, fmt, type] = toGlFormat(pixelFormat);

		glTexImage2D(
			GL_TEXTURE_2D, 0,
			internalFmt,
			width, height, 0,
			fmt, type,
			nullptr);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glBindTexture(GL_TEXTURE_2D, 0);

		return texture;
	}

	~WebGLTexture() override
	{
		if (m_texture != 0)
		{
			glDeleteTextures(1, &m_texture);
		}
	}

	WebGLTexture(const WebGLTexture&) = delete;
	WebGLTexture& operator=(const WebGLTexture&) = delete;

	WebGLTexture(WebGLTexture&& other) noexcept
		: m_texture(other.m_texture)
		, m_width(other.m_width)
		, m_height(other.m_height)
		, m_format(other.m_format)
	{
		other.m_texture = 0;
	}

	WebGLTexture& operator=(WebGLTexture&& other) noexcept
	{
		if (this != &other)
		{
			if (m_texture != 0)
			{
				glDeleteTextures(1, &m_texture);
			}
			m_texture = other.m_texture;
			m_width = other.m_width;
			m_height = other.m_height;
			m_format = other.m_format;
			other.m_texture = 0;
		}
		return *this;
	}

	/// @brief テクスチャ幅を取得する
	[[nodiscard]] int width() const noexcept override { return m_width; }

	/// @brief テクスチャ高さを取得する
	[[nodiscard]] int height() const noexcept override { return m_height; }

	/// @brief ピクセルフォーマットを取得する
	[[nodiscard]] PixelFormat format() const noexcept override { return m_format; }

	/// @brief GLテクスチャハンドルを取得する
	[[nodiscard]] GLuint handle() const noexcept { return m_texture; }

	/// @brief テクスチャを指定テクスチャユニットにバインドする
	/// @param unit テクスチャユニット番号（0〜31）
	void bind(GLuint unit = 0) const noexcept
	{
		glActiveTexture(GL_TEXTURE0 + unit);
		glBindTexture(GL_TEXTURE_2D, m_texture);
	}

	/// @brief テクスチャバインドを解除する
	/// @param unit テクスチャユニット番号
	static void unbind(GLuint unit = 0) noexcept
	{
		glActiveTexture(GL_TEXTURE0 + unit);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	/// @brief テクスチャのサブ領域を更新する
	/// @param x 更新開始X座標
	/// @param y 更新開始Y座標
	/// @param w 更新幅
	/// @param h 更新高さ
	/// @param data ピクセルデータ
	void updateSubImage(
		int x, int y, int w, int h,
		const void* data) noexcept
	{
		if (m_texture == 0 || !data)
		{
			return;
		}

		const auto [internalFmt, fmt, type] = toGlFormat(m_format);
		static_cast<void>(internalFmt);

		glBindTexture(GL_TEXTURE_2D, m_texture);
		glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, fmt, type, data);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	/// @brief フィルタモードを設定する
	/// @param minFilter ミニフィケーションフィルタ
	/// @param magFilter マグニフィケーションフィルタ
	void setFilter(TextureFilter minFilter, TextureFilter magFilter) noexcept
	{
		if (m_texture == 0)
		{
			return;
		}

		glBindTexture(GL_TEXTURE_2D, m_texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
			toGlFilter(minFilter));
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
			toGlFilter(magFilter));
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	/// @brief ラップモードを設定する
	/// @param wrapS S軸ラップモード
	/// @param wrapT T軸ラップモード
	void setWrap(TextureWrap wrapS, TextureWrap wrapT) noexcept
	{
		if (m_texture == 0)
		{
			return;
		}

		glBindTexture(GL_TEXTURE_2D, m_texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, toGlWrap(wrapS));
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, toGlWrap(wrapT));
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	/// @brief ミップマップを生成する
	void generateMipmaps() noexcept
	{
		if (m_texture == 0)
		{
			return;
		}

		glBindTexture(GL_TEXTURE_2D, m_texture);
		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
			GL_LINEAR_MIPMAP_LINEAR);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

private:
	/// @brief デフォルトコンストラクタ（ファクトリからのみ使用）
	WebGLTexture() = default;

	/// @brief GLフォーマット情報
	struct GlFormatInfo
	{
		GLint internalFormat;
		GLenum format;
		GLenum type;
	};

	/// @brief PixelFormatからGLフォーマットに変換する
	[[nodiscard]] static GlFormatInfo toGlFormat(PixelFormat pf) noexcept
	{
		switch (pf)
		{
		case PixelFormat::RGBA8:
			return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
		case PixelFormat::BGRA8:
			/// WebGL2はBGRAを直接サポートしないため、RGBAとして扱う
			return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
		case PixelFormat::R8:
			return {GL_R8, GL_RED, GL_UNSIGNED_BYTE};
		case PixelFormat::Depth24Stencil8:
			return {GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8};
		}
		return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
	}

	/// @brief TextureFilterからGLフィルタに変換する
	[[nodiscard]] static GLint toGlFilter(TextureFilter filter) noexcept
	{
		switch (filter)
		{
		case TextureFilter::Nearest: return GL_NEAREST;
		case TextureFilter::Linear:  return GL_LINEAR;
		}
		return GL_LINEAR;
	}

	/// @brief TextureWrapからGLラップモードに変換する
	[[nodiscard]] static GLint toGlWrap(TextureWrap wrap) noexcept
	{
		switch (wrap)
		{
		case TextureWrap::Repeat:         return GL_REPEAT;
		case TextureWrap::ClampToEdge:    return GL_CLAMP_TO_EDGE;
		case TextureWrap::MirroredRepeat: return GL_MIRRORED_REPEAT;
		}
		return GL_CLAMP_TO_EDGE;
	}

	GLuint m_texture = 0;                         ///< GLテクスチャオブジェクト
	int m_width = 0;                               ///< テクスチャ幅
	int m_height = 0;                              ///< テクスチャ高さ
	PixelFormat m_format = PixelFormat::RGBA8;     ///< ピクセルフォーマット
};

} // namespace mitiru::gfx

#endif // __EMSCRIPTEN__
