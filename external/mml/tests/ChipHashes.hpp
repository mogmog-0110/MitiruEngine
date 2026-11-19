#pragma once
/// @file ChipHashes.hpp
/// @brief チップごとの曲 1 本の出力 PCM (44100 Hz、16bit ステレオ) の SHA-256 の表
/// @details 曲の定義は ChipSongs.hpp。MSVC 17.14 (x64、Debug) で記録した。
///          曲やチップを変えたら、隠しテスト "[emit]" (mml_tests "[.emit]") の出力で作り直す。

#include <cstddef>
#include <cstdint>
#include <string>

namespace mitiru_mml_test
{

/// @brief 1 曲ぶんの記録
struct ChipHashEntry
{
	const char* name;
	std::size_t frames;   ///< 出力のフレーム数 (ステレオ 1 組で 1)
	const char* sha256;   ///< 16bit リトルエンディアンの interleaved ステレオのハッシュ
};

inline constexpr ChipHashEntry CHIP_HASHES[] = {
	{"opm_song", 302400u, "7281429ab6b6de1d3a0bbe984b10647d52cf622ce9199b935d46f871a459af9e"},
	{"opl_song", 320726u, "6bf6d04c44440e0462b18e0ebbe39db34d8e8862a78bb70aefcc03b842ae936f"},
	{"opl2_song", 320726u, "6bf6d04c44440e0462b18e0ebbe39db34d8e8862a78bb70aefcc03b842ae936f"},
	{"opl3_song", 320726u, "cfcf83a361966a6616973ce62ddfe27268c268720afb83f6661487ecaffa4301"},
	{"sn76489_song", 282239u, "e264a13212f0a3215b35c766e2442ea45646258662fc607df2a08e452e886d81"},
	{"ay8910_song", 282239u, "bacbb52fed1dbc3f6fdd6c34ece251ada1905acfae7c5b80b06e1f2c60b2d767"},
	{"nes_song", 282239u, "ec09ac6636e11fb5621e24424832960b23d6b9ccc113e4e0feda832771cef6bb"},
	{"multi_chip_song", 302400u, "a97d571db58a46fb0de3789814e7006aed7b0e6a82909e55098009d8e2959e96"},
};

/// @brief 曲名から記録を探す (無ければ nullptr)
[[nodiscard]] inline const ChipHashEntry* findChipHash(const std::string& name)
{
	for (const auto& entry : CHIP_HASHES)
	{
		if (name == entry.name)
		{
			return &entry;
		}
	}
	return nullptr;
}

} // namespace mitiru_mml_test
