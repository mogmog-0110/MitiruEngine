#pragma once
/// @file BaselineHashes.hpp
/// @brief OpnaSequencer::render の出力 PCM の SHA-256 の表 (2 つ)
/// @details 曲の定義は BaselineSongs.hpp。MSVC 17.14 (x64、Debug と Release で同じ値) で記録した。
///          PCM は 16bit リトルエンディアンのバイト列としてハッシュする。
///          曲を変えたら mml_bench --emit-table で作り直す。
///          BASELINE_HASHES_BEFORE_PITCH_FIX: コミット 9c0b83e (リファクタリング前) の出力。FM の音が
///            3 オクターブ高い状態 (noteToFnumBlock の不具合)。音の高さの修正後は一致しないので、
///            検査には使わない。
///          BASELINE_HASHES_BEFORE_DEFAULTS: 音の高さの修正後、RenderOptions の既定 (stableEventOrder と
///            isolateSsgControl) を true に変える前の出力。
///          BASELINE_HASHES: 既定を変えた後の最新の出力。テストが照合するのはこちら。

#include <cstddef>
#include <cstdint>
#include <string>

namespace mitiru_mml_test
{

/// @brief 基準の出力 1 曲ぶん
struct BaselineEntry
{
	const char* name;
	std::size_t samples;  ///< 出力のサンプル数 (44100 Hz モノラル)
	const char* sha256;
};

inline constexpr BaselineEntry BASELINE_HASHES_BEFORE_PITCH_FIX[] = {
	{"fm_single", 88200u, "e868a535d9e5450d70540efc43dc59a7a60ffef94efd36f247e9d5a8d3c70578"},
	{"fm_ssg_mixed", 88200u, "90dcf39b7a648414148e3b092633b505cd512aade1846e21ce5da6dab015a741"},
	{"ref_am_three_track", 1681863u, "487ce101e7e1a295069ad8447a7edf43245167d0b18b3dec89705599d05795f1"},
	{"five_track_16bar", 1209561u, "fbfabe5666835085c89e05b75449a3937544854086cac6ffb6ffa2463ed836d3"},
	{"technique_mix", 711464u, "18eed4bfb47b1d8eeca8bf61d879231321ba45859fc08a2f72e2ef150e852a48"},
	{"fm_overflow_seven_tracks", 88200u, "d20c3bc836021aa1c0a62206b01250b52feec93669fad89107e27d744c1697a6"},
	{"long_five_track", 3848505u, "eba03738bef980232c68da2e122eba937d805714e560d568d6ed7f6306250bd1"},
};

inline constexpr BaselineEntry BASELINE_HASHES[] = {
	{"fm_single", 88200u, "2989ed1aadfabbc19642aaf434268db9fb60ba30eb07f5de6edad0c141a58cfb"},
	{"fm_ssg_mixed", 88200u, "9d6ad8b780f8dd2d39bc2aa8a8c5a8cc2f89e8515b57636f3030e61d6aadbf8b"},
	{"ref_am_three_track", 1681863u, "56c36f75a76d25f6bdf7fcd1abc02b310617f5b5d1dd89fe2052108c3f77090c"},
	{"five_track_16bar", 1209561u, "c250d2a16384d53d14fdc59804cdb87e4dcde20c7e37c6b0cd3d8bbadd30103a"},
	{"technique_mix", 711464u, "775c0312f97a9cab3642865135b3bf832fe7570cf8f96a5fd37609b6516dacfb"},
	{"fm_overflow_seven_tracks", 88200u, "aca60857f4fcf90b784c9f928552c8c0ae27743c8340c48f7869d3f10d7c5c6f"},
	{"long_five_track", 3848505u, "244de79b0977593079ebc71b1099ad6a3abe8f80be2c6ab11cd586b0849f8ef2"},
};

inline constexpr BaselineEntry BASELINE_HASHES_BEFORE_DEFAULTS[] = {
	{"fm_single", 88200u, "2989ed1aadfabbc19642aaf434268db9fb60ba30eb07f5de6edad0c141a58cfb"},
	{"fm_ssg_mixed", 88200u, "9d6ad8b780f8dd2d39bc2aa8a8c5a8cc2f89e8515b57636f3030e61d6aadbf8b"},
	{"ref_am_three_track", 1681863u, "56c36f75a76d25f6bdf7fcd1abc02b310617f5b5d1dd89fe2052108c3f77090c"},
	{"five_track_16bar", 1209561u, "cc5d3c4078bab6f581dfc0ee1582b10d4748133f5dab7e6d440a21b384a472e9"},
	{"technique_mix", 711464u, "8333ede9994ba4e55814ddb8cbc4c95738d07ec8f71108cb0876f3bdbcacc9c3"},
	{"fm_overflow_seven_tracks", 88200u, "aca60857f4fcf90b784c9f928552c8c0ae27743c8340c48f7869d3f10d7c5c6f"},
	{"long_five_track", 3848505u, "f30c38d0500699751825a3b18959748d38d0af08b6afe3e528120a5cd5244956"},
};

/// @brief 曲名から基準の記録を探す (無ければ nullptr)
[[nodiscard]] inline const BaselineEntry* findBaseline(const std::string& name)
{
	for (const auto& entry : BASELINE_HASHES)
	{
		if (name == entry.name)
		{
			return &entry;
		}
	}
	return nullptr;
}

} // namespace mitiru_mml_test
