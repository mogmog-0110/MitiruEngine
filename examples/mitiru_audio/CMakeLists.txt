add_executable(mitiru_audio main.cpp)
target_link_libraries(mitiru_audio PRIVATE mitiru)

# Same deploy as mitiru_renderer — the mitiru target's unconditional CEF link
# means non-CEF examples still need libcef.dll alongside to load.
if(COMMAND mitiru_add_cef_game)
    mitiru_add_cef_game(mitiru_audio)
endif()
