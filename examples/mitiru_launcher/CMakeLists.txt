# mitiru_launcher — GUI launcher dogfooded as a Game-as-DLL (v0.2.0 / BAA)
#
# The launcher itself is a mitiru module: mitiru_host.exe loads its DLL just
# like any user game. Its "gameplay" is project management — listing
# registered game DLLs and spawning child mitiru_host processes to run them.
#
# Per ADR 0005 the launcher DLL never calls into the engine's C++ API. All
# host services it needs come from intent fields (state pushes, JS exec) or
# from Win32 directly (CreateProcess, ShellExecute, GetOpenFileName,
# SHGetKnownFolderPath) — those are not engine API.

add_library(mitiru_launcher SHARED mitiru_launcher_dll.cpp)
target_link_libraries(mitiru_launcher PRIVATE mitiru)

if(MSVC)
	target_compile_options(mitiru_launcher PRIVATE /bigobj)
	# Win32 file dialog + Shell APIs
	target_link_libraries(mitiru_launcher PRIVATE comdlg32 shell32 ole32)
endif()

# Layout next to mitiru_host so a single tree contains both the launcher and
# the example games it can launch:
#
#   <host_dir>/
#     mitiru_host.exe + CEF runtime
#     mitiru_launcher/
#       mitiru_launcher.dll
#       assets/scene.html + mitiru_runtime/
#     hello_game/
#       hello_game.dll
#       assets/

if(TARGET mitiru_host)
	set(_launcher_runtime_dir "$<TARGET_FILE_DIR:mitiru_host>/mitiru_launcher")

	set_target_properties(mitiru_launcher PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${_launcher_runtime_dir}"
		LIBRARY_OUTPUT_DIRECTORY "${_launcher_runtime_dir}")

	# === asset auto-deploy (re-deploy when any file under assets/ changes) ===
	# POST_BUILD は DLL が dirty な時しか fire しない。assets/scene.html を
	# 編集しただけだと旧 copy が残る bug を防ぐため、source-tracked な
	# custom_command(OUTPUT) + custom_target(ALL) で stamp 経由依存させる。
	# Stamp は build/ 配下 (generator-expression-free path) に置き、
	# 実 deploy 先 (_launcher_runtime_dir) は COMMAND 内で generator
	# expression として評価させる — OUTPUT に generator expression は
	# 使えないため。
	file(GLOB_RECURSE _mitiru_launcher_assets CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
	set(_mitiru_launcher_stamp "${CMAKE_CURRENT_BINARY_DIR}/mitiru_launcher_assets.stamp")
	add_custom_command(
		OUTPUT  "${_mitiru_launcher_stamp}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_CURRENT_SOURCE_DIR}/assets"
			"${_launcher_runtime_dir}/assets"
		COMMAND ${CMAKE_COMMAND} -E touch "${_mitiru_launcher_stamp}"
		DEPENDS ${_mitiru_launcher_assets}
		VERBATIM
		COMMENT "mitiru_launcher: deploying assets/ (source-tracked)")
	add_custom_target(mitiru_launcher_assets ALL
		DEPENDS "${_mitiru_launcher_stamp}")
	add_dependencies(mitiru_launcher mitiru_launcher_assets)

	# Single-file engine runtime copy — also source-tracked so edits to
	# the engine-shared bridge JS propagate without a DLL rebuild.
	set(_mitiru_launcher_cefjs_src
		"${CMAKE_SOURCE_DIR}/web/mitiru_runtime/mitiru_cef_state.js")
	set(_mitiru_launcher_cefjs_stamp
		"${CMAKE_CURRENT_BINARY_DIR}/mitiru_launcher_cefjs.stamp")
	add_custom_command(
		OUTPUT  "${_mitiru_launcher_cefjs_stamp}"
		COMMAND ${CMAKE_COMMAND} -E make_directory
			"${_launcher_runtime_dir}/assets/mitiru_runtime"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${_mitiru_launcher_cefjs_src}"
			"${_launcher_runtime_dir}/assets/mitiru_runtime/mitiru_cef_state.js"
		COMMAND ${CMAKE_COMMAND} -E touch "${_mitiru_launcher_cefjs_stamp}"
		DEPENDS "${_mitiru_launcher_cefjs_src}"
		VERBATIM
		COMMENT "mitiru_launcher: deploying mitiru_cef_state.js (source-tracked)")
	add_custom_target(mitiru_launcher_cefjs ALL
		DEPENDS "${_mitiru_launcher_cefjs_stamp}")
	add_dependencies(mitiru_launcher mitiru_launcher_cefjs)

	add_dependencies(mitiru_launcher mitiru_host)
endif()
