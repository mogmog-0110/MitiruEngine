#pragma once

/// @file CaptureAviExport.hpp
/// @brief --capture-dir の PNG 連番と --audio-capture の WAV から AVI (MJPEG + PCM) を作る、読み取り専用の後処理
/// @details n 枚目の画像は n ステップ目の update の後で、WAV の n ステップ目の区間 [n/fps, (n+1)/fps) 秒は
///          その update で積んだ音から始まる (OfflineMixCapture と同じ境界 floor(n * rate / fps))。
///          だから n 枚目の JPEG の直後に、その区間の PCM を置けば、画像と音のずれは 1 フレームに収まる。
///          WAV が画像より長い分 (EOF の update が音だけ 1 ステップ回る) は捨て、短い分は無音で埋める。

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <stb_image.h>
#include <stb_image_write.h>

#include <mitiru/video/AviMjpegWriter.hpp>

namespace mitiru::video
{

struct CaptureAviOptions
{
	std::filesystem::path framesDir;  ///< frame_%06d.png が 0 から途切れず並ぶフォルダ
	std::filesystem::path audioWav;   ///< 空なら映像だけ。float32 か 16bit の PCM
	std::filesystem::path out;
	std::uint32_t         fps = 60;
	int                   jpegQuality = 90;  ///< 90 以下は色差を半分に間引く (どの再生ソフトでも開ける形)
};

struct CaptureAviResult
{
	bool          ok = false;
	std::string   error;
	std::uint64_t frames = 0;
	std::uint64_t audioSamples = 0;       ///< 書いた音のサンプル数 (1 チャンネルあたり)
	std::uint64_t audioPaddedSamples = 0; ///< WAV が足りず無音で埋めた分
	std::uint64_t audioDroppedSamples = 0;///< WAV が長く、捨てた分
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::uint64_t bytes = 0;
};

namespace detail
{

struct WavData
{
	std::uint32_t      sampleRate = 0;
	std::uint16_t      channels = 0;
	std::vector<std::int16_t> pcm;  ///< インターリーブの 16bit
	std::uint64_t      sampleFrames = 0;
};

inline std::uint32_t le32(const std::uint8_t* p)
{
	return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
	     | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
inline std::uint16_t le16(const std::uint8_t* p)
{
	return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

inline std::int16_t floatToS16(float v)
{
	// 1.0 を超えた分は丸める。AVI の PCM は 16bit なので、割れた音は割れたまま残る
	if (v > 1.0f) { v = 1.0f; }
	if (v < -1.0f) { v = -1.0f; }
	const float scaled = v * 32767.0f;
	return static_cast<std::int16_t>(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
}

[[nodiscard]] inline bool readWav(const std::filesystem::path& path, WavData& out, std::string& error)
{
	std::ifstream f(path, std::ios::binary);
	if (!f.is_open()) { error = "WAV " + path.string() + " を開けません"; return false; }
	std::vector<std::uint8_t> all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	if (all.size() < 12 || std::memcmp(all.data(), "RIFF", 4) != 0 || std::memcmp(all.data() + 8, "WAVE", 4) != 0)
	{
		error = "WAV " + path.string() + " の形式が RIFF/WAVE ではありません";
		return false;
	}
	std::uint16_t tag = 0, bits = 0;
	const std::uint8_t* data = nullptr;
	std::size_t dataBytes = 0;
	for (std::size_t i = 12; i + 8 <= all.size();)
	{
		const std::uint32_t n = le32(&all[i + 4]);
		const std::size_t body = i + 8;
		const std::size_t avail = std::min<std::size_t>(n, all.size() - body);
		if (std::memcmp(&all[i], "fmt ", 4) == 0 && avail >= 16)
		{
			tag = le16(&all[body]);
			out.channels = le16(&all[body + 2]);
			out.sampleRate = le32(&all[body + 4]);
			bits = le16(&all[body + 14]);
		}
		else if (std::memcmp(&all[i], "data", 4) == 0)
		{
			data = &all[body];
			dataBytes = avail;
			break;
		}
		i = body + n + (n & 1u);
	}
	const bool isFloat = tag == 3 && bits == 32;
	const bool isS16 = tag == 1 && bits == 16;
	if (data == nullptr || out.channels == 0 || out.sampleRate == 0 || (!isFloat && !isS16))
	{
		error = "WAV " + path.string() + " は float32 か 16bit の PCM ではありません";
		return false;
	}
	const std::size_t bytesPerSample = bits / 8;
	const std::size_t total = dataBytes / bytesPerSample / out.channels * out.channels;
	out.pcm.resize(total);
	for (std::size_t i = 0; i < total; ++i)
	{
		if (isFloat)
		{
			float v;
			std::memcpy(&v, data + i * 4, 4);
			out.pcm[i] = floatToS16(v);
		}
		else
		{
			out.pcm[i] = static_cast<std::int16_t>(le16(data + i * 2));
		}
	}
	out.sampleFrames = total / out.channels;
	return true;
}

inline void jpegSink(void* ctx, void* data, int size)
{
	auto* v = static_cast<std::vector<std::uint8_t>*>(ctx);
	const auto* p = static_cast<const std::uint8_t*>(data);
	v->insert(v->end(), p, p + size);
}

[[nodiscard]] inline std::filesystem::path framePath(const std::filesystem::path& dir, std::uint64_t n)
{
	char name[40];
	std::snprintf(name, sizeof(name), "frame_%06llu.png", static_cast<unsigned long long>(n));
	return dir / name;
}

}  // namespace detail

/// n 枚目の画像と、その直後に置く音の区間を AVI へ足していく。失敗の理由は result.error に入る
class CaptureAviPipeline
{
public:
	CaptureAviPipeline(const CaptureAviOptions& opt, CaptureAviResult& result, const detail::WavData* wav)
		: m_opt(opt), m_r(result), m_wav(wav) {}

	[[nodiscard]] bool addFrame(std::uint64_t n)
	{
		const std::string path = detail::framePath(m_opt.framesDir, n).string();
		int w = 0, h = 0, comp = 0;
		stbi_uc* rgba = stbi_load(path.c_str(), &w, &h, &comp, 4);
		if (rgba == nullptr) { return fail("PNG " + path + " を読めません"); }
		const bool opened = n != 0 || open(w, h);
		const bool sameSize = static_cast<std::uint32_t>(w) == m_r.width && static_cast<std::uint32_t>(h) == m_r.height;
		bool ok = opened && sameSize;
		if (ok)
		{
			m_jpeg.clear();
			ok = stbi_write_jpg_to_func(&detail::jpegSink, &m_jpeg, w, h, 4, rgba, m_opt.jpegQuality) != 0
			  && m_writer.addVideoFrame(m_jpeg.data(), m_jpeg.size());
		}
		stbi_image_free(rgba);
		if (!opened) { return false; }
		if (!sameSize) { return fail("PNG " + path + " の大きさが 1 枚目と違います"); }
		if (!ok) { return fail(m_writer.error().empty() ? "JPEG にできません" : m_writer.error()); }
		return m_wav == nullptr || addAudio(n);
	}

	[[nodiscard]] bool finish(std::uint64_t frames)
	{
		if (m_wav != nullptr && m_wav->sampleFrames > m_audioPos)
		{
			m_r.audioDroppedSamples = m_wav->sampleFrames - m_audioPos;
		}
		m_r.frames = frames;
		return m_writer.close() || fail(m_writer.error());
	}

private:
	bool fail(const std::string& why) { m_r.error = why; return false; }

	bool open(int w, int h)
	{
		m_r.width = static_cast<std::uint32_t>(w);
		m_r.height = static_cast<std::uint32_t>(h);
		const AviAudioSpec spec{m_wav ? m_wav->sampleRate : 0u, m_wav ? m_wav->channels : std::uint16_t{0}};
		const AviVideoSpec video{m_r.width, m_r.height, m_opt.fps};
		if (m_writer.open(m_opt.out.string(), video, m_wav ? &spec : nullptr)) { return true; }
		return fail(m_writer.error());
	}

	/// 区間の境界は OfflineMixCapture と同じ floor(n * rate / fps)。WAV が足りない分は無音にして数える
	bool addAudio(std::uint64_t n)
	{
		const std::uint64_t end = (n + 1) * m_wav->sampleRate / m_opt.fps;
		const std::uint64_t want = end - m_audioPos;
		const std::uint64_t have = m_audioPos < m_wav->sampleFrames ? std::min(want, m_wav->sampleFrames - m_audioPos) : 0;
		m_chunk.assign(static_cast<std::size_t>(want) * m_wav->channels, 0);
		if (have > 0)
		{
			std::copy_n(m_wav->pcm.begin() + static_cast<std::ptrdiff_t>(m_audioPos * m_wav->channels),
			            static_cast<std::ptrdiff_t>(have * m_wav->channels), m_chunk.begin());
		}
		m_r.audioPaddedSamples += want - have;
		m_audioPos = end;
		m_r.audioSamples += want;
		return want == 0 || m_writer.addAudio(m_chunk.data(), static_cast<std::size_t>(want)) || fail(m_writer.error());
	}

	const CaptureAviOptions&  m_opt;
	CaptureAviResult&         m_r;
	const detail::WavData*    m_wav;
	AviMjpegWriter            m_writer;
	std::vector<std::uint8_t> m_jpeg;
	std::vector<std::int16_t> m_chunk;
	std::uint64_t             m_audioPos = 0;  // WAV から使い終えたサンプル数 (チャンネルをまとめて 1)
};

[[nodiscard]] inline CaptureAviResult exportCaptureAvi(const CaptureAviOptions& opt)
{
	CaptureAviResult r;
	std::error_code ec;
	if (opt.fps == 0) { r.error = "フレームレートが 0 です"; return r; }
	std::uint64_t count = 0;
	while (std::filesystem::exists(detail::framePath(opt.framesDir, count), ec)) { ++count; }
	if (count == 0)
	{
		r.error = opt.framesDir.string() + " に frame_000000.png がありません (--capture-every 1 で撮ってください)";
		return r;
	}
	detail::WavData wav;
	const bool withAudio = !opt.audioWav.empty();
	if (withAudio && !detail::readWav(opt.audioWav, wav, r.error)) { return r; }

	CaptureAviPipeline pipeline(opt, r, withAudio ? &wav : nullptr);
	for (std::uint64_t n = 0; n < count; ++n)
	{
		if (!pipeline.addFrame(n)) { return r; }
	}
	if (!pipeline.finish(count)) { return r; }
	r.bytes = static_cast<std::uint64_t>(std::filesystem::file_size(opt.out, ec));
	r.ok = true;
	return r;
}

}  // namespace mitiru::video
