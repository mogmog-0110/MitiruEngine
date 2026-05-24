/// @file TestSiv3DAudioPlayer.cpp
/// @brief Siv3DAudioPlayer adapter tests with stub

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/siv3d/Siv3DAudioPlayer.hpp"

using namespace sgc::siv3d;
using namespace siv3d_stub;
using Catch::Approx;

// ── テスト前にスタブをリセット ─────────────────────────

namespace
{

struct ResetFixture
{
	ResetFixture() { siv3d_stub::reset(); }
};

} // anonymous namespace

// ── BGM tests ───────────────────────────────────────────────

TEST_CASE("Siv3DAudioPlayer playBgm starts playback", "[siv3d][audio]")
{
	ResetFixture fix;
	Siv3DAudioPlayer player;

	player.playBgm("bgm/title.mp3");

	REQUIRE(player.isBgmPlaying());
	REQUIRE_FALSE(audioRecords().empty());
}

TEST_CASE("Siv3DAudioPlayer stopBgm stops playback", "[siv3d][audio]")
{
	ResetFixture fix;
	Siv3DAudioPlayer player;

	player.playBgm("bgm/title.mp3");
	player.stopBgm();

	REQUIRE_FALSE(player.isBgmPlaying());
}

TEST_CASE("Siv3DAudioPlayer stopBgm with fade out", "[siv3d][audio]")
{
	ResetFixture fix;
	Siv3DAudioPlayer player;

	player.playBgm("bgm/title.mp3");
	player.stopBgm(1.0f);

	REQUIRE_FALSE(player.isBgmPlaying());
}

TEST_CASE("Siv3DAudioPlayer pauseBgm pauses playback", "[siv3d][audio]")
{
	ResetFixture fix;
	Siv3DAudioPlayer player;

	player.playBgm("bgm/title.mp3");
	player.pauseBgm();

	REQUIRE_FALSE(player.isBgmPlaying());
}

TEST_CASE("Siv3DAudioPlayer resumeBgm restarts playback", "[siv3d][audio]")
{
	ResetFixture fix;
	Siv3DAudioPlayer player;

	player.playBgm("bgm/title.mp3");
	player.pauseBgm();
	player.resumeBgm();

	REQUIRE(player.isBgmPlaying());
}

TEST_CASE("Siv3DAudioPlayer setBgmVolume updates volume", "[siv3d][audio]")
{
	ResetFixture fix;
	Siv3DAudioPlayer player;

	player.playBgm("bgm/title.mp3", 1.0f);
	player.setBgmVolume(0.5f);

	// ボリュームは 0.5(bgmVol) * 1.0(master) = 0.5
	// audioRecordの最後のvolume設定を確認
	bool foundVolume = false;
	for (const auto& call : drawCalls())
	{
		if (call.type == DrawType::AudioSetVolume)
		{
			if (call.params[0] == Approx(0.5))
			{
				foundVolume = true;
			}
		}
	}
	REQUIRE(foundVolume);
}

TEST_CASE("Siv3DAudioPlayer playBgm replaces current BGM", "[siv3d][audio]")
{
	ResetFixture fix;
	Siv3DAudioPlayer player;

	player.playBgm("bgm/title.mp3");
	player.playBgm("bgm/battle.mp3");

	REQUIRE(player.isBgmPlaying());
	// 2つのAudioがロードされている
	REQUIRE(audioRecords().size() == 2);
}

// ── SE tests ────────────────────────────────────────────────

TEST_CASE("Siv3DAudioPlayer playSe returns positive handle", "[siv3d][audio]")
{
	ResetFixture fix;
	Siv3DAudioPlayer player;

	const int h = player.playSe("se/shot.wav");

	REQUIRE(h > 0);
}

TEST_CASE("Siv3DAudioPlayer playSe plays audio", "[siv3d][audio]")
{
	ResetFixture fix;
	Siv3DAudioPlayer player;

	player.playSe("se/shot.wav");

	// playOneShotのDrawCallが記録されている
	bool found = false;
	for (const auto& call : drawCalls())
	{
		if (call.type == DrawType::AudioPlayOneShot)
		{
			found = true;
		}
	}
	REQUIRE(found);
}

TEST_CASE("Siv3DAudioPlayer stopSe stops specific SE", "[siv3d][audio]")
{
	ResetFixture fix;
	Siv3DAudioPlayer player;

	const int h1 = player.playSe("se/shot.wav");
	player.playSe("se/hit.wav");

	player.stopSe(h1);

	// AudioStopが記録されている
	bool foundStop = false;
	for (const auto& call : drawCalls())
	{
		if (call.type == DrawType::AudioStop)
		{
			foundStop = true;
		}
	}
	REQUIRE(foundStop);
}

TEST_CASE("Siv3DAudioPlayer stopAllSe stops all SEs", "[siv3d][audio]")
{
	ResetFixture fix;
	Siv3DAudioPlayer player;

	player.playSe("se/shot.wav");
	player.playSe("se/hit.wav");

	player.stopAllSe();

	// AudioStopが複数記録されている
	int stopCount = 0;
	for (const auto& call : drawCalls())
	{
		if (call.type == DrawType::AudioStop)
		{
			++stopCount;
		}
	}
	REQUIRE(stopCount >= 2);
}

// ── Volume tests ────────────────────────────────────────────

TEST_CASE("Siv3DAudioPlayer setMasterVolume affects BGM volume", "[siv3d][audio]")
{
	ResetFixture fix;
	Siv3DAudioPlayer player;

	player.playBgm("bgm/title.mp3", 1.0f);
	player.setMasterVolume(0.5f);

	// 1.0(bgm) * 0.5(master) = 0.5
	bool foundHalfVolume = false;
	for (const auto& call : drawCalls())
	{
		if (call.type == DrawType::AudioSetVolume && call.params[0] == Approx(0.5))
		{
			foundHalfVolume = true;
		}
	}
	REQUIRE(foundHalfVolume);
}

TEST_CASE("Siv3DAudioPlayer playBgm with custom volume", "[siv3d][audio]")
{
	ResetFixture fix;
	Siv3DAudioPlayer player;

	player.playBgm("bgm/title.mp3", 0.5f);

	// 0.5 * 1.0(bgmVol) * 1.0(master) = 0.5
	bool foundVolume = false;
	for (const auto& call : drawCalls())
	{
		if (call.type == DrawType::AudioSetVolume && call.params[0] == Approx(0.5))
		{
			foundVolume = true;
		}
	}
	REQUIRE(foundVolume);
}

// ── Cache test ──────────────────────────────────────────────

TEST_CASE("Siv3DAudioPlayer caches loaded audio by path", "[siv3d][audio]")
{
	ResetFixture fix;
	Siv3DAudioPlayer player;

	player.playSe("se/shot.wav");
	player.playSe("se/shot.wav");

	// 同じパスは1回だけAudioがロードされる
	// audioRecordsは1つだけ
	REQUIRE(audioRecords().size() == 1);
}

TEST_CASE("Siv3DAudioPlayer different paths create separate audio", "[siv3d][audio]")
{
	ResetFixture fix;
	Siv3DAudioPlayer player;

	player.playSe("se/shot.wav");
	player.playSe("se/hit.wav");

	REQUIRE(audioRecords().size() == 2);
}
