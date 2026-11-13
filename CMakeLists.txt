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
# A reused build must not keep the commit from its first configuration. Resolve
# Git's paths as well, so linked worktrees and detached CI checkouts are covered.
execute_process(COMMAND git symbolic-ref --quiet HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE JIXELLIGHT_GIT_REF OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
foreach(git_path IN ITEMS HEAD packed-refs "${JIXELLIGHT_GIT_REF}")
    if(NOT git_path STREQUAL "")
        execute_process(COMMAND git rev-parse --git-path "${git_path}" WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE git_dependency OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(NOT git_dependency STREQUAL "")
            get_filename_component(git_dependency "${git_dependency}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
            if(EXISTS "${git_dependency}")
                set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${git_dependency}")
            endif()
        endif()
    endif()
endforeach()
if(NOT JIXELLIGHT_GIT_COMMIT)
    set(JIXELLIGHT_GIT_COMMIT "unknown")
endif()
add_library(JixelLightCore STATIC
    core/commands/CommandSession.cpp
    core/commands/EditRecipe.cpp
    app/PhotoController.cpp
    app/PhotoControllerMoveImport.cpp
    app/PhotoControllerXmp.cpp
    app/XmpMergeReviewDialog.cpp
    core/metadata/XmpMerge.cpp
    core/metadata/XmpCapture.cpp
    app/NativeBrushController.cpp
    core/gpu/brush/NativeBrushSource.cpp
    core/gpu/brush/NativeBrushUploadAdmission.cpp
    core/gpu/brush/NativeBrushFrame.cpp
    core/gpu/brush/NativeBrushSourceJob.cpp
    app/BrushController.cpp
    app/RelinkController.cpp
    app/LookController.cpp
    app/DisplayColorController.cpp
    app/PhotoControllerSoftProof.cpp
    core/cache/SourceCache.cpp
    core/cache/FloatSceneSourceCache.cpp
    core/cache/RenderedPreviewCache.cpp
    core/cache/FullScopesCache.cpp
    core/cache/AnalysisFrameCache.cpp
    core/cache/ScopePlotCache.cpp
    core/color/ColorManagement.cpp
    core/color/MonitorColorTransform.cpp
    core/color/SoftProof.cpp
    core/preview/SoftProofPreview.cpp
    core/image/ProcessedImageProvider.cpp
    core/image/ThumbnailProvider.cpp
    core/import/CopyImport.cpp
    core/import/ImportNaming.cpp
    core/import/CopyImportQueue.cpp
    core/import/MoveImport.cpp
    core/import/MoveImportQueue.cpp
    core/metadata/MetadataReader.cpp
    core/metadata/XmpSidecar.cpp
    core/look/SonyLookMetadata.cpp
    core/look/LookLut.cpp
    core/look/LookProfiles.cpp
    core/look/LookDetail.cpp
    core/look/CameraReference.cpp
    core/look/LookCalibration.cpp
    core/pipeline/ImagePipeline.cpp
    core/pipeline/BrushRenderRequest.cpp
    core/pipeline/SpatialClarity.cpp core/pipeline/SpatialTexture.cpp core/pipeline/SpatialDehaze.cpp core/pipeline/PointColor.cpp
    core/pipeline/SpatialDenoise.cpp
    core/pipeline/ColorStageDiagnostics.cpp
    core/pipeline/GeometryState.cpp
    core/pipeline/GeometryCorrections.cpp
    core/preview/PreviewTasks.cpp
    core/project/ProjectDatabase.cpp
    core/raw/RawDecoder.cpp
    core/raw/SensorMosaicAudit.cpp
    core/raw/SensorLinearMosaic.cpp
    core/raw/SensorFloatDevelop.cpp
    core/raw/FloatRawTrial.cpp
    core/scopes/ScopesEngine.cpp
    core/scopes/ScopePlot.cpp
    core/export/PngExporter.cpp
    core/export/RasterExporter.cpp
    core/export/JpegExporter.cpp
    core/export/ExportQueue.cpp
    core/export/ExportNaming.cpp
    diagnostics/ActionTrace.cpp
    diagnostics/LoggingEngine.cpp
    diagnostics/PerformanceRecorder.cpp
    diagnostics/ZipStoreWriter.cpp
    diagnostics/DiagnosticBundle.cpp
    diagnostics/DiagnosticReplay.cpp
)
target_include_directories(JixelLightCore PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_definitions(JixelLightCore PUBLIC JIXELLIGHT_VERSION="${PROJECT_VERSION}-alpha.11" JIXELLIGHT_GIT_COMMIT="${JIXELLIGHT_GIT_COMMIT}")
target_link_libraries(JixelLightCore PUBLIC Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::Qml Qt6::QuickControls2 Qt6::Sql libraw::raw_r lcms2::lcms2 Exiv2::exiv2lib JPEG::JPEG)
if(WIN32)
    target_link_libraries(JixelLightCore PRIVATE psapi gdi32 user32)
endif()
if(APPLE)
    target_link_libraries(JixelLightCore PRIVATE Qt6::GuiPrivate)
endif()
qt_add_executable(JixelLight app/main.cpp app/MoveImportSmokeRun.cpp app/SoftProofSmokeRun.cpp app/ColorMaskSmokeRun.cpp app/LuminanceMaskSmokeRun.cpp app/PointColorSmokeRun.cpp app/DehazeSmokeRun.cpp app/TextureSmokeRun.cpp app/ClaritySmokeRun.cpp app/DenoiseSmokeRun.cpp app/TangentialGeometrySmokeRun.cpp app/RawPressureEventSmokeRun.cpp tests/brush_tile_oracle/BrushTileOriginalPipeline.cpp app/ProjectMenuLayoutSmokeRun.cpp app/XmpMergeSmokeRun.cpp app/NativeBrushMonitorSmokeRun.cpp app/NativeBrushSonySmokeRun.cpp app/NativeBrushSmokeRun.cpp app/NativeBrushDeliverySmokeRun.cpp app/EditRecipeSmokeRun.cpp app/EditRecipeClipboardSmokeRun.cpp app/RecipeClipboardReader.cpp app/RecipeClipboardReader.h app/ShortcutHelpSmokeRun.cpp app/BrushTabletInput.cpp app/BrushTabletInput.h app/BrushPressureSmokeRun.cpp app/BrushPressureEdgesSmokeRun.cpp app/BrushRecoverySmokeRun.cpp app/BrushStabilizationSmokeRun.cpp app/SonyBrushPressureSmokeRun.cpp app/SmokeRun.cpp app/BrushSmokeRun.cpp app/BrushEraseSmokeRun.cpp diagnostics/CrashReporter.cpp)
target_link_libraries(JixelLight PRIVATE JixelLightCore)
target_link_libraries(JixelLight PRIVATE Qt6::GuiPrivate)
if(APPLE)
    enable_language(OBJCXX)
    target_sources(JixelLight PRIVATE app/GuiSmokeActivity.mm)
    target_link_libraries(JixelLight PRIVATE "-framework Foundation")
endif()
set(QML_SOURCES ui/Main.qml ui/HistogramView.qml ui/ScopePlotView.qml ui/AdjustmentSlider.qml ui/AdjustmentTransferDialog.qml ui/CopyImportDialog.qml ui/MoveImportRecoveryDialog.qml ui/CurveEditor.qml ui/PhotoCanvas.qml ui/CropOverlay.qml ui/MaskCanvasOverlay.qml ui/BrushCanvasOverlay.qml ui/BrushGuide.js ui/SonyLookPanel.qml ui/CameraReferenceView.qml ui/DevelopRightPanel.qml ui/SoftProofPanel.qml ui/LibrarySidebar.qml ui/LibraryWorkspace.qml ui/Filmstrip.qml)
foreach(file IN LISTS QML_SOURCES)
    get_filename_component(alias ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${alias})
endforeach()
qt_add_qml_module(JixelLight URI JixelLight VERSION 1.0 QML_FILES ${QML_SOURCES})
add_library(JixelLightGpu STATIC core/gpu/GpuEngine.cpp
    core/gpu/brush/NativeBrushUpload.cpp core/gpu/brush/NativeBrushFrames.cpp
    core/gpu/brush/NativeBrushHistogram.cpp core/gpu/brush/NativeBrushDiagnostics.cpp)
target_link_libraries(JixelLightGpu PUBLIC JixelLightCore Qt6::GuiPrivate)
set(JIXELLIGHT_SHADER_SOURCES
    shaders/brush_mask_coverage.comp shaders/brush_pipeline.comp
    shaders/look_horizontal.comp shaders/look_detail.comp shaders/pipeline.comp
    shaders/denoise_prepare.comp shaders/denoise_filter.comp
    shaders/clarity_prepare.comp shaders/clarity_horizontal.comp shaders/clarity_filter.comp
    shaders/dehaze_prepare.comp shaders/dehaze_horizontal.comp shaders/dehaze_vertical.comp shaders/dehaze_filter.comp
    shaders/texture_prepare.comp shaders/texture_small_horizontal.comp shaders/texture_small_vertical.comp
    shaders/texture_large_horizontal.comp shaders/texture_filter.comp
    shaders/histogram.comp shaders/histogram_reduce.comp shaders/display.vert shaders/display.frag)
qt_add_shaders(JixelLightGpu "pipeline_shaders" PREFIX "/jixellight" BASE shaders GLSL "430,310es" HLSL 50 MSL 12 FILES ${JIXELLIGHT_SHADER_SOURCES})
target_sources(JixelLight PRIVATE core/gpu/GpuPreviewItem.cpp)
target_link_libraries(JixelLight PRIVATE JixelLightGpu)
if(WIN32)
    set_target_properties(JixelLight PROPERTIES WIN32_EXECUTABLE TRUE)
elseif(APPLE)
    set_target_properties(JixelLight PROPERTIES MACOSX_BUNDLE TRUE MACOSX_BUNDLE_BUNDLE_NAME "JixelLight" MACOSX_BUNDLE_GUI_IDENTIFIER "com.jossxxx.jixellight")
endif()
install(TARGETS JixelLight BUNDLE DESTINATION . RUNTIME DESTINATION .)
enable_testing()
# QA-only byte-filter utility. Keep it independent of Qt and every rendering
# kernel; the Python validators still check each PNG chunk/CRC/row/ICC/pixel.
add_library(JixelLightPngRows SHARED tests/png_rows/PngRows.cpp)
set_target_properties(JixelLightPngRows PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
target_compile_definitions(JixelLightPngRows PRIVATE JIXELLIGHT_PNG_ROWS_COMMIT="${JIXELLIGHT_GIT_COMMIT}")
qt_add_executable(JixelLightCoreTests tests/CoreTests.cpp)
target_link_libraries(JixelLightCoreTests PRIVATE JixelLightCore Qt6::Test)
# A build dependency avoids changing the direct QtTest link requirements used
# by matching targets' PCH reuse. Tests load the actual utility via QLibrary.
add_dependencies(JixelLightCoreTests JixelLightPngRows)
add_test(NAME core-tests COMMAND JixelLightCoreTests -o core-results.txt,txt -o -,txt)
qt_add_executable(JixelLightCommandSessionTests tests/CommandSessionTests.cpp)
target_link_libraries(JixelLightCommandSessionTests PRIVATE JixelLightCore Qt6::Test)
add_test(NAME command-session COMMAND JixelLightCommandSessionTests -o command-session-results.txt,txt -o -,txt)
set_tests_properties(command-session PROPERTIES TIMEOUT 60)
qt_add_executable(JixelLightEditRecipeTests tests/EditRecipeTests.cpp)
target_link_libraries(JixelLightEditRecipeTests PRIVATE JixelLightCore Qt6::Test)
add_test(NAME edit-recipe COMMAND JixelLightEditRecipeTests -o edit-recipe-results.txt,txt -o -,txt)
set_tests_properties(edit-recipe PROPERTIES TIMEOUT 60)
set_target_properties(JixelLightEditRecipeTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
qt_add_executable(JixelLightProjectRelinkTests tests/ProjectRelinkTests.cpp)
target_link_libraries(JixelLightProjectRelinkTests PRIVATE JixelLightCore Qt6::Test)
add_test(NAME project-relink COMMAND JixelLightProjectRelinkTests -o project-relink-results.txt,txt -o -,txt)
set_tests_properties(project-relink PROPERTIES TIMEOUT 120)
set_target_properties(JixelLightProjectRelinkTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
qt_add_executable(JixelLightMissingSourceTests tests/MissingSourceTests.cpp)
target_link_libraries(JixelLightMissingSourceTests PRIVATE JixelLightCore Qt6::Test)
add_test(NAME missing-source COMMAND JixelLightMissingSourceTests -o missing-source-results.txt,txt -o -,txt)
set_tests_properties(missing-source PROPERTIES TIMEOUT 120)
set_target_properties(JixelLightMissingSourceTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
qt_add_executable(JixelLightBrushAdmissionTests tests/BrushAdmissionTests.cpp)
target_link_libraries(JixelLightBrushAdmissionTests PRIVATE JixelLightCore Qt6::Test)
add_test(NAME brush-admission COMMAND JixelLightBrushAdmissionTests -o brush-admission-results.txt,txt -o -,txt)
set_tests_properties(brush-admission PROPERTIES TIMEOUT 120)
set_target_properties(JixelLightBrushAdmissionTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
qt_add_executable(JixelLightBrushRoutingTests tests/BrushRoutingTests.cpp)
target_link_libraries(JixelLightBrushRoutingTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightBrushRoutingTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME brush-routing-cache COMMAND JixelLightBrushRoutingTests cpuPreviewCachePreservesIntentAndLeasesAcrossHitsEvictionAndRefusal -o brush-routing-cache-results.txt,txt -o -,txt)
add_test(NAME brush-routing-queue COMMAND JixelLightBrushRoutingTests queuedEnabledBrushExportsAllFormatsWithFrozenStateAndIcc -o brush-routing-queue-results.txt,txt -o -,txt)
add_test(NAME brush-routing-cancel COMMAND JixelLightBrushRoutingTests queueBudgetRefusalAndRealStripCancelPreserveTargetThenRecover -o brush-routing-cancel-results.txt,txt -o -,txt)
add_test(NAME brush-routing-native COMMAND JixelLightBrushRoutingTests queuedRealRawAsShotLoadsSourceAndMatchesIndependentTiffPixels -o brush-routing-native-results.txt,txt -o -,txt)
set_tests_properties(brush-routing-cache brush-routing-queue brush-routing-cancel brush-routing-native PROPERTIES TIMEOUT 120)
qt_add_executable(JixelLightBrushScopeRoutingTests tests/BrushScopeRoutingTests.cpp)
target_link_libraries(JixelLightBrushScopeRoutingTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightBrushScopeRoutingTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME brush-scopes-cache COMMAND JixelLightBrushScopeRoutingTests originalGeometryAndCompletedFrameReuse -o brush-scopes-cache-results.txt,txt -o -,txt)
add_test(NAME brush-scopes-cancel COMMAND JixelLightBrushScopeRoutingTests admissionFailuresAndCollectorCancellation -o brush-scopes-cancel-results.txt,txt -o -,txt)
add_test(NAME brush-scopes-native COMMAND JixelLightBrushScopeRoutingTests nativeSonyFullStatisticsReuse -o brush-scopes-native-results.txt,txt -o -,txt)
add_test(NAME brush-scopes-controller COMMAND JixelLightBrushScopeRoutingTests controllerFailuresRetryAndDiagnostic -o brush-scopes-controller-results.txt,txt -o -,txt)
add_test(NAME brush-scopes-scheduling COMMAND JixelLightBrushScopeRoutingTests controllerDefersExactPlotsWithoutBlockingFallback controllerStartsPendingExactAfterFinalFrame domainSelectionKeepsPixelsHistoryAndRevisionsAndRefusesStaleDomains domainChangesCancelBlockedExactWorkWithoutPublishingOldCounts linearPreviewBudgetRefusalRetriesAndSwitchingClearsDeferredWork domainSignalCanDestroyItsOwnerBeforeAnyNewWorkStarts scopePresentationCommandsAndLegacyAdaptersShareStrictAcceptedTraces controllerFullLinearPlotsReuseCompletedFramesWhileBrushBudgetIsBusy asynchronousLinearScopeDeliveryMayDestroyOwner previewCompletionSignalsMayCloseOrDestroyOwner -o brush-scopes-scheduling-results.txt,txt -o -,txt)
set_tests_properties(brush-scopes-cache brush-scopes-cancel brush-scopes-native brush-scopes-controller brush-scopes-scheduling PROPERTIES TIMEOUT 120)
qt_add_executable(JixelLightBrushLivePreviewTests tests/BrushLivePreviewTests.cpp)
target_link_libraries(JixelLightBrushLivePreviewTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightBrushLivePreviewTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME brush-live-queue COMMAND JixelLightBrushLivePreviewTests continuousQueueAndCancellationReleaseFutureLeases -o brush-live-queue-results.txt,txt -o -,txt)
add_test(NAME brush-live-display COMMAND JixelLightBrushLivePreviewTests versionedManagedDisplayPreservesCommittedPixelsAndLeases -o brush-live-display-results.txt,txt -o -,txt)
add_test(NAME brush-live-controller COMMAND JixelLightBrushLivePreviewTests controllerKeepsTransientStateOutOfHistoryScopesAndFinalSampling previewCoalescesBurstWithoutLosingTailOrLeakingAcrossGestures earlierFrameCannotCancelNewScheduledFrame scheduledPreviewStopsOnControllerShutdown budgetRetryAndScheduledInputRecoverTogether continuousEventLoopStreamPublishesBeforeRelease -o brush-live-controller-results.txt,txt -o -,txt)
set_tests_properties(brush-live-queue brush-live-display brush-live-controller PROPERTIES TIMEOUT 120)
qt_add_executable(JixelLightBrushPersistenceTests tests/BrushPersistenceTests.cpp)
qt_add_executable(JixelLightBrushGestureTests tests/BrushGestureTests.cpp)
target_link_libraries(JixelLightBrushGestureTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightBrushGestureTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME brush-gesture COMMAND JixelLightBrushGestureTests completedStrokeIsOneUndoAndProducesAllExportsAfterReopen geometryViewportCancellationAndSourceGuards invalidLongAndOverBudgetGesturesNeverPublishPartialState -o brush-gesture-results.txt,txt -o -,txt)
add_test(NAME brush-pressure-gesture COMMAND JixelLightBrushGestureTests pressureStartRejectsInvalidAndKeepsExactZeroWithoutHistoryLeak earlyCapacityWarningTracksRemainingDocumentAndResets zeroPressureOnlyDoesNotWarnAtOneRemainingDab warningAtOneRemainingDabPaint warningAtOneRemainingDabErase warningSamePointPulsePaint warningSamePointPulseErase warningDifferentPointRefusalPaint warningDifferentPointRefusalErase recoveryAppliesBoundedPrefixAsOneUndo recoveryClearsOnEditAndInvalidInput recoveryAtDocumentLimitPaint recoveryAtDocumentLimitErase recoveryDiscardPreservesRedoBranch stabilizedPressureInputKeepsBoundsPressureAndExplicitPixelDifference stabilizedPressureInputCommitsSinglePaintEraseAndHistory stabilizedPressureInputKeepsRawModeRefusalAndRecoveryLock stabilizedPressureInputRejectsInvalidAndStillHonorsCapacity onePixelSelectedPressureCommitsEightUniqueStreamsAndHistory onePixelChoiceRejectsInvalidAndKeepsRawHalfRecoveryDecisions onePixelCapturesChoicePerGestureAndResetsAnchor onePixelStillHonorsDocumentAndSingleStrokeCapacity -o brush-pressure-gesture-results.txt,txt -o -,txt)
set_tests_properties(brush-pressure-gesture PROPERTIES TIMEOUT 120)
add_test(NAME brush-gesture-sony COMMAND JixelLightBrushGestureTests realSonyGestureProducesExactFullStatistics -o brush-gesture-sony-results.txt,txt -o -,txt)
add_test(NAME brush-layer COMMAND JixelLightBrushGestureTests layerRemovalCommandsPreserveOtherLayersAndRoundTrip -o brush-layer-results.txt,txt -o -,txt)
add_test(NAME brush-capture COMMAND JixelLightBrushGestureTests clippedNativeCaptureSamplesTheVisibleStrokeAtDeviceScale -o brush-capture-results.txt,txt -o -,txt)
add_test(NAME brush-stroke-selection COMMAND JixelLightBrushGestureTests selectedStrokeUsesFrozenIdentityAndPreservesRemainingIntent -o brush-stroke-selection-results.txt,txt -o -,txt)
add_test(NAME brush-capacity COMMAND JixelLightBrushGestureTests capacityReadsTargetLimitsAndRefusesFullDocumentBeforeDrawing -o brush-capacity-results.txt,txt -o -,txt)
add_test(NAME brush-erase-gesture COMMAND JixelLightBrushGestureTests eraserGestureFreezesModeIsOneUndoAndPersistsExactPng -o brush-erase-gesture-results.txt,txt -o -,txt)
add_test(NAME brush-erase-refusal COMMAND JixelLightBrushGestureTests eraserModeRejectsMalformedDisabledAndFullTargetsBeforeTransient -o brush-erase-refusal-results.txt,txt -o -,txt)
set_tests_properties(brush-gesture brush-gesture-sony brush-layer brush-capture brush-stroke-selection brush-capacity brush-erase-gesture brush-erase-refusal PROPERTIES TIMEOUT 120)
add_dependencies(JixelLightBrushGestureTests JixelLightCli)
target_link_libraries(JixelLightBrushPersistenceTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightBrushPersistenceTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_dependencies(JixelLightBrushPersistenceTests JixelLightCli)
add_test(NAME brush-persistence COMMAND JixelLightBrushPersistenceTests activeSnapshotsAndStrictCommandsPersistAtomically controllerActiveProjectVersionsUndoScopesAndExports -o brush-persistence-results.txt,txt -o -,txt)
set_tests_properties(brush-persistence PROPERTIES TIMEOUT 120)
foreach(format png tif webp jpg)
    add_test(NAME brush-cli-sony-${format} COMMAND JixelLightBrushPersistenceTests realSonyCliEnabledBrushExport:${format} -o brush-cli-sony-${format}-results.txt,txt -o -,txt)
    set_tests_properties(brush-cli-sony-${format} PROPERTIES TIMEOUT 120)
endforeach()
qt_add_executable(JixelLightLargeBrushTests tests/LargeBrushTests.cpp)
target_link_libraries(JixelLightLargeBrushTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightLargeBrushTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME brush-large-float24 COMMAND JixelLightLargeBrushTests float24MpGeometryWholeAndPannedUnevenRoiMatchIndependentStrips -o brush-large-float24-results.txt,txt -o -,txt)
foreach(space srgb p3 prophoto)
    add_test(NAME brush-large-sony-${space} COMMAND JixelLightLargeBrushTests sony33MpWholeAndRoiMatchIndependentStrips:${space} -o brush-large-sony-${space}-results.txt,txt -o -,txt)
    set_tests_properties(brush-large-sony-${space} PROPERTIES TIMEOUT 120)
endforeach()
add_test(NAME brush-large-scopes COMMAND JixelLightLargeBrushTests sony33MpSharedFullScopesAndPlotsMatchIndependentEncodedFrame:paint -o brush-large-scopes-results.txt,txt -o -,txt)
set_tests_properties(brush-large-float24 brush-large-scopes PROPERTIES TIMEOUT 120)
foreach(format png16 tiff16 webp8)
    add_test(NAME brush-large-${format} COMMAND JixelLightLargeBrushTests sony33MpRasterExportsMatchIndependentPixelsAndIcc:${format} -o brush-large-${format}-results.txt,txt -o -,txt)
    set_tests_properties(brush-large-${format} PROPERTIES TIMEOUT 120)
endforeach()
add_test(NAME brush-large-jpeg COMMAND JixelLightLargeBrushTests sony33MpJpegCancellationPreservesTargetAndHealthyOutputMatchesCpu:paint -o brush-large-jpeg-results.txt,txt -o -,txt)
set_tests_properties(brush-large-jpeg PROPERTIES TIMEOUT 120)
# Test-only compiled copies observe completed geometry rows and brush tiles;
# Core/App never define the probes and retain the original pixel algorithms.
qt_add_executable(JixelLightPerformanceTests tests/PerformanceTests.cpp tests/LegacyPipeline.cpp core/pipeline/GeometryCorrections.cpp core/pipeline/ImagePipeline.cpp)
target_compile_definitions(JixelLightPerformanceTests PRIVATE JIXELLIGHT_GEOMETRY_TEST_PROGRESS=1 JIXELLIGHT_BRUSH_FRAME_TEST_PROGRESS=1)
target_link_libraries(JixelLightPerformanceTests PRIVATE JixelLightCore Qt6::Test)
add_test(NAME performance-correctness COMMAND JixelLightPerformanceTests -o performance-results.txt,txt -o -,txt)
qt_add_executable(JixelLightBenchmark tools/Benchmark.cpp tests/LegacyPipeline.cpp)
target_link_libraries(JixelLightBenchmark PRIVATE JixelLightCore)
qt_add_executable(JixelLightGpuTests tests/GpuTests.cpp)
target_link_libraries(JixelLightGpuTests PRIVATE JixelLightGpu Qt6::Test)
add_test(NAME gpu-correctness COMMAND JixelLightGpuTests -o gpu-results.txt,txt -o -,txt)
qt_add_executable(JixelLightBrushGpuPreviewTests tests/BrushGpuPreviewTests.cpp)
target_link_libraries(JixelLightBrushGpuPreviewTests PRIVATE JixelLightGpu Qt6::Test)
set_target_properties(JixelLightBrushGpuPreviewTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME brush-gpu-admission COMMAND JixelLightBrushGpuPreviewTests admissionRefusalDoesNotUploadOrPoisonEngineAndLeaseSurvivesUpload -o brush-gpu-admission-results.txt,txt -o -,txt)
add_test(NAME brush-gpu-hdr COMMAND JixelLightBrushGpuPreviewTests preparedHdrRetainsSignedRgbHiddenColorAndFractionalAlpha:paint -o brush-gpu-hdr-results.txt,txt -o -,txt)
foreach(view fit pan100)
    add_test(NAME brush-gpu-${view} COMMAND JixelLightBrushGpuPreviewTests realSonyPreparedPreview:${view} -o brush-gpu-${view}-results.txt,txt -o -,txt)
    set_tests_properties(brush-gpu-${view} PROPERTIES TIMEOUT 120)
endforeach()
set_tests_properties(brush-gpu-admission brush-gpu-hdr PROPERTIES TIMEOUT 120)

# Ordered Paint/Erase/Paint coverage retains every paint-only row and its gate.
foreach(format png tif webp jpg)
    add_test(NAME brush-erase-cli-sony-${format} COMMAND JixelLightBrushPersistenceTests realSonyCliEnabledBrushExport:erase-${format} -o brush-erase-cli-sony-${format}-results.txt,txt -o -,txt)
    set_tests_properties(brush-erase-cli-sony-${format} PROPERTIES TIMEOUT 120)
endforeach()
foreach(space srgb p3 prophoto)
    add_test(NAME brush-erase-large-sony-${space} COMMAND JixelLightLargeBrushTests sony33MpWholeAndRoiMatchIndependentStrips:erase-${space} -o brush-erase-large-sony-${space}-results.txt,txt -o -,txt)
    set_tests_properties(brush-erase-large-sony-${space} PROPERTIES TIMEOUT 120)
endforeach()
add_test(NAME brush-erase-large-scopes COMMAND JixelLightLargeBrushTests sony33MpSharedFullScopesAndPlotsMatchIndependentEncodedFrame:erase -o brush-erase-large-scopes-results.txt,txt -o -,txt)
foreach(format png16 tiff16 webp8)
    add_test(NAME brush-erase-large-${format} COMMAND JixelLightLargeBrushTests sony33MpRasterExportsMatchIndependentPixelsAndIcc:erase-${format} -o brush-erase-large-${format}-results.txt,txt -o -,txt)
    set_tests_properties(brush-erase-large-${format} PROPERTIES TIMEOUT 120)
endforeach()
add_test(NAME brush-erase-large-jpeg COMMAND JixelLightLargeBrushTests sony33MpJpegCancellationPreservesTargetAndHealthyOutputMatchesCpu:erase -o brush-erase-large-jpeg-results.txt,txt -o -,txt)
add_test(NAME brush-erase-gpu-hdr COMMAND JixelLightBrushGpuPreviewTests preparedHdrRetainsSignedRgbHiddenColorAndFractionalAlpha:erase -o brush-erase-gpu-hdr-results.txt,txt -o -,txt)
foreach(view fit pan100)
    add_test(NAME brush-erase-gpu-${view} COMMAND JixelLightBrushGpuPreviewTests realSonyPreparedPreview:erase-${view} -o brush-erase-gpu-${view}-results.txt,txt -o -,txt)
    set_tests_properties(brush-erase-gpu-${view} PROPERTIES TIMEOUT 120)
endforeach()
set_tests_properties(brush-erase-large-scopes brush-erase-large-jpeg brush-erase-gpu-hdr PROPERTIES TIMEOUT 120)

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
qt_add_executable(JixelLightCrossRawProbe tools/CrossRawProbe.cpp)
target_link_libraries(JixelLightCrossRawProbe PRIVATE JixelLightGpu)
set_target_properties(JixelLightCrossRawProbe PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
target_link_libraries(JixelLightRawRenderMatrix PRIVATE JixelLightGpu)
set_target_properties(JixelLightRawRenderMatrix PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)

# Experimental, opt-in only. Not installed in normal Windows/macOS packages.
qt_add_executable(JixelLightFloatRawTrial tools/FloatRawTrial.cpp)
target_link_libraries(JixelLightFloatRawTrial PRIVATE JixelLightCore)
set_target_properties(JixelLightFloatRawTrial PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
# Offline/private fixtures are opt-in and never installed or uploaded as images.
qt_add_executable(JixelLightOfflineRawProbe tools/OfflineRawProbe.cpp)
target_link_libraries(JixelLightOfflineRawProbe PRIVATE JixelLightCore)
set_target_properties(JixelLightOfflineRawProbe PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
include(cmake/QsbDepfile.cmake)
jixellight_escape_qsb_depfiles(${JIXELLIGHT_SHADER_SOURCES})
add_test(NAME qsb-depfile-spaces COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tests/TestQsbDepfile.py"
    --cmake "${CMAKE_COMMAND}" --qsb "$<TARGET_FILE:Qt6::qsb>"
    --generator "${CMAKE_GENERATOR}" --make-program "${CMAKE_MAKE_PROGRAM}"
    --report-directory "${CMAKE_BINARY_DIR}/qsb-depfile-test"
    --results-file "${CMAKE_BINARY_DIR}/qsb-depfile-results.txt")
set_tests_properties(qsb-depfile-spaces PROPERTIES TIMEOUT 120)
add_test(NAME sony-real-files COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tools/ValidateSonyFixtures.py $<TARGET_FILE:JixelLightSonyValidation>
    ${CMAKE_SOURCE_DIR}/tests/fixtures/sony-real-manifest.json ${CMAKE_BINARY_DIR}/sony-real-reports)
set_tests_properties(sony-real-files PROPERTIES TIMEOUT 540 SKIP_RETURN_CODE 77)
# The native GPU test consumes the actual profile fitted by the real-file test.
set_tests_properties(gpu-correctness PROPERTIES DEPENDS sony-real-files
    ENVIRONMENT "JIXELLIGHT_TEST_SONY_PROFILE=${CMAKE_BINARY_DIR}/sony-real-reports/dataset/empirical.jlook.json")

# Offline automation uses the same parameter registry and CPU reference engine.
qt_add_executable(JixelLightCli tools/Cli.cpp)
target_link_libraries(JixelLightCli PRIVATE JixelLightCore)
set_target_properties(JixelLightCli PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME cli-schema COMMAND JixelLightCli --schema)
add_test(NAME cross-raw-conformance COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/ValidateCrossRaw.py $<TARGET_FILE:JixelLightCrossRawProbe>
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/cross-raw.json @env
    ${CMAKE_CURRENT_BINARY_DIR}/cross-raw-conformance.json --expected-commit ${JIXELLIGHT_GIT_COMMIT})
set_tests_properties(cross-raw-conformance PROPERTIES TIMEOUT 180 ENVIRONMENT "JIXELLIGHT_CPU_THREADS=4")
add_test(NAME cli-export-safety COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tools/ValidateCli.py $<TARGET_FILE:JixelLightCli>)
set_tests_properties(cli-schema cli-export-safety PROPERTIES TIMEOUT 120)

# Controlled Qt input fixture; actual photo UI and device hardware have separate gates.
qt_add_executable(JixelLightBrushInputProbe tests/BrushPressureInputProbe.cpp app/BrushTabletInput.cpp app/BrushTabletInput.h)
target_include_directories(JixelLightBrushInputProbe PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(JixelLightBrushInputProbe PRIVATE Qt6::Quick Qt6::Qml Qt6::GuiPrivate)
target_compile_definitions(JixelLightBrushInputProbe PRIVATE JIXELLIGHT_GIT_COMMIT="${JIXELLIGHT_GIT_COMMIT}")
set_target_properties(JixelLightBrushInputProbe PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME brush-pressure-input COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tools/ValidateBrushPressureInput.py
    --probe $<TARGET_FILE:JixelLightBrushInputProbe>
    --fixture ${CMAKE_SOURCE_DIR}/tests/qml/BrushPressureInputFixture.qml
    --output-dir ${CMAKE_BINARY_DIR}/brush-pressure-input
    --result-file ${CMAKE_BINARY_DIR}/brush-pressure-input-results.txt
    --expected-source-commit ${JIXELLIGHT_GIT_COMMIT})
set_tests_properties(brush-pressure-input PROPERTIES TIMEOUT 120)

qt_add_executable(JixelLightBrushPressureExportVerifier tools/BrushPressureExportVerifier.cpp)
target_link_libraries(JixelLightBrushPressureExportVerifier PRIVATE JixelLightCore)
set_target_properties(JixelLightBrushPressureExportVerifier PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)

# Native working-output/monitor presentation regression. All existing tests stay.
qt_add_executable(JixelLightNativeBrushMonitorTests tests/NativeBrushMonitorTests.cpp)
target_link_libraries(JixelLightNativeBrushMonitorTests PRIVATE JixelLightGpu Qt6::Test Qt6::GuiPrivate)
if(APPLE)
    target_sources(JixelLightNativeBrushMonitorTests PRIVATE tests/MacMonitorProfileWitness.mm)
    target_link_libraries(JixelLightNativeBrushMonitorTests PRIVATE "-framework AppKit")
endif()
set_target_properties(JixelLightNativeBrushMonitorTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME native-brush-monitor COMMAND JixelLightNativeBrushMonitorTests -o native-brush-monitor-results.txt,txt -o -,txt)
set_tests_properties(native-brush-monitor PROPERTIES TIMEOUT 120
    ENVIRONMENT "JIXELLIGHT_NATIVE_MONITOR_REPORT=${CMAKE_BINARY_DIR}/native-brush-monitor.json")

# BEGIN XMP normal-controller native coverage; the original70 names remain unchanged.
qt_add_executable(JixelLightXmpControllerTests tests/XmpControllerTests.cpp)
target_link_libraries(JixelLightXmpControllerTests PRIVATE JixelLightCore Qt6::Test Qt6::Widgets Qt6::Quick Qt6::Qml Qt6::Sql Qt6::Concurrent)
set_target_properties(JixelLightXmpControllerTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME xmp-controller-merge COMMAND JixelLightXmpControllerTests -o xmp-controller-results.txt,txt -o -,txt)
if(WIN32)
    set(JIXELLIGHT_XMP_COMPONENT_QPA windows)
elseif(APPLE)
    set(JIXELLIGHT_XMP_COMPONENT_QPA cocoa)
else()
    set(JIXELLIGHT_XMP_COMPONENT_QPA offscreen)
endif()
set_tests_properties(xmp-controller-merge PROPERTIES TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=${JIXELLIGHT_XMP_COMPONENT_QPA};JIXEL_XMP_EVIDENCE_DIR=${CMAKE_BINARY_DIR}/xmp-controller-native;JIXELLIGHT_FORCE_CPU=1")
# END XMP normal-controller native coverage.

# BEGIN curation snapshot storage domains; preserve the original71 CTest order.
qt_add_executable(JixelLightCurationDomainTests tests/TestCurationDomain.cpp)
target_link_libraries(JixelLightCurationDomainTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightCurationDomainTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME curation-domain-snapshot COMMAND JixelLightCurationDomainTests -o curation-domain-results.txt,txt -o -,txt)

set(JIXELLIGHT_BRUSH_PREVIEW_TEST_RUNTIME "${CMAKE_CURRENT_BINARY_DIR}/brush-preview-admission-worker-owned")
file(MAKE_DIRECTORY "${JIXELLIGHT_BRUSH_PREVIEW_TEST_RUNTIME}")
qt_add_executable(JixelLightBrushPreviewAdmissionWorkerTests tests/BrushPreviewAdmissionWorkerTests.cpp)
target_link_libraries(JixelLightBrushPreviewAdmissionWorkerTests PRIVATE JixelLightCore Qt6::Test)
target_compile_features(JixelLightBrushPreviewAdmissionWorkerTests PRIVATE cxx_std_20)
set_target_properties(JixelLightBrushPreviewAdmissionWorkerTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME brush-preview-admission-worker COMMAND JixelLightBrushPreviewAdmissionWorkerTests -o brush-preview-admission-worker-results.txt,txt -o -,txt)
set_tests_properties(brush-preview-admission-worker PROPERTIES TIMEOUT 120)
if(WIN32)
    set_tests_properties(brush-preview-admission-worker PROPERTIES ENVIRONMENT "JIXELLIGHT_QA_OWNED_ROOT=${JIXELLIGHT_BRUSH_PREVIEW_TEST_RUNTIME};QT_QPA_PLATFORM=windows")
elseif(APPLE)
    set_tests_properties(brush-preview-admission-worker PROPERTIES ENVIRONMENT "JIXELLIGHT_QA_OWNED_ROOT=${JIXELLIGHT_BRUSH_PREVIEW_TEST_RUNTIME};QT_QPA_PLATFORM=cocoa")
else()
    set_tests_properties(brush-preview-admission-worker PROPERTIES ENVIRONMENT "JIXELLIGHT_QA_OWNED_ROOT=${JIXELLIGHT_BRUSH_PREVIEW_TEST_RUNTIME};QT_QPA_PLATFORM=offscreen")
endif()
set_tests_properties(curation-domain-snapshot PROPERTIES TIMEOUT 120)
# END curation snapshot storage domains.

# Normal planned CPU tile coverage; preserve original73 registrations.
set(JIXELLIGHT_BRUSH_TILE_RUNTIME "${CMAKE_CURRENT_BINARY_DIR}/brush-tile-lowerbound-owned")
file(MAKE_DIRECTORY "${JIXELLIGHT_BRUSH_TILE_RUNTIME}")
qt_add_executable(JixelLightBrushTileLowerBoundTests tests/BrushTileLowerBoundTests.cpp
    tests/brush_tile_oracle/BrushTileOriginalPipeline.cpp)
target_link_libraries(JixelLightBrushTileLowerBoundTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightBrushTileLowerBoundTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
if(MSVC)
    set_source_files_properties(tests/brush_tile_oracle/BrushTileOriginalPipeline.cpp PROPERTIES COMPILE_OPTIONS "/fp:strict")
else()
    set_source_files_properties(tests/brush_tile_oracle/BrushTileOriginalPipeline.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()
add_test(NAME brush-tile-lowerbounds COMMAND JixelLightBrushTileLowerBoundTests -o brush-tile-lowerbound-results.txt,txt -o -,txt)
set_tests_properties(brush-tile-lowerbounds PROPERTIES TIMEOUT 120
    ENVIRONMENT "JIXELLIGHT_CPU_THREADS=4;JIXEL_BRUSH_TILE_EVIDENCE_PATH=${JIXELLIGHT_BRUSH_TILE_RUNTIME}/brush-tile-lowerbound-results.json")

# Manual tangential geometry uses actual Core default arithmetic. The test-only
# row observation copy preserves Core/App cancellation and numerical bodies.
qt_add_executable(JixelLightTangentialGeometryTests tests/TangentialGeometryTests.cpp
    tests/tangential_oracle/LegacyGeometryState.cpp tests/tangential_oracle/LegacyGeometryCorrections.cpp
    core/pipeline/GeometryCorrections.cpp)
target_compile_definitions(JixelLightTangentialGeometryTests PRIVATE JIXELLIGHT_GEOMETRY_TEST_PROGRESS=1)
target_link_libraries(JixelLightTangentialGeometryTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightTangentialGeometryTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
set(JIXELLIGHT_TANGENTIAL_RUNTIME "${CMAKE_BINARY_DIR}/tangential-geometry-owned")
file(MAKE_DIRECTORY "${JIXELLIGHT_TANGENTIAL_RUNTIME}")
add_test(NAME tangential-geometry COMMAND JixelLightTangentialGeometryTests -o tangential-geometry-results.txt,txt -o -,txt)
set_tests_properties(tangential-geometry PROPERTIES TIMEOUT 120
    ENVIRONMENT "JIXELLIGHT_CPU_THREADS=4;JIXEL_TANGENTIAL_EVIDENCE_PATH=${JIXELLIGHT_TANGENTIAL_RUNTIME}/tangential-geometry-results.json")

# Tangential geometry output on actual compute and full plot references.
qt_add_executable(JixelLightTangentialGpuTests tests/TangentialGpuTests.cpp)
target_link_libraries(JixelLightTangentialGpuTests PRIVATE JixelLightGpu Qt6::Test)
set_target_properties(JixelLightTangentialGpuTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME tangential-gpu COMMAND JixelLightTangentialGpuTests -o tangential-gpu-results.txt,txt -o -,txt)
set_tests_properties(tangential-gpu PROPERTIES TIMEOUT 120 ENVIRONMENT "JIXELLIGHT_REQUIRE_GPU=1;JIXELLIGHT_TANGENTIAL_GPU_EVIDENCE=${CMAKE_BINARY_DIR}/tangential-gpu-owned")

qt_add_executable(JixelLightFilmGrainTests tests/FilmGrainTests.cpp)
target_link_libraries(JixelLightFilmGrainTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightFilmGrainTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME film-grain COMMAND JixelLightFilmGrainTests -o film-grain-results.txt,txt -o -,txt)
set_tests_properties(film-grain PROPERTIES TIMEOUT 120 ENVIRONMENT "JIXELLIGHT_CPU_THREADS=4;JIXELLIGHT_GRAIN_EVIDENCE=${CMAKE_BINARY_DIR}/grain-owned")

qt_add_executable(JixelLightSharpenTests tests/IndependentSharpenTests.cpp)
target_link_libraries(JixelLightSharpenTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightSharpenTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME independent-sharpen COMMAND JixelLightSharpenTests -o sharpen-results.txt,txt -o -,txt)
set_tests_properties(independent-sharpen PROPERTIES TIMEOUT 120 ENVIRONMENT "JIXELLIGHT_CPU_THREADS=4;JIXELLIGHT_SHARPEN_EVIDENCE=${CMAKE_BINARY_DIR}/sharpen-owned")

qt_add_executable(JixelLightDenoiseTests tests/SpatialDenoiseTests.cpp)
target_link_libraries(JixelLightDenoiseTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightDenoiseTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME spatial-denoise COMMAND JixelLightDenoiseTests -o denoise-results.txt,txt -o -,txt)
set_tests_properties(spatial-denoise PROPERTIES TIMEOUT 120 ENVIRONMENT "JIXELLIGHT_CPU_THREADS=4")

qt_add_executable(JixelLightDenoiseWorkflowTests tests/DenoiseWorkflowTests.cpp)
target_link_libraries(JixelLightDenoiseWorkflowTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightDenoiseWorkflowTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME denoise-workflows COMMAND JixelLightDenoiseWorkflowTests -o denoise-workflow-results.txt,txt -o -,txt)
set_tests_properties(denoise-workflows PROPERTIES TIMEOUT 120 ENVIRONMENT "JIXELLIGHT_CPU_THREADS=4;JIXELLIGHT_DENOISE_EVIDENCE=${CMAKE_BINARY_DIR}/denoise-owned")

qt_add_executable(JixelLightDenoiseCacheTests tests/DenoisePreviewCacheTests.cpp)
target_link_libraries(JixelLightDenoiseCacheTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightDenoiseCacheTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME denoise-preview-cache COMMAND JixelLightDenoiseCacheTests -o denoise-cache-results.txt,txt -o -,txt)
set_tests_properties(denoise-preview-cache PROPERTIES TIMEOUT 120 ENVIRONMENT "JIXELLIGHT_CPU_THREADS=4")

qt_add_executable(JixelLightClarityTests tests/SpatialClarityTests.cpp)
target_link_libraries(JixelLightClarityTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightClarityTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME spatial-clarity COMMAND JixelLightClarityTests -o clarity-results.txt,txt -o -,txt)
set_tests_properties(spatial-clarity PROPERTIES TIMEOUT 120 ENVIRONMENT "JIXELLIGHT_CPU_THREADS=4")

qt_add_executable(JixelLightClarityWorkflowTests tests/ClarityWorkflowTests.cpp)
target_link_libraries(JixelLightClarityWorkflowTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightClarityWorkflowTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME clarity-workflows COMMAND JixelLightClarityWorkflowTests -o clarity-workflow-results.txt,txt -o -,txt)
set_tests_properties(clarity-workflows PROPERTIES TIMEOUT 120 ENVIRONMENT "JIXELLIGHT_CPU_THREADS=4;JIXELLIGHT_CLARITY_EVIDENCE=${CMAKE_BINARY_DIR}/clarity-owned")

qt_add_executable(JixelLightClarityCacheTests tests/ClarityPreviewCacheTests.cpp)
target_link_libraries(JixelLightClarityCacheTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightClarityCacheTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME clarity-preview-cache COMMAND JixelLightClarityCacheTests -o clarity-cache-results.txt,txt -o -,txt)
set_tests_properties(clarity-preview-cache PROPERTIES TIMEOUT 120 ENVIRONMENT "JIXELLIGHT_CPU_THREADS=4")

qt_add_executable(JixelLightTextureTests tests/SpatialTextureTests.cpp)
target_link_libraries(JixelLightTextureTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightTextureTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME spatial-texture COMMAND JixelLightTextureTests -o texture-results.txt,txt -o -,txt)
set_tests_properties(spatial-texture PROPERTIES TIMEOUT 120 ENVIRONMENT "JIXELLIGHT_CPU_THREADS=4")

qt_add_executable(JixelLightTextureWorkflowTests tests/TextureWorkflowTests.cpp)
target_link_libraries(JixelLightTextureWorkflowTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightTextureWorkflowTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME texture-workflows COMMAND JixelLightTextureWorkflowTests -o texture-workflow-results.txt,txt -o -,txt)
set_tests_properties(texture-workflows PROPERTIES TIMEOUT 120 ENVIRONMENT "JIXELLIGHT_CPU_THREADS=4;JIXELLIGHT_TEXTURE_EVIDENCE=${CMAKE_BINARY_DIR}/texture-owned")

qt_add_executable(JixelLightTextureCacheTests tests/TexturePreviewCacheTests.cpp core/cache/RenderedPreviewCache.cpp)
target_compile_definitions(JixelLightTextureCacheTests PRIVATE JIXELLIGHT_PREVIEW_STAGE_TEST_PROGRESS=1)
target_link_libraries(JixelLightTextureCacheTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightTextureCacheTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME texture-preview-cache COMMAND JixelLightTextureCacheTests -o texture-cache-results.txt,txt -o -,txt)
set_tests_properties(texture-preview-cache PROPERTIES TIMEOUT 120 ENVIRONMENT "JIXELLIGHT_CPU_THREADS=4")

qt_add_executable(JixelLightDehazeTests tests/SpatialDehazeTests.cpp)
target_link_libraries(JixelLightDehazeTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightDehazeTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME spatial-dehaze COMMAND JixelLightDehazeTests -o dehaze-results.txt,txt -o -,txt)
set_tests_properties(spatial-dehaze PROPERTIES TIMEOUT 120 ENVIRONMENT "JIXELLIGHT_CPU_THREADS=4")

qt_add_executable(JixelLightDehazeWorkflowTests tests/DehazeWorkflowTests.cpp)
target_link_libraries(JixelLightDehazeWorkflowTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightDehazeWorkflowTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME dehaze-workflows COMMAND JixelLightDehazeWorkflowTests -o dehaze-workflow-results.txt,txt -o -,txt)
set_tests_properties(dehaze-workflows PROPERTIES TIMEOUT 120 ENVIRONMENT "JIXELLIGHT_CPU_THREADS=4;JIXELLIGHT_DEHAZE_EVIDENCE=${CMAKE_BINARY_DIR}/dehaze-owned")

qt_add_executable(JixelLightDehazeCacheTests tests/DehazePreviewCacheTests.cpp core/cache/RenderedPreviewCache.cpp)
target_compile_definitions(JixelLightDehazeCacheTests PRIVATE JIXELLIGHT_PREVIEW_STAGE_TEST_PROGRESS=1)
target_link_libraries(JixelLightDehazeCacheTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightDehazeCacheTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME dehaze-preview-cache COMMAND JixelLightDehazeCacheTests -o dehaze-cache-results.txt,txt -o -,txt)
set_tests_properties(dehaze-preview-cache PROPERTIES TIMEOUT 120 ENVIRONMENT "JIXELLIGHT_CPU_THREADS=4")

qt_add_executable(JixelLightPointColorTests tests/PointColorTests.cpp)
target_link_libraries(JixelLightPointColorTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightPointColorTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME point-color COMMAND JixelLightPointColorTests -o point-color-results.txt,txt -o -,txt)
set_tests_properties(point-color PROPERTIES TIMEOUT 120 ENVIRONMENT "JIXELLIGHT_CPU_THREADS=4")

qt_add_executable(JixelLightPointColorWorkflowTests tests/PointColorWorkflowTests.cpp)
target_link_libraries(JixelLightPointColorWorkflowTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightPointColorWorkflowTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME point-color-workflows COMMAND JixelLightPointColorWorkflowTests -o point-color-workflow-results.txt,txt -o -,txt)
set_tests_properties(point-color-workflows PROPERTIES TIMEOUT 120 ENVIRONMENT "JIXELLIGHT_CPU_THREADS=4;JIXELLIGHT_POINT_COLOR_EVIDENCE=${CMAKE_BINARY_DIR}/point-color-owned")

qt_add_executable(JixelLightLuminanceMaskTests tests/LuminanceRangeMaskTests.cpp)
target_link_libraries(JixelLightLuminanceMaskTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightLuminanceMaskTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME luminance-range-mask COMMAND JixelLightLuminanceMaskTests -o luminance-mask-results.txt,txt -o -,txt)
set_tests_properties(luminance-range-mask PROPERTIES TIMEOUT 120 ENVIRONMENT "JIXELLIGHT_CPU_THREADS=4")

qt_add_executable(JixelLightLuminanceMaskWorkflowTests tests/LuminanceMaskWorkflowTests.cpp)
target_link_libraries(JixelLightLuminanceMaskWorkflowTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightLuminanceMaskWorkflowTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME luminance-mask-workflows COMMAND JixelLightLuminanceMaskWorkflowTests -o luminance-mask-workflow-results.txt,txt -o -,txt)
set_tests_properties(luminance-mask-workflows PROPERTIES TIMEOUT 120 ENVIRONMENT "JIXELLIGHT_CPU_THREADS=4;JIXELLIGHT_LUMINANCE_MASK_EVIDENCE=${CMAKE_BINARY_DIR}/luminance-mask-owned")

qt_add_executable(JixelLightColorMaskWorkflowTests tests/ColorMaskWorkflowTests.cpp)
target_link_libraries(JixelLightColorMaskWorkflowTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightColorMaskWorkflowTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME color-range-mask-workflows COMMAND JixelLightColorMaskWorkflowTests -o color-mask-workflow-results.txt,txt -o -,txt)
set_tests_properties(color-range-mask-workflows PROPERTIES TIMEOUT 120 ENVIRONMENT "JIXELLIGHT_CPU_THREADS=4;JIXELLIGHT_COLOR_MASK_EVIDENCE=${CMAKE_BINARY_DIR}/color-mask-owned")

qt_add_executable(JixelLightSoftProofTests tests/SoftProofTests.cpp)
target_link_libraries(JixelLightSoftProofTests PRIVATE JixelLightCore Qt6::Test)
add_test(NAME soft-proof COMMAND JixelLightSoftProofTests -o soft-proof-results.txt,txt -o -,txt)
set_tests_properties(soft-proof PROPERTIES TIMEOUT 60)

qt_add_executable(JixelLightSoftProofWorkflowTests tests/SoftProofWorkflowTests.cpp)
target_link_libraries(JixelLightSoftProofWorkflowTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightSoftProofWorkflowTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME soft-proof-workflows COMMAND JixelLightSoftProofWorkflowTests -o soft-proof-workflow-results.txt,txt -o -,txt)
set_tests_properties(soft-proof-workflows PROPERTIES TIMEOUT 120 ENVIRONMENT "JIXELLIGHT_CPU_THREADS=4")


qt_add_executable(JixelLightBrushGuideTests tests/BrushGuideTests.cpp)
target_link_libraries(JixelLightBrushGuideTests PRIVATE Qt6::Qml Qt6::Test)
set_target_properties(JixelLightBrushGuideTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME brush-visual-guide COMMAND JixelLightBrushGuideTests -o brush-guide-results.txt,txt -o -,txt)
set_tests_properties(brush-visual-guide PROPERTIES TIMEOUT 60)


qt_add_executable(JixelLightMoveImportTests tests/MoveImportTests.cpp)
target_link_libraries(JixelLightMoveImportTests PRIVATE JixelLightCore Qt6::Test)
set_target_properties(JixelLightMoveImportTests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_test(NAME move-import COMMAND JixelLightMoveImportTests -o move-import-results.txt,txt -o -,txt)
set_tests_properties(move-import PROPERTIES TIMEOUT 120 ENVIRONMENT "JIXELLIGHT_CPU_THREADS=4;JIXELLIGHT_FORCE_CPU=1")

include(cmake/PrecompiledHeaders.cmake)
