/// @file TestSend.cpp
/// @brief #track の send=(リバーブ、ディレイ、コーラスへの送り)が ModernSong のミックスに反映されることを検査する

#include <catch2/catch_test_macros.hpp>

#include "StreamTestUtil.hpp"

#include <mitiru_mml/MitiruMML.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mitiru_mml;
using namespace mitiru_mml_test;

namespace
{

// 生のミックス式を確認するため、リミッター、マスターゲイン、幅、ディザを使わない
const char* const SEND_HEAD =
	"#tempo 120\n#limiter off\n#master gain=0 width=1\n#dither off\n#tail 1.5\n";

/// @param sendA トラック a の #track 行に足す項目(例: "send=reverb:80")
std::string sendSong(const std::string& sendA, const std::string& extraHead = "")
{
	return std::string(SEND_HEAD) + extraHead
		+ "#track a chip=synth voice=pad group=g " + sendA + "\nO4 L8 C E G\n"
		+ "#track b chip=synth voice=saw-bass group=g\nO3 L4 C R C R\n";
}

const StereoBuffer* stemNamed(const ModernSong::Result& result, const std::string& name)
{
	for (const auto& stem : result.stems)
	{
		if (stem.name == name)
		{
			return &stem.audio;
		}
	}
	return nullptr;
}

double energyBetween(const StereoBuffer& audio, std::size_t from, std::size_t to)
{
	double sum = 0.0;
	to = std::min(to, audio.frames());
	for (std::size_t i = from; i < to; ++i)
	{
		sum += static_cast<double>(audio.samples[i * 2]) * audio.samples[i * 2]
			+ static_cast<double>(audio.samples[i * 2 + 1]) * audio.samples[i * 2 + 1];
	}
	return sum;
}

double peakOf(const StereoBuffer& audio)
{
	double peak = 0.0;
	for (const float s : audio.samples)
	{
		peak = std::max(peak, std::fabs(static_cast<double>(s)));
	}
	return peak;
}

ModernSong::Result renderText(const std::string& text)
{
	auto parsed = ModernSongScript::parse(text);
	REQUIRE(parsed.ok());
	return parsed.song.render();
}

} // namespace

TEST_CASE("send= adds a wet return and leaves the dry stems alone", "[send][grammar][modern]")
{
	const ModernSong::Result without = renderText(sendSong(""));
	const ModernSong::Result with = renderText(sendSong("send=reverb:80"));

	REQUIRE(without.stems.size() == 1);
	REQUIRE(with.stems.size() == 2);
	CHECK(with.stems[1].name == "send_reverb");
	CHECK(sameBits(with.stems[0].audio.samples, without.stems[0].audio.samples));
	CHECK_FALSE(sameBits(with.mix.samples, without.mix.samples));
	REQUIRE(with.mix.frames() == without.mix.frames());

	// 戻りはミックスの手前でステムの和に足され、戻りを含めてもステムの和がミックスになる
	const StereoBuffer& wet = with.stems[1].audio;
	REQUIRE(wet.frames() == with.mix.frames());
	CHECK(energyBetween(wet, 0, wet.frames()) > 0.0);
	for (std::size_t i = 0; i < with.mix.samples.size(); ++i)
	{
		REQUIRE(with.mix.samples[i] == with.stems[0].audio.samples[i] + wet.samples[i]);
	}
	// 残響は、トラック b が終わる 2 秒後より先にも残る
	const std::size_t late = 44100 * 2 + 4410;
	CHECK(energyBetween(wet, late, wet.frames()) > 0.0);
	CHECK(energyBetween(without.mix, late, without.mix.frames()) < energyBetween(with.mix, late, with.mix.frames()));
}

TEST_CASE("send amount scales the return linearly and zero means no send", "[send][grammar][modern]")
{
	const ModernSong::Result low = renderText(sendSong("send=reverb:20"));
	const ModernSong::Result high = renderText(sendSong("send=reverb:80"));
	const StereoBuffer& wetLow = *stemNamed(low, "send_reverb");
	const StereoBuffer& wetHigh = *stemNamed(high, "send_reverb");
	REQUIRE(wetLow.samples.size() == wetHigh.samples.size());
	const double peak = peakOf(wetHigh);
	REQUIRE(peak > 1.0e-4);
	for (std::size_t i = 0; i < wetLow.samples.size(); ++i)
	{
		REQUIRE(std::fabs(static_cast<double>(wetLow.samples[i]) * 4.0 - wetHigh.samples[i]) <= peak * 1.0e-4);
	}

	// 0% は指定なしと同じで、出力は 1 ビットも変わらず、戻りのステムも作らない
	const ModernSong::Result none = renderText(sendSong(""));
	const ModernSong::Result zero = renderText(sendSong("send=reverb:0,delay:0,chorus:0"));
	CHECK(zero.stems.size() == 1);
	CHECK(sameBits(zero.mix.samples, none.mix.samples));
}

TEST_CASE("The delay returns echoes after its delay time and the chorus after its base delay", "[send][grammar][modern]")
{
	const ModernSong::Result delayed = renderText(sendSong("send=delay:100"));
	const StereoBuffer& echo = *stemNamed(delayed, "send_delay");
	// 既定の 0.25 秒より前には反復がない
	CHECK(energyBetween(echo, 0, 11025) == 0.0);
	CHECK(energyBetween(echo, 11025, 11025 + 22050) > 0.0);

	const ModernSong::Result chorused = renderText(sendSong("send=chorus:100"));
	const StereoBuffer& voice = *stemNamed(chorused, "send_chorus");
	CHECK(energyBetween(voice, 0, 700) == 0.0);
	CHECK(energyBetween(voice, 700, voice.frames()) > 0.0);

	// 3 つ同時に送ると、戻りのステムは reverb、delay、chorus の順に並ぶ
	const ModernSong::Result all = renderText(sendSong("send=chorus:30,reverb:30,delay:30"));
	REQUIRE(all.stems.size() == 4);
	CHECK(all.stems[1].name == "send_reverb");
	CHECK(all.stems[2].name == "send_delay");
	CHECK(all.stems[3].name == "send_chorus");
}

TEST_CASE("Each track sends on its own", "[send][grammar][modern]")
{
	// トラック a の送りは、トラック a の音だけで決まる。同じグループのトラック b の音量を 0 にしても、戻りはビット単位で同じになる
	// 休符にすると曲の最後のイベントが早まり、尾の描き方が変わるため、同じ音符のまま音量だけを変える
	const ModernSong::Result both = renderText(sendSong("send=reverb:50"));
	const std::string bSilent = std::string(SEND_HEAD)
		+ "#track a chip=synth voice=pad group=g send=reverb:50\nO4 L8 C E G\n"
		+ "#track b chip=synth voice=saw-bass group=g vol=0\nO3 L4 C R C R\n";
	const ModernSong::Result silent = renderText(bSilent);
	const StereoBuffer& wetBoth = *stemNamed(both, "send_reverb");
	const StereoBuffer& wetSilent = *stemNamed(silent, "send_reverb");
	REQUIRE(peakOf(wetBoth) > 1.0e-4);
	CHECK(sameBits(wetBoth.samples, wetSilent.samples));
	CHECK_FALSE(sameBits(both.stems[0].audio.samples, silent.stems[0].audio.samples));

	// 送りを持つトラックを入れ替えると、戻りも変わる
	const std::string bSends = std::string(SEND_HEAD)
		+ "#track a chip=synth voice=pad group=g\nO4 L8 C E G\n"
		+ "#track b chip=synth voice=saw-bass group=g send=reverb:50\nO3 L4 C R C R\n";
	const ModernSong::Result other = renderText(bSends);
	CHECK_FALSE(sameBits(stemNamed(other, "send_reverb")->samples, wetBoth.samples));
	// dry は、どのトラックが送っても同じ
	CHECK(sameBits(other.stems[0].audio.samples, both.stems[0].audio.samples));
}

TEST_CASE("Sends follow the group gain and the ducking of their group", "[send][grammar][modern]")
{
	const ModernSong::Result unity = renderText(sendSong("send=reverb:60"));
	const ModernSong::Result quiet = renderText(sendSong("send=reverb:60", "#group g gain=-6\n"));
	const StereoBuffer& wetUnity = *stemNamed(unity, "send_reverb");
	const StereoBuffer& wetQuiet = *stemNamed(quiet, "send_reverb");
	const double gain = static_cast<double>(dbToLinear(-6.0f));
	const double peak = peakOf(wetUnity);
	REQUIRE(wetQuiet.samples.size() == wetUnity.samples.size());
	for (std::size_t i = 0; i < wetQuiet.samples.size(); ++i)
	{
		REQUIRE(std::fabs(static_cast<double>(wetUnity.samples[i]) * gain - wetQuiet.samples[i]) <= peak * 1.0e-4);
	}

	// グループがキックで下がると、そのグループの送りも下がる
	const std::string duckTracks = "#track k chip=drums drum=kick group=kick\nO4 L4 [C]4\n";
	const std::string duckHead = "#duck target=g by=kick depth=-12 attack=3 release=300\n";
	const ModernSong::Result plain = renderText(sendSong("send=reverb:60", "") + duckTracks);
	const ModernSong::Result ducked = renderText(sendSong("send=reverb:60", duckHead) + duckTracks);
	const StereoBuffer& wetPlain = *stemNamed(plain, "send_reverb");
	const StereoBuffer& wetDucked = *stemNamed(ducked, "send_reverb");
	CHECK(peakOf(wetDucked) < peakOf(wetPlain));
	CHECK(energyBetween(wetDucked, 0, 44100) < energyBetween(wetPlain, 0, 44100));
}

TEST_CASE("The send values are checked", "[send][grammar][modern]")
{
	for (const float bad : {1.5f, -0.1f, std::numeric_limits<float>::quiet_NaN()})
	{
		ModernSong song;
		ModernSong::TrackSpec spec;
		spec.name = "a";
		spec.mml = "O4 C";
		spec.sends[SendBus::DELAY] = bad;
		song.addTrack(spec);
		CHECK_THROWS_AS(song.render(), std::invalid_argument);
		CHECK_THROWS_AS(ModernSongStream(song), std::invalid_argument);
	}

	// 戻りのステムと同じ名前のグループは、書き出すファイルが重なるため作れない
	auto clash = ModernSongScript::parse("#track a chip=synth group=send_reverb send=reverb:20\nC\n");
	REQUIRE(clash.ok());
	CHECK_THROWS_AS(clash.song.render(), std::invalid_argument);
	auto fine = ModernSongScript::parse("#track a chip=synth group=send_reverb send=delay:20\nC\n");
	REQUIRE(fine.ok());
	CHECK_NOTHROW(fine.song.render());

	// ModernSongScript は % を 0〜1 に直して TrackSpec に渡し、ParseResult::sends にも残す
	auto parsed = ModernSongScript::parse("#track a chip=synth send=chorus:25,reverb:100\nC\n");
	REQUIRE(parsed.ok());
	REQUIRE(parsed.sends.size() == 2);
	CHECK(parsed.sends[0].effect == "chorus");
	CHECK(parsed.sends[0].percent == 25.0f);
}

TEST_CASE("Sends work on the older chips", "[send][grammar][modern]")
{
	const std::string text = std::string(SEND_HEAD)
		+ "#track lead chip=opm voice=lead group=retro send=delay:40\nO5 L8 E G A B\n"
		+ "#track beep chip=sn76489 group=retro send=reverb:30\nO4 L16 [C E G >C< G E]2\n"
		+ "#track fm chip=opna voice=piano group=retro\nO4 L4 C E G R\n";
	const ModernSong::Result result = renderText(text);
	REQUIRE(result.stems.size() == 3);
	CHECK(result.stems[1].name == "send_reverb");
	CHECK(result.stems[2].name == "send_delay");
	CHECK(energyBetween(result.stems[1].audio, 0, result.stems[1].audio.frames()) > 0.0);
	CHECK(energyBetween(result.stems[2].audio, 11025, result.stems[2].audio.frames()) > 0.0);
}

TEST_CASE("ModernSong without sends renders exactly as before", "[send][grammar][modern][hash]")
{
	// 送りを使わない曲は送りの処理に入らず、ステムも増えない。2 回描いても同じになる
	const std::string text = sendSong("");
	auto parsed = ModernSongScript::parse(text);
	REQUIRE(parsed.ok());
	const auto first = parsed.song.render();
	const auto second = parsed.song.render();
	CHECK(first.stems.size() == 1);
	CHECK(sameBits(first.mix.samples, second.mix.samples));
}
