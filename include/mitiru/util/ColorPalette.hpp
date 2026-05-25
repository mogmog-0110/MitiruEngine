#pragma once

/// @file ColorPalette.hpp
/// @brief constexpr定義済みカラーパレット
/// @details PICO8/NES/PASTEL/GAMEBOY各16色のパレット定義と安全なインデックスアクセス。

#include <array>
#include <sgc/types/Color.hpp>

namespace mitiru::util
{

/// @brief パレット識別子
enum class PaletteType
{
	Pico8,    ///< PICO-8パレット（16色）
	Nes,      ///< NES風パレット（16色）
	Pastel,   ///< パステルパレット（16色）
	GameBoy   ///< ゲームボーイ風パレット（4色+12パディング=16色）
};

/// @brief constexprカラーパレット（16色固定）
struct ColorPalette
{
	static constexpr int PALETTE_SIZE = 16;

	/// @brief PICO-8パレット
	static constexpr std::array<sgc::Colorf, PALETTE_SIZE> PICO8 = {{
		sgc::Colorf{0.0f/255, 0.0f/255, 0.0f/255, 1.0f},       // 0: 黒
		sgc::Colorf{29.0f/255, 43.0f/255, 83.0f/255, 1.0f},     // 1: 濃い青
		sgc::Colorf{126.0f/255, 37.0f/255, 83.0f/255, 1.0f},    // 2: 濃い紫
		sgc::Colorf{0.0f/255, 135.0f/255, 81.0f/255, 1.0f},     // 3: 濃い緑
		sgc::Colorf{171.0f/255, 82.0f/255, 54.0f/255, 1.0f},    // 4: 茶
		sgc::Colorf{95.0f/255, 87.0f/255, 79.0f/255, 1.0f},     // 5: 濃い灰
		sgc::Colorf{194.0f/255, 195.0f/255, 199.0f/255, 1.0f},  // 6: 薄い灰
		sgc::Colorf{255.0f/255, 241.0f/255, 232.0f/255, 1.0f},  // 7: 白
		sgc::Colorf{255.0f/255, 0.0f/255, 77.0f/255, 1.0f},     // 8: 赤
		sgc::Colorf{255.0f/255, 163.0f/255, 0.0f/255, 1.0f},    // 9: 橙
		sgc::Colorf{255.0f/255, 236.0f/255, 39.0f/255, 1.0f},   // 10: 黄
		sgc::Colorf{0.0f/255, 228.0f/255, 54.0f/255, 1.0f},     // 11: 緑
		sgc::Colorf{41.0f/255, 173.0f/255, 255.0f/255, 1.0f},   // 12: 青
		sgc::Colorf{131.0f/255, 118.0f/255, 156.0f/255, 1.0f},  // 13: ラベンダー
		sgc::Colorf{255.0f/255, 119.0f/255, 168.0f/255, 1.0f},  // 14: ピンク
		sgc::Colorf{255.0f/255, 204.0f/255, 170.0f/255, 1.0f}   // 15: 薄いピーチ
	}};

	/// @brief NES風パレット（代表的な16色）
	static constexpr std::array<sgc::Colorf, PALETTE_SIZE> NES = {{
		sgc::Colorf{0.0f/255, 0.0f/255, 0.0f/255, 1.0f},
		sgc::Colorf{252.0f/255, 252.0f/255, 252.0f/255, 1.0f},
		sgc::Colorf{188.0f/255, 188.0f/255, 188.0f/255, 1.0f},
		sgc::Colorf{124.0f/255, 124.0f/255, 124.0f/255, 1.0f},
		sgc::Colorf{168.0f/255, 16.0f/255, 0.0f/255, 1.0f},
		sgc::Colorf{228.0f/255, 92.0f/255, 16.0f/255, 1.0f},
		sgc::Colorf{248.0f/255, 216.0f/255, 120.0f/255, 1.0f},
		sgc::Colorf{88.0f/255, 216.0f/255, 84.0f/255, 1.0f},
		sgc::Colorf{0.0f/255, 168.0f/255, 0.0f/255, 1.0f},
		sgc::Colorf{0.0f/255, 168.0f/255, 68.0f/255, 1.0f},
		sgc::Colorf{0.0f/255, 136.0f/255, 136.0f/255, 1.0f},
		sgc::Colorf{0.0f/255, 120.0f/255, 248.0f/255, 1.0f},
		sgc::Colorf{104.0f/255, 68.0f/255, 252.0f/255, 1.0f},
		sgc::Colorf{148.0f/255, 0.0f/255, 132.0f/255, 1.0f},
		sgc::Colorf{216.0f/255, 0.0f/255, 204.0f/255, 1.0f},
		sgc::Colorf{248.0f/255, 120.0f/255, 248.0f/255, 1.0f}
	}};

	/// @brief パステルパレット（16色）
	static constexpr std::array<sgc::Colorf, PALETTE_SIZE> PASTEL = {{
		sgc::Colorf{255.0f/255, 179.0f/255, 186.0f/255, 1.0f},
		sgc::Colorf{255.0f/255, 223.0f/255, 186.0f/255, 1.0f},
		sgc::Colorf{255.0f/255, 255.0f/255, 186.0f/255, 1.0f},
		sgc::Colorf{186.0f/255, 255.0f/255, 201.0f/255, 1.0f},
		sgc::Colorf{186.0f/255, 225.0f/255, 255.0f/255, 1.0f},
		sgc::Colorf{186.0f/255, 186.0f/255, 255.0f/255, 1.0f},
		sgc::Colorf{223.0f/255, 186.0f/255, 255.0f/255, 1.0f},
		sgc::Colorf{255.0f/255, 186.0f/255, 255.0f/255, 1.0f},
		sgc::Colorf{255.0f/255, 218.0f/255, 218.0f/255, 1.0f},
		sgc::Colorf{218.0f/255, 255.0f/255, 218.0f/255, 1.0f},
		sgc::Colorf{218.0f/255, 218.0f/255, 255.0f/255, 1.0f},
		sgc::Colorf{255.0f/255, 245.0f/255, 218.0f/255, 1.0f},
		sgc::Colorf{230.0f/255, 230.0f/255, 230.0f/255, 1.0f},
		sgc::Colorf{200.0f/255, 200.0f/255, 200.0f/255, 1.0f},
		sgc::Colorf{170.0f/255, 220.0f/255, 230.0f/255, 1.0f},
		sgc::Colorf{240.0f/255, 200.0f/255, 220.0f/255, 1.0f}
	}};

	/// @brief ゲームボーイ風パレット（4色、残りは最暗色で埋める）
	static constexpr std::array<sgc::Colorf, PALETTE_SIZE> GAMEBOY = {{
		sgc::Colorf{15.0f/255, 56.0f/255, 15.0f/255, 1.0f},     // 0: 最暗
		sgc::Colorf{48.0f/255, 98.0f/255, 48.0f/255, 1.0f},     // 1: 暗
		sgc::Colorf{139.0f/255, 172.0f/255, 15.0f/255, 1.0f},   // 2: 明
		sgc::Colorf{155.0f/255, 188.0f/255, 15.0f/255, 1.0f},   // 3: 最明
		sgc::Colorf{15.0f/255, 56.0f/255, 15.0f/255, 1.0f},
		sgc::Colorf{15.0f/255, 56.0f/255, 15.0f/255, 1.0f},
		sgc::Colorf{15.0f/255, 56.0f/255, 15.0f/255, 1.0f},
		sgc::Colorf{15.0f/255, 56.0f/255, 15.0f/255, 1.0f},
		sgc::Colorf{15.0f/255, 56.0f/255, 15.0f/255, 1.0f},
		sgc::Colorf{15.0f/255, 56.0f/255, 15.0f/255, 1.0f},
		sgc::Colorf{15.0f/255, 56.0f/255, 15.0f/255, 1.0f},
		sgc::Colorf{15.0f/255, 56.0f/255, 15.0f/255, 1.0f},
		sgc::Colorf{15.0f/255, 56.0f/255, 15.0f/255, 1.0f},
		sgc::Colorf{15.0f/255, 56.0f/255, 15.0f/255, 1.0f},
		sgc::Colorf{15.0f/255, 56.0f/255, 15.0f/255, 1.0f},
		sgc::Colorf{15.0f/255, 56.0f/255, 15.0f/255, 1.0f}
	}};

	/// @brief パレットから色を安全に取得する（インデックスはラップアラウンド）
	/// @param palette パレット種別
	/// @param index カラーインデックス（負値やサイズ超過時はラップ）
	/// @return 指定パレットの色
	[[nodiscard]] static constexpr sgc::Colorf get(PaletteType palette, int index) noexcept
	{
		const int wrapped = ((index % PALETTE_SIZE) + PALETTE_SIZE) % PALETTE_SIZE;
		switch (palette)
		{
		case PaletteType::Pico8:   return PICO8[static_cast<std::size_t>(wrapped)];
		case PaletteType::Nes:     return NES[static_cast<std::size_t>(wrapped)];
		case PaletteType::Pastel:  return PASTEL[static_cast<std::size_t>(wrapped)];
		case PaletteType::GameBoy: return GAMEBOY[static_cast<std::size_t>(wrapped)];
		}
		return PICO8[0]; // フォールバック
	}

	/// @brief 配列から色を安全に取得する（ラップアラウンド）
	/// @param palette 16色配列
	/// @param index カラーインデックス
	/// @return 色
	[[nodiscard]] static constexpr sgc::Colorf get(
		const std::array<sgc::Colorf, PALETTE_SIZE>& palette, int index) noexcept
	{
		const int wrapped = ((index % PALETTE_SIZE) + PALETTE_SIZE) % PALETTE_SIZE;
		return palette[static_cast<std::size_t>(wrapped)];
	}
};

} // namespace mitiru::util
