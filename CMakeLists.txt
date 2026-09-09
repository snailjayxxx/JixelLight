cmake_minimum_required(VERSION 3.24)
project(JixelLight VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
find_package(Qt6 6.8.3 REQUIRED COMPONENTS Core Concurrent Gui Widgets Quick Qml QuickControls2 Sql Test ShaderTools)
qt_standard_project_setup(REQUIRES 6.8)

# vcpkg on Windows/macOS; distribution packages or a supplied prefix on Linux.
find_package(libraw CONFIG QUIET)
if(NOT TARGET libraw::raw_r)
    find_path(LIBRAW_INCLUDE_DIR libraw/libraw.h REQUIRED)
    find_library(LIBRAW_LIBRARY NAMES raw_r raw REQUIRED)
    add_library(libraw::raw_r UNKNOWN IMPORTED)
    set_target_properties(libraw::raw_r PROPERTIES IMPORTED_LOCATION "${LIBRAW_LIBRARY}" INTERFACE_INCLUDE_DIRECTORIES "${LIBRAW_INCLUDE_DIR}")
endif()
find_package(lcms2 CONFIG QUIET)
if(NOT TARGET lcms2::lcms2)
    find_path(LCMS2_INCLUDE_DIR lcms2.h REQUIRED)
    find_library(LCMS2_LIBRARY NAMES lcms2 REQUIRED)
    add_library(lcms2::lcms2 UNKNOWN IMPORTED)
    set_target_properties(lcms2::lcms2 PROPERTIES IMPORTED_LOCATION "${LCMS2_LIBRARY}" INTERFACE_INCLUDE_DIRECTORIES "${LCMS2_INCLUDE_DIR}")
endif()
find_package(exiv2 CONFIG QUIET NAMES exiv2)
if(NOT TARGET Exiv2::exiv2lib)
    find_path(EXIV2_INCLUDE_DIR exiv2/exiv2.hpp REQUIRED)
    find_library(EXIV2_LIBRARY NAMES exiv2 REQUIRED)
    add_library(Exiv2::exiv2lib UNKNOWN IMPORTED)
    set_target_properties(Exiv2::exiv2lib PROPERTIES IMPORTED_LOCATION "${EXIV2_LIBRARY}" INTERFACE_INCLUDE_DIRECTORIES "${EXIV2_INCLUDE_DIR}")
endif()
find_package(JPEG REQUIRED)
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE JIXELLIGHT_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT JIXELLIGHT_GIT_COMMIT)
    set(JIXELLIGHT_GIT_COMMIT "unknown")
endif()
add_library(JixelLightCore STATIC
    app/PhotoController.cpp
    app/LookController.cpp
    core/cache/SourceCache.cpp
    core/color/ColorManagement.cpp
    core/color/MonitorColorTransform.cpp
    core/image/ProcessedImageProvider.cpp
    core/metadata/MetadataReader.cpp
    core/look/SonyLookMetadata.cpp
    core/look/LookLut.cpp
    core/look/LookProfiles.cpp
    core/look/LookDetail.cpp
    core/look/CameraReference.cpp
    core/look/LookCalibration.cpp
    core/pipeline/ImagePipeline.cpp
    core/preview/PreviewTasks.cpp
    core/project/ProjectDatabase.cpp
    core/raw/RawDecoder.cpp
    core/scopes/ScopesEngine.cpp
    core/export/JpegExporter.cpp
    core/export/ExportQueue.cpp
    diagnostics/ActionTrace.cpp
    diagnostics/LoggingEngine.cpp
    diagnostics/PerformanceRecorder.cpp
    diagnostics/ZipStoreWriter.cpp
    diagnostics/DiagnosticBundle.cpp
)
target_include_directories(JixelLightCore PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_definitions(JixelLightCore PUBLIC JIXELLIGHT_VERSION="${PROJECT_VERSION}-alpha.11" JIXELLIGHT_GIT_COMMIT="${JIXELLIGHT_GIT_COMMIT}")
target_link_libraries(JixelLightCore PUBLIC Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::Qml Qt6::QuickControls2 Qt6::Sql libraw::raw_r lcms2::lcms2 Exiv2::exiv2lib JPEG::JPEG)
if(WIN32)
    target_link_libraries(JixelLightCore PRIVATE psapi gdi32 user32)
endif()
qt_add_executable(JixelLight app/main.cpp app/SmokeRun.cpp diagnostics/CrashReporter.cpp)
target_link_libraries(JixelLight PRIVATE JixelLightCore)
set(QML_SOURCES ui/Main.qml ui/HistogramView.qml ui/AdjustmentSlider.qml ui/CurveEditor.qml ui/PhotoCanvas.qml ui/SonyLookPanel.qml ui/CameraReferenceView.qml)
foreach(file IN LISTS QML_SOURCES)
    get_filename_component(alias ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${alias})
endforeach()
qt_add_qml_module(JixelLight URI JixelLight VERSION 1.0 QML_FILES ${QML_SOURCES})
add_library(JixelLightGpu STATIC core/gpu/GpuEngine.cpp)
target_link_libraries(JixelLightGpu PUBLIC JixelLightCore Qt6::GuiPrivate)
qt_add_shaders(JixelLightGpu "pipeline_shaders" PREFIX "/jixellight" BASE shaders GLSL "430,310es" HLSL 50 MSL 12 FILES shaders/look_horizontal.comp shaders/look_detail.comp shaders/pipeline.comp shaders/histogram.comp shaders/histogram_reduce.comp shaders/display.vert shaders/display.frag)
target_sources(JixelLight PRIVATE core/gpu/GpuPreviewItem.cpp)
target_link_libraries(JixelLight PRIVATE JixelLightGpu)
if(WIN32)
    set_target_properties(JixelLight PROPERTIES WIN32_EXECUTABLE TRUE)
elseif(APPLE)
    set_target_properties(JixelLight PROPERTIES MACOSX_BUNDLE TRUE MACOSX_BUNDLE_BUNDLE_NAME "JixelLight" MACOSX_BUNDLE_GUI_IDENTIFIER "com.jossxxx.jixellight")
endif()
install(TARGETS JixelLight BUNDLE DESTINATION . RUNTIME DESTINATION .)
enable_testing()
qt_add_executable(JixelLightCoreTests tests/CoreTests.cpp)
target_link_libraries(JixelLightCoreTests PRIVATE JixelLightCore Qt6::Test)
add_test(NAME core-tests COMMAND JixelLightCoreTests -o core-results.txt,txt -o -,txt)
qt_add_executable(JixelLightPerformanceTests tests/PerformanceTests.cpp tests/LegacyPipeline.cpp)
target_link_libraries(JixelLightPerformanceTests PRIVATE JixelLightCore Qt6::Test)
add_test(NAME performance-correctness COMMAND JixelLightPerformanceTests -o performance-results.txt,txt -o -,txt)
qt_add_executable(JixelLightBenchmark tools/Benchmark.cpp tests/LegacyPipeline.cpp)
target_link_libraries(JixelLightBenchmark PRIVATE JixelLightCore)
qt_add_executable(JixelLightGpuTests tests/GpuTests.cpp)
target_link_libraries(JixelLightGpuTests PRIVATE JixelLightGpu Qt6::Test)
add_test(NAME gpu-correctness COMMAND JixelLightGpuTests -o gpu-results.txt,txt -o -,txt)
qt_add_executable(JixelLightMonitorColorTests tests/MonitorColorTests.cpp)
target_link_libraries(JixelLightMonitorColorTests PRIVATE JixelLightCore Qt6::Test)
add_test(NAME monitor-color COMMAND JixelLightMonitorColorTests -o monitor-color-results.txt,txt -o -,txt)
qt_add_executable(JixelLightDisplayColorGpuTests tests/DisplayColorGpuTests.cpp)
target_link_libraries(JixelLightDisplayColorGpuTests PRIVATE JixelLightGpu Qt6::Test)
add_test(NAME display-color-gpu COMMAND JixelLightDisplayColorGpuTests -o display-color-gpu-results.txt,txt -o -,txt)

set_tests_properties(core-tests performance-correctness gpu-correctness monitor-color display-color-gpu PROPERTIES TIMEOUT 120)

qt_add_executable(JixelLightRawWorkerTests tests/RawWorkerTests.cpp)
target_link_libraries(JixelLightRawWorkerTests PRIVATE JixelLightCore Qt6::Test)
add_test(NAME raw-worker-stack COMMAND JixelLightRawWorkerTests -o raw-worker-results.txt,txt -o -,txt)
set_tests_properties(raw-worker-stack PROPERTIES TIMEOUT 120)
# Tests and CLI benchmarks must retain standard output on Windows.
set_target_properties(JixelLightCoreTests JixelLightPerformanceTests JixelLightGpuTests
    JixelLightMonitorColorTests JixelLightDisplayColorGpuTests JixelLightRawWorkerTests JixelLightBenchmark
    PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)

# Match the image kernel's arithmetic order to the shader's precise operations.
# Other application code remains normally optimized.
if(MSVC)
    set_source_files_properties(core/pipeline/ImagePipeline.cpp core/look/LookDetail.cpp PROPERTIES COMPILE_OPTIONS "/fp:strict")
else()
    set_source_files_properties(core/pipeline/ImagePipeline.cpp core/look/LookDetail.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()

qt_add_executable(JixelLightSonyLookTests tests/SonyLookTests.cpp)
target_link_libraries(JixelLightSonyLookTests PRIVATE JixelLightCore Qt6::Test)
add_test(NAME sony-look COMMAND JixelLightSonyLookTests -o sony-look-results.txt,txt -o -,txt)
set_tests_properties(sony-look PROPERTIES TIMEOUT 180)
set_target_properties(JixelLightSonyLookTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)

qt_add_executable(JixelLightSonyValidation tools/SonyValidation.cpp)
target_link_libraries(JixelLightSonyValidation PRIVATE JixelLightCore)
set_target_properties(JixelLightSonyValidation PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
qt_add_executable(JixelLightRawRenderMatrix tools/RawRenderMatrix.cpp)
target_link_libraries(JixelLightRawRenderMatrix PRIVATE JixelLightGpu)
set_target_properties(JixelLightRawRenderMatrix PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME sony-real-files COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tools/ValidateSonyFixtures.py $<TARGET_FILE:JixelLightSonyValidation>
    ${CMAKE_SOURCE_DIR}/tests/fixtures/sony-real-manifest.json ${CMAKE_BINARY_DIR}/sony-real-reports)
set_tests_properties(sony-real-files PROPERTIES TIMEOUT 540 SKIP_RETURN_CODE 77)
# The native GPU test consumes the actual profile fitted by the real-file test.
set_tests_properties(gpu-correctness PROPERTIES DEPENDS sony-real-files
    ENVIRONMENT "JIXELLIGHT_TEST_SONY_PROFILE=${CMAKE_BINARY_DIR}/sony-real-reports/dataset/empirical.jlook.json")
