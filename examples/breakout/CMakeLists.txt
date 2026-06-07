# breakout — 旗艦サンプル (Game-as-DLL、ADR 0005)
#
# パドルでボールを返してブロックを全部壊す王道ブロック崩し。エンジンの売り
# (物理 / 当たり判定 / 手触り / zero-JS HTML HUD / 効果音 intent) を 1 本に
# まとめた参照実装。mitiru_host.exe が runtime に load する SHARED library。
#
# 実行:  mitiru_host.exe breakout/breakout.dll

add_library(breakout SHARED breakout_dll.cpp)
target_link_libraries(breakout PRIVATE mitiru)

if(MSVC)
	target_compile_options(breakout PRIVATE /bigobj)
endif()

# mitiru_host の隣に breakout/ を作り DLL と assets を配置する (hello_game と同じ規約)。
# host は cwd を自身の dir に固定し cefStartUrl = "breakout/assets/scene.html" を解決する。
if(TARGET mitiru_host)
	set(_bk_runtime_dir "$<TARGET_FILE_DIR:mitiru_host>/breakout")

	set_target_properties(breakout PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${_bk_runtime_dir}"
		LIBRARY_OUTPUT_DIRECTORY "${_bk_runtime_dir}")

	# assets/ を source-tracked stamp 経由で deploy (編集が確実に再 copy される)。
	file(GLOB_RECURSE _bk_assets CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
	set(_bk_stamp "${CMAKE_CURRENT_BINARY_DIR}/breakout_assets.stamp")
	add_custom_command(
		OUTPUT  "${_bk_stamp}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_CURRENT_SOURCE_DIR}/assets"
			"${_bk_runtime_dir}/assets"
		COMMAND ${CMAKE_COMMAND} -E touch "${_bk_stamp}"
		DEPENDS ${_bk_assets}
		VERBATIM
		COMMENT "breakout: deploying assets/ (source-tracked)")
	add_custom_target(breakout_assets ALL DEPENDS "${_bk_stamp}")
	add_dependencies(breakout breakout_assets)

	# zero-JS HUD が <link>/<script> する engine 共有 runtime (mitiru_bind.js 等) を同梱。
	file(GLOB_RECURSE _bk_runtime_assets CONFIGURE_DEPENDS
		"${CMAKE_SOURCE_DIR}/web/mitiru_runtime/*")
	set(_bk_runtime_stamp "${CMAKE_CURRENT_BINARY_DIR}/breakout_runtime.stamp")
	add_custom_command(
		OUTPUT  "${_bk_runtime_stamp}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_SOURCE_DIR}/web/mitiru_runtime"
			"${_bk_runtime_dir}/assets/mitiru_runtime"
		COMMAND ${CMAKE_COMMAND} -E touch "${_bk_runtime_stamp}"
		DEPENDS ${_bk_runtime_assets}
		VERBATIM
		COMMENT "breakout: deploying mitiru_runtime/ (source-tracked)")
	add_custom_target(breakout_runtime ALL DEPENDS "${_bk_runtime_stamp}")
	add_dependencies(breakout breakout_runtime)

	add_dependencies(breakout mitiru_host)
endif()
