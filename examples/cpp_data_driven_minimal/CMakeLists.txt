add_executable(mitiru_cpp_data_driven_minimal main.cpp)
target_link_libraries(mitiru_cpp_data_driven_minimal PRIVATE Mitiru::mitiru)
set_target_properties(mitiru_cpp_data_driven_minimal PROPERTIES
	FOLDER "examples"
)
