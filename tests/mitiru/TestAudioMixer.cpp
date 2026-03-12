/// @file TestAudioMixer.cpp
/// @brief AudioMixerのユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <mitiru/audio/AudioMixer.hpp>

using namespace mitiru::audio;
using Catch::Approx;

// ── 基本再生テスト ──────────────────────────────────

TEST_CASE("AudioMixer - play returns valid handle", "[audio][mixer]")
{
	AudioMixer mixer;
	const int handle = mixer.play("test_sound", SoundCategory::Se);

	REQUIRE(handle >= 0);
	REQUIRE(mixer.isPlaying(handle));
	REQUIRE(mixer.activeChannelCount() == 1);
}

TEST_CASE("AudioMixer - multiple SE play simultaneously", "[audio][mixer]")
{
	AudioMixer mixer;
	const int h1 = mixer.play("se1", SoundCategory::Se);
	const int h2 = mixer.play("se2", SoundCategory::Se);
	const int h3 = mixer.play("se3", SoundCategory::Se);

	REQUIRE(h1 != h2);
	REQUIRE(h2 != h3);
	REQUIRE(mixer.activeChannelCount() == 3);
}

TEST_CASE("AudioMixer - BGM replaces previous BGM", "[audio][mixer]")
{
	AudioMixer mixer;
	const int h1 = mixer.play("bgm1", SoundCategory::Bgm, true);
	REQUIRE(mixer.isPlaying(h1));

	const int h2 = mixer.play("bgm2", SoundCategory::Bgm, true);
	REQUIRE_FALSE(mixer.isPlaying(h1));
	REQUIRE(mixer.isPlaying(h2));
	REQUIRE(mixer.activeChannelCount() == 1);
}

TEST_CASE("AudioMixer - Voice replaces previous Voice", "[audio][mixer]")
{
	AudioMixer mixer;
	const int h1 = mixer.play("voice1", SoundCategory::Voice);
	const int h2 = mixer.play("voice2", SoundCategory::Voice);

	REQUIRE_FALSE(mixer.isPlaying(h1));
	REQUIRE(mixer.isPlaying(h2));
}

// ── 停止テスト ──────────────────────────────────────

TEST_CASE("AudioMixer - stop by handle", "[audio][mixer]")
{
	AudioMixer mixer;
	const int h = mixer.play("se1", SoundCategory::Se);
	mixer.stop(h);

	REQUIRE_FALSE(mixer.isPlaying(h));
	REQUIRE(mixer.activeChannelCount() == 0);
}

TEST_CASE("AudioMixer - stopByCategory stops only that category", "[audio][mixer]")
{
	AudioMixer mixer;
	const int bgm = mixer.play("bgm", SoundCategory::Bgm, true);
	const int se = mixer.play("se", SoundCategory::Se);

	mixer.stopByCategory(SoundCategory::Se);

	REQUIRE(mixer.isPlaying(bgm));
	REQUIRE_FALSE(mixer.isPlaying(se));
}

TEST_CASE("AudioMixer - stopAll clears everything", "[audio][mixer]")
{
	AudioMixer mixer;
	mixer.play("bgm", SoundCategory::Bgm, true);
	mixer.play("se1", SoundCategory::Se);
	mixer.play("se2", SoundCategory::Se);

	mixer.stopAll();
	REQUIRE(mixer.activeChannelCount() == 0);
}

// ── 一時停止テスト ──────────────────────────────────

TEST_CASE("AudioMixer - pause and resume", "[audio][mixer]")
{
	AudioMixer mixer;
	const int h = mixer.play("bgm", SoundCategory::Bgm, true);

	mixer.pause(h);
	REQUIRE(mixer.channelState(h) == ChannelState::Paused);
	REQUIRE(mixer.isPlaying(h)); // Pausedも「再生中」扱い

	mixer.resume(h);
	REQUIRE(mixer.channelState(h) == ChannelState::Playing);
}

// ── フェードテスト ──────────────────────────────────

TEST_CASE("AudioMixer - fadeOut reduces volume to zero", "[audio][mixer]")
{
	AudioMixer mixer;
	const int h = mixer.play("bgm", SoundCategory::Bgm, true, 1.0f);

	mixer.fadeOut(h, 1.0f); // 1秒フェードアウト
	REQUIRE(mixer.channelState(h) == ChannelState::FadingOut);

	/// 0.5秒経過
	mixer.update(0.5f);
	REQUIRE(mixer.isPlaying(h)); // まだ再生中

	/// さらに0.6秒経過（合計1.1秒 > 1.0秒）
	mixer.update(0.6f);
	REQUIRE_FALSE(mixer.isPlaying(h)); // フェードアウト完了で停止
}

TEST_CASE("AudioMixer - fadeIn increases volume", "[audio][mixer]")
{
	AudioMixer mixer;
	const int h = mixer.play("bgm", SoundCategory::Bgm, true, 0.0f);

	mixer.fadeIn(h, 1.0f, 2.0f); // 2秒で0→1
	REQUIRE(mixer.channelState(h) == ChannelState::FadingIn);

	mixer.update(1.0f); // 1秒経過
	REQUIRE(mixer.channelState(h) == ChannelState::FadingIn);

	mixer.update(1.5f); // 合計2.5秒
	REQUIRE(mixer.channelState(h) == ChannelState::Playing);
}

TEST_CASE("AudioMixer - crossfadeBgm transitions smoothly", "[audio][mixer]")
{
	AudioMixer mixer;
	const int h1 = mixer.play("bgm1", SoundCategory::Bgm, true, 1.0f);

	const int h2 = mixer.crossfadeBgm("bgm2", 1.0f);
	REQUIRE(h2 >= 0);

	/// 旧BGMはフェードアウト中
	REQUIRE(mixer.channelState(h1) == ChannelState::FadingOut);
	/// 新BGMはフェードイン中
	REQUIRE(mixer.channelState(h2) == ChannelState::FadingIn);

	/// 完了まで更新
	mixer.update(1.5f);
	REQUIRE_FALSE(mixer.isPlaying(h1));
	REQUIRE(mixer.isPlaying(h2));
}

// ── ボリュームテスト ────────────────────────────────

TEST_CASE("AudioMixer - master volume affects effective volume", "[audio][mixer]")
{
	AudioMixer mixer;
	const int h = mixer.play("se", SoundCategory::Se, false, 0.8f);

	mixer.setMasterVolume(0.5f);
	REQUIRE(mixer.effectiveVolume(h) == Approx(0.5f * 1.0f * 0.8f));
}

TEST_CASE("AudioMixer - category volume affects effective volume", "[audio][mixer]")
{
	AudioMixer mixer;
	const int h = mixer.play("se", SoundCategory::Se, false, 1.0f);

	mixer.setCategoryVolume(SoundCategory::Se, 0.6f);
	REQUIRE(mixer.effectiveVolume(h) == Approx(1.0f * 0.6f * 1.0f));
}

TEST_CASE("AudioMixer - combined volume calculation", "[audio][mixer]")
{
	AudioMixer mixer;
	mixer.setMasterVolume(0.5f);
	mixer.setCategoryVolume(SoundCategory::Bgm, 0.8f);

	const int h = mixer.play("bgm", SoundCategory::Bgm, true, 0.7f);
	REQUIRE(mixer.effectiveVolume(h) == Approx(0.5f * 0.8f * 0.7f));
}

// ── コールバックテスト ──────────────────────────────

TEST_CASE("AudioMixer - onChannelStopped callback fires", "[audio][mixer]")
{
	AudioMixer mixer;
	int stoppedHandle = -1;
	mixer.setOnChannelStopped([&](int h) { stoppedHandle = h; });

	const int h = mixer.play("se", SoundCategory::Se);
	mixer.stop(h);

	REQUIRE(stoppedHandle == h);
}

// ── チャンネル上限テスト ────────────────────────────

TEST_CASE("AudioMixer - returns -1 when channels exhausted", "[audio][mixer]")
{
	AudioMixer mixer;

	/// 全チャンネルを埋める
	for (int i = 0; i < AudioMixer::MAX_CHANNELS; ++i)
	{
		const int h = mixer.play("se" + std::to_string(i), SoundCategory::Se);
		REQUIRE(h >= 0);
	}

	/// 上限超え
	const int overflow = mixer.play("overflow", SoundCategory::Se);
	REQUIRE(overflow == -1);
}

// ── 無効ハンドルテスト ──────────────────────────────

TEST_CASE("AudioMixer - invalid handle returns safe defaults", "[audio][mixer]")
{
	AudioMixer mixer;

	REQUIRE_FALSE(mixer.isPlaying(999));
	REQUIRE(mixer.channelState(999) == ChannelState::Idle);
	REQUIRE(mixer.effectiveVolume(999) == Approx(0.0f));

	/// 無効ハンドルへの操作は安全にノーオペレーション
	mixer.stop(999);
	mixer.pause(999);
	mixer.resume(999);
	mixer.fadeOut(999, 1.0f);
	mixer.fadeIn(999, 1.0f, 1.0f);
}
