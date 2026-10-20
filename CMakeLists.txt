cmake_minimum_required(VERSION 3.20)

project(GameEngine LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(GameEngine
    src/main.c
    src/game.c
    src/player.c
    src/input.c
    src/level.c
    src/renderer/render_camera.c
    src/renderer/renderer.c
    src/renderer/render_planes.c
    src/renderer/render_visibility.c
    src/renderer/render_walls.c
    src/renderer/render_portals.c
    src/renderer/render_sector_traversal.c
    src/textures/texture.c
    src/textures/texture_manager.c
)

target_include_directories(GameEngine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(SDL3 CONFIG REQUIRED)

target_link_libraries(GameEngine PRIVATE
    SDL3::SDL3
)