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
mitiru_add_example(music_layers)   # 曲の強さで層を足す (music.json) と、候補と揺れのある効果音 (sounds.json)
mitiru_add_example(html_hud)       # 最小の操作シーン (C++ ネイティブ描画の箱庭) に HTML HUD を重ねる: ゲーム状態が text/tween/style/class/repeat を駆動、JS ゼロ (軸①)
mitiru_add_example(html_menu)      # binder 応用: list / dispatch / confirm
mitiru_add_example(observe)
mitiru_add_example(rewind)         # time-travel inspector (軸②)
mitiru_add_example(restart_save)   # requestRestart + .msav
mitiru_add_example(save_slots)     # セーブを 3 つのスロットに分ける: 一覧 (RML) から saveSlot / load / deleteSlot
mitiru_add_example(objects_kitchen) # クラスとコンポーネントで書く: MITIRU_GAME_OBJECTS (進行データだけ POD、ADR 0040)
if(TARGET Jolt)
	mitiru_add_example(physics_rewind) # DLL が持つ Jolt の世界を状態の外の窓口として預け、巻き戻す (ADR 0054)
	mitiru_add_example(ragdoll3d)      # アニメを追う人形: 殴ると上半身がよろけ、倒れて起き上がる (ADR 0065)
	mitiru_add_example(destruction3d)  # 割れる木箱と柱: 読み込みの時に割った破片へ差し替える (ADR 0069)
	mitiru_add_example(cloth3d)        # マントと旗: Jolt の布を骨と竿に留め、体のカプセルに当てる (ADR 0069)
	# 人形は ragdoll3d の物を使う
	if(TARGET cloth3d AND TARGET mitiru_host AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ragdoll3d/assets/mannequin/mannequin.glb")
		set(_to "$<TARGET_FILE_DIR:mitiru_host>/cloth3d/assets/mannequin")
		add_custom_command(TARGET cloth3d POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E make_directory "${_to}"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/ragdoll3d/assets/mannequin/mannequin.glb" "${_to}/")
	endif()
endif()
mitiru_add_example(scene3d)        # GPU 3D: 影 + WBOIT 半透明 + skybox
mitiru_add_example(model3d)        # 大規模モデル: drawModel + 自動 LOD
mitiru_add_example(anim3d)         # 骨格アニメ: drawModelPose (レイヤの姿勢 + 頭の IK)
mitiru_add_example(springbones3d)  # 狐の尻尾: 姿勢の後に掛ける骨のばね (ADR 0069)。狐は anim3d の物で、隣の fox.springs.json だけを持つ
if(TARGET springbones3d AND TARGET mitiru_host AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/anim3d/assets/fox/fox.glb")
	set(_to "$<TARGET_FILE_DIR:mitiru_host>/springbones3d/assets/fox")
	add_custom_command(TARGET springbones3d POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${_to}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/anim3d/assets/fox/fox.glb"
		        "${CMAKE_CURRENT_SOURCE_DIR}/anim3d/assets/fox/CREDITS.md" "${_to}/")
endif()
mitiru_add_example(footing3d)      # 足を段と坂に置く: 足の接地 (FootPlacement) と、着いた足を留める滑り止め。人形は ragdoll3d の物
if(TARGET footing3d AND TARGET mitiru_host AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ragdoll3d/assets/mannequin/mannequin.glb")
	set(_to "$<TARGET_FILE_DIR:mitiru_host>/footing3d/assets/mannequin")
	add_custom_command(TARGET footing3d POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${_to}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/ragdoll3d/assets/mannequin/mannequin.glb" "${_to}/")
endif()
mitiru_add_example(anim_events)    # アニメに合わせて動く: ルートモーション、足音のイベント、口のソケット
mitiru_add_example(lights3d)       # 点光源・スポットライト (影) + インスタンス描画 (ABI v48)
mitiru_add_example(csg_solid)      # Makina の焼き済み CSG: drawSolid (姉妹プロジェクト連携)
mitiru_add_example(plane_probe)    # #56 の再現プローブ: cube と plane を並べて撮る
mitiru_add_example(action3d)       # 3D アクションの部品 (mitiru/action/): 同じフレームの当たり判定、キャラ、カメラ、跳んだ筋、着地の跡と土ぼこり
mitiru_add_example(cutscene3d)     # カットシーン・会話・クエスト (mitiru/narrative/): JSON と台本で書き、時刻と旗だけを GameMemory に置く
mitiru_add_example(level3d)        # Blender で作ったステージ (.glb) を drawModel で描き、同じファイルの面と印で歩く
# level3d の光 (放射照度と反射のプローブ) はビルドの時に mitiru_lightbake で焼き、DLL の隣の assets/ に置く (lightingBake3D が読む)
if(TARGET level3d AND TARGET mitiru_lightbake AND TARGET mitiru_host)
	set(_light_json "${CMAKE_CURRENT_SOURCE_DIR}/level3d/assets/stage.lighting.json")
	set(_light_stamp "${CMAKE_CURRENT_BINARY_DIR}/level3d_lightbake_$<CONFIG>.stamp")
	add_custom_command(
		OUTPUT  "${_light_stamp}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:mitiru_host>/level3d/assets"
		COMMAND mitiru_lightbake "${_light_json}" -o "$<TARGET_FILE_DIR:mitiru_host>/level3d/assets/stage.lighting.bin"
		COMMAND ${CMAKE_COMMAND} -E touch "${_light_stamp}"
		DEPENDS mitiru_lightbake "${_light_json}" "${CMAKE_CURRENT_SOURCE_DIR}/level3d/assets/stage.glb"
		VERBATIM
		COMMENT "level3d: baking assets/stage.lighting.json -> stage.lighting.bin")
	add_custom_target(level3d_lightbake ALL DEPENDS "${_light_stamp}")
	add_dependencies(level3d level3d_lightbake)
endif()
mitiru_add_example(gi3d)           # 動く光の GI (DDGI): 扉から差す朝日と持ち歩く松明の照り返しが毎フレーム部屋を染め直す
# gi3d の焼いた光 (扉を閉めた部屋) も mitiru_lightbake で焼く。動く GI の最初の値と、G で比べる焼いた光になる
if(TARGET gi3d AND TARGET mitiru_lightbake AND TARGET mitiru_host)
	set(_gi3d_json "${CMAKE_CURRENT_SOURCE_DIR}/gi3d/assets/room.lighting.json")
	set(_gi3d_stamp "${CMAKE_CURRENT_BINARY_DIR}/gi3d_lightbake_$<CONFIG>.stamp")
	add_custom_command(
		OUTPUT  "${_gi3d_stamp}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:mitiru_host>/gi3d/assets"
		COMMAND mitiru_lightbake "${_gi3d_json}" -o "$<TARGET_FILE_DIR:mitiru_host>/gi3d/assets/room.lighting.bin"
		COMMAND ${CMAKE_COMMAND} -E touch "${_gi3d_stamp}"
		DEPENDS mitiru_lightbake "${_gi3d_json}"
		VERBATIM
		COMMENT "gi3d: baking assets/room.lighting.json -> room.lighting.bin")
	add_custom_target(gi3d_lightbake ALL DEPENDS "${_gi3d_stamp}")
	add_dependencies(gi3d gi3d_lightbake)
endif()
mitiru_add_example(outdoor3d)      # 屋外の 1 枚 (mitiru/terrain/): world.json の地形・草・岩と松・水面を drawOutdoor で描き、同じファイルで歩く
mitiru_add_example(views3d)        # 別のカメラの絵 (副ビュー): 小さな地図を RML の <img src="view3d:0"/> に、監視カメラをモニターの板に
mitiru_add_example(coop3d)         # 2 人で画面を分ける: 副ビュー 2 枚に、それぞれのカメラで灯り・粒・影まで描く

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
# enemy_ai は、敵の AI の部品 (mitiru/gameai/) の見本。箱の表からナビメッシュを DLL の中で焼くので Recast を link する
if(TARGET mitiru_nav_bake)
	mitiru_add_example(enemy_ai)
	if(TARGET enemy_ai)
		target_link_libraries(enemy_ai PRIVATE mitiru_nav_bake)
	endif()
endif()
# crowd は、120 体の群衆 (NavCrowd) と扉の障害物。箱の表から .navcache を DLL の中で焼くので Recast を link する
if(TARGET mitiru_nav_bake)
	mitiru_add_example(crowd)
	if(TARGET crowd)
		target_link_libraries(crowd PRIVATE mitiru_nav_bake)
	endif()
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
