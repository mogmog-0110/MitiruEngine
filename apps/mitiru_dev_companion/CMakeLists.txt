# mitiru_dev_companion — compact "dev console" bar that runs alongside the
# game during watch-mode sessions. Built like any other Game-as-DLL.
#
# Atomic-tools split (2026-05-21):
#   - launcher (project picker only)             → mitiru_launcher.dll
#   - game     (your gameplay)                   → your_game.dll
#   - companion (Build / Inspector / Stop bar)   → mitiru_dev_companion.dll  ← this
#
# Each is a separate mitiru_host process. The companion learns about the
# active session by reading a JSON file the launcher dropped in %TEMP%
# (see kSessionFileEnv in the .cpp).

add_library(mitiru_dev_companion SHARED mitiru_dev_companion.cpp)
target_link_libraries(mitiru_dev_companion PRIVATE mitiru)

if(MSVC)
	target_compile_options(mitiru_dev_companion PRIVATE /bigobj)
	target_link_libraries(mitiru_dev_companion PRIVATE shell32 user32)
endif()

if(TARGET mitiru_host)
	set(_companion_runtime_dir "$<TARGET_FILE_DIR:mitiru_host>/mitiru_dev_companion")

	set_target_properties(mitiru_dev_companion PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${_companion_runtime_dir}"
		LIBRARY_OUTPUT_DIRECTORY "${_companion_runtime_dir}")

	# === asset auto-deploy (re-deploy when any file under assets/ changes) ===
	# POST_BUILD は DLL が dirty な時しか fire しない。assets/scene.html を
	# 編集しただけだと旧 copy が残る bug を防ぐため、source-tracked な
	# custom_command(OUTPUT) + custom_target(ALL) で stamp 経由依存させる。
	# Stamp は build/ 配下 (generator-expression-free path) に置く。
	file(GLOB_RECURSE _mitiru_dev_companion_assets CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
	set(_mitiru_dev_companion_stamp "${CMAKE_CURRENT_BINARY_DIR}/mitiru_dev_companion_assets.stamp")
	add_custom_command(
		OUTPUT  "${_mitiru_dev_companion_stamp}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_CURRENT_SOURCE_DIR}/assets"
			"${_companion_runtime_dir}/assets"
		COMMAND ${CMAKE_COMMAND} -E touch "${_mitiru_dev_companion_stamp}"
		DEPENDS ${_mitiru_dev_companion_assets}
		VERBATIM
		COMMENT "mitiru_dev_companion: deploying assets/ (source-tracked)")
	add_custom_target(mitiru_dev_companion_assets ALL
		DEPENDS "${_mitiru_dev_companion_stamp}")
	add_dependencies(mitiru_dev_companion mitiru_dev_companion_assets)

	# Engine runtime JS copy (bridge + binder) — also source-tracked.
	# Page is zero-JS (data-m-* only).
	set(_mitiru_dev_companion_cefjs_src
		"${CMAKE_SOURCE_DIR}/web/mitiru_runtime/mitiru_cef_state.js"
		"${CMAKE_SOURCE_DIR}/web/mitiru_runtime/mitiru_bind.js")
	set(_mitiru_dev_companion_cefjs_stamp
		"${CMAKE_CURRENT_BINARY_DIR}/mitiru_dev_companion_cefjs.stamp")
	add_custom_command(
		OUTPUT  "${_mitiru_dev_companion_cefjs_stamp}"
		COMMAND ${CMAKE_COMMAND} -E make_directory
			"${_companion_runtime_dir}/assets/mitiru_runtime"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			${_mitiru_dev_companion_cefjs_src}
			"${_companion_runtime_dir}/assets/mitiru_runtime"
		COMMAND ${CMAKE_COMMAND} -E touch "${_mitiru_dev_companion_cefjs_stamp}"
		DEPENDS ${_mitiru_dev_companion_cefjs_src}
		VERBATIM
		COMMENT "mitiru_dev_companion: deploying mitiru_runtime JS (source-tracked)")
	add_custom_target(mitiru_dev_companion_cefjs ALL
		DEPENDS "${_mitiru_dev_companion_cefjs_stamp}")
	add_dependencies(mitiru_dev_companion mitiru_dev_companion_cefjs)

	add_dependencies(mitiru_dev_companion mitiru_host)
endif()
