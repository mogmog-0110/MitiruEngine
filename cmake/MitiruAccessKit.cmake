# AccessKit (読み上げソフトに RML の木を見せる、ADR 0117) は opt-in。accesskit.dll (約 350 KB) が exe の隣に増えて
# 配布物の作りが変わるので、既定は OFF。ライブラリの出どころは external/accesskit/SOURCE.txt、
# 無いときは python tools/fetch_accesskit.py が置く。
#
# dll は遅延読み込みにする。dll が無い PC でも host は起動し、読み上げソフトに見せる木が出ないだけ (段階 1 の通知は残る)。

# exe が実行時に読む accesskit.dll を exe の隣へ置く。使わない構成では、前の構成の dll が出力に残って
# mitiru dist が配ってしまわないよう、消す。
function(mitiru_deploy_accesskit target)
	if(NOT WIN32)
		return()
	endif()
	if(TARGET mitiru_accesskit_dll)
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"$<TARGET_FILE:mitiru_accesskit_dll>" "$<TARGET_FILE_DIR:${target}>/accesskit.dll"
			COMMENT "${target}: deploying accesskit.dll")
	else()
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:${target}>/accesskit.dll")
	endif()
endfunction()

option(MITIRU_WITH_ACCESSKIT "AccessKit で読み上げソフト (Narrator など) に UI の木を見せる。配布物に accesskit.dll が加わる" OFF)
if(NOT MITIRU_WITH_ACCESSKIT)
	return()
endif()

set(_mitiru_ak_root "${CMAKE_CURRENT_SOURCE_DIR}/external/accesskit")
set(_mitiru_ak_dir "${_mitiru_ak_root}/lib/windows/x86_64/msvc/shared")
if(NOT (WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8))
	message(WARNING "MITIRU_WITH_ACCESSKIT は Windows x64 の MSVC だけに対応しています。AccessKit なしでビルドします。")
	return()
endif()
if(NOT EXISTS "${_mitiru_ak_root}/include/accesskit.h" OR NOT EXISTS "${_mitiru_ak_dir}/accesskit.lib"
   OR NOT EXISTS "${_mitiru_ak_dir}/accesskit.dll")
	message(WARNING
		"MITIRU_WITH_ACCESSKIT=ON ですが ${_mitiru_ak_root} に accesskit.h / accesskit.lib / accesskit.dll が揃っていません。"
		"AccessKit なしでビルドします。\n"
		"  対処: python tools/fetch_accesskit.py を流してから configure し直してください。")
	return()
endif()

add_library(mitiru_accesskit_dll SHARED IMPORTED GLOBAL)
set_target_properties(mitiru_accesskit_dll PROPERTIES
	IMPORTED_LOCATION "${_mitiru_ak_dir}/accesskit.dll"
	IMPORTED_IMPLIB "${_mitiru_ak_dir}/accesskit.lib")

add_library(mitiru_accesskit INTERFACE)
target_include_directories(mitiru_accesskit INTERFACE "$<BUILD_INTERFACE:${_mitiru_ak_root}/include>")
target_link_libraries(mitiru_accesskit INTERFACE "${_mitiru_ak_dir}/accesskit.lib" delayimp
	bcrypt ntdll propsys runtimeobject uiautomationcore userenv ws2_32)
target_compile_definitions(mitiru_accesskit INTERFACE MITIRU_HAS_ACCESSKIT=1)
target_link_options(mitiru_accesskit INTERFACE
	"$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:/DELAYLOAD:accesskit.dll;/IGNORE:4199>")
target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} mitiru_accesskit)
message(STATUS "AccessKit enabled - UI tree for screen readers (accesskit.dll ships with the game)")
