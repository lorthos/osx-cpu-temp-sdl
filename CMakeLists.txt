cmake_minimum_required(VERSION 3.16)
project(osx-cpu-temp-sdl)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake_modules)

add_executable(osx-cpu-temp-sdl main.cpp smc.h)
if (APPLE)
    SET(CMAKE_EXE_LINKER_FLAGS "-framework Foundation -w")
    target_link_libraries(osx-cpu-temp-sdl "-framework OpenGL -framework IOKit")
endif ()

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIR})
target_link_libraries(osx-cpu-temp-sdl ${SDL2_LIBRARY})

find_package(SDL2_TTF REQUIRED)
include_directories(${SDL2_TTF_INCLUDE_DIRS})
target_link_libraries(osx-cpu-temp-sdl ${SDL2_TTF_LIBRARIES})

