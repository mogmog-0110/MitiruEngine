/// @file TestAudioBus.cpp
/// @brief AudioBus.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/audio/AudioBus.hpp"

using Catch::Approx;
using sgc::audio::AudioBusManager;
using sgc::audio::BusId;

TEST_CASE("AudioBusManager default volume is 1.0", "[audio][bus]")
{
	AudioBusManager mixer;
	REQUIRE(mixer.getVolume(BusId::Master) == Approx(1.0f));
	REQUIRE(mixer.getVolume(BusId::BGM) == Approx(1.0f));
	REQUIRE(mixer.getVolume(BusId::SFX) == Approx(1.0f));
}

TEST_CASE("AudioBusManager setVolume clamps to [0,1]", "[audio][bus]")
{
	AudioBusManager mixer;
	mixer.setVolume(BusId::BGM, 1.5f);
	REQUIRE(mixer.getVolume(BusId::BGM) == Approx(1.0f));

	mixer.setVolume(BusId::BGM, -0.5f);
	REQUIRE(mixer.getVolume(BusId::BGM) == Approx(0.0f));
}

TEST_CASE("AudioBusManager effective volume multiplies parent", "[audio][bus]")
{
	AudioBusManager mixer;
	mixer.setVolume(BusId::Master, 0.5f);
	mixer.setVolume(BusId::BGM, 0.8f);

	REQUIRE(mixer.getEffectiveVolume(BusId::BGM) == Approx(0.4f));
}

TEST_CASE("AudioBusManager mute/unmute", "[audio][bus]")
{
	AudioBusManager mixer;
	REQUIRE_FALSE(mixer.isMuted(BusId::SFX));

	mixer.setMuted(BusId::SFX, true);
	REQUIRE(mixer.isMuted(BusId::SFX));
	REQUIRE(mixer.getEffectiveVolume(BusId::SFX) == Approx(0.0f));

	mixer.setMuted(BusId::SFX, false);
	REQUIRE_FALSE(mixer.isMuted(BusId::SFX));
}

TEST_CASE("AudioBusManager effective mute from parent", "[audio][bus]")
{
	AudioBusManager mixer;
	mixer.setMuted(BusId::Master, true);

	/// 子バス自体はミュートしていないが、親がミュートなので実効ミュート
	REQUIRE_FALSE(mixer.isMuted(BusId::BGM));
	REQUIRE(mixer.isEffectivelyMuted(BusId::BGM));
	REQUIRE(mixer.getEffectiveVolume(BusId::BGM) == Approx(0.0f));
}

TEST_CASE("AudioBusManager muteAll/unmuteAll", "[audio][bus]")
{
	AudioBusManager mixer;
	mixer.muteAll();

	REQUIRE(mixer.isMuted(BusId::Master));
	REQUIRE(mixer.isMuted(BusId::BGM));
	REQUIRE(mixer.isMuted(BusId::SFX));
	REQUIRE(mixer.isMuted(BusId::Voice));

	mixer.unmuteAll();

	REQUIRE_FALSE(mixer.isMuted(BusId::Master));
	REQUIRE_FALSE(mixer.isMuted(BusId::BGM));
}

TEST_CASE("AudioBusManager setParent changes hierarchy", "[audio][bus]")
{
	AudioBusManager mixer;
	/// VoiceをSFXの子に設定
	mixer.setParent(BusId::Voice, BusId::SFX);
	mixer.setVolume(BusId::SFX, 0.5f);
	mixer.setVolume(BusId::Voice, 0.6f);
	mixer.setVolume(BusId::Master, 1.0f);

	/// Voice実効音量 = Voice * SFX * Master = 0.6 * 0.5 * 1.0 = 0.3
	REQUIRE(mixer.getEffectiveVolume(BusId::Voice) == Approx(0.3f));
}

TEST_CASE("AudioBusManager master effective volume equals own volume", "[audio][bus]")
{
	AudioBusManager mixer;
	mixer.setVolume(BusId::Master, 0.7f);
	REQUIRE(mixer.getEffectiveVolume(BusId::Master) == Approx(0.7f));
}
