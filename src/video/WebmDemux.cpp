#include "video/WebmDemux.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "mkvparser/mkvparser.h"

namespace mitiru::video
{

namespace
{

class MemoryReader final : public mkvparser::IMkvReader
{
public:
	explicit MemoryReader(const std::vector<std::uint8_t>& bytes) : m_bytes(bytes) {}

	int Read(long long pos, long len, unsigned char* buf) override
	{
		if (pos < 0 || len < 0) { return -1; }
		const auto size = static_cast<long long>(m_bytes.size());
		if (pos > size || len > size - pos) { return -1; }
		if (len > 0) { std::memcpy(buf, m_bytes.data() + pos, static_cast<std::size_t>(len)); }
		return 0;
	}

	int Length(long long* total, long long* available) override
	{
		*total = static_cast<long long>(m_bytes.size());
		*available = *total;
		return 0;
	}

private:
	const std::vector<std::uint8_t>& m_bytes;
};

void fail(std::string* error, const char* why)
{
	if (error != nullptr) { *error = why; }
}

// 格納された fps (DefaultDuration は ns に丸めた値) を、よくある値の厳密な比へ寄せる。floor(秒 × fps) の境目の秒
// (30 fps の 0.5 秒) が丸めの誤差で前の絵に落ちないようにする。寄せる幅は ns の丸めに足りる 1e-5
double snapExactFps(double fps)
{
	static constexpr double kCommon[] = {24000.0 / 1001.0, 30000.0 / 1001.0, 60000.0 / 1001.0};
	for (const double c : kCommon)
	{
		if (std::fabs(fps - c) / c < 1.0e-5) { return c; }
	}
	const double whole = std::round(fps);
	return whole > 0.0 && std::fabs(fps - whole) / whole < 1.0e-5 ? whole : fps;
}

// 時刻から求めた fps (WebM の時刻は 1 ms 刻みで ±1 ms ずれる) を、いちばん近いよくある値へ寄せる。29.97 と 30 は 0.1% しか違わないので、
// 近いほうを選ぶ。0.2% より遠い値はそのまま使う
double snapEstimatedFps(double fps)
{
	static constexpr double kCommon[] = {24000.0 / 1001.0, 24.0, 25.0, 30000.0 / 1001.0, 30.0, 48.0, 50.0, 60000.0 / 1001.0, 60.0, 120.0};
	double best = fps;
	double bestRel = 0.002;
	for (const double c : kCommon)
	{
		const double rel = std::fabs(fps - c) / c;
		if (rel < bestRel)
		{
			bestRel = rel;
			best = c;
		}
	}
	return best;
}

// 可変フレームレートは、秒 → 番号の決め方 (floor(秒 × fps)) と合わない。時刻が等間隔の格子から 1/4 コマ (最低 2 ms) 以上外れたら断る
bool isConstantRate(const std::vector<long long>& timesNs, double fps)
{
	if (timesNs.size() < 2) { return true; }
	const double frameNs = 1.0e9 / fps;
	const double tolerance = std::max(frameNs * 0.25, 2.0e6);
	for (std::size_t i = 0; i < timesNs.size(); ++i)
	{
		const double expected = static_cast<double>(timesNs.front()) + frameNs * static_cast<double>(i);
		if (std::fabs(static_cast<double>(timesNs[i]) - expected) > tolerance) { return false; }
	}
	return true;
}

}  // namespace

WebmDemux::~WebmDemux() = default;

std::unique_ptr<WebmDemux> WebmDemux::open(std::vector<std::uint8_t>&& bytes, std::string* error)
{
	if (bytes.empty())
	{
		fail(error, "ファイルが空です");
		return nullptr;
	}
	std::unique_ptr<WebmDemux> self(new WebmDemux());
	self->m_bytes = std::move(bytes);
	MemoryReader reader(self->m_bytes);

	mkvparser::EBMLHeader header;
	long long pos = 0;
	if (header.Parse(&reader, pos) < 0)
	{
		fail(error, "WebM として読めません");
		return nullptr;
	}
	mkvparser::Segment* rawSegment = nullptr;
	if (mkvparser::Segment::CreateInstance(&reader, pos, rawSegment) != 0 || rawSegment == nullptr)
	{
		fail(error, "WebM の Segment が読めません");
		return nullptr;
	}
	const std::unique_ptr<mkvparser::Segment> segment(rawSegment);
	if (segment->Load() < 0)
	{
		fail(error, "WebM の中身が壊れています");
		return nullptr;
	}

	const mkvparser::Tracks* tracks = segment->GetTracks();
	const mkvparser::VideoTrack* video = nullptr;
	bool sawOtherVideoCodec = false;
	for (unsigned long i = 0; tracks != nullptr && i < tracks->GetTracksCount(); ++i)
	{
		const mkvparser::Track* t = tracks->GetTrackByIndex(i);
		if (t == nullptr || t->GetType() != mkvparser::Track::kVideo) { continue; }
		const char* codec = t->GetCodecId();
		if (codec != nullptr && std::strcmp(codec, "V_AV1") == 0)
		{
			video = static_cast<const mkvparser::VideoTrack*>(t);
			break;
		}
		sawOtherVideoCodec = true;
	}
	if (video == nullptr)
	{
		fail(error, sawOtherVideoCodec ? "映像が AV1 ではありません。ffmpeg の -c:v libaom-av1 (または libsvtav1) で書き出してください"
		                              : "WebM に映像のトラックがありません");
		return nullptr;
	}

	std::size_t privSize = 0;
	const unsigned char* priv = video->GetCodecPrivate(privSize);
	if (priv != nullptr && privSize > 4) { self->m_configObus.assign(priv + 4, priv + privSize); }

	const long trackNumber = video->GetNumber();
	std::vector<long long> times;
	const mkvparser::Cluster* cluster = segment->GetFirst();
	while (cluster != nullptr && !cluster->EOS())
	{
		const mkvparser::BlockEntry* entry = nullptr;
		if (cluster->GetFirst(entry) < 0)
		{
			fail(error, "WebM の Cluster が壊れています");
			return nullptr;
		}
		while (entry != nullptr && !entry->EOS())
		{
			const mkvparser::Block* block = entry->GetBlock();
			if (block != nullptr && block->GetTrackNumber() == trackNumber)
			{
				const long long timeNs = block->GetTime(cluster);
				// AV1 の WebM は、1 Block が 1 つの Temporal Unit (lacing は使わない決まり)。まとめた Block は時刻が決められない
				if (block->GetFrameCount() != 1)
				{
					fail(error, "WebM の 1 Block に複数のフレームが入っています (AV1 では使えません)");
					return nullptr;
				}
				for (int f = 0; f < block->GetFrameCount(); ++f)
				{
					const auto& frame = block->GetFrame(f);
					if (frame.pos < 0 || frame.len <= 0 || static_cast<unsigned long long>(frame.pos) + static_cast<unsigned long long>(frame.len) > self->m_bytes.size())
					{
						fail(error, "WebM のフレームの位置が壊れています");
						return nullptr;
					}
					self->m_packets.push_back({static_cast<std::size_t>(frame.pos), static_cast<std::size_t>(frame.len), timeNs});
					times.push_back(timeNs);
				}
			}
			if (cluster->GetNext(entry, entry) < 0)
			{
				fail(error, "WebM の Block が壊れています");
				return nullptr;
			}
		}
		cluster = segment->GetNext(cluster);
	}
	if (self->m_packets.empty())
	{
		fail(error, "WebM に映像のフレームがありません");
		return nullptr;
	}
	if (!std::is_sorted(times.begin(), times.end()))
	{
		fail(error, "WebM の映像の時刻が並んでいません (AV1 は表示順に並べて書き出してください)");
		return nullptr;
	}

	double fps = 0.0;
	if (video->GetDefaultDuration() > 0) { fps = snapExactFps(1.0e9 / static_cast<double>(video->GetDefaultDuration())); }
	else if (video->GetFrameRate() > 0.0) { fps = snapExactFps(video->GetFrameRate()); }
	else if (times.size() >= 2 && times.back() > times.front())
	{
		fps = snapEstimatedFps(static_cast<double>(times.size() - 1) * 1.0e9 / static_cast<double>(times.back() - times.front()));
	}
	else { fps = 30.0; }   // 1 コマだけで、fps を示す物が何も無い
	if (!(fps > 0.0) || fps > 1000.0)
	{
		fail(error, "WebM から fps が読めません");
		return nullptr;
	}
	if (!isConstantRate(times, fps))
	{
		fail(error, "可変フレームレートの映像です。ffmpeg の -r で固定のフレームレートにして書き出してください");
		return nullptr;
	}
	self->m_fps = fps;
	return self;
}

bool WebmDemux::next(std::vector<std::uint8_t>& out)
{
	if (m_cursor >= m_packets.size()) { return false; }
	const Packet& p = m_packets[m_cursor++];
	out.assign(m_bytes.begin() + static_cast<std::ptrdiff_t>(p.offset), m_bytes.begin() + static_cast<std::ptrdiff_t>(p.offset + p.size));
	return true;
}

}  // namespace mitiru::video
