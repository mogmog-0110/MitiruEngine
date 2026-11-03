#include <mitiru/video/Av1Source.hpp>

#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <dav1d/dav1d.h>

#include <mitiru/video/YuvToRgba.hpp>

#include "video/WebmDemux.hpp"

namespace mitiru::video
{

namespace
{

// 復号器の寸法の上限。texture と RGBA の変換先を作る前に、極端に大きい申告を断る
constexpr int kMaxSide = 8192;
constexpr long long kMaxPixels = 8192LL * 4608LL;

void silentLog(void*, const char*, va_list) {}

void failWith(std::string* error, const char* why)
{
	if (error != nullptr) { *error = why; }
}

YuvMatrix matrixOf(const Dav1dSequenceHeader& seq, int height)
{
	switch (seq.mtrx)
	{
	case DAV1D_MC_IDENTITY: return YuvMatrix::Identity;
	case DAV1D_MC_BT709: return YuvMatrix::Bt709;
	case DAV1D_MC_BT470BG:
	case DAV1D_MC_BT601: return YuvMatrix::Bt601;
	case DAV1D_MC_BT2020_NCL:
	case DAV1D_MC_BT2020_CL: return YuvMatrix::Bt2020;
	default: break;
	}
	// 色空間が示されていない映像: 一般的な慣習で、HD は BT.709、それ未満は BT.601
	return height >= 720 ? YuvMatrix::Bt709 : YuvMatrix::Bt601;
}

YuvLayout layoutOf(const Dav1dPicture& pic)
{
	YuvLayout l;
	l.matrix = matrixOf(*pic.seq_hdr, pic.p.h);
	l.fullRange = pic.seq_hdr->color_range != 0;
	l.mono = pic.p.layout == DAV1D_PIXEL_LAYOUT_I400;
	l.subX = pic.p.layout == DAV1D_PIXEL_LAYOUT_I444 ? 0 : 1;
	l.subY = pic.p.layout == DAV1D_PIXEL_LAYOUT_I420 ? 1 : 0;
	if (l.mono) { l.matrix = l.matrix == YuvMatrix::Identity ? YuvMatrix::Bt709 : l.matrix; }
	return l;
}

class Av1Source final : public IAv1Source
{
public:
	~Av1Source() override
	{
		dav1d_data_unref(&m_data);
		if (m_ctx != nullptr) { dav1d_close(&m_ctx); }
	}

	bool open(std::vector<std::uint8_t>&& bytes, std::string* error)
	{
		m_demux = WebmDemux::open(std::move(bytes), error);
		if (!m_demux) { return false; }

		if (!m_demux->configObus().empty())
		{
			Dav1dSequenceHeader seq{};
			if (dav1d_parse_sequence_header(&seq, m_demux->configObus().data(), m_demux->configObus().size()) == 0 && seq.hbd != 0)
			{
				failWith(error, "10 ビット以上の AV1 は読めません。8 ビットの yuv420p で書き出してください");
				return false;
			}
		}

		Dav1dSettings s;
		dav1d_default_settings(&s);
		// 先読みは VideoStream の専用 thread がする。dav1d は自分の thread を増やさず、1 コマごとに結果を返す
		s.n_threads = kThreads;
		s.max_frame_delay = 1;
		s.all_layers = 0;
		s.operating_point = 0;
		s.frame_size_limit = static_cast<unsigned>(kMaxPixels);
		s.logger.callback = silentLog;
		if (dav1d_open(&m_ctx, &s) < 0)
		{
			failWith(error, "AV1 の復号器を作れません");
			return false;
		}

		// 先頭のコマを 1 枚復号して、大きさと色の形を決める。以降のコマは同じ大きさの前提で、違えば復号を止める
		{
			Held pic;
			if (!pull(pic, error)) { return false; }
			m_first = copyOf(pic);
		}
		m_info.width = m_first.width;
		m_info.height = m_first.height;
		m_info.fps = m_demux->fps();
		m_haveFirst = true;
		if (m_info.width > kMaxSide || m_info.height > kMaxSide || static_cast<long long>(m_info.width) * m_info.height > kMaxPixels)
		{
			failWith(error, "映像が大きすぎます (幅と高さは 8192 まで)");
			return false;
		}
		return true;
	}

	[[nodiscard]] const VideoInfo& info() const noexcept override { return m_info; }

	bool decodeNext(std::uint8_t* rgba) override
	{
		if (m_haveFirst)
		{
			m_haveFirst = false;
			if (rgba != nullptr) { convertFrame(m_first, rgba); }
			return true;
		}
		Held pic;
		if (!pull(pic, nullptr)) { return false; }
		if (pic.p.w != m_info.width || pic.p.h != m_info.height) { return false; }   // 途中で大きさが変わる映像は扱わない
		if (rgba != nullptr) { convertPicture(pic, rgba); }
		return true;
	}

	bool decodeNextPlanes(Av1Planes& out) override
	{
		Frame frame;
		if (m_haveFirst)
		{
			m_haveFirst = false;
			frame = std::move(m_first);
		}
		else
		{
			Held pic;
			if (!pull(pic, nullptr)) { return false; }
			if (pic.p.w != m_info.width || pic.p.h != m_info.height) { return false; }
			frame = copyOf(pic);
		}
		out.width = frame.width;
		out.height = frame.height;
		out.y = std::move(frame.y);
		out.u = std::move(frame.u);
		out.v = std::move(frame.v);
		out.chromaWidth = frame.layout.mono ? 0 : (frame.width + frame.layout.subX) >> frame.layout.subX;
		out.chromaHeight = frame.layout.mono ? 0 : (frame.height + frame.layout.subY) >> frame.layout.subY;
		return true;
	}

	void rewind() override
	{
		dav1d_data_unref(&m_data);
		dav1d_flush(m_ctx);
		m_demux->rewind();
		m_sentConfig = false;
		m_haveFirst = false;
		m_first = Frame{};
	}

private:
	static constexpr int kThreads = 2;

	// dav1d の picture を持つ間だけ生きる。返すときに必ず unref する
	struct Held : Dav1dPicture
	{
		Held() : Dav1dPicture{} {}
		Held(const Held&) = delete;
		Held& operator=(const Held&) = delete;
		~Held() { dav1d_picture_unref(this); }
	};

	// 最初の 1 コマだけは、大きさを決めるために open で復号して詰め直して持つ
	struct Frame
	{
		int                       width = 0;
		int                       height = 0;
		YuvLayout                 layout;
		std::vector<std::uint8_t> y;
		std::vector<std::uint8_t> u;
		std::vector<std::uint8_t> v;
	};

	static void copyPlane(const std::uint8_t* src, std::ptrdiff_t stride, int w, int h, std::vector<std::uint8_t>& dst)
	{
		dst.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
		for (int row = 0; row < h; ++row)
		{
			std::memcpy(dst.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(w), src + row * stride, static_cast<std::size_t>(w));
		}
	}

	static Frame copyOf(const Dav1dPicture& pic)
	{
		Frame out;
		out.width = pic.p.w;
		out.height = pic.p.h;
		out.layout = layoutOf(pic);
		copyPlane(static_cast<const std::uint8_t*>(pic.data[0]), pic.stride[0], pic.p.w, pic.p.h, out.y);
		if (!out.layout.mono)
		{
			const int cw = (pic.p.w + out.layout.subX) >> out.layout.subX;
			const int ch = (pic.p.h + out.layout.subY) >> out.layout.subY;
			copyPlane(static_cast<const std::uint8_t*>(pic.data[1]), pic.stride[1], cw, ch, out.u);
			copyPlane(static_cast<const std::uint8_t*>(pic.data[2]), pic.stride[1], cw, ch, out.v);
		}
		return out;
	}

	// 次の 1 コマを pic に取り出す。パケットが尽きたり復号に失敗したら false (error は null でもよい)
	bool pull(Held& pic, std::string* error)
	{
		for (;;)
		{
			const int got = dav1d_get_picture(m_ctx, &pic);
			if (got == 0) { return accept(pic, error); }
			if (got != DAV1D_ERR(EAGAIN))
			{
				failWith(error, "AV1 の復号に失敗しました (壊れた映像か、対応しない機能です)");
				return false;
			}
			if (m_data.sz == 0 && !fillData(error)) { return false; }
			const int sent = dav1d_send_data(m_ctx, &m_data);
			if (sent < 0 && sent != DAV1D_ERR(EAGAIN))
			{
				dav1d_data_unref(&m_data);
				failWith(error, "AV1 のデータを復号器へ渡せません");
				return false;
			}
		}
	}

	// 次に復号器へ渡す塊を m_data に作る。最初は av1C の configOBUs、あとはパケットを順に
	bool fillData(std::string* error)
	{
		std::vector<std::uint8_t> bytes;
		if (!m_sentConfig)
		{
			m_sentConfig = true;
			bytes = m_demux->configObus();
		}
		if (bytes.empty() && !m_demux->next(bytes)) { return false; }
		std::uint8_t* buf = dav1d_data_create(&m_data, bytes.size());
		if (buf == nullptr)
		{
			failWith(error, "AV1 のデータ用の領域を確保できません");
			return false;
		}
		std::memcpy(buf, bytes.data(), bytes.size());
		return true;
	}

	static bool accept(const Dav1dPicture& pic, std::string* error)
	{
		if (pic.p.bpc != 8)
		{
			failWith(error, "10 ビット以上の AV1 は読めません。8 ビットの yuv420p で書き出してください");
			return false;
		}
		if (pic.p.w <= 0 || pic.p.h <= 0 || pic.p.w > kMaxSide || pic.p.h > kMaxSide)
		{
			failWith(error, "映像の大きさが読めません");
			return false;
		}
		return true;
	}

	static void convertPicture(const Dav1dPicture& pic, std::uint8_t* rgba)
	{
		const YuvLayout layout = layoutOf(pic);
		convertYuvToRgba(static_cast<const std::uint8_t*>(pic.data[0]), pic.stride[0], static_cast<const std::uint8_t*>(pic.data[1]),
		                 static_cast<const std::uint8_t*>(pic.data[2]), pic.stride[1], pic.p.w, pic.p.h, layout, rgba);
	}

	static void convertFrame(const Frame& f, std::uint8_t* rgba)
	{
		const int cw = f.layout.mono ? 0 : (f.width + f.layout.subX) >> f.layout.subX;
		convertYuvToRgba(f.y.data(), f.width, f.u.data(), f.v.data(), cw, f.width, f.height, f.layout, rgba);
	}

	std::unique_ptr<WebmDemux> m_demux;
	Dav1dContext*              m_ctx = nullptr;
	Dav1dData                  m_data{};
	bool                       m_sentConfig = false;
	bool                       m_haveFirst = false;
	Frame                      m_first;
	VideoInfo                  m_info;
};

}  // namespace

std::unique_ptr<IAv1Source> openAv1WebM(std::vector<std::uint8_t>&& bytes, std::string* error)
{
	auto source = std::make_unique<Av1Source>();
	if (!source->open(std::move(bytes), error)) { return nullptr; }
	return source;
}

}  // namespace mitiru::video
