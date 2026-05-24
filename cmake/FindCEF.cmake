# FindCEF.cmake — CEF (Chromium Embedded Framework) プリビルト探索モジュール
#
# 事前準備:
#   python tools/fetch_cef.py
#
# 生成されるターゲット:
#   CEF::libcef             — libcef.dll インポートライブラリ
#   CEF::libcef_dll_wrapper — libcef_dll_wrapper (静的グルーライブラリ)
#
# 公開変数:
#   CEF_FOUND
#   CEF_ROOT         — cef_binary_*_windows64 ディレクトリ
#   CEF_INCLUDE_DIR  — ヘッダー (include/)
#   CEF_BINARY_DIR   — DLL / .lib (Release/ or Debug/)
#   CEF_RESOURCE_DIR — .pak / locales / icudtl.dat (Resources/ or binary dir)

# このプロジェクトでは Windows 専用
if(NOT WIN32)
    set(CEF_FOUND FALSE CACHE INTERNAL "CEF found" FORCE)
    return()
endif()

# G-11 fix: CEF base はこの Find モジュール自身からの相対参照にする。
# add_subdirectory(../engine) 経由で外側プロジェクトが engine を取り込んでも
# engine/external/cef を指し続ける (CMAKE_SOURCE_DIR は外側 top を指してしまうため使わない)。
# MITIRU_CEF_ROOT_DIR で override 可能 (CI や sibling-repo 配置向け)。
if(DEFINED MITIRU_CEF_ROOT_DIR)
    set(_CEF_BASE "${MITIRU_CEF_ROOT_DIR}")
else()
    get_filename_component(_CEF_BASE "${CMAKE_CURRENT_LIST_DIR}/../external/cef" ABSOLUTE)
endif()

# cef_binary_*_windows64 ディレクトリを検索する (バージョン問わず)
file(GLOB _CEF_CANDIDATES "${_CEF_BASE}/cef_binary_*_windows64")
list(FILTER _CEF_CANDIDATES INCLUDE REGEX ".*_windows64$")
list(LENGTH _CEF_CANDIDATES _CEF_COUNT)

if(_CEF_COUNT EQUAL 0)
    if(NOT CEF_FIND_QUIETLY)
        message(STATUS "CEF: 見つかりません (${_CEF_BASE})")
        message(STATUS "  → python tools/fetch_cef.py を実行してください")
    endif()
    set(CEF_FOUND FALSE CACHE INTERNAL "CEF found" FORCE)
    return()
endif()

# 複数バージョンがある場合は降順で最新を選ぶ
list(SORT _CEF_CANDIDATES ORDER DESCENDING)
list(GET _CEF_CANDIDATES 0 CEF_ROOT)
set(CEF_ROOT "${CEF_ROOT}" CACHE PATH "CEF root directory" FORCE)

set(CEF_INCLUDE_DIR "${CEF_ROOT}" CACHE PATH "CEF include directory" FORCE)

# ── バイナリディレクトリの検出 ────────────────────────────────
# minimal 配布は Debug/Release フォルダ構成
foreach(_cfg Debug Release)
    if(EXISTS "${CEF_ROOT}/${_cfg}/libcef.dll")
        if(_cfg STREQUAL "Debug")
            set(_CEF_BIN_DEBUG   "${CEF_ROOT}/${_cfg}")
        else()
            set(_CEF_BIN_RELEASE "${CEF_ROOT}/${_cfg}")
        endif()
    endif()
endforeach()

# どちらかしかない場合はフォールバック
if(NOT _CEF_BIN_DEBUG   AND _CEF_BIN_RELEASE)
    set(_CEF_BIN_DEBUG "${_CEF_BIN_RELEASE}")
endif()
if(NOT _CEF_BIN_RELEASE AND _CEF_BIN_DEBUG)
    set(_CEF_BIN_RELEASE "${_CEF_BIN_DEBUG}")
endif()

if(NOT _CEF_BIN_RELEASE)
    message(STATUS "CEF: libcef.dll が見つかりません (${CEF_ROOT})")
    set(CEF_FOUND FALSE CACHE INTERNAL "CEF found" FORCE)
    return()
endif()

# CMAKE_BUILD_TYPE に合わせてデフォルトを選ぶ
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CEF_BINARY_DIR "${_CEF_BIN_DEBUG}"   CACHE PATH "" FORCE)
else()
    set(CEF_BINARY_DIR "${_CEF_BIN_RELEASE}" CACHE PATH "" FORCE)
endif()

# Resources ディレクトリ (standard 配布にある; minimal では binary dir に同梱)
if(EXISTS "${CEF_ROOT}/Resources")
    set(CEF_RESOURCE_DIR "${CEF_ROOT}/Resources" CACHE PATH "" FORCE)
else()
    set(CEF_RESOURCE_DIR "${CEF_BINARY_DIR}" CACHE PATH "" FORCE)
endif()

# ── CEF::libcef インポートターゲット ─────────────────────────
if(NOT TARGET CEF::libcef)
    add_library(CEF::libcef SHARED IMPORTED GLOBAL)
    set_target_properties(CEF::libcef PROPERTIES
        IMPORTED_LOCATION              "${_CEF_BIN_RELEASE}/libcef.dll"
        IMPORTED_IMPLIB                "${_CEF_BIN_RELEASE}/libcef.lib"
        IMPORTED_LOCATION_DEBUG        "${_CEF_BIN_DEBUG}/libcef.dll"
        IMPORTED_IMPLIB_DEBUG          "${_CEF_BIN_DEBUG}/libcef.lib"
        INTERFACE_INCLUDE_DIRECTORIES  "${CEF_INCLUDE_DIR}"
    )
endif()

# ── libcef_dll_wrapper (ソースから静的ビルド) ────────────────
set(_WRAPPER_SRC_DIR "${CEF_ROOT}/libcef_dll")

if(NOT EXISTS "${_WRAPPER_SRC_DIR}")
    message(WARNING "CEF: libcef_dll ソースが見つかりません: ${_WRAPPER_SRC_DIR}")
    set(CEF_FOUND FALSE CACHE INTERNAL "CEF found" FORCE)
    return()
endif()

if(NOT TARGET libcef_dll_wrapper)
    file(GLOB_RECURSE _WRAPPER_SRCS
        "${_WRAPPER_SRC_DIR}/*.cc"
        "${_WRAPPER_SRC_DIR}/*.cpp"
    )

    add_library(libcef_dll_wrapper STATIC ${_WRAPPER_SRCS})

    # インクルードパス: include/ (CEF ヘッダー) と ルート (cef_dll_wrapper 内部 include)
    target_include_directories(libcef_dll_wrapper PUBLIC
        "${CEF_ROOT}"
        "${CEF_ROOT}/include"
    )

    # CEF 128 は C++17 (std::in_place_t 等) を使用する
    target_compile_features(libcef_dll_wrapper PUBLIC cxx_std_17)

    if(MSVC)
        # MSVC ランタイムを消費側 (mitiru) と合わせる — /MDd Debug, /MD Release
        # MSVC_RUNTIME_LIBRARY プロパティは VS generator でジェネレーター式展開後の値が
        # "not known" エラーになるため、直接コンパイルフラグで指定する
        target_compile_options(libcef_dll_wrapper PRIVATE
            $<$<CONFIG:Debug>:/MDd>
            $<$<NOT:$<CONFIG:Debug>>:/MD>
        )
        # CEF ラッパーの警告は抑制する (メンテナンス対象外コード)
        target_compile_options(libcef_dll_wrapper PRIVATE
            /W0 /wd4100 /wd4127 /wd4996 /bigobj /utf-8 /FS
        )
    endif()

    target_compile_definitions(libcef_dll_wrapper PRIVATE
        USING_CEF_SHARED
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        WRAPPING_CEF_SHARED
    )

    target_link_libraries(libcef_dll_wrapper PUBLIC CEF::libcef)

    add_library(CEF::libcef_dll_wrapper ALIAS libcef_dll_wrapper)
endif()

# G-12 fix: CACHE に入れないと add_subdirectory(../engine) 経由で呼ばれたときに
# engine スコープ内でのみ TRUE になり、外側 game CMakeLists からは不可視になる。
# CACHE INTERNAL で親スコープへ伝播させる。
set(CEF_FOUND TRUE CACHE INTERNAL "CEF found" FORCE)
message(STATUS "CEF found: ${CEF_ROOT}")
message(STATUS "  binary : ${CEF_BINARY_DIR}")
message(STATUS "  resource: ${CEF_RESOURCE_DIR}")
