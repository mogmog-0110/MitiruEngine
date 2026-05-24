# mitiru_host — Universal launcher exe for any mitiru game DLL.
#
# Usage:
#   mitiru_host.exe path/to/your_game.dll [scene.html relative to host dir]
#
# This is the engine's reference host process (ADR 0005, v0.2.0 step 3/N).
# Future: `mitiru-cli run` will resolve a project's module.toml to call this.

add_executable(mitiru_host WIN32 main.cpp)
target_link_libraries(mitiru_host PRIVATE mitiru)

if(MSVC)
	target_compile_options(mitiru_host PRIVATE /bigobj)
endif()

# WIN32 subsystem requires WinMain; main.cpp also exports a plain main()
# fallback. We rely on the linker picking the right one.
# Override to CONSOLE so argv/stdout work as expected for a CLI launcher.
if(WIN32)
	set_target_properties(mitiru_host PROPERTIES WIN32_EXECUTABLE OFF)
endif()

# Deploy CEF runtime + helper exe next to mitiru_host.exe — the host owns
# CEF entirely; loaded DLLs see CEF through the engine's normal interfaces.
mitiru_add_cef_game(mitiru_host)

# Dev convenience: drop the launcher .bat next to mitiru_host.exe so the
# build tree mirrors the deployed zip layout. Users can double-click the
# .bat in the build directory without going through cpack / install.
if(WIN32)
	# Configure the .bat with version interpolation, then copy to build dir.
	set(_dev_bat "${CMAKE_CURRENT_BINARY_DIR}/MitiruEngine_Launcher.bat")
	configure_file(
		"${CMAKE_SOURCE_DIR}/cmake/dist/MitiruEngine_Launcher.bat.in"
		"${_dev_bat}"
		@ONLY)
	add_custom_command(TARGET mitiru_host POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${_dev_bat}"
			"$<TARGET_FILE_DIR:mitiru_host>/MitiruEngine_Launcher.bat"
		COMMENT "mitiru_host: deploying MitiruEngine_Launcher.bat to build dir")
endif()
