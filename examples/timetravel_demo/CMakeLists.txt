# timetravel_demo — タイムトラベル inspector の模範サンプル (ADR 0017)
#
# flat POD な GameMemory + MITIRU_GAME_SERIES の probe 宣言だけで、host が過去フレームを
# 自動記録し inspector の time-travel graph に HP 履歴を出す。graph click で巻き戻る。
#
# 実行:  mitiru_host.exe timetravel_demo/timetravel_demo.dll --inspect timetravel

add_library(timetravel_demo SHARED timetravel_demo_dll.cpp)
target_link_libraries(timetravel_demo PRIVATE mitiru)

if(MSVC)
	target_compile_options(timetravel_demo PRIVATE /bigobj)
endif()

if(TARGET mitiru_host)
	set(_tt_runtime_dir "$<TARGET_FILE_DIR:mitiru_host>/timetravel_demo")

	set_target_properties(timetravel_demo PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${_tt_runtime_dir}"
		LIBRARY_OUTPUT_DIRECTORY "${_tt_runtime_dir}")

	# assets/ を source-tracked stamp 経由で deploy。
	file(GLOB_RECURSE _tt_assets CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
	set(_tt_stamp "${CMAKE_CURRENT_BINARY_DIR}/timetravel_demo_assets.stamp")
	add_custom_command(
		OUTPUT  "${_tt_stamp}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_CURRENT_SOURCE_DIR}/assets"
			"${_tt_runtime_dir}/assets"
		COMMAND ${CMAKE_COMMAND} -E touch "${_tt_stamp}"
		DEPENDS ${_tt_assets}
		VERBATIM
		COMMENT "timetravel_demo: deploying assets/ (source-tracked)")
	add_custom_target(timetravel_demo_assets ALL DEPENDS "${_tt_stamp}")
	add_dependencies(timetravel_demo timetravel_demo_assets)

	add_dependencies(timetravel_demo mitiru_host)
endif()
