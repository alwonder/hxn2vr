cmake_minimum_required(VERSION 3.25)
project(hxn2vr C CXX)

if(NOT (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8))
    message(FATAL_ERROR "hxn2vr currently supports Windows x64 only")
endif()
enable_language(RC)

include(cmake/glhexen2.cmake)