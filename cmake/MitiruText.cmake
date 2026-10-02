# 文字描画の基盤: FreeType (輪郭とラスタ) + HarfBuzz (字形選択と配置) + msdfgen /
# msdf-atlas-gen (多チャンネル距離場)。文字はすべてのゲームで使うので opt-in にはしない。
# 依存は最小構成にする: ICU / glib / cairo / zlib / bzip2 / png / brotli には依存しない。

set(_mt_ext "${CMAKE_CURRENT_SOURCE_DIR}/external")
foreach(_mt_dir harfbuzz freetype msdfgen msdf-atlas-gen)
	if(NOT EXISTS "${_mt_ext}/${_mt_dir}/CMakeLists.txt")
		message(FATAL_ERROR
			"external/${_mt_dir} が空です。git submodule update --init external/${_mt_dir} を実行してください")
	endif()
endforeach()

# 子プロジェクトの option() を上書きするため、通常変数を cache より優先させる。
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(_mt_saved_shared ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)

# HarfBuzz は FreeType より先に追加する (TARGET freetype があると hb-ft が有効になってしまう)。
set(HB_BUILD_SUBSET OFF)
set(HB_BUILD_RASTER OFF)
set(HB_BUILD_VECTOR OFF)
set(HB_BUILD_GPU OFF)
set(HB_BUILD_GPU_DEMO OFF CACHE STRING "" FORCE)
set(HB_BUILD_UTILS OFF)
set(HB_HAVE_FREETYPE OFF)
set(HB_HAVE_GLIB OFF)
set(HB_HAVE_ICU OFF)
set(HB_HAVE_CAIRO OFF)
set(HB_HAVE_GOBJECT OFF)
set(HB_HAVE_INTROSPECTION OFF)
add_subdirectory(${_mt_ext}/harfbuzz ${CMAKE_BINARY_DIR}/external/harfbuzz EXCLUDE_FROM_ALL)

set(FT_DISABLE_ZLIB ON)
set(FT_DISABLE_BZIP2 ON)
set(FT_DISABLE_PNG ON)
set(FT_DISABLE_HARFBUZZ ON)
set(FT_DISABLE_BROTLI ON)
add_subdirectory(${_mt_ext}/freetype ${CMAKE_BINARY_DIR}/external/freetype EXCLUDE_FROM_ALL)
if(NOT TARGET Freetype::Freetype)
	add_library(Freetype::Freetype ALIAS freetype)
endif()

set(MSDFGEN_CORE_ONLY OFF)
set(MSDFGEN_BUILD_STANDALONE OFF)
set(MSDFGEN_USE_VCPKG OFF)
set(MSDFGEN_USE_SKIA OFF)
set(MSDFGEN_USE_OPENMP OFF)
set(MSDFGEN_DISABLE_SVG ON)
set(MSDFGEN_DISABLE_PNG ON)
set(MSDFGEN_INSTALL OFF)
set(MSDFGEN_DYNAMIC_RUNTIME ON)
add_subdirectory(${_mt_ext}/msdfgen ${CMAKE_BINARY_DIR}/external/msdfgen EXCLUDE_FROM_ALL)

set(BUILD_SHARED_LIBS ${_mt_saved_shared})

# msdf-atlas-gen は本体の CMake (vcpkg / artery-font / standalone 前提) を使わず、
# 字形 1 個の輪郭の読み込みと枠の計算 (GlyphGeometry) だけを取り込む。
set(_mt_atlas "${_mt_ext}/msdf-atlas-gen/msdf-atlas-gen")
add_library(mitiru_text STATIC
	${_mt_atlas}/GlyphGeometry.cpp
	${_mt_atlas}/Padding.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/text/FontFace.cpp
)
target_include_directories(mitiru_text
	PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	PRIVATE ${_mt_atlas} ${_mt_ext}/msdfgen
)
target_link_libraries(mitiru_text PRIVATE harfbuzz freetype msdfgen::msdfgen-core msdfgen::msdfgen-ext)
target_compile_features(mitiru_text PUBLIC cxx_std_20)
if(MSVC)
	target_compile_options(mitiru_text PRIVATE /FS /utf-8)
	foreach(_mt_t harfbuzz freetype msdfgen-core msdfgen-ext)
		if(TARGET ${_mt_t})
			target_compile_options(${_mt_t} PRIVATE /W0 /FS)
		endif()
	endforeach()
endif()
