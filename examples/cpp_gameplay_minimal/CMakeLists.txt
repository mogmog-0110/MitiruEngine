add_executable(mitiru_cpp_gameplay_minimal main.cpp)
target_link_libraries(mitiru_cpp_gameplay_minimal PRIVATE Mitiru::mitiru)
set_target_properties(mitiru_cpp_gameplay_minimal PROPERTIES
	FOLDER "examples"
)
