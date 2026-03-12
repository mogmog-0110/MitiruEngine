cmake_minimum_required(VERSION 3.21)

project(MitiruEngine
	VERSION 0.1.0
	DESCRIPTION "AI-first C++20 game engine built on ShiggyGameCore"
	LANGUAGES CXX
)

# Prevent in-source builds
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
	message(FATAL_ERROR "In-source builds are not allowed. Use: cmake --preset default")
endif()

# ── Third-party dependencies ────────────────────────────────
include(FetchContent)

# ShiggyGameCore (sgc)
FetchContent_Declare(sgc
	GIT_REPOSITORY https://github.com/mogmog-0110/ShiggyGameCore.git
	GIT_TAG main
	GIT_SHALLOW TRUE
)
set(SGC_BUILD_TESTS OFF CACHE INTERNAL "")
set(SGC_BUILD_EXAMPLES OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(sgc)

# ── INTERFACE library (header-only) ──────────────────────────────
add_library(mitiru INTERFACE)
add_library(mitiru::mitiru ALIAS mitiru)

target_include_directories(mitiru INTERFACE
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:include>
)

target_compile_features(mitiru INTERFACE cxx_std_20)
target_link_libraries(mitiru INTERFACE sgc::sgc)

# MSVC: /FSで並列コンパイル時のPDBロックを回避
if(MSVC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /FS")
endif()

# ── Tests ────────────────────────────────────────────────────────
if(PROJECT_IS_TOP_LEVEL)
	option(MITIRU_BUILD_TESTS "Build tests" ON)
else()
	option(MITIRU_BUILD_TESTS "Build tests" OFF)
endif()

if(MITIRU_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()
