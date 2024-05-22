cmake_minimum_required(VERSION 3.27.0)

set(Project CABrANS)
set(CMAKE_C_STANDARD 17)

project(${Progect} LANGUAGE C)

add_executable(${Project} main.c)