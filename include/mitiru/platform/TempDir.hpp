#pragma once

/// @file TempDir.hpp
/// @brief 一時フォルダの場所。TEMP が無い・存在しない PC でも例外を投げない。
/// @details std::filesystem::temp_directory_path() は、TEMP の指す先が無いと例外を投げて、起動の途中で host が終わる。
///          学校の PC のように TEMP が切れたドライブを指す環境で、使わない機能のために遊べなくならないよう、
///          取れなければ作業フォルダへ回す。そこに書けるかは呼ぶ側が確かめる (書けなければ、その機能だけを諦める)。

#include <filesystem>

namespace mitiru::platform
{

[[nodiscard]] inline std::filesystem::path tempDirectory()
{
	std::error_code ec;
	std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
	if (!ec && !dir.empty()) { return dir; }
	dir = std::filesystem::current_path(ec);
	return !ec && !dir.empty() ? dir : std::filesystem::path(".");
}

}  // namespace mitiru::platform
