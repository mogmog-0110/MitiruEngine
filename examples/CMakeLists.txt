# MitiruEngine consumer samples
#
# The previous sample set was retired in Round 39 pending a redesigned
# example collection. New samples will be added back here under
# `examples/<name>/CMakeLists.txt` and linked with:
#
#   target_link_libraries(<game> PRIVATE Mitiru::mitiru)
#
# Skipped automatically when mitiru target is unavailable (e.g. Emscripten
# builds selecting a different entry path).

if(NOT TARGET mitiru)
	message(STATUS "examples/ skipped: mitiru target not defined")
	return()
endif()

# v0.1.1 examples — minimum runnable samples linked against the mitiru target.
# Build all:
#     cmake --build build --config Debug
# Build one:
#     cmake --build build --config Debug --target hello_window

# ── ゲームサンプルは全撤去 (再設計待ち、2026-06-09) ────────────────────
# 旧サンプル (hello_window/input/scene/shapes/cef_overlay, hello_game, breakout,
# anchor, dodge, showcase_platformer) は「おもちゃ感が強く真面目なエンジンに不向き」
# との判断で削除。エンジンの証明は差別化デモ (timetravel_demo) と本物のゲーム
# (FLUID DROP 等) に寄せる方針 (証明の置き方は未確定)。
#
# mitiru_host は timetravel_demo より前に置く (後者が $<TARGET_FILE_DIR:mitiru_host>/
# 配下へ DLL を install し、configure 時に host の位置を参照するため)。
add_subdirectory(mitiru_host)
add_subdirectory(mitiru_start)
add_subdirectory(timetravel_demo)   # 差別化デモ (time-travel / AI Lens、ADR 0017/0018)
# hello3d: 3D ゲームデモ (CPU ソフトレンダラー DeferredPipeline を drawPixelGrid で 2D に
# 合成)。GPU 3D 経路に頼らず draw(Screen&) だけで完結。allowlist 同梱なので無条件参照。
add_subdirectory(hello3d)
# catch: チュートリアルの題材 (スプライト入門)。仮素材のため dev-only・snapshot 非同梱。
# EXISTS ガード必須 (allowlist 非追加なので release snapshot には catch/ が無い)。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/catch/CMakeLists.txt")
	add_subdirectory(catch)
endif()
# hookline: サンプル再建の試作 (2026-06-11 凍結、dev-only)。snapshot 非同梱。
# 凍結理由 = 「いかにも AI が作ったゲーム」感: プレースホルダー幾何美術 + 合成音 +
# チェックリスト設計で、作家性 (具体的な美学への決断・手の入った素材) が無い。
# 公開物は美学へのコミットが先、機構は後 (FLUID DROP = 流体が美学、サチ = 手描きドット)。
# tutorial/ の段階コーパス (S1-S7 全過程) は教材原稿として価値が残るので保持。
# snapshot 非同梱のため EXISTS ガード必須 (無条件参照は public の configure を壊す)。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/hookline/CMakeLists.txt")
	add_subdirectory(hookline)
endif()
# 研究 example は 2026-06-12 に E:/user/research へ搬出 (リファクタ P1)。
# fluidtetris は attic/fluidtetris ブランチへ退避 (本体は Fluid-Drop repo で公開済)。
add_subdirectory(mitiru_launcher)
add_subdirectory(mitiru_dev_companion)
# サブシステム単独起動 (軸③)。
add_subdirectory(mitiru_subsys_renderer)
add_subdirectory(mitiru_subsys_replay)
add_subdirectory(mitiru_subsys_audio)
add_subdirectory(mitiru_subsys_input)
add_subdirectory(mitiru_subsys_scene)
# ツール窓は汎用 CEF ホスト mitiru_tool_cef --page <name> に統一 (ADR 0014)。
# 旧個別 exe (mitiru_perf/mixer/scene_tree/replay/inspector) と旧 subsystem 種
# (mitiru_renderer/audio) は退役。
add_subdirectory(mitiru_tool_cef)

