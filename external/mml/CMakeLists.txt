cmake_minimum_required(VERSION 3.21)
project(MitiruMML VERSION 0.1.0 DESCRIPTION "MML music engine for game development" LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
	message(FATAL_ERROR "In-source builds not allowed")
endif()

# ymfm — YM2608 (OPNA)、YM2151 (OPM)、OPL 系のエミュレーション (BSD-3-Clause)
add_library(ymfm STATIC
	external/ymfm/src/ymfm_adpcm.cpp
	external/ymfm/src/ymfm_misc.cpp
	external/ymfm/src/ymfm_opl.cpp
	external/ymfm/src/ymfm_opm.cpp
	external/ymfm/src/ymfm_opn.cpp
	external/ymfm/src/ymfm_pcm.cpp
	external/ymfm/src/ymfm_ssg.cpp
)
target_include_directories(ymfm PUBLIC external/ymfm/src)
target_compile_features(ymfm PUBLIC cxx_std_17)
if(MSVC)
	target_compile_options(ymfm PRIVATE /W0 /FS)
endif()

# emu76489 — SN76489 (MIT)。.c を C++ としてコンパイルする (親のプロジェクトに C 言語を要求しないため)
add_library(emu76489 STATIC external/emu76489/emu76489.c)
set_source_files_properties(external/emu76489/emu76489.c PROPERTIES LANGUAGE CXX)
target_include_directories(emu76489 SYSTEM PUBLIC external/emu76489)
target_compile_features(emu76489 PUBLIC cxx_std_17)
if(MSVC)
	target_compile_options(emu76489 PRIVATE /W0 /FS)
endif()

# vgsound_emu の NES APU (zlib License)。nes.hpp と nes.cpp は 2 か所を直して先頭に明記してある
add_library(vgsound_emu_nes STATIC external/vgsound_emu/src/nes/nes.cpp)
target_include_directories(vgsound_emu_nes SYSTEM PUBLIC external)
target_compile_features(vgsound_emu_nes PUBLIC cxx_std_17)
if(MSVC)
	target_compile_options(vgsound_emu_nes PRIVATE /W0 /FS)
endif()

add_library(mitiru_mml INTERFACE)
add_library(mitiru_mml::mitiru_mml ALIAS mitiru_mml)
target_include_directories(mitiru_mml INTERFACE
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:include>
)
target_compile_features(mitiru_mml INTERFACE cxx_std_20)
target_link_libraries(mitiru_mml INTERFACE ymfm emu76489 vgsound_emu_nes)
# BOM の無い UTF-8 のヘッダー (日本語のコメントを含む) を、既定のコードページ (日本語 Windows なら 932) で読まないため
if(MSVC)
	target_compile_options(mitiru_mml INTERFACE /utf-8)
endif()

# MSVC parallel compile PDB lock fix
if(MSVC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /FS")
endif()

if(PROJECT_IS_TOP_LEVEL)
	option(MITIRU_MML_BUILD_TESTS "Build tests" ON)
else()
	option(MITIRU_MML_BUILD_TESTS "Build tests" OFF)
endif()

if(MITIRU_MML_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()
