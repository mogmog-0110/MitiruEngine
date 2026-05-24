# MitiruEngineConfig.cmake — find_package(MitiruEngine) 用のディスカバリ
#
# ゲーム側はこのファイルを介して MitiruEngine を検出し、
# ``Mitiru::mitiru`` ターゲットにリンクできるようになる:
#
#   find_package(MitiruEngine REQUIRED)
#   add_executable(my_game main.cpp)
#   target_link_libraries(my_game PRIVATE Mitiru::mitiru)
#
# 検出は 2 段階で進む:
#
#   Phase A (CMake 標準探索 / 本ファイル読込決定) — find_package 実行直後、
#     CMake は CMAKE_PREFIX_PATH / ユーザーパッケージレジストリ /
#     <prefix>/cmake/ などを探索して「どの MitiruEngineConfig.cmake を読むか」
#     を決定する。複数の engine チェックアウトが登録 or 探索パスに並んでいる
#     場合、最初にヒットしたものが「勝ち」となり、その engine が以下の
#     `_from_self` に解決される。したがって `worktrees/<game>/` と
#     `~/MitiruEngine/` が同じマシン上に共存する場合、ユーザー登録が後者を
#     知っていると、ゲーム側で `../engine` を指したくても先に
#     `~/MitiruEngine` の Config.cmake が読み込まれてしまう。
#
#   Phase B (このファイルが読込後、実際の engine 検出) — 候補リストを組み立てて
#     先頭から順に `CMakeLists.txt` と `include/mitiru/Mitiru.hpp` を
#     持つかチェック、最初に該当したものを採用。順序:
#       1. MITIRU_ENGINE_DIR (CMake キャッシュ変数、明示固定の推奨手段)
#       2. MitiruEngine_ROOT (find_package 規約 env / find_package arg)
#       3. ENV{MITIRU_ENGINE_DIR} (環境変数)
#       4. _from_self — この Config.cmake を所有する engine 自身
#          (Phase A で解決されたもの; ユーザーレジストリ経由なら ~/MitiruEngine)
#       5. 呼び出し側から見た近隣ワークツリー: ../engine, ../MitiruEngine,
#          ../../engine, ../../MitiruEngine, ../..
#
# 実運用上の推奨:
#   worktree レイアウトで「必ず ../engine を選びたい」場合は、
#   ゲーム側 CMakeLists.txt の find_package より前に
#   `-DMITIRU_ENGINE_DIR=<path>` で明示指定するか、CMakeLists.txt 内で
#   キャッシュ変数として設定する (テンプレートが自動実装済)。
#   または cmake コマンドラインで -DMITIRU_ENGINE_DIR=.. を毎回渡す。
#
# 見つかった場所に CMakeLists.txt がある前提で add_subdirectory() する。

# ── 検出 ─────────────────────────────────────────────────────────
set(_mitiru_candidates)

if(MITIRU_ENGINE_DIR)
	list(APPEND _mitiru_candidates "${MITIRU_ENGINE_DIR}")
endif()
if(MitiruEngine_ROOT)
	list(APPEND _mitiru_candidates "${MitiruEngine_ROOT}")
endif()
if(DEFINED ENV{MITIRU_ENGINE_DIR} AND NOT "$ENV{MITIRU_ENGINE_DIR}" STREQUAL "")
	list(APPEND _mitiru_candidates "$ENV{MITIRU_ENGINE_DIR}")
endif()

# 近隣のワークスペースレイアウトを探る
get_filename_component(_this_dir "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
# このファイル自身が <engine>/cmake/ 下にあるので、engine ルートは親ディレクトリ
get_filename_component(_from_self "${_this_dir}/.." ABSOLUTE)
list(APPEND _mitiru_candidates "${_from_self}")

# ゲーム側から見た典型的な配置
foreach(_rel
	"../engine"
	"../MitiruEngine"
	"../../engine"
	"../../MitiruEngine"
	"../..")
	if(CMAKE_CURRENT_SOURCE_DIR)
		get_filename_component(_abs "${CMAKE_CURRENT_SOURCE_DIR}/${_rel}" ABSOLUTE)
		list(APPEND _mitiru_candidates "${_abs}")
	endif()
endforeach()

list(REMOVE_DUPLICATES _mitiru_candidates)

set(MitiruEngine_DIR "")
foreach(_cand IN LISTS _mitiru_candidates)
	if(EXISTS "${_cand}/CMakeLists.txt"
	   AND EXISTS "${_cand}/include/mitiru/Mitiru.hpp")
		set(MitiruEngine_DIR "${_cand}")
		break()
	endif()
endforeach()

if(NOT MitiruEngine_DIR)
	set(MitiruEngine_FOUND FALSE)
	if(MitiruEngine_FIND_REQUIRED)
		message(FATAL_ERROR
			"MitiruEngine not found. Tried:\n  ${_mitiru_candidates}\n"
			"Pass -DMITIRU_ENGINE_DIR=<path> or set MitiruEngine_ROOT.")
	endif()
	return()
endif()

message(STATUS "MitiruEngine found: ${MitiruEngine_DIR}")

# ── 既に取り込まれていれば何もしない (multiple find_package 安全) ──
if(TARGET Mitiru::mitiru OR TARGET mitiru::mitiru)
	set(MitiruEngine_FOUND TRUE)
	return()
endif()

# ── add_subdirectory で取り込む ─────────────────────────────────
# EXCLUDE_FROM_ALL を付けて、engine のサンプル/テストが親プロジェクトの
# 既定 build target に入らないようにする
add_subdirectory(
	"${MitiruEngine_DIR}"
	"${CMAKE_BINARY_DIR}/_deps/MitiruEngine"
	EXCLUDE_FROM_ALL)

# INTERFACE target "mitiru" が ALIAS "Mitiru::mitiru" で公開されている
# ことを確認する (engine 本体の CMakeLists.txt で add_library(mitiru INTERFACE)
# + add_library(Mitiru::mitiru ALIAS mitiru) が行われる)
if(NOT TARGET Mitiru::mitiru)
	message(FATAL_ERROR
		"MitiruEngine was found at ${MitiruEngine_DIR} but its CMakeLists.txt "
		"did not define the Mitiru::mitiru target. Engine version mismatch?")
endif()

# ── Web runtime パスをエクスポート ──────────────────────────────
# ゲーム側で mitiru_runtime/mitiru_base.css 等を POST_BUILD コピーしたい
# ケースに便利。
set(MitiruEngine_WEB_RUNTIME_DIR "${MitiruEngine_DIR}/web/mitiru_runtime"
	CACHE PATH "Path to engine-side web runtime (CSS / JS / test harness)")

# ── Asset pipeline helper (F-09) ───────────────────────────────
# mitiru_assets() を使えるようにする。
if(EXISTS "${MitiruEngine_DIR}/cmake/MitiruAssets.cmake")
	include("${MitiruEngine_DIR}/cmake/MitiruAssets.cmake")
endif()

# ── CEF game POST_BUILD helper ─────────────────────────────────
# mitiru_register_cef_game() を使えるようにする (各ゲームの ~30 行 POST_BUILD
# ブロックを 1 関数化)。
if(EXISTS "${MitiruEngine_DIR}/cmake/MitiruCefGameHelper.cmake")
	include("${MitiruEngine_DIR}/cmake/MitiruCefGameHelper.cmake")
endif()

set(MitiruEngine_FOUND TRUE)
