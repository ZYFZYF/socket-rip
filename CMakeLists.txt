cmake_minimum_required (VERSION 3.1)

project (RIP C)

add_executable(RIP rip.c)

set(CMAKE_C_FLAGS "-std=c99 -lpthread ${CMAKE_C_FLAGS}")