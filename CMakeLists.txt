cmake_minimum_required(VERSION 3.16)
project(desktop_remote VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Серверная часть
add_executable(remote_server
    server/main.cpp
    server/server.cpp
    server/shell_executor.cpp
)

target_include_directories(remote_server PRIVATE ${CMAKE_SOURCE_DIR})

# Клиентская часть
add_executable(remote_client
    client/main.cpp
    client/client.cpp
)

target_include_directories(remote_client PRIVATE ${CMAKE_SOURCE_DIR})

# Для macOS/Linux
if(UNIX)
    target_link_libraries(remote_server pthread)
    target_link_libraries(remote_client pthread)
endif()

