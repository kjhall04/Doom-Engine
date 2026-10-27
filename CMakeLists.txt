cmake_minimum_required(VERSION 3.20)

project(DoomEngine LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL3 CONFIG REQUIRED)

# --------------------------------------------------
# Shared engine library
# --------------------------------------------------

add_library(EngineCore STATIC
    external/cJSON/cJSON.c

    src/engine/level/level.c
    src/engine/level/level_io.c

    src/engine/textures/texture.c
    src/engine/textures/texture_manager.c

    src/engine/renderer/renderer.c
    src/engine/renderer/render_camera.c
    src/engine/renderer/render_walls.c
    src/engine/renderer/render_planes.c
    src/engine/renderer/render_visibility.c
    src/engine/renderer/render_portals.c
    src/engine/renderer/render_sector_traversal.c
)

target_include_directories(EngineCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external/cJSON
)

target_link_libraries(EngineCore PUBLIC
    SDL3::SDL3
)

# --------------------------------------------------
# Game executable
# --------------------------------------------------

add_executable(GameEngine
    src/game/main.c
    src/game/game.c
    src/game/player.c
    src/game/input.c
)

target_link_libraries(GameEngine PRIVATE
    EngineCore
)

# --------------------------------------------------
# Standalone level editor executable
# --------------------------------------------------

add_executable(LevelEditor
    src/editor/main.c
    src/editor/editor.c
    src/editor/editor_viewport.c
)

target_link_libraries(LevelEditor PRIVATE
    EngineCore
)