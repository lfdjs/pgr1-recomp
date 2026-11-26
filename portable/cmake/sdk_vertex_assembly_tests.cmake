# EN: Differential tests use only SDK code and synthetic guest memory.
# PT-BR: Testes diferenciais usam somente código SDK e memória guest sintética.
get_filename_component(PGR1_GEOMETRY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(PGR1_VERTEX_REFERENCE "${CMAKE_CURRENT_BINARY_DIR}/sdk_vertex_reference.cpp")
add_custom_command(OUTPUT "${PGR1_VERTEX_REFERENCE}"
  COMMAND "${Python3_EXECUTABLE}" "${PGR1_GEOMETRY_ROOT}/portable/tools/build_vertex_reference.py"
    "${PGR1_SDK_SOURCE_DIR}" "${PGR1_VERTEX_REFERENCE}"
  DEPENDS "${PGR1_GEOMETRY_ROOT}/portable/tools/build_vertex_reference.py"
  VERBATIM)
add_executable(pgr1_sdk_vertex_assembly_test
  "${PGR1_GEOMETRY_ROOT}/portable/linux/gldirect/tests/test_sdk_vertex_assembly.cpp"
  "${PGR1_SDK_SOURCE_DIR}/runtime/gpu/vertex.cpp" "${PGR1_VERTEX_REFERENCE}")
target_include_directories(pgr1_sdk_vertex_assembly_test PRIVATE
  "${PGR1_GEOMETRY_ROOT}/portable/linux/gldirect" "${PGR1_SDK_SOURCE_DIR}/runtime"
  "${PGR1_SDK_SOURCE_DIR}/harness/compat")
target_compile_definitions(pgr1_sdk_vertex_assembly_test PRIVATE XBE_RAW_VERTICES=0)
target_compile_options(pgr1_sdk_vertex_assembly_test PRIVATE
  -Wall -Wextra -Wpedantic -UNDEBUG -ffunction-sections -fdata-sections)
target_link_options(pgr1_sdk_vertex_assembly_test PRIVATE -Wl,--gc-sections)
add_test(NAME sdk_vertex_assembly_differential COMMAND pgr1_sdk_vertex_assembly_test)
