# showcase_platformer — 旗艦サンプル (Game-as-DLL、ADR 0005)
#
# 既存 engine capability (physics / camera / juice / sound intent / zero-JS HUD /
# ADR 0013 GameMemory recording) を 1 本の横スクロール platformer に組み上げた参照実装。
# mitiru_host.exe が runtime に load する SHARED library。
#
# 実行:  mitiru_host.exe showcase_platformer/showcase_platformer.dll

add_library(showcase_platformer SHARED showcase_platformer_dll.cpp)
target_link_libraries(showcase_platformer PRIVATE mitiru)

if(MSVC)
	target_compile_options(showcase_platformer PRIVATE /bigobj)
endif()

# mitiru_host の隣に <name>/ を作り、DLL と assets を配置する (hello_game と同じ規約)。
# host は cwd を自身の dir に固定し cefStartUrl = "<dll_dir>/assets/scene.html" を解決する。
if(TARGET mitiru_host)
	set(_sp_runtime_dir "$<TARGET_FILE_DIR:mitiru_host>/showcase_platformer")

	set_target_properties(showcase_platformer PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${_sp_runtime_dir}"
		LIBRARY_OUTPUT_DIRECTORY "${_sp_runtime_dir}")

	# assets/ を source-tracked stamp 経由で deploy (編集が確実に再 copy される)。
	file(GLOB_RECURSE _sp_assets CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
	set(_sp_stamp "${CMAKE_CURRENT_BINARY_DIR}/showcase_platformer_assets.stamp")
	add_custom_command(
		OUTPUT  "${_sp_stamp}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_CURRENT_SOURCE_DIR}/assets"
			"${_sp_runtime_dir}/assets"
		COMMAND ${CMAKE_COMMAND} -E touch "${_sp_stamp}"
		DEPENDS ${_sp_assets}
		VERBATIM
		COMMENT "showcase_platformer: deploying assets/ (source-tracked)")
	add_custom_target(showcase_platformer_assets ALL DEPENDS "${_sp_stamp}")
	add_dependencies(showcase_platformer showcase_platformer_assets)

	# zero-JS HUD が <link>/​<script> する engine 共有 runtime (mitiru_bind.js 等) を同梱。
	file(GLOB_RECURSE _sp_runtime_assets CONFIGURE_DEPENDS
		"${CMAKE_SOURCE_DIR}/web/mitiru_runtime/*")
	set(_sp_runtime_stamp "${CMAKE_CURRENT_BINARY_DIR}/showcase_platformer_runtime.stamp")
	add_custom_command(
		OUTPUT  "${_sp_runtime_stamp}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_SOURCE_DIR}/web/mitiru_runtime"
			"${_sp_runtime_dir}/assets/mitiru_runtime"
		COMMAND ${CMAKE_COMMAND} -E touch "${_sp_runtime_stamp}"
		DEPENDS ${_sp_runtime_assets}
		VERBATIM
		COMMENT "showcase_platformer: deploying mitiru_runtime/ (source-tracked)")
	add_custom_target(showcase_platformer_runtime ALL DEPENDS "${_sp_runtime_stamp}")
	add_dependencies(showcase_platformer showcase_platformer_runtime)

	add_dependencies(showcase_platformer mitiru_host)
endif()
