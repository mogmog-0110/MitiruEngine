# WebGPU (Google の Dawn) の描画の層 (ADR 0087 段階 1)。既定は OFF。
# Dawn は公式のビルド済み (github.com/google/dawn の Releases) をそのまま参照する。大きな物 (展開後 897MB) は
# リポジトリに入れず、MITIRU_DAWN_DIR (または同名の環境変数) でその展開先を指す。固定した版は cmake/dawn_version.cmake。
#
# 足すもの:
#   mitiru_dawn … Dawn を静的リンクした描画の層 (src/dawn/)。mitiru に MITIRU_HAS_DAWN=1 を伝える
#
# Dawn の静的ライブラリは Release の CRT (/MD、_ITERATOR_DEBUG_LEVEL=0) で作られているので、Debug 構成では
# リンクできない。RelWithDebInfo か Release で使う。配る DLL は無い (Dawn は exe に入る)。D3D11 / D3D12 の
# シェーダーは OS の d3dcompiler_47.dll (FXC) で作る。D3D12 で DXC を使うときだけ、exe の隣の dxcompiler.dll と
# dxil.dll を実行時に読む (MITIRU_WITH_DXC=ON の配置がそのまま使える)。

option(MITIRU_WITH_DAWN "Build the WebGPU (Dawn) rendering layer from a prebuilt Dawn (ADR 0087 stage 1)" OFF)
option(MITIRU_BUILD_DAWN_TESTS "Build tests/dawn (needs MITIRU_WITH_DAWN; uses the GPU)" OFF)
if(NOT MITIRU_WITH_DAWN)
	return()
endif()
if(NOT WIN32)
	message(FATAL_ERROR "MITIRU_WITH_DAWN は今のところ Windows だけ (Linux / macOS は ADR 0087 段階 3)")
endif()

include(${CMAKE_CURRENT_LIST_DIR}/dawn_version.cmake)

set(MITIRU_DAWN_DIR "" CACHE PATH "Dawn-<コミット>-windows-latest-Release の展開先 (include と lib がある所)")
if(NOT MITIRU_DAWN_DIR AND DEFINED ENV{MITIRU_DAWN_DIR})
	# cache へ残す。環境変数のままだと、Ninja が CMakeLists.txt の更新で configure をやり直すとき、変数の無いシェルで落ちる
	set(MITIRU_DAWN_DIR "$ENV{MITIRU_DAWN_DIR}" CACHE PATH "Dawn-<コミット>-windows-latest-Release の展開先 (include と lib がある所)" FORCE)
endif()
if(NOT EXISTS "${MITIRU_DAWN_DIR}/include/webgpu/webgpu_cpp.h" OR NOT EXISTS "${MITIRU_DAWN_DIR}/lib/webgpu_dawn.lib")
	message(FATAL_ERROR
		"MITIRU_WITH_DAWN=ON には Dawn のビルド済みが要る。\n"
		"  ${MITIRU_DAWN_RELEASE_URL}\n"
		"  SHA-256 ${MITIRU_DAWN_ARCHIVE_SHA256}\n"
		"を展開して -DMITIRU_DAWN_DIR=<展開先> を渡す (今の値: '${MITIRU_DAWN_DIR}')")
endif()
if(NOT MITIRU_DAWN_DIR MATCHES "${MITIRU_DAWN_COMMIT}")
	message(WARNING "MITIRU_DAWN_DIR が固定した版 (${MITIRU_DAWN_COMMIT}) の名前を含まない。別の版だと API が合わないことがある")
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	message(WARNING "Dawn のビルド済みは Release の CRT なので、Debug 構成ではリンクで _ITERATOR_DEBUG_LEVEL の食い違いが出る。RelWithDebInfo を使う")
endif()

# Slang が出した WGSL を、C++ の文字列として取り込む。MSVC の文字列定数は 1 つ 16KB までなので、
# 8000 byte ずつの raw 文字列に分けて並べる。ファイルが変わったら configure し直す。
set(_dawn_shader_dir "${CMAKE_CURRENT_LIST_DIR}/../shaders/dawn/generated")
set(_dawn_gen_dir "${CMAKE_BINARY_DIR}/mitiru_dawn_generated")
file(MAKE_DIRECTORY "${_dawn_gen_dir}")
file(GLOB _dawn_wgsl_files "${_dawn_shader_dir}/*.wgsl")
set(_dawn_shader_header "${_dawn_gen_dir}/DawnShaderSources.inc")
set(_dawn_shader_text "// cmake/MitiruDawn.cmake が shaders/dawn/generated/*.wgsl から作る。手で書き換えない\n")
foreach(_wgsl IN LISTS _dawn_wgsl_files)
	get_filename_component(_name "${_wgsl}" NAME_WE)
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_wgsl}")
	file(READ "${_wgsl}" _src)
	string(LENGTH "${_src}" _len)
	string(APPEND _dawn_shader_text "inline const std::string& dawnShaderSource_${_name}()\n{\n\tstatic const std::string s = std::string()\n")
	set(_pos 0)
	while(_pos LESS _len)
		math(EXPR _remain "${_len} - ${_pos}")
		if(_remain GREATER 8000)
			set(_take 8000)
		else()
			set(_take ${_remain})
		endif()
		string(SUBSTRING "${_src}" ${_pos} ${_take} _chunk)
		string(APPEND _dawn_shader_text "\t\t+ std::string(R\"mtr(${_chunk})mtr\")\n")
		math(EXPR _pos "${_pos} + ${_take}")
	endwhile()
	string(APPEND _dawn_shader_text "\t;\n\treturn s;\n}\n")
endforeach()
# 中身が変わらないときは書かない (書くと更新時刻が進み、Dawn のファイルが毎回組み直される)
set(_dawn_shader_old "")
if(EXISTS "${_dawn_shader_header}")
	file(READ "${_dawn_shader_header}" _dawn_shader_old)
endif()
if(NOT _dawn_shader_old STREQUAL _dawn_shader_text)
	file(WRITE "${_dawn_shader_header}" "${_dawn_shader_text}")
endif()

set(_dawn_src "${CMAKE_CURRENT_LIST_DIR}/../src/dawn")
add_library(mitiru_dawn STATIC
	"${_dawn_src}/DawnContext.cpp"
	"${_dawn_src}/DawnDevice.cpp"
	"${_dawn_src}/Renderer3D_Dawn.cpp"
	"${_dawn_src}/Renderer3D_DawnFx.cpp"
	"${_dawn_src}/Renderer3D_DawnTables.cpp"
	"${_dawn_src}/Renderer3D_DawnViews.cpp"
	"${_dawn_src}/DawnViewSlots.cpp"
	"${_dawn_src}/DawnBatch2D.cpp"
	"${_dawn_src}/DawnShadowPass.cpp"
	"${_dawn_src}/DawnShadowSettings.cpp"
	"${_dawn_src}/DawnLinePass.cpp"
	"${_dawn_src}/DawnBloomPass.cpp"
	"${_dawn_src}/DawnScenePipelines.cpp"
	"${_dawn_src}/DawnSkyPass.cpp"
	"${_dawn_src}/DawnParticles.cpp"
	"${_dawn_src}/DawnLightingBake.cpp"
	"${_dawn_src}/DawnFxaaPass.cpp"
	"${_dawn_src}/DawnImpactPass.cpp"
	"${_dawn_src}/DawnModelStore.cpp"
	"${_dawn_src}/DawnSkinning.cpp"
	"${_dawn_src}/DawnRmlCore.cpp"
	"${_dawn_src}/DawnRmlEffects.cpp"
	"${_dawn_src}/DawnVfxTextures.cpp"
	"${_dawn_src}/DawnDecals.cpp"
	"${_dawn_src}/DawnMaterialMaps.cpp"
	"${_dawn_src}/DawnLocalLights.cpp")
# DawnShaderSources.inc を include するファイルは、Ninja の include の追跡では作り直しが決まらないことがあり、
# シェーダーだけ直しても古い物が実行された。明示の依存にして、シェーダーの変更で必ず組み直す
get_target_property(_dawn_srcs mitiru_dawn SOURCES)
set_property(SOURCE ${_dawn_srcs} APPEND PROPERTY OBJECT_DEPENDS "${_dawn_shader_header}")
target_include_directories(mitiru_dawn
	PRIVATE "${_dawn_gen_dir}" "${MITIRU_DAWN_DIR}/include" "${_dawn_src}"
	PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../include" "${CMAKE_CURRENT_LIST_DIR}/../external"
	       "${CMAKE_CURRENT_LIST_DIR}/../external/stb" "${CMAKE_CURRENT_LIST_DIR}/../external/cgltf"
	       "${CMAKE_CURRENT_LIST_DIR}/../external/vendored")
# Dawn のビルド済みは MSVC 14.51 の STL を参照する。それより古い STL に無い入口を、小さな lib で埋める。
# 入口ごとに有る無しが違うので (14.50 は rotate と unique だけ持つ)、StlShims.cpp は /alternatename で本物が無いときだけ使われる形にし、
# lib の中身を取り込ませるため /INCLUDE で anchor を要求する
set(_dawn_link "${MITIRU_DAWN_DIR}/lib/webgpu_dawn.lib")
if(MSVC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.51)
	add_library(mitiru_dawn_stlshim STATIC "${_dawn_src}/StlShims.cpp")
	target_compile_options(mitiru_dawn_stlshim PRIVATE /utf-8 /FS)
	target_link_options(mitiru_dawn_stlshim INTERFACE "/INCLUDE:mitiru_stl_shim_anchor")
	list(APPEND _dawn_link mitiru_dawn_stlshim)
endif()
target_link_libraries(mitiru_dawn PUBLIC
	${_dawn_link}
	dxgi d3d11 d3d12 dxguid onecore_apiset user32 gdi32 ole32 advapi32 shell32 version dbghelp)
target_link_libraries(mitiru_dawn PRIVATE sgc::sgc stb_impl meshopt_impl)   # スキンの LOD (SkinnedLod.hpp) が meshoptimizer を使う
if(TARGET mitiru_rmlui)
	target_link_libraries(mitiru_dawn PUBLIC mitiru_rmlui)
	target_compile_definitions(mitiru_dawn PRIVATE MITIRU_HAS_RMLUI=1)
endif()
target_compile_definitions(mitiru_dawn PUBLIC MITIRU_HAS_DAWN=1)
target_compile_definitions(mitiru_dawn PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_features(mitiru_dawn PUBLIC cxx_std_20)
if(MSVC)
	target_compile_options(mitiru_dawn PRIVATE /utf-8 /FS /W4 /wd4100)
endif()
# RmlUiHost (mitiru_ui_rml) が RmlRenderInterfaceDawn::create を呼ぶ。依存の向きは ui_rml -> dawn だけ
if(TARGET mitiru_ui_rml)
	target_link_libraries(mitiru_ui_rml PUBLIC mitiru_dawn)
endif()
message(STATUS "Dawn: ${MITIRU_DAWN_DIR}")
