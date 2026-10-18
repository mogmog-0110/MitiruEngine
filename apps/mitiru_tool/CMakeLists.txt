# mitiru_tool — 独立ツール窓の汎用ホスト (RmlUi)。`--page <name>` で assets/<name>.rml を 1 枚だけ
# 小さな DX12 の窓に出す。ページの振る舞いは mitiru_tool_ui (テストからも使う) にある。
# RmlUi が無いビルド (MITIRU_WITH_RMLUI=OFF、Windows 以外) ではツール窓は作らない。

if(NOT TARGET mitiru_ui_rml)
	message(STATUS "mitiru_tool skipped: RmlUi (mitiru_ui_rml) is not built")
	return()
endif()

# エンジンの根。ゲームが add_subdirectory で取り込んだ時は CMAKE_SOURCE_DIR がゲーム側になるので使わない
get_filename_component(_mitiru_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

add_library(mitiru_tool_ui STATIC
	ui/HttpWorker.cpp
	ui/ImageDecode.cpp
	ui/SnapshotFormat.cpp
	ui/ToolSession.cpp
	ui/ToolUiHost.cpp
	ui/ToolWindow.cpp
	ui/pages/AiPage.cpp
	ui/pages/AnimGraphNames.cpp
	ui/pages/AnimPage.cpp
	ui/pages/FrameViewPage.cpp
	ui/pages/KvPages.cpp
	ui/pages/MeterPages.cpp
	ui/pages/NavMap.cpp
	ui/pages/NavPage.cpp
	ui/pages/PageFactory.cpp
	ui/pages/ReplayPage.cpp
	ui/pages/RewindPage.cpp
	ui/pages/SceneViewEditor.cpp
	ui/pages/SceneViewPage.cpp
	ui/pages/SceneViewPage_View.cpp
	ui/pages/SideStatePage.cpp
	ui/pages/StateMachineGraph.cpp
	ui/pages/StateMachinePage.cpp
	ui/pages/StoryPage.cpp
	ui/pages/TypedValues.cpp
	ui/motion_tune/MotionTunePage.cpp
	ui/motion_tune/MotionTuneRows.cpp
	ui/motion_tune/MotionTuneScript.cpp
	ui/motion_tune/TraceView.cpp
	ui/motion_tune/TuneModel.cpp
	ui/motion_tune/TuneSchema.cpp
	ui/pages/WhyViewPage.cpp
	ui/stage_edit/Canvas.cpp
	ui/stage_edit/EditSchema.cpp
	ui/stage_edit/Geometry.cpp
	ui/stage_edit/OrbitView.cpp
	ui/stage_edit/StageDoc.cpp
	ui/stage_edit/StageEditInput.cpp
	ui/stage_edit/StageEditPage.cpp
	ui/stage_edit/StageEditScript.cpp
	ui/stage_edit/StageFiles.cpp
	ui/stage_edit/TopView.cpp)
target_include_directories(mitiru_tool_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
	PRIVATE "${_mitiru_root}/external")
target_link_libraries(mitiru_tool_ui PUBLIC mitiru mitiru_ui_rml)
# nav ページが焼いたナビメッシュを読むため。Detour が無いビルドでは床を描かず、理由をページに出す。
if(TARGET mitiru_nav)
	target_link_libraries(mitiru_tool_ui PRIVATE mitiru_nav)
endif()
target_compile_definitions(mitiru_tool_ui PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
if(MSVC)
	target_compile_options(mitiru_tool_ui PRIVATE /utf-8 /bigobj /W4 /wd4100)
endif()

add_executable(mitiru_tool main.cpp Capture.cpp)

mitiru_use_utf8_codepage(mitiru_tool)
# ゲームが取り込んだ時は、遊んでいる間もこの exe を link し直せるようにする。add_custom_command(TARGET) は target を作った
# directory でしか呼べないので、ゲームの CMakeLists の終わりに走る一括の付与 (mitiru_relink_while_running_in_dir) に任せず、ここで付ける
get_property(_mitiru_top GLOBAL PROPERTY MITIRU_ENGINE_IS_TOP_LEVEL)
if(NOT _mitiru_top AND COMMAND mitiru_relink_while_running)
	mitiru_relink_while_running(mitiru_tool)
endif()
target_link_libraries(mitiru_tool PRIVATE mitiru_tool_ui)
target_compile_definitions(mitiru_tool PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
	target_compile_options(mitiru_tool PRIVATE /utf-8 /bigobj)
endif()
mitiru_deploy_sdl3(mitiru_tool)
if(COMMAND mitiru_deploy_dxc)
	mitiru_deploy_dxc(mitiru_tool)
endif()

# ページ (assets/*.rml / *.rcss) と、RmlUi が読む同梱の書体・既定の RCSS を exe の隣へ。
set(_tool_dir "$<TARGET_FILE_DIR:mitiru_tool>")
file(GLOB _tool_pages CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
file(GLOB _tool_engine_ui CONFIGURE_DEPENDS "${_mitiru_root}/assets/ui/*.rcss")
set(_tool_fonts)
foreach(_font "MPLUSRounded1c-Regular.ttf" "MPLUSRounded1c-Bold.ttf" "IBMPlexMono-Regular.ttf" "OFL.txt")
	if(EXISTS "${_mitiru_root}/assets/fonts/${_font}")
		list(APPEND _tool_fonts "${_mitiru_root}/assets/fonts/${_font}")
	endif()
endforeach()
set(_tool_stamp "${CMAKE_CURRENT_BINARY_DIR}/tool_assets.stamp")
add_custom_command(
	OUTPUT  "${_tool_stamp}"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${_tool_dir}/assets/ui" "${_tool_dir}/assets/fonts"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_tool_pages} "${_tool_dir}/assets"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_tool_engine_ui} "${_tool_dir}/assets/ui"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_tool_fonts} "${_tool_dir}/assets/fonts"
	COMMAND ${CMAKE_COMMAND} -E touch "${_tool_stamp}"
	DEPENDS ${_tool_pages} ${_tool_engine_ui} ${_tool_fonts}
	VERBATIM
	COMMENT "mitiru_tool: deploying pages, fonts and engine RCSS")
add_custom_target(mitiru_tool_assets ALL DEPENDS "${_tool_stamp}")
add_dependencies(mitiru_tool mitiru_tool_assets)
