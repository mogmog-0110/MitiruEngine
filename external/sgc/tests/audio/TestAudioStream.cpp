/// @file TestAudioStream.cpp
/// @brief AudioStream.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/audio/AudioStream.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <numeric>
#include <vector>

using Catch::Approx;
using namespace sgc::audio;

// ── テスト用モックストリームソース ───────────────────────

/// @brief テスト用のメモリ上ストリームソース
class MockStreamSource : public IStreamSource
{
public:
	explicit MockStreamSource(std::size_t frames)
		: m_data(frames)
	{
		/// 0, 1, 2, ... のシーケンスを生成
		std::iota(m_data.begin(), m_data.end(), 0.0f);
	}

	std::size_t read(float* buffer, std::size_t frames) override
	{
		const std::size_t available = m_data.size() - m_pos;
		const std::size_t toRead = std::min(frames, available);
		std::copy_n(m_data.begin() + static_cast<std::ptrdiff_t>(m_pos), toRead, buffer);
		m_pos += toRead;
		return toRead;
	}

	bool seek(std::size_t frame) override
	{
		if (frame > m_data.size()) return false;
		m_pos = frame;
		return true;
	}

	[[nodiscard]] std::size_t totalFrames() const override
	{
		return m_data.size();
	}

	[[nodiscard]] bool isEnd() const override
	{
		return m_pos >= m_data.size();
	}

private:
	std::vector<float> m_data;
	std::size_t m_pos = 0;
};

// ── 基本テスト ──────────────────────────────────────────

TEST_CASE("AudioStreamPlayer initial state is Stopped", "[audio][stream]")
{
	auto src = std::make_unique<MockStreamSource>(1000);
	AudioStreamPlayer player(std::move(src), 256);

	REQUIRE(player.state() == StreamState::Stopped);
	REQUIRE(player.currentFrame() == 0);
	REQUIRE(player.totalFrames() == 1000);
	REQUIRE(player.bufferSize() == 256);
	REQUIRE(player.hasSource());
}

TEST_CASE("AudioStreamPlayer play changes state to Playing", "[audio][stream]")
{
	auto src = std::make_unique<MockStreamSource>(1000);
	AudioStreamPlayer player(std::move(src), 256);

	player.play();
	REQUIRE(player.state() == StreamState::Playing);
}

TEST_CASE("AudioStreamPlayer stop resets to Stopped", "[audio][stream]")
{
	auto src = std::make_unique<MockStreamSource>(1000);
	AudioStreamPlayer player(std::move(src), 256);

	player.play();
	player.stop();
	REQUIRE(player.state() == StreamState::Stopped);
	REQUIRE(player.currentFrame() == 0);
}

TEST_CASE("AudioStreamPlayer pause and resume", "[audio][stream]")
{
	auto src = std::make_unique<MockStreamSource>(1000);
	AudioStreamPlayer player(std::move(src), 256);

	player.play();
	player.pause();
	REQUIRE(player.state() == StreamState::Paused);

	player.resume();
	REQUIRE(player.state() == StreamState::Playing);
}

TEST_CASE("AudioStreamPlayer pause when stopped has no effect", "[audio][stream]")
{
	auto src = std::make_unique<MockStreamSource>(1000);
	AudioStreamPlayer player(std::move(src), 256);

	player.pause();
	REQUIRE(player.state() == StreamState::Stopped);
}

TEST_CASE("AudioStreamPlayer resume when stopped has no effect", "[audio][stream]")
{
	auto src = std::make_unique<MockStreamSource>(1000);
	AudioStreamPlayer player(std::move(src), 256);

	player.resume();
	REQUIRE(player.state() == StreamState::Stopped);
}

// ── バッファリングテスト ────────────────────────────────

TEST_CASE("AudioStreamPlayer play fills front buffer with data", "[audio][stream]")
{
	auto src = std::make_unique<MockStreamSource>(1000);
	AudioStreamPlayer player(std::move(src), 256);

	player.play();
	const auto& buf = player.frontBuffer();
	REQUIRE(buf.size() == 256);
	/// 最初のフレームは0.0f
	REQUIRE(buf[0] == Approx(0.0f));
	REQUIRE(buf[1] == Approx(1.0f));
}

TEST_CASE("AudioStreamPlayer swapBuffers exchanges front and back", "[audio][stream]")
{
	auto src = std::make_unique<MockStreamSource>(1000);
	AudioStreamPlayer player(std::move(src), 256);

	player.play();
	/// play()後: front=[0..255], back=[256..511]
	const auto& buf1 = player.frontBuffer();
	REQUIRE(buf1[0] == Approx(0.0f));

	player.swapBuffers();
	/// swap後: front=[256..511], back=[0..255]
	const auto& buf2 = player.frontBuffer();
	REQUIRE(buf2[0] == Approx(256.0f));
}

TEST_CASE("AudioStreamPlayer update refills back buffer", "[audio][stream]")
{
	auto src = std::make_unique<MockStreamSource>(2000);
	AudioStreamPlayer player(std::move(src), 256);

	player.play();
	/// play(): front=[0..255], back=[256..511], currentFrame=512

	player.swapBuffers();
	/// swap: front=[256..511]

	player.update();
	/// update: back filled with [512..767]

	player.swapBuffers();
	/// swap: front=[512..767]
	const auto& buf = player.frontBuffer();
	REQUIRE(buf[0] == Approx(512.0f));
}

// ── シークテスト ────────────────────────────────────────

TEST_CASE("AudioStreamPlayer seek changes position", "[audio][stream]")
{
	auto src = std::make_unique<MockStreamSource>(1000);
	AudioStreamPlayer player(std::move(src), 256);

	player.play();
	REQUIRE(player.seek(500));

	/// シーク後にバッファが再充填される
	const auto& buf = player.frontBuffer();
	REQUIRE(buf[0] == Approx(500.0f));
}

TEST_CASE("AudioStreamPlayer seek beyond total returns false", "[audio][stream]")
{
	auto src = std::make_unique<MockStreamSource>(100);
	AudioStreamPlayer player(std::move(src), 32);

	REQUIRE_FALSE(player.seek(200));
}

// ── 自動停止テスト ──────────────────────────────────────

TEST_CASE("AudioStreamPlayer stops when source ends without loop", "[audio][stream]")
{
	/// ソースがバッファサイズ*2（play時fill分）ちょうどで終わるケース
	auto src = std::make_unique<MockStreamSource>(64);
	AudioStreamPlayer player(std::move(src), 32);

	player.play();
	/// play(): front=[0..31], back=[32..63], currentFrame=64
	/// ソースは64フレームなのでちょうど使い切り

	player.update();
	/// update(): backBufferを補充しようとするが残り0フレーム→停止
	REQUIRE(player.state() == StreamState::Stopped);
}

// ── ループテスト ────────────────────────────────────────

TEST_CASE("AudioStreamPlayer loops back to start when loop enabled", "[audio][stream]")
{
	auto src = std::make_unique<MockStreamSource>(128);
	AudioStreamPlayer player(std::move(src), 32);

	LoopPoints loop;
	loop.enabled = true;
	loop.startFrame = 0;
	loop.endFrame = 0; /// 0 = 末尾まで
	player.setLoopPoints(loop);

	player.play();
	/// play(): front=[0..31], back=[32..63], currentFrame=64

	/// 何回かupdateしてソース末尾に到達させる
	player.update(); /// back=[64..95], currentFrame=96
	player.update(); /// back=[96..127], currentFrame=128
	player.update(); /// ソース末端→ループ→back=[0..31]

	/// ループしたので再生は継続
	REQUIRE(player.state() == StreamState::Playing);
}

TEST_CASE("AudioStreamPlayer loop points getter returns set values", "[audio][stream]")
{
	auto src = std::make_unique<MockStreamSource>(1000);
	AudioStreamPlayer player(std::move(src), 256);

	LoopPoints lp;
	lp.enabled = true;
	lp.startFrame = 100;
	lp.endFrame = 500;
	player.setLoopPoints(lp);

	const auto& got = player.loopPoints();
	REQUIRE(got.enabled);
	REQUIRE(got.startFrame == 100);
	REQUIRE(got.endFrame == 500);
}

TEST_CASE("AudioStreamPlayer loop with endFrame limits reading", "[audio][stream]")
{
	auto src = std::make_unique<MockStreamSource>(1000);
	AudioStreamPlayer player(std::move(src), 32);

	LoopPoints lp;
	lp.enabled = true;
	lp.startFrame = 0;
	lp.endFrame = 48; /// バッファ32 + 残り16
	player.setLoopPoints(lp);

	player.play();
	/// play(): front=[0..31], back: read min(32, 48-32)=16 → back=[32..47, 0*16]

	/// update後、endFrameに到達 → ループ
	player.update();
	REQUIRE(player.state() == StreamState::Playing);
}

// ── nullソーステスト ────────────────────────────────────

TEST_CASE("AudioStreamPlayer with null source stays stopped", "[audio][stream]")
{
	AudioStreamPlayer player(nullptr, 256);
	REQUIRE_FALSE(player.hasSource());
	REQUIRE(player.totalFrames() == 0);

	player.play();
	REQUIRE(player.state() == StreamState::Stopped);
}

// ── バッファサイズテスト ────────────────────────────────

TEST_CASE("AudioStreamPlayer zero bufferSize defaults to 4096", "[audio][stream]")
{
	auto src = std::make_unique<MockStreamSource>(10000);
	AudioStreamPlayer player(std::move(src), 0);
	REQUIRE(player.bufferSize() == 4096);
}
