project (Tone C)

cmake_minimum_required(VERSION 2.8.11)

INCLUDE (../build/SynchronetMacros.cmake)
INCLUDE (CheckIncludeFiles)

add_executable(netcw netcw.c)
require_lib(netcw xpdev)
require_lib(netcw comio)

install(TARGETS netcw DESTINATION bin)
