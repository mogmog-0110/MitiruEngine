include(FetchContent)
FetchContent_Declare(Catch2
	GIT_REPOSITORY https://github.com/catchorg/Catch2.git
	GIT_TAG v3.8.0
	GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

# チップごとの検査 (TestChip*.cpp) は、足すだけで拾う
file(GLOB MML_CHIP_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/TestChip*.cpp)
add_executable(mml_tests TestMml.cpp TestFoundation.cpp TestOpnaFoundation.cpp TestPitch.cpp TestStreamMulti.cpp ${MML_CHIP_TEST_SOURCES})
target_link_libraries(mml_tests PRIVATE mitiru_mml::mitiru_mml Catch2::Catch2WithMain)

if(WIN32)
	target_link_libraries(mml_tests PRIVATE winmm.lib cabinet.lib)
endif()

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(mml_tests)

# レンダリングの速度とハッシュを測る道具 (Release でビルドして使う)
add_executable(mml_bench MmlBench.cpp)
target_link_libraries(mml_bench PRIVATE mitiru_mml::mitiru_mml)

# 現代的な音源 (SF2 サンプラー、シンセ、ドラムシンセ) と制作の機能の検査
add_executable(mml_modern_tests TestModernSyntax.cpp TestModernSampler.cpp TestModernSynth.cpp TestModernDrums.cpp TestModernMix.cpp TestModernSong.cpp TestModernHash.cpp TestIntegratedSong.cpp TestRealSf2.cpp TestStreamModern.cpp TestGrammar.cpp TestStreamMacro.cpp TestSend.cpp TestMacroArgs.cpp)
target_link_libraries(mml_modern_tests PRIVATE mitiru_mml::mitiru_mml Catch2::Catch2WithMain)
# ハッシュの表 (modern_hashes.txt) の場所
target_compile_definitions(mml_modern_tests PRIVATE MITIRU_MML_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
	MITIRU_MML_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../examples")
if(WIN32)
	target_link_libraries(mml_modern_tests PRIVATE winmm.lib cabinet.lib)
endif()
catch_discover_tests(mml_modern_tests)

# 現代的な曲の例を WAV (ミックスとステム) に書く道具 (Release でビルドして使う)
add_executable(mml_modern_demo ModernDemo.cpp)
target_link_libraries(mml_modern_demo PRIVATE mitiru_mml::mitiru_mml)
