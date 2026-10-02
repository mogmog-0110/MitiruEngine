# Steam Audio (HRTF) は opt-in。SDK は 170MB を超えるので同梱せず、
# tools/fetch_steamaudio.py によって external/steamaudio/ に置かれたものを参照する。
# 見つかれば mitiru_steamaudio::phonon を作り、mitiru に MITIRU_HAS_STEAMAUDIO=1 を付ける。
# 無ければパン (PanSpatialRenderer) のまま。ON にしても使えない場合は警告で知らせる。

# phonon は共有ライブラリなので、mitiru を link した exe と同じ場所に置く (mitiru_deploy_sdl2 と同じ理由)。
# Steam Audio を使わない構成では何もしない。
function(mitiru_deploy_steamaudio target)
	if(TARGET mitiru_steamaudio_phonon)
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"$<TARGET_FILE:mitiru_steamaudio_phonon>"
				"$<TARGET_FILE_DIR:${target}>"
			COMMENT "${target}: deploying Steam Audio (phonon)")
	endif()
endfunction()

option(MITIRU_WITH_STEAMAUDIO "Steam Audio の HRTF で 3D 音を両耳に畳み込む (tools/fetch_steamaudio.py が要る)" OFF)
if(NOT MITIRU_WITH_STEAMAUDIO)
	return()
endif()
set(_mitiru_sa_root "${CMAKE_CURRENT_SOURCE_DIR}/external/steamaudio")
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(_mitiru_sa_lib "${_mitiru_sa_root}/lib/windows-x64/phonon.lib")
	set(_mitiru_sa_dll "${_mitiru_sa_root}/lib/windows-x64/phonon.dll")
elseif(UNIX AND NOT APPLE AND CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(_mitiru_sa_lib "")
	set(_mitiru_sa_dll "${_mitiru_sa_root}/lib/linux-x64/libphonon.so")
else()
	set(_mitiru_sa_dll "")
endif()

if(NOT EXISTS "${_mitiru_sa_root}/include/phonon.h" OR NOT _mitiru_sa_dll OR NOT EXISTS "${_mitiru_sa_dll}")
	message(WARNING
		"MITIRU_WITH_STEAMAUDIO=ON ですが Steam Audio SDK が見つかりません (${_mitiru_sa_root})。"
		"HRTF は無効で、3D 音はパンで鳴ります。\n"
		"  対処: python tools/fetch_steamaudio.py を実行してから configure し直してください。")
	return()
endif()

add_library(mitiru_steamaudio_phonon SHARED IMPORTED GLOBAL)
set_target_properties(mitiru_steamaudio_phonon PROPERTIES
	IMPORTED_LOCATION "${_mitiru_sa_dll}"
	INTERFACE_INCLUDE_DIRECTORIES "${_mitiru_sa_root}/include")
if(_mitiru_sa_lib)
	set_target_properties(mitiru_steamaudio_phonon PROPERTIES IMPORTED_IMPLIB "${_mitiru_sa_lib}")
endif()
add_library(mitiru_steamaudio::phonon ALIAS mitiru_steamaudio_phonon)

target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} mitiru_steamaudio::phonon)
target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_STEAMAUDIO=1)
message(STATUS "Steam Audio found - HRTF spatial renderer enabled (${_mitiru_sa_dll})")
