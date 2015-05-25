cmake_minimum_required(VERSION 2.8.4)
project(such_shared_memory)

if (WIN32)
else ()
  SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --std=c99 -g -Wall -Werror -pedantic")
endif (WIN32)

include_directories(include/such_shared_memory)

add_library(such_shared_memory SHARED
  lib/such_shared_memory/such_shared_memory.c)

add_executable(test_such_shared_memory
  app/test_such_shared_memory/test_such_shared_memory.c)

target_link_libraries(test_such_shared_memory such_shared_memory)



