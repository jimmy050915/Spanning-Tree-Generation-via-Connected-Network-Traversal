cmake_minimum_required(VERSION 3.21)

project(
    NovelRelationAnalyzer
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(enable_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(novel_relation_domain
    src/domain/alias/AliasDictionary.cpp
    src/domain/chapter/ChapterCollection.cpp
    src/domain/graph/AdjacencyMultilistGraph.cpp
    src/domain/project/NovelRelationProject.cpp
    src/domain/statistics/GraphStatisticsBuilder.cpp
    src/domain/validation/ValidationReport.cpp
)
target_include_directories(novel_relation_domain PUBLIC src)
target_compile_features(novel_relation_domain PUBLIC cxx_std_17)
enable_project_warnings(novel_relation_domain)

include(CTest)
if(BUILD_TESTING)
    add_executable(domain_graph_tests
        tests/domain/test_adjacency_multilist_graph.cpp
    )
    target_link_libraries(domain_graph_tests PRIVATE novel_relation_domain)
    enable_project_warnings(domain_graph_tests)
    add_test(NAME domain_graph_tests COMMAND domain_graph_tests)

    add_executable(domain_phase_two_tests
        tests/domain/test_phase_two.cpp
    )
    target_link_libraries(domain_phase_two_tests PRIVATE novel_relation_domain)
    enable_project_warnings(domain_phase_two_tests)
    add_test(NAME domain_phase_two_tests COMMAND domain_phase_two_tests)
endif()
