# EmscriptenToolchain.cmake
# Emscripten/WASM cross-compilation toolchain for MitiruEngine.
#
# Usage:
#   cmake -B build-wasm -DCMAKE_TOOLCHAIN_FILE=cmake/EmscriptenToolchain.cmake
#
# Note: Prefer `emcmake cmake` which sets the toolchain automatically.
#       This file provides explicit control and engine-specific WASM flags.

cmake_minimum_required(VERSION 3.21)

# ── System identification ────────────────────────────────────
set(CMAKE_SYSTEM_NAME Emscripten)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_CROSSCOMPILING TRUE)

# ── Compiler ─────────────────────────────────────────────────
# If EMSDK is set, derive compiler paths. Otherwise assume emcc is on PATH.
if(DEFINED ENV{EMSDK})
	set(EMSCRIPTEN_ROOT "$ENV{EMSDK}/upstream/emscripten")
	set(CMAKE_C_COMPILER   "${EMSCRIPTEN_ROOT}/emcc")
	set(CMAKE_CXX_COMPILER "${EMSCRIPTEN_ROOT}/em++")
	set(CMAKE_AR           "${EMSCRIPTEN_ROOT}/emar")
	set(CMAKE_RANLIB       "${EMSCRIPTEN_ROOT}/emranlib")
else()
	find_program(CMAKE_C_COMPILER   NAMES emcc   REQUIRED)
	find_program(CMAKE_CXX_COMPILER NAMES em++   REQUIRED)
	find_program(CMAKE_AR           NAMES emar   REQUIRED)
	find_program(CMAKE_RANLIB       NAMES emranlib REQUIRED)
endif()

# ── C++20 standard ───────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Output format ────────────────────────────────────────────
set(CMAKE_EXECUTABLE_SUFFIX ".html")

# ── WASM linker flags ───────────────────────────────────────
# Memory: 256MB initial, allow growth up to 512MB
set(MITIRU_WASM_MEMORY_FLAGS
	"-sINITIAL_MEMORY=268435456"
	"-sALLOW_MEMORY_GROWTH=1"
	"-sMAXIMUM_MEMORY=536870912"
	"-sSTACK_SIZE=1048576"
)

# WebGL2 (OpenGL ES 3.0 emulation)
set(MITIRU_WASM_GL_FLAGS
	"-sUSE_WEBGL2=1"
	"-sFULL_ES3=1"
	"-sMIN_WEBGL_VERSION=2"
	"-sMAX_WEBGL_VERSION=2"
)

# WebAudio via SDL2 audio shim
set(MITIRU_WASM_AUDIO_FLAGS
	"-sUSE_SDL=0"
)

# Exported runtime functions for JS interop
set(MITIRU_WASM_EXPORT_FLAGS
	"-sEXPORTED_FUNCTIONS=['_main','_malloc','_free']"
	"-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','stringToUTF8']"
)

# Filesystem and fetch support
set(MITIRU_WASM_FS_FLAGS
	"-sFORCE_FILESYSTEM=1"
	"-sFETCH=1"
)

# Combine all link flags
string(JOIN " " MITIRU_WASM_LINK_FLAGS
	${MITIRU_WASM_MEMORY_FLAGS}
	${MITIRU_WASM_GL_FLAGS}
	${MITIRU_WASM_AUDIO_FLAGS}
	${MITIRU_WASM_EXPORT_FLAGS}
	${MITIRU_WASM_FS_FLAGS}
)

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${MITIRU_WASM_LINK_FLAGS}")

# ── Compile definitions ─────────────────────────────────────
add_compile_definitions(
	MITIRU_PLATFORM_WEB=1
	__EMSCRIPTEN__=1
)

# ── Find root path mode ─────────────────────────────────────
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
