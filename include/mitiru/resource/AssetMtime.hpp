#pragma once

/// @file AssetMtime.hpp
/// @brief アセットファイルの mtime 変化を検知する最小プリミティブ。
/// @details texture / wav / json 等、game が hot reload したい任意の asset に共通で使える。
///          重い `HotReloadManager`(コールバック方式) より軽い「叩いて聞く」スタイル。
///
/// @code
/// static std::filesystem::file_time_type lastMt{};
/// if (mitiru::resource::pollFileMtimeChanged("assets/hero.png", lastMt)) {
///     if (auto t = mitiru::render::Texture::fromFile("assets/hero.png")) { tex = *t; }
/// }
/// @endcode

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace mitiru::resource
{

/// @brief path の mtime を取得し、last より新しければ last を更新して true を返す。
/// @details 初回呼び出し (last が default = epoch) は必ず true を返して last を埋める。
///          ファイル不在 / アクセス不可なら false (last は触らない)。
[[nodiscard]] inline bool pollFileMtimeChanged(std::string_view path,
                                               std::filesystem::file_time_type& last)
{
	std::error_code ec;
	const auto cur = std::filesystem::last_write_time(std::filesystem::path{path}, ec);
	if (ec) { return false; }
	if (cur > last)
	{
		last = cur;
		return true;
	}
	return false;
}

}  // namespace mitiru::resource
