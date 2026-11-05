add_library(pgr_portable_host STATIC src/host.cpp)
target_include_directories(pgr_portable_host PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(pgr_portable_host PUBLIC cxx_std_20)

if(BUILD_TESTING)
    add_executable(pgr_portable_host_tests tests/host_tests.cpp)
    target_link_libraries(pgr_portable_host_tests PRIVATE pgr_portable_host)
    add_test(NAME pgr_portable_host_tests COMMAND pgr_portable_host_tests)
endif()
