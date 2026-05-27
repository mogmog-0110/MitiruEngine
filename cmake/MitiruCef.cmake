# MitiruCef.cmake — ゲーム側 CMakeLists を 5 行以下にする CEF セットアップマクロ
#
# Usage (ゲーム側):
#   find_package(Mitiru CONFIG REQUIRED)       # or add_subdirectory(engine)
#   add_executable(MingePort WIN32 src/main.cpp)
#   target_link_libraries(MingePort PRIVATE Mitiru::mitiru)
#   mitiru_add_cef_game(MingePort)             # ← これだけで CEF 一式配置
#
# これで以下が自動で行われる:
#   - CEF::libcef と CEF::libcef_dll_wrapper のリンク
#   - libcef.dll / chrome_elf.dll / *.bin / icudtl.dat / vk_swiftshader.dll 他
#     の POST_BUILD コピー (CEF_BINARY_DIR → target exe dir)
#   - resources.pak / chrome_100_percent.pak / chrome_200_percent.pak /
#     locales/ のコピー (CEF_RESOURCE_DIR → target exe dir)
#   - MitiruCefHelper.exe (engine 同梱 subprocess) のコピー
#
# オプション:
#   HELPER_SOURCE <src>   カスタム subprocess 源を指定 (未指定時は engine 同梱)
#   HELPER_NAME <name>    カスタムヘルパーの target 名 (default: <target>Helper)
#   NO_OPTIONAL_DLLS      vk_swiftshader.dll 等の任意 DLL をスキップ
#   SKIP_LOCALES          locales/ ディレクトリのコピーをスキップ (通常は非推奨)
#
# CEF 未発見時 (CEF_FOUND=FALSE) は STATUS メッセージを出して no-op 。

if(COMMAND mitiru_add_cef_game)
    return()
endif()

function(mitiru_add_cef_game target)
    # --- Parse args -------------------------------------------------------
    set(options NO_OPTIONAL_DLLS SKIP_LOCALES)
    set(oneValueArgs HELPER_SOURCE HELPER_NAME)
    set(multiValueArgs "")
    cmake_parse_arguments(MACG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT WIN32)
        message(STATUS "mitiru_add_cef_game(${target}): non-Windows platform, skipping")
        return()
    endif()

    if(NOT CEF_FOUND)
        message(STATUS "mitiru_add_cef_game(${target}): CEF not found, skipping (run python tools/fetch_cef.py)")
        return()
    endif()

    if(NOT TARGET CEF::libcef OR NOT TARGET CEF::libcef_dll_wrapper)
        message(STATUS "mitiru_add_cef_game(${target}): CEF targets not available, skipping")
        return()
    endif()

    # --- Link CEF libs ----------------------------------------------------
    target_link_libraries(${target} PRIVATE CEF::libcef CEF::libcef_dll_wrapper)
    target_compile_definitions(${target} PRIVATE MITIRU_HAS_CEF=1)

    # --- Helper subprocess exe -------------------------------------------
    set(_helper_target "")
    if(MACG_HELPER_SOURCE)
        # Custom per-game helper.
        if(MACG_HELPER_NAME)
            set(_helper_target "${MACG_HELPER_NAME}")
        else()
            set(_helper_target "${target}Helper")
        endif()
        if(NOT TARGET ${_helper_target})
            add_executable(${_helper_target} "${MACG_HELPER_SOURCE}")
            # libcef は /MD (Release CRT) 固定。/MDd でビルドすると CefExecuteProcess
            # が "error_code=63" で落ちる。engine と同じ wrapper_mdrel を使う。
            if(TARGET libcef_dll_wrapper_mdrel)
                target_link_libraries(${_helper_target} PRIVATE
                    CEF::libcef libcef_dll_wrapper_mdrel)
            else()
                target_link_libraries(${_helper_target} PRIVATE
                    CEF::libcef CEF::libcef_dll_wrapper)
            endif()
            target_compile_definitions(${_helper_target} PRIVATE
                MITIRU_HAS_CEF=1 NOMINMAX WIN32_LEAN_AND_MEAN)
            target_include_directories(${_helper_target} PRIVATE
                "${CEF_ROOT}" "${CEF_ROOT}/include")
            # Release CRT (/MD) は CMP0091 のプロパティで宣言する。手動 /MD は
            # CMake 自動注入の既定 /MDd と二重になり D9025 を出す。
            set_target_properties(${_helper_target} PROPERTIES
                FOLDER "cef"
                MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
        endif()
    else()
        # Default: use engine-shipped MitiruCefHelper (already defined by engine/CMakeLists.txt).
        if(TARGET MitiruCefHelper)
            set(_helper_target "MitiruCefHelper")
        else()
            message(WARNING
                "mitiru_add_cef_game(${target}): MitiruCefHelper target not found. "
                "Ensure engine/CMakeLists.txt was included before this call, or "
                "pass HELPER_SOURCE to build a custom helper.")
        endif()
    endif()

    if(_helper_target)
        add_dependencies(${target} ${_helper_target})
        # Copy helper exe next to the game exe with the expected filename
        # (MitiruCefConfig::buildCefSettings looks for MitiruCefHelper.exe by default
        # when no custom helper-name is wired through).
        if(_helper_target STREQUAL "MitiruCefHelper")
            set(_helper_dest "MitiruCefHelper.exe")
        elseif(MACG_HELPER_NAME)
            set(_helper_dest "${MACG_HELPER_NAME}.exe")
        else()
            set(_helper_dest "${_helper_target}.exe")
        endif()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${_helper_target}>"
                "$<TARGET_FILE_DIR:${target}>/${_helper_dest}"
            COMMENT "mitiru_add_cef_game: copying ${_helper_dest}")
    endif()

    # --- CEF runtime binaries --------------------------------------------
    # copy_directory は existing file を全部上書きする。大量のファイルを
    # 個別に列挙するより安全で保守がラク。
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CEF_BINARY_DIR}"
            "$<TARGET_FILE_DIR:${target}>"
        COMMENT "mitiru_add_cef_game: copying CEF binary runtime (libcef.dll + peers)")

    # --- CEF resources (.pak + icudtl.dat) -------------------------------
    if(MACG_SKIP_LOCALES)
        # locales/ 以外をファイル単位でコピー
        foreach(_res IN ITEMS
            "icudtl.dat"
            "resources.pak"
            "chrome_100_percent.pak"
            "chrome_200_percent.pak"
            "snapshot_blob.bin"
            "v8_context_snapshot.bin")
            if(EXISTS "${CEF_RESOURCE_DIR}/${_res}")
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${CEF_RESOURCE_DIR}/${_res}"
                        "$<TARGET_FILE_DIR:${target}>/${_res}"
                    COMMENT "mitiru_add_cef_game: copying ${_res}")
            endif()
        endforeach()
    else()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CEF_RESOURCE_DIR}"
                "$<TARGET_FILE_DIR:${target}>"
            COMMENT "mitiru_add_cef_game: copying CEF resources (.pak + locales/ + icudtl.dat)")
    endif()

    # --- Optional DLLs skip hook -----------------------------------------
    # NO_OPTIONAL_DLLS は当座 no-op (CEF_BINARY_DIR の copy_directory は任意 DLL も
    # すべて含むため、ここで filter するのは get_filename_component 連打が必要で
    # 実用性が低い)。将来的に配布サイズが問題になったら blacklist 方式で追加。
    if(MACG_NO_OPTIONAL_DLLS)
        message(STATUS "mitiru_add_cef_game(${target}): NO_OPTIONAL_DLLS recorded "
            "but copy_directory does not filter; manual cleanup post-build if needed")
    endif()
endfunction()
