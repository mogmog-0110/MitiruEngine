# UI の層 (RmlUi、ADR 0051)。Windows では既定で組み込み、main window の HUD / メニューを
# エンジンの DX12 デバイスの上で同じフレームに描く。足すものは次のとおり:
#   mitiru_rmlui      … RmlUi 本体 + include/mitiru/ui_rml/ の状態 → data model の橋 (RmlUi のヘッダを使う側用)
#   mitiru_ui_rml     … エンジンが持つ UI 窓口 (RmlUiHost) と DX12 の RenderInterface (src/ui_rml/)
#   mitiru_rmlui_dx12 … RmlUi 同梱の DX12 backend。撮影道具 (apps/mitiru_rml_capture) だけが使う
# OFF のとき、または submodule が無いときは RmlUiHost が何もしない版になり、エンジンは UI 無しで動く。
# 文字のラスタライズは文字描画 (cmake/MitiruText.cmake) が組む FreeType をそのまま使う。

if(WIN32 AND NOT EMSCRIPTEN)
	set(_rml_default ON)
else()
	set(_rml_default OFF)
endif()
option(MITIRU_WITH_RMLUI "Build the RmlUi UI layer (RML/RCSS HUD and menus on the engine's DX12 device)" ${_rml_default})
if(NOT MITIRU_WITH_RMLUI)
	return()
endif()

set(_rml_root "${CMAKE_CURRENT_LIST_DIR}/../external/rmlui")
if(NOT EXISTS "${_rml_root}/CMakeLists.txt")
	message(WARNING
		"external/rmlui が空なので UI の層 (RmlUi) 無しでビルドする。\n"
		"  git submodule update --init external/rmlui を実行してから configure し直すと RML の HUD が使える。")
	return()
endif()
if(NOT TARGET freetype)
	message(FATAL_ERROR "MitiruRmlUi.cmake は MitiruText.cmake (FreeType) の後に読む")
endif()
# RmlUi は find_package(Freetype) の後で Freetype::Freetype target の有無だけを確認するので、
# 文字描画が先に用意した alias がそのまま使われ、FreeType は 1 回しかビルドされない。

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(_rml_saved_shared ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "" FORCE)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
add_subdirectory("${_rml_root}" "${CMAKE_BINARY_DIR}/external/rmlui" EXCLUDE_FROM_ALL)
set(BUILD_SHARED_LIBS ${_rml_saved_shared})
if(MSVC AND TARGET rmlui_core)
	target_compile_options(rmlui_core PRIVATE /W0 /FS)
endif()

add_library(mitiru_rmlui INTERFACE)
target_link_libraries(mitiru_rmlui INTERFACE RmlUi::RmlUi)
target_include_directories(mitiru_rmlui INTERFACE
	"${CMAKE_CURRENT_LIST_DIR}/../include"
	"${CMAKE_CURRENT_LIST_DIR}/../external")
target_compile_definitions(mitiru_rmlui INTERFACE MITIRU_HAS_RMLUI=1)
target_compile_features(mitiru_rmlui INTERFACE cxx_std_20)

if(NOT WIN32)
	return()
endif()

set(_ui_src "${CMAKE_CURRENT_LIST_DIR}/../src/ui_rml")
add_library(mitiru_ui_rml STATIC
	"${_ui_src}/RmlDx12Pipelines.cpp"
	"${_ui_src}/RmlDx12Targets.cpp"
	"${_ui_src}/RmlRenderInterfaceDx12.cpp"
	"${_ui_src}/RmlRenderInterfaceDx12_Effects.cpp"
	"${_ui_src}/RmlRuntime.cpp"
	"${_ui_src}/RmlUiHost.cpp")
target_link_libraries(mitiru_ui_rml PUBLIC mitiru_rmlui d3d12ma_impl stb_impl d3d12 dxgi d3dcompiler)
# 文言の訳 (core/Localization.hpp) が複数形の規則を sgc から引く
target_link_libraries(mitiru_ui_rml PRIVATE sgc::sgc)
target_compile_definitions(mitiru_ui_rml PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
	target_compile_options(mitiru_ui_rml PRIVATE /utf-8 /FS /W4 /wd4100)
endif()

set(_rml_be "${_rml_root}/Backends")
if(NOT EXISTS "${_rml_be}/RmlUi_Renderer_DX12.cpp")
	return()
endif()
add_library(mitiru_rmlui_dx12 STATIC EXCLUDE_FROM_ALL
	"${_rml_be}/RmlUi_Renderer_DX12.cpp"
	"${_rml_be}/RmlUi_DirectX/D3D12MemAlloc.cpp"
	"${_rml_be}/RmlUi_DirectX/offsetAllocator.cpp")
target_include_directories(mitiru_rmlui_dx12 PUBLIC "${_rml_be}")
target_link_libraries(mitiru_rmlui_dx12 PUBLIC RmlUi::RmlUi d3d12 dxgi dxguid d3dcompiler)
# backend は Windows.h の min/max マクロと UNICODE 版 API を前提に書かれている。
target_compile_definitions(mitiru_rmlui_dx12 PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
set(MITIRU_RMLUI_MSAA 8 CACHE STRING "MSAA sample count for the RmlUi DX12 backend used by mitiru_rml_capture")
target_compile_definitions(mitiru_rmlui_dx12 PUBLIC RMLUI_RENDER_BACKEND_FIELD_MSAA_SAMPLE_COUNT=${MITIRU_RMLUI_MSAA})
if(MSVC)
	target_compile_options(mitiru_rmlui_dx12 PRIVATE /utf-8 /W0)
endif()
