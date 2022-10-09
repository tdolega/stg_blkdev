cmake_minimum_required(VERSION 3.23)
project(userlandable C)

set(CMAKE_C_STANDARD 99)

add_executable(userlandable main.c definitions.h separate.c separate.h common.c common.h utils.c utils.h main.h)
