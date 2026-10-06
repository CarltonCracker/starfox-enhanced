# MODERN renderer: the shared VR scene (starfox_scene) drawn through SDL_GPU
# for flat-screen targets. Needs SDL3::SDL3 from the runtime build.
if(NOT TARGET starfox_scene)
    include("${CMAKE_CURRENT_LIST_DIR}/Scene.cmake")
endif()
add_library(starfox_modern STATIC src/modern/sdl_scene_packets.cpp src/modern/sdl_source_scene.cpp)
target_include_directories(starfox_modern PUBLIC include PRIVATE src)
target_compile_features(starfox_modern PUBLIC cxx_std_20)
target_link_libraries(starfox_modern PUBLIC starfox_scene SDL3::SDL3)
if(BUILD_TESTING AND Python3_EXECUTABLE)
    add_test(NAME starfox_modern_shader_freshness
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../tools/generate_modern_shaders.py" --check)
    set_tests_properties(starfox_modern_shader_freshness PROPERTIES LABELS modern TIMEOUT 30)
endif()
if(TARGET starfox_vr_game)
    # M1 harness (fork-local): SDL_GPU packets vs starfox_vr_scene_route_bench captures.
    add_executable(starfox_modern_scene_check EXCLUDE_FROM_ALL tools/check_modern_scene.cpp)
    target_link_libraries(starfox_modern_scene_check PRIVATE starfox_modern starfox_vr_game SDL3::SDL3)
endif()
