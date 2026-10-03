# mitiru_start は、配布用 GUI ランチャのスタブ (CEF / engine 非依存、Win32 のみ)。
# mitiru dist がトップ階層に <game>.exe としてコピーし、data\mitiru_host.exe を
# コンソール窓なしで起動する。詳細は apps/mitiru_start/main.cpp の冒頭を参照。
if(WIN32)
	add_executable(mitiru_start WIN32 main.cpp)
	# engine にも CEF にも link しないため、標準 / utf-8 を自前で指定する
	# (engine を link しないと CMAKE_CXX_STANDARD が引き継がれず既定の標準になり、
	#  <filesystem> / Win32 API がコンパイルできない)。
	target_compile_features(mitiru_start PRIVATE cxx_std_20)
	# engine には link しないが、純ヘッダ (platform/Utf8Args.hpp) だけ使う。
	target_include_directories(mitiru_start PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../include")
	target_link_options(mitiru_start PRIVATE /SUBSYSTEM:WINDOWS)
	# 配布物のトップに data/ の外で単独で置くので、VC ランタイムの DLL を要らない形にする
	set_property(TARGET mitiru_start PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	if(MSVC)
		# 日本語コメントの CP932 での誤読を防ぐ。
		target_compile_options(mitiru_start PRIVATE /utf-8)
	endif()
endif()
