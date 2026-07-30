# MitiruEngine examples — 章立てカリキュラム (規格: docs/EXAMPLE_STANDARD.md)
#
# 章は 1 機能 1 DLL の教材。追加は mitiru_add_example(<name>) 1 行
# (+ tests/CMakeLists.txt の _example_e2e_chapters に 1 行で headless E2E も付く)。
# 学習順序は名前でなく docs/EXAMPLE_STANDARD.md の章立てと examples/README.md の表が持つ。

if(NOT TARGET mitiru)
	message(STATUS "examples/ skipped: mitiru target not defined")
	return()
endif()

# 章 example を 1 行で追加する:
#   mitiru_add_example(welcome)
#     - <name>/<name>_dll.cpp を SHARED (game DLL) でビルド
#     - mitiru_host の隣 (<host dir>/<name>/) へ deploy — host は相対 path で load できる
#     - <name>/assets/ があれば同じ dir へ source-tracked stamp 経由で配る
function(mitiru_add_example name)
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
	# assets/scene.html がある章はエンジン付属 binder/bridge JS も deploy する
	# (単一源 web/mitiru_runtime/ からの build-time copy。in-repo 複製は drift 源なので禁止)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${name}/assets/scene.html")
		set(_cefjs
			"${CMAKE_SOURCE_DIR}/web/mitiru_runtime/mitiru_cef_state.js"
			"${CMAKE_SOURCE_DIR}/web/mitiru_runtime/mitiru_bind.js"
			"${CMAKE_SOURCE_DIR}/web/mitiru_runtime/mitiru_bind_tools.js"
			"${CMAKE_SOURCE_DIR}/web/mitiru_runtime/mitiru_modal.js")
		set(_cefjs_stamp "${CMAKE_CURRENT_BINARY_DIR}/${name}_cefjs.stamp")
		add_custom_command(
			OUTPUT  "${_cefjs_stamp}"
			COMMAND ${CMAKE_COMMAND} -E make_directory "${_dir}/assets/mitiru_runtime"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_cefjs} "${_dir}/assets/mitiru_runtime"
			COMMAND ${CMAKE_COMMAND} -E touch "${_cefjs_stamp}"
			DEPENDS ${_cefjs}
			VERBATIM
			COMMENT "${name}: deploying mitiru_runtime JS (source-tracked)")
		add_custom_target(${name}_cefjs ALL DEPENDS "${_cefjs_stamp}")
		add_dependencies(${name} ${name}_cefjs)
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
mitiru_add_example(html_hud)       # 最小の操作シーン (C++ ネイティブ描画の箱庭) に HTML HUD を重ねる: ゲーム状態が text/tween/style/class/repeat を駆動、JS ゼロ (軸①、ADR 0005/0007)
mitiru_add_example(html_menu)      # binder 応用: list / dispatch / confirm
mitiru_add_example(observe)
mitiru_add_example(rewind)         # time-travel inspector (軸②、ADR 0017/0018)
mitiru_add_example(restart_save)   # requestRestart + .msav (ADR 0020/0024)
mitiru_add_example(scene3d)        # GPU 3D: 影 + WBOIT 半透明 + skybox
mitiru_add_example(model3d)        # 大規模モデル: drawModel + 自動 LOD (ADR 0027)
mitiru_add_example(anim3d)         # 骨格アニメ: drawModelBlend + クリップ時間 (ADR 0028)

# ── チュートリアル頁の題材ゲーム ───────────────────────────────────────────
# beko_run: 赤べこの横スクロールアクション。カリキュラム章 (1 機能 1 DLL) ではなく
# site/homepage/tutorial.html の実演用。複数ソースに分けるので独立 CMakeLists を持つ。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/beko_run/CMakeLists.txt")
	add_subdirectory(beko_run)
endif()

# hello_preview: `mitiru new` (hello テンプレ) の画面キャプチャ用 (install.html の頁生成専用)。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/hello_preview/hello_preview_dll.cpp")
	mitiru_add_example(hello_preview)
endif()

# welcome_start: `mitiru new` の新既定 (welcome) テンプレの実演体 = 画面キャプチャ用
# (install.html の頁生成専用)。左は C++ 描画の絵、右は HTML/CSS の操作パネル。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/welcome_start/welcome_start_dll.cpp")
	mitiru_add_example(welcome_start)
endif()

# ── 検証用 (従来形式のまま) ─────────────────────────────────────────────────
# why_demo: `mitiru why`(分岐根本原因特定) の検証用 (dev 専用、snapshot 非同梱)。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/why_demo/CMakeLists.txt")
	add_subdirectory(why_demo)
endif()
# neon_asteroids: NeonVector の発光ラインを持ち込んだ Asteroids (dev 専用、snapshot 非同梱)。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/neon_asteroids/CMakeLists.txt")
	add_subdirectory(neon_asteroids)
endif()
# 大型ゲームは examples に置かない (ここはエンジン demo / subsystem 起動のみ)。

# ── subsystem 単独起動 (軸③、単体 exe) ─────────────────────────────────────
add_subdirectory(subsys)
