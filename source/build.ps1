# Build script for Sonnet5chess24hrs.exe
# Compiler: GCC 15.2.0 (MinGW-w64, via scoop), g++
# Target: x86-64-v3 baseline (POPCNT/BMI1/BMI2/AVX2), fully static, no MinGW
# runtime DLL dependencies.
#
# Uses an explicit path to the scoop-installed g++ rather than relying on
# PATH resolution: on this dev machine, legacy Windows PowerShell (as
# opposed to pwsh or a bash shell) resolves an old bundled MinGW g++
# (Anaconda's, GCC 5.3.0 circa 2015 — predates C++20 support) ahead of the
# scoop one on PATH, which silently builds with the wrong compiler. If g++
# isn't at this exact path on your machine, edit $gxx below or just run the
# g++ command manually with your own toolchain's g++.
#
# Usage: powershell -File source/build.ps1   (run from the project root)
# Produces: final/Sonnet5chess24hrs.exe

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$gxx = "$env:USERPROFILE\scoop\apps\gcc\current\bin\g++.exe"
if (-not (Test-Path $gxx)) { $gxx = "g++" }

New-Item -ItemType Directory -Force -Path "source/build" | Out-Null

& $gxx -std=c++20 -O3 -flto -march=x86-64-v3 -static -Isource/src `
    source/src/bitboard.cpp source/src/board.cpp source/src/zobrist.cpp `
    source/src/movegen.cpp source/src/eval.cpp source/src/tt.cpp `
    source/src/search.cpp source/src/init.cpp source/src/uci.cpp `
    -o final/Sonnet5chess24hrs.exe -lpthread

if ($LASTEXITCODE -ne 0) { throw "Build failed" }
Write-Host "Built final/Sonnet5chess24hrs.exe"
