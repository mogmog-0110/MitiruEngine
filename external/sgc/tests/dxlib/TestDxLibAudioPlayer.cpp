/// @file TestDxLibAudioPlayer.cpp
/// @brief DxLibAudioPlayer adapter tests with stub

#include <catch2/catch_test_macros.hpp>

#include "sgc/dxlib/DxLibAudioPlayer.hpp"

using namespace sgc::dxlib;
using namespace dxlib_stub;

// ── テスト前にスタブをリセット ─────────────────────────

namespace
{

struct ResetFixture
{
	ResetFixture() { dxlib_stub::reset(); }
};

} // anonymous namespace

// ── BGM tests ───────────────────────────────────────────────

TEST_CASE("DxLibAudioPlayer playBgm starts playback", "[dxlib][audio]")
{
	ResetFixture fix;
	DxLibAudioPlayer player;

	player.playBgm("bgm/title.mp3");

	REQUIRE(player.isBgmPlaying());
	REQUIRE(soundRecords().size() == 1);
	REQUIRE(soundRecords()[0].playing);
}

TEST_CASE("DxLibAudioPlayer stopBgm stops playback", "[dxlib][audio]")
{
	ResetFixture fix;
	DxLibAudioPlayer player;

	player.playBgm("bgm/title.mp3");
	player.stopBgm();

	REQUIRE_FALSE(player.isBgmPlaying());
	REQUIRE_FALSE(soundRecords()[0].playing);
}

TEST_CASE("DxLibAudioPlayer pauseBgm stops sound", "[dxlib][audio]")
{
	ResetFixture fix;
	DxLibAudioPlayer player;

	player.playBgm("bgm/title.mp3");
	player.pauseBgm();

	REQUIRE_FALSE(player.isBgmPlaying());
}

TEST_CASE("DxLibAudioPlayer resumeBgm restarts playback", "[dxlib][audio]")
{
	ResetFixture fix;
	DxLibAudioPlayer player;

	player.playBgm("bgm/title.mp3");
	player.pauseBgm();
	player.resumeBgm();

	REQUIRE(player.isBgmPlaying());
}

TEST_CASE("DxLibAudioPlayer setBgmVolume updates volume", "[dxlib][audio]")
{
	ResetFixture fix;
	DxLibAudioPlayer player;

	player.playBgm("bgm/title.mp3");
	player.setBgmVolume(0.5f);

	// 0.5 * 1.0(master) * 255 = 128 (approximately)
	REQUIRE(soundRecords()[0].volume >= 127);
	REQUIRE(soundRecords()[0].volume <= 128);
}

// ── SE tests ────────────────────────────────────────────────

TEST_CASE("DxLibAudioPlayer playSe loads and plays sound", "[dxlib][audio]")
{
	ResetFixture fix;
	DxLibAudioPlayer player;

	const int h = player.playSe("se/shot.wav");

	REQUIRE(h > 0);
	REQUIRE(soundRecords().size() == 1);
	REQUIRE(soundRecords()[0].playing);
}

TEST_CASE("DxLibAudioPlayer stopSe stops specific SE", "[dxlib][audio]")
{
	ResetFixture fix;
	DxLibAudioPlayer player;

	const int h1 = player.playSe("se/shot.wav");
	player.playSe("se/hit.wav");

	player.stopSe(h1);

	// shot.wav should be stopped
	REQUIRE_FALSE(soundRecords()[0].playing);
	// hit.wav should still be playing
	REQUIRE(soundRecords()[1].playing);
}

TEST_CASE("DxLibAudioPlayer stopAllSe stops all SE", "[dxlib][audio]")
{
	ResetFixture fix;
	DxLibAudioPlayer player;

	player.playSe("se/shot.wav");
	player.playSe("se/hit.wav");

	player.stopAllSe();

	for (const auto& rec : soundRecords())
	{
		REQUIRE_FALSE(rec.playing);
	}
}

// ── Volume tests ────────────────────────────────────────────

TEST_CASE("DxLibAudioPlayer setMasterVolume affects BGM", "[dxlib][audio]")
{
	ResetFixture fix;
	DxLibAudioPlayer player;

	player.playBgm("bgm/title.mp3", 1.0f);
	player.setMasterVolume(0.5f);

	// 1.0(bgm) * 0.5(master) * 255 = 128
	REQUIRE(soundRecords()[0].volume >= 127);
	REQUIRE(soundRecords()[0].volume <= 128);
}

TEST_CASE("DxLibAudioPlayer playBgm with custom volume", "[dxlib][audio]")
{
	ResetFixture fix;
	DxLibAudioPlayer player;

	player.playBgm("bgm/title.mp3", 0.5f);

	// 0.5 * 1.0(bgmVol) * 1.0(master) * 255 = 128
	REQUIRE(soundRecords()[0].volume >= 127);
	REQUIRE(soundRecords()[0].volume <= 128);
}

// ── Cache test ──────────────────────────────────────────────

TEST_CASE("DxLibAudioPlayer caches loaded sounds", "[dxlib][audio]")
{
	ResetFixture fix;
	DxLibAudioPlayer player;

	player.playSe("se/shot.wav");
	player.playSe("se/shot.wav");

	// same path should reuse the same sound handle (only 1 LoadSoundMem call)
	REQUIRE(soundRecords().size() == 1);
}

// ── Destructor test ─────────────────────────────────────────

TEST_CASE("DxLibAudioPlayer destructor deletes all sounds", "[dxlib][audio]")
{
	ResetFixture fix;

	{
		DxLibAudioPlayer player;
		player.playSe("se/shot.wav");
		player.playSe("se/hit.wav");
	}

	for (const auto& rec : soundRecords())
	{
		REQUIRE(rec.deleted);
	}
}
