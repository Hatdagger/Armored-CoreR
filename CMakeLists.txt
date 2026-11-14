cmake_minimum_required(VERSION 3.20)
project(ArmoredCoreR C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(ACR_BUILD_GAME "Build the SDL3 window shell (downloads SDL3)" ON)

if(MSVC)
    add_compile_options(/W4 /D_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Core: no operating-system calls, so it ports to the Xbox (nxdk) unchanged.
add_library(acr_core STATIC
    src/core/acr_clock.c
    src/core/acr_pools.c)
target_include_directories(acr_core PUBLIC src/core)

# Gen 1 (Armored Core, Project Phantasma, Master of Arena) rules.
add_library(acr_gen1 STATIC
    src/gen1/acr_gen1_compile.c
    src/gen1/acr_gen1_fit.c
    src/gen1/acr_gen1_anim.c
    src/gen1/acr_gen1_sim.c
    src/gen1/acr_gen1_battle.c)
target_include_directories(acr_gen1 PUBLIC src/gen1)
target_link_libraries(acr_gen1 PUBLIC acr_core)

# Renderer: CPU lighting (PS1-style) and an OpenGL 2.1 back end loaded at
# run time, so nothing links against a GL library.
add_library(acr_light STATIC src/render/acr_light.c)
target_include_directories(acr_light PUBLIC src/render)

# Models: PNG reader, skeletons, joint animations, OBJ/MTL (no OS calls).
add_library(acr_model STATIC src/model/acr_png.c src/model/acr_model.c)
target_include_directories(acr_model PUBLIC src/model)
if(NOT MSVC)
    target_link_libraries(acr_model PUBLIC m)
endif()

# Disc importer.
add_library(acr_import_lib STATIC
    tools/acr_import/acr_disc.c
    tools/acr_import/acr_import.c
    tools/acr_import/acr_parts_moa.c
    tools/acr_import/acr_motion_moa.c
    tools/acr_import/acr_models_moa.c)
target_include_directories(acr_import_lib PUBLIC tools/acr_import)
target_link_libraries(acr_import_lib PUBLIC acr_gen1)
add_executable(acr-import tools/acr_import/main.c)
target_link_libraries(acr-import PRIVATE acr_import_lib)

enable_testing()
add_executable(test_core tests/test_core.c)
target_link_libraries(test_core PRIVATE acr_core)
add_test(NAME core COMMAND test_core)
add_executable(test_gen1 tests/test_gen1.c)
target_link_libraries(test_gen1 PRIVATE acr_gen1)
add_test(NAME gen1 COMMAND test_gen1)
add_executable(test_gen1_fit tests/test_gen1_fit.c)
target_link_libraries(test_gen1_fit PRIVATE acr_gen1)
add_test(NAME gen1_fit COMMAND test_gen1_fit)
add_executable(test_gen1_anim tests/test_gen1_anim.c)
target_link_libraries(test_gen1_anim PRIVATE acr_gen1)
add_test(NAME gen1_anim COMMAND test_gen1_anim)
add_executable(test_gen1_sim tests/test_gen1_sim.c)
target_link_libraries(test_gen1_sim PRIVATE acr_gen1)
add_test(NAME gen1_sim COMMAND test_gen1_sim)
add_executable(test_gen1_battle tests/test_gen1_battle.c)
target_link_libraries(test_gen1_battle PRIVATE acr_gen1)
add_test(NAME gen1_battle COMMAND test_gen1_battle)
add_executable(test_light tests/test_light.c)
target_link_libraries(test_light PRIVATE acr_light)
add_test(NAME light COMMAND test_light)
add_executable(test_models tests/test_models.c)
target_link_libraries(test_models PRIVATE acr_import_lib)
add_test(NAME models COMMAND test_models ${CMAKE_CURRENT_BINARY_DIR}/test_models.png)
add_executable(test_model tests/test_model.c)
target_link_libraries(test_model PRIVATE acr_model acr_import_lib)
add_test(NAME model COMMAND test_model ${CMAKE_CURRENT_BINARY_DIR}/test_model.png)
add_executable(test_import tests/test_import.c)
target_link_libraries(test_import PRIVATE acr_import_lib)
add_test(NAME import COMMAND test_import ${CMAKE_CURRENT_BINARY_DIR}/test_import_work)

if(ACR_BUILD_GAME)
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.2.10
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3)
    add_executable(armored-corer WIN32 src/platform/sdl3/main.c src/platform/sdl3/demo_data.c src/platform/sdl3/demo_model.c)
    add_library(acr_render_gl STATIC src/render/acr_render_gl.c)
    target_include_directories(acr_render_gl PUBLIC src/render)
    target_link_libraries(armored-corer PRIVATE acr_core acr_gen1 acr_import_lib acr_model acr_light acr_render_gl SDL3::SDL3-static)
    if(NOT MSVC)
        target_link_libraries(armored-corer PRIVATE m)
    endif()
endif()
