cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)

project(Mafia1Online CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/")
add_subdirectory(code)
