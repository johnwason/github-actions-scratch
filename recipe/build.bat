@echo off

:: v0.1.0 declares cmake_minimum_required(VERSION 3.3), which CMake 4 rejects without a policy minimum
cmake -GNinja ^
  %CMAKE_ARGS% ^
  -DBUILD_SHARED_LIBS=ON ^
  -DBUILD_TESTING=OFF ^
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ^
  -S . ^
  -B build_dir
if %errorlevel% neq 0 exit 1
cmake --build build_dir --config Release
if %errorlevel% neq 0 exit 1
cmake --build build_dir --config Release --target install
if %errorlevel% neq 0 exit 1
