# dodge — 初心者向け C++-only サンプル (よけゲーム)
#
# HTML を使わず C++ だけで動く最小例。mitiru_host.exe が load する SHARED library。
# assets/ は空の透明 scene.html 1 枚だけ (mitiru_runtime は不要 — script を使わない)。
#
# 実行:  mitiru_host.exe dodge/dodge.dll

add_library(dodge SHARED dodge_dll.cpp)
target_link_libraries(dodge PRIVATE mitiru)

if(MSVC)
	target_compile_options(dodge PRIVATE /bigobj)
endif()

if(TARGET mitiru_host)
	set(_dodge_runtime_dir "$<TARGET_FILE_DIR:mitiru_host>/dodge")

	set_target_properties(dodge PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${_dodge_runtime_dir}"
		LIBRARY_OUTPUT_DIRECTORY "${_dodge_runtime_dir}")

	# assets/ を source-tracked stamp 経由で deploy。
	file(GLOB_RECURSE _dodge_assets CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
	set(_dodge_stamp "${CMAKE_CURRENT_BINARY_DIR}/dodge_assets.stamp")
	add_custom_command(
		OUTPUT  "${_dodge_stamp}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_CURRENT_SOURCE_DIR}/assets"
			"${_dodge_runtime_dir}/assets"
		COMMAND ${CMAKE_COMMAND} -E touch "${_dodge_stamp}"
		DEPENDS ${_dodge_assets}
		VERBATIM
		COMMENT "dodge: deploying assets/ (source-tracked)")
	add_custom_target(dodge_assets ALL DEPENDS "${_dodge_stamp}")
	add_dependencies(dodge dodge_assets)

	add_dependencies(dodge mitiru_host)
endif()
