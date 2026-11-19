#pragma once
/// @file ChipPeriodCheck.hpp
/// @brief 周期 (分周比) の整数で音程が決まるチップの検査に共通の部品と、曲のハッシュ検査

#include <catch2/catch_test_macros.hpp>

#include "ChipHashes.hpp"
#include "ChipPitch.hpp"
#include "ChipSongs.hpp"

#include <mitiru_mml/MultiChipSequencer.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

namespace mitiru_mml_test
{

/// @brief 検査する音域 (A2 から C7)
inline constexpr int PERIOD_FIRST_KEY = 45;
inline constexpr int PERIOD_LAST_KEY = 96;

/// @brief 周期の整数化が作る誤差の上限 (セント)。1200 * log2(1 + 0.5 / 周期)
[[nodiscard]] inline double quantizationBoundCents(double period)
{
	return 1200.0 * std::log2(1.0 + 0.5 / period);
}

/// @brief 1 つのチップに SongTrackDef の並びを載せたシーケンサーを作る
[[nodiscard]] inline std::unique_ptr<mitiru_mml::MultiChipSequencer> buildSong(
	std::unique_ptr<mitiru_mml::ChipDriver> driver, const SongTrackDef* tracks, std::size_t count)
{
	auto seq = std::make_unique<mitiru_mml::MultiChipSequencer>();
	const int chip = seq->addChip(std::move(driver));
	for (std::size_t i = 0; i < count; ++i)
	{
		seq->addTrack(chip, tracks[i].channel, tracks[i].mml, tracks[i].voice);
	}
	return seq;
}

/// @brief 選んだ周期が最も近い整数で、測った周波数がその周期どおりで、12 平均律からのずれが限界以内であること
/// @param periodOf キーから周期を選ぶ関数
/// @param hzOf 周期が作る周波数を返す関数
template <class Driver, class PeriodOf, class HzOf>
void checkPeriodChip(Driver& driver, int channel, int voice, PeriodOf periodOf, HzOf hzOf)
{
	for (int key = PERIOD_FIRST_KEY; key <= PERIOD_LAST_KEY; ++key)
	{
		const int period = periodOf(key);
		const double chipHz = hzOf(period);
		const double ideal = keyHz(key);
		INFO("key " << key << " period " << period << " chip " << chipHz << " ideal " << ideal);

		REQUIRE(std::abs(centsBetween(chipHz, ideal)) <= std::abs(centsBetween(hzOf(period > 1 ? period - 1 : period), ideal)) + 1.0e-9);
		REQUIRE(std::abs(centsBetween(chipHz, ideal)) <= std::abs(centsBetween(hzOf(period + 1), ideal)) + 1.0e-9);

		const DriverTone t = measureNote(driver, channel, key, voice);
		REQUIRE(std::abs(centsBetween(t.peakHz, chipHz)) <= 0.3);
		REQUIRE(std::abs(centsBetween(t.peakHz, ideal)) <= quantizationBoundCents(period) + 0.3);
	}
}

/// @brief 左へ振った声は右が、右へ振った声は左が、ほぼ無音になること
template <class Driver>
void checkPanSplit(int channel, int key)
{
	for (const int pan : {0, 2})
	{
		Driver driver;
		driver.setPan(channel, pan);
		driver.noteOn(channel, key, 15);
		std::vector<float> out(2 * 40000);
		driver.device().generate(out.data(), 40000);
		double left = 0.0;
		double right = 0.0;
		for (std::size_t i = 10000; i < 40000; ++i)
		{
			left += std::abs(out[i * 2]);
			right += std::abs(out[i * 2 + 1]);
		}
		INFO("pan " << pan << " left " << left << " right " << right);
		if (pan == 0)
		{
			REQUIRE(left > 1.0);
			REQUIRE(right < left * 0.001);
		}
		else
		{
			REQUIRE(right > 1.0);
			REQUIRE(left < right * 0.001);
		}
	}
}

/// @brief 保存した状態を別の Driver に戻すと、続きの出力が同じになること
template <class Driver>
void checkSaveRestore()
{
	Driver a;
	a.noteOn(0, 60, 15);
	std::vector<float> skip(2 * 5000);
	a.device().generate(skip.data(), 5000);
	const auto state = a.device().saveState();
	REQUIRE_FALSE(state.empty());
	Driver b;
	REQUIRE(b.device().loadState(state));
	std::vector<float> outA(2 * 2000);
	std::vector<float> outB(2 * 2000);
	a.device().generate(outA.data(), 2000);
	b.device().generate(outB.data(), 2000);
	REQUIRE(outA == outB);
}

/// @brief 曲を 2 回描いて同じハッシュになり、記録した値と一致すること
inline void checkSongHash(const char* name, std::unique_ptr<mitiru_mml::MultiChipSequencer> (*build)())
{
	auto first = build();
	auto second = build();
	const RenderedHash h1 = hashRender(*first);
	const RenderedHash h2 = hashRender(*second);
	INFO(name << " frames " << h1.frames << " sha256 " << h1.sha256);
	REQUIRE(h1.frames > 44100u * 3u);
	REQUIRE(h1.sha256 == h2.sha256);

	const ChipHashEntry* expected = findChipHash(name);
	REQUIRE(expected != nullptr);
	REQUIRE(h1.frames == expected->frames);
	REQUIRE(h1.sha256 == expected->sha256);
}

/// @brief 曲が無音でなく、満点を超えないこと
inline void checkSongAudible(std::unique_ptr<mitiru_mml::MultiChipSequencer> (*build)())
{
	auto seq = build();
	const mitiru_mml::StereoBuffer buf = seq->render();
	float peak = 0.0f;
	for (const float s : buf.samples)
	{
		peak = std::max(peak, std::abs(s));
	}
	REQUIRE(peak > 0.05f);
	REQUIRE(peak <= 1.0f);
}

} // namespace mitiru_mml_test
