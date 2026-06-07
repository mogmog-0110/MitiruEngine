# mitiru_tool_cef — 独立ツール窓の汎用 CEF/HTML ホスト。
# `--page <name>` で assets/<name>.html を全面表示し、SharedSnapshot 全体を JS に push。
# 新規ツール窓 = HTML を 1 枚足すだけ (C++ 改変ゼロ)。

add_executable(mitiru_tool_cef main.cpp)
target_link_libraries(mitiru_tool_cef PRIVATE mitiru)

if(MSVC)
	target_compile_options(mitiru_tool_cef PRIVATE /bigobj)
endif()

# CEF ランタイム (libcef.dll / *.pak / locales/ / MitiruCefHelper.exe) を exe の隣へ。
if(COMMAND mitiru_add_cef_game)
	mitiru_add_cef_game(mitiru_tool_cef)
endif()

# assets/ (各 *.html + tool.css) を source-tracked で配備 (asset 編集でも re-deploy)。
set(_toolcef_dir "$<TARGET_FILE_DIR:mitiru_tool_cef>")
file(GLOB_RECURSE _toolcef_assets CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
set(_toolcef_stamp "${CMAKE_CURRENT_BINARY_DIR}/tool_cef_assets.stamp")
add_custom_command(
	OUTPUT  "${_toolcef_stamp}"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_CURRENT_SOURCE_DIR}/assets"
		"${_toolcef_dir}/assets"
	COMMAND ${CMAKE_COMMAND} -E touch "${_toolcef_stamp}"
	DEPENDS ${_toolcef_assets}
	VERBATIM
	COMMENT "mitiru_tool_cef: deploying assets/ (source-tracked)")
add_custom_target(mitiru_tool_cef_assets ALL DEPENDS "${_toolcef_stamp}")
add_dependencies(mitiru_tool_cef mitiru_tool_cef_assets)
