add_executable(hello_cef_overlay main.cpp)
target_link_libraries(hello_cef_overlay PRIVATE mitiru)

# Copy assets/ next to the built executable so cef can resolve `assets/scene.html`.
add_custom_command(TARGET hello_cef_overlay POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:hello_cef_overlay>/assets"
    COMMENT "Copying hello_cef_overlay/assets/ to runtime directory")

# Deploy CEF runtime (libcef.dll + Resources/icudtl.dat + locales + helper)
# next to the exe. The mitiru_register_cef_game helper is only registered
# via find_package(Mitiru) for downstream consumers, so we replicate its
# behavior inline for in-engine examples.
if(WIN32 AND CEF_FOUND AND TARGET MitiruCefHelper)
    add_dependencies(hello_cef_overlay MitiruCefHelper)
    add_custom_command(TARGET hello_cef_overlay POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:MitiruCefHelper>"
            "$<TARGET_FILE_DIR:hello_cef_overlay>/MitiruCefHelper.exe"
        COMMENT "hello_cef_overlay: copying MitiruCefHelper.exe (CEF subprocess)")
    add_custom_command(TARGET hello_cef_overlay POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CEF_BINARY_DIR}"
            "$<TARGET_FILE_DIR:hello_cef_overlay>"
        COMMENT "hello_cef_overlay: copying CEF binary runtime")
    add_custom_command(TARGET hello_cef_overlay POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CEF_RESOURCE_DIR}"
            "$<TARGET_FILE_DIR:hello_cef_overlay>"
        COMMENT "hello_cef_overlay: copying CEF resources")
endif()
