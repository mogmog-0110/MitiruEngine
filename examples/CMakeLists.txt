# MitiruEngine examples の章立てに沿ったカリキュラム (規格: docs/EXAMPLE_STANDARD.md)
# 各章は 1 機能を 1 DLL で扱う教材。追加は mitiru_add_example(<name>) の 1 行
# (+ tests/CMakeLists.txt の _example_e2e_chapters に 1 行追加すると headless E2E も付く)。
# 学習順序は名前ではなく、docs/EXAMPLE_STANDARD.md の章立てと examples/README.md の表で示す。

if(NOT TARGET mitiru)
	message(STATUS "examples/ skipped: mitiru target not defined")
	return()
endif()

# 章の example を 1 行で追加する方法。
#   mitiru_add_example(welcome)
#     - <name>/<name>_dll.cpp を SHARED (ゲーム DLL) としてビルド
#     - mitiru_host の隣 (<host dir>/<name>/) に配置し、host が相対 path で load できるようにする
#     - <name>/assets/ があれば、source-tracked stamp を介して同じ dir に配置
function(mitiru_add_example name)
	# 公開スナップショットには、一部の example を同梱しない (教本の検証用など)。存在しない example を登録すると
	# 利用者の configure が「No SOURCES given」で失敗するため、ソースがなければ登録しない。
	if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${name}/${name}_dll.cpp")
		message(STATUS "example ${name}: ソースが無いので飛ばす (${name}/${name}_dll.cpp)")
		return()
	endif()
	add_library(${name} SHARED "${CMAKE_CURRENT_SOURCE_DIR}/${name}/${name}_dll.cpp")
	target_link_libraries(${name} PRIVATE mitiru)
	if(MSVC)
		target_compile_options(${name} PRIVATE /bigobj)
	endif()
	if(NOT TARGET mitiru_host)
		return()
	endif()
	set(_dir "$<TARGET_FILE_DIR:mitiru_host>/${name}")
	set_target_properties(${name} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${_dir}"
		LIBRARY_OUTPUT_DIRECTORY "${_dir}")
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${name}/assets")
		file(GLOB_RECURSE _assets CONFIGURE_DEPENDS
			"${CMAKE_CURRENT_SOURCE_DIR}/${name}/assets/*")
		set(_stamp "${CMAKE_CURRENT_BINARY_DIR}/${name}_assets.stamp")
		add_custom_command(
			OUTPUT  "${_stamp}"
			COMMAND ${CMAKE_COMMAND} -E copy_directory
				"${CMAKE_CURRENT_SOURCE_DIR}/${name}/assets" "${_dir}/assets"
			COMMAND ${CMAKE_COMMAND} -E touch "${_stamp}"
			DEPENDS ${_assets}
			VERBATIM
			COMMENT "${name}: deploying assets/ (source-tracked)")
		add_custom_target(${name}_assets ALL DEPENDS "${_stamp}")
		add_dependencies(${name} ${name}_assets)
	endif()
	add_dependencies(${name} mitiru_host)
endfunction()

# ── 章 (docs/EXAMPLE_STANDARD.md の章立て順) ────────────────────────────────
mitiru_add_example(welcome)
mitiru_add_example(shapes)
mitiru_add_example(text)
mitiru_add_example(input)
mitiru_add_example(motion)
mitiru_add_example(sprites)
mitiru_add_example(camera)         # 追従カメラ (FollowCam): 画面より広い世界を deadzone + 先読みで追う
mitiru_add_example(audio)
mitiru_add_example(html_hud)       # 最小の操作シーン (C++ ネイティブ描画の箱庭) に HTML HUD を重ねる: ゲーム状態が text/tween/style/class/repeat を駆動、JS ゼロ (軸①)
mitiru_add_example(html_menu)      # binder 応用: list / dispatch / confirm
mitiru_add_example(observe)
mitiru_add_example(rewind)         # time-travel inspector (軸②)
mitiru_add_example(restart_save)   # requestRestart + .msav
mitiru_add_example(objects_kitchen) # クラスとコンポーネントで書く: MITIRU_GAME_OBJECTS (進行データだけ POD、ADR 0040)
mitiru_add_example(scene3d)        # GPU 3D: 影 + WBOIT 半透明 + skybox
mitiru_add_example(model3d)        # 大規模モデル: drawModel + 自動 LOD
mitiru_add_example(anim3d)         # 骨格アニメ: drawModelBlend + クリップ時間
mitiru_add_example(csg_solid)      # Makina の焼き済み CSG: drawSolid (姉妹プロジェクト連携)
mitiru_add_example(plane_probe)    # #56 の再現プローブ: cube と plane を並べて撮る

# navmesh は、ナビメッシュを使って経路を探索する。レベルの .obj をビルド工程で mitiru_navbake が .navmesh に変換し、
# DLL の隣にある assets/ に配置する (Recast/Detour の submodule がある場合のみ)。
if(TARGET mitiru_navbake)
	mitiru_add_example(navmesh)
	target_link_libraries(navmesh PRIVATE mitiru_nav)
	set(_nav_level "${CMAKE_CURRENT_SOURCE_DIR}/navmesh/assets/level.obj")
	set(_nav_stamp "${CMAKE_CURRENT_BINARY_DIR}/navmesh_bake.stamp")
	add_custom_command(
		OUTPUT  "${_nav_stamp}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:mitiru_host>/navmesh/assets"
		COMMAND mitiru_navbake "${_nav_level}" -o "$<TARGET_FILE_DIR:mitiru_host>/navmesh/assets/level.navmesh"
		COMMAND ${CMAKE_COMMAND} -E touch "${_nav_stamp}"
		DEPENDS mitiru_navbake "${_nav_level}"
		VERBATIM
		COMMENT "navmesh: baking assets/level.obj -> level.navmesh")
	add_custom_target(navmesh_bake ALL DEPENDS "${_nav_stamp}")
	add_dependencies(navmesh navmesh_bake)
endif()
if(MITIRU_WITH_EFFEKSEER)
	mitiru_add_example(effekseer_fx)  # Effekseer のエフェクトを経過秒で描く (drawModel の時刻つき版)
endif()
mitiru_add_example(rollback_duel)  # 1 台で 2 人のテニス。mitiru_rollback (opt-in) でロールバック対戦の検証に使う

# textbook_ch4..6 は、docs/textbook/index.html の第 4〜6 章の完成形 (M6)。examples/README.md の
# カリキュラム表とは別枠 (教本の実機検証専用で、beko_run/hello_preview と同じ位置づけ)。
mitiru_add_example(textbook_ch4)   # 敵 + 当たり判定 + MITIRU_INVARIANT (HP は負にならない)
mitiru_add_example(textbook_ch5)   # --record / --replay-test / --inspect rewind
mitiru_add_example(textbook_ch6)   # 分岐エディタ: beginObject(name, fieldX, fieldY)

# ── チュートリアル頁の題材ゲーム ───────────────────────────────────────────
# beko_run は、赤べこの横スクロールアクション。カリキュラムの章 (1 機能 1 DLL) ではなく、
# site/homepage/tutorial.html での実演用。複数のソースに分けるため、独立した CMakeLists を持つ。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/beko_run/CMakeLists.txt")
	add_subdirectory(beko_run)
endif()

# hello_preview は、`mitiru new` (hello テンプレ) の画面キャプチャ用 (install.html の頁生成専用)。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/hello_preview/hello_preview_dll.cpp")
	mitiru_add_example(hello_preview)
endif()

# welcome_start は、`mitiru new` の新しい既定 (welcome) テンプレの実演用 = 画面キャプチャ用
# (install.html の頁生成専用)。左側は C++ で描画した絵、右側は HTML/CSS の操作パネル。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/welcome_start/welcome_start_dll.cpp")
	mitiru_add_example(welcome_start)
endif()

# ── 検証用 (従来形式のまま) ─────────────────────────────────────────────────
# why_demo は、`mitiru why` (分岐の根本原因を特定する機能) の検証用 (dev 専用、snapshot 非同梱)。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/why_demo/CMakeLists.txt")
	add_subdirectory(why_demo)
endif()
# neon_asteroids は、NeonVector の発光ラインを取り入れた Asteroids (dev 専用、snapshot 非同梱)。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neon_asteroids/CMakeLists.txt")
	add_subdirectory(neon_asteroids)
endif()
# 大型ゲームは examples に置かない (ここはエンジンの demo / subsystem の起動のみ)。

# ── subsystem 単独起動 (軸③、単体 exe) ─────────────────────────────────────
add_subdirectory(subsys)
