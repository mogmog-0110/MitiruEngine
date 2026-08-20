# mitiru_selfrun — 自己展開ランチャー (1 ファイル配布の器)。
#
# engine の C++ API に依存しない (AssetPack は純データのヘッダ)。CEF も描画も
# 使わないので、リンクは include パスだけでよい。GUI サブシステムでコンソール窓を
# 出さない。
add_executable(mitiru_selfrun WIN32 main.cpp)
target_include_directories(mitiru_selfrun PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_compile_features(mitiru_selfrun PRIVATE cxx_std_20)
