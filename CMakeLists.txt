cmake_minimum_required(VERSION 3.20)
project(ArmoredCoreR C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(ACR_BUILD_GAME "Build the SDL3 window shell (downloads SDL3)" ON)
# OpenGL ES renderer (src/render/acr_render_gles.c) instead of OpenGL 2.1:
# always on Android; on a PC it is for testing the GLES path.
if(ANDROID)
    set(ACR_RENDER_GLES ON CACHE BOOL "" FORCE)
endif()
option(ACR_RENDER_GLES "Draw with OpenGL ES (Android) instead of OpenGL 2.1" OFF)

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
    src/gen1/acr_gen1_pose.c
    src/gen1/acr_gen1_battle.c
    src/gen1/acr_gen1_mission.c
    src/gen1/acr_gen1_missions.c
    src/gen1/acr_gen1_garage.c
    src/gen1/acr_gen1_perf.c
    src/gen1/acr_gen1_ai.c
    src/gen1/acr_gen1_stage.c
    src/gen1/acr_gen1_rogue.c
    src/gen1/acr_gen1_coop.c
    src/gen1/acr_gen1_arena.c
    src/gen1/acr_gen1_match.c)
target_include_directories(acr_gen1 PUBLIC src/gen1)

# Netplay: rollback over any packet link (no sockets in here, so it ports
# to the Xbox's System Link later).
add_library(acr_net STATIC src/net/acr_rollback.c src/net/acr_msg.c src/net/acr_brief.c src/net/acr_brief_words.c src/net/acr_upnp.c)
target_include_directories(acr_net PUBLIC src/net)
target_link_libraries(acr_net PUBLIC acr_gen1)
target_link_libraries(acr_gen1 PUBLIC acr_core)

# Renderer: CPU lighting (PS1-style) and an OpenGL 2.1 back end loaded at
# run time, so nothing links against a GL library.
add_library(acr_light STATIC src/render/acr_light.c)
target_include_directories(acr_light PUBLIC src/render)

# Models: PNG reader, skeletons, joint animations, OBJ/MTL (no OS calls).
add_library(acr_model STATIC src/model/acr_png.c src/model/acr_model.c src/model/acr_aim.c)
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
    tools/acr_import/acr_models_moa.c
    tools/acr_import/acr_stages_moa.c
    tools/acr_import/acr_units_moa.c
    tools/acr_import/acr_hud_moa.c
    tools/acr_import/acr_sfx_moa.c
    tools/acr_import/acr_arena_moa.c
    tools/acr_import/acr_arena_pp.c
    tools/acr_import/acr_missions_moa.c)
target_include_directories(acr_import_lib PUBLIC tools/acr_import)
target_link_libraries(acr_import_lib PUBLIC acr_gen1)
if(NOT MSVC)
    target_link_libraries(acr_import_lib PUBLIC m)
endif()
add_executable(acr-import tools/acr_import/main.c)
target_link_libraries(acr-import PRIVATE acr_import_lib)

enable_testing()
add_executable(test_core tests/test_core.c)
target_link_libraries(test_core PRIVATE acr_core)
add_test(NAME core COMMAND test_core)
add_executable(test_battle_warn tests/test_battle_warn.c src/platform/sdl3/battle_warn.c)
add_test(NAME battle_warn COMMAND test_battle_warn)
add_executable(test_gen1 tests/test_gen1.c)
target_link_libraries(test_gen1 PRIVATE acr_gen1)
add_test(NAME gen1 COMMAND test_gen1)
add_executable(test_gen1_fit tests/test_gen1_fit.c)
target_link_libraries(test_gen1_fit PRIVATE acr_gen1)
add_test(NAME gen1_fit COMMAND test_gen1_fit)
add_executable(test_gen1_perf tests/test_gen1_perf.c)
target_link_libraries(test_gen1_perf PRIVATE acr_gen1)
add_test(NAME gen1_perf COMMAND test_gen1_perf)
add_executable(test_gen1_anim tests/test_gen1_anim.c)
target_link_libraries(test_gen1_anim PRIVATE acr_gen1)
add_test(NAME gen1_anim COMMAND test_gen1_anim)
add_executable(test_gen1_sim tests/test_gen1_sim.c)
target_link_libraries(test_gen1_sim PRIVATE acr_gen1)
add_test(NAME gen1_sim COMMAND test_gen1_sim)
add_executable(test_gen1_pose tests/test_gen1_pose.c)
target_link_libraries(test_gen1_pose PRIVATE acr_gen1)
add_test(NAME gen1_pose COMMAND test_gen1_pose)
add_executable(test_gen1_battle tests/test_gen1_battle.c)
target_link_libraries(test_gen1_battle PRIVATE acr_gen1)
if(NOT MSVC)
    target_link_libraries(test_gen1_battle PRIVATE m)
endif()
add_test(NAME gen1_battle COMMAND test_gen1_battle)
add_executable(test_gen1_units tests/test_gen1_units.c)
target_link_libraries(test_gen1_units PRIVATE acr_gen1)
if(NOT MSVC)
    target_link_libraries(test_gen1_units PRIVATE m)
endif()
add_test(NAME gen1_units COMMAND test_gen1_units)
add_executable(test_gen1_stage tests/test_gen1_stage.c)
target_link_libraries(test_gen1_stage PRIVATE acr_gen1)
add_test(NAME gen1_stage COMMAND test_gen1_stage)
# Maps made in Tinkercad (tools/maps, docs/MAPS.md): needs Python 3 for the converter.
add_executable(test_custom_stage tests/test_custom_stage.c)
target_link_libraries(test_custom_stage PRIVATE acr_gen1 acr_model)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME custom_stage COMMAND ${CMAKE_COMMAND} -DPY=${Python3_EXECUTABLE}
             -DSRC=${CMAKE_CURRENT_SOURCE_DIR} -DBIN=${CMAKE_CURRENT_BINARY_DIR}
             -DTEST=$<TARGET_FILE:test_custom_stage> -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/custom_stage.cmake)
endif()
add_executable(test_gen1_garage tests/test_gen1_garage.c)
target_link_libraries(test_gen1_garage PRIVATE acr_gen1)
add_test(NAME gen1_garage COMMAND test_gen1_garage)
add_executable(test_gen1_ai tests/test_gen1_ai.c)
target_link_libraries(test_gen1_ai PRIVATE acr_gen1)
add_test(NAME gen1_ai COMMAND test_gen1_ai)
add_executable(test_gen1_rogue tests/test_gen1_rogue.c)
target_link_libraries(test_gen1_rogue PRIVATE acr_gen1)
add_test(NAME gen1_rogue COMMAND test_gen1_rogue)
add_executable(test_gen1_coop tests/test_gen1_coop.c)
target_link_libraries(test_gen1_coop PRIVATE acr_gen1)
add_test(NAME gen1_coop COMMAND test_gen1_coop)
add_executable(test_gen1_arena tests/test_gen1_arena.c)
target_link_libraries(test_gen1_arena PRIVATE acr_gen1)
add_test(NAME gen1_arena COMMAND test_gen1_arena)
add_executable(test_gen1_missions tests/test_gen1_missions.c)
target_link_libraries(test_gen1_missions PRIVATE acr_gen1)
add_test(NAME gen1_missions COMMAND test_gen1_missions)
add_executable(test_rollback tests/test_rollback.c)
target_link_libraries(test_rollback PRIVATE acr_net)
add_test(NAME rollback COMMAND test_rollback)
add_executable(test_brief tests/test_brief.c)
target_link_libraries(test_brief PRIVATE acr_net)
add_test(NAME brief COMMAND test_brief)
add_executable(test_msg tests/test_msg.c)
target_link_libraries(test_msg PRIVATE acr_net)
add_test(NAME msg COMMAND test_msg)
add_executable(test_upnp tests/test_upnp.c)
target_link_libraries(test_upnp PRIVATE acr_net)
add_test(NAME upnp COMMAND test_upnp)
# The router talk itself (plain sockets): against a fake router on this PC.
add_library(acr_upnp_io STATIC src/platform/sdl3/net_upnp.c)
target_include_directories(acr_upnp_io PUBLIC src/platform/sdl3)
target_link_libraries(acr_upnp_io PUBLIC acr_net)
if(WIN32)
    target_link_libraries(acr_upnp_io PUBLIC ws2_32)
else()
    add_executable(test_upnp_io tests/test_upnp_io.c)
    target_link_libraries(test_upnp_io PRIVATE acr_upnp_io)
    add_test(NAME upnp_io COMMAND test_upnp_io)
endif()
add_executable(test_light tests/test_light.c)
target_link_libraries(test_light PRIVATE acr_light)
add_test(NAME light COMMAND test_light)
# Replays: a fight saved as its pads and played back through the same rules
# (docs/REPLAYS.md). Part of acr_net.
target_sources(acr_net PRIVATE src/net/acr_replay.c)
add_executable(test_replay tests/test_replay.c)
target_link_libraries(test_replay PRIVATE acr_net)
add_test(NAME replay COMMAND test_replay)
add_executable(test_models tests/test_models.c)
target_link_libraries(test_models PRIVATE acr_import_lib)
add_test(NAME models COMMAND test_models ${CMAKE_CURRENT_BINARY_DIR}/test_models.png)
# Lockstep netplay for LAN: 2 to 4 machines (docs/LOCKSTEP.md). Part of acr_net.
target_sources(acr_net PRIVATE src/net/acr_lockstep.c)
add_executable(test_lockstep tests/test_lockstep.c)
target_link_libraries(test_lockstep PRIVATE acr_net)
add_test(NAME lockstep COMMAND test_lockstep)
add_executable(test_model tests/test_model.c)
target_link_libraries(test_model PRIVATE acr_model acr_import_lib)
add_test(NAME model COMMAND test_model ${CMAKE_CURRENT_BINARY_DIR}/test_model.png)
add_executable(test_import tests/test_import.c)
target_link_libraries(test_import PRIVATE acr_import_lib)
add_test(NAME import COMMAND test_import ${CMAKE_CURRENT_BINARY_DIR}/test_import_work)

if(ACR_BUILD_GAME)
    include(FetchContent)
    if(ANDROID)
        # Android: SDL's Java side loads libSDL3.so and the game's libmain.so.
        set(SDL_SHARED ON CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    else()
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
    endif()
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.2.10
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3)
    set(ACR_GAME_SOURCES src/platform/sdl3/main.c src/platform/sdl3/demo_data.c src/platform/sdl3/demo_model.c
                   src/platform/sdl3/demo_stage.c src/platform/sdl3/demo_units.c src/platform/sdl3/garage.c src/platform/sdl3/hangar_scene.c src/platform/sdl3/rogue.c src/platform/sdl3/arena.c src/platform/sdl3/controls.c
                   src/platform/sdl3/settings.c src/platform/sdl3/update.c
                   src/platform/sdl3/battle_hud.c src/platform/sdl3/battle_warn.c src/platform/sdl3/net_link.c
                   src/platform/sdl3/net_menu.c src/platform/sdl3/net_brief_ui.c
                   src/platform/sdl3/fx.c src/platform/sdl3/sfx.c src/platform/sdl3/rogue_sound.c
                   src/platform/sdl3/disc_pick.c
                   src/platform/sdl3/vs_screen.c
                   src/platform/sdl3/missions.c)
    if(ANDROID)
        # The game is libmain.so; SDL's Java activity starts it.
        add_library(armored-corer SHARED ${ACR_GAME_SOURCES})
        set_target_properties(armored-corer PROPERTIES OUTPUT_NAME main)
        set(ACR_SDL_TARGET SDL3::SDL3-shared)
    else()
        add_executable(armored-corer WIN32 ${ACR_GAME_SOURCES})
        set(ACR_SDL_TARGET SDL3::SDL3-static)
    endif()
    if(ACR_RENDER_GLES)
        add_library(acr_render_gl STATIC src/render/acr_render_gles.c)
        target_compile_definitions(armored-corer PRIVATE ACR_GLES=1)
    else()
        add_library(acr_render_gl STATIC src/render/acr_render_gl.c)
    endif()
    target_include_directories(acr_render_gl PUBLIC src/render)
    target_link_libraries(armored-corer PRIVATE acr_core acr_gen1 acr_import_lib acr_model acr_light acr_render_gl acr_net acr_upnp_io ${ACR_SDL_TARGET})
    if(WIN32)
        target_link_libraries(armored-corer PRIVATE ws2_32)
    endif()
    if(NOT MSVC)
        target_link_libraries(armored-corer PRIVATE m)
    endif()
endif()
