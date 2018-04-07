cmake_minimum_required (VERSION 3.1)
get_filename_component(PROJ_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
project(${PROJ_NAME})

set(CMAKE_CXX_STANDARD 11)
set(CXX_EXTENSIONS NO)
set(CXX_STANDARD_REQUIRED YES)

file(GLOB SOURCES "*/*.cpp" )
file(GLOB HEADERS "*/*.h" "*/*.hpp" )
find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h)

add_executable(${PROJ_NAME} ${SOURCES} ${HEADERS})
target_include_directories(${PROJ_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${AVCODEC_INCLUDE_DIR})
target_link_libraries(${PROJ_NAME} 
  avutil avformat avcodec
  alsa ass sdl GL GLEW
)
