cmake_minimum_required(VERSION 3.21)

# CMP0091: MSVC ランタイムライブラリのポリシー (MITIRU_DIST の静的 CRT に必要)
cmake_policy(SET CMP0091 NEW)

# #56: vcpkg applocal の pwsh パスがバージョン固定でキャッシュされ、pwsh の自動更新で古くなる問題を
# 修正する (バージョンに依存しない alias に固定)。vcpkg toolchain の find_program より前に実行する必要があるため、
# project() より前に include する。
include(${CMAKE_CURRENT_LIST_DIR}/cmake/VcpkgPwshFix.cmake)

project(MitiruEngine
	VERSION 0.34.0
	DESCRIPTION "C++ ゲームエンジン"
	LANGUAGES CXX
)

# ── ビルドモード切替 ─────────────────────────────────────────────
# ON (デフォルト): INTERFACE ライブラリ。従来どおりの header-only 動作。
# OFF: STATIC ライブラリ。Phase 2 以降でモジュールを .cpp に分離する際に使用。
# コンシューマー側はこのオプションを設定しない限り動作に変化なし。
option(MITIRU_HEADER_ONLY "Build engine as header-only INTERFACE library" ON)

# ── ネットワーク (GameNetworkingSockets) opt-in ────────────────
# OFF (デフォルト): NullReliableTransport スタブのみ。consumer への影響なし。
# ON: external/GameNetworkingSockets + protobuf + libsodium を submodule から build。
#     有効化前に実行: git submodule update --init --recursive external/GameNetworkingSockets external/protobuf external/libsodium
# 詳細: docs/NETWORK_GNS.md
option(MITIRU_ENABLE_GNS "Build with GameNetworkingSockets (requires external/GameNetworkingSockets and deps)" OFF)

# ソースディレクトリ内でのビルドを禁止する
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
	message(FATAL_ERROR "In-source builds are not allowed. Use: cmake --preset default")
endif()

# ── Third-party dependencies ────────────────────────────────

# ShiggyGameCore (sgc)。external/sgc にある git submodule
set(SGC_BUILD_TESTS OFF CACHE INTERNAL "")
set(SGC_BUILD_EXAMPLES OFF CACHE INTERNAL "")
add_subdirectory(external/sgc)

# MitiruMML。external/mml にある git submodule
set(MITIRU_MML_BUILD_TESTS OFF CACHE INTERNAL "")
add_subdirectory(external/mml)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

# MITIRU_HOST_GUI=ON: mitiru_host を GUI subsystem でリンクし、配布時にコンソールウィンドウを
# 表示しない (entry は main() のまま)。デフォルトは OFF。dev ではコンソールでログを確認する。
# `mitiru dist` が ON で configure する。
option(MITIRU_HOST_GUI
    "Link mitiru_host as a GUI-subsystem exe (no console window) for distribution."
    OFF)

# ── GameNetworkingSockets (opt-in via MITIRU_ENABLE_GNS) ────────
if(MITIRU_ENABLE_GNS)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/GameNetworkingSockets/CMakeLists.txt")
        message(FATAL_ERROR
            "MITIRU_ENABLE_GNS=ON ですが external/GameNetworkingSockets が空です。\n"
            "  git submodule update --init --recursive external/GameNetworkingSockets external/protobuf external/libsodium\n"
            "詳細: docs/NETWORK_GNS.md")
    endif()

    # protobuf。GNS の必須依存
    set(protobuf_BUILD_TESTS OFF CACHE INTERNAL "")
    set(protobuf_BUILD_PROTOC_BINARIES ON CACHE INTERNAL "")
    set(protobuf_INSTALL OFF CACHE INTERNAL "")
    add_subdirectory(external/protobuf EXCLUDE_FROM_ALL)

    # GNS 本体。Windows では BCrypt が既定 (libsodium は不要)、それ以外では libsodium
    if(WIN32)
        set(USE_CRYPTO "BCrypt" CACHE INTERNAL "GNS crypto backend")
        set(USE_CRYPTO25519 "Reference" CACHE INTERNAL "GNS curve25519 backend")
    else()
        set(USE_CRYPTO "libsodium" CACHE INTERNAL "GNS crypto backend")
        set(USE_CRYPTO25519 "libsodium" CACHE INTERNAL "GNS curve25519 backend")
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/libsodium/CMakeLists.txt")
            add_subdirectory(external/libsodium EXCLUDE_FROM_ALL)
        else()
            message(WARNING
                "external/libsodium に CMakeLists.txt が無い。"
                "libsodium はシステムインストール (apt/brew/vcpkg) を使う。詳細: docs/NETWORK_GNS.md")
        endif()
    endif()
    set(BUILD_TESTS OFF CACHE INTERNAL "")
    set(BUILD_EXAMPLES OFF CACHE INTERNAL "")
    add_subdirectory(external/GameNetworkingSockets EXCLUDE_FROM_ALL)

    message(STATUS "MITIRU_ENABLE_GNS=ON — GameNetworkingSockets を build (USE_CRYPTO=${USE_CRYPTO})")
else()
    message(STATUS "MITIRU_ENABLE_GNS=OFF (default) — networking は NullReliableTransport を使用")
endif()

# ── stb implementation (static library) ──────────────────────────
add_library(stb_impl STATIC src/stb_impl.cpp)
target_include_directories(stb_impl PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/stb>
)
target_compile_features(stb_impl PUBLIC cxx_std_20)
if(MSVC)
	target_compile_options(stb_impl PRIVATE /W0 /FS)
elseif(EMSCRIPTEN)
	target_compile_options(stb_impl PRIVATE -w)
endif()

# ── fpng 1.0.6 (static library、util/ImageWriter.hpp の PNG 符号化) ──
# SSE4.1 / PCLMUL は実行時に CPU を確認して使い分けるため、MSVC 以外の x86 では
# それらの intrinsics を有効にしてビルドする。WASM では SSE を使わない。
add_library(fpng_impl STATIC external/fpng/fpng.cpp)
target_include_directories(fpng_impl PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external>
)
target_compile_features(fpng_impl PUBLIC cxx_std_20)
if(MSVC)
	target_compile_options(fpng_impl PRIVATE /W0 /FS)
elseif(EMSCRIPTEN)
	target_compile_definitions(fpng_impl PRIVATE FPNG_NO_SSE=1)
	target_compile_options(fpng_impl PRIVATE -w)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i[3-6]86")
	target_compile_options(fpng_impl PRIVATE -w -msse4.1 -mpclmul)
else()
	target_compile_options(fpng_impl PRIVATE -w)
endif()

# ── xxHash implementation (static library、util/Hash.hpp の xxhash) ──
add_library(xxhash_impl STATIC src/xxhash_impl.cpp)
target_include_directories(xxhash_impl PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external>
)
target_compile_features(xxhash_impl PUBLIC cxx_std_20)
if(MSVC)
	target_compile_options(xxhash_impl PRIVATE /W0 /FS)
elseif(EMSCRIPTEN)
	target_compile_options(xxhash_impl PRIVATE -w)
endif()

# ── cpp-httplib (static library、server/HttpListener.hpp と network/WebSocketTransport.hpp の実装) ──
# cpp-httplib は windows.h / winsock2.h を取り込むため、エンジンのヘッダには公開せず、
# この 2 TU 内に限定する。WASM では HTTP サーバーを持たない (EngineHttpServer はスタブ)。
if(NOT EMSCRIPTEN)
	add_library(httplib_impl STATIC src/http_listener_impl.cpp src/websocket_transport_impl.cpp)
	target_include_directories(httplib_impl PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/include
		${CMAKE_CURRENT_SOURCE_DIR}/external
	)
	target_compile_features(httplib_impl PUBLIC cxx_std_20)
	if(MSVC)
		target_compile_options(httplib_impl PRIVATE /utf-8 /FS)
	endif()
	if(WIN32)
		target_link_libraries(httplib_impl PUBLIC ws2_32)
	else()
		find_package(Threads REQUIRED)
		target_link_libraries(httplib_impl PUBLIC Threads::Threads)
	endif()
endif()

# ── tinyobjloader implementation (static library) ────────────────
add_library(tinyobj_impl STATIC src/tinyobj_impl.cpp)
target_include_directories(tinyobj_impl PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external>
)
target_compile_features(tinyobj_impl PUBLIC cxx_std_20)
if(MSVC)
	target_compile_options(tinyobj_impl PRIVATE /W0 /FS)
elseif(EMSCRIPTEN)
	target_compile_options(tinyobj_impl PRIVATE -w)
endif()

# ── cgltf implementation (static library、#16: 重複定義回避) ────────
add_library(cgltf_impl STATIC src/cgltf_impl.cpp)
target_include_directories(cgltf_impl PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/cgltf>
)
target_compile_features(cgltf_impl PUBLIC cxx_std_20)
if(MSVC)
	target_compile_options(cgltf_impl PRIVATE /W0 /FS)
elseif(EMSCRIPTEN)
	target_compile_options(cgltf_impl PRIVATE -w)
endif()

# ── テクスチャ圧縮 (BC7/BC5/BC4 の DDS、static library) ─────────
add_subdirectory(cmake/texcompress)

# ── meshoptimizer + clod import (static library) ─────────────────
# drawModel の import cache (OBJ/glTF →.clod への初回変換、テクスチャは DDS に圧縮)。
# clusterlod.h の実装 TU と meshoptimizer 本体をここに限定する。
file(GLOB _meshopt_sources ${CMAKE_CURRENT_SOURCE_DIR}/external/meshoptimizer/src/*.cpp)
add_library(meshopt_impl STATIC ${_meshopt_sources} src/clod_import_impl.cpp)
target_link_libraries(meshopt_impl PRIVATE texcompress_impl)
target_include_directories(meshopt_impl PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/meshoptimizer/src>
)
target_include_directories(meshopt_impl PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/external
	${CMAKE_CURRENT_SOURCE_DIR}/external/cgltf
	${CMAKE_CURRENT_SOURCE_DIR}/external/meshoptimizer/demo
	${CMAKE_CURRENT_SOURCE_DIR}/external/sgc/include
	${CMAKE_CURRENT_SOURCE_DIR}/external/stb
)
target_compile_features(meshopt_impl PUBLIC cxx_std_20)
if(MSVC)
	target_compile_options(meshopt_impl PRIVATE /W0 /FS /utf-8)
elseif(EMSCRIPTEN)
	target_compile_options(meshopt_impl PRIVATE -w)
endif()

# ── D3D12 Memory Allocator (static library、DX12 の GPU メモリ確保) ──
if(WIN32)
	add_library(d3d12ma_impl STATIC
		${CMAKE_CURRENT_SOURCE_DIR}/external/D3D12MemoryAllocator/src/D3D12MemAlloc.cpp)
	target_include_directories(d3d12ma_impl PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/D3D12MemoryAllocator/include>)
	target_link_libraries(d3d12ma_impl PUBLIC d3d12 dxgi)
	if(MSVC)
		target_compile_options(d3d12ma_impl PRIVATE /W0 /FS)
	endif()
endif()

# ── miniaudio implementation (static library) ───────────────────
# Emscripten では WebAudioEngine を使うため、miniaudio は不要
if(NOT EMSCRIPTEN)
	add_library(miniaudio_impl STATIC src/miniaudio_impl.cpp)
	target_include_directories(miniaudio_impl PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external>
	)
	# Vorbis は stb_vorbis (external/stb/stb_vorbis.c) を miniaudio に組み込んで復号する
	target_include_directories(miniaudio_impl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external/stb)
	# PulseAudio / ALSA / JACK は miniaudio が実行時に dlopen する (リンク時の libpulse などは不要)
	if(UNIX AND NOT APPLE)
		target_link_libraries(miniaudio_impl PUBLIC ${CMAKE_DL_LIBS} pthread m)
	endif()
	target_compile_features(miniaudio_impl PUBLIC cxx_std_20)
	if(MSVC)
		target_compile_options(miniaudio_impl PRIVATE /W0 /FS)
	endif()
endif()

# ── 文字描画 (FreeType + HarfBuzz + msdfgen、static library) ─────
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/MitiruText.cmake)

# UI の層 (RmlUi、Windows では既定 ON)。FreeType を文字描画と共有するため、その後に読み込む
include(MitiruRmlUi)

# ── メインライブラリ (header-only / STATIC 切替) ──────────────────
if(MITIRU_HEADER_ONLY)
    add_library(mitiru INTERFACE)
    set(MITIRU_TARGET_SCOPE INTERFACE)
else()
    # STATIC モード: Phase 2 でモジュールの .cpp を順次追加する。
    # プレースホルダーは Phase 1 時点の最小翻訳単位。
    # Engine.cpp は 3-F Phase 2 pilot。Engine.hpp の out-of-class 定義 (約 6 個の detail headers) を
    # 1 つの TU に集約する。
    add_library(mitiru STATIC
        src/mitiru_static_placeholder.cpp
        src/core/Engine.cpp
    )
    set(MITIRU_TARGET_SCOPE PUBLIC)
endif()

add_library(mitiru::mitiru ALIAS mitiru)
# 大文字で始まる namespace alias (下流の find_package / FetchContent での使用を推奨)
add_library(Mitiru::mitiru ALIAS mitiru)

# MITIRU_HEADER_ONLY を compile definition としてヘッダー側に伝える
if(MITIRU_HEADER_ONLY)
    target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HEADER_ONLY=1)
endif()

# クラッシュ報告 (debug/CrashReport.hpp) が「どの engine で落ちたか」を記録するためのバージョン番号。
target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_ENGINE_VERSION="${PROJECT_VERSION}")

target_include_directories(mitiru ${MITIRU_TARGET_SCOPE}
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/stb>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/cgltf>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/vendored>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/vendored/lygia>
	$<INSTALL_INTERFACE:include>
)

target_compile_features(mitiru ${MITIRU_TARGET_SCOPE} cxx_std_20)
if(EMSCRIPTEN)
	target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} sgc::sgc mitiru_mml::mitiru_mml stb_impl fpng_impl xxhash_impl tinyobj_impl cgltf_impl mitiru_text)
	target_link_options(mitiru ${MITIRU_TARGET_SCOPE}
		-sUSE_WEBGL2=1
		-sFULL_ES3=1
		-sALLOW_MEMORY_GROWTH=1
		-sEXPORTED_RUNTIME_METHODS=ccall,cwrap
		-sINVOKE_RUN=1
	)

	# WebGPU のサポート (-DMITIRU_ENABLE_WEBGPU=ON で有効化)
	option(MITIRU_ENABLE_WEBGPU "Enable WebGPU backend for Emscripten" OFF)
	if(MITIRU_ENABLE_WEBGPU)
		message(STATUS "WebGPU backend enabled")
		target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_WEBGPU=1)
		target_link_options(mitiru ${MITIRU_TARGET_SCOPE} -sUSE_WEBGPU=1)
	endif()

	# web/shell.html の {{GAME_JS}} は手作業で置換していた (docs/TODO_ENGINE_2026_09_17.md U1)。
	# wasm game target を追加するときは mitiru_generate_wasm_shell(<target>) を
	# 1 行追加するだけで、その target の出力 dir に置換済みの shell.html が配置される。
	include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/WasmShell.cmake)
else()
	target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} sgc::sgc mitiru_mml::mitiru_mml stb_impl fpng_impl xxhash_impl httplib_impl tinyobj_impl cgltf_impl miniaudio_impl meshopt_impl mitiru_text texcompress_impl)

	if(TARGET mitiru_ui_rml)
		target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} mitiru_ui_rml)
	endif()

	# MITIRU_ENABLE_GNS=ON の場合のみ GNS をリンクし、MITIRU_HAS_GNS=1 を伝える
	if(MITIRU_ENABLE_GNS)
		target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} GameNetworkingSockets::shared)
		target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_GNS=1)
	endif()
endif()

# MSVC: UTF-8 ソースエンコーディング (INTERFACE で利用者にも伝える)
if(MSVC)
	target_compile_options(mitiru ${MITIRU_TARGET_SCOPE} /utf-8 /FS)
endif()

# ── Platform detection ──────────────────────────────────────
if(EMSCRIPTEN)
	message(STATUS "Building for WebAssembly (Emscripten)")
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_PLATFORM_WEB=1)
elseif(WIN32)
	message(STATUS "Building for Windows")
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_PLATFORM_WIN32=1)
elseif(APPLE)
	message(STATUS "Building for macOS/iOS")
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_PLATFORM_APPLE=1)
elseif(UNIX)
	message(STATUS "Building for Linux/Unix")
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_PLATFORM_UNIX=1)
endif()

# ── Optional backend detection (Emscripten ではスキップ) ────
if(NOT EMSCRIPTEN)
# SDL2 は DualShock 4 などの DirectInput 系パッドに不可欠 (XInput は Xbox コントローラー系のみ)。
# システムになければ external/sdl2/ (tools/fetch_sdl2.py が配置する) を自動検出する。
if(NOT SDL2_DIR)
	file(GLOB _mitiru_sdl2_local "${CMAKE_CURRENT_SOURCE_DIR}/external/sdl2/SDL2-*/cmake")
	if(_mitiru_sdl2_local)
		list(GET _mitiru_sdl2_local 0 SDL2_DIR)
		message(STATUS "SDL2: using vendored copy at ${SDL2_DIR}")
	endif()
endif()
find_package(SDL2 2.0.10 QUIET)
if(SDL2_FOUND)
	message(STATUS "SDL2 found (${SDL2_VERSION}) - enabling SDL2 backend")
	# SDL_MAIN_HANDLED: console app (mitiru_host が main を持つ) のため、SDL2 に main を置き換えさせない。
	# 副次的に SDL2main.lib への link が不要になる。
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_SDL2=1 SDL_MAIN_HANDLED=1)
	target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} SDL2::SDL2)
	# imported target はディレクトリスコープ。consumer (add_subdirectory でエンジンを取り込む
	# プロジェクト) の CMakeLists からも `TARGET SDL2::SDL2` で参照できるよう昇格する
	# (これがないと consumer 側で SDL2.dll の deploy が通知なしで skip され、host が起動時に異常終了する)。
	if(TARGET SDL2::SDL2)
		set_target_properties(SDL2::SDL2 PROPERTIES IMPORTED_GLOBAL TRUE)
	endif()
elseif(WIN32)
	# 知らないうちに機能が劣化しないようにする。DS4 が使えない host が通知なしで作られるのを防ぐ。
	message(WARNING
		"SDL2 が見つかりません。DualShock 4 等の DirectInput 系ゲームパッドが効かない host になります。\n"
		"  対処: python tools/fetch_sdl2.py を実行してから configure し直してください。")
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/MitiruDeploySdl2.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/MitiruSteamAudio.cmake)

find_package(glfw3 3.3 QUIET)
if(NOT glfw3_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/glfw/CMakeLists.txt")
	message(STATUS "GLFW not found via find_package - building from external/glfw")
	set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
	set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
	add_subdirectory(external/glfw)
	set(glfw3_FOUND TRUE)
endif()
if(glfw3_FOUND)
	message(STATUS "GLFW found - enabling GLFW backend")
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_GLFW=1)
	target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} glfw)
endif()

find_package(OpenGL QUIET)
if(OpenGL_FOUND AND (SDL2_FOUND OR glfw3_FOUND))
	message(STATUS "OpenGL found (SDL2=${SDL2_FOUND}, GLFW=${glfw3_FOUND}) - enabling OpenGL backend")
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_OPENGL=1)
	target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} OpenGL::GL)
elseif(OpenGL_FOUND)
	message(STATUS "OpenGL found but no window backend (SDL2/GLFW) - OpenGL backend disabled")
endif()

find_package(Vulkan QUIET)
if(Vulkan_FOUND)
	message(STATUS "Vulkan SDK found - enabling Vulkan backend")
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_VULKAN=1)
	target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} Vulkan::Vulkan)
endif()

endif() # NOT EMSCRIPTEN (optional backend detection)

# ── Desktop-only optional libraries (Emscripten ではスキップ) ──
if(NOT EMSCRIPTEN)

# ── GLEW (OpenGL Extension Wrangler) ─────────────────────────
# 最初にシステムの GLEW を試し、次に同梱したコピーを使う
find_package(GLEW QUIET)
if(NOT GLEW_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/glew/include/GL/glew.h")
	message(STATUS "GLEW not found via find_package - using vendored copy")
	add_library(GLEW::GLEW STATIC IMPORTED)
	if(WIN32)
		# 同梱した GLEW ライブラリのアーキテクチャを判定する
		if(CMAKE_SIZEOF_VOID_P EQUAL 8)
			set(_GLEW_ARCH "x64")
		else()
			set(_GLEW_ARCH "Win32")
		endif()
		set(_GLEW_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/glew/lib/Release/${_GLEW_ARCH}")
		if(EXISTS "${_GLEW_LIB_DIR}/glew32s.lib")
			set_target_properties(GLEW::GLEW PROPERTIES
				IMPORTED_LOCATION_RELEASE "${_GLEW_LIB_DIR}/glew32s.lib"
				IMPORTED_LOCATION_DEBUG "${_GLEW_LIB_DIR}/glew32s.lib"
				IMPORTED_LOCATION_RELWITHDEBINFO "${_GLEW_LIB_DIR}/glew32s.lib"
				IMPORTED_LOCATION_MINSIZEREL "${_GLEW_LIB_DIR}/glew32s.lib"
			)
		else()
			message(WARNING "GLEW vendored library not found at ${_GLEW_LIB_DIR}/glew32s.lib")
			set(GLEW_FOUND FALSE)
		endif()
		unset(_GLEW_ARCH)
		unset(_GLEW_LIB_DIR)
	endif()
	target_include_directories(GLEW::GLEW INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/external/glew/include")
	target_compile_definitions(GLEW::GLEW INTERFACE GLEW_STATIC)
	set(GLEW_FOUND TRUE)
elseif(GLEW_FOUND)
	message(STATUS "GLEW found via find_package")
else()
	message(STATUS "GLEW not found - OpenGL extension loading unavailable")
endif()

# ── Live2D Cubism SDK (optional) ─────────────────────────────
# external/cubism/ 以下にある SDK のバージョンディレクトリを自動検出する
set(CUBISM_SDK_DIR "" CACHE PATH "Cubism SDK root (auto-detected if empty)")
if(NOT CUBISM_SDK_DIR OR NOT EXISTS "${CUBISM_SDK_DIR}")
	file(GLOB _cubism_candidates "${CMAKE_CURRENT_SOURCE_DIR}/external/cubism/CubismSdkForNative-*")
	list(LENGTH _cubism_candidates _cubism_count)
	if(_cubism_count GREATER 0)
		list(SORT _cubism_candidates ORDER DESCENDING)
		list(GET _cubism_candidates 0 CUBISM_SDK_DIR)
		message(STATUS "Cubism SDK auto-detected: ${CUBISM_SDK_DIR}")
	endif()
	unset(_cubism_candidates)
	unset(_cubism_count)
endif()

set(CUBISM_CORE_PATH "${CUBISM_SDK_DIR}/Core")
set(CUBISM_FW_SRC "${CUBISM_SDK_DIR}/Framework/src")

if(CUBISM_SDK_DIR
   AND EXISTS "${CUBISM_CORE_PATH}/include/Live2DCubismCore.h"
   AND EXISTS "${CUBISM_FW_SRC}/CubismFramework.hpp"
   AND WIN32 AND GLEW_FOUND)
	message(STATUS "Live2D Cubism SDK found - enabling Live2D integration")

	add_library(Live2DCubismCore STATIC IMPORTED)
	set_target_properties(Live2DCubismCore PROPERTIES
		IMPORTED_LOCATION_DEBUG
			"${CUBISM_CORE_PATH}/lib/windows/x86_64/143/Live2DCubismCore_MDd.lib"
		IMPORTED_LOCATION_RELEASE
			"${CUBISM_CORE_PATH}/lib/windows/x86_64/143/Live2DCubismCore_MD.lib"
		IMPORTED_LOCATION_RELWITHDEBINFO
			"${CUBISM_CORE_PATH}/lib/windows/x86_64/143/Live2DCubismCore_MD.lib"
		IMPORTED_LOCATION_MINSIZEREL
			"${CUBISM_CORE_PATH}/lib/windows/x86_64/143/Live2DCubismCore_MD.lib"
		INTERFACE_INCLUDE_DIRECTORIES "${CUBISM_CORE_PATH}/include"
	)

	set(CUBISM_FW_SOURCES
		"${CUBISM_FW_SRC}/CubismCdiJson.cpp"
		"${CUBISM_FW_SRC}/CubismDefaultParameterId.cpp"
		"${CUBISM_FW_SRC}/CubismFramework.cpp"
		"${CUBISM_FW_SRC}/CubismModelSettingJson.cpp"
		"${CUBISM_FW_SRC}/Effect/CubismBreath.cpp"
		"${CUBISM_FW_SRC}/Effect/CubismEyeBlink.cpp"
		"${CUBISM_FW_SRC}/Effect/CubismPose.cpp"
		"${CUBISM_FW_SRC}/Id/CubismId.cpp"
		"${CUBISM_FW_SRC}/Id/CubismIdManager.cpp"
		"${CUBISM_FW_SRC}/Math/CubismMath.cpp"
		"${CUBISM_FW_SRC}/Math/CubismMatrix44.cpp"
		"${CUBISM_FW_SRC}/Math/CubismModelMatrix.cpp"
		"${CUBISM_FW_SRC}/Math/CubismTargetPoint.cpp"
		"${CUBISM_FW_SRC}/Math/CubismVector2.cpp"
		"${CUBISM_FW_SRC}/Math/CubismViewMatrix.cpp"
		"${CUBISM_FW_SRC}/Model/CubismMoc.cpp"
		"${CUBISM_FW_SRC}/Model/CubismModel.cpp"
		"${CUBISM_FW_SRC}/Model/CubismModelUserData.cpp"
		"${CUBISM_FW_SRC}/Model/CubismModelUserDataJson.cpp"
		"${CUBISM_FW_SRC}/Model/CubismUserModel.cpp"
		"${CUBISM_FW_SRC}/Motion/ACubismMotion.cpp"
		"${CUBISM_FW_SRC}/Motion/CubismExpressionMotion.cpp"
		"${CUBISM_FW_SRC}/Motion/CubismExpressionMotionManager.cpp"
		"${CUBISM_FW_SRC}/Motion/CubismMotion.cpp"
		"${CUBISM_FW_SRC}/Motion/CubismMotionJson.cpp"
		"${CUBISM_FW_SRC}/Motion/CubismMotionManager.cpp"
		"${CUBISM_FW_SRC}/Motion/CubismMotionQueueEntry.cpp"
		"${CUBISM_FW_SRC}/Motion/CubismMotionQueueManager.cpp"
		"${CUBISM_FW_SRC}/Physics/CubismPhysics.cpp"
		"${CUBISM_FW_SRC}/Physics/CubismPhysicsJson.cpp"
		"${CUBISM_FW_SRC}/Rendering/CubismRenderer.cpp"
		"${CUBISM_FW_SRC}/Rendering/OpenGL/CubismOffscreenSurface_OpenGLES2.cpp"
		"${CUBISM_FW_SRC}/Rendering/OpenGL/CubismRenderer_OpenGLES2.cpp"
		"${CUBISM_FW_SRC}/Rendering/OpenGL/CubismShader_OpenGLES2.cpp"
		"${CUBISM_FW_SRC}/Type/csmRectF.cpp"
		"${CUBISM_FW_SRC}/Type/csmString.cpp"
		"${CUBISM_FW_SRC}/Utils/CubismDebug.cpp"
		"${CUBISM_FW_SRC}/Utils/CubismJson.cpp"
		"${CUBISM_FW_SRC}/Utils/CubismString.cpp"
	)

	add_library(Live2DCubismFramework STATIC ${CUBISM_FW_SOURCES})
	target_include_directories(Live2DCubismFramework PUBLIC
		"${CUBISM_FW_SRC}"
		"${CUBISM_CORE_PATH}/include"
	)
	target_compile_definitions(Live2DCubismFramework PUBLIC CSM_TARGET_WIN_GL)
	target_link_libraries(Live2DCubismFramework PUBLIC
		Live2DCubismCore GLEW::GLEW opengl32
	)
	if(MSVC)
		target_compile_options(Live2DCubismFramework PRIVATE /utf-8 /FS /W0)
		set_property(TARGET Live2DCubismFramework PROPERTY
			MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
	endif()

	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_CUBISM=1)
	target_include_directories(mitiru ${MITIRU_TARGET_SCOPE}
		"${CUBISM_FW_SRC}"
		"${CUBISM_CORE_PATH}/include"
	)
	target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} Live2DCubismFramework)
else()
	if(NOT CUBISM_SDK_DIR)
		message(STATUS "Live2D Cubism SDK not found in external/cubism/ - Live2D disabled")
		message(STATUS "  Hint: Place CubismSdkForNative-* directory under external/cubism/")
	elseif(NOT EXISTS "${CUBISM_CORE_PATH}/include/Live2DCubismCore.h")
		message(STATUS "Live2D Cubism SDK directory found but Core library missing at ${CUBISM_CORE_PATH}")
	elseif(NOT GLEW_FOUND)
		message(STATUS "Live2D Cubism SDK found but GLEW missing - Live2D disabled")
	elseif(NOT WIN32)
		message(STATUS "Live2D Cubism SDK found but not on Windows - Live2D disabled (Windows-only)")
	endif()
endif()

# ── Live2D Cubism Framework (D3D12 headless path: model+motion+physics+effects) ─────
# 上のブロックは OpenGL レンダラ用 (GLEW 必須)。こちらは Framework の非描画ソースだけを
# ビルドし (Rendering/* と Updater/scheduler を除外、GLEW に非依存)、自前の D3D12 レンダラ
# (dx12/DX12Live2D.hpp) で描画する。公式サンプル相当の model3.json のロード、motion3.json の再生、
# physics、目パチ/呼吸、ドラッグ追従を Framework が担う (src/live2d/Live2DModel.cpp)。
# Core は Framework 内で Live2D::Cubism::Core 名前空間にラップされる。静的 Core lib (/MD) をリンクする。
if(CUBISM_SDK_DIR AND WIN32
   AND EXISTS "${CUBISM_CORE_PATH}/include/Live2DCubismCore.h"
   AND EXISTS "${CUBISM_CORE_PATH}/lib/windows/x86_64/143/Live2DCubismCore_MD.lib"
   AND EXISTS "${CUBISM_FW_SRC}/CubismFramework.hpp")
	if(NOT TARGET Live2DCubismCore)
		add_library(Live2DCubismCore STATIC IMPORTED)
		set_target_properties(Live2DCubismCore PROPERTIES
			IMPORTED_LOCATION_DEBUG          "${CUBISM_CORE_PATH}/lib/windows/x86_64/143/Live2DCubismCore_MDd.lib"
			IMPORTED_LOCATION_RELEASE        "${CUBISM_CORE_PATH}/lib/windows/x86_64/143/Live2DCubismCore_MD.lib"
			IMPORTED_LOCATION_RELWITHDEBINFO "${CUBISM_CORE_PATH}/lib/windows/x86_64/143/Live2DCubismCore_MD.lib"
			IMPORTED_LOCATION_MINSIZEREL     "${CUBISM_CORE_PATH}/lib/windows/x86_64/143/Live2DCubismCore_MD.lib"
			INTERFACE_INCLUDE_DIRECTORIES    "${CUBISM_CORE_PATH}/include")
	endif()

	# 非描画の Framework ソース (Rendering/* と *Updater/CubismUpdateScheduler は除外)
	if(NOT TARGET Live2DCubismFrameworkD3D)
		set(CUBISM_FW_D3D_SOURCES
			"${CUBISM_FW_SRC}/CubismCdiJson.cpp"
			"${CUBISM_FW_SRC}/CubismDefaultParameterId.cpp"
			"${CUBISM_FW_SRC}/CubismFramework.cpp"
			"${CUBISM_FW_SRC}/CubismModelSettingJson.cpp"
			"${CUBISM_FW_SRC}/Effect/CubismBreath.cpp"
			"${CUBISM_FW_SRC}/Effect/CubismEyeBlink.cpp"
			"${CUBISM_FW_SRC}/Effect/CubismPose.cpp"
			"${CUBISM_FW_SRC}/Id/CubismId.cpp"
			"${CUBISM_FW_SRC}/Id/CubismIdManager.cpp"
			"${CUBISM_FW_SRC}/Math/CubismMath.cpp"
			"${CUBISM_FW_SRC}/Math/CubismMatrix44.cpp"
			"${CUBISM_FW_SRC}/Math/CubismModelMatrix.cpp"
			"${CUBISM_FW_SRC}/Math/CubismTargetPoint.cpp"
			"${CUBISM_FW_SRC}/Math/CubismVector2.cpp"
			"${CUBISM_FW_SRC}/Math/CubismViewMatrix.cpp"
			"${CUBISM_FW_SRC}/Model/CubismMoc.cpp"
			"${CUBISM_FW_SRC}/Model/CubismModel.cpp"
			"${CUBISM_FW_SRC}/Model/CubismModelMultiplyAndScreenColor.cpp"
			"${CUBISM_FW_SRC}/Model/CubismModelUserData.cpp"
			"${CUBISM_FW_SRC}/Model/CubismModelUserDataJson.cpp"
			"${CUBISM_FW_SRC}/Motion/ACubismMotion.cpp"
			"${CUBISM_FW_SRC}/Motion/CubismExpressionMotion.cpp"
			"${CUBISM_FW_SRC}/Motion/CubismExpressionMotionManager.cpp"
			"${CUBISM_FW_SRC}/Motion/CubismMotion.cpp"
			"${CUBISM_FW_SRC}/Motion/CubismMotionJson.cpp"
			"${CUBISM_FW_SRC}/Motion/CubismMotionManager.cpp"
			"${CUBISM_FW_SRC}/Motion/CubismMotionQueueEntry.cpp"
			"${CUBISM_FW_SRC}/Motion/CubismMotionQueueManager.cpp"
			"${CUBISM_FW_SRC}/Physics/CubismPhysics.cpp"
			"${CUBISM_FW_SRC}/Physics/CubismPhysicsJson.cpp"
			# csmBlendMode は Rendering/ 以下にあるが、GL に依存しないブレンドモードデータ (CubismModel が参照)
			"${CUBISM_FW_SRC}/Rendering/csmBlendMode.cpp"
			"${CUBISM_FW_SRC}/Type/csmRectF.cpp"
			"${CUBISM_FW_SRC}/Type/csmString.cpp"
			"${CUBISM_FW_SRC}/Utils/CubismDebug.cpp"
			"${CUBISM_FW_SRC}/Utils/CubismJson.cpp"
			"${CUBISM_FW_SRC}/Utils/CubismString.cpp"
			# 自前の接続処理: アロケータ + StaticRelease スタブ + Framework の起動 + Live2DModel pimpl の実装
			"${CMAKE_CURRENT_SOURCE_DIR}/src/live2d/Live2DModel.cpp"
		)
		add_library(Live2DCubismFrameworkD3D STATIC ${CUBISM_FW_D3D_SOURCES})
		# 自前の接続処理 (src/live2d/Live2DModel.cpp) は C++20 の機能を使う。独立した STATIC lib は
		# エンジン本体の標準を継承しないため明示する (未設定だと MSVC 既定の C++14 で nested-namespace のコンパイルに失敗する)。
		target_compile_features(Live2DCubismFrameworkD3D PRIVATE cxx_std_20)
		target_include_directories(Live2DCubismFrameworkD3D PUBLIC
			"${CUBISM_FW_SRC}"
			"${CUBISM_CORE_PATH}/include"
			"${CMAKE_CURRENT_SOURCE_DIR}/include")
		target_compile_definitions(Live2DCubismFrameworkD3D PUBLIC MITIRU_HAS_CUBISM_FRAMEWORK=1)
		target_link_libraries(Live2DCubismFrameworkD3D PUBLIC Live2DCubismCore)
		if(MSVC)
			target_compile_options(Live2DCubismFrameworkD3D PRIVATE /utf-8 /FS /W0)
			set_property(TARGET Live2DCubismFrameworkD3D PROPERTY
				MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
		endif()
	endif()

	target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} Live2DCubismFrameworkD3D)
	target_include_directories(mitiru ${MITIRU_TARGET_SCOPE}
		"${CUBISM_CORE_PATH}/include")
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE}
		MITIRU_HAS_CUBISM_CORE=1 MITIRU_HAS_CUBISM_FRAMEWORK=1)
	message(STATUS "Live2D Cubism Framework (D3D12 headless path) enabled")
endif()

# ── Jolt Physics (3D 物理の既定バックエンド) ──────────────────
# 巻き戻しとリプレイは物理の結果がビット単位で一致することに依存するため、Jolt は常にクロスプラット
# フォーム決定論を有効にして build する (浮動小数点数の縮約を止める。Jolt 自身の計測では約 8% 遅くなる)。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/jolt/CMakeLists.txt"
   OR EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/jolt/Build/CMakeLists.txt")
	message(STATUS "Jolt Physics found - enabling Jolt backend (cross-platform deterministic)")
	set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
	set(CROSS_PLATFORM_DETERMINISTIC ON CACHE BOOL "" FORCE)
	add_subdirectory(external/jolt/Build)
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_JOLT=1)
	target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} Jolt)
	if(MSVC AND TARGET Jolt)
		set_property(TARGET Jolt PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
	endif()
else()
	message(STATUS "Jolt Physics not found - PhysicsSystem3D and module physics queries are disabled (no-op)")
endif()

# ── Box2D v3 (2D 物理の既定バックエンド。physics/Box2DBridge.hpp) ───
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/box2d/CMakeLists.txt")
	message(STATUS "Box2D v3 found - enabling 2D physics")
	set(BOX2D_SAMPLES OFF CACHE BOOL "" FORCE)
	set(BOX2D_BENCHMARKS OFF CACHE BOOL "" FORCE)
	set(BOX2D_DOCS OFF CACHE BOOL "" FORCE)
	set(BOX2D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
	set(BOX2D_VALIDATE OFF CACHE BOOL "" FORCE)
	add_subdirectory(external/box2d)
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_BOX2D=1)
	target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} box2d)
else()
	message(STATUS "Box2D v3 not found - 2D physics bridge disabled (stub)")
endif()

# ── Recast/Detour (ナビメッシュ。nav/NavMeshBake.hpp と nav/NavMesh.hpp) ───
# mitiru 本体には足さず、使う target が link する:
#   mitiru_nav       … 焼いた blob を読んで経路を問い合わせる (Detour)。ゲーム DLL が link する
#   mitiru_nav_bake  … レベルのメッシュから blob を焼く (Recast)。mitiru_navbake とテストが link する
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/recastnavigation/Detour/Source/DetourNavMesh.cpp")
	message(STATUS "Recast/Detour found - enabling navmesh bake + query (mitiru_nav / mitiru_nav_bake)")
	set(_rn "${CMAKE_CURRENT_SOURCE_DIR}/external/recastnavigation")
	file(GLOB _mitiru_detour_sources CONFIGURE_DEPENDS "${_rn}/Detour/Source/*.cpp")
	file(GLOB _mitiru_recast_sources CONFIGURE_DEPENDS "${_rn}/Recast/Source/*.cpp")
	add_library(mitiru_detour STATIC ${_mitiru_detour_sources})
	target_include_directories(mitiru_detour PUBLIC $<BUILD_INTERFACE:${_rn}/Detour/Include>)
	add_library(mitiru_recast STATIC ${_mitiru_recast_sources})
	target_include_directories(mitiru_recast PUBLIC $<BUILD_INTERFACE:${_rn}/Recast/Include>)

	add_library(mitiru_nav INTERFACE)
	target_link_libraries(mitiru_nav INTERFACE mitiru_detour)
	target_compile_definitions(mitiru_nav INTERFACE MITIRU_HAS_DETOUR=1)

	add_library(mitiru_nav_bake INTERFACE)
	target_link_libraries(mitiru_nav_bake INTERFACE mitiru_nav mitiru_recast)
	target_compile_definitions(mitiru_nav_bake INTERFACE MITIRU_HAS_RECAST=1)
else()
	message(STATUS "Recast/Detour not found - navmesh disabled (git submodule update --init external/recastnavigation)")
endif()

# ── GekkoNet (ロールバック netcode。network/RollbackSession.hpp) — opt-in ──
# 決定論の GameMemory を memcpy で保存・復元できることを利用し、2 人以上の入力を予測して処理を進め、
# 予測が外れた場合は巻き戻して処理し直す。使う target が mitiru_rollback_net を link する。
# UDP は GekkoNet 同梱の asio を使わず、こちらの adapter (同じプロセス内の loopback など) で渡す。
option(MITIRU_WITH_GEKKONET "ロールバック netcode (GekkoNet) を build する (要 external/GekkoNet)" OFF)
if(MITIRU_WITH_GEKKONET)
	set(_gekko "${CMAKE_CURRENT_SOURCE_DIR}/external/GekkoNet/GekkoLib")
	if(NOT EXISTS "${_gekko}/include/gekkonet.h")
		message(FATAL_ERROR "MITIRU_WITH_GEKKONET=ON ですが external/GekkoNet がありません。\n"
			"  git submodule update --init external/GekkoNet を実行してから configure し直してください。")
	endif()
	file(GLOB _mitiru_gekko_sources CONFIGURE_DEPENDS "${_gekko}/src/*.cpp")
	add_library(mitiru_gekkonet STATIC ${_mitiru_gekko_sources})
	target_include_directories(mitiru_gekkonet
		PUBLIC $<BUILD_INTERFACE:${_gekko}/include>
		PRIVATE "${_gekko}/include/private" "${_gekko}/thirdparty")
	target_compile_definitions(mitiru_gekkonet PUBLIC GEKKONET_STATIC GEKKONET_NO_ASIO)
	target_compile_features(mitiru_gekkonet PUBLIC cxx_std_20)

	add_library(mitiru_rollback_net INTERFACE)
	target_link_libraries(mitiru_rollback_net INTERFACE mitiru_gekkonet)
	target_compile_definitions(mitiru_rollback_net INTERFACE MITIRU_HAS_GEKKONET=1)
	message(STATUS "GekkoNet enabled - rollback netcode (mitiru_rollback_net)")
endif()

# ── NativeEngine physics backend (周期境界を持つ自作剛体エンジン) — opt-in ──
# 他の物理 backend に無い能力: 端が反対側へ繋がる周期境界の世界と、決定的な結果。
# アダプタは include/mitiru/physics/NativePhysicsBridge.hpp (scene::ISystem)。
#
# 2 段構えの opt-in — configure で build するかを決め、link で「見えるか」を決める:
#   cmake --preset default -DMITIRU_USE_NATIVEPHYS=ON      # NativeEngine を build
#   target_link_libraries(mygame PRIVATE mitiru_nativephys)  # 使う target だけ
# mitiru 本体には何も足さない (define も include path も)。link しない target では
# アダプタは no-op のままなので、既存 build は ON にしても再ビルドされない。
#
# ソースの在り処 (上から順に探す):
#   -DMITIRU_NATIVEPHYS_ROOT=<path> / external/NativeEngine (submodule) / 環境変数 NATIVEENGINE_ROOT
option(MITIRU_USE_NATIVEPHYS "Build the NativeEngine physics backend (opt-in per target via mitiru_nativephys)" OFF)
set(MITIRU_NATIVEPHYS_ROOT "" CACHE PATH "Path to the NativeEngine repo root (empty = auto-detect)")
if(MITIRU_USE_NATIVEPHYS)
	set(_nativephys_root "${MITIRU_NATIVEPHYS_ROOT}")
	if(NOT _nativephys_root)
		if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/NativeEngine/src/world.cpp")
			set(_nativephys_root "${CMAKE_CURRENT_SOURCE_DIR}/external/NativeEngine")
		elseif(DEFINED ENV{NATIVEENGINE_ROOT})
			set(_nativephys_root "$ENV{NATIVEENGINE_ROOT}")
		endif()
	endif()

	if(EXISTS "${_nativephys_root}/src/world.cpp")
		message(STATUS "NativeEngine physics backend: ${_nativephys_root}")
		add_library(NativeEngine STATIC
			"${_nativephys_root}/src/world.cpp"
			"${_nativephys_root}/src/physics_world.cpp")
		target_include_directories(NativeEngine PUBLIC "${_nativephys_root}/src")
		target_compile_features(NativeEngine PUBLIC cxx_std_17)
		if(MSVC)
			target_compile_options(NativeEngine PRIVATE /utf-8)
		endif()

		# 使う target が link するもの。sgc 型の binding header (C++20) を提供する。
		add_library(mitiru_nativephys INTERFACE)
		target_link_libraries(mitiru_nativephys INTERFACE NativeEngine sgc::sgc)
		target_include_directories(mitiru_nativephys INTERFACE "${_nativephys_root}/mitiru")
		target_compile_definitions(mitiru_nativephys INTERFACE MITIRU_HAS_NATIVEPHYS=1)
		target_compile_features(mitiru_nativephys INTERFACE cxx_std_20)
	else()
		message(WARNING "MITIRU_USE_NATIVEPHYS=ON but NativeEngine sources not found"
			" (tried MITIRU_NATIVEPHYS_ROOT, external/NativeEngine, \$ENV{NATIVEENGINE_ROOT})")
	endif()
else()
	message(STATUS "NativeEngine physics backend disabled (-DMITIRU_USE_NATIVEPHYS=ON to build it)")
endif()

# ── クラッシュ報告 (sentry-native、breakpad backend) — opt-in ──
# game の callback 内のフォールトは常時有効な module/ModuleFaultGuard.hpp が処理する。こちらは
# host / engine 自身が落ちた時の minidump 用で、mitiru_host が target mitiru_crash_report を
# 見つけた時だけ link する (debug/CrashReporter.hpp)。既定ではローカルにのみ保存する。送信は
# MITIRU_CRASH_DSN を明示した時だけ行う。breakpad を選ぶのは、crashpad と違って handler exe を
# host の隣に配布せずに済み、単一 exe での配布を維持できるため。docs/CRASH_REPORTS.md を参照。
#   git submodule update --init external/sentry-native
#   git -C external/sentry-native submodule update --init external/breakpad
#   cmake --preset default -DMITIRU_WITH_CRASH_REPORT=ON
option(MITIRU_WITH_CRASH_REPORT "host のクラッシュを sentry-native (breakpad) で minidump に残す" OFF)
if(MITIRU_WITH_CRASH_REPORT)
	set(_sentry_root "${CMAKE_CURRENT_SOURCE_DIR}/external/sentry-native")
	if(NOT WIN32)
		message(WARNING "MITIRU_WITH_CRASH_REPORT は Windows のみ対応 (無視します)")
	elseif(NOT EXISTS "${_sentry_root}/external/breakpad/src/client/windows")
		message(WARNING "MITIRU_WITH_CRASH_REPORT=ON だが sentry-native / breakpad の submodule が無い"
			" (上のコメントの 2 行で取得する)")
	else()
		set(SENTRY_BACKEND "breakpad" CACHE STRING "" FORCE)
		set(SENTRY_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
		set(SENTRY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
		set(SENTRY_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
		add_subdirectory(external/sentry-native EXCLUDE_FROM_ALL)
		add_library(mitiru_crash_report INTERFACE)
		target_link_libraries(mitiru_crash_report INTERFACE sentry::sentry)
		target_compile_definitions(mitiru_crash_report INTERFACE MITIRU_HAS_SENTRY=1)
		message(STATUS "crash report: sentry-native (breakpad) — local only unless MITIRU_CRASH_DSN is set")
	endif()
endif()

# ── makina-core (optional) ────────────────────────────────────
# CSG ソリッドモデラーの headless なコア。header-only で依存が無いので、追加するのは include
# パスだけ。
#
# 依存は一方向だけ。makina-core はエンジンについて何も知らない (PLAN.md 3.1)。
# 逆向きにつなぐと、makina-core のテストすべての後ろに GPU とウィンドウが要ることになる。
#
# makina-core は <nlohmann/json.hpp> を include する。このツリーはすでに vendor しているので、include パス上に
# 2 つ目を置かず、エンジン側のコピーが使われる。
set(MITIRU_MAKINA_ROOT "" CACHE PATH "Path to makina-core (the CSG solid modeller core)")
set(_makina_root "")
foreach(_candidate "${MITIRU_MAKINA_ROOT}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/external/makina-core"
                   "${CMAKE_CURRENT_SOURCE_DIR}/../MakinaModeler/makina-core"
                   "${CMAKE_CURRENT_SOURCE_DIR}/../makina/makina-core")
	if(_candidate AND EXISTS "${_candidate}/include/makina/Eval.hpp")
		set(_makina_root "${_candidate}")
		break()
	endif()
endforeach()

if(_makina_root)
	message(STATUS "makina-core found at ${_makina_root} - CSG solids enabled")
	target_include_directories(mitiru ${MITIRU_TARGET_SCOPE}
		$<BUILD_INTERFACE:${_makina_root}/include>)
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_MAKINA=1)
else()
	message(STATUS "makina-core not found - CSG solids disabled"
		" (-DMITIRU_MAKINA_ROOT=<path> to enable)")
endif()

# ── Tracy Profiler (optional) ──────────────────────────────────
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/tracy/public/TracyClient.cpp")
	message(STATUS "Tracy found - enabling profiler")
	# 既定は OFF。ON にすると全ゲームと全テストで起動時に計測スレッドと待ち受けソケットが立ち、
	# 計測データが蓄積し続ける。計測する時だけ -DMITIRU_ENABLE_TRACY=ON。
	# TRACY_ENABLE は Tracy 側の変数なので、古いキャッシュに ON が残っていても上書きする
	option(MITIRU_ENABLE_TRACY "Tracy の計測を有効にする (計測する時だけ ON)" OFF)
	set(TRACY_ENABLE ${MITIRU_ENABLE_TRACY} CACHE BOOL "MITIRU_ENABLE_TRACY から設定する" FORCE)
	add_subdirectory(external/tracy)
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_TRACY=1)
	target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} TracyClient)
else()
	message(STATUS "Tracy not found - profiling macros will be no-ops")
endif()

# ── Zstandard (optional compression) ──────────────────────────
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/zstd/lib/zstd.h")
	message(STATUS "Zstandard found - enabling compression")
	set(MITIRU_ZSTD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/zstd")
	add_subdirectory(cmake/zstd)
	target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} mitiru_zstd)
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_ZSTD=1)
else()
	message(STATUS "Zstandard not found - compression will use passthrough fallback")
endif()

# ── efsw (ファイル監視: DLL / HTML / アセットのホットリロード) ───────
# efsw 同梱の CMake は 3.27 を要求するため、zstd と同様に src/ を直接まとめる。
# efsw がないビルドと Emscripten では asset/FileWatcher.hpp が mtime の全走査に切り替わる
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/efsw/include/efsw/efsw.hpp" AND NOT EMSCRIPTEN)
	message(STATUS "efsw found - enabling native file watching")
	set(_efsw "${CMAKE_CURRENT_SOURCE_DIR}/external/efsw/src/efsw")
	set(_mitiru_efsw_sources
		${_efsw}/Debug.cpp ${_efsw}/DirectorySnapshot.cpp ${_efsw}/DirectorySnapshotDiff.cpp
		${_efsw}/DirWatcherGeneric.cpp ${_efsw}/FileActionBatch.cpp ${_efsw}/FileInfo.cpp
		${_efsw}/FileSystem.cpp ${_efsw}/FileWatcher.cpp ${_efsw}/FileWatcherGeneric.cpp
		${_efsw}/FileWatcherImpl.cpp ${_efsw}/Log.cpp ${_efsw}/String.cpp ${_efsw}/System.cpp
		${_efsw}/Watcher.cpp ${_efsw}/WatcherGeneric.cpp)
	if(WIN32)
		list(APPEND _mitiru_efsw_sources
			${_efsw}/platform/win/FileSystemImpl.cpp ${_efsw}/platform/win/SystemImpl.cpp
			${_efsw}/FileWatcherWin32.cpp ${_efsw}/WatcherWin32.cpp)
	else()
		list(APPEND _mitiru_efsw_sources
			${_efsw}/platform/posix/FileSystemImpl.cpp ${_efsw}/platform/posix/SystemImpl.cpp)
	endif()
	if(APPLE)
		list(APPEND _mitiru_efsw_sources
			${_efsw}/FileWatcherFSEvents.cpp ${_efsw}/FileWatcherKqueue.cpp
			${_efsw}/WatcherFSEvents.cpp ${_efsw}/WatcherKqueue.cpp)
	elseif(CMAKE_SYSTEM_NAME MATCHES "Linux|Android")
		list(APPEND _mitiru_efsw_sources ${_efsw}/FileWatcherInotify.cpp ${_efsw}/WatcherInotify.cpp)
	elseif(CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
		list(APPEND _mitiru_efsw_sources ${_efsw}/FileWatcherKqueue.cpp ${_efsw}/WatcherKqueue.cpp)
	endif()
	add_library(mitiru_efsw STATIC ${_mitiru_efsw_sources})
	target_include_directories(mitiru_efsw
		PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/efsw/include>
		PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external/efsw/src)
	if(MSVC)
		target_compile_definitions(mitiru_efsw PRIVATE _SCL_SECURE_NO_WARNINGS)
		set_property(TARGET mitiru_efsw PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
	endif()
	if(APPLE)
		target_link_libraries(mitiru_efsw PRIVATE "-framework CoreFoundation" "-framework CoreServices")
	elseif(NOT WIN32)
		find_package(Threads REQUIRED)
		target_link_libraries(mitiru_efsw PRIVATE Threads::Threads)
	endif()
	target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} mitiru_efsw)
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_EFSW=1)
else()
	message(STATUS "efsw not found - file watching will poll mtimes")
endif()

# ── Taskflow (header-only: core/JobSystem.hpp のワーカースレッド) ─────
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/taskflow/taskflow/taskflow.hpp")
	message(STATUS "Taskflow found - enabling worker threads for JobSystem")
	target_include_directories(mitiru ${MITIRU_TARGET_SCOPE}
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/taskflow>)
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_TASKFLOW=1)
else()
	message(STATUS "Taskflow not found - JobSystem runs jobs inline")
endif()

# ── spdlog (vendored, header-only) ────────────────────────────
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/vendored/spdlog/spdlog.h")
	message(STATUS "spdlog (vendored) found - enabling structured logging")
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_SPDLOG=1 SPDLOG_HEADER_ONLY=1)
else()
	message(STATUS "spdlog not found - logging will use built-in ILogger")
endif()

# ── Ozz-Animation (optional skeletal animation) ───────────────
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/ozz/CMakeLists.txt")
	message(STATUS "Ozz-Animation found - enabling skeletal animation")
	set(ozz_build_samples OFF CACHE BOOL "" FORCE)
	set(ozz_build_howtos OFF CACHE BOOL "" FORCE)
	set(ozz_build_tests OFF CACHE BOOL "" FORCE)
	set(ozz_build_tools OFF CACHE BOOL "" FORCE)
	add_subdirectory(external/ozz)
	if(MSVC)
		foreach(target ozz_animation ozz_base)
			if(TARGET ${target})
				set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
				# Ozz upstream はディレクトリスコープで /WX を追加する
				# (external/ozz/build-utils/cmake/compiler_settings.cmake を参照)。
				# 親プロジェクトの CMAKE_CXX_FLAGS に /EHsc が含まれない場合
				# (テンプレート化された下流の consumer、/W4 への引き上げなど)、MSVC は C4530 を
				# エラーとして扱う。親側の設定に影響されないよう、これらの target に
				# /EHsc を明示的に指定する。
				target_compile_options(${target} PRIVATE /EHsc)
			endif()
		endforeach()
	endif()
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_OZZ=1)
	target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} ozz_animation ozz_base)
else()
	message(STATUS "Ozz-Animation not found - will use NullOzzAnimationSampler stub")
endif()

endif() # NOT EMSCRIPTEN (desktop-only optional libraries)

# DXC を使う exe の隣の dxc/ に dxcompiler.dll と dxil.dll を配置する (MITIRU_WITH_DXC=OFF なら何もしない)。
# MITIRU_WITH_SLANG=ON では Slang の DLL も slang/ に配置する (Slang は DXIL の最終段階で dxc/ を使う)。
function(mitiru_deploy_dxc target)
	if(NOT (MITIRU_WITH_DXC OR MITIRU_WITH_SLANG) OR NOT TARGET ${target})
		return()
	endif()
	add_custom_command(TARGET ${target} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/dxc"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${MITIRU_DXC_BIN}/dxcompiler.dll" "${MITIRU_DXC_BIN}/dxil.dll"
			"$<TARGET_FILE_DIR:${target}>/dxc"
		COMMENT "Deploying DXC next to ${target} (dxc/)")
	if(MITIRU_WITH_SLANG)
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/slang"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MITIRU_SLANG_DLLS} "$<TARGET_FILE_DIR:${target}>/slang"
			COMMENT "Deploying Slang next to ${target} (slang/)")
	endif()
endfunction()

# Windows 専用の backend と system library
if(WIN32)
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_DX11=1 MITIRU_HAS_DX12=1)
	target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE}
		d3d11 d3d12 dxgi d3dcompiler ws2_32 winmm xinput d3d12ma_impl)

	# ── DXC (opt-in): DX12 シェーダーを SM 6 (DXIL) でコンパイルする ──
	# DLL は tools/fetch_dxc.py が external/dxc/ に配置する (dxil.dll は MS ライセンスのため同梱しない)。
	# 実行時は exe の隣の dxc/ から絶対パスで読み込むため、exe の隣に別の版の同名 DLL があっても混同しない。
	option(MITIRU_WITH_DXC "DX12 のシェーダーを DXC (SM 6) でコンパイルする (要 python tools/fetch_dxc.py)" OFF)
	# ── Slang (opt-in、ADR 0047 のスパイク): DX12 シェーダーを Slang → DXIL にして VRT で画素を比較する ──
	# 既定のコンパイラは変更しない。DLL は tools/fetch_slang.py が external/slang/ に配置する。
	option(MITIRU_WITH_SLANG "DX12 のシェーダーを Slang 経由の DXIL でコンパイルする (要 python tools/fetch_slang.py と fetch_dxc.py)" OFF)
	if(MITIRU_WITH_DXC OR MITIRU_WITH_SLANG)
		set(MITIRU_DXC_BIN "${CMAKE_CURRENT_SOURCE_DIR}/external/dxc/bin/x64")
		if(NOT EXISTS "${MITIRU_DXC_BIN}/dxcompiler.dll" OR NOT EXISTS "${MITIRU_DXC_BIN}/dxil.dll")
			message(FATAL_ERROR
				"MITIRU_WITH_DXC / MITIRU_WITH_SLANG が ON ですが external/dxc に DXC がありません。\n"
				"  python tools/fetch_dxc.py を実行してから configure し直してください。")
		endif()
	endif()
	if(MITIRU_WITH_DXC)
		target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_DXC=1)
		# compileDx12Shader は inline であり、本体と UI 層で内容が食い違うと ODR 違反になる
		if(TARGET mitiru_ui_rml)
			target_compile_definitions(mitiru_ui_rml PRIVATE MITIRU_HAS_DXC=1)
		endif()
		message(STATUS "DXC enabled - DX12 shaders compile as SM 6.0 (${MITIRU_DXC_BIN})")
	endif()
	if(MITIRU_WITH_SLANG)
		set(MITIRU_SLANG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/slang")
		if(NOT EXISTS "${MITIRU_SLANG_ROOT}/bin/slang.dll" OR NOT EXISTS "${MITIRU_SLANG_ROOT}/include/slang.h")
			message(FATAL_ERROR
				"MITIRU_WITH_SLANG=ON ですが external/slang に Slang がありません。\n"
				"  python tools/fetch_slang.py を実行してから configure し直してください。")
		endif()
		# slang-llvm (CPU ターゲット用、80MB 超) と gfx は DXIL に不要なため配置しない
		set(MITIRU_SLANG_DLLS
			"${MITIRU_SLANG_ROOT}/bin/slang.dll" "${MITIRU_SLANG_ROOT}/bin/slang-compiler.dll"
			"${MITIRU_SLANG_ROOT}/bin/slang-glslang.dll" "${MITIRU_SLANG_ROOT}/bin/slang-glsl-module.dll"
			"${MITIRU_SLANG_ROOT}/bin/slang-rt.dll")
		target_include_directories(mitiru ${MITIRU_TARGET_SCOPE} $<BUILD_INTERFACE:${MITIRU_SLANG_ROOT}/include>)
		target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_SLANG=1)
		message(STATUS "Slang enabled - DX12 shaders compile through Slang to DXIL (${MITIRU_SLANG_ROOT})")
	endif()

	# ── Effekseer (opt-in): パーティクル演出を DX12 の 3D フレームに描く (render/dx12/EffekseerRuntime.hpp) ──
	# ゲームからは drawModel(path.efkefc, 位置, 回転, 倍率, key, 経過秒) で渡される。エフェクトを作成する
	# Effekseer のエディタは別のツールであり、エンジンには含めない。
	option(MITIRU_WITH_EFFEKSEER "Effekseer のエフェクトを DX12 で描く (要 external/Effekseer と LLGI)" OFF)
	if(MITIRU_WITH_EFFEKSEER)
		set(MITIRU_EFFEKSEER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/Effekseer")
		if(NOT EXISTS "${MITIRU_EFFEKSEER_ROOT}/Dev/Cpp/3rdParty/LLGI/src/LLGI.Graphics.h")
			message(FATAL_ERROR "MITIRU_WITH_EFFEKSEER=ON ですが external/Effekseer (と中の LLGI) がありません。\n"
				"  git submodule update --init --checkout --depth 1 external/Effekseer\n"
				"  git -C external/Effekseer submodule update --init --depth 1 Dev/Cpp/3rdParty/LLGI\n"
				"  を実行してから configure し直してください。")
		endif()
		add_subdirectory(cmake/effekseer)
		target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} mitiru_effekseer)
		target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_EFFEKSEER=1)
		message(STATUS "Effekseer enabled - .efkefc draws through drawModel (${MITIRU_EFFEKSEER_ROOT})")
	endif()

	# ── ONNX Runtime + DirectML EP (GPU ニューラル post-process, NeuralStyle.hpp) ──
	# 実体は IRenderer3D の内部 (host が DX12 レンダラを生成する側) でのみコンパイルされる。
	# ゲーム DLL は仮想呼び出しのみを行い、ORT シンボルを参照しない。
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/onnxruntime/lib/onnxruntime.lib")
		target_include_directories(mitiru ${MITIRU_TARGET_SCOPE}
			$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/onnxruntime/include>)
		target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE}
			"${CMAKE_CURRENT_SOURCE_DIR}/external/onnxruntime/lib/onnxruntime.lib")
		target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_ONNX=1)
	endif()

	# ── DirectML SDK (raw DirectML in-pipeline neural post-process, DX12DirectML.hpp) ──
	# RT を CPU との間で転送せずにテンソル化し、自前の DirectML グラフをパイプライン内で実行する。
	# DirectML.dll は ONNX Runtime 同梱版 (=1.15.4) を host がすでに deploy している。
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/directml/lib/DirectML.lib")
		target_include_directories(mitiru ${MITIRU_TARGET_SCOPE}
			$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/directml/include>)
		target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE}
			"${CMAKE_CURRENT_SOURCE_DIR}/external/directml/lib/DirectML.lib")
		target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_DIRECTML=1)
	endif()
endif()

# MSVC: /FS で並列コンパイル時の PDB ロックを回避し、/utf-8 で UTF-8 ソースを正しく解釈する
if(MSVC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /FS /utf-8")
endif()

# ── 配布ビルド設定 ──────────────────────────────────────────
# 配布形式は「動的 CRT (/MD) + VC ランタイムを zip に同梱」(tools/release/build_windows_zip.py)。
# prebuilt の Live2DCubismFrameworkD3D.lib が /MD 前提のため、
# 静的 CRT (/MT) は Cubism を外した構成でしかリンクできない (LNK2038)。必要な場合のみ明示的に選ぶ。
option(MITIRU_DIST "Enable distribution build (LTO, dynamic CRT)" OFF)
option(MITIRU_DIST_STATIC_CRT "Distribution build with static CRT (/MT). Requires Cubism disabled" OFF)

if(MITIRU_DIST_STATIC_CRT AND NOT MITIRU_DIST)
	message(FATAL_ERROR "MITIRU_DIST_STATIC_CRT は MITIRU_DIST=ON と一緒に指定してください")
endif()

if(MITIRU_DIST)
	if(MITIRU_DIST_STATIC_CRT)
		if(TARGET Live2DCubismFramework OR TARGET Live2DCubismFrameworkD3D)
			message(FATAL_ERROR "MITIRU_DIST_STATIC_CRT=ON は Cubism (prebuilt /MD) とリンクできません。"
				" Cubism を無効にするか、既定の動的 CRT 配布 (MITIRU_DIST_STATIC_CRT=OFF) を使ってください")
		endif()
		set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
		message(STATUS "Distribution build: static CRT, LTO enabled")
	else()
		message(STATUS "Distribution build: dynamic CRT (VC runtime は zip に同梱), LTO enabled")
	endif()

	if(MSVC)
		add_compile_options(/GL /O2)
		add_link_options(/LTCG)
	elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
		add_compile_options(-flto -O2)
		add_link_options(-flto)
	endif()
endif()

# ── Apps + Examples (トップレベルビルド時のみ) ──────────────────────
# apps/ = 製品・インフラ (host / launcher / tool ウィンドウ)、examples/ = ゲームサンプル。
# apps/ を先に処理する (examples/ の DLL 群が mitiru_host の場所を configure 時に参照する)。
# ゲームプロジェクトは別リポジトリで管理する (MitiruEngine はライブラリ)。
if(PROJECT_IS_TOP_LEVEL AND NOT EMSCRIPTEN)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/CMakeLists.txt")
		add_subdirectory(apps)
	endif()
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
		add_subdirectory(examples)
	endif()
endif()

# ── Tests ────────────────────────────────────────────────────────
if(PROJECT_IS_TOP_LEVEL)
	option(MITIRU_BUILD_TESTS "Build tests" ON)
else()
	option(MITIRU_BUILD_TESTS "Build tests" OFF)
endif()

if(MITIRU_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
	# EXISTS ガード: 公開 snapshot には tests/ を同梱しない。無条件に参照すると、
	# 公開リポジトリの top-level configure が失敗する (verify_snapshot_build.py が検出する種類の事故)。
	enable_testing()
	add_subdirectory(tests)
endif()

# ── Packaging (portable zip for v0.2.0 release) ───────────────────
# CPack は opt-in。CPack のテンプレートを同梱していない CMake 4.0 の一部の環境では、
# include(CPack) が "CPack.cmake:994" でエラーになる。ローカルの開発者はまず
# パッケージングを使わないので、既定は OFF にし、リリースのパイプラインで明示的に有効にする。
#
# レイアウトのターゲット。次の構成の MitiruEngine-<ver>-Windows-x64.zip を作る。
#
#   MitiruEngine/
#     mitiru_host.exe + assets/fonts/ + assets/ui/ (UI の書体と既定の RCSS)
#     mitiru_launcher/
#       mitiru_launcher.dll
#       assets/ui/main.rml
#     README.txt — "double-click mitiru_host.exe./mitiru_launcher/mitiru_launcher.dll"
#
# ユーザーは zip を展開し、起動用のエントリ (.bat /
# ショートカットで用意する) をダブルクリックすれば、ターミナルを使わずに済む。
option(MITIRU_ENABLE_CPACK "Generate CPack portable-zip configuration" OFF)
if(MITIRU_ENABLE_CPACK AND WIN32 AND PROJECT_IS_TOP_LEVEL)
	# ── install rules。dist ツリーを構成する ────────────────────────
	# Targets は最上位の dest に配置する (`bin/` prefix なし)。target ごとのサブディレクトリは DESTINATION で指定する。

	install(TARGETS mitiru_host         RUNTIME DESTINATION . COMPONENT runtime)

	# 任意: mogmog-0110/mitiru-cli の release から取得した mitiru CLI + installer。
	# CI は configure 前に build/extra_runtime/ 以下へ配置する。
	# ない場合 (初回実行 / dev builds) は、通知せずにこれらを含めず配布する。
	# 仕様: docs/INSTALLER.md
	if(EXISTS "${CMAKE_BINARY_DIR}/extra_runtime/mitiru.exe")
		install(PROGRAMS "${CMAKE_BINARY_DIR}/extra_runtime/mitiru.exe"
		        DESTINATION . COMPONENT runtime)
	endif()
	if(EXISTS "${CMAKE_BINARY_DIR}/extra_runtime/MitiruEngine_Installer.exe")
		install(PROGRAMS "${CMAKE_BINARY_DIR}/extra_runtime/MitiruEngine_Installer.exe"
		        DESTINATION . COMPONENT runtime)
	endif()

	# UI (RmlUi) が読む同梱の書体と RCSS。dev の build tree と同じく exe の隣の assets/ に置く。
	install(FILES
	        assets/fonts/MPLUSRounded1c-Regular.ttf
	        assets/fonts/MPLUSRounded1c-Bold.ttf
	        assets/fonts/PixelMplus12-Regular.ttf
	        assets/fonts/OFL.txt
	        assets/fonts/PixelMplus-README.txt
	        DESTINATION assets/fonts COMPONENT runtime)
	install(DIRECTORY assets/ui/ DESTINATION assets/ui COMPONENT runtime
	        FILES_MATCHING PATTERN "*.rcss")

	# launcher DLL はサブディレクトリに置き、DLL の隣の assets/ui/main.rml を host が見つけられるようにする
	# (dev の配置と一致させる)。
	if(TARGET mitiru_launcher)
		install(TARGETS mitiru_launcher RUNTIME DESTINATION mitiru_launcher COMPONENT runtime
		                                LIBRARY DESTINATION mitiru_launcher COMPONENT runtime)
		install(DIRECTORY apps/mitiru_launcher/assets/
		        DESTINATION mitiru_launcher/assets COMPONENT runtime)
	endif()

	# 最上位の簡易 launcher。コンソールウィンドウを一瞬表示することなく GUI launcher を
	# 実行する .bat file。ユーザーは host exe と DLL path を直接指定する代わりに、
	# このファイルをダブルクリックする。
	configure_file(
		${CMAKE_CURRENT_SOURCE_DIR}/cmake/dist/MitiruEngine_Launcher.bat.in
		${CMAKE_CURRENT_BINARY_DIR}/dist/MitiruEngine_Launcher.bat
		@ONLY)
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/dist/MitiruEngine_Launcher.bat
	        DESTINATION . COMPONENT runtime)

	configure_file(
		${CMAKE_CURRENT_SOURCE_DIR}/cmake/dist/README_DIST.txt.in
		${CMAKE_CURRENT_BINARY_DIR}/dist/README.txt
		@ONLY)
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/dist/README.txt
	        DESTINATION . COMPONENT runtime)

	# ── CPack settings ──────────────────────────────────────────────
	set(CPACK_PACKAGE_NAME "MitiruEngine")
	set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
	set(CPACK_PACKAGE_VENDOR "mogmog-0110")
	set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
	set(CPACK_PACKAGE_INSTALL_DIRECTORY "MitiruEngine-${PROJECT_VERSION}")
	set(CPACK_PACKAGE_FILE_NAME
		"MitiruEngine-${PROJECT_VERSION}-Windows-x64")
	set(CPACK_GENERATOR "ZIP")
	set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
	# `runtime` component (明示的にタグ付けしたすべてのもの) だけをパッケージに含める。
	# これにより、自動で取り込まれた submodule の install rules (sgc headers など) と、
	# runtime files ではない static lib を除外する。
	# 必要な組み合わせ: COMPONENT_INSTALL=ON で絞り込みを有効にし、
	# ALL_COMPONENTS_IN_ONE で再び 1 つの zip にまとめる。
	set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
	set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
	set(CPACK_COMPONENTS_ALL runtime)
	# 最上位ディレクトリの prefix を手動で追加する。COMPONENT mode では、既定の
	# CPACK_INCLUDE_TOPLEVEL_DIRECTORY の動作により prefix がなくなる。この設定がないと、
	# すべての file がユーザーの current directory に展開される (使い勝手が悪い)。
	set(CPACK_PACKAGING_INSTALL_PREFIX "/MitiruEngine-${PROJECT_VERSION}")
	include(CPack)
endif()
