#pragma once

/// @file AviMjpegWriter.hpp
/// @brief 依存なしの AVI (Motion JPEG + 16bit PCM) 書き出し。OpenDML は使わず idx1 の索引つきで、2 GB 未満だけ作る
/// @details 映像は 1 フレーム = 1 つの JPEG を '00dc' に、音は 1 フレーム分の PCM を '01wb' に、交互に置く。
///          ヘッダーは固定長なので、閉じるときに同じ位置へ確定値を書き直す。
///          H.264 などの符号化器を持たないのは、Windows の N エディション (Media Foundation 無し) でも動かすため (ADR 0134)。

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace mitiru::video
{

struct AviVideoSpec
{
	std::uint32_t width  = 0;
	std::uint32_t height = 0;
	std::uint32_t fps    = 60;
};

struct AviAudioSpec
{
	std::uint32_t sampleRate = 48000;
	std::uint16_t channels   = 2;
};

class AviMjpegWriter
{
public:
	/// 古い再生ソフトが符号付き 32 bit でオフセットを読むので、書き出しの上限はそこに合わせる
	static constexpr std::uint64_t kMaxFileBytes = 0x7FFFFFFFull - 64ull * 1024ull;

	~AviMjpegWriter() { (void)close(); }

	/// 上限を下げる (試験が 2 GB を実際に書かずに上限の止まり方を確かめるため)。open の前に呼ぶ
	void setMaxFileBytes(std::uint64_t bytes) noexcept { m_maxFileBytes = std::min(bytes, kMaxFileBytes); }

	/// @param audio nullptr なら映像だけ
	[[nodiscard]] bool open(const std::string& path, const AviVideoSpec& video, const AviAudioSpec* audio)
	{
		(void)close();
		m_error.clear();
		if (video.width == 0 || video.height == 0 || video.fps == 0)
		{
			m_error = "映像の大きさとフレームレートが 0 です";
			return false;
		}
		m_video = video;
		m_hasAudio = audio != nullptr;
		if (m_hasAudio) { m_audio = *audio; }
		m_file.open(path, std::ios::binary | std::ios::trunc);
		if (!m_file.is_open())
		{
			m_error = "書き出し先 " + path + " を開けません";
			return false;
		}
		m_videoFrames = m_audioSamples = 0;
		m_maxVideoChunk = m_maxAudioChunk = 0;
		m_moviBytes = 4;  // 'movi' の fourcc 分
		m_index.clear();
		const std::vector<std::uint8_t> head = buildHeader();
		m_headerBytes = head.size();
		m_file.write(reinterpret_cast<const char*>(head.data()), static_cast<std::streamsize>(head.size()));
		writeFourcc("LIST");
		m_moviSizePos = m_file.tellp();
		writeU32(0);
		writeFourcc("movi");
		m_open = m_file.good();
		if (!m_open) { m_error = "ヘッダーを書けません"; }
		return m_open;
	}

	[[nodiscard]] bool addVideoFrame(const std::uint8_t* jpeg, std::size_t size)
	{
		if (!m_open || size == 0) { return false; }
		if (!addChunk("00dc", jpeg, size)) { return false; }
		++m_videoFrames;
		m_maxVideoChunk = std::max<std::uint32_t>(m_maxVideoChunk, static_cast<std::uint32_t>(size));
		return true;
	}

	/// @param sampleFrames チャンネルをまとめた 1 サンプルを 1 と数える
	[[nodiscard]] bool addAudio(const std::int16_t* interleaved, std::size_t sampleFrames)
	{
		if (!m_open || !m_hasAudio || sampleFrames == 0) { return false; }
		const std::size_t bytes = sampleFrames * m_audio.channels * sizeof(std::int16_t);
		if (!addChunk("01wb", reinterpret_cast<const std::uint8_t*>(interleaved), bytes)) { return false; }
		m_audioSamples += sampleFrames;
		m_maxAudioChunk = std::max<std::uint32_t>(m_maxAudioChunk, static_cast<std::uint32_t>(bytes));
		return true;
	}

	/// @brief 索引を書き、ヘッダーを確定値で書き直す。二度呼んでもよい
	[[nodiscard]] bool close()
	{
		if (!m_open) { return true; }
		m_open = false;
		writeFourcc("idx1");
		writeU32(static_cast<std::uint32_t>(m_index.size() * 16));
		for (const IndexEntry& e : m_index)
		{
			writeFourcc(e.id);
			writeU32(0x10);  // AVIIF_KEYFRAME (MJPEG は全フレームが単独で復号でき、PCM も同じ)
			writeU32(e.offset);
			writeU32(e.size);
		}
		const std::uint64_t total = static_cast<std::uint64_t>(m_file.tellp());
		const std::vector<std::uint8_t> head = buildHeader(total - 8);
		m_file.seekp(0);
		m_file.write(reinterpret_cast<const char*>(head.data()), static_cast<std::streamsize>(head.size()));
		m_file.seekp(m_moviSizePos);
		writeU32(static_cast<std::uint32_t>(m_moviBytes));
		m_file.flush();
		const bool ok = m_file.good();
		m_file.close();
		if (!ok && m_error.empty()) { m_error = "書き出しの最後に失敗しました"; }
		return ok;
	}

	[[nodiscard]] const std::string& error() const noexcept { return m_error; }
	[[nodiscard]] std::uint64_t videoFrames() const noexcept { return m_videoFrames; }
	[[nodiscard]] std::uint64_t audioSamples() const noexcept { return m_audioSamples; }

private:
	struct IndexEntry
	{
		char          id[4];
		std::uint32_t offset;
		std::uint32_t size;
	};

	bool addChunk(const char* id, const std::uint8_t* data, std::size_t size)
	{
		const std::uint64_t padded = (static_cast<std::uint64_t>(size) + 1u) & ~1ull;
		// idx1 (16 byte/件) とヘッダーの分を足しても上限に収まる所までだけ書く
		const std::uint64_t projected = m_headerBytes + 12 + m_moviBytes + 8 + padded
			+ (m_index.size() + 1) * 16 + 8;
		if (projected > m_maxFileBytes)
		{
			m_error = "AVI が 2 GB に近づいたので止めました (OpenDML には対応していません)。"
			          "フレーム数か画質を減らしてください";
			return false;
		}
		IndexEntry e{};
		std::memcpy(e.id, id, 4);
		e.offset = static_cast<std::uint32_t>(m_moviBytes);
		e.size = static_cast<std::uint32_t>(size);
		writeFourcc(id);
		writeU32(static_cast<std::uint32_t>(size));
		m_file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
		if ((size & 1u) != 0) { m_file.put('\0'); }  // 偶数に揃える。サイズのフィールドには含めない
		m_moviBytes += 8 + padded;
		m_index.push_back(e);
		if (!m_file.good()) { m_error = "書き出し中に失敗しました (ディスクの空きを確かめてください)"; return false; }
		return true;
	}

	static void putU32(std::vector<std::uint8_t>& b, std::uint32_t v)
	{
		for (int i = 0; i < 4; ++i) { b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu)); }
	}
	static void putU16(std::vector<std::uint8_t>& b, std::uint16_t v)
	{
		b.push_back(static_cast<std::uint8_t>(v & 0xFFu));
		b.push_back(static_cast<std::uint8_t>(v >> 8));
	}
	static void putId(std::vector<std::uint8_t>& b, const char* id)
	{
		for (int i = 0; i < 4; ++i) { b.push_back(static_cast<std::uint8_t>(id[i])); }
	}
	static void patchU32(std::vector<std::uint8_t>& b, std::size_t pos, std::uint32_t v)
	{
		for (int i = 0; i < 4; ++i) { b[pos + i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu); }
	}

	void writeFourcc(const char* id) { m_file.write(id, 4); }
	void writeU32(std::uint32_t v)
	{
		std::vector<std::uint8_t> b;
		putU32(b, v);
		m_file.write(reinterpret_cast<const char*>(b.data()), 4);
	}

	/// 'RIFF' から 'LIST' ('movi') の手前まで。長さは映像と音の有無だけで決まり、確定値を入れても変わらない
	[[nodiscard]] std::vector<std::uint8_t> buildHeader(std::uint64_t riffSize = 0) const
	{
		std::vector<std::uint8_t> b;
		const std::uint32_t streams = m_hasAudio ? 2u : 1u;
		const std::uint32_t blockAlign = m_hasAudio ? m_audio.channels * 2u : 0u;
		const std::uint32_t avgBytes = m_hasAudio ? m_audio.sampleRate * blockAlign : 0u;
		const double seconds = m_videoFrames > 0 ? static_cast<double>(m_videoFrames) / m_video.fps : 0.0;
		const std::uint64_t dataBytes = m_moviBytes > 4 ? m_moviBytes - 4 : 0;
		const std::uint32_t maxBytesPerSec = seconds > 0.0
			? static_cast<std::uint32_t>(static_cast<double>(dataBytes) / seconds) : 0u;

		putId(b, "RIFF");
		putU32(b, static_cast<std::uint32_t>(riffSize));
		putId(b, "AVI ");

		const std::uint32_t strlVideo = 4 + (8 + 56) + (8 + 40);
		const std::uint32_t strlAudio = m_hasAudio ? 4 + (8 + 56) + (8 + 18) : 0u;
		const std::uint32_t hdrl = 4 + (8 + 56) + (8 + strlVideo) + (m_hasAudio ? 8 + strlAudio : 0u);
		putId(b, "LIST"); putU32(b, hdrl); putId(b, "hdrl");

		putId(b, "avih"); putU32(b, 56);
		putU32(b, 1000000u / m_video.fps);                 // dwMicroSecPerFrame
		putU32(b, maxBytesPerSec);
		putU32(b, 0);                                      // dwPaddingGranularity
		putU32(b, 0x10u | 0x100u);                         // AVIF_HASINDEX | AVIF_ISINTERLEAVED
		putU32(b, static_cast<std::uint32_t>(m_videoFrames));
		putU32(b, 0);                                      // dwInitialFrames
		putU32(b, streams);
		putU32(b, std::max(m_maxVideoChunk, m_maxAudioChunk));
		putU32(b, m_video.width);
		putU32(b, m_video.height);
		for (int i = 0; i < 4; ++i) { putU32(b, 0); }

		putId(b, "LIST"); putU32(b, strlVideo); putId(b, "strl");
		putId(b, "strh"); putU32(b, 56);
		putId(b, "vids"); putId(b, "MJPG");
		putU32(b, 0); putU16(b, 0); putU16(b, 0); putU32(b, 0);
		putU32(b, 1); putU32(b, m_video.fps); putU32(b, 0);  // dwScale, dwRate, dwStart
		putU32(b, static_cast<std::uint32_t>(m_videoFrames));
		putU32(b, m_maxVideoChunk);
		putU32(b, 0xFFFFFFFFu);                            // dwQuality (既定)
		putU32(b, 0);                                      // dwSampleSize (可変)
		putU16(b, 0); putU16(b, 0);
		putU16(b, static_cast<std::uint16_t>(m_video.width)); putU16(b, static_cast<std::uint16_t>(m_video.height));
		putId(b, "strf"); putU32(b, 40);
		putU32(b, 40); putU32(b, m_video.width); putU32(b, m_video.height);
		putU16(b, 1); putU16(b, 24);
		putId(b, "MJPG");
		putU32(b, m_video.width * m_video.height * 3u);
		putU32(b, 0); putU32(b, 0); putU32(b, 0); putU32(b, 0);

		if (m_hasAudio)
		{
			putId(b, "LIST"); putU32(b, strlAudio); putId(b, "strl");
			putId(b, "strh"); putU32(b, 56);
			putId(b, "auds"); putU32(b, 0);
			putU32(b, 0); putU16(b, 0); putU16(b, 0); putU32(b, 0);
			putU32(b, 1); putU32(b, m_audio.sampleRate); putU32(b, 0);
			putU32(b, static_cast<std::uint32_t>(m_audioSamples));
			putU32(b, m_maxAudioChunk);
			putU32(b, 0xFFFFFFFFu);
			putU32(b, blockAlign);
			putU16(b, 0); putU16(b, 0); putU16(b, 0); putU16(b, 0);
			putId(b, "strf"); putU32(b, 18);
			putU16(b, 1);                                  // WAVE_FORMAT_PCM
			putU16(b, m_audio.channels);
			putU32(b, m_audio.sampleRate);
			putU32(b, avgBytes);
			putU16(b, static_cast<std::uint16_t>(blockAlign));
			putU16(b, 16);
			putU16(b, 0);                                  // cbSize
		}
		return b;
	}

	std::ofstream           m_file;
	std::streampos          m_moviSizePos{};
	std::size_t             m_headerBytes = 0;
	std::vector<IndexEntry> m_index;
	AviVideoSpec            m_video;
	AviAudioSpec            m_audio;
	bool                    m_hasAudio = false;
	bool                    m_open = false;
	std::uint64_t           m_videoFrames = 0;
	std::uint64_t           m_audioSamples = 0;
	std::uint64_t           m_moviBytes = 4;
	std::uint32_t           m_maxVideoChunk = 0;
	std::uint32_t           m_maxAudioChunk = 0;
	std::uint64_t           m_maxFileBytes = kMaxFileBytes;
	std::string             m_error;
};

}  // namespace mitiru::video
