include(FetchContent)
FetchContent_Declare(Catch2
	GIT_REPOSITORY https://github.com/catchorg/Catch2.git
	GIT_TAG v3.8.0
	GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

add_executable(mml_tests TestMml.cpp)
target_link_libraries(mml_tests PRIVATE mitiru_mml::mitiru_mml Catch2::Catch2WithMain)

if(WIN32)
	target_link_libraries(mml_tests PRIVATE winmm.lib cabinet.lib)
endif()

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(mml_tests)
