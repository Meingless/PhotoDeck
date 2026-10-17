cmake_minimum_required(VERSION 3.21)
project(PhotoDeck VERSION 0.10.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(MSVC)
    add_compile_options(/utf-8)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/compiler-runtime.txt" "${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}")
endif()
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets PrintSupport)
add_library(photodeck-core STATIC
    src/brushpreset.cpp src/brushpreset.h
    src/tones.cpp src/tones.h src/tonedialog.cpp src/tonedialog.h
    src/selection.cpp src/selection.h
    src/blending.cpp src/blending.h
    src/transformdialog.cpp src/transformdialog.h
    src/filters.cpp src/filters.h
    src/effectdialog.cpp src/effectdialog.h
    src/output.cpp src/output.h
    src/outputdialog.cpp src/outputdialog.h
    src/document.cpp src/document.h
    src/recoverymanager.cpp src/recoverymanager.h
    src/recoveryfiles.cpp src/recoveryfiles.h
    src/canvas.cpp src/canvas.h src/transformhandles.h
    src/mainwindow.cpp src/mainwindow.h
    src/appearance.cpp src/appearance.h)
target_include_directories(photodeck-core PUBLIC src)
target_link_libraries(photodeck-core PUBLIC Qt6::Widgets Qt6::PrintSupport Threads::Threads)
qt_add_executable(photodeck WIN32 MACOSX_BUNDLE src/main.cpp resources.qrc)
target_link_libraries(photodeck PRIVATE photodeck-core)
set_target_properties(photodeck PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME PhotoDeck
    MACOSX_BUNDLE_GUI_IDENTIFIER org.photodeck.editor
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist)
include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    qt_add_executable(photodeck-tests tests/test_photodeck.cpp resources.qrc)
    target_link_libraries(photodeck-tests PRIVATE photodeck-core Qt6::Test)
    add_test(NAME photodeck-functional COMMAND photodeck-tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-tests-native.xml,junitxml"
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-tests-native.txt,txt")
    set_tests_properties(photodeck-functional PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
install(TARGETS photodeck BUNDLE DESTINATION . RUNTIME DESTINATION bin)
if(BUILD_TESTING)
    qt_add_executable(photodeck-feature-audit tests/test_features.cpp resources.qrc)
    target_link_libraries(photodeck-feature-audit PRIVATE photodeck-core Qt6::Test)
    add_test(NAME photodeck-feature-audit COMMAND photodeck-feature-audit
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-feature-audit-native.xml,junitxml"
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-feature-audit-native.txt,txt")
    set_tests_properties(photodeck-feature-audit PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(BUILD_TESTING)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/CTestCustom.cmake" "set(CTEST_CUSTOM_MAXIMUM_PASSED_TEST_OUTPUT_SIZE 100000)\n")
    qt_add_executable(photodeck-tones-tests tests/test_tones.cpp resources.qrc)
    target_link_libraries(photodeck-tones-tests PRIVATE photodeck-core Qt6::Test)
    add_test(NAME photodeck-tones COMMAND photodeck-tones-tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-tones-tests-native.xml,junitxml"
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-tones-tests-native.txt,txt")
    set_tests_properties(photodeck-tones PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    qt_add_executable(photodeck-selections-tests tests/test_selections.cpp resources.qrc)
    target_link_libraries(photodeck-selections-tests PRIVATE photodeck-core Qt6::Test)
    add_test(NAME photodeck-selections COMMAND photodeck-selections-tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-selections-tests-native.xml,junitxml"
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-selections-tests-native.txt,txt")
    set_tests_properties(photodeck-selections PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    qt_add_executable(photodeck-blending-tests tests/test_blending.cpp resources.qrc)
    target_link_libraries(photodeck-blending-tests PRIVATE photodeck-core Qt6::Test)
    add_test(NAME photodeck-blending COMMAND photodeck-blending-tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-blending-tests-native.xml,junitxml"
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-blending-tests-native.txt,txt")
    set_tests_properties(photodeck-blending PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    qt_add_executable(photodeck-transforms-tests tests/test_transforms.cpp resources.qrc)
    target_link_libraries(photodeck-transforms-tests PRIVATE photodeck-core Qt6::Test)
    add_test(NAME photodeck-transforms COMMAND photodeck-transforms-tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-transforms-tests-native.xml,junitxml"
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-transforms-tests-native.txt,txt")
    set_tests_properties(photodeck-transforms PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    qt_add_executable(photodeck-layer-ops-tests tests/test_layer_ops.cpp resources.qrc)
    target_link_libraries(photodeck-layer-ops-tests PRIVATE photodeck-core Qt6::Test)
    add_test(NAME photodeck-layer-ops COMMAND photodeck-layer-ops-tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-layer-ops-tests-native.xml,junitxml"
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-layer-ops-tests-native.txt,txt")
    set_tests_properties(photodeck-layer-ops PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    qt_add_executable(photodeck-layer-drag-tests tests/test_layer_drag.cpp resources.qrc)
    target_link_libraries(photodeck-layer-drag-tests PRIVATE photodeck-core Qt6::Test)
    add_test(NAME photodeck-layer-drag COMMAND photodeck-layer-drag-tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-layer-drag-tests-native.xml,junitxml"
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-layer-drag-tests-native.txt,txt")
    set_tests_properties(photodeck-layer-drag PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
    qt_add_executable(photodeck-effects-tests tests/test_effects.cpp resources.qrc)
    target_link_libraries(photodeck-effects-tests PRIVATE photodeck-core Qt6::Test)
    add_test(NAME photodeck-effects COMMAND photodeck-effects-tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-effects-tests-native.xml,junitxml"
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-effects-tests-native.txt,txt")
    set_tests_properties(photodeck-effects PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    qt_add_executable(photodeck-output-tests tests/test_output.cpp resources.qrc)
    target_link_libraries(photodeck-output-tests PRIVATE photodeck-core Qt6::Test)
    add_test(NAME photodeck-output COMMAND photodeck-output-tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-output-tests-native.xml,junitxml"
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-output-tests-native.txt,txt")
    set_tests_properties(photodeck-output PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    qt_add_executable(photodeck-brushes-tests tests/test_brushes.cpp resources.qrc)
    target_link_libraries(photodeck-brushes-tests PRIVATE photodeck-core Qt6::Test)
    add_test(NAME photodeck-brushes COMMAND photodeck-brushes-tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-brushes-tests-native.xml,junitxml"
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-brushes-tests-native.txt,txt")
    set_tests_properties(photodeck-brushes PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    qt_add_executable(photodeck-presets-tests tests/test_presets.cpp resources.qrc)
    target_link_libraries(photodeck-presets-tests PRIVATE photodeck-core Qt6::Test)
    add_test(NAME photodeck-presets COMMAND photodeck-presets-tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-presets-tests-native.xml,junitxml"
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-presets-tests-native.txt,txt")
    set_tests_properties(photodeck-presets PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
endif()

if(BUILD_TESTING)
    foreach(suite IN ITEMS save_safety parser_safety ui_resilience)
        string(REPLACE "_" "-" test_name "${suite}")
        qt_add_executable(photodeck-${test_name}-tests tests/test_${suite}.cpp resources.qrc)
        target_link_libraries(photodeck-${test_name}-tests PRIVATE photodeck-core Qt6::Test)
        add_test(NAME photodeck-${test_name} COMMAND photodeck-${test_name}-tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-${test_name}-tests-native.xml,junitxml"
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-${test_name}-tests-native.txt,txt")
        set_tests_properties(photodeck-${test_name} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
    endforeach()
endif()

if(BUILD_TESTING)
    qt_add_executable(photodeck-selection-workflow-tests tests/test_selection_workflow.cpp resources.qrc)
    target_link_libraries(photodeck-selection-workflow-tests PRIVATE photodeck-core Qt6::Test)
    add_test(NAME photodeck-selection-workflow COMMAND photodeck-selection-workflow-tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-selection-workflow-tests-native.xml,junitxml"
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-selection-workflow-tests-native.txt,txt")
    set_tests_properties(photodeck-selection-workflow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
endif()

if(BUILD_TESTING)
    qt_add_executable(photodeck-positioning-tests tests/test_positioning.cpp resources.qrc)
    target_link_libraries(photodeck-positioning-tests PRIVATE photodeck-core Qt6::Test)
    add_test(NAME photodeck-positioning COMMAND photodeck-positioning-tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-positioning-tests-native.xml,junitxml"
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-positioning-tests-native.txt,txt")
    set_tests_properties(photodeck-positioning PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
    qt_add_executable(photodeck-history-navigation-tests tests/test_history_navigation.cpp resources.qrc)
    target_link_libraries(photodeck-history-navigation-tests PRIVATE photodeck-core Qt6::Test)
    add_test(NAME photodeck-history-navigation COMMAND photodeck-history-navigation-tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-history-navigation-tests-native.xml,junitxml"
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-history-navigation-tests-native.txt,txt")
    set_tests_properties(photodeck-history-navigation PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
endif()

if(BUILD_TESTING)
    foreach(suite IN ITEMS recovery recovery_files recovery_codec)
        string(REPLACE "_" "-" test_name "${suite}")
        qt_add_executable(photodeck-${test_name}-tests tests/test_${suite}.cpp resources.qrc)
        target_link_libraries(photodeck-${test_name}-tests PRIVATE photodeck-core Qt6::Test)
        add_test(NAME photodeck-${test_name} COMMAND photodeck-${test_name}-tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-${test_name}-tests-native.xml,junitxml"
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-${test_name}-tests-native.txt,txt")
        set_tests_properties(photodeck-${test_name} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
    endforeach()
endif()

if(BUILD_TESTING)
    qt_add_executable(photodeck-usability-tests tests/test_usability.cpp resources.qrc)
    target_link_libraries(photodeck-usability-tests PRIVATE photodeck-core Qt6::Test)
    add_test(NAME photodeck-usability COMMAND photodeck-usability-tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-usability-tests-native.xml,junitxml"
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-usability-tests-native.txt,txt")
    set_tests_properties(photodeck-usability PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
endif()

if(BUILD_TESTING)
    qt_add_executable(photodeck-smart-objects-tests tests/test_smart_objects.cpp resources.qrc)
    target_link_libraries(photodeck-smart-objects-tests PRIVATE photodeck-core Qt6::Test)
    add_test(NAME photodeck-smart-objects COMMAND photodeck-smart-objects-tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-smart-objects-tests-native.xml,junitxml"
        -o "${CMAKE_CURRENT_BINARY_DIR}/photodeck-smart-objects-tests-native.txt,txt")
    set_tests_properties(photodeck-smart-objects PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
endif()
