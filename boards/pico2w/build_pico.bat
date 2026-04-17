@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul 2>&1
set PATH=C:\arm-gnu-toolchain\bin;%PATH%
set PICO_SDK_PATH=C:\source\pico-sdk
if exist "C:\source\picocompress\boards\pico2w\build" rmdir /s /q "C:\source\picocompress\boards\pico2w\build"
mkdir "C:\source\picocompress\boards\pico2w\build"
cd /d "C:\source\picocompress\boards\pico2w\build"
"C:\Program Files\CMake\bin\cmake.exe" -G "Ninja" -DCMAKE_C_COMPILER=C:\arm-gnu-toolchain\bin\arm-none-eabi-gcc.exe -DCMAKE_CXX_COMPILER=C:\arm-gnu-toolchain\bin\arm-none-eabi-g++.exe -DCMAKE_ASM_COMPILER=C:\arm-gnu-toolchain\bin\arm-none-eabi-gcc.exe -DPICO_BOARD=pico2_w ..
if errorlevel 1 (echo CMAKE CONFIGURE FAILED & exit /b 1)
"C:\Program Files\CMake\bin\cmake.exe" --build . --parallel
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
echo BUILD SUCCEEDED
dir /b *.uf2
