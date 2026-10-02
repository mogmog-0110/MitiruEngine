# SDL3 (zlib) はパッドの唯一の経路 (ADR 0048 段階 2)。Xbox / DualShock 4 / DualSense / Switch Pro を同じ形で読む。
# 見つからないまま configure が通るとパッドが 1 台も効かない host ができるので、見つからなければ必ず警告する。
#
# Windows: external/sdl3/SDL3-<版>/ (公式の VC 開発パッケージ) を使う。無ければ MITIRU_FETCH_SDL3 (既定 ON) で
#          configure 中に取りに行き、SHA-256 を照合して展開する。tools/fetch_sdl3.py も同じ版を取る。
# 他 OS:   システムの SDL3 (find_package) を使う。
#
# 結果: MITIRU_HAS_SDL3=1 と SDL3 の link を mitiru に付け、SDL3_FOUND を立てる。

set(MITIRU_SDL3_VERSION "3.4.16")
set(MITIRU_SDL3_VC_SHA256 "1a784cb2a5c64d56fe7a62090fe9d242d9865f235e4ea9678f1a6ba4e693e7de")
set(MITIRU_SDL3_ROOT "${CMAKE_CURRENT_LIST_DIR}/../external/sdl3")
option(MITIRU_FETCH_SDL3 "SDL3 が無ければ configure 中に公式の VC 開発パッケージを取りに行く (Windows)" ON)

function(_mitiru_sdl3_download)
	set(_name "SDL3-devel-${MITIRU_SDL3_VERSION}-VC.zip")
	set(_url "https://github.com/libsdl-org/SDL/releases/download/release-${MITIRU_SDL3_VERSION}/${_name}")
	set(_zip "${CMAKE_BINARY_DIR}/_downloads/${_name}")
	message(STATUS "SDL3: downloading ${_url}")
	file(DOWNLOAD "${_url}" "${_zip}" EXPECTED_HASH SHA256=${MITIRU_SDL3_VC_SHA256} TLS_VERIFY ON STATUS _st)
	list(GET _st 0 _code)
	if(NOT _code EQUAL 0)
		list(GET _st 1 _msg)
		message(WARNING "SDL3 を取れなかった (${_msg})。オフラインなら python tools/fetch_sdl3.py --from <展開済み SDL3> を使う。")
		return()
	endif()
	file(ARCHIVE_EXTRACT INPUT "${_zip}" DESTINATION "${MITIRU_SDL3_ROOT}")
endfunction()

if(WIN32 AND NOT SDL3_DIR)
	set(_mitiru_sdl3_cmake "${MITIRU_SDL3_ROOT}/SDL3-${MITIRU_SDL3_VERSION}/cmake")
	if(NOT EXISTS "${_mitiru_sdl3_cmake}/SDL3Config.cmake" AND MITIRU_FETCH_SDL3)
		_mitiru_sdl3_download()
	endif()
	if(EXISTS "${_mitiru_sdl3_cmake}/SDL3Config.cmake")
		set(SDL3_DIR "${_mitiru_sdl3_cmake}")
	endif()
endif()

find_package(SDL3 3.2 CONFIG QUIET)
if(SDL3_FOUND AND TARGET SDL3::SDL3)
	message(STATUS "SDL3 found (${SDL3_VERSION}) - gamepads via SDL3 (Xbox / DualShock 4 / DualSense / Switch Pro)")
	# imported target はディレクトリスコープ。エンジンを add_subdirectory で取り込む consumer の CMakeLists からも
	# 見えないと、SDL3.dll の deploy が飛ばされて host が起動できない。SDL3::SDL3 は ALIAS で global にできないので、
	# 実体の SDL3::SDL3-shared を global にして、link もそちらで行う
	foreach(_t SDL3::SDL3-shared SDL3::Headers)
		if(TARGET ${_t})
			set_target_properties(${_t} PROPERTIES IMPORTED_GLOBAL TRUE)
		endif()
	endforeach()
	if(TARGET SDL3::SDL3-shared)
		set(_mitiru_sdl3_lib SDL3::SDL3-shared)
	else()
		set(_mitiru_sdl3_lib SDL3::SDL3)
	endif()
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_SDL3=1)
	target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} ${_mitiru_sdl3_lib})
	if(MSVC)
		# exe は SDL3.dll を遅延読み込みにする。DLL を配り忘れても 0xC0000135 で起動前に落ちず、
		# SdlGamepadInput::init が警告してパッドだけを止める。SDL を呼ばない exe の LNK4199 は黙らせる
		target_link_options(mitiru ${MITIRU_TARGET_SCOPE}
			"$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:/DELAYLOAD:SDL3.dll;/IGNORE:4199>")
		target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} delayimp)
	endif()
else()
	set(SDL3_FOUND FALSE)
	message(WARNING
		"SDL3 が見つからない。パッドが 1 台も効かない host になる (Xbox も DualShock 4 も)。\n"
		"  対処: python tools/fetch_sdl3.py を実行して configure し直す"
		" (Windows はネットにつながっていれば -DMITIRU_FETCH_SDL3=ON の configure でも取れる)。")
endif()

# SDL3 は DLL で配るので、mitiru を link した exe は起動時に SDL3.dll が要る (無いと 0xC0000135 で起動前に落ちる)。
# PATH に頼らず exe の隣に置く。POST_BUILD は追加順に走るので、catch_discover_tests より先に呼べばテストの列挙に間に合う。
function(mitiru_deploy_sdl3 target)
	if(WIN32 AND TARGET SDL3::SDL3-shared)
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"$<TARGET_FILE:SDL3::SDL3-shared>"
				"$<TARGET_FILE_DIR:${target}>/SDL3.dll"
			COMMENT "${target}: deploying SDL3.dll")
	endif()
endfunction()
