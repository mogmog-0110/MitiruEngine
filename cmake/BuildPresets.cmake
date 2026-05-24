# BuildPresets.cmake
# Platform-specific build preset configurations for MitiruEngine.
#
# Usage (from root CMakeLists.txt):
#   include(cmake/BuildPresets.cmake)
#   mitiru_apply_preset()          # auto-detect
#   mitiru_apply_preset(windows_release)  # explicit
#
# Available presets:
#   windows_debug, windows_release
#   linux_debug, linux_release
#   wasm_release

# ── Windows Debug ────────────────────────────────────────────
function(preset_windows_debug)
	if(NOT MSVC)
		message(WARNING "preset_windows_debug: MSVC expected but not detected")
		return()
	endif()

	set(CMAKE_BUILD_TYPE Debug PARENT_SCOPE)

	target_compile_options(mitiru ${MITIRU_TARGET_SCOPE}
		/W4            # High warning level
		/WX-           # Warnings are not errors (debug flexibility)
		/Zi            # Debug info
		/Od            # No optimization
		/utf-8         # UTF-8 source
		/FS            # Parallel PDB writes
		/bigobj        # Large object files (template-heavy code)
		/EHsc          # Standard C++ exceptions
		/permissive-   # Strict conformance
		/Zc:__cplusplus  # Correct __cplusplus macro
	)

	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE}
		MITIRU_DEBUG=1
		_DEBUG
		WIN32_LEAN_AND_MEAN
		NOMINMAX
	)
endfunction()

# ── Windows Release ──────────────────────────────────────────
function(preset_windows_release)
	if(NOT MSVC)
		message(WARNING "preset_windows_release: MSVC expected but not detected")
		return()
	endif()

	set(CMAKE_BUILD_TYPE Release PARENT_SCOPE)

	target_compile_options(mitiru ${MITIRU_TARGET_SCOPE}
		/W4
		/WX            # Warnings as errors
		/O2            # Maximize speed
		/GL            # Whole program optimization
		/utf-8
		/FS
		/bigobj
		/EHsc
		/permissive-
		/Zc:__cplusplus
		/DNDEBUG
	)

	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE}
		MITIRU_RELEASE=1
		NDEBUG
		WIN32_LEAN_AND_MEAN
		NOMINMAX
	)

	target_link_options(mitiru ${MITIRU_TARGET_SCOPE}
		/LTCG          # Link-time code generation
	)
endfunction()

# ── Linux Debug ──────────────────────────────────────────────
function(preset_linux_debug)
	set(CMAKE_BUILD_TYPE Debug PARENT_SCOPE)

	target_compile_options(mitiru ${MITIRU_TARGET_SCOPE}
		-Wall
		-Wextra
		-Wpedantic
		-Wno-unused-parameter
		-g3              # Full debug info
		-O0              # No optimization
		-fno-omit-frame-pointer  # Better stack traces
	)

	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE}
		MITIRU_DEBUG=1
		_DEBUG
	)
endfunction()

# ── Linux Release ────────────────────────────────────────────
function(preset_linux_release)
	set(CMAKE_BUILD_TYPE Release PARENT_SCOPE)

	target_compile_options(mitiru ${MITIRU_TARGET_SCOPE}
		-Wall
		-Wextra
		-Wpedantic
		-Werror         # Warnings as errors
		-O2             # Optimize for speed
		-DNDEBUG
		-flto           # Link-time optimization
		-ffunction-sections
		-fdata-sections
	)

	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE}
		MITIRU_RELEASE=1
		NDEBUG
	)

	target_link_options(mitiru ${MITIRU_TARGET_SCOPE}
		-flto
		-Wl,--gc-sections   # Strip unused sections
	)
endfunction()

# ── WASM Release ─────────────────────────────────────────────
function(preset_wasm_release)
	set(CMAKE_BUILD_TYPE Release PARENT_SCOPE)

	target_compile_options(mitiru ${MITIRU_TARGET_SCOPE}
		-Wall
		-Wextra
		-O2              # Optimize for speed (Oz for size)
		-DNDEBUG
		-flto
	)

	target_compile_definitions(mitiru ${MITIRU_TARGET_SCOPE}
		MITIRU_RELEASE=1
		MITIRU_PLATFORM_WEB=1
		NDEBUG
	)

	target_link_options(mitiru ${MITIRU_TARGET_SCOPE}
		-O2
		-flto
		--closure=1      # Closure compiler on JS glue
	)
endfunction()

# ── Auto-detection ───────────────────────────────────────────
# Call mitiru_apply_preset() without arguments to auto-detect,
# or pass one of: windows_debug, windows_release, linux_debug,
# linux_release, wasm_release.
function(mitiru_apply_preset)
	# Explicit preset
	if(ARGC GREATER 0)
		set(PRESET_NAME ${ARGV0})
	else()
		# Auto-detect from platform + build type
		if(EMSCRIPTEN)
			set(PRESET_NAME "wasm_release")
		elseif(WIN32)
			if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
				set(PRESET_NAME "windows_release")
			else()
				set(PRESET_NAME "windows_debug")
			endif()
		elseif(UNIX)
			if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
				set(PRESET_NAME "linux_release")
			else()
				set(PRESET_NAME "linux_debug")
			endif()
		else()
			message(STATUS "BuildPresets: No matching preset for this platform")
			return()
		endif()
	endif()

	message(STATUS "BuildPresets: Applying preset '${PRESET_NAME}'")

	if(PRESET_NAME STREQUAL "windows_debug")
		preset_windows_debug()
	elseif(PRESET_NAME STREQUAL "windows_release")
		preset_windows_release()
	elseif(PRESET_NAME STREQUAL "linux_debug")
		preset_linux_debug()
	elseif(PRESET_NAME STREQUAL "linux_release")
		preset_linux_release()
	elseif(PRESET_NAME STREQUAL "wasm_release")
		preset_wasm_release()
	else()
		message(WARNING "BuildPresets: Unknown preset '${PRESET_NAME}'")
	endif()
endfunction()
