add_executable(mitiru_inspector main.cpp)
target_link_libraries(mitiru_inspector PRIVATE mitiru)

# Same CEF-runtime deploy as the other axis-3/5 standalone tools — the
# engine target links libcef.dll unconditionally even when this example
# never calls into CEF.
if(COMMAND mitiru_add_cef_game)
    mitiru_add_cef_game(mitiru_inspector)
endif()
