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

# 共有 JS runtime (state bridge + binder + tool widgets) を assets/mitiru_runtime へ。
# ページは zero-JS (data-m-* 属性のみ) — ロジックは全てこの 3 ファイルが持つ。
set(_toolcef_runtime_files
	"${CMAKE_SOURCE_DIR}/web/mitiru_runtime/mitiru_cef_state.js"
	"${CMAKE_SOURCE_DIR}/web/mitiru_runtime/mitiru_bind.js"
	"${CMAKE_SOURCE_DIR}/web/mitiru_runtime/mitiru_bind_tools.js")
set(_toolcef_runtime_stamp "${CMAKE_CURRENT_BINARY_DIR}/tool_cef_runtime.stamp")
add_custom_command(
	OUTPUT  "${_toolcef_runtime_stamp}"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${_toolcef_dir}/assets/mitiru_runtime"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_toolcef_runtime_files}
		"${_toolcef_dir}/assets/mitiru_runtime"
	COMMAND ${CMAKE_COMMAND} -E touch "${_toolcef_runtime_stamp}"
	DEPENDS ${_toolcef_runtime_files}
	VERBATIM
	COMMENT "mitiru_tool_cef: deploying mitiru_runtime JS (source-tracked)")
add_custom_target(mitiru_tool_cef_runtime ALL DEPENDS "${_toolcef_runtime_stamp}")
add_dependencies(mitiru_tool_cef mitiru_tool_cef_runtime)
