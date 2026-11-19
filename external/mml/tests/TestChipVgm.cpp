/// @file TestChipVgm.cpp
/// @brief 複数チップの VGM プレイヤー (VgmMultiPlayer) の検査。VGM はテストの中で組み立てる

#include <catch2/catch_test_macros.hpp>

#include "ChipPitch.hpp"
#include "Sha256.hpp"

#include <mitiru_mml/VgmMultiPlayer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace mitiru_mml;
using namespace mitiru_mml_test;

namespace
{

/// @brief 小さな VGM (バージョン 1.71) を組み立てる
class VgmBuilder
{
public:
	static constexpr std::size_t HEADER_SIZE = 0xC0;

	VgmBuilder()
		: m_bytes(HEADER_SIZE, 0)
	{
		put(0x00, 'V');
		put(0x01, 'g');
		put(0x02, 'm');
		put(0x03, ' ');
		put32(0x08, 0x171);
		put32(0x34, static_cast<std::uint32_t>(HEADER_SIZE - 0x34));
	}

	VgmBuilder& clock(std::size_t headerOffset, std::uint32_t value)
	{
		put32(headerOffset, value);
		return *this;
	}

	VgmBuilder& raw(std::initializer_list<std::uint8_t> bytes)
	{
		m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
		return *this;
	}

	VgmBuilder& wait(std::uint16_t samples)
	{
		m_samples += samples;
		return raw({0x61, static_cast<std::uint8_t>(samples & 0xFF), static_cast<std::uint8_t>(samples >> 8)});
	}

	/// @brief ここから先をループの戻り先にする
	VgmBuilder& markLoop()
	{
		m_loopStart = m_bytes.size();
		return *this;
	}

	std::vector<std::uint8_t> finish()
	{
		raw({0x66});
		std::vector<std::uint8_t> out = m_bytes;
		const auto set32 = [&out](std::size_t at, std::uint32_t v)
		{
			for (int i = 0; i < 4; ++i)
			{
				out[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
			}
		};
		set32(0x04, static_cast<std::uint32_t>(out.size() - 4));
		set32(0x18, m_samples);
		if (m_loopStart != 0)
		{
			set32(0x1C, static_cast<std::uint32_t>(m_loopStart - 0x1C));
		}
		return out;
	}

private:
	void put(std::size_t at, std::uint8_t v)
	{
		m_bytes[at] = v;
	}

	void put32(std::size_t at, std::uint32_t v)
	{
		for (int i = 0; i < 4; ++i)
		{
			m_bytes[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
		}
	}

	std::vector<std::uint8_t> m_bytes;
	std::uint32_t m_samples = 0;
	std::size_t m_loopStart = 0;
};

constexpr std::size_t HDR_SN76489 = 0x0C;
constexpr std::size_t HDR_YM2151 = 0x30;
constexpr std::size_t HDR_YM3812 = 0x50;
constexpr std::size_t HDR_YM3526 = 0x54;
constexpr std::size_t HDR_YMF262 = 0x5C;
constexpr std::size_t HDR_AY8910 = 0x74;
constexpr std::size_t HDR_NES = 0x84;

/// @brief VGM を描き、先頭 0.1 秒を捨てた L と R の平均の基音を測る
DriverTone measureVgm(const std::vector<std::uint8_t>& bytes)
{
	VgmMultiPlayer player;
	REQUIRE(player.loadMemory(bytes.data(), bytes.size()));
	const StereoBuffer buf = player.render();
	REQUIRE(buf.frames() > 4410u + 16384u);
	std::vector<double> mono;
	for (std::size_t i = 4410; i < 4410 + 16384; ++i)
	{
		mono.push_back(0.5 * (static_cast<double>(buf.samples[i * 2]) + static_cast<double>(buf.samples[i * 2 + 1])));
	}
	return analyzeMono(std::move(mono), 44100.0);
}

/// @brief SN76489 の 1 声目に period を書いて最大音量にするコマンド
VgmBuilder& snTone(VgmBuilder& b, int period, bool second = false)
{
	const std::uint8_t cmd = second ? 0x30 : 0x50;
	b.raw({cmd, static_cast<std::uint8_t>(0x80 | (period & 0x0F))});
	b.raw({cmd, static_cast<std::uint8_t>((period >> 4) & 0x3F)});
	b.raw({cmd, 0x90});
	return b;
}

} // namespace

TEST_CASE("VgmMultiPlayer plays SN76489 at the pitch of the programmed period", "[mml][vgm]")
{
	VgmBuilder b;
	b.clock(HDR_SN76489, 3579545);
	snTone(b, 254);
	b.wait(22050).wait(22050);
	const DriverTone t = measureVgm(b.finish());
	const double expected = 3579536.0 / (32.0 * 254.0); // 3579545 / 16 を整数にして 16 倍した値がエミュレーターのクロック
	INFO("measured " << t.peakHz << " expected " << expected);
	REQUIRE(std::abs(centsBetween(t.peakHz, expected)) < 0.5);
	REQUIRE(t.rmsDb > -40.0);
}

TEST_CASE("VgmMultiPlayer plays YM2151 at 440 Hz", "[mml][vgm]")
{
	VgmBuilder b;
	b.clock(HDR_YM2151, 3579545);
	const auto w = [&b](std::uint8_t reg, std::uint8_t value) { b.raw({0x54, reg, value}); };
	w(0x20, 0xC7); // 左右とも出力、アルゴリズム 7
	for (std::uint8_t slot = 0; slot < 4; ++slot)
	{
		w(static_cast<std::uint8_t>(0x40 + slot * 8), 0x01);
		w(static_cast<std::uint8_t>(0x60 + slot * 8), slot == 0 ? 0 : 127);
		w(static_cast<std::uint8_t>(0x80 + slot * 8), 0x1F);
		w(static_cast<std::uint8_t>(0xA0 + slot * 8), 0x00);
		w(static_cast<std::uint8_t>(0xC0 + slot * 8), 0x00);
		w(static_cast<std::uint8_t>(0xE0 + slot * 8), 0x07);
	}
	w(0x28, 0x4A); // オクターブ 4、A
	w(0x30, 0x00);
	w(0x08, 0x78);
	b.wait(22050).wait(22050);
	const DriverTone t = measureVgm(b.finish());
	INFO("measured " << t.peakHz);
	REQUIRE(std::abs(centsBetween(t.peakHz, 440.0)) <= 1.0);
}

TEST_CASE("VgmMultiPlayer plays YM3812 and YM3526 at 440 Hz", "[mml][vgm]")
{
	for (const bool opl2 : {true, false})
	{
		VgmBuilder b;
		b.clock(opl2 ? HDR_YM3812 : HDR_YM3526, 3579545);
		const std::uint8_t cmd = opl2 ? 0x5A : 0x5B;
		const auto w = [&b, cmd](std::uint8_t reg, std::uint8_t value) { b.raw({cmd, reg, value}); };
		if (opl2)
		{
			w(0x01, 0x20);
		}
		w(0x20, 0x21); // 変調器: 持続、倍率 1
		w(0x40, 0x3F); // 変調器の TL を最大にして無音にする
		w(0x60, 0xF0);
		w(0x80, 0x0F);
		w(0x23, 0x21); // 搬送波
		w(0x43, 0x00);
		w(0x63, 0xF0);
		w(0x83, 0x00);
		w(0xC0, 0x00);
		w(0xA0, 0x44); // F-Number 580 の下位
		w(0xB0, 0x32); // キーオン、ブロック 4、F-Number の上位 2
		b.wait(22050).wait(22050);
		const DriverTone t = measureVgm(b.finish());
		INFO((opl2 ? "opl2" : "opl") << " measured " << t.peakHz);
		REQUIRE(std::abs(centsBetween(t.peakHz, 440.0)) <= 1.0);
	}
}

TEST_CASE("VgmMultiPlayer plays YMF262 with the second register bank", "[mml][vgm]")
{
	VgmBuilder b;
	b.clock(HDR_YMF262, 14318180);
	const auto w0 = [&b](std::uint8_t reg, std::uint8_t value) { b.raw({0x5E, reg, value}); };
	const auto w1 = [&b](std::uint8_t reg, std::uint8_t value) { b.raw({0x5F, reg, value}); };
	w1(0x05, 0x01); // OPL3 のモード
	// 後半のバンクのチャンネル 0 (全体のチャンネル 9)
	w1(0x20, 0x21);
	w1(0x40, 0x3F);
	w1(0x60, 0xF0);
	w1(0x80, 0x0F);
	w1(0x23, 0x21);
	w1(0x43, 0x00);
	w1(0x63, 0xF0);
	w1(0x83, 0x00);
	w1(0xC0, 0x30);
	w1(0xA0, 0x44);
	w1(0xB0, 0x32);
	w0(0xBD, 0x00);
	b.wait(22050).wait(22050);
	const DriverTone t = measureVgm(b.finish());
	INFO("measured " << t.peakHz);
	REQUIRE(std::abs(centsBetween(t.peakHz, 440.0)) <= 1.0);
}

TEST_CASE("VgmMultiPlayer plays AY-3-8910 and NES APU at the pitch of the programmed period", "[mml][vgm]")
{
	{
		VgmBuilder b;
		b.clock(HDR_AY8910, 1789773);
		const auto w = [&b](std::uint8_t reg, std::uint8_t value) { b.raw({0xA0, reg, value}); };
		w(0x00, 254);
		w(0x01, 0);
		w(0x07, 0x38);
		w(0x08, 0x0F);
		b.wait(22050).wait(22050);
		const DriverTone t = measureVgm(b.finish());
		const double expected = 1789773.0 / (16.0 * 254.0);
		INFO("ay measured " << t.peakHz << " expected " << expected);
		REQUIRE(std::abs(centsBetween(t.peakHz, expected)) < 0.5);
	}
	{
		VgmBuilder b;
		b.clock(HDR_NES, 1789773);
		const auto w = [&b](std::uint8_t reg, std::uint8_t value) { b.raw({0xB4, reg, value}); };
		w(0x15, 0x0F);
		w(0x17, 0x40);
		w(0x01, 0x08);
		w(0x00, 0xBF); // デューティ 50%、停止フラグ、固定音量 15
		w(0x02, 253);
		w(0x03, 0x08);
		b.wait(22050).wait(22050);
		const DriverTone t = measureVgm(b.finish());
		const double expected = 1789773.0 / (16.0 * 254.0);
		INFO("nes measured " << t.peakHz << " expected " << expected);
		REQUIRE(std::abs(centsBetween(t.peakHz, expected)) < 0.5);
	}
}

TEST_CASE("VgmMultiPlayer reads dual chips and masks the clock flag bits", "[mml][vgm]")
{
	VgmBuilder b;
	b.clock(HDR_SN76489, 0x40000000u | 3579545u); // bit 30 は 2 個目のチップがあることを示す
	b.raw({0x50, 0x9F}); // 1 個目は無音のまま
	snTone(b, 508, true); // 2 個目だけを鳴らす
	b.wait(22050).wait(22050);
	const std::vector<std::uint8_t> bytes = b.finish();

	VgmMultiPlayer player;
	REQUIRE(player.loadMemory(bytes.data(), bytes.size()));
	REQUIRE(player.chips().size() == 2);
	REQUIRE(player.chips()[0].clock == 3579545u);
	REQUIRE(player.chips()[1].clock == 3579545u);
	REQUIRE(player.chips()[0].instance == 0);
	REQUIRE(player.chips()[1].instance == 1);

	const DriverTone t = measureVgm(bytes);
	const double expected = 3579536.0 / (32.0 * 508.0);
	INFO("measured " << t.peakHz << " expected " << expected);
	REQUIRE(std::abs(centsBetween(t.peakHz, expected)) < 0.5);
}

TEST_CASE("VgmMultiPlayer mixes chips of different kinds on one time axis", "[mml][vgm]")
{
	VgmBuilder b;
	b.clock(HDR_SN76489, 3579545).clock(HDR_YM2151, 3579545);
	snTone(b, 254);
	b.wait(22050);
	// 0.5 秒後に OPM を鳴らし始める。SN76489 は鳴りつづける
	b.raw({0x54, 0x20, 0xC7});
	for (std::uint8_t slot = 0; slot < 4; ++slot)
	{
		b.raw({0x54, static_cast<std::uint8_t>(0x40 + slot * 8), 0x01});
		b.raw({0x54, static_cast<std::uint8_t>(0x60 + slot * 8), slot == 0 ? std::uint8_t{0} : std::uint8_t{127}});
		b.raw({0x54, static_cast<std::uint8_t>(0x80 + slot * 8), 0x1F});
		b.raw({0x54, static_cast<std::uint8_t>(0xE0 + slot * 8), 0x07});
	}
	b.raw({0x54, 0x28, 0x4A});
	b.raw({0x54, 0x08, 0x78});
	b.wait(22050);

	VgmMultiPlayer player;
	const auto bytes = b.finish();
	REQUIRE(player.loadMemory(bytes.data(), bytes.size()));
	const StereoBuffer buf = player.render();
	REQUIRE(player.chips().size() == 2);
	REQUIRE(buf.frames() == 44100u);

	const auto rms = [&buf](std::size_t from, std::size_t to)
	{
		double acc = 0.0;
		for (std::size_t i = from * 2; i < to * 2; ++i)
		{
			acc += static_cast<double>(buf.samples[i]) * buf.samples[i];
		}
		return std::sqrt(acc / static_cast<double>((to - from) * 2));
	};
	// 後半は OPM が加わるので、前半より大きい
	REQUIRE(rms(30000, 44000) > rms(5000, 20000) * 1.2);
}

TEST_CASE("VgmMultiPlayer skips commands of chips it does not play and still reaches the later ones", "[mml][vgm]")
{
	VgmBuilder b;
	b.clock(HDR_SN76489, 3579545);
	b.raw({0x51, 0x20, 0x10});                    // YM2413 の書き込み (2 引数)
	b.raw({0x52, 0x2A, 0x80});                    // YM2612 の書き込み (2 引数)
	b.raw({0x67, 0x66, 0x00, 0x04, 0x00, 0x00, 0x00, 1, 2, 3, 4}); // データブロック
	b.raw({0xE0, 0x01, 0x02, 0x03, 0x04});        // PCM バンクのシーク (4 引数)
	b.raw({0xC0, 0x00, 0x00, 0x00});              // 3 引数のコマンド
	b.raw({0x90, 0x00, 0x00, 0x00, 0x00});        // DAC ストリームの設定
	snTone(b, 254);
	b.wait(22050).wait(22050);
	const DriverTone t = measureVgm(b.finish());
	REQUIRE(std::abs(centsBetween(t.peakHz, 3579536.0 / (32.0 * 254.0))) < 0.5);
}

TEST_CASE("VgmMultiPlayer repeats from the loop point and renders the same bytes every time", "[mml][vgm]")
{
	VgmBuilder b;
	b.clock(HDR_SN76489, 3579545);
	b.raw({0x50, 0x9F});
	b.wait(4410);
	b.markLoop();
	snTone(b, 254);
	b.wait(4410);
	b.raw({0x50, 0x9F});
	b.wait(4410);
	const auto bytes = b.finish();

	VgmMultiPlayer player;
	REQUIRE(player.loadMemory(bytes.data(), bytes.size()));
	const StereoBuffer once = player.render(1);
	const StereoBuffer twice = player.render(2);
	REQUIRE(once.frames() == 13230u);
	REQUIRE(twice.frames() == 13230u + 8820u);

	const StereoBuffer again = player.render(2);
	REQUIRE(Sha256::ofPcm(twice.toPcm16Stereo()) == Sha256::ofPcm(again.toPcm16Stereo()));
}

TEST_CASE("VgmMultiPlayer ignores absurdly small clocks in a damaged header", "[mml][vgm]")
{
	VgmBuilder b;
	b.clock(HDR_SN76489, 5).clock(HDR_AY8910, 3);
	b.wait(100);
	const auto bytes = b.finish();
	VgmMultiPlayer player;
	REQUIRE_FALSE(player.loadMemory(bytes.data(), bytes.size()));
	REQUIRE(player.chips().empty());
}

TEST_CASE("VgmMultiPlayer rejects data that is not a VGM", "[mml][vgm]")
{
	std::vector<std::uint8_t> junk(0x100, 0x55);
	VgmMultiPlayer player;
	REQUIRE_FALSE(player.loadMemory(junk.data(), junk.size()));
	VgmBuilder b; // どのチップのクロックも 0
	b.wait(100);
	const auto bytes = b.finish();
	REQUIRE_FALSE(player.loadMemory(bytes.data(), bytes.size()));
	REQUIRE(player.render().empty());
}
