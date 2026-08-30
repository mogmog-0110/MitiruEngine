# mitiru_host — Universal launcher exe for any mitiru game DLL.
#
# Usage:
#   mitiru_host.exe path/to/your_game.dll [scene.html relative to host dir]
#
# This is the engine's reference host process.
# Future: `mitiru-cli run` will resolve a project's module.toml to call this.

add_executable(mitiru_host WIN32 main.cpp)
target_link_libraries(mitiru_host PRIVATE mitiru)
if(WIN32)
	# ShellExecuteA を使うため (--console 用の既定ブラウザ起動 phase 3)
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

# Debug CRT を host の隣へ deploy する (#55: clean 環境で mitiru run が 0xC0000135
# STATUS_DLL_NOT_FOUND で即死するのを防ぐ)。Debug CRT は再配布不可の dev 専用 DLL で
# PATH 依存になりやすいため、CEF/SDL2 と同じく exe 隣へ置く。Debug ビルド限定。
if(WIN32 AND MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug")
	# VC Debug ランタイム (msvcp140d / vcruntime140d / vcruntime140_1d) を取得。
	set(CMAKE_INSTALL_DEBUG_LIBRARIES ON)
	set(CMAKE_INSTALL_DEBUG_LIBRARIES_ONLY ON)
	set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP ON)   # install 規則は作らず POST_BUILD で copy
	include(InstallRequiredSystemLibraries)
	foreach(_crt IN LISTS CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
		add_custom_command(TARGET mitiru_host POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${_crt}" "$<TARGET_FILE_DIR:mitiru_host>"
			COMMENT "mitiru_host: deploying Debug CRT (${_crt})")
	endforeach()

	# Debug UCRT (ucrtbased.dll) は redist に含まれないため Windows SDK から locate する。
	file(GLOB _mitiru_ucrt_dbg
		"$ENV{WindowsSdkDir}/bin/*/x64/ucrt/ucrtbased.dll"
		"C:/Program Files (x86)/Windows Kits/10/bin/*/x64/ucrt/ucrtbased.dll")
	if(_mitiru_ucrt_dbg)
		list(SORT _mitiru_ucrt_dbg)
		list(POP_BACK _mitiru_ucrt_dbg _mitiru_ucrtbased)   # 最新 SDK
		add_custom_command(TARGET mitiru_host POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${_mitiru_ucrtbased}" "$<TARGET_FILE_DIR:mitiru_host>/ucrtbased.dll"
			COMMENT "mitiru_host: deploying Debug UCRT (ucrtbased.dll)")
	else()
		message(WARNING "mitiru_host: ucrtbased.dll が見つからず deploy をスキップ。clean 環境で mitiru run が 0xC0000135 になる場合は Windows SDK を確認 (#55)。")
	endif()
endif()

# ONNX Runtime + DirectML ランタイム DLL を host の隣へ deploy する (GPU ニューラル post-process)。
# onnxruntime.dll は DirectML.dll に依存し、provider_shared は EP 解決に要る。
if(WIN32)
	# engine root は apps/mitiru_host から見て ../../ (CLI ビルドでも CMAKE_SOURCE_DIR に依存しない)
	get_filename_component(_ort_bin "${CMAKE_CURRENT_SOURCE_DIR}/../../external/onnxruntime/bin" ABSOLUTE)
	foreach(_ortdll "onnxruntime.dll" "onnxruntime_providers_shared.dll" "DirectML.dll")
		if(EXISTS "${_ort_bin}/${_ortdll}")
			add_custom_command(TARGET mitiru_host POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different
					"${_ort_bin}/${_ortdll}"
					"$<TARGET_FILE_DIR:mitiru_host>/${_ortdll}"
				COMMENT "mitiru_host: deploying ${_ortdll} (ONNX Runtime / DirectML)")
		endif()
	endforeach()
endif()

# 同梱の日本語フォント (normal=M+ Rounded 1c / retro=PixelMplus) を host の隣へ配る。
# initFont が exe 相対で解決するので、native 描画 (エラー帯・ゲーム内テキスト) が日本語可。
foreach(_jpfont "MPLUSRounded1c-Regular.ttf" "PixelMplus12-Regular.ttf")
	if(EXISTS "${CMAKE_SOURCE_DIR}/assets/fonts/${_jpfont}")
		add_custom_command(TARGET mitiru_host POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${CMAKE_SOURCE_DIR}/assets/fonts/${_jpfont}"
				"$<TARGET_FILE_DIR:mitiru_host>/assets/fonts/${_jpfont}"
			COMMENT "mitiru_host: deploying bundled JP font (${_jpfont})")
	endif()
endforeach()
