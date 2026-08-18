cmake_minimum_required(VERSION 3.24)
project(JixelLight VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Quick Qml QuickControls2 Sql Test)
find_package(libraw CONFIG REQUIRED)
qt_standard_project_setup(REQUIRES 6.5)

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE JIXELLIGHT_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT JIXELLIGHT_GIT_COMMIT)
    set(JIXELLIGHT_GIT_COMMIT "unknown")
endif()

qt_add_executable(JixelLight
    app/main.cpp
    app/PhotoController.cpp
    core/image/ProcessedImageProvider.cpp
    core/pipeline/ImagePipeline.cpp
    core/raw/RawDecoder.cpp
    core/scopes/ScopesEngine.cpp
    core/project/ProjectDatabase.cpp
    diagnostics/ActionTrace.cpp
    diagnostics/LoggingEngine.cpp
    diagnostics/CrashReporter.cpp
    diagnostics/ZipStoreWriter.cpp
    diagnostics/DiagnosticBundle.cpp
)

set_source_files_properties(ui/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(ui/HistogramView.qml PROPERTIES QT_RESOURCE_ALIAS HistogramView.qml)
set_source_files_properties(ui/AdjustmentSlider.qml PROPERTIES QT_RESOURCE_ALIAS AdjustmentSlider.qml)

qt_add_qml_module(JixelLight
    URI JixelLight
    VERSION 1.0
    QML_FILES
        ui/Main.qml
        ui/HistogramView.qml
        ui/AdjustmentSlider.qml
)

target_include_directories(JixelLight PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_definitions(JixelLight PRIVATE
    JIXELLIGHT_VERSION="${PROJECT_VERSION}-alpha.3"
    JIXELLIGHT_GIT_COMMIT="${JIXELLIGHT_GIT_COMMIT}"
)

target_link_libraries(JixelLight PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickControls2
    Qt6::Sql
    libraw::raw_r
)

if(WIN32)
    set_target_properties(JixelLight PROPERTIES WIN32_EXECUTABLE TRUE)
elseif(APPLE)
    set_target_properties(JixelLight PROPERTIES MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "JixelLight"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.jossxxx.jixellight")
endif()

install(TARGETS JixelLight
    BUNDLE DESTINATION .
    RUNTIME DESTINATION .)

enable_testing()

qt_add_executable(JixelLightCoreTests
    tests/CoreTests.cpp
    app/PhotoController.cpp
    core/image/ProcessedImageProvider.cpp
    core/pipeline/ImagePipeline.cpp
    core/raw/RawDecoder.cpp
    core/scopes/ScopesEngine.cpp
    core/project/ProjectDatabase.cpp
    diagnostics/ActionTrace.cpp
    diagnostics/LoggingEngine.cpp
    diagnostics/ZipStoreWriter.cpp
    diagnostics/DiagnosticBundle.cpp
)
target_include_directories(JixelLightCoreTests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(JixelLightCoreTests PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Quick
    Qt6::Sql
    Qt6::Test
    libraw::raw_r
)
add_test(NAME core-tests COMMAND JixelLightCoreTests)
