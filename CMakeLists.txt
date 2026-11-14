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
    src/gen1/acr_gen1_anim.c)
target_include_directories(acr_gen1 PUBLIC src/gen1)
target_link_libraries(acr_gen1 PUBLIC acr_core)

# Disc importer.
add_library(acr_import_lib STATIC
    tools/acr_import/acr_disc.c
    tools/acr_import/acr_import.c
    tools/acr_import/acr_parts_moa.c
    tools/acr_import/acr_motion_moa.c)
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
    add_executable(armored-corer WIN32 src/platform/sdl3/main.c)
    target_link_libraries(armored-corer PRIVATE acr_core SDL3::SDL3-static)
endif()
