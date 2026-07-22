cmake_minimum_required(VERSION 3.21)

project(
    NovelRelationAnalyzer
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

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
    src/domain/traversal/ChildListTree.cpp
    src/domain/traversal/GraphTraversal.cpp
    src/domain/validation/ValidationReport.cpp
    src/infrastructure/persistence/AtomicSaveRecovery.h
    src/infrastructure/persistence/BinaryProjectSerializer.cpp
    src/infrastructure/persistence/Sha256.cpp
    src/infrastructure/text/ChapterTextParser.cpp
    src/infrastructure/text/DictionaryNameExtractor.cpp
    src/infrastructure/text/DictionaryTextParser.cpp
    src/infrastructure/text/Utf8TextFileLoader.cpp
)
target_include_directories(novel_relation_domain PUBLIC src)
target_compile_features(novel_relation_domain PUBLIC cxx_std_17)
enable_project_warnings(novel_relation_domain)

add_library(novel_relation_application
    src/application/ProjectApplicationService.cpp
)
target_include_directories(novel_relation_application PUBLIC src)
target_link_libraries(novel_relation_application PUBLIC novel_relation_domain)
target_compile_features(novel_relation_application PUBLIC cxx_std_17)
enable_project_warnings(novel_relation_application)

add_library(novel_relation_presentation
    src/presentation/MainWindow.cpp
    src/presentation/MainWindow.h
    src/presentation/dialogs/AliasManagementDialog.cpp
    src/presentation/dialogs/AliasManagementDialog.h
    src/presentation/dialogs/ChapterEditorDialog.cpp
    src/presentation/dialogs/ChapterEditorDialog.h
    src/presentation/dialogs/DialogData.h
    src/presentation/dialogs/NewProjectDialog.cpp
    src/presentation/dialogs/NewProjectDialog.h
    src/presentation/dialogs/PersonManagementDialog.cpp
    src/presentation/dialogs/PersonManagementDialog.h
    src/presentation/graphics/PersonNodeItem.cpp
    src/presentation/graphics/PersonNodeItem.h
    src/presentation/graphics/RelationEdgeItem.cpp
    src/presentation/graphics/RelationEdgeItem.h
    src/presentation/graphics/RelationGraphSceneController.cpp
    src/presentation/graphics/RelationGraphSceneController.h
    src/presentation/graphics/RelationGraphView.cpp
    src/presentation/graphics/RelationGraphView.h
    src/presentation/graphics/TraversalTreeSceneController.cpp
    src/presentation/graphics/TraversalTreeSceneController.h
    src/presentation/graphics/TraversalTreeView.cpp
    src/presentation/graphics/TraversalTreeView.h
    src/presentation/interaction/IUserInteraction.h
    src/presentation/interaction/QtUserInteraction.cpp
    src/presentation/interaction/QtUserInteraction.h
    src/presentation/models/ChapterTableModel.cpp
    src/presentation/models/ChapterTableModel.h
    src/presentation/models/PersonTableModel.cpp
    src/presentation/models/PersonTableModel.h
    src/presentation/models/RelationTableModel.cpp
    src/presentation/models/RelationTableModel.h
    src/presentation/models/SearchSortProxyModel.cpp
    src/presentation/models/SearchSortProxyModel.h
    src/presentation/models/UiRows.h
)
target_include_directories(novel_relation_presentation PUBLIC src)
target_link_libraries(novel_relation_presentation
    PUBLIC novel_relation_application Qt6::Widgets
)
target_compile_features(novel_relation_presentation PUBLIC cxx_std_17)
enable_project_warnings(novel_relation_presentation)

add_executable(novel_relation_app WIN32 src/main.cpp)
target_link_libraries(novel_relation_app PRIVATE novel_relation_presentation)
enable_project_warnings(novel_relation_app)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6 REQUIRED COMPONENTS Test)

    add_executable(domain_graph_tests
        tests/domain/test_adjacency_multilist_graph.cpp
    )
    target_link_libraries(domain_graph_tests PRIVATE novel_relation_domain)
    enable_project_warnings(domain_graph_tests)
    add_test(NAME domain_graph_tests COMMAND domain_graph_tests)
    set_tests_properties(domain_graph_tests PROPERTIES
        LABELS "unit;graph-validation"
        TIMEOUT 30
    )

    add_executable(domain_phase_six_graph_validation_tests
        tests/domain/test_phase_six_graph_validation.cpp
    )
    target_link_libraries(domain_phase_six_graph_validation_tests
        PRIVATE novel_relation_domain
    )
    enable_project_warnings(domain_phase_six_graph_validation_tests)
    add_test(NAME domain_phase_six_graph_validation_tests
             COMMAND domain_phase_six_graph_validation_tests)
    set_tests_properties(domain_phase_six_graph_validation_tests PROPERTIES
        LABELS "unit;graph-validation;phase-six"
        TIMEOUT 30
    )

    add_executable(domain_phase_two_tests
        tests/domain/test_phase_two.cpp
    )
    target_link_libraries(domain_phase_two_tests PRIVATE novel_relation_domain)
    enable_project_warnings(domain_phase_two_tests)
    add_test(NAME domain_phase_two_tests COMMAND domain_phase_two_tests)
    set_tests_properties(domain_phase_two_tests PROPERTIES
        LABELS "unit;integration;graph-validation"
        TIMEOUT 30
    )

    add_executable(domain_phase_three_tests
        tests/domain/test_phase_three.cpp
    )
    target_link_libraries(domain_phase_three_tests PRIVATE novel_relation_domain)
    enable_project_warnings(domain_phase_three_tests)
    add_test(NAME domain_phase_three_tests COMMAND domain_phase_three_tests)
    set_tests_properties(domain_phase_three_tests PROPERTIES
        LABELS "unit"
        TIMEOUT 30
    )

    add_executable(performance_phase_six_tests
        tests/phase_six/test_phase_six_performance.cpp
    )
    target_link_libraries(performance_phase_six_tests
        PRIVATE novel_relation_domain
    )
    enable_project_warnings(performance_phase_six_tests)
    add_test(NAME performance_phase_six_tests
             COMMAND performance_phase_six_tests)
    set_tests_properties(performance_phase_six_tests PROPERTIES
        LABELS "performance;phase-six"
        TIMEOUT 60
    )

    add_executable(infrastructure_phase_four_tests
        tests/infrastructure/test_phase_four.cpp
    )
    target_link_libraries(infrastructure_phase_four_tests
        PRIVATE novel_relation_domain
    )
    enable_project_warnings(infrastructure_phase_four_tests)
    add_test(NAME infrastructure_phase_four_tests
             COMMAND infrastructure_phase_four_tests)
    set_tests_properties(infrastructure_phase_four_tests PROPERTIES
        LABELS "unit;integration;corruption"
        TIMEOUT 60
    )

    add_executable(infrastructure_phase_six_atomic_save_tests
        tests/infrastructure/test_phase_six_atomic_save.cpp
    )
    target_link_libraries(infrastructure_phase_six_atomic_save_tests
        PRIVATE novel_relation_domain
    )
    enable_project_warnings(infrastructure_phase_six_atomic_save_tests)
    add_test(NAME infrastructure_phase_six_atomic_save_tests
             COMMAND infrastructure_phase_six_atomic_save_tests)
    set_tests_properties(infrastructure_phase_six_atomic_save_tests PROPERTIES
        LABELS "unit;corruption;phase-six"
        TIMEOUT 30
    )

    add_executable(application_phase_five_tests
        tests/application/test_phase_five_application.cpp
    )
    target_link_libraries(application_phase_five_tests
        PRIVATE novel_relation_application
    )
    enable_project_warnings(application_phase_five_tests)
    add_test(NAME application_phase_five_tests
             COMMAND application_phase_five_tests)
    set_tests_properties(application_phase_five_tests PROPERTIES
        LABELS "integration;corruption;user-flow"
        TIMEOUT 60
    )

    add_executable(presentation_graphics_phase_five_tests
        tests/presentation/test_graphics_phase_five.cpp
    )
    target_link_libraries(presentation_graphics_phase_five_tests
        PRIVATE novel_relation_presentation Qt6::Test Qt6::Widgets
    )
    enable_project_warnings(presentation_graphics_phase_five_tests)
    add_test(NAME presentation_graphics_phase_five_tests
             COMMAND presentation_graphics_phase_five_tests)
    set_tests_properties(presentation_graphics_phase_five_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        LABELS "unit;user-flow"
        TIMEOUT 60
    )

    add_executable(presentation_ui_phase_five_tests
        tests/presentation/test_ui_phase_five.cpp
    )
    target_link_libraries(presentation_ui_phase_five_tests
        PRIVATE novel_relation_presentation Qt6::Test Qt6::Widgets
    )
    enable_project_warnings(presentation_ui_phase_five_tests)
    add_test(NAME presentation_ui_phase_five_tests
             COMMAND presentation_ui_phase_five_tests)
    set_tests_properties(presentation_ui_phase_five_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        LABELS "integration;user-flow"
        TIMEOUT 60
    )

    add_executable(presentation_phase_six_user_flow_tests
        tests/presentation/test_phase_six_user_flows.cpp
    )
    target_link_libraries(presentation_phase_six_user_flow_tests
        PRIVATE novel_relation_presentation Qt6::Test Qt6::Widgets
    )
    enable_project_warnings(presentation_phase_six_user_flow_tests)
    add_test(NAME presentation_phase_six_user_flow_tests
             COMMAND presentation_phase_six_user_flow_tests)
    set_tests_properties(presentation_phase_six_user_flow_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        LABELS "integration;user-flow;phase-six"
        TIMEOUT 60
    )
endif()
