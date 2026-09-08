@echo off
REM Gateway OTA Worker 调试版本编译脚本 (Windows)

echo === Building Gateway OTA Worker (Debug) ===

REM 清理旧的build目录
set BUILD_DIR=build-debug
if exist %BUILD_DIR% (
    echo Cleaning old build directory...
    rmdir /s /q %BUILD_DIR%
)

REM 创建build目录
mkdir %BUILD_DIR%
cd %BUILD_DIR%

REM 配置CMake（Debug模式）
echo Configuring CMake (Debug mode)...
cmake .. ^
    -DCMAKE_BUILD_TYPE=Debug ^
    -DENABLE_SWUPDATE_BRIDGE=ON ^
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

REM 编译
echo Building...
cmake --build . --config Debug -j

REM 显示生成的可执行文件
echo.
echo === Build Complete ===
echo Debug binaries:
dir /b mcu-updater* 2>nul
dir /b token-signer-daemon* 2>nul

echo.
echo To deploy to device:
echo   adb push mcu-updater /tmp/mcu-updater-debug
echo.
echo To debug remotely:
echo   On device: gdbserver :1234 /tmp/mcu-updater-debug
echo   On PC: gdb-multiarch mcu-updater
echo          (gdb) target remote 10.112.4.229:1234

cd ..
