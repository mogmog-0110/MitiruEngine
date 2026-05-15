add_executable(mitiru_hello_headless main.cpp)
target_link_libraries(mitiru_hello_headless PRIVATE Mitiru::mitiru)
set_target_properties(mitiru_hello_headless PROPERTIES
	FOLDER "examples"
)
