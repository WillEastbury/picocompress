@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul 2>&1
set PATH=C:\arm-gnu-toolchain\bin;%PATH%
set PICO_SDK_PATH=C:\source\pico-sdk
cd /d "C:\source\picocompress\boards\pico2w\build"
"C:\Program Files\CMake\bin\cmake.exe" --build . --parallel 2>&1
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
echo BUILD SUCCEEDED
