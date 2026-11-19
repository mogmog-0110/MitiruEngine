#pragma once

/// @file MmlBake.hpp
/// @brief assets/audio/<名前>.mml を <名前>.wav に焼く (mitiru_mmlbake と、mitiru_host --watch-assets が使う)
/// @details 書き方は docs/MML.md。ピークを -3 dBFS にそろえるのは、OPNA の出力が -20 dBFS 前後で曲ごとに違うため。
///          末尾の LIST チャンクに元の MML のハッシュを残し、同じなら焼き直さず、ハッシュの無い WAV (手で置いた音) は上書きしない。

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <mitiru_mml/OpnaSequencer.hpp>
#include <mitiru_mml/WavWriter.hpp>

#include <mitiru/audio/mml/MmlSong.hpp>

namespace mitiru::audio::mml
{

namespace detail
{

/// 焼き方を変えたら上げる。ハッシュに入るので、上げると全部が焼き直しになる
inline constexpr std::string_view kBakeTag = "mitiru_mmlbake 2";
/// ピークをそろえる先 (満振幅 = 1)。-3 dBFS
inline constexpr double kPeakTarget = 0.7079457843841379;
/// これより小さいピークは無音とみなす (-60 dBFS)
inline constexpr double kSilentPeak = 0.001;
inline constexpr std::string_view kMarkerPrefix = "mitiru_mmlbake ";
inline constexpr std::size_t      kMaxSeconds = 600;

[[nodiscard]] inline std::uint64_t fnv1a(std::string_view s, std::uint64_t h = 14695981039346656037ULL)
{
	for (const unsigned char c : s) { h = (h ^ c) * 1099511628211ULL; }
	return h;
}

[[nodiscard]] inline std::string hex16(std::uint64_t v)
{
	static constexpr char digits[] = "0123456789abcdef";
	std::string s(16, '0');
	for (int i = 15; i >= 0; --i, v >>= 4) { s[static_cast<std::size_t>(i)] = digits[v & 0xF]; }
	return s;
}

inline void put32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
	for (int i = 0; i < 4; ++i) { out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF)); }
}

inline void putTag(std::vector<std::uint8_t>& out, const char* tag)
{
	out.insert(out.end(), tag, tag + 4);
}

[[nodiscard]] inline std::optional<std::string> readText(const std::filesystem::path& p)
{
	std::ifstream f(p, std::ios::binary);
	if (!f) { return std::nullopt; }
	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

/// 別名に書き切ってから置き換える。エディタの保存と同じ形で、書き途中のファイルを --watch-assets に見せない。誤りは文で返す
[[nodiscard]] inline std::string writeAtomically(const std::filesystem::path& dest,
	const std::vector<std::uint8_t>& bytes)
{
	std::filesystem::path tmp = dest;
	tmp += ".tmp";
	{
		std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
		if (!f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
		{
			return tmp.filename().string() + " に書き出せません";
		}
	}
	std::error_code ec;
	std::filesystem::rename(tmp, dest, ec);
	if (!ec) { return {}; }
	// Windows は開いているファイルへの置き換えを拒む。開いたままでも名前は変えられるので、退けてから置く
	std::filesystem::path aside = dest;
	aside += ".old";
	std::filesystem::remove(aside, ec);
	std::filesystem::rename(dest, aside, ec);
	if (!ec)
	{
		std::filesystem::rename(tmp, dest, ec);
		std::error_code restore;
		if (ec) { std::filesystem::rename(aside, dest, restore); }   // 置けなかったときは元のファイルを戻す
	}
	if (ec)
	{
		std::filesystem::remove(tmp, ec);
		return dest.filename().string() + " に書き込めません (別のプロセスが使っています)";
	}
	std::filesystem::remove(aside, ec);   // 鳴らしている最中は消せない。次の焼きで消す
	return {};
}

}  // namespace detail

/// @brief 元の MML と焼き方から決まるハッシュ。改行の CRLF と LF は同じとみなす
[[nodiscard]] inline std::string sourceFingerprint(std::string_view text)
{
	std::uint64_t h = detail::fnv1a(detail::kBakeTag);
	std::string normalized;
	normalized.reserve(text.size());
	for (const char c : text) { if (c != '\r') { normalized.push_back(c); } }
	h = detail::fnv1a(normalized, h);
	return detail::hex16(h);
}

struct PcmResult
{
	std::vector<std::int16_t> pcm;
	std::uint32_t             rate = mitiru_mml::OpnaSequencer::OUTPUT_SAMPLE_RATE;
	std::string               error;

	[[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

[[nodiscard]] inline PcmResult renderSong(const ParsedSong& song)
{
	PcmResult out;
	mitiru_mml::OpnaSequencer seq;
	mml::addTracks(seq, song);
	out.pcm = seq.render(1);
	out.rate = seq.sampleRate();
	if (out.pcm.empty()) { out.error = "音が出ませんでした。音符が書かれているか確かめてください"; return out; }
	if (out.pcm.size() > static_cast<std::size_t>(out.rate) * detail::kMaxSeconds)
	{
		out.error = "10 分より長い音になりました。繰り返し [ ] の回数を確かめてください";
		out.pcm.clear();
		return out;
	}
	int peak = 0;
	for (const auto v : out.pcm) { peak = std::max(peak, std::abs(static_cast<int>(v))); }
	if (static_cast<double>(peak) / 32768.0 < detail::kSilentPeak)
	{
		out.error = "音が出ませんでした。音符が書かれているか、V (音量) が 0 になっていないか確かめてください";
		out.pcm.clear();
		return out;
	}
	const double gain = detail::kPeakTarget * 32768.0 / static_cast<double>(peak);
	for (auto& v : out.pcm)
	{
		v = static_cast<std::int16_t>(std::clamp(std::lround(static_cast<double>(v) * gain), -32768L, 32767L));
	}
	return out;
}

/// @brief WAV のバイト列。末尾の LIST チャンクにハッシュを残す (WAV の読み手は知らないチャンクを読み飛ばす)
[[nodiscard]] inline std::vector<std::uint8_t> toWavBytes(const PcmResult& r, const std::string& fingerprint)
{
	std::vector<std::uint8_t> wav = mitiru_mml::WavWriter::toWav(r.pcm, r.rate);
	std::string info(detail::kMarkerPrefix);
	info += fingerprint;
	info.push_back('\0');
	if (info.size() % 2 != 0) { info.push_back('\0'); }
	const auto isftSize = static_cast<std::uint32_t>(info.size());
	detail::putTag(wav, "LIST");
	detail::put32(wav, 4 + 8 + isftSize);
	detail::putTag(wav, "INFO");
	detail::putTag(wav, "ISFT");
	detail::put32(wav, isftSize);
	wav.insert(wav.end(), info.begin(), info.end());
	const auto riffSize = static_cast<std::uint32_t>(wav.size() - 8);
	for (std::size_t i = 0; i < 4; ++i) { wav[4 + i] = static_cast<std::uint8_t>((riffSize >> (8 * i)) & 0xFF); }
	return wav;
}

/// @brief WAV のハッシュ。焼いた WAV でなければ nullopt
[[nodiscard]] inline std::optional<std::string> readBakedFingerprint(const std::filesystem::path& wav)
{
	std::ifstream f(wav, std::ios::binary);
	if (!f) { return std::nullopt; }
	char head[12];
	if (!f.read(head, 12) || std::memcmp(head, "RIFF", 4) != 0 || std::memcmp(head + 8, "WAVE", 4) != 0)
	{
		return std::nullopt;
	}
	for (;;)
	{
		char hdr[8];
		if (!f.read(hdr, 8)) { return std::nullopt; }
		std::uint32_t size = 0;
		std::memcpy(&size, hdr + 4, 4);
		const std::uint32_t padded = size + (size & 1U);
		if (std::memcmp(hdr, "LIST", 4) == 0 && size >= 12 && size <= 256)
		{
			std::string body(size, '\0');
			if (!f.read(body.data(), static_cast<std::streamsize>(size))) { return std::nullopt; }
			const auto at = body.find(detail::kMarkerPrefix);
			const std::size_t idEnd = at + detail::kMarkerPrefix.size() + 16;
			if (body.compare(0, 4, "INFO") != 0 || at == std::string::npos || idEnd > body.size())
			{
				return std::nullopt;
			}
			return body.substr(at + detail::kMarkerPrefix.size(), 16);
		}
		f.seekg(static_cast<std::streamoff>(padded), std::ios::cur);
		if (!f) { return std::nullopt; }
	}
}

enum class BakeStatus
{
	Baked,      ///< WAV を書いた
	UpToDate,   ///< ハッシュが同じなので書かなかった
	Stale,      ///< dryRun で、焼き直しが要る (WAV が無いか、MML が変わった)
	Refused,    ///< 同じ名前の WAV が手で置かれた音なので書かなかった
	Failed,     ///< MML を読めない、音が出ない、書き出せない
};

struct BakeResult
{
	BakeStatus            status = BakeStatus::Failed;
	std::filesystem::path wav;
	std::string           message;   ///< 人に見せる 1 行。成功でも入る
	double                seconds = 0.0;   ///< 焼いた WAV の長さ
	double                elapsedSec = 0.0;   ///< 焼くのにかかった時間
};

[[nodiscard]] inline std::filesystem::path wavPathFor(const std::filesystem::path& mml)
{
	std::filesystem::path wav = mml;
	return wav.replace_extension(".wav");
}

/// @param force ハッシュが同じ WAV も、手で置いた WAV も焼き直す
/// @param dryRun 書かずに、焼き直しが要るかだけを返す
[[nodiscard]] inline BakeResult bakeFile(const std::filesystem::path& mml, bool force = false, bool dryRun = false)
{
	using Clock = std::chrono::steady_clock;
	const auto started = Clock::now();
	BakeResult out;
	out.wav = wavPathFor(mml);
	const std::string name = mml.filename().string();
	const auto text = detail::readText(mml);
	if (!text)
	{
		out.message = name + " を読めません";
		return out;
	}
	const std::string fingerprint = sourceFingerprint(*text);
	std::error_code ec;
	const bool wavExists = std::filesystem::is_regular_file(out.wav, ec);
	const auto existing = wavExists ? readBakedFingerprint(out.wav) : std::nullopt;
	if (!force && existing && *existing == fingerprint)
	{
		out.status = BakeStatus::UpToDate;
		out.message = out.wav.filename().string() + " は最新です";
		return out;
	}
	if (wavExists && !existing && !force)
	{
		out.status = BakeStatus::Refused;
		out.message = out.wav.filename().string() + " は手で置いた音なので上書きしません (上書きするなら mitiru_mmlbake --force)";
		return out;
	}
	if (dryRun)
	{
		out.status = BakeStatus::Stale;
		out.message = out.wav.filename().string() + (wavExists ? " は古いので焼き直しが要ります" : " がないので焼きます");
		return out;
	}
	const ParsedSong song = parseMml(*text);
	if (!song.ok()) { out.message = name + " の " + song.error; return out; }
	const PcmResult pcm = renderSong(song);
	if (!pcm.ok()) { out.message = name + ": " + pcm.error; return out; }
	const std::string writeError = detail::writeAtomically(out.wav, toWavBytes(pcm, fingerprint));
	if (!writeError.empty()) { out.message = writeError; return out; }
	out.status = BakeStatus::Baked;
	out.seconds = static_cast<double>(pcm.pcm.size()) / static_cast<double>(pcm.rate);
	out.elapsedSec = std::chrono::duration<double>(Clock::now() - started).count();
	out.message = out.wav.filename().string() + " を焼きました";
	return out;
}

}  // namespace mitiru::audio::mml
