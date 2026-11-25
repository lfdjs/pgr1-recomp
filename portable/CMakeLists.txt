add_library(pgr_portable_host STATIC src/host.cpp)
target_include_directories(pgr_portable_host PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(pgr_portable_host PUBLIC cxx_std_20)

if(BUILD_TESTING)
    # EN: The coherency core needs no SDK, graphics API or proprietary assets.
    # PT-BR: O núcleo de coerência dispensa SDK, API gráfica e assets proprietários.
    find_package(Threads REQUIRED)
    add_executable(pgr_portable_guest_memory_tests tests/memory/test_guest_memory_tracker.cpp)
    target_include_directories(pgr_portable_guest_memory_tests PRIVATE include linux/gldirect)
    target_link_libraries(pgr_portable_guest_memory_tests PRIVATE Threads::Threads)
    if(MSVC)
        target_compile_options(pgr_portable_guest_memory_tests PRIVATE /W4 /UNDEBUG)
    else()
        target_compile_options(pgr_portable_guest_memory_tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    endif()
    add_test(NAME pgr_portable_guest_memory_contract COMMAND pgr_portable_guest_memory_tests)
    add_executable(pgr_portable_host_tests tests/host_tests.cpp)
    target_link_libraries(pgr_portable_host_tests PRIVATE pgr_portable_host)
    add_test(NAME pgr_portable_host_tests COMMAND pgr_portable_host_tests)
endif()

# Validate the *real* SDK's platform-neutral interfaces without compiling the game.
# The CI host-only job may omit submodules; in that case this test is skipped.
if(BUILD_TESTING AND EXISTS "${CMAKE_SOURCE_DIR}/sdk/runtime/gpu/backend.h")
    add_executable(pgr_portable_sdk_contract_tests tests/sdk_contract_tests.cpp)
    target_include_directories(pgr_portable_sdk_contract_tests PRIVATE "${CMAKE_SOURCE_DIR}/sdk/runtime")
    target_compile_features(pgr_portable_sdk_contract_tests PRIVATE cxx_std_20)
    add_test(NAME pgr_portable_sdk_contract_tests COMMAND pgr_portable_sdk_contract_tests)
endif()
