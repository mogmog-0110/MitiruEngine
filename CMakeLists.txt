cmake_minimum_required(VERSION 3.21)

# CMP0091: MSVC runtime library policy (needed for MITIRU_DIST static CRT)
cmake_policy(SET CMP0091 NEW)

project(MitiruEngine
	VERSION 0.7.4
	DESCRIPTION "C++ ゲームエンジン"
	LANGUAGES CXX
)

# ── ビルドモード切替 ─────────────────────────────────────────────
# ON (デフォルト): INTERFACE ライブラリ — 従来通りの header-only 動作。
# OFF: STATIC ライブラリ — Phase 2 以降でモジュールを .cpp に分離する際に使用。
# コンシューマー側はこのオプションを設定しない限り動作変化なし。
option(MITIRU_HEADER_ONLY "Build engine as header-only INTERFACE library" ON)

# ── ネットワーク (GameNetworkingSockets) opt-in ────────────────
# OFF (デフォルト): NullReliableTransport スタブのみ。consumer 影響ゼロ。
# ON: external/GameNetworkingSockets + protobuf + libsodium を submodule から build。
#     有効化前に: git submodule update --init --recursive external/GameNetworkingSockets external/protobuf external/libsodium
# 詳細: docs/NETWORK_GNS.md
option(MITIRU_ENABLE_GNS "Build with GameNetworkingSockets (requires external/GameNetworkingSockets and deps)" OFF)

# Prevent in-source builds
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
	message(FATAL_ERROR "In-source builds are not allowed. Use: cmake --preset default")
endif()

# ── Third-party dependencies ────────────────────────────────

# ShiggyGameCore (sgc) — git submodule at external/sgc
set(SGC_BUILD_TESTS OFF CACHE INTERNAL "")
set(SGC_BUILD_EXAMPLES OFF CACHE INTERNAL "")
add_subdirectory(external/sgc)

# MitiruMML — git submodule at external/mml
set(MITIRU_MML_BUILD_TESTS OFF CACHE INTERNAL "")
add_subdirectory(external/mml)

# ── CEF (Chromium Embedded Framework) ───────────────────────────
# 事前準備: python tools/fetch_cef.py
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

# MITIRU_DISABLE_CEF=ON: build engine without CEF (pure Mode A).
# Consumer exe needs no libcef.dll, *.pak, locales/ or MitiruCefHelper.exe.
# Default OFF — existing behaviour is bit-identical when this is not set.
option(MITIRU_DISABLE_CEF
    "Build engine without CEF support (consumer needs no libcef.dll). \
Useful for pure Mode A games (headless / console / 3D action / tools)."
    OFF)

if(NOT MITIRU_DISABLE_CEF)
    if(WIN32)
        find_package(CEF QUIET)
    endif()
else()
    message(STATUS "MITIRU_DISABLE_CEF=ON — building without CEF support (NullCefContext only)")
endif()

# G-10: mitiru_add_cef_game() マクロ — ゲーム側 CMakeLists の CEF 配置 100+ 行
# を 1 行で済ませる helper。定義だけ load (マクロは呼ばれた時だけ何かする)。
include(MitiruCef)

# ── GameNetworkingSockets (opt-in via MITIRU_ENABLE_GNS) ────────
if(MITIRU_ENABLE_GNS)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/GameNetworkingSockets/CMakeLists.txt")
        message(FATAL_ERROR
            "MITIRU_ENABLE_GNS=ON ですが external/GameNetworkingSockets が空です。\n"
            "  git submodule update --init --recursive external/GameNetworkingSockets external/protobuf external/libsodium\n"
            "詳細: docs/NETWORK_GNS.md")
    endif()

    # protobuf — GNS の必須依存
    set(protobuf_BUILD_TESTS OFF CACHE INTERNAL "")
    set(protobuf_BUILD_PROTOC_BINARIES ON CACHE INTERNAL "")
    set(protobuf_INSTALL OFF CACHE INTERNAL "")
    add_subdirectory(external/protobuf EXCLUDE_FROM_ALL)

    # GNS 本体: Windows なら BCrypt 既定 (libsodium 不要), それ以外は libsodium
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

# ── miniaudio implementation (static library) ───────────────────
# Emscripten では WebAudioEngine を使うため miniaudio は不要
if(NOT EMSCRIPTEN)
	add_library(miniaudio_impl STATIC src/miniaudio_impl.cpp)
	target_include_directories(miniaudio_impl PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external>
	)
	target_compile_features(miniaudio_impl PUBLIC cxx_std_20)
	if(MSVC)
		target_compile_options(miniaudio_impl PRIVATE /W0 /FS)
	endif()
endif()

# ── メインライブラリ (header-only / STATIC 切替) ──────────────────
if(MITIRU_HEADER_ONLY)
    add_library(mitiru INTERFACE)
    set(MITIRU_TARGET_SCOPE INTERFACE)
else()
    # STATIC モード: Phase 2 でモジュールの .cpp を順次追加していく。
    # プレースホルダーは Phase 1 時の最小翻訳単位。
    # Engine.cpp は 3-F Phase 2 pilot — Engine.hpp の out-of-class 定義 (~6 detail headers) を
    # 1 つの TU に集約する。
    add_library(mitiru STATIC
        src/mitiru_static_placeholder.cpp
        src/core/Engine.cpp
    )
    set(MITIRU_TARGET_SCOPE PUBLIC)
endif()

add_library(mitiru::mitiru ALIAS mitiru)
# Capitalized namespace alias (preferred for downstream find_package / FetchContent usage)
add_library(Mitiru::mitiru ALIAS mitiru)

# MITIRU_HEADER_ONLY を compile definition としてヘッダー側に伝搬する
if(MITIRU_HEADER_ONLY)
    target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HEADER_ONLY=1)
endif()

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
	target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} sgc::sgc mitiru_mml::mitiru_mml stb_impl tinyobj_impl)
	target_link_options(mitiru ${MITIRU_TARGET_SCOPE}
		-sUSE_WEBGL2=1
		-sFULL_ES3=1
		-sALLOW_MEMORY_GROWTH=1
		-sEXPORTED_RUNTIME_METHODS=ccall,cwrap
		-sINVOKE_RUN=1
	)

	# WebGPU support (opt-in via -DMITIRU_ENABLE_WEBGPU=ON)
	option(MITIRU_ENABLE_WEBGPU "Enable WebGPU backend for Emscripten" OFF)
	if(MITIRU_ENABLE_WEBGPU)
		message(STATUS "WebGPU backend enabled")
		target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_WEBGPU=1)
		target_link_options(mitiru ${MITIRU_TARGET_SCOPE} -sUSE_WEBGPU=1)
	endif()
else()
	target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} sgc::sgc mitiru_mml::mitiru_mml stb_impl tinyobj_impl miniaudio_impl)

	# MITIRU_ENABLE_GNS=ON 時のみ GNS をリンク + MITIRU_HAS_GNS=1 を伝搬
	if(MITIRU_ENABLE_GNS)
		target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} GameNetworkingSockets::shared)
		target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_GNS=1)
	endif()
endif()

# MSVC: UTF-8ソースエンコーディング（INTERFACEで利用者にも伝搬）
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
	# CEF が見つかった場合は mitiru に MITIRU_HAS_CEF を伝搬する
	# (MITIRU_DISABLE_CEF=ON の場合は CEF_FOUND が false のため、このブロックは実行されない)
	if(CEF_FOUND AND NOT MITIRU_DISABLE_CEF)
		message(STATUS "CEF enabled — MITIRU_HAS_CEF=1")
		target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_CEF=1)
		target_include_directories(mitiru ${MITIRU_TARGET_SCOPE} "${CEF_ROOT}" "${CEF_ROOT}/include")
		target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} CEF::libcef CEF::libcef_dll_wrapper)

		# ── libcef_dll_wrapper_mdrel (Release CRT 版ラッパー) ──────
		# CEF minimal 配布の libcef.dll は Release CRT (/MD) 固定。
		# subprocess exe は深い初期化 (CefExecuteProcess) を行うため
		# CRT を libcef と揃える必要がある (mismatch なら
		# "GPU process launch failed: error_code=63" で FATAL 終了)。
		# 主 exe と Debug ビルドで共有される libcef_dll_wrapper (/MDd) とは
		# 別に、subprocess 専用の /MD 版を静的ビルドする。
		file(GLOB_RECURSE _WRAP_SRCS_MDREL
			"${CEF_ROOT}/libcef_dll/*.cc"
			"${CEF_ROOT}/libcef_dll/*.cpp")
		add_library(libcef_dll_wrapper_mdrel STATIC ${_WRAP_SRCS_MDREL})
		target_include_directories(libcef_dll_wrapper_mdrel PUBLIC
			"${CEF_ROOT}" "${CEF_ROOT}/include")
		target_compile_features(libcef_dll_wrapper_mdrel PUBLIC cxx_std_17)
		target_compile_definitions(libcef_dll_wrapper_mdrel PRIVATE
			USING_CEF_SHARED NOMINMAX WIN32_LEAN_AND_MEAN WRAPPING_CEF_SHARED)
		target_link_libraries(libcef_dll_wrapper_mdrel PUBLIC CEF::libcef)
		# Release CRT (/MD) を CMP0091 のプロパティで宣言する。手動 /MD フラグだと
		# CMake が自動注入する既定 /MDd (Debug) と二重になり D9025 が出る。
		target_compile_options(libcef_dll_wrapper_mdrel PRIVATE
			/W0 /wd4100 /wd4127 /wd4996 /bigobj /utf-8 /FS)
		set_target_properties(libcef_dll_wrapper_mdrel PROPERTIES
			FOLDER "cef"
			MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

		# ── MitiruCefHelper.exe (CEF renderer/gpu/utility subprocess) ──
		# MitiruCefConfig::buildCefSettings は既定で exeDir/MitiruCefHelper.exe を
		# browser_subprocess_path に指定する。ゲーム側 exe と同じフォルダに配置する。
		# サブシステムは指定しない (= CONSOLE)。Chromium は主 exe と subprocess の
		# サブシステムが揃っていることを前提にしており、ゲーム側 exe を将来
		# WINDOWS に切り替える場合は両方を同時に変更する必要がある。
		add_executable(MitiruCefHelper src/cef_subprocess_main.cpp)
		target_link_libraries(MitiruCefHelper PRIVATE
			CEF::libcef libcef_dll_wrapper_mdrel)
		target_compile_definitions(MitiruCefHelper PRIVATE
			MITIRU_HAS_CEF=1 NOMINMAX WIN32_LEAN_AND_MEAN)
		target_include_directories(MitiruCefHelper PRIVATE
			"${CEF_ROOT}" "${CEF_ROOT}/include")
		# subprocess は libcef と同じ Release CRT。プロパティで /MD を宣言し
		# 手動フラグによる D9025 (/MDd→/MD 上書き) を避ける。
		set_target_properties(MitiruCefHelper PROPERTIES
			FOLDER "cef"
			MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
	endif()
elseif(APPLE)
	message(STATUS "Building for macOS/iOS")
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_PLATFORM_APPLE=1)
elseif(UNIX)
	message(STATUS "Building for Linux/Unix")
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_PLATFORM_UNIX=1)

	# PulseAudio detection (Linux audio output backend)
	find_library(PULSEAUDIO_SIMPLE_LIBRARY pulse-simple)
	find_library(PULSEAUDIO_LIBRARY pulse)
	find_path(PULSEAUDIO_INCLUDE_DIR pulse/simple.h)

	if(PULSEAUDIO_SIMPLE_LIBRARY AND PULSEAUDIO_LIBRARY AND PULSEAUDIO_INCLUDE_DIR)
		message(STATUS "PulseAudio found: ${PULSEAUDIO_SIMPLE_LIBRARY}")
		target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_PULSEAUDIO=1)
		target_include_directories(mitiru ${MITIRU_TARGET_SCOPE} ${PULSEAUDIO_INCLUDE_DIR})
		target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} ${PULSEAUDIO_SIMPLE_LIBRARY} ${PULSEAUDIO_LIBRARY})
	else()
		message(STATUS "PulseAudio not found - audio output will use NullAudioOutput")
	endif()
endif()

# ── Optional backend detection (Emscripten ではスキップ) ────
if(NOT EMSCRIPTEN)
find_package(SDL2 2.0.10 QUIET)
if(SDL2_FOUND)
	message(STATUS "SDL2 found (${SDL2_VERSION}) - enabling SDL2 backend")
	# SDL_MAIN_HANDLED: console app (mitiru_host が main を持つ) のため SDL2 に main を奪わせない。
	# 副次効果として SDL2main.lib への link が不要になる。
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_SDL2=1 SDL_MAIN_HANDLED=1)
	target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} SDL2::SDL2)
endif()

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

# ── Desktop-only optional libraries (Emscriptenではスキップ) ──
if(NOT EMSCRIPTEN)

# ── GLEW (OpenGL Extension Wrangler) ─────────────────────────
# Try system GLEW first, then fallback to vendored copy
find_package(GLEW QUIET)
if(NOT GLEW_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/glew/include/GL/glew.h")
	message(STATUS "GLEW not found via find_package - using vendored copy")
	add_library(GLEW::GLEW STATIC IMPORTED)
	if(WIN32)
		# Architecture detection for vendored GLEW library
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
# Auto-detect SDK version directory under external/cubism/
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

# ── Jolt Physics (optional 3D physics engine) ─────────────────
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/jolt/CMakeLists.txt"
   OR EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/jolt/Build/CMakeLists.txt")
	message(STATUS "Jolt Physics found - enabling Jolt backend")
	set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
	add_subdirectory(external/jolt/Build)
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_JOLT=1)
	target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} Jolt)
	if(MSVC AND TARGET Jolt)
		set_property(TARGET Jolt PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
	endif()
else()
	message(STATUS "Jolt Physics not found - 3D physics will use NullJoltPhysicsWorld stub")
endif()

# ── Box2D v3 (optional, used by mitiru::cef::PhysicsBridge) ───
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/box2d/CMakeLists.txt")
	message(STATUS "Box2D v3 found - enabling 2D physics bridge")
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

# ── Tracy Profiler (optional) ──────────────────────────────────
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/tracy/public/TracyClient.cpp")
	message(STATUS "Tracy found - enabling profiler")
	option(TRACY_ENABLE "" ON)
	add_subdirectory(external/tracy)
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_TRACY=1)
	target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE} TracyClient)
else()
	message(STATUS "Tracy not found - profiling macros will be no-ops")
endif()

# ── Zstandard (optional compression) ──────────────────────────
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/zstd/lib/zstd.h")
	message(STATUS "Zstandard found - enabling compression")
	target_include_directories(mitiru ${MITIRU_TARGET_SCOPE}
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/zstd/lib>)
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_ZSTD=1)
else()
	message(STATUS "Zstandard not found - compression will use passthrough fallback")
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
				# Ozz upstream injects /WX at directory scope (see
				# external/ozz/build-utils/cmake/compiler_settings.cmake). When the
				# parent project's CMAKE_CXX_FLAGS doesn't carry /EHsc (templated
				# downstream consumers, /W4 escalation, etc.), MSVC turns C4530 into
				# a hard error. Force /EHsc on these specific targets so it doesn't
				# matter what the parent did.
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

# Windows-only backends + system libraries
if(WIN32)
	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE} MITIRU_HAS_DX11=1 MITIRU_HAS_DX12=1)
	target_link_libraries(mitiru ${MITIRU_TARGET_SCOPE}
		d3d11 d3d12 dxgi d3dcompiler ws2_32 winmm xinput)
endif()

# MSVC: /FSで並列コンパイル時のPDBロックを回避、/utf-8でUTF-8ソースを正しく解釈
if(MSVC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /FS /utf-8")
endif()

# ── 配布ビルド設定 ──────────────────────────────────────────
option(MITIRU_DIST "Enable distribution build (static CRT, LTO, no console)" OFF)

if(MITIRU_DIST)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	message(STATUS "Distribution build: static CRT, LTO enabled")

	if(MSVC)
		add_compile_options(/GL /O2)
		add_link_options(/LTCG)
	elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
		add_compile_options(-flto -O2)
		add_link_options(-flto)
	endif()
endif()

# Examples removed — all demos/studio migrated out (CEF integration pending)

# ── Examples (トップレベルビルド時のみ) ─────────────────────────────
# ゲームプロジェクトは別リポジトリで管理する (MitiruEngine はライブラリ)。
if(PROJECT_IS_TOP_LEVEL AND NOT EMSCRIPTEN)
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

if(MITIRU_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()

# ── Packaging (portable zip for v0.2.0 release) ───────────────────
# CPack is opt-in: include(CPack) errors with "CPack.cmake:994" on some CMake 4.0
# installs that ship without the CPack template. Local devs almost never want
# packaging, so default OFF and let release pipelines explicitly enable it.
#
# Layout target — produces MitiruEngine-<ver>-Windows-x64.zip with:
#
#   MitiruEngine/
#     mitiru_host.exe + MitiruCefHelper.exe + libcef.dll + *.pak + locales/
#     mitiru_launcher/
#       mitiru_launcher.dll
#       assets/scene.html + mitiru_runtime/mitiru_cef_state.js
#     hello_game/
#       hello_game.dll
#       assets/scene.html + mitiru_runtime/mitiru_cef_state.js
#     README.txt — "double-click mitiru_host.exe ./mitiru_launcher/mitiru_launcher.dll"
#
# The user unzips, double-clicks the launcher entry (provided via .bat /
# shortcut), and is in a no-terminal experience.
option(MITIRU_ENABLE_CPACK "Generate CPack portable-zip configuration" OFF)
if(MITIRU_ENABLE_CPACK AND WIN32 AND PROJECT_IS_TOP_LEVEL)
	# ── install rules — lay out the dist tree ────────────────────────
	# Targets land at top-level dest (no `bin/` prefix); subdir-per-target via DESTINATION.

	install(TARGETS mitiru_host         RUNTIME DESTINATION . COMPONENT runtime)
	if(TARGET MitiruCefHelper)
		install(TARGETS MitiruCefHelper RUNTIME DESTINATION . COMPONENT runtime)
	endif()

	# Optional: mitiru CLI + installer pulled from the mogmog-0110/mitiru-cli
	# release. CI stages them under build/extra_runtime/ before configure.
	# When absent (first run / dev builds) we silently ship without them.
	# Spec: docs/INSTALLER.md
	if(EXISTS "${CMAKE_BINARY_DIR}/extra_runtime/mitiru.exe")
		install(PROGRAMS "${CMAKE_BINARY_DIR}/extra_runtime/mitiru.exe"
		        DESTINATION . COMPONENT runtime)
	endif()
	if(EXISTS "${CMAKE_BINARY_DIR}/extra_runtime/MitiruEngine_Installer.exe")
		install(PROGRAMS "${CMAKE_BINARY_DIR}/extra_runtime/MitiruEngine_Installer.exe"
		        DESTINATION . COMPONENT runtime)
	endif()

	# Game / launcher DLLs go in subdirectories so cefStartUrl resolves
	# relative to host cwd (matching the dev layout).
	if(TARGET hello_game)
		install(TARGETS hello_game RUNTIME DESTINATION hello_game COMPONENT runtime
		                            LIBRARY DESTINATION hello_game COMPONENT runtime)
		install(DIRECTORY examples/hello_game/assets/
		        DESTINATION hello_game/assets COMPONENT runtime
		        PATTERN "*.html"
		        PATTERN "*.css"
		        PATTERN "*.js"
		        PATTERN "*.json")
		install(FILES web/mitiru_runtime/mitiru_cef_state.js
		        DESTINATION hello_game/assets/mitiru_runtime COMPONENT runtime)
	endif()

	if(TARGET mitiru_launcher)
		install(TARGETS mitiru_launcher RUNTIME DESTINATION mitiru_launcher COMPONENT runtime
		                                LIBRARY DESTINATION mitiru_launcher COMPONENT runtime)
		install(DIRECTORY examples/mitiru_launcher/assets/
		        DESTINATION mitiru_launcher/assets COMPONENT runtime
		        PATTERN "*.html"
		        PATTERN "*.css"
		        PATTERN "*.js"
		        PATTERN "*.json")
		install(FILES web/mitiru_runtime/mitiru_cef_state.js
		        DESTINATION mitiru_launcher/assets/mitiru_runtime COMPONENT runtime)
	endif()

	# CEF runtime — libcef.dll + chrome_elf.dll + *.bin + .pak + locales/
	# Exclude cef_sandbox.lib (96MB compile-only static lib, not a runtime asset).
	if(CEF_FOUND)
		install(DIRECTORY ${CEF_BINARY_DIR}/   DESTINATION . COMPONENT runtime
		        PATTERN "cef_sandbox.lib" EXCLUDE
		        PATTERN "libcef.lib"      EXCLUDE)
		install(DIRECTORY ${CEF_RESOURCE_DIR}/ DESTINATION . COMPONENT runtime)
	endif()

	# Top-level convenience launcher: a .bat file that runs the GUI launcher
	# without a console window flash. Users double-click this instead of the
	# bare host exe + DLL path.
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
	# Only pack the `runtime` component (everything we explicitly tagged).
	# This drops the auto-imported submodule install rules (sgc headers, etc.)
	# and any libcef.lib / cef_sandbox.lib that aren't runtime files.
	# Combination needed: COMPONENT_INSTALL=ON enables filtering;
	# ALL_COMPONENTS_IN_ONE merges them back into a single zip.
	set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
	set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
	set(CPACK_COMPONENTS_ALL runtime)
	# Manually add the toplevel dir prefix — with COMPONENT mode the default
	# CPACK_INCLUDE_TOPLEVEL_DIRECTORY behaviour drops it. Without this every
	# file unzips to the user's current directory (bad UX).
	set(CPACK_PACKAGING_INSTALL_PREFIX "/MitiruEngine-${PROJECT_VERSION}")
	include(CPack)
endif()
