cmake_minimum_required(VERSION 3.21)
project(MitiruMML VERSION 0.1.0 DESCRIPTION "MML music engine for game development" LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
	message(FATAL_ERROR "In-source builds not allowed")
endif()

# ymfm — YM2608 (OPNA) エミュレーション (BSD-3-Clause)
add_library(ymfm STATIC
	external/ymfm/src/ymfm_adpcm.cpp
	external/ymfm/src/ymfm_misc.cpp
	external/ymfm/src/ymfm_opn.cpp
	external/ymfm/src/ymfm_ssg.cpp
)
target_include_directories(ymfm PUBLIC external/ymfm/src)
target_compile_features(ymfm PUBLIC cxx_std_17)
if(MSVC)
	target_compile_options(ymfm PRIVATE /W0 /FS)
endif()

add_library(mitiru_mml INTERFACE)
add_library(mitiru_mml::mitiru_mml ALIAS mitiru_mml)
target_include_directories(mitiru_mml INTERFACE
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:include>
)
target_compile_features(mitiru_mml INTERFACE cxx_std_20)
target_link_libraries(mitiru_mml INTERFACE ymfm)

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
