# hello_game — Game-as-DLL example (v0.2.0 step 3, ADR 0005)
#
# Built as a SHARED library that the engine host (mitiru_host.exe) loads
# at runtime via Engine::loadModule(). DLL boundary is C-only signal flow
# per ADR 0005 — see hello_game_dll.cpp for the contract impl.
#
# Assets (scene.html, mitiru_cef_state.js) are deployed next to the DLL.
# The mitiru_host launcher cwd-anchors next to the host exe and sets
# cefStartUrl = "file:///./hello_game/assets/scene.html" so the path works
# both when the DLL is loaded from build/examples/hello_game/ during dev
# and from a deployed install dir.

add_library(hello_game SHARED hello_game_dll.cpp)
target_link_libraries(hello_game PRIVATE mitiru)

# /bigobj because mitiru is header-only and pulls in many template specs.
if(MSVC)
	target_compile_options(hello_game PRIVATE /bigobj)
endif()

# Layout the runtime tree expected by mitiru_host:
#
#   <build>/examples/mitiru_host/
#     mitiru_host.exe
#     libcef.dll, *.pak, locales/, MitiruCefHelper.exe   (deployed by mitiru_host)
#     hello_game/
#       hello_game.dll                                    (this target)
#       assets/
#         scene.html
#         mitiru_runtime/mitiru_cef_state.js
#
# Putting the DLL next to its assets keeps mitiru_host's cwd anchor stable
# (cwd = mitiru_host dir → relative path "hello_game/assets/scene.html").

if(TARGET mitiru_host)
	set(_hello_runtime_dir "$<TARGET_FILE_DIR:mitiru_host>/hello_game")

	set_target_properties(hello_game PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${_hello_runtime_dir}"
		LIBRARY_OUTPUT_DIRECTORY "${_hello_runtime_dir}")

	# === asset auto-deploy (re-deploy when any file under assets/ changes) ===
	# POST_BUILD は DLL が dirty な時しか fire しない。assets/scene.html を
	# 編集しただけだと旧 copy が残る bug を防ぐため、source-tracked な
	# custom_command(OUTPUT) + custom_target(ALL) で stamp 経由依存させる。
	# Stamp は build/ 配下 (generator-expression-free path) に置き、
	# 実 deploy 先 (_hello_runtime_dir) は COMMAND 内で generator expression
	# として評価させる — OUTPUT に generator expression は使えないため。
	file(GLOB_RECURSE _hello_game_assets CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
	set(_hello_game_stamp "${CMAKE_CURRENT_BINARY_DIR}/hello_game_assets.stamp")
	add_custom_command(
		OUTPUT  "${_hello_game_stamp}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_CURRENT_SOURCE_DIR}/assets"
			"${_hello_runtime_dir}/assets"
		COMMAND ${CMAKE_COMMAND} -E touch "${_hello_game_stamp}"
		DEPENDS ${_hello_game_assets}
		VERBATIM
		COMMENT "hello_game: deploying assets/ (source-tracked)")
	add_custom_target(hello_game_assets ALL
		DEPENDS "${_hello_game_stamp}")
	add_dependencies(hello_game hello_game_assets)

	# Copy the whole mitiru_runtime tree so scene.html can <link> any of
	# the shared CSS / JS modules (mitiru_tokens.css, mitiru_cef_state.js,
	# theme overlays, etc.) without per-file CMake plumbing. Tracked via
	# the same stamp pattern so edits to engine-shared runtime re-deploy.
	file(GLOB_RECURSE _hello_game_runtime_assets CONFIGURE_DEPENDS
		"${CMAKE_SOURCE_DIR}/web/mitiru_runtime/*")
	set(_hello_game_runtime_stamp "${CMAKE_CURRENT_BINARY_DIR}/hello_game_runtime_assets.stamp")
	add_custom_command(
		OUTPUT  "${_hello_game_runtime_stamp}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_SOURCE_DIR}/web/mitiru_runtime"
			"${_hello_runtime_dir}/assets/mitiru_runtime"
		COMMAND ${CMAKE_COMMAND} -E touch "${_hello_game_runtime_stamp}"
		DEPENDS ${_hello_game_runtime_assets}
		VERBATIM
		COMMENT "hello_game: deploying mitiru_runtime/ (source-tracked)")
	add_custom_target(hello_game_runtime_assets ALL
		DEPENDS "${_hello_game_runtime_stamp}")
	add_dependencies(hello_game hello_game_runtime_assets)

	add_dependencies(hello_game mitiru_host)
endif()
