cmake_minimum_required(VERSION 3.25)
project(pgr1_vulkan_native_milestone LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(PGR1_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
# In FindVulkan, Headers is an imported TARGET, not a component.
find_package(Vulkan REQUIRED)
find_program(PGR1_VK_GLSLANG glslangValidator REQUIRED)
find_package(SDL3 3.2 CONFIG QUIET)
if(NOT TARGET SDL3::SDL3)
  set(PGR1_SDL_SOURCE "${PGR1_ROOT}/build/linux/_deps/sdl3-src")
  if(NOT EXISTS "${PGR1_SDL_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR "SDL3 >=3.2 unavailable: run 'bash scripts/portable-dev.sh linux-tools' or install SDL3 development files")
  endif()
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  set(SDL_UNIX_CONSOLE_BUILD OFF CACHE BOOL "" FORCE)
  set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
  set(SDL_RENDER OFF CACHE BOOL "" FORCE)
  set(SDL_GPU OFF CACHE BOOL "" FORCE)
  set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  add_subdirectory("${PGR1_SDL_SOURCE}" "${CMAKE_BINARY_DIR}/sdl3" EXCLUDE_FROM_ALL)
endif()
set(PGR1_VK_SHADER_OUTPUT "${CMAKE_BINARY_DIR}/vk_shaders")
foreach(stage IN ITEMS vert frag)
  set(source "${CMAKE_CURRENT_SOURCE_DIR}/shaders/xyzw_color.${stage}")
  set(binary "${PGR1_VK_SHADER_OUTPUT}/xyzw_color.${stage}.spv")
  add_custom_command(OUTPUT "${binary}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${PGR1_VK_SHADER_OUTPUT}"
    COMMAND "${PGR1_VK_GLSLANG}" -V -S "${stage}" "${source}" -o "${binary}"
    DEPENDS "${source}"
    VERBATIM)
  list(APPEND PGR1_SHADER_BINARIES "${binary}")
endforeach()
add_custom_target(pgr1_vk_shaders DEPENDS ${PGR1_SHADER_BINARIES})
# 04D.4 fullscreen FXAA/sharpen pass (compiled for Vulkan 1.0).
set(PGR1_POSTFX_BINARY_DIR "${CMAKE_BINARY_DIR}/vk_shaders")
foreach(stage IN ITEMS vert frag)
  set(src "${CMAKE_CURRENT_SOURCE_DIR}/shaders/postfx.${stage}")
  set(dst "${PGR1_POSTFX_BINARY_DIR}/postfx.${stage}.spv")
  add_custom_command(OUTPUT "${dst}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${PGR1_POSTFX_BINARY_DIR}"
    COMMAND "${PGR1_VK_GLSLANG}" -V -S "${stage}" "${src}" -o "${dst}"
    DEPENDS "${src}" VERBATIM)
  list(APPEND PGR1_POSTFX_SPIRV "${dst}")
endforeach()
add_custom_target(pgr1_vk_postfx_shaders DEPENDS ${PGR1_POSTFX_SPIRV})

add_executable(pgr1_vk_wsi vk_wsi.cpp vk_geometry.cpp vk_guest_frame.cpp vk_command_bridge.cpp)
add_dependencies(pgr1_vk_wsi pgr1_vk_shaders)
target_link_libraries(pgr1_vk_wsi PRIVATE SDL3::SDL3 Vulkan::Vulkan)
target_compile_definitions(pgr1_vk_wsi PRIVATE "PGR1_VK_SHADER_DIR=\"${PGR1_VK_SHADER_OUTPUT}\"")
target_compile_options(pgr1_vk_wsi PRIVATE -Wall -Wextra -Wpedantic)
include(CTest)
set(PGR1_SDK_SOURCE_DIR "${PGR1_ROOT}/sdk" CACHE PATH "Compatible, pinned XboxXBERecomp SDK checkout")
if(EXISTS "${PGR1_SDK_SOURCE_DIR}/runtime/xdk/dsound.cpp")
  # Generate an overlay of the pinned SDK mixer in the BUILD tree, not sdk/.
  # The Xbox call IDirectSound_SetRolloffFactor was previously a no-op.
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  set(PGR1_VK_DSOUND_OVERLAY "${CMAKE_BINARY_DIR}/pgr1_vk_dsound.cpp")
  execute_process(COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/audio/build_dsound_overlay_04d5.py"
      "${PGR1_SDK_SOURCE_DIR}/runtime/xdk/dsound.cpp" "${PGR1_VK_DSOUND_OVERLAY}"
    RESULT_VARIABLE PGR1_VK_DSOUND_STATUS)
  if(NOT PGR1_VK_DSOUND_STATUS EQUAL 0)
    message(FATAL_ERROR "Vulkan audio mixer overlay incompatible with pinned SDK")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/audio/build_dsound_overlay.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/audio/build_dsound_overlay_04d5.py"
    "${PGR1_SDK_SOURCE_DIR}/runtime/xdk/dsound.cpp")
endif()

if(BUILD_TESTING)
  # EN: Compare expanded Xbox vertex attributes against the original mapping.
  # PT-BR: Compare os atributos expandidos com o mapeamento original do Xbox.
  add_executable(pgr1_vk_vertex_expander_test ../gldirect/tests/test_vertex_register_expand.cpp)
  target_include_directories(pgr1_vk_vertex_expander_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../gldirect")
  target_compile_options(pgr1_vk_vertex_expander_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
  add_test(NAME vk_vertex_register_expansion COMMAND pgr1_vk_vertex_expander_test)
  # EN: Verify that state changes invalidate safe draw-batch binding reuse.
  # PT-BR: Garanta atualizacao de estados ao trocar pipeline/blend/pass.
  add_executable(pgr1_vk_draw_bind_state_test test_draw_bind_state.cpp)
  target_compile_options(pgr1_vk_draw_bind_state_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
  add_test(NAME vk_draw_state_reuse COMMAND pgr1_vk_draw_bind_state_test)
  # EN: Verify real concurrent shader jobs without a Vulkan device.
  # PT-BR: Teste jobs de shader paralelos sem exigir uma GPU.
  find_package(Threads REQUIRED)
  add_executable(pgr1_guest_memory_tracker_test ../../tests/memory/test_guest_memory_tracker.cpp)
  target_include_directories(pgr1_guest_memory_tracker_test PRIVATE "${PGR1_ROOT}/portable/include" "${CMAKE_CURRENT_LIST_DIR}/../gldirect")
  target_link_libraries(pgr1_guest_memory_tracker_test PRIVATE Threads::Threads)
  target_compile_options(pgr1_guest_memory_tracker_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
  add_test(NAME guest_memory_coherency_contract COMMAND pgr1_guest_memory_tracker_test)
  add_executable(pgr1_flat_primitive_indices_test ../gldirect/tests/test_flat_primitive_indices.cpp)
  target_include_directories(pgr1_flat_primitive_indices_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../gldirect")
  target_compile_options(pgr1_flat_primitive_indices_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
  add_test(NAME vk_flat_primitive_indices COMMAND pgr1_flat_primitive_indices_test)
  # EN/PT-BR: Queue wake-up and bounded idle polling / Notificação e espera limitada.
  # EN/PT-BR: Exact worker comparisons / Comparações exatas no worker.
  # EN/PT-BR: Exact shader cache identity / Identidade exata do cache de shaders.
  add_executable(pgr1_vk_vertex_program_cache_test ../gldirect/tests/test_vertex_program_cache.cpp)
  target_compile_options(pgr1_vk_vertex_program_cache_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
  add_test(NAME vk_vertex_program_exact_cache COMMAND pgr1_vk_vertex_program_cache_test)
  add_executable(pgr1_vk_texture_overlap_test ../gldirect/tests/test_texture_compare_worker.cpp)
  target_link_libraries(pgr1_vk_texture_overlap_test PRIVATE Threads::Threads)
  target_compile_options(pgr1_vk_texture_overlap_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
  add_test(NAME vk_texture_compare_overlap COMMAND pgr1_vk_texture_overlap_test)
  add_executable(pgr1_vk_queue_test ../gldirect/tests/test_submission_queue.cpp ../gldirect/commands.cpp)
  target_link_libraries(pgr1_vk_queue_test PRIVATE Threads::Threads)
  target_compile_options(pgr1_vk_queue_test PRIVATE -UNDEBUG)
  add_test(NAME vk_submission_queue_wakeup COMMAND pgr1_vk_queue_test)
  add_executable(pgr1_vk_shader_pool_test ../gldirect/tests/test_shader_pair_pool.cpp)
  target_include_directories(pgr1_vk_shader_pool_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../gldirect")
  target_link_libraries(pgr1_vk_shader_pool_test PRIVATE Threads::Threads)
  target_compile_options(pgr1_vk_shader_pool_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
  add_test(NAME vk_multicore_shader_pool COMMAND pgr1_vk_shader_pool_test)
  add_executable(pgr1_vk_shader_policy_test ../gldirect/tests/test_shader_worker_policy.cpp)
  target_include_directories(pgr1_vk_shader_policy_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../gldirect")
  target_compile_options(pgr1_vk_shader_policy_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
  add_test(NAME vk_shader_worker_policy COMMAND pgr1_vk_shader_policy_test)
  # EN: Validate synchronous fallback and pipelining controls without a GPU.
  # PT-BR: Valide fallback sincrono e controle do paralelismo sem uma GPU.
  add_executable(pgr1_vk_gpu_completion_policy_test test_gpu_completion_policy.cpp)
  target_compile_options(pgr1_vk_gpu_completion_policy_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
  add_test(NAME vk_async_gpu_completion_policy COMMAND pgr1_vk_gpu_completion_policy_test)
  # EN: Hardware-independent tests for automatic and manual Vulkan GPU choice.
  # PT-BR: Testes sem GPU para a seleção Vulkan automática e manual.
  add_executable(pgr1_vk_device_selection_test test_device_selection.cpp)
  target_compile_options(pgr1_vk_device_selection_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
  add_test(NAME vk_auto_gpu_selection COMMAND pgr1_vk_device_selection_test)
  add_executable(pgr1_texture_key_test ../gldirect/tests/test_texture_snapshot_key.cpp)
  target_include_directories(pgr1_texture_key_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../gldirect")
  target_compile_options(pgr1_texture_key_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
  add_test(NAME vk_texture_snapshot_identity COMMAND pgr1_texture_key_test)
  # EN/PT-BR: Exact-byte mipmap reuse regression / Teste de reutilização exata de mipmaps.
  # EN/PT-BR: Bounded cache policy / Limites de capacidade do cache.
  add_executable(pgr1_texture_cache_policy_test ../gldirect/tests/test_texture_cache_policy.cpp)
  target_include_directories(pgr1_texture_cache_policy_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../gldirect")
  target_compile_options(pgr1_texture_cache_policy_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
  add_test(NAME vk_texture_cache_entry_policy COMMAND pgr1_texture_cache_policy_test)
  add_executable(pgr1_texture_level_reuse_test ../gldirect/tests/test_texture_level_reuse.cpp)
  target_include_directories(pgr1_texture_level_reuse_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../gldirect")
  target_compile_options(pgr1_texture_level_reuse_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
  add_test(NAME vk_texture_mip_level_identity COMMAND pgr1_texture_level_reuse_test)
  add_executable(pgr1_vk_batch_test test_draw_batch.cpp)
  target_compile_options(pgr1_vk_batch_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
  add_test(NAME vk_static_draw_batch_planner COMMAND pgr1_vk_batch_test)
  add_executable(pgr1_vk_present_counter_test test_present_counter.cpp)
  target_compile_options(pgr1_vk_present_counter_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
  add_test(NAME vk_presented_frame_counter COMMAND pgr1_vk_present_counter_test)
  if(Python3_EXECUTABLE)
    add_test(NAME vk_presentation_report COMMAND "${Python3_EXECUTABLE}" -B "${PGR1_ROOT}/portable/tests/frame_report_tests.py")
    add_test(NAME vk_cpu_profile_detection COMMAND "${Python3_EXECUTABLE}" -B "${PGR1_ROOT}/portable/tests/test_cpu_info.py")
    add_test(NAME vk_texture_tracker_report COMMAND "${Python3_EXECUTABLE}" -B "${PGR1_ROOT}/portable/tests/texture_report_tests.py")
    add_test(NAME vk_capture_comparison_report COMMAND "${Python3_EXECUTABLE}" -B "${PGR1_ROOT}/portable/tests/compare_capture_tests.py")
  endif()
  add_executable(pgr1_vk_guest_frame_test test_vk_guest_frame.cpp vk_guest_frame.cpp)
  target_compile_options(pgr1_vk_guest_frame_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
  add_test(NAME vk_guest_geometry_model COMMAND pgr1_vk_guest_frame_test)
  add_executable(pgr1_vk_command_bridge_test vk_guest_frame.cpp vk_command_bridge.cpp test_vk_command_bridge.cpp)
  target_compile_options(pgr1_vk_command_bridge_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
  add_test(NAME vk_owned_submission_bridge COMMAND pgr1_vk_command_bridge_test)
  set(PGR1_SDK_SOURCE_DIR "${PGR1_ROOT}/sdk" CACHE PATH "Compatible, pinned XboxXBERecomp SDK checkout")
  if(EXISTS "${PGR1_SDK_SOURCE_DIR}/runtime/gpu/shaders.cpp")
    find_package(Threads REQUIRED)
    execute_process(COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/audio/write_test_symbols.py" "${PGR1_VK_DSOUND_OVERLAY}"
      "${CMAKE_BINARY_DIR}/audio-test-symbols/image_symbols.h" COMMAND_ERROR_IS_FATAL ANY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/audio/write_test_symbols.py")
    add_executable(pgr1_audio_mixer_test audio/test_audio_mixer.cpp)
    target_include_directories(pgr1_audio_mixer_test PRIVATE "${CMAKE_BINARY_DIR}/audio-test-symbols" "${CMAKE_CURRENT_SOURCE_DIR}"
      "${CMAKE_CURRENT_SOURCE_DIR}/audio" "${PGR1_SDK_SOURCE_DIR}/runtime"
      "${PGR1_SDK_SOURCE_DIR}/harness" "${PGR1_SDK_SOURCE_DIR}/harness/compat")
    target_compile_definitions(pgr1_audio_mixer_test PRIVATE "PGR1_DSOUND_OVERLAY=\"${PGR1_VK_DSOUND_OVERLAY}\"")
    target_compile_options(pgr1_audio_mixer_test PRIVATE -UNDEBUG -ffunction-sections -fdata-sections)
    target_link_options(pgr1_audio_mixer_test PRIVATE -Wl,--gc-sections)
    target_link_libraries(pgr1_audio_mixer_test PRIVATE Threads::Threads)
    add_test(NAME audio_production_mixer COMMAND pgr1_audio_mixer_test)
    add_executable(pgr1_linux_settings_test ../gldirect/test_linux_settings.cpp ../gldirect/app_linux.cpp)
    target_include_directories(pgr1_linux_settings_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../gldirect/tests"
      "${PGR1_ROOT}/src" "${PGR1_SDK_SOURCE_DIR}/runtime" "${PGR1_SDK_SOURCE_DIR}/harness")
    target_compile_definitions(pgr1_linux_settings_test PRIVATE PGR1_NATIVE_VULKAN=1)
    target_compile_options(pgr1_linux_settings_test PRIVATE -UNDEBUG -ffunction-sections -fdata-sections)
    target_link_options(pgr1_linux_settings_test PRIVATE -Wl,--gc-sections)
    target_link_libraries(pgr1_linux_settings_test PRIVATE Threads::Threads)
    add_test(NAME linux_settings_persistence COMMAND pgr1_linux_settings_test)
    add_executable(pgr1_vk_renderer_test test_game_renderer.cpp game_renderer.cpp vk_postfx.cpp vk_fps_overlay.cpp
      ../gldirect/commands.cpp ../gldirect/shader_compiler.cpp ../gldirect/draw_constants.cpp
      "${PGR1_SDK_SOURCE_DIR}/runtime/gpu/shaders.cpp")
    target_include_directories(pgr1_vk_renderer_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../gldirect"
      "${PGR1_SDK_SOURCE_DIR}/runtime")
    target_compile_definitions(pgr1_vk_renderer_test PRIVATE
      "PGR1_GLSLANG_EXECUTABLE=\"${PGR1_VK_GLSLANG}\"")
    target_compile_options(pgr1_vk_renderer_test PRIVATE -UNDEBUG -ffunction-sections -fdata-sections)
    target_link_options(pgr1_vk_renderer_test PRIVATE -Wl,--gc-sections)
    target_link_libraries(pgr1_vk_renderer_test PRIVATE Threads::Threads SDL3::SDL3 Vulkan::Vulkan)
    target_compile_definitions(pgr1_vk_renderer_test PRIVATE
      "PGR1_VK_POSTFX_VERT_SPV=\"${PGR1_POSTFX_BINARY_DIR}/postfx.vert.spv\""
      "PGR1_VK_POSTFX_FRAG_SPV=\"${PGR1_POSTFX_BINARY_DIR}/postfx.frag.spv\"")
    add_dependencies(pgr1_vk_renderer_test pgr1_vk_postfx_shaders)
    # EN: Validate async fence polling and the legacy synchronous fallback.
    # PT-BR: Verifique a sondagem assincrona e o modo sincrono anterior.
    add_test(NAME vk_sdk_offscreen_pixels COMMAND pgr1_vk_renderer_test)
    set_tests_properties(vk_sdk_offscreen_pixels PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90
      ENVIRONMENT "PGR1_VK_PIPELINED=1")
    add_test(NAME vk_sdk_offscreen_pixels_sync COMMAND pgr1_vk_renderer_test)
    set_tests_properties(vk_sdk_offscreen_pixels_sync PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90
      ENVIRONMENT "PGR1_VK_PIPELINED=0")

    add_executable(pgr1_vk_backend_test backend.cpp ../gldirect/commands.cpp ../gldirect/shader_compiler.cpp
      ../gldirect/draw_constants.cpp ../gldirect/tests/backend_test.cpp
      "${PGR1_SDK_SOURCE_DIR}/runtime/gpu/shaders.cpp"
      "${PGR1_SDK_SOURCE_DIR}/runtime/gpu/vertex.cpp"
      "${PGR1_SDK_SOURCE_DIR}/runtime/gpu/texture.cpp")
    target_include_directories(pgr1_vk_backend_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../gldirect"
      "${PGR1_SDK_SOURCE_DIR}/runtime" "${PGR1_SDK_SOURCE_DIR}/harness" "${PGR1_SDK_SOURCE_DIR}/harness/compat")
    target_compile_definitions(pgr1_vk_backend_test PRIVATE XBE_RAW_VERTICES=0 PGR1_NATIVE_VULKAN=1
      "PGR1_GLSLANG_EXECUTABLE=\"${PGR1_VK_GLSLANG}\"")
    target_compile_options(pgr1_vk_backend_test PRIVATE -UNDEBUG -ffunction-sections -fdata-sections)
    target_link_options(pgr1_vk_backend_test PRIVATE -Wl,--gc-sections)
    target_link_libraries(pgr1_vk_backend_test PRIVATE Threads::Threads)
    add_test(NAME vk_sdk_backend_capture COMMAND pgr1_vk_backend_test)
    set_tests_properties(vk_sdk_backend_capture PROPERTIES TIMEOUT 90)
    add_test(NAME vk_sdk_backend_capture_audit COMMAND pgr1_vk_backend_test)
    set_tests_properties(vk_sdk_backend_capture_audit PROPERTIES TIMEOUT 90
      ENVIRONMENT "PGR1_VK_TEXTURE_COHERENCY=audit")
    add_test(NAME vk_sdk_backend_capture_tracked_fallback COMMAND pgr1_vk_backend_test)
    set_tests_properties(vk_sdk_backend_capture_tracked_fallback PROPERTIES TIMEOUT 90
      ENVIRONMENT "PGR1_VK_TEXTURE_COHERENCY=tracked;PGR1_VK_TEXTURE_DIAGNOSTIC=skip-exact")
  endif()
endif()

# The fixture stays independent of the proprietary game input and SDK. Enable
# this target explicitly through portable-vulkan.sh build-game.
option(PGR1_VK_BUILD_GAME "Build the recompiled PGR1 native Vulkan executable" OFF)
if(PGR1_VK_BUILD_GAME)
  include("${CMAKE_CURRENT_LIST_DIR}/cpu_build_profile.cmake")
  set(PGR1_SDK_SOURCE_DIR "${PGR1_ROOT}/sdk" CACHE PATH "Compatible, pinned XboxXBERecomp SDK checkout")
  set(PGR1_GENERATED_DIR "${PGR1_ROOT}/generated/game" CACHE PATH "Recompiled game sources")
  set(PGR1_RELEASE "2" CACHE STRING "Disc release selected by XBE SHA-1 (0, 1, 2)")
  if(NOT PGR1_RELEASE MATCHES "^[012]$")
    message(FATAL_ERROR "PGR1_RELEASE must be 0, 1 or 2")
  endif()
  if(NOT EXISTS "${PGR1_SDK_SOURCE_DIR}/runtime/gpu/vertex.cpp")
    message(FATAL_ERROR "Pinned SDK checkout missing: restore sdk before build-game; no SDK revision is changed by this build")
  endif()
  if(NOT EXISTS "${PGR1_GENERATED_DIR}/x86_func_mapping.cpp")
    message(FATAL_ERROR "Missing recompiled game sources: run bash scripts/portable-dev.sh recompile")
  endif()
  find_package(Threads REQUIRED)
  file(GLOB runtime_sources CONFIGURE_DEPENDS "${PGR1_SDK_SOURCE_DIR}/runtime/*/*.cpp")
  list(FILTER runtime_sources EXCLUDE REGEX "/runtime/app/|/core/input.cpp$|/core/wheel.cpp$|/gpu/d3d12.cpp$|/gpu/d3d11.cpp$|/gpu/d3d_common.cpp$|/audio/xaudio2.cpp$")
  list(FILTER runtime_sources EXCLUDE REGEX "/runtime/xdk/dsound[.]cpp$")
  list(APPEND runtime_sources "${PGR1_VK_DSOUND_OVERLAY}")
  file(GLOB harness_sources CONFIGURE_DEPENDS "${PGR1_SDK_SOURCE_DIR}/harness/*.cpp")
  list(FILTER harness_sources EXCLUDE REGEX "/main_test.cpp$|/null_backend.cpp$|/null_audio.cpp$|/app_headless.cpp$")
  file(GLOB game_sources CONFIGURE_DEPENDS "${PGR1_ROOT}/src/common/*.cpp" "${PGR1_ROOT}/src/game/*.cpp")
  file(GLOB generated_sources CONFIGURE_DEPENDS "${PGR1_GENERATED_DIR}/x86_recomp.*.cpp" "${PGR1_GENERATED_DIR}/x86_func_mapping.cpp")
  add_library(pgr1_vk_generated OBJECT ${generated_sources})
  target_include_directories(pgr1_vk_generated PRIVATE "${PGR1_SDK_SOURCE_DIR}/runtime" "${PGR1_GENERATED_DIR}")
  target_compile_options(pgr1_vk_generated PRIVATE "-O${PGR1_VK_GUEST_OPT}" -g0 -w -fno-strict-aliasing ${PGR1_CPU_FLAGS})
  if(PGR1_CPU_LOCAL)
    target_compile_definitions(pgr1_vk_generated PRIVATE "PGR1_CPU_BUILD_SIGNATURE=${PGR1_VK_CPU_SIGNATURE}")
  endif()
  add_executable(pgr1_vk_game main.cpp backend.cpp game_renderer.cpp vk_postfx.cpp vk_fps_overlay.cpp
    ../gldirect/app_linux.cpp ../gldirect/commands.cpp ../gldirect/shader_compiler.cpp
    ../gldirect/draw_constants.cpp ../gldirect/sdl_audio.cpp
    ${runtime_sources} ${harness_sources} ${game_sources} $<TARGET_OBJECTS:pgr1_vk_generated>)
  target_include_directories(pgr1_vk_game PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../gldirect"
    "${PGR1_ROOT}/src" "${CMAKE_CURRENT_SOURCE_DIR}" "${PGR1_SDK_SOURCE_DIR}/runtime" "${PGR1_SDK_SOURCE_DIR}/harness"
    "${PGR1_SDK_SOURCE_DIR}/harness/compat" "${PGR1_GENERATED_DIR}")
  target_compile_definitions(pgr1_vk_game PRIVATE PGR1_RELEASE=${PGR1_RELEASE}
    XBE_RAW_VERTICES=0 XBE_RENDER_THREAD=0 PGR1_NATIVE_VULKAN=1 PGR1_ENABLE_REFERENCE_COMPARE=1
    "PGR1_GLSLANG_EXECUTABLE=\"${PGR1_VK_GLSLANG}\"")
  target_compile_options(pgr1_vk_game PRIVATE -fno-strict-aliasing ${PGR1_CPU_FLAGS})
  if(PGR1_CPU_LOCAL)
    target_compile_definitions(pgr1_vk_game PRIVATE "PGR1_CPU_BUILD_SIGNATURE=${PGR1_VK_CPU_SIGNATURE}")
  endif()
  target_link_libraries(pgr1_vk_game PRIVATE Threads::Threads SDL3::SDL3 Vulkan::Vulkan)
  target_include_directories(pgr1_vk_game PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/audio")
  target_compile_definitions(pgr1_vk_game PRIVATE
    "PGR1_VK_POSTFX_VERT_SPV=\"${PGR1_POSTFX_BINARY_DIR}/postfx.vert.spv\""
    "PGR1_VK_POSTFX_FRAG_SPV=\"${PGR1_POSTFX_BINARY_DIR}/postfx.frag.spv\"")
  add_dependencies(pgr1_vk_game pgr1_vk_postfx_shaders)
endif()
