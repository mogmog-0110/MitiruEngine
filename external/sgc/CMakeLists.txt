cmake_minimum_required(VERSION 3.21)

project(ShiggyGameCore
	VERSION 0.1.0
	DESCRIPTION "C++20 game utility library for design patterns, math, and utilities"
	LANGUAGES CXX
)

# Prevent in-source builds
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
	message(FATAL_ERROR "In-source builds are not allowed. Use: cmake --preset default")
endif()

# ── INTERFACE library (header-only) ──────────────────────────────
add_library(sgc INTERFACE)
add_library(sgc::sgc ALIAS sgc)

target_include_directories(sgc INTERFACE
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:include>
)

target_compile_features(sgc INTERFACE cxx_std_20)

# MSVC: /FSで並列コンパイル時のPDBロックを回避
if(MSVC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /FS")
endif()

# ── Tests ────────────────────────────────────────────────────────
if(PROJECT_IS_TOP_LEVEL)
	option(SGC_BUILD_TESTS "Build tests" ON)
else()
	option(SGC_BUILD_TESTS "Build tests" OFF)
endif()

if(SGC_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()

# ── Examples ──────────────────────────────────────────────────────
if(PROJECT_IS_TOP_LEVEL)
	option(SGC_BUILD_EXAMPLES "Build examples" ON)
else()
	option(SGC_BUILD_EXAMPLES "Build examples" OFF)
endif()

if(SGC_BUILD_EXAMPLES)
	add_subdirectory(examples)
endif()

# ── Documentation ──────────────────────────────────────────────
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND AND PROJECT_IS_TOP_LEVEL)
	add_custom_target(docs
		COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		COMMENT "Generating API documentation with Doxygen"
		VERBATIM
	)
endif()

# ── Install / Export ─────────────────────────────────────────────
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS sgc
	EXPORT sgcTargets
	INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/sgc
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT sgcTargets
	FILE sgcTargets.cmake
	NAMESPACE sgc::
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sgc
)

configure_package_config_file(
	cmake/sgcConfig.cmake.in
	${CMAKE_CURRENT_BINARY_DIR}/sgcConfig.cmake
	INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sgc
)

write_basic_package_version_file(
	${CMAKE_CURRENT_BINARY_DIR}/sgcConfigVersion.cmake
	COMPATIBILITY SameMajorVersion
)

install(FILES
	${CMAKE_CURRENT_BINARY_DIR}/sgcConfig.cmake
	${CMAKE_CURRENT_BINARY_DIR}/sgcConfigVersion.cmake
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sgc
)
