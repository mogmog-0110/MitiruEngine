# anchor (錨) — Game-as-DLL サンプル (制約パズル、ADR 0005)
#
# 「触れた anchor へしか動けない」一行ルールの計画パズル。gameplay は C++ DLL、
# HUD は zero-JS HTML/CSS、効果音は intent。incubator の kept-concept ANCHOR を
# 現行 DLL + HTML/CSS アーキへ port した demo サンプル。
#
# 実行:  mitiru_host.exe anchor/anchor.dll

add_library(anchor SHARED anchor_dll.cpp)
target_link_libraries(anchor PRIVATE mitiru)

if(MSVC)
	target_compile_options(anchor PRIVATE /bigobj)
endif()

# mitiru_host の隣に anchor/ を作り DLL と assets を配置する (breakout と同じ規約)。
if(TARGET mitiru_host)
	set(_ac_runtime_dir "$<TARGET_FILE_DIR:mitiru_host>/anchor")

	set_target_properties(anchor PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${_ac_runtime_dir}"
		LIBRARY_OUTPUT_DIRECTORY "${_ac_runtime_dir}")

	# assets/ を source-tracked stamp 経由で deploy (編集が確実に再 copy される)。
	file(GLOB_RECURSE _ac_assets CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
	set(_ac_stamp "${CMAKE_CURRENT_BINARY_DIR}/anchor_assets.stamp")
	add_custom_command(
		OUTPUT  "${_ac_stamp}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_CURRENT_SOURCE_DIR}/assets"
			"${_ac_runtime_dir}/assets"
		COMMAND ${CMAKE_COMMAND} -E touch "${_ac_stamp}"
		DEPENDS ${_ac_assets}
		VERBATIM
		COMMENT "anchor: deploying assets/ (source-tracked)")
	add_custom_target(anchor_assets ALL DEPENDS "${_ac_stamp}")
	add_dependencies(anchor anchor_assets)

	# zero-JS HUD が <link>/<script> する engine 共有 runtime を同梱。
	file(GLOB_RECURSE _ac_runtime_assets CONFIGURE_DEPENDS
		"${CMAKE_SOURCE_DIR}/web/mitiru_runtime/*")
	set(_ac_runtime_stamp "${CMAKE_CURRENT_BINARY_DIR}/anchor_runtime.stamp")
	add_custom_command(
		OUTPUT  "${_ac_runtime_stamp}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_SOURCE_DIR}/web/mitiru_runtime"
			"${_ac_runtime_dir}/assets/mitiru_runtime"
		COMMAND ${CMAKE_COMMAND} -E touch "${_ac_runtime_stamp}"
		DEPENDS ${_ac_runtime_assets}
		VERBATIM
		COMMENT "anchor: deploying mitiru_runtime/ (source-tracked)")
	add_custom_target(anchor_runtime ALL DEPENDS "${_ac_runtime_stamp}")
	add_dependencies(anchor anchor_runtime)

	add_dependencies(anchor mitiru_host)
endif()
