# 引数と使い方は docs/adr/ の web 配布の ADR 案にある
if(NOT EMSCRIPTEN)
	message(FATAL_ERROR "MitiruWebGame.cmake は Emscripten のツールチェーンで configure したときだけ使えます (cmake --preset emscripten)")
endif()

set(_mitiru_web_dir "${CMAKE_CURRENT_LIST_DIR}/../web")

function(mitiru_add_web_game TARGET_NAME)
	cmake_parse_arguments(ARG "SINGLE_FILE" "TITLE;WIDTH;HEIGHT;AUDIO_DIR;ASSETS;OUTPUT_NAME" "MAIN" ${ARGN})
	if(NOT ARG_MAIN)
		message(FATAL_ERROR "mitiru_add_web_game(${TARGET_NAME}): MAIN にゲームの .cpp を渡してください")
	endif()
	if(NOT DEFINED ARG_TITLE)
		set(ARG_TITLE "${TARGET_NAME}")
	endif()
	if(NOT DEFINED ARG_WIDTH)
		set(ARG_WIDTH 1280)
	endif()
	if(NOT DEFINED ARG_HEIGHT)
		set(ARG_HEIGHT 720)
	endif()
	if(NOT DEFINED ARG_AUDIO_DIR)
		set(ARG_AUDIO_DIR "assets/audio")
	endif()
	if(NOT DEFINED ARG_OUTPUT_NAME)
		set(ARG_OUTPUT_NAME "index")
	endif()

	set(MITIRU_WEB_TITLE "${ARG_TITLE}")
	set(MITIRU_WEB_WIDTH "${ARG_WIDTH}")
	set(MITIRU_WEB_HEIGHT "${ARG_HEIGHT}")
	set(MITIRU_WEB_AUDIO_DIR "${ARG_AUDIO_DIR}")
	set(_gen "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_web")
	configure_file("${_mitiru_web_dir}/mitiru_web_config.hpp.in" "${_gen}/mitiru_web_config.hpp" @ONLY)
	configure_file("${_mitiru_web_dir}/dist_shell.html" "${_gen}/shell.html" @ONLY)

	add_executable(${TARGET_NAME} ${ARG_MAIN} "${_mitiru_web_dir}/web_main.cpp")
	target_include_directories(${TARGET_NAME} PRIVATE "${_gen}")
	target_link_libraries(${TARGET_NAME} PRIVATE mitiru)
	set_target_properties(${TARGET_NAME} PROPERTIES
		OUTPUT_NAME "${ARG_OUTPUT_NAME}"
		SUFFIX ".html"
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}")

	# 出荷済みのブラウザ版 (oscar-rythm) と同じ値
	target_link_options(${TARGET_NAME} PRIVATE
		"-sSTACK_SIZE=2097152"
		"-sEXIT_RUNTIME=0"
		"SHELL:--shell-file ${_gen}/shell.html")

	if(ARG_ASSETS)
		get_filename_component(_assets_abs "${ARG_ASSETS}" ABSOLUTE)
		if(ARG_SINGLE_FILE)
			target_link_options(${TARGET_NAME} PRIVATE "SHELL:--embed-file ${_assets_abs}@/assets")
		else()
			target_link_options(${TARGET_NAME} PRIVATE "SHELL:--preload-file ${_assets_abs}@/assets")
		endif()
		set_property(TARGET ${TARGET_NAME} APPEND PROPERTY LINK_DEPENDS "${_assets_abs}")
	endif()
	if(ARG_SINGLE_FILE)
		target_link_options(${TARGET_NAME} PRIVATE "-sSINGLE_FILE=1")
	endif()
endfunction()
