# EmbedAssets.cmake
# ─────────────────────────────────────────────────────────────────────────────
# embed_assets(TARGET <target> OUTPUT <header_path> FILES <file> ...)
#
# Generates a C++ header at <header_path> that embeds each <file> as a
# const std::span<const uint8_t>.  The virtual path used in the app:// URL
# is the path relative to the common prefix of all input files.
#
# Usage in CMakeLists.txt:
#   embed_assets(
#     TARGET        MyGame
#     OUTPUT        "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_assets.hpp"
#     BASE_DIR      "${CMAKE_CURRENT_SOURCE_DIR}/assets"
#     FILES
#       assets/ui/title.html
#       assets/ui/cooking.html
#       assets/css/main.css
#   )
# ─────────────────────────────────────────────────────────────────────────────

function(embed_assets)
    cmake_parse_arguments(EA "" "TARGET;OUTPUT;BASE_DIR" "FILES" ${ARGN})

    if(NOT EA_TARGET OR NOT EA_OUTPUT OR NOT EA_FILES)
        message(FATAL_ERROR "embed_assets: TARGET, OUTPUT, and FILES are required")
    endif()

    if(NOT EA_BASE_DIR)
        set(EA_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()

    # Find the Python interpreter
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(NOT Python3_FOUND)
        find_program(PYTHON_EXE python)
        if(NOT PYTHON_EXE)
            find_program(PYTHON_EXE python3)
        endif()
    else()
        set(PYTHON_EXE "${Python3_EXECUTABLE}")
    endif()

    if(NOT PYTHON_EXE)
        message(FATAL_ERROR "embed_assets: Python not found")
    endif()

    set(GENERATOR_SCRIPT "${CMAKE_SOURCE_DIR}/tools/gen_embedded_assets.py")

    # Convert FILES list to semicolon string for passing to Python
    string(JOIN ";" FILES_ARG ${EA_FILES})

    get_filename_component(OUTPUT_DIR "${EA_OUTPUT}" DIRECTORY)

    add_custom_command(
        OUTPUT  "${EA_OUTPUT}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${OUTPUT_DIR}"
        COMMAND "${PYTHON_EXE}"
                "${GENERATOR_SCRIPT}"
                "--base-dir" "${EA_BASE_DIR}"
                "--output"   "${EA_OUTPUT}"
                ${EA_FILES}
        DEPENDS "${GENERATOR_SCRIPT}" ${EA_FILES}
        COMMENT "Embedding assets into ${EA_OUTPUT}"
        VERBATIM
    )

    # Create a custom target that triggers the generation
    set(EMBED_TARGET "_embed_${EA_TARGET}")
    add_custom_target("${EMBED_TARGET}" DEPENDS "${EA_OUTPUT}")

    # Make the main target depend on asset generation
    add_dependencies("${EA_TARGET}" "${EMBED_TARGET}")

    # Expose the output dir as an include path to the target
    target_include_directories("${EA_TARGET}" PRIVATE "${OUTPUT_DIR}")
endfunction()
