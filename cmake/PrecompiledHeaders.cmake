option(JIXELLIGHT_PRECOMPILE_HEADERS "Precompile common headers for Release C++ builds" ON)
if(NOT JIXELLIGHT_PRECOMPILE_HEADERS)
    return()
endif()

# Keep target-local artifacts: the App has private Qt requirements that differ
# from the core library. Objective-C++ and non-Release builds remain independent.
set(jixel_pch_cpp_release "$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>")
foreach(jixel_pch_target IN ITEMS JixelLightCore JixelLight)
    target_precompile_headers(${jixel_pch_target} PRIVATE
        "$<${jixel_pch_cpp_release}:${CMAKE_CURRENT_SOURCE_DIR}/cmake/QtBuildPch.h>")
endforeach()
# The independent oracle and production color kernels keep their own existing
# strict floating-point options, without consuming a differently compiled PCH.
set_source_files_properties(core/pipeline/ImagePipeline.cpp core/look/LookDetail.cpp
    tests/brush_tile_oracle/BrushTileOriginalPipeline.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

# REUSE_FROM requires an owner header in every generated configuration. A
# Release-only owner has no Debug/MinSizeRel/RelWithDebInfo header for VS to
# reference. Reuse is safe when Release is the only generated configuration,
# including a multi-config CI build explicitly restricted to Release. Keep the
# original path when any non-Release configuration can be generated.
get_property(jixel_pch_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
set(jixel_pch_release_only FALSE)
if(jixel_pch_multi_config)
    if(CMAKE_CONFIGURATION_TYPES STREQUAL "Release")
        set(jixel_pch_release_only TRUE)
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(jixel_pch_release_only TRUE)
endif()
if(NOT jixel_pch_release_only)
    message(STATUS "Release C++ PCH: core/app target-local; QtTest reuse disabled when non-Release configurations are available")
    return()
endif()
target_precompile_headers(JixelLightCoreTests PRIVATE
    "$<${jixel_pch_cpp_release}:${CMAKE_CURRENT_SOURCE_DIR}/cmake/QtTestPch.h>")

# Reuse only for matching test targets, including their direct usage requirements,
# local flags/definitions and QtTest source/build-directory definitions. Targets
# carrying progress-probe macros or additional Qt libraries build independently.
get_target_property(jixel_pch_owner_links JixelLightCoreTests LINK_LIBRARIES)
get_property(jixel_pch_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
set(jixel_pch_reused 0)
foreach(jixel_pch_target IN LISTS jixel_pch_targets)
    if(jixel_pch_target STREQUAL "JixelLightCoreTests")
        continue()
    endif()
    get_target_property(jixel_pch_type ${jixel_pch_target} TYPE)
    get_target_property(jixel_pch_links ${jixel_pch_target} LINK_LIBRARIES)
    if(NOT jixel_pch_type STREQUAL "EXECUTABLE" OR NOT jixel_pch_links STREQUAL jixel_pch_owner_links)
        continue()
    endif()
    set(jixel_pch_match TRUE)
    # MOC include directories are target-specific; no project headers are in
    # the shared QtTest PCH. Its Qt package/link requirements must match exactly.
    foreach(jixel_pch_property IN ITEMS COMPILE_OPTIONS COMPILE_DEFINITIONS COMPILE_FLAGS COMPILE_FEATURES
            CXX_STANDARD CXX_STANDARD_REQUIRED CXX_EXTENSIONS CXX_VISIBILITY_PRESET VISIBILITY_INLINES_HIDDEN
            POSITION_INDEPENDENT_CODE MSVC_RUNTIME_LIBRARY WIN32_EXECUTABLE MACOSX_BUNDLE
            INTERPROCEDURAL_OPTIMIZATION INTERPROCEDURAL_OPTIMIZATION_RELEASE SOURCE_DIR BINARY_DIR)
        get_target_property(jixel_pch_owner_value JixelLightCoreTests ${jixel_pch_property})
        get_target_property(jixel_pch_value ${jixel_pch_target} ${jixel_pch_property})
        if(NOT jixel_pch_owner_value)
            set(jixel_pch_owner_value "")
        endif()
        if(NOT jixel_pch_value)
            set(jixel_pch_value "")
        endif()
        if(NOT jixel_pch_value STREQUAL jixel_pch_owner_value)
            set(jixel_pch_match FALSE)
        endif()
    endforeach()
    if(jixel_pch_match)
        target_precompile_headers(${jixel_pch_target} REUSE_FROM JixelLightCoreTests)
        math(EXPR jixel_pch_reused "${jixel_pch_reused}+1")
    endif()
endforeach()
message(STATUS "Release C++ PCH: core/app target-local; ${jixel_pch_reused} matching QtTest targets reuse CoreTests")
