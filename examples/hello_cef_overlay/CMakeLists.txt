add_executable(hello_cef_overlay main.cpp)
target_link_libraries(hello_cef_overlay PRIVATE mitiru)

# Copy assets/ next to the built executable so cef can resolve `assets/scene.html`.
add_custom_command(TARGET hello_cef_overlay POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:hello_cef_overlay>/assets"
    COMMENT "Copying hello_cef_overlay/assets/ to runtime directory")
