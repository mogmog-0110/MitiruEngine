# beko_run — 赤べこの横スクロールアクション（flat-POD ゲーム）。
# site/homepage/tutorial.html の題材。カリキュラム章ではないので独立 CMakeLists を持つ。
# ソースは複数ファイルに分ける: 入口 + ゲーム本体。
# 実行: mitiru_host.exe beko_run/beko_run.dll

add_library(beko_run SHARED
	beko_run_dll.cpp   # DLL の入口 (MITIRU_GAME)
	game.cpp)          # ゲーム本体 (update / draw)
target_link_libraries(beko_run PRIVATE mitiru)

if(MSVC)
	target_compile_options(beko_run PRIVATE /bigobj)
endif()

if(TARGET mitiru_host)
	set(_beko_runtime_dir "$<TARGET_FILE_DIR:mitiru_host>/beko_run")
	set_target_properties(beko_run PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${_beko_runtime_dir}"
		LIBRARY_OUTPUT_DIRECTORY "${_beko_runtime_dir}")

	# assets/ (画像と HUD の assets/ui/main.rml) を DLL の隣へ deploy (source-tracked stamp)。
	file(GLOB_RECURSE _beko_assets CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
	set(_beko_stamp "${CMAKE_CURRENT_BINARY_DIR}/beko_run_assets.stamp")
	add_custom_command(
		OUTPUT  "${_beko_stamp}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_CURRENT_SOURCE_DIR}/assets"
			"${_beko_runtime_dir}/assets"
		COMMAND ${CMAKE_COMMAND} -E touch "${_beko_stamp}"
		DEPENDS ${_beko_assets}
		VERBATIM
		COMMENT "beko_run: deploying assets/ (source-tracked)")
	add_custom_target(beko_run_assets ALL DEPENDS "${_beko_stamp}")
	add_dependencies(beko_run beko_run_assets)

	add_dependencies(beko_run mitiru_host)
endif()
