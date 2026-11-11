add_library(JixelLightNativeBrushSource STATIC
    NativeBrushSource.cpp NativeBrushUploadAdmission.cpp NativeBrushUpload.cpp NativeBrushSourceJob.cpp
    NativeBrushFrame.cpp NativeBrushFrames.cpp NativeBrushHistogram.cpp NativeBrushDiagnostics.cpp)
target_include_directories(JixelLightNativeBrushSource PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
target_link_libraries(JixelLightNativeBrushSource PUBLIC JixelLightCore Qt6::Core Qt6::Gui Qt6::Concurrent PRIVATE Qt6::GuiPrivate)
target_compile_features(JixelLightNativeBrushSource PUBLIC cxx_std_20)
