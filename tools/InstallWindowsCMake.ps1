$ErrorActionPreference = 'Stop'
$cmakeRoot = Join-Path $env:RUNNER_TEMP 'jixellight-cmake-4.4.4'
python -m pip install --disable-pip-version-check --only-binary=:all: --no-deps --require-hashes --target $cmakeRoot -r "$PSScriptRoot/../.github/windows-cmake-requirements.txt"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$cmakeBin = Join-Path $cmakeRoot 'cmake/data/bin'
& "$cmakeBin/cmake.exe" --version
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$cmakeBin | Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append
