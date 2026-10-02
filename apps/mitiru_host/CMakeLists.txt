# mitiru_host は、任意の mitiru ゲーム DLL を起動するための汎用ランチャー exe。
#
# 使用方法:
#   mitiru_host.exe path/to/your_game.dll [options]
#
# これはエンジンのリファレンスホストプロセス。
# 将来は `mitiru-cli run` がプロジェクトの module.toml を解決して、これを呼び出す。

add_executable(mitiru_host WIN32 main.cpp)
target_link_libraries(mitiru_host PRIVATE mitiru)
if(WIN32)
	# ShellExecuteA を使うため (--console 用の既定ブラウザを起動するフェーズ 3)
	target_link_libraries(mitiru_host PRIVATE shell32)
endif()

if(MSVC)
	target_compile_options(mitiru_host PRIVATE /bigobj)
endif()

# -DMITIRU_WITH_CRASH_REPORT=ON のときだけ存在する (ルート CMakeLists の sentry-native 節)。
if(TARGET mitiru_crash_report)
	target_link_libraries(mitiru_host PRIVATE mitiru_crash_report)
endif()

# サブシステムは、dev ではコンソール (argv/stdout/ログが見える)。配布時は MITIRU_HOST_GUI=ON で
# GUI サブシステムにしてコンソール窓を出さない (mitiru dist が設定)。エントリは main() のまま
# (/ENTRY:mainCRTStartup) なので argv の処理は変わらない。
if(WIN32)
	if(MITIRU_HOST_GUI)
		# 明示的に GUI サブシステムにする。エントリは main() のまま (/ENTRY:mainCRTStartup)。
		set_target_properties(mitiru_host PROPERTIES WIN32_EXECUTABLE ON)
		target_link_options(mitiru_host PRIVATE
			/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
	else()
		set_target_properties(mitiru_host PROPERTIES WIN32_EXECUTABLE OFF)
		target_link_options(mitiru_host PRIVATE /SUBSYSTEM:CONSOLE)
	endif()
endif()

# 開発用として、ランチャーの .bat を mitiru_host.exe の隣に置き、
# ビルドツリーを配置用 zip と同じ構成にする。ユーザーは cpack / install を介さず、
# ビルドディレクトリ内の .bat をダブルクリックできる。
if(WIN32)
	# バージョンを展開して .bat を構成し、ビルドディレクトリにコピーする。
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

mitiru_deploy_sdl3(mitiru_host)
mitiru_deploy_steamaudio(mitiru_host)
mitiru_deploy_steamworks(mitiru_host)

mitiru_deploy_dxc(mitiru_host)

# Debug CRT をホストの隣へ配置する (#55: クリーン環境で mitiru run が 0xC0000135
# STATUS_DLL_NOT_FOUND により直ちに終了するのを防ぐ)。Debug CRT は再配布できない開発専用 DLL で
# PATH に依存しやすいため、SDL3 と同じく exe の隣へ置く。Debug ビルドに限定する。
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

	# Debug UCRT (ucrtbased.dll) は redist に含まれないため、Windows SDK から探す。
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

# ONNX Runtime と DirectML のランタイム DLL をホストの隣へ配置する (GPU ニューラル後処理)。
# onnxruntime.dll は DirectML.dll に依存し、provider_shared は EP の解決に必要となる。
if(WIN32)
	# エンジンのルートは apps/mitiru_host から見て../../ (CLI ビルドでも CMAKE_SOURCE_DIR に依存しない)
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

# 同梱の日本語フォント (normal=M+ Rounded 1c / retro=PixelMplus) とそのライセンスをホストの隣へ配置する。
# initFont が exe からの相対パスで解決するため、ネイティブ描画 (エラー帯・ゲーム内テキスト) で日本語を表示できる。
# Bold は UI (RmlUi) の太字に使う。
foreach(_jpfont "MPLUSRounded1c-Regular.ttf" "MPLUSRounded1c-Bold.ttf" "PixelMplus12-Regular.ttf" "OFL.txt" "PixelMplus-README.txt")
	if(EXISTS "${CMAKE_SOURCE_DIR}/assets/fonts/${_jpfont}")
		add_custom_command(TARGET mitiru_host POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${CMAKE_SOURCE_DIR}/assets/fonts/${_jpfont}"
				"$<TARGET_FILE_DIR:mitiru_host>/assets/fonts/${_jpfont}"
			COMMENT "mitiru_host: deploying bundled JP font (${_jpfont})")
	endif()
endforeach()
# UI (RmlUi) に同梱する RCSS。RML から href="mitiru:base.rcss" で参照する。
if(TARGET mitiru_ui_rml)
	file(GLOB _ui_rcss CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/ui/*.rcss")
	foreach(_rcss IN LISTS _ui_rcss)
		get_filename_component(_rcss_name "${_rcss}" NAME)
		add_custom_command(TARGET mitiru_host POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${_rcss}" "$<TARGET_FILE_DIR:mitiru_host>/assets/ui/${_rcss_name}"
			COMMENT "mitiru_host: deploying UI stylesheet (${_rcss_name})")
	endforeach()
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt")
	add_custom_command(TARGET mitiru_host POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt"
			"$<TARGET_FILE_DIR:mitiru_host>/THIRD_PARTY_NOTICES.txt"
		COMMENT "mitiru_host: deploying THIRD_PARTY_NOTICES.txt")
endif()
