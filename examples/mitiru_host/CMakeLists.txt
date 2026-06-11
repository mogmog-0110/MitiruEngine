# mitiru_host — Universal launcher exe for any mitiru game DLL.
#
# Usage:
#   mitiru_host.exe path/to/your_game.dll [scene.html relative to host dir]
#
# This is the engine's reference host process (ADR 0005, v0.2.0 step 3/N).
# Future: `mitiru-cli run` will resolve a project's module.toml to call this.

add_executable(mitiru_host WIN32 main.cpp)
target_link_libraries(mitiru_host PRIVATE mitiru)
if(WIN32)
	# ShellExecuteA を使うため (--console 用の既定ブラウザ起動、ADR 0011 phase 3)
	target_link_libraries(mitiru_host PRIVATE shell32)
endif()

if(MSVC)
	target_compile_options(mitiru_host PRIVATE /bigobj)
endif()

# subsystem: dev はコンソール (argv/stdout/ログが見える)。配布は MITIRU_HOST_GUI=ON で
# GUI subsystem にしてコンソール窓を出さない (mitiru dist が設定)。entry は main() のまま
# (/ENTRY:mainCRTStartup) なので argv 処理は不変。
if(WIN32)
	if(MITIRU_HOST_GUI)
		# 明示的に GUI subsystem。entry は main() のまま (/ENTRY:mainCRTStartup)。
		set_target_properties(mitiru_host PROPERTIES WIN32_EXECUTABLE ON)
		target_link_options(mitiru_host PRIVATE
			/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
	else()
		set_target_properties(mitiru_host PROPERTIES WIN32_EXECUTABLE OFF)
		target_link_options(mitiru_host PRIVATE /SUBSYSTEM:CONSOLE)
	endif()
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

# SDL2 backend 有効時は SDL2.dll を host の隣へ deploy する (DS4 等 DirectInput パッド対応)。
# SDL2::SDL2 は shared imported target なので TARGET_FILE = SDL2.dll。
if(WIN32 AND TARGET SDL2::SDL2)
	add_custom_command(TARGET mitiru_host POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"$<TARGET_FILE:SDL2::SDL2>"
			"$<TARGET_FILE_DIR:mitiru_host>/SDL2.dll"
		COMMENT "mitiru_host: deploying SDL2.dll (DirectInput gamepad support)")
endif()
