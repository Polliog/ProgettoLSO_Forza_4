cmake_minimum_required(VERSION 3.10)
project(ProgettoLSO C)

set(CMAKE_C_STANDARD 11)

add_executable(forza4_server
    server/server.c
    server/game.c
    server/client_handler.c
)
target_include_directories(forza4_server PRIVATE shared)
target_link_libraries(forza4_server pthread)

add_executable(forza4_client
    client/client.c
    client/ui.c
    client/game_loop.c
)
target_include_directories(forza4_client PRIVATE shared)
