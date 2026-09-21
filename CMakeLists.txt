cmake_minimum_required(VERSION 3.20)

project(GameEngine LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(GameEngine
    src/main.c
    src/game.c
    src/player.c
    src/input.c
    src/renderer.c
    src/level.c
)

target_include_directories(GameEngine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(SDL3 CONFIG REQUIRED)

target_link_libraries(GameEngine PRIVATE
    SDL3::SDL3
)