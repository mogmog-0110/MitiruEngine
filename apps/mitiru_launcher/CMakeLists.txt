# Game-as-DLL として実運用されている GUI ランチャー mitiru_launcher (v0.2.0 / BAA)
# ランチャー自体が mitiru モジュールであり、mitiru_host.exe はほかの
# ユーザーゲームと同様に、その DLL を読み込む。その「ゲームプレイ」は、
# 登録済みのゲーム DLL の一覧表示と、それらを実行する子 mitiru_host
# プロセスの起動を行うプロジェクト管理である。
# ランチャー DLL は、エンジンの C++ API を一切呼び出さない。必要な
# ホストサービスはすべて、インテントのフィールド (状態のプッシュ、action の受け取り)、
# または Win32 (CreateProcess, ShellExecute, GetOpenFileName,
# SHGetKnownFolderPath) から直接利用する。これらはエンジン API ではない。

add_library(mitiru_launcher SHARED mitiru_launcher_dll.cpp)
target_link_libraries(mitiru_launcher PRIVATE mitiru)

if(MSVC)
	target_compile_options(mitiru_launcher PRIVATE /bigobj)
	# Win32 のファイルダイアログと Shell API
	target_link_libraries(mitiru_launcher PRIVATE comdlg32 shell32 ole32)
endif()

# ランチャーと起動可能なサンプルゲームの両方が 1 つのツリーに収まるように、
# mitiru_host の隣に配置する。
# <host_dir>/
# mitiru_host.exe
# mitiru_launcher/
# mitiru_launcher.dll
# assets/ui/main.rml

if(TARGET mitiru_host)
	set(_launcher_runtime_dir "$<TARGET_FILE_DIR:mitiru_host>/mitiru_launcher")

	set_target_properties(mitiru_launcher PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${_launcher_runtime_dir}"
		LIBRARY_OUTPUT_DIRECTORY "${_launcher_runtime_dir}")

	# === アセットの自動配置 (assets/ 以下のいずれかのファイルが変更されたときに再配置) ===
	# POST_BUILD は DLL が変更されたときにしか実行されない。assets/ui/main.rml を
	# 編集しただけでは古いコピーが残る不具合を防ぐため、ソースの変更を追跡する
	# custom_command(OUTPUT) + custom_target(ALL) により、スタンプを介して依存させる。
	# スタンプは build/ 配下 (ジェネレーター式を含まないパス) に置き、
	# 実際の配置先 (_launcher_runtime_dir) は COMMAND 内でジェネレーター式
	# として評価させる。OUTPUT ではジェネレーター式を使えないためである。
	file(GLOB_RECURSE _mitiru_launcher_assets CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
	set(_mitiru_launcher_stamp "${CMAKE_CURRENT_BINARY_DIR}/mitiru_launcher_assets.stamp")
	add_custom_command(
		OUTPUT  "${_mitiru_launcher_stamp}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_CURRENT_SOURCE_DIR}/assets"
			"${_launcher_runtime_dir}/assets"
		COMMAND ${CMAKE_COMMAND} -E touch "${_mitiru_launcher_stamp}"
		DEPENDS ${_mitiru_launcher_assets}
		VERBATIM
		COMMENT "mitiru_launcher: deploying assets/ (source-tracked)")
	add_custom_target(mitiru_launcher_assets ALL
		DEPENDS "${_mitiru_launcher_stamp}")
	add_dependencies(mitiru_launcher mitiru_launcher_assets)

	add_dependencies(mitiru_launcher mitiru_host)
endif()
