# MitiruCefGameHelper.cmake — CEF ゲーム配置の POST_BUILD ヘルパー
#
# 各 CEF ゲームが
# CMakeLists.txt 末尾でやっていた「mitiru_runtime 同期 + CEF DLL/リソース配置 +
# MitiruCefHelper.exe コピー」の同一 ~30 行ブロックを 1 関数にまとめる。
#
# Usage (ゲーム側):
#   include(MitiruEngineConfig)
#   add_executable(my_game src/main.cpp)
#   target_link_libraries(my_game PRIVATE Mitiru::mitiru)
#   mitiru_register_cef_game(TARGET my_game)
#
# WEB_RUNTIME_DEST のデフォルトは ${CMAKE_CURRENT_SOURCE_DIR}/web/mitiru_runtime。
# assets/ui/shared/mitiru_runtime に置きたい場合は明示指定:
#   mitiru_register_cef_game(
#       TARGET my_game
#       WEB_RUNTIME_DEST "${CMAKE_CURRENT_SOURCE_DIR}/assets/ui/shared/mitiru_runtime")
#
# 動作:
#   1. mitiru_runtime ディレクトリを WEB_RUNTIME_DEST へ copy_directory
#   2. (Windows + CEF + MitiruCefHelper target あり時のみ)
#      MitiruCefHelper.exe / CEF_BINARY_DIR / CEF_RESOURCE_DIR を target exe dir へ配置
#
# 同一 target に対して二度呼ばれた場合は警告だけ出して no-op する (idempotent)。

if(COMMAND mitiru_register_cef_game)
	return()
endif()

# 既に登録済み target を覚えるためのグローバルプロパティ
define_property(GLOBAL PROPERTY MITIRU_CEF_REGISTERED_TARGETS
	BRIEF_DOCS "Targets already wired by mitiru_register_cef_game"
	FULL_DOCS  "Internal list used to make mitiru_register_cef_game idempotent")

function(mitiru_register_cef_game)
	set(options "")
	set(oneValueArgs TARGET WEB_RUNTIME_DEST)
	set(multiValueArgs "")
	cmake_parse_arguments(MRCG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

	if(NOT MRCG_TARGET)
		message(FATAL_ERROR "mitiru_register_cef_game: TARGET <name> is required")
	endif()
	if(NOT TARGET ${MRCG_TARGET})
		message(FATAL_ERROR
			"mitiru_register_cef_game: '${MRCG_TARGET}' is not a target "
			"(call after add_executable)")
	endif()

	# --- Idempotency check ------------------------------------------------
	get_property(_already GLOBAL PROPERTY MITIRU_CEF_REGISTERED_TARGETS)
	if(_already)
		list(FIND _already "${MRCG_TARGET}" _idx)
		if(NOT _idx EQUAL -1)
			message(WARNING
				"mitiru_register_cef_game: target '${MRCG_TARGET}' already registered, skipping")
			return()
		endif()
	endif()
	set_property(GLOBAL APPEND PROPERTY MITIRU_CEF_REGISTERED_TARGETS "${MRCG_TARGET}")

	# --- WEB_RUNTIME_DEST default ----------------------------------------
	if(NOT MRCG_WEB_RUNTIME_DEST)
		set(MRCG_WEB_RUNTIME_DEST "${CMAKE_CURRENT_SOURCE_DIR}/web/mitiru_runtime")
	endif()

	if(NOT MitiruEngine_WEB_RUNTIME_DIR)
		message(WARNING
			"mitiru_register_cef_game(${MRCG_TARGET}): MitiruEngine_WEB_RUNTIME_DIR is "
			"not set — did you include(MitiruEngineConfig) before calling? "
			"Skipping mitiru_runtime sync.")
	else()
		add_custom_command(TARGET ${MRCG_TARGET} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_directory
				"${MitiruEngine_WEB_RUNTIME_DIR}"
				"${MRCG_WEB_RUNTIME_DEST}"
			COMMENT "mitiru_register_cef_game: syncing mitiru_runtime/* into ${MRCG_WEB_RUNTIME_DEST}")
	endif()

	# --- CEF runtime placement (Windows only) ----------------------------
	if(WIN32 AND CEF_FOUND AND TARGET MitiruCefHelper)
		add_dependencies(${MRCG_TARGET} MitiruCefHelper)
		add_custom_command(TARGET ${MRCG_TARGET} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"$<TARGET_FILE:MitiruCefHelper>"
				"$<TARGET_FILE_DIR:${MRCG_TARGET}>/MitiruCefHelper.exe"
			COMMENT "mitiru_register_cef_game: copying MitiruCefHelper.exe (CEF subprocess)")
		add_custom_command(TARGET ${MRCG_TARGET} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_directory
				"${CEF_BINARY_DIR}"
				"$<TARGET_FILE_DIR:${MRCG_TARGET}>"
			COMMENT "mitiru_register_cef_game: copying CEF binary runtime")
		add_custom_command(TARGET ${MRCG_TARGET} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_directory
				"${CEF_RESOURCE_DIR}"
				"$<TARGET_FILE_DIR:${MRCG_TARGET}>"
			COMMENT "mitiru_register_cef_game: copying CEF resources (icudtl.dat, locales, ...)")
	endif()
endfunction()
