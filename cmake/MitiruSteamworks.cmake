# Steamworks (実績・統計・Steam Cloud・リッチプレゼンス) は opt-in。SDK は Valve の利用規約の下にあり、
# リポジトリにも公開スナップショットにも入れない。partner.steamgames.com から利用者が取得して
# external/steamworks_sdk/ に置く (tools/fetch_steamworks.py が置き場所を確かめる)。
# 見つかれば mitiru_steamworks::steam_api を作り、mitiru に MITIRU_HAS_STEAMWORKS=1 を付ける。
# 無ければ include/mitiru/steam/SteamService.hpp は「Steam は無い」と答える実装のままビルドが通る。

# steam_api は共有ライブラリなので、exe の隣に置く (mitiru_deploy_steamaudio と同じ理由)。
function(mitiru_deploy_steamworks target)
	if(TARGET mitiru_steamworks_api)
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"$<TARGET_FILE:mitiru_steamworks_api>"
				"$<TARGET_FILE_DIR:${target}>"
			COMMENT "${target}: deploying Steamworks (steam_api)")
	endif()
endfunction()

option(MITIRU_WITH_STEAMWORKS "Steamworks SDK の実績・統計・Steam Cloud を使う (SDK は自分で external/steamworks_sdk に置く)" OFF)
if(NOT MITIRU_WITH_STEAMWORKS)
	return()
endif()
set(_mitiru_sw_root "${CMAKE_CURRENT_SOURCE_DIR}/external/steamworks_sdk")
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(_mitiru_sw_lib "${_mitiru_sw_root}/redistributable_bin/win64/steam_api64.lib")
	set(_mitiru_sw_dll "${_mitiru_sw_root}/redistributable_bin/win64/steam_api64.dll")
elseif(UNIX AND NOT APPLE AND CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(_mitiru_sw_lib "")
	set(_mitiru_sw_dll "${_mitiru_sw_root}/redistributable_bin/linux64/libsteam_api.so")
elseif(APPLE)
	set(_mitiru_sw_lib "")
	set(_mitiru_sw_dll "${_mitiru_sw_root}/redistributable_bin/osx/libsteam_api.dylib")
else()
	set(_mitiru_sw_dll "")
endif()

if(NOT EXISTS "${_mitiru_sw_root}/public/steam/steam_api.h" OR NOT _mitiru_sw_dll OR NOT EXISTS "${_mitiru_sw_dll}")
	message(WARNING
		"MITIRU_WITH_STEAMWORKS=ON ですが Steamworks SDK が見つかりません (${_mitiru_sw_root})。"
		"Steam の機能は無効のままビルドします。\n"
		"  対処: python tools/fetch_steamworks.py の案内どおり SDK を置いてから configure し直してください。")
	return()
endif()

add_library(mitiru_steamworks_api SHARED IMPORTED GLOBAL)
set_target_properties(mitiru_steamworks_api PROPERTIES
	IMPORTED_LOCATION "${_mitiru_sw_dll}"
	INTERFACE_INCLUDE_DIRECTORIES "${_mitiru_sw_root}/public")
if(_mitiru_sw_lib)
	set_target_properties(mitiru_steamworks_api PROPERTIES IMPORTED_IMPLIB "${_mitiru_sw_lib}")
endif()
add_library(mitiru_steamworks::steam_api ALIAS mitiru_steamworks_api)

target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} mitiru_steamworks::steam_api)
target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_STEAMWORKS=1)
message(STATUS "Steamworks SDK found - achievements / stats / Steam Cloud enabled (${_mitiru_sw_dll})")
