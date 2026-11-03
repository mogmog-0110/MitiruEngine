#pragma once

/// @file WebmDemux.hpp
/// @brief WebM (Matroska) から AV1 の映像トラックのパケットを順に取り出す (ADR 0116)。読むのは libwebm の mkvparser
/// @details 音のトラックは読み飛ばす。動画の音は別のファイルを鳴らす決まり (ADR 0104) で、miniaudio は Matroska を読めない。

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::video
{

class WebmDemux
{
public:
	~WebmDemux();
	WebmDemux(const WebmDemux&) = delete;
	WebmDemux& operator=(const WebmDemux&) = delete;

	/// bytes は WebmDemux が持つ。失敗したら nullptr を返し、error に理由を入れる
	[[nodiscard]] static std::unique_ptr<WebmDemux> open(std::vector<std::uint8_t>&& bytes, std::string* error);

	[[nodiscard]] double fps() const noexcept { return m_fps; }
	[[nodiscard]] std::size_t frameCount() const noexcept { return m_packets.size(); }
	/// av1C (CodecPrivate) の 4 byte の後ろにある configOBUs。無ければ空
	[[nodiscard]] const std::vector<std::uint8_t>& configObus() const noexcept { return m_configObus; }

	/// 次のパケットを out に入れて true を返す。終わりなら false
	bool next(std::vector<std::uint8_t>& out);
	void rewind() noexcept { m_cursor = 0; }

private:
	WebmDemux() = default;

	struct Packet
	{
		std::size_t   offset = 0;   ///< m_bytes の中の位置
		std::size_t   size = 0;
		long long     timeNs = 0;
	};

	std::vector<std::uint8_t> m_bytes;
	std::vector<Packet>       m_packets;
	std::vector<std::uint8_t> m_configObus;
	double                    m_fps = 0.0;
	std::size_t               m_cursor = 0;
};

}  // namespace mitiru::video
