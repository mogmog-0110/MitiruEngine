/// @file TestAudioPlayer.cpp
/// @brief IAudioPlayer interface tests with MockAudioPlayer

#include <catch2/catch_test_macros.hpp>

#include "sgc/audio/IAudioPlayer.hpp"

#include <string>
#include <unordered_map>
#include <vector>

// ── MockAudioPlayer ─────────────────────────────────────────

namespace
{

/// @brief テスト用モックオーディオプレイヤー
class MockAudioPlayer : public sgc::IAudioPlayer
{
public:
	void playBgm(std::string_view path, float volume = 1.0f) override
	{
		m_bgmPath = std::string{path};
		m_bgmVolume = volume;
		m_bgmPlaying = true;
		m_bgmPaused = false;
	}

	void stopBgm(float fadeOutSeconds = 0.0f) override
	{
		m_bgmFadeOut = fadeOutSeconds;
		m_bgmPlaying = false;
		m_bgmPaused = false;
	}

	void pauseBgm() override
	{
		if (m_bgmPlaying)
		{
			m_bgmPaused = true;
			m_bgmPlaying = false;
		}
	}

	void resumeBgm() override
	{
		if (m_bgmPaused)
		{
			m_bgmPaused = false;
			m_bgmPlaying = true;
		}
	}

	void setBgmVolume(float volume) override
	{
		m_bgmVolume = volume;
	}

	[[nodiscard]] bool isBgmPlaying() const override
	{
		return m_bgmPlaying;
	}

	int playSe(std::string_view path, float volume = 1.0f) override
	{
		const int handle = m_nextHandle++;
		m_seHandles[handle] = SeInfo{std::string{path}, volume, true};
		return handle;
	}

	void stopSe(int handle) override
	{
		const auto it = m_seHandles.find(handle);
		if (it != m_seHandles.end())
		{
			it->second.playing = false;
		}
	}

	void stopAllSe() override
	{
		for (auto& [handle, info] : m_seHandles)
		{
			info.playing = false;
		}
	}

	void setSeVolume(float volume) override
	{
		m_seVolume = volume;
	}

	void setMasterVolume(float volume) override
	{
		m_masterVolume = volume;
	}

	// テスト検証用アクセサ
	[[nodiscard]] const std::string& bgmPath() const { return m_bgmPath; }
	[[nodiscard]] float bgmVolume() const { return m_bgmVolume; }
	[[nodiscard]] float bgmFadeOut() const { return m_bgmFadeOut; }
	[[nodiscard]] bool bgmPaused() const { return m_bgmPaused; }
	[[nodiscard]] float seVolume() const { return m_seVolume; }
	[[nodiscard]] float masterVolume() const { return m_masterVolume; }

	[[nodiscard]] bool isSeHandlePlaying(int handle) const
	{
		const auto it = m_seHandles.find(handle);
		return it != m_seHandles.end() && it->second.playing;
	}

	[[nodiscard]] std::size_t seCount() const { return m_seHandles.size(); }

private:
	struct SeInfo
	{
		std::string path;
		float volume;
		bool playing;
	};

	std::string m_bgmPath;
	float m_bgmVolume = 1.0f;
	float m_bgmFadeOut = 0.0f;
	bool m_bgmPlaying = false;
	bool m_bgmPaused = false;
	float m_seVolume = 1.0f;
	float m_masterVolume = 1.0f;
	int m_nextHandle = 1;
	std::unordered_map<int, SeInfo> m_seHandles;
};

} // anonymous namespace

// ── BGM tests ───────────────────────────────────────────────

TEST_CASE("IAudioPlayer - playBgm starts playback", "[audio]")
{
	MockAudioPlayer player;

	REQUIRE_FALSE(player.isBgmPlaying());

	player.playBgm("bgm/title.mp3", 0.8f);

	REQUIRE(player.isBgmPlaying());
	REQUIRE(player.bgmPath() == "bgm/title.mp3");
	REQUIRE(player.bgmVolume() == 0.8f);
}

TEST_CASE("IAudioPlayer - stopBgm stops playback", "[audio]")
{
	MockAudioPlayer player;
	player.playBgm("bgm/title.mp3");

	player.stopBgm(1.5f);

	REQUIRE_FALSE(player.isBgmPlaying());
	REQUIRE(player.bgmFadeOut() == 1.5f);
}

TEST_CASE("IAudioPlayer - pauseBgm and resumeBgm", "[audio]")
{
	MockAudioPlayer player;
	player.playBgm("bgm/battle.mp3");
	REQUIRE(player.isBgmPlaying());

	player.pauseBgm();
	REQUIRE_FALSE(player.isBgmPlaying());
	REQUIRE(player.bgmPaused());

	player.resumeBgm();
	REQUIRE(player.isBgmPlaying());
	REQUIRE_FALSE(player.bgmPaused());
}

TEST_CASE("IAudioPlayer - resumeBgm without pause does nothing", "[audio]")
{
	MockAudioPlayer player;
	player.resumeBgm();
	REQUIRE_FALSE(player.isBgmPlaying());
}

TEST_CASE("IAudioPlayer - setBgmVolume updates volume", "[audio]")
{
	MockAudioPlayer player;
	player.setBgmVolume(0.5f);
	REQUIRE(player.bgmVolume() == 0.5f);
}

// ── SE tests ────────────────────────────────────────────────

TEST_CASE("IAudioPlayer - playSe returns unique handles", "[audio]")
{
	MockAudioPlayer player;

	const int h1 = player.playSe("se/shot.wav");
	const int h2 = player.playSe("se/hit.wav");

	REQUIRE(h1 != h2);
	REQUIRE(player.isSeHandlePlaying(h1));
	REQUIRE(player.isSeHandlePlaying(h2));
}

TEST_CASE("IAudioPlayer - stopSe stops specific SE", "[audio]")
{
	MockAudioPlayer player;

	const int h1 = player.playSe("se/shot.wav");
	const int h2 = player.playSe("se/hit.wav");

	player.stopSe(h1);

	REQUIRE_FALSE(player.isSeHandlePlaying(h1));
	REQUIRE(player.isSeHandlePlaying(h2));
}

TEST_CASE("IAudioPlayer - stopAllSe stops all SE", "[audio]")
{
	MockAudioPlayer player;

	const int h1 = player.playSe("se/shot.wav");
	const int h2 = player.playSe("se/hit.wav");
	const int h3 = player.playSe("se/explosion.wav");

	player.stopAllSe();

	REQUIRE_FALSE(player.isSeHandlePlaying(h1));
	REQUIRE_FALSE(player.isSeHandlePlaying(h2));
	REQUIRE_FALSE(player.isSeHandlePlaying(h3));
}

TEST_CASE("IAudioPlayer - setSeVolume updates SE volume", "[audio]")
{
	MockAudioPlayer player;
	player.setSeVolume(0.3f);
	REQUIRE(player.seVolume() == 0.3f);
}

// ── Master volume tests ─────────────────────────────────────

TEST_CASE("IAudioPlayer - setMasterVolume updates master volume", "[audio]")
{
	MockAudioPlayer player;
	player.setMasterVolume(0.7f);
	REQUIRE(player.masterVolume() == 0.7f);
}

// ── Interface polymorphism test ─────────────────────────────

TEST_CASE("IAudioPlayer - works through base pointer", "[audio]")
{
	MockAudioPlayer mock;
	sgc::IAudioPlayer& player = mock;

	player.playBgm("bgm/test.mp3");
	REQUIRE(player.isBgmPlaying());

	const int h = player.playSe("se/test.wav");
	player.stopSe(h);

	player.setMasterVolume(0.5f);
	player.setBgmVolume(0.8f);
	player.setSeVolume(0.6f);

	player.stopBgm();
	REQUIRE_FALSE(player.isBgmPlaying());
}
