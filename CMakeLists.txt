cmake_minimum_required(VERSION 3.21)
project(PhotoDeck VERSION 0.2.1 LANGUAGES CXX)
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
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
add_library(photodeck-core STATIC
    src/document.cpp src/document.h
    src/canvas.cpp src/canvas.h
    src/mainwindow.cpp src/mainwindow.h
    src/appearance.cpp src/appearance.h)
target_include_directories(photodeck-core PUBLIC src)
target_link_libraries(photodeck-core PUBLIC Qt6::Widgets)
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
    add_test(NAME photodeck-functional COMMAND photodeck-tests)
    set_tests_properties(photodeck-functional PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
install(TARGETS photodeck BUNDLE DESTINATION . RUNTIME DESTINATION bin)
if(BUILD_TESTING)
    qt_add_executable(photodeck-feature-audit tests/test_features.cpp resources.qrc)
    target_link_libraries(photodeck-feature-audit PRIVATE photodeck-core Qt6::Test)
    add_test(NAME photodeck-feature-audit COMMAND photodeck-feature-audit)
    set_tests_properties(photodeck-feature-audit PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(BUILD_TESTING)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/CTestCustom.cmake" "set(CTEST_CUSTOM_MAXIMUM_PASSED_TEST_OUTPUT_SIZE 100000)\n")
endif()
