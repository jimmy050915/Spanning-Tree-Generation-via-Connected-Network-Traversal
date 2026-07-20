cmake_minimum_required(VERSION 3.16)

project(aml_graph_course_design LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(aml_graph
    src/aml_graph.cpp
)
target_include_directories(aml_graph PUBLIC include)

if(MSVC)
    target_compile_options(aml_graph PRIVATE /W4 /permissive-)
else()
    target_compile_options(aml_graph PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(graph_app src/main.cpp)
target_link_libraries(graph_app PRIVATE aml_graph)
if(MSVC)
    target_compile_options(graph_app PRIVATE /W4 /permissive-)
else()
    target_compile_options(graph_app PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(graph_tests tests/test_graph.cpp)
    target_link_libraries(graph_tests PRIVATE aml_graph)
    if(MSVC)
        target_compile_options(graph_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(graph_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME graph_tests COMMAND graph_tests)
endif()
