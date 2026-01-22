cmake_minimum_required(VERSION 3.16)
project(chess_train CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(chess_ai main.cpp)
add_executable(trainer trainer.cpp)
