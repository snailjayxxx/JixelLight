# M147: Release-only native dependencies for desktop CI

Status at source preparation, 2026-11-14: not yet compiled or qualified. Parent
3572fa1 has completed local106/all100/all33/all18 plus its own Linux/macOS gates;
its Windows job remains in installed-package validation. New-source acceptance
requires the complete independent gates below.

## Observed constraint

CI443's original Windows job132188251328 passes the mandatory three-test preflight
in225.35s and full106 in2476.12s, then reaches the fixed120-minute job limit during
the final installed GPU Move-import check. The partial artifact contains that
App's passed3680ms native report, while the outer result, final installed88
manifest, Windows artifact and strict delivery receipt are absent. Original log
SHA256 is `9bee1123a287d1e1d8d992d499d15c8682f59b556d07e92b5099419dfff679b5`.
Provider annotations confirm the two-hour maximum. This original cancellation
stays cancelled; native Windows-only retry132243922876 is independently observed.

That job's559s Configure step compiles both `-dbg` and `-rel` variants of native
ports, including LibRaw and Exiv2. Desktop CMake configure, application build,
CTest and deployed-package checks all use Release. The duplicated dependency
configuration is avoidable work inside an already bounded product job.

## Configuration change

Both product and base-cache workflows select dedicated `x64-windows-ci-release`
and `arm64-osx-ci-release` triplets. They retain the existing architecture,
dynamic CRT, static-library preference, Windows Fortran setting and macOS Darwin
system, and set the documented `VCPKG_BUILD_TYPE=release`. General developer
triplets keep their existing Debug/Release behavior. Existing cache keys already
include the selected triplet and complete `triplets/**` hash, so both workflows
use the same new ABI-separated cache identity and normal vcpkg compiler checks.

The setting is described in [Microsoft's triplet documentation](https://learn.microsoft.com/en-us/vcpkg/users/triplets#vcpkg_build_type).
Windows baseline values were checked against the repository's pinned vcpkg
revision [04a9d8e5212d01ee1dd9478eadd9caade4f8b0d4 stock triplet](https://github.com/microsoft/vcpkg/blob/04a9d8e5212d01ee1dd9478eadd9caade4f8b0d4/triplets/x64-windows-static-md.cmake):
Git blob `f2e9781a033ebec593722cc6dff8713122139451`, UTF-8 SHA256
`583a1b57956affe6c3daa5f6eea37867cf663792950ec52e0b91adecb8170c9c`.
The macOS baseline is the existing general-purpose triplet in this repository.

## Required independent verification

- Execute each new triplet through CMake and verify baseline ABI/platform values
  plus Release-only dependency selection. Compare both workflows' selected
  triplets/cache identities and retain their original deadlines.
- Before compilation, Python283 and source equivalence to3572fa1 for application,
  core, shaders, UI, tools, tests and CMake registry. Registry stays106/100/18.
- Fresh native Release, real-Sony/legacyGPU82/native8 preflight,336 actual
  GPU/reference cases and18 admission policies. RGB16 bound40, exact alpha and
  1024-bin histograms stay mandatory.
- Targeted15, complete106/all100, all33 local contexts and all18 frozen runtime
  identities. The new build is stored in a fresh NAS directory because local
  storage is below the existing1GiB fresh-build threshold. Completed output
  retention copies preserve original files already on NAS.
- Own Linux/macOS/Windows terminal originals and matching trees, complete
  installed88 on both desktops and strict artifact delivery. Windows120min,
  macOS180min, Linux60min and base-cache20min remain. Measured time savings can
  only be reported after actual new-source CI execution.

## Camera evidence and remaining scope

Private compatibility checks on the separately accepted DDE build now cover two
distinct EXIF/LibRaw-identified ILCE-7RM6 captures, pre-demosaic U16 audits, actual
Cocoa CPU/Metal normal-App flows and full30,670,848-pixel PNG16 export with all4608
rows independently verified. Their files, identifying metadata and detailed
receipts remain private. They qualify those compatibility checks on DDE and do
not substitute for this checkpoint's own source/build gates.

Independent photographic color calibration, calibrated native float RAW,
per-packet native live brushes and physical device/printer evidence remain
open. Main and protected PR15 retain their independent review/acceptance path.
