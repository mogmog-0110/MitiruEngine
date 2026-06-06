# mitiru_start — 配布用 GUI ランチャ stub (CEF / engine 非依存、Win32 のみ)。
# mitiru dist がトップ階層に <game>.exe としてコピーし、data\mitiru_host.exe を
# コンソール窓なしで起動する。詳細は examples/mitiru_start/main.cpp 冒頭。
if(WIN32)
	add_executable(mitiru_start WIN32 main.cpp)
	# engine にも CEF にも link しないので、標準 / utf-8 を自前で指定する
	# (engine を link しないと CMAKE_CXX_STANDARD が伝播せず既定標準になり
	#  <filesystem> / Win32 API がコンパイルできない)。
	target_compile_features(mitiru_start PRIVATE cxx_std_20)
	target_link_options(mitiru_start PRIVATE /SUBSYSTEM:WINDOWS)
	if(MSVC)
		# JP コメントの CP932 誤読を防ぐ。
		target_compile_options(mitiru_start PRIVATE /utf-8)
	endif()
endif()
