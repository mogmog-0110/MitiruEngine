# subsystem 単独起動デモ (軸③、docs/SUBSYSTEMS.md)。1 subsystem = 1 exe。
#
# target 名と exe 出力先 (build/examples/mitiru_subsys_<name>/) は移設前と不変 —
# mitiru-cli (`mitiru renderer` 等) と docs/SUBSYSTEMS.md が既知 path として参照する。
foreach(_subsys renderer audio input scene)
	add_subdirectory(mitiru_subsys_${_subsys})
	set_target_properties(mitiru_subsys_${_subsys} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples/mitiru_subsys_${_subsys}")
endforeach()
