cmake_minimum_required(VERSION 3.0)
project(obftunnel)

set(CMAKE_CXX_STANDARD 17)
set(BUILD_SHARED_LIBS OFF)

set(CMAKE_EXE_LINKER_FLAGS "-static")
link_libraries("-static")

add_executable(obftun main.c
        obfsm.c
        obfsm.h
        log.h
        log.c
        tunnel.c
        tunnel.h)

target_link_libraries(obftun config)
target_link_libraries(obftun event)
