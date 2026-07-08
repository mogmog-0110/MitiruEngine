# MitiruAssets.cmake — artist asset pipeline (F-09)
#
# Drop .psd files into a source directory; get composited .png files in the
# build tree, watched and rebuilt on every edit.  Export runs at build time
# via tools/psd_export.py (psd-tools preferred, ImageMagick fallback).
#
# ── Usage ───────────────────────────────────────────────────────────────
#
#   include(${MitiruEngine_DIR}/cmake/MitiruAssets.cmake)
#
#   mitiru_assets(my_game_art
#       SOURCE_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/assets-src
#       OUT_DIR     ${CMAKE_CURRENT_BINARY_DIR}/assets/images
#   )
#
#   # Wire the output dir into your game at build time.
#   add_dependencies(my_game my_game_art)
#
# Or, one-line convenience that both creates the aggregate target AND
# attaches it to an existing target:
#
#   mitiru_assets(my_game_art
#       SOURCE_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/assets-src
#       OUT_DIR     $<TARGET_FILE_DIR:my_game>/assets
#       TARGET      my_game
#   )
#
# ── Options ─────────────────────────────────────────────────────────────
#
#   TARGET      <existing-target>   optional, add_dependencies() wires it in
#   SOURCE_DIR  <dir>                required, scanned recursively for *.psd
#   OUT_DIR     <dir>                required, PNGs mirrored here
#   EXTENSION   png|webp|jpg         default png
#   PSD_TOOL    <python-exe>         override Python interpreter
#   QUIET                            suppress per-file "up-to-date" lines
#
# ── Behaviour ───────────────────────────────────────────────────────────
#
#   * Globs **/*.psd under SOURCE_DIR at configure time.  Each .psd becomes
#     <OUT_DIR>/<relative-subpath>/<name>.<EXTENSION>.
#   * A per-file add_custom_command generates the output, declared with a
#     DEPENDS on the source PSD and the exporter script so edits to either
#     trigger a rebuild.
#   * If no PSD files are found the aggregate target is still created but
#     has no outputs — a warning is emitted so typos in SOURCE_DIR surface.
#   * If psd_export.py's --check probe fails (no psd-tools AND no
#     ImageMagick) the configure step prints a warning but does NOT fail;
#     builds that actually touch a PSD will then surface the error.

include_guard(DIRECTORY)

# ── locate the exporter script ─────────────────────────────────────────
# This file lives in <engine>/cmake/, the exporter lives in <engine>/tools/.
get_filename_component(_mitiru_assets_engine_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_MITIRU_PSD_EXPORT "${_mitiru_assets_engine_root}/tools/psd_export.py"
    CACHE INTERNAL "Path to psd_export.py" FORCE)

if(NOT EXISTS "${_MITIRU_PSD_EXPORT}")
    message(WARNING
        "MitiruAssets: exporter not found at ${_MITIRU_PSD_EXPORT}. "
        "mitiru_assets() calls will fail to build. "
        "Ensure you included MitiruAssets.cmake from an engine install, not a copy.")
endif()

# ── public entry ───────────────────────────────────────────────────────
function(mitiru_assets target_name)
    set(_options QUIET)
    set(_single  SOURCE_DIR OUT_DIR EXTENSION PSD_TOOL TARGET)
    set(_multi   "")
    cmake_parse_arguments(ARG "${_options}" "${_single}" "${_multi}" ${ARGN})

    if(NOT ARG_SOURCE_DIR)
        message(FATAL_ERROR "mitiru_assets(${target_name}): SOURCE_DIR is required.")
    endif()
    if(NOT ARG_OUT_DIR)
        message(FATAL_ERROR "mitiru_assets(${target_name}): OUT_DIR is required.")
    endif()
    if(NOT IS_ABSOLUTE "${ARG_SOURCE_DIR}")
        get_filename_component(ARG_SOURCE_DIR "${ARG_SOURCE_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()

    if(NOT ARG_EXTENSION)
        set(ARG_EXTENSION "png")
    endif()

    # Python interpreter.
    if(ARG_PSD_TOOL)
        set(_python "${ARG_PSD_TOOL}")
    else()
        if(NOT DEFINED Python3_EXECUTABLE)
            find_package(Python3 COMPONENTS Interpreter QUIET)
        endif()
        if(Python3_EXECUTABLE)
            set(_python "${Python3_EXECUTABLE}")
        else()
            # Fall back to whatever's on PATH.  If PATH has neither 'python'
            # nor 'python3', the custom_command will error at build time with
            # a clear message from the shell.
            set(_python "python")
        endif()
    endif()

    # One-shot tooling probe (configure-time, warning only).
    execute_process(
        COMMAND "${_python}" "${_MITIRU_PSD_EXPORT}" --check
        RESULT_VARIABLE _probe_rc
        OUTPUT_QUIET
        ERROR_VARIABLE _probe_err)
    if(NOT _probe_rc EQUAL 0)
        message(WARNING
            "mitiru_assets(${target_name}): no PSD tooling detected. "
            "Install `pip install psd-tools` or ImageMagick to enable builds. "
            "(${_probe_err})")
    endif()

    # Glob *.psd recursively.  CONFIGURE_DEPENDS forces re-glob on every
    # build so newly-added files register without a fresh `cmake` invocation.
    file(GLOB_RECURSE _psd_sources
        CONFIGURE_DEPENDS
        RELATIVE "${ARG_SOURCE_DIR}"
        "${ARG_SOURCE_DIR}/*.psd")

    if(NOT _psd_sources)
        message(WARNING
            "mitiru_assets(${target_name}): no .psd files found under ${ARG_SOURCE_DIR}.")
    endif()

    set(_outputs)
    foreach(_rel IN LISTS _psd_sources)
        set(_src "${ARG_SOURCE_DIR}/${_rel}")
        get_filename_component(_dir "${_rel}" DIRECTORY)
        get_filename_component(_stem "${_rel}" NAME_WE)
        if(_dir)
            set(_out "${ARG_OUT_DIR}/${_dir}/${_stem}.${ARG_EXTENSION}")
        else()
            set(_out "${ARG_OUT_DIR}/${_stem}.${ARG_EXTENSION}")
        endif()

        set(_cmd "${_python}" "${_MITIRU_PSD_EXPORT}" --input "${_src}" --output "${_out}")
        if(ARG_QUIET)
            list(APPEND _cmd --quiet)
        endif()

        add_custom_command(
            OUTPUT  "${_out}"
            COMMAND ${_cmd}
            DEPENDS "${_src}" "${_MITIRU_PSD_EXPORT}"
            COMMENT "mitiru_assets: ${_rel} → ${_out}"
            VERBATIM)
        list(APPEND _outputs "${_out}")
    endforeach()

    add_custom_target(${target_name} ALL DEPENDS ${_outputs})
    set_target_properties(${target_name} PROPERTIES
        FOLDER "MitiruAssets"
        MITIRU_ASSETS_SOURCE_DIR "${ARG_SOURCE_DIR}"
        MITIRU_ASSETS_OUT_DIR    "${ARG_OUT_DIR}")

    if(ARG_TARGET)
        if(TARGET ${ARG_TARGET})
            add_dependencies(${ARG_TARGET} ${target_name})
        else()
            message(WARNING
                "mitiru_assets(${target_name}): TARGET '${ARG_TARGET}' "
                "does not exist — skipping add_dependencies().")
        endif()
    endif()

    message(STATUS
        "mitiru_assets(${target_name}): ${ARG_SOURCE_DIR} → ${ARG_OUT_DIR} "
        "(${_psd_sources} .psd files)")
endfunction()
