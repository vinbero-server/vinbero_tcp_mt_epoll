cmake_minimum_required(VERSION 2.8)
project(vinbero_tcp_mt_epoll C)
set(CMAKE_BUILD_TYPE RelWithDebInfo)

set(vinbero_tcp_mt_epoll_version "0.3.0")
set(vinbero_tcp_mt_epoll_soversion "0.3")

if(CMAKE_VERSION VERSION_LESS "3.1")
  if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set (CMAKE_C_FLAGS "-std=gnu11 ${CMAKE_C_FLAGS}")
  endif()
else()
    set(CMAKE_C_STANDARD 11)
endif()

configure_file("${PROJECT_SOURCE_DIR}/src/${PROJECT_NAME}_Version.h.in"
               "${PROJECT_BINARY_DIR}/${PROJECT_NAME}_Version.h" @ONLY)
include_directories("${PROJECT_BINARY_DIR}")

execute_process(COMMAND git shortlog -nse --no-merges origin/master OUTPUT_FILE "${CMAKE_SOURCE_DIR}/AUTHORS")
add_custom_target(authors ALL git shortlog -nse --no-merges origin/master > "${CMAKE_SOURCE_DIR}/AUTHORS")

add_subdirectory(src)
enable_testing()
add_subdirectory(test)
