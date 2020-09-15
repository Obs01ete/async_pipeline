cmake_minimum_required(VERSION 2.8 FATAL_ERROR)
project(async_pipeline)

add_executable(async-pipeline-with-functions async_pipeline_with_functions.cpp)
target_link_libraries(async-pipeline-with-functions -lpthread)

SET(COMPILE_FLAGS "-std=c++11")
add_definitions(${COMPILE_FLAGS})
