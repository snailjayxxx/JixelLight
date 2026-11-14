# CI consumes Release libraries with the existing static-library/dynamic-CRT ABI.
# The pinned stock settings are recorded in CI_RELEASE_DEPENDENCIES_STAGE_M147.md.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_PROVIDED_FORTRAN ON)
set(VCPKG_BUILD_TYPE release)
