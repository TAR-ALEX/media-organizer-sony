cmake_minimum_required(VERSION 3.16)
project(media-organizer) # this has to be first, otherwise bad things happen

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# set(CMAKE_VERBOSE_MAKEFILE ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})

include_directories(
    "./vendor/include/"
    "./include/"
    "./src"
    "./vendor/src"
)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS "./src/*.cpp" "./vendor/src/*.cpp")

add_executable(${PROJECT_NAME} ${APP_SOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE -lstdc++fs)    


