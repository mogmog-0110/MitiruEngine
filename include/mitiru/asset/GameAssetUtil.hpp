#pragma once

/// @file GameAssetUtil.hpp
/// @brief ゲームアセット共通ユーティリティ

#include <cstdio>
#include <string>

namespace mitiru::asset
{

/// @brief ゲームアセット用ユーティリティ関数群
class GameAssetUtil
{
public:
	/// @brief ネオン色を暗くした背景色を生成する
	/// @param hexColor #RRGGBB形式の色
	/// @return 暗くした色（#RRGGBB形式）
	[[nodiscard]] static std::string darkenColor(const std::string& hexColor)
	{
		if (hexColor.size() < 7 || hexColor[0] != '#')
		{
			return "#111111";
		}

		auto parseHex = [](const std::string& s, size_t pos) -> int
		{
			int val = 0;
			for (size_t i = 0; i < 2; ++i)
			{
				char c = s[pos + i];
				val *= 16;
				if (c >= '0' && c <= '9')
				{
					val += c - '0';
				}
				else if (c >= 'a' && c <= 'f')
				{
					val += c - 'a' + 10;
				}
				else if (c >= 'A' && c <= 'F')
				{
					val += c - 'A' + 10;
				}
			}
			return val;
		};

		const int r = parseHex(hexColor, 1) / 4;
		const int g = parseHex(hexColor, 3) / 4;
		const int b = parseHex(hexColor, 5) / 4;

		char buf[8];
		std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
		return std::string(buf);
	}
};

} // namespace mitiru::asset
