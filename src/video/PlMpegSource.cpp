#include <mitiru/video/PlMpegSource.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

namespace mitiru::video
{

namespace
{

class PlMpegSource final : public IVideoSource
{
public:
	~PlMpegSource() override
	{
		if (m_plm != nullptr) { plm_destroy(m_plm); }
	}

	// 失敗理由は error に書き、成功時は true を返す
	bool open(std::vector<std::uint8_t>&& bytes, std::string* error)
	{
		m_bytes = std::move(bytes);
		if (m_bytes.empty())
		{
			fail(error, "ファイルが空です");
			return false;
		}
		// bytes は m_bytes が持ち続けるため、free_when_done は 0
		m_plm = plm_create_with_memory(m_bytes.data(), m_bytes.size(), 0);
		if (m_plm == nullptr)
		{
			fail(error, "MPEG-PS として読めません");
			return false;
		}
		if (!plm_has_headers(m_plm) || plm_get_num_video_streams(m_plm) <= 0)
		{
			fail(error, "MPEG-1 の映像が見つかりません。ffmpeg の -c:v mpeg1video -f mpeg で書き出してください");
			return false;
		}
		plm_set_audio_enabled(m_plm, 0);
		plm_set_loop(m_plm, 0);
		m_info.width = plm_get_width(m_plm);
		m_info.height = plm_get_height(m_plm);
		m_info.fps = plm_get_framerate(m_plm);
		if (m_info.width <= 0 || m_info.height <= 0 || !(m_info.fps > 0.0))
		{
			fail(error, "映像の大きさか fps が読めません");
			return false;
		}
		// pl_mpeg の RGB 変換は 2x2 画素ずつ進むため、奇数の端は変換されず前の画像が残る
		if ((m_info.width | m_info.height) & 1)
		{
			fail(error, "映像の幅と高さは偶数にしてください");
			return false;
		}
		return true;
	}

	[[nodiscard]] const VideoInfo& info() const noexcept override { return m_info; }

	bool decodeNext(std::uint8_t* rgba) override
	{
		plm_frame_t* frame = plm_decode_video(m_plm);
		if (frame == nullptr) { return false; }
		if (rgba != nullptr)
		{
			plm_frame_to_rgba(frame, rgba, m_info.width * 4);
			// 変換は RGB だけを書き、4 byte 目には触れないため、不透明にする
			const std::size_t bytes = static_cast<std::size_t>(m_info.width) * static_cast<std::size_t>(m_info.height) * 4;
			for (std::size_t i = 3; i < bytes; i += 4) { rgba[i] = 255; }
		}
		return true;
	}

	void rewind() override { plm_rewind(m_plm); }

private:
	static void fail(std::string* error, const char* why)
	{
		if (error != nullptr) { *error = why; }
	}

	plm_t*                    m_plm = nullptr;
	std::vector<std::uint8_t> m_bytes;
	VideoInfo                 m_info;
};

}  // namespace

std::unique_ptr<IVideoSource> openPlMpeg(std::vector<std::uint8_t>&& bytes, std::string* error)
{
	auto source = std::make_unique<PlMpegSource>();
	if (!source->open(std::move(bytes), error)) { return nullptr; }
	return source;
}

}  // namespace mitiru::video
