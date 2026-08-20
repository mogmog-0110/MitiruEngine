# mitiru_selfpack — 配布フォルダを mitiru_selfrun へ連結する梱包ツール (コンソール)。
add_executable(mitiru_selfpack main.cpp)
target_include_directories(mitiru_selfpack PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_compile_features(mitiru_selfpack PRIVATE cxx_std_20)
